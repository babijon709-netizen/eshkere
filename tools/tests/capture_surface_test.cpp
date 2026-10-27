// Exercise the production SurfaceFlinger ABI wrapper with fake libgui symbols.
// This verifies requested flags and fallbacks, not an Android compositor.
#include "native_surface/ANativeWindowCreator.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using anwc::ANativeWindowCreator;
using namespace anwc::detail;

static int version = 12;
static int sdk = 31;
static bool codename = false;
static std::string missing;
static int creates = 0, flags_calls = 0, metadata_calls = 0;
static int layer_calls = 0, trusted_calls = 0, apply_calls = 0;
static bool fail_apply = false, last_sync = false, last_oneway = false;
static uint32_t created_flags = 0, last_flags = 0, last_mask = 0;
static int32_t window_type = 0, metadata_key = 0, metadata_value = 0;
static ANativeWindow windows[4]{};
static int controls[4]{};
static int active = 0;

extern "C" int __system_property_get(const char* name, char* value) {
    if (std::strcmp(name, "ro.build.version.release") == 0) {
        std::sprintf(value, codename ? "Preview" : "%d", version);
    } else if (std::strcmp(name, "ro.build.version.sdk") == 0) {
        std::sprintf(value, "%d", sdk);
    } else {
        return 0;
    }
    return static_cast<int>(std::strlen(value));
}

static void* fake_open(const char*, int) { return reinterpret_cast<void*>(1); }
static int fake_close(void*) { return 0; }
static void metadata_ctor(void*) {}
static void metadata_set(void*, uint32_t key, int32_t value) {
    ++metadata_calls; metadata_key = static_cast<int32_t>(key); metadata_value = value;
}
static StrongPointer<void> control_result() {
    active = creates++;
    assert(active < 4);
    windows[active].id = active;
    StrongPointer<void> result{};
    result.pointer = &controls[active];
    return result;
}
static StrongPointer<void> create_legacy(void*, void*, uint32_t w, uint32_t h,
                                          int32_t format, uint32_t flags, void*, int32_t type, int32_t) {
    assert(w == 432 && h == 432 && format == 1);
    created_flags = flags; window_type = type;
    return control_result();
}
static StrongPointer<void> create_modern(void*, void*, uint32_t w, uint32_t h,
                                          int32_t format, uint32_t flags, void*, void*, uint32_t*) {
    assert(w == 432 && h == 432 && format == 1);
    created_flags = flags;
    return control_result();
}
static StrongPointer<Surface> get_surface(void* control) {
    assert(control == &controls[active]);
    StrongPointer<Surface> result{};
    // The wrapper adds this offset to libgui's Surface pointer to reach its
    // ANativeWindow subobject. It is never dereferenced by this fake.
    result.pointer = reinterpret_cast<Surface*>(
        reinterpret_cast<uintptr_t>(&windows[active]) - sizeof(std::max_align_t) / 2);
    return result;
}
static void transaction_ctor(void*) {}
static void* set_layer(void* thiz, StrongPointer<void>& control, int32_t z) {
    assert(control.pointer == &controls[active] && z == INT_MAX);
    ++layer_calls; return thiz;
}
static void* set_trusted(void* thiz, StrongPointer<void>& control, bool trusted) {
    assert(control.pointer == &controls[active] && trusted);
    ++trusted_calls; return thiz;
}
static void* set_flags(void* thiz, StrongPointer<void>& control, uint32_t flags, uint32_t mask) {
    assert(control.pointer == &controls[active]);
    ++flags_calls; last_flags = flags; last_mask = mask; return thiz;
}
static int32_t apply(bool sync, bool oneway) {
    ++apply_calls; last_sync = sync; last_oneway = oneway;
    if (fail_apply) { fail_apply = false; return -1; }
    return 0;
}
static int32_t apply_old(void*, bool sync) { return apply(sync, false); }
static int32_t apply_new(void*, bool sync, bool oneway) { return apply(sync, oneway); }

