#include "record_display.h"

#include <android/native_window.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <sys/system_properties.h>
#include <unordered_map>

namespace record_display {
namespace {
// android::sp<T> has a non-trivial destructor and is returned indirectly by
// the C++ ABI. The helper retains these references until process exit. They
// are not constructed or destroyed by the overlay's different libc++ build.
struct Sp { void* pointer = nullptr; ~Sp() {} };
struct IdVector { uint64_t* begin = nullptr; uint64_t* end = nullptr; uint64_t* capacity = nullptr; ~IdVector() {} };
struct Rect { int32_t left, top, right, bottom; };
struct DisplayStateAbi { uint32_t stack; int32_t rotation, width, height; uint8_t reserved[128]; };
struct DisplayInfoAbi {
    uint32_t width, height; float xdpi, ydpi, fps, density; uint8_t rotation; bool secure;
    int64_t app_vsync, present_deadline; uint32_t viewport_w, viewport_h; uint8_t reserved[128];
};

// Android 9 is the sole version with the Transaction API but without the
// public ASurfaceTransaction_create factory (added in 10). Its inline default
// constructor is not exported. The platform copy constructor *does* construct
// every member, so feed it a correctly laid out empty source. This is used only
// on API 28/64-bit and guarded by an emulator ABI smoke test.
struct EmptySortedVector28 {
    void* vtable = nullptr;
    void* storage = nullptr;
    size_t count = 0;
    uint32_t flags = 0;
    size_t item_size = 72; // sizeof(android::DisplayState) in Android 9/64-bit.
};
struct EmptyTransaction28 {
    void* vtable = nullptr;
    std::unordered_map<void*, void*> composer_states;
    EmptySortedVector28 display_states;
    uint32_t force_sync = 0, nest_count = 0;
    bool animation = false, early_wakeup = false;
    int32_t status = 0;
};
static_assert(sizeof(void*) == 8, "the recorder helper supports 64-bit Android only");
static_assert(sizeof(EmptySortedVector28) == 40, "unexpected Android vector layout");

template <typename T> T sym(void* library, const char* name) {
    return reinterpret_cast<T>(library ? dlsym(library, name) : nullptr);
}
} // namespace

struct Display::Impl {
    int sdk = 0;
    void* gui = nullptr;
    void* utils = nullptr;
    void* android = nullptr;
    std::string why;
    Sp physical, display, producer, mirror, client;
    uint64_t physical_id = 0;
    uint32_t mirror_stack = 0;

    Sp (*get_builtin)(int) = nullptr;
    Sp (*get_internal)() = nullptr;
    IdVector (*get_ids)() = nullptr;
    Sp (*get_token)(uint64_t) = nullptr;
    int (*get_state)(const Sp&, DisplayStateAbi*) = nullptr;
    int (*get_info)(const Sp&, DisplayInfoAbi*) = nullptr;
    void (*string_init)(void*, const char*) = nullptr;
    void (*string_destroy)(void*) = nullptr;
    Sp (*create_old)(const void*, bool) = nullptr;
    Sp (*create_old_refresh)(const void*, bool, float) = nullptr;
    Sp (*create_new)(const std::string&, bool, const std::string&, float) = nullptr;
    Sp (*create_new_power)(const std::string&, bool, bool, const std::string&, float) = nullptr;
    void (*destroy_old)(const Sp&) = nullptr;
    int (*destroy_new)(const Sp&) = nullptr;
    Sp (*get_producer)(void*) = nullptr;
    Sp (*get_default)() = nullptr;
    Sp (*mirror_display)(void*, uint64_t) = nullptr;
    void* (*create_transaction)() = nullptr;
    void (*delete_transaction)(void*) = nullptr;
    void (*copy_transaction)(void*, const void*) = nullptr;
    int (*apply_one)(void*, bool) = nullptr;
    int (*apply_two)(void*, bool, bool) = nullptr;
    void (*open_global)() = nullptr;
    void (*close_global)(bool) = nullptr;
    void (*old_surface)(const Sp&, Sp) = nullptr;
    void (*old_stack)(const Sp&, uint32_t) = nullptr;
    void (*old_projection)(const Sp&, uint32_t, const Rect&, const Rect&) = nullptr;
    void (*tx_surface)(void*, const Sp&, const Sp&) = nullptr;
    void (*tx_stack)(void*, const Sp&, uint32_t) = nullptr;
    void (*tx_projection)(void*, const Sp&, uint32_t, const Rect&, const Rect&) = nullptr;
    void (*tx_layer_stack)(void*, const Sp&, uint32_t) = nullptr;