template <typename T> static void* address(T f) { return reinterpret_cast<void*>(f); }
static void* fake_find(void*, const char* name) {
    if (std::strstr(name, "createSurface"))
        return version <= 9 ? address(create_legacy) : address(create_modern);
    if (std::strstr(name, "LayerMetadataC2")) return missing == "metadata_ctor" ? nullptr : address(metadata_ctor);
    if (std::strstr(name, "LayerMetadata8setInt32")) return missing == "metadata_set" ? nullptr : address(metadata_set);
    if (std::strstr(name, "SurfaceControl10getSurface")) return address(get_surface);
    if (std::strstr(name, "TransactionC2")) return missing == "constructor" ? nullptr : address(transaction_ctor);
    if (std::strstr(name, "Transaction8setLayer")) return address(set_layer);
    if (std::strstr(name, "Transaction17setTrustedOverlay") && std::strstr(name, "EEEb")) return address(set_trusted);
    if (std::strstr(name, "Transaction8setFlags")) return missing == "flags" ? nullptr : address(set_flags);
    if (std::strstr(name, "Transaction5applyEbb"))
        return (version >= 13 && missing != "apply") ? address(apply_new) : nullptr;
    if (std::strstr(name, "Transaction5applyEb"))
        return (version == 12 && missing != "apply") ? address(apply_old) : nullptr;
    return nullptr;
}

int main(int argc, char** argv) {
    assert(argc >= 3);
    version = std::atoi(argv[1]); sdk = std::atoi(argv[2]);
    if (argc > 3) missing = argv[3];
    if (argc > 4) codename = std::strcmp(argv[4], "codename") == 0;
    const Functionals::SymbolMethod symbols{fake_open, fake_find, fake_close};
    assert(Functionals::GetInstance(symbols).systemVersion == static_cast<size_t>(version));

    auto* first = ANativeWindowCreator::Create("test", 432, 432, false);
    assert(first == &windows[0] && created_flags == 0);
    if (version <= 9) assert(window_type == -1);
    if (version == 10 || version == 11) assert(metadata_calls == 0);
    if (version >= 12) assert(layer_calls == 1 && trusted_calls == 1);

    ANativeWindow unknown{};
    using Change = ANativeWindowCreator::CaptureChange;
    assert(ANativeWindowCreator::SetCaptureVisible(&unknown, true) == Change::Failed);
    const bool missing_transaction = missing == "flags" || missing == "apply" || missing == "constructor";
    if (version < 12 || missing_transaction) {
        assert(ANativeWindowCreator::SetCaptureVisible(first, false) == Change::Unavailable);
        assert(flags_calls == 0);
    } else if (missing == "fail") {
        fail_apply = true;
        assert(ANativeWindowCreator::SetCaptureVisible(first, false) == Change::Failed);
        const int prior_calls = flags_calls;
        assert(ANativeWindowCreator::SetCaptureVisible(first, true) == Change::Unavailable);
        assert(flags_calls == prior_calls); // poisoned transaction is never reused
    } else {
        assert(ANativeWindowCreator::SetCaptureVisible(first, false) == Change::Applied);
        assert(flags_calls == 1 && last_flags == 0x40 && last_mask == 0xc0);
        assert(last_sync && !last_oneway);
        assert(ANativeWindowCreator::SetCaptureVisible(first, true) == Change::Applied);
        assert(flags_calls == 2 && last_flags == 0 && last_mask == 0xc0);
        assert(last_sync && !last_oneway);
    }
    ANativeWindowCreator::Destroy(first);
    assert(ANativeWindowCreator::SetCaptureVisible(first, true) == Change::Failed);

    const bool missing_meta = missing == "metadata_ctor" || missing == "metadata_set";
    const bool expected_exclusion = !(version >= 10 && version <= 11 && missing_meta);
    assert(ANativeWindowCreator::CanCreateCaptureExcluded() == expected_exclusion);
    if (expected_exclusion) {
        auto* second = ANativeWindowCreator::Create("test", 432, 432, true);
        assert(second == &windows[1]);
        if (version <= 9) assert(window_type == 441731 && created_flags == 0);
        else if (version <= 11) assert(metadata_calls == 1 && metadata_key == 2 && metadata_value == 441731);
        else assert(created_flags == 0x40);
        ANativeWindowCreator::Destroy(second);
    }
    std::printf("PASS: Android %d sdk %d %s%s\n", version, sdk,
                missing.empty() ? "normal" : missing.c_str(), codename ? " (codename)" : "");
}