    bool fail(const char* text) { if (why.empty()) why = text; return false; }
    bool initialize() {
        char value[PROP_VALUE_MAX]{};
        if (__system_property_get("ro.build.version.sdk", value) <= 0) return fail("cannot read Android API level");
        sdk = std::atoi(value);
        if (sdk < 26 || sdk > 36) return fail("Android version is not supported by this recorder");
        gui = dlopen("/system/lib64/libgui.so", RTLD_NOW | RTLD_LOCAL);
        utils = dlopen("/system/lib64/libutils.so", RTLD_NOW | RTLD_LOCAL);
        android = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!gui || !utils || !android) return fail("cannot load Android display libraries");
#define GET(field, lib, name) field = sym<decltype(field)>(lib, name)
        GET(get_builtin, gui, "_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi");
        GET(get_internal, gui, "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
        GET(get_ids, gui, "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
        GET(get_token, gui, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
        if (!get_token) GET(get_token, gui, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenEm");
        GET(get_state, gui, "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE");
        GET(get_info, gui, "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_11DisplayInfoE");
        GET(string_init, utils, "_ZN7android7String8C2EPKc");
        GET(string_destroy, utils, "_ZN7android7String8D2Ev");
        GET(create_old, gui, "_ZN7android21SurfaceComposerClient13createDisplayERKNS_7String8Eb");
        GET(create_old_refresh, gui, "_ZN7android21SurfaceComposerClient13createDisplayERKNS_7String8Ebf");
        GET(create_new, gui, "_ZN7android21SurfaceComposerClient20createVirtualDisplayERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEbS9_f");
        GET(create_new_power, gui, "_ZN7android21SurfaceComposerClient20createVirtualDisplayERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEbbS9_f");
        GET(destroy_old, gui, "_ZN7android21SurfaceComposerClient14destroyDisplayERKNS_2spINS_7IBinderEEE");
        GET(destroy_new, gui, "_ZN7android21SurfaceComposerClient21destroyVirtualDisplayERKNS_2spINS_7IBinderEEE");
        GET(get_producer, gui, "_ZNK7android7Surface25getIGraphicBufferProducerEv");
        GET(get_default, gui, "_ZN7android21SurfaceComposerClient10getDefaultEv");
        GET(mirror_display, gui, "_ZN7android21SurfaceComposerClient13mirrorDisplayENS_9DisplayIdE");
        GET(create_transaction, android, "ASurfaceTransaction_create");
        GET(delete_transaction, android, "ASurfaceTransaction_delete");
        GET(copy_transaction, gui, "_ZN7android21SurfaceComposerClient11TransactionC2ERKS1_");
        GET(apply_one, gui, "_ZN7android21SurfaceComposerClient11Transaction5applyEb");
        GET(apply_two, gui, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
        GET(open_global, gui, "_ZN7android21SurfaceComposerClient21openGlobalTransactionEv");
        GET(close_global, gui, "_ZN7android21SurfaceComposerClient22closeGlobalTransactionEb");
        GET(old_surface, gui, "_ZN7android21SurfaceComposerClient17setDisplaySurfaceERKNS_2spINS_7IBinderEEENS1_INS_22IGraphicBufferProducerEEE");
        GET(old_stack, gui, "_ZN7android21SurfaceComposerClient20setDisplayLayerStackERKNS_2spINS_7IBinderEEEj");
        GET(old_projection, gui, "_ZN7android21SurfaceComposerClient20setDisplayProjectionERKNS_2spINS_7IBinderEEEjRKNS_4RectES8_");
        GET(tx_surface, gui, "_ZN7android21SurfaceComposerClient11Transaction17setDisplaySurfaceERKNS_2spINS_7IBinderEEERKNS2_INS_22IGraphicBufferProducerEEE");
        GET(tx_stack, gui, "_ZN7android21SurfaceComposerClient11Transaction20setDisplayLayerStackERKNS_2spINS_7IBinderEEENS_2ui10LayerStackE");
        if (!tx_stack) GET(tx_stack, gui, "_ZN7android21SurfaceComposerClient11Transaction20setDisplayLayerStackERKNS_2spINS_7IBinderEEEj");
        GET(tx_projection, gui, "_ZN7android21SurfaceComposerClient11Transaction20setDisplayProjectionERKNS_2spINS_7IBinderEEENS_2ui8RotationERKNS_4RectESB_");
        if (!tx_projection) GET(tx_projection, gui, "_ZN7android21SurfaceComposerClient11Transaction20setDisplayProjectionERKNS_2spINS_7IBinderEEEjRKNS_4RectES8_");
        GET(tx_layer_stack, gui, "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE");
        if (!tx_layer_stack) GET(tx_layer_stack, gui, "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEEj");
#undef GET
        if (!get_producer) return fail("Surface buffer producer API unavailable");
        if (sdk <= 28) {
            if (!get_builtin || !get_info) return fail("legacy physical display API unavailable");
            physical = get_builtin(0);
        } else if (sdk < 34 && get_internal) {
            physical = get_internal();
        } else {
            if (!get_ids || !get_token) return fail("physical display ID API unavailable");
            const IdVector ids = get_ids();
            if (!ids.begin || ids.begin == ids.end) return fail("no physical display found");
            physical_id = ids.begin[0]; physical = get_token(physical_id);
        }
        if (!physical.pointer) return fail("cannot obtain physical display token");
        if (sdk < 30 && !get_info) return fail("display info API unavailable");
        if (sdk >= 30 && !get_state) return fail("display state API unavailable");
        if (sdk >= 35) {
            if ((!create_new && !create_new_power) || !destroy_new) return fail("virtual display API unavailable");
        } else if ((!create_old && !create_old_refresh) || !destroy_old || !string_init || !string_destroy) {
            return fail("legacy virtual display API unavailable");
        }
        if (sdk == 26 || sdk == 27) {
            if (!open_global || !close_global || !old_surface || !old_stack || !old_projection)
                return fail("legacy display transaction API unavailable");
        } else {
            if ((!create_transaction || !delete_transaction) && (sdk != 28 || !copy_transaction))
                return fail("display transaction factory unavailable");
            if ((!apply_one && !apply_two) || !tx_surface || !tx_stack || !tx_projection)
                return fail("display transaction API unavailable");
        }
        if (sdk >= 34 && (!get_default || !mirror_display || !tx_layer_stack))
            return fail("display mirror API unavailable");
        return true;
    }

    bool state(State& out) {
        if (!physical.pointer) return fail("physical display not initialized");
        if (sdk >= 30) {
            DisplayStateAbi state{};
            if (get_state(physical, &state) != 0) return fail("cannot read physical display state");
            out = {state.width, state.height, state.rotation, state.stack};
        } else {
            DisplayInfoAbi info{};
            if (get_info(physical, &info) != 0) return fail("cannot read physical display info");
            int width = static_cast<int>(info.width), height = static_cast<int>(info.height);
            if (info.rotation == 1 || info.rotation == 3) std::swap(width, height);
            out = {width, height, info.rotation, 0};
        }
        if (out.width <= 0 || out.height <= 0 || out.width > 32768 || out.height > 32768)
            return fail("invalid physical display dimensions");
        return true;
    }

    void* transaction() {
        if (create_transaction) return create_transaction();
        if (sdk != 28 || !copy_transaction) return nullptr;
        EmptyTransaction28 empty;
        void* storage = ::operator new(4096, std::nothrow);
        if (!storage) return nullptr;
        copy_transaction(storage, &empty);
        // Android 9 does not export a destructor consistently. The private
        // helper is short-lived; retaining this transaction is safer than
        // calling a destructor from a different C++ runtime.
        return storage;
    }
    bool apply(void* tx) {
        const int result = apply_two ? apply_two(tx, true, false) : apply_one(tx, true);
        if (delete_transaction) delete_transaction(tx);
        return result == 0 || fail("SurfaceFlinger rejected display transaction");
    }
    bool configure(const State& st, int width, int height, bool first) {
        if (width <= 0 || height <= 0 || !display.pointer) return fail("invalid virtual display dimensions");
        const Rect source{0, 0, st.width, st.height};
        int out_w = width, out_h = static_cast<int>(int64_t(width) * st.height / st.width);
        if (out_h > height) { out_h = height; out_w = static_cast<int>(int64_t(height) * st.width / st.height); }
        const int x = (width - out_w) / 2, y = (height - out_h) / 2;
        const Rect target{x, y, x + out_w, y + out_h};
        const uint32_t stack = sdk >= 34 ? mirror_stack : st.layer_stack;
        if (sdk <= 27) {
            open_global();
            if (first) { old_surface(display, producer); old_stack(display, stack); }
            old_projection(display, 0, source, target);
            close_global(true);
            return true;
        }
        void* tx = transaction();
        if (!tx) return fail("cannot allocate display transaction");
        if (first) {
            tx_surface(tx, display, producer);
            tx_stack(tx, display, stack);
            if (sdk >= 34) tx_layer_stack(tx, mirror, stack);
        }
        tx_projection(tx, display, 0, source, target);
        return apply(tx);
    }

    bool attach(ANativeWindow* window, const State& st, int width, int height) {
        if (!window) return fail("encoder input surface is null");
        // In android::ANativeObjectBase<ANativeWindow, Surface, RefBase>,
        // RefBase is the primary polymorphic base (vptr + weakref pointer),
        // followed by the non-polymorphic ANativeWindow subobject.
        auto* surface = reinterpret_cast<uint8_t*>(window) - 2 * sizeof(void*);
        producer = get_producer(surface);
        if (!producer.pointer) return fail("cannot obtain encoder buffer producer");
        if (sdk >= 35) {
            const std::string name("ScreenRecorder"), unique;
            display = create_new_power ? create_new_power(name, false, true, unique, 0.0f)
                                       : create_new(name, false, unique, 0.0f);
        } else {
            alignas(16) uint8_t name[64]{};
            string_init(name, "ScreenRecorder");
            display = create_old_refresh ? create_old_refresh(name, false, 0.0f) : create_old(name, false);
            string_destroy(name);
        }
        if (!display.pointer) return fail("cannot create virtual display");
        if (sdk >= 34) {
            client = get_default();
            if (!client.pointer) return fail("cannot obtain display composer");
            mirror = mirror_display(client.pointer, physical_id);
            if (!mirror.pointer) return fail("cannot mirror physical display");
            mirror_stack = static_cast<uint32_t>(std::rand()) & 0x7fffffffU;
            if (mirror_stack == 0 || mirror_stack == st.layer_stack) mirror_stack = 0x6abc1234U;
        }
        return configure(st, width, height, true);
    }
    void detach() {
        if (!display.pointer) return;
        if (destroy_new) (void)destroy_new(display);
        else if (destroy_old) destroy_old(display);
        display.pointer = nullptr;
    }
};

Display::Display() : impl_(new Impl) {}
Display::~Display() { impl_->detach(); }
bool Display::initialize() { return impl_->initialize(); }
bool Display::state(State& state) { return impl_->state(state); }
bool Display::attach(ANativeWindow* surface, const State& state, int width, int height) { return impl_->attach(surface, state, width, height); }
bool Display::update(const State& state, int width, int height) { return impl_->configure(state, width, height, false); }
void Display::detach() { impl_->detach(); }
const char* Display::error() const { return impl_->why.c_str(); }
} // namespace record_display
