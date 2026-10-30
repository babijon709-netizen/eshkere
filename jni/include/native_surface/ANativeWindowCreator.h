#ifndef A_NATIVE_WINDOW_CREATOR_H
#define A_NATIVE_WINDOW_CREATOR_H

#include <android/native_window.h>
#include <dlfcn.h>
#include <sys/system_properties.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <vector>

#include "overlay_capture.h"

#define ResolveMethod(ClassName, MethodName, Handle, MethodSignature) \
    ClassName##__##MethodName = reinterpret_cast<decltype(ClassName##__##MethodName)>(symbolMethod.Find(Handle, MethodSignature));

namespace anwc {
    namespace detail {
        namespace ui {
            struct LayerStack {
                uint32_t id = UINT32_MAX;
            };

            enum class Rotation {
                Rotation0 = 0,
                Rotation90 = 1,
                Rotation180 = 2,
                Rotation270 = 3
            };

            struct Size {
                int32_t width = -1;
                int32_t height = -1;
            };

            struct DisplayState {
                LayerStack layerStack;
                Rotation orientation = Rotation::Rotation0;
                Size layerStackSpaceRect;
                uint8_t reserved[128]{};
            };

            typedef int64_t nsecs_t;
            struct DisplayInfo {
                uint32_t w{0};
                uint32_t h{0};
                float xdpi{0};
                float ydpi{0};
                float fps{0};
                float density{0};
                uint8_t orientation{0};
                bool secure{false};
                nsecs_t appVsyncOffset{0};
                nsecs_t presentationDeadline{0};
                uint32_t viewportW{0};
                uint32_t viewportH{0};
                uint8_t reserved[128]{};
            };

            enum class DisplayType {
                DisplayIdMain = 0,
                DisplayIdHdmi = 1
            };

            struct PhysicalDisplayId {
                uint64_t value;
            };

            // ABI-compatible return layout for std::vector<PhysicalDisplayId> returned
            // by system libgui.so (linked against /system/lib64/libc++.so) without
            // invoking benzware's c++_static std::vector destructor across STL boundaries.
            struct PhysicalDisplayIdVector {
                PhysicalDisplayId *begin_ptr = nullptr;
                PhysicalDisplayId *end_ptr = nullptr;
                PhysicalDisplayId *cap_ptr = nullptr;

                PhysicalDisplayIdVector() noexcept = default;
                PhysicalDisplayIdVector(const PhysicalDisplayIdVector &) noexcept = default;
                ~PhysicalDisplayIdVector() noexcept {}

                bool empty() const noexcept { return begin_ptr == nullptr || begin_ptr == end_ptr; }
                PhysicalDisplayId operator[](size_t i) const noexcept { return begin_ptr[i]; }
            };
        }

        struct String8;
        struct LayerMetadata;
        struct Surface;
        struct SurfaceControl;
        struct SurfaceComposerClientTransaction;
        struct SurfaceComposerClient;

        template<typename any_t>
        struct StrongPointer {
            union {
                any_t *pointer;
                char padding[sizeof(std::max_align_t)];
            };

            inline any_t *operator->() const { return pointer; }
            inline any_t *get() const { return pointer; }
            inline explicit operator bool() const { return nullptr != pointer; }
        };

        static inline void ApplyOrientationSwap(int32_t &outWidth, int32_t &outHeight, const ui::DisplayState &state) {
            uint32_t orient = static_cast<uint32_t>(state.orientation);
            if (orient == 1 || orient == 3) {
                outWidth  = state.layerStackSpaceRect.height;
                outHeight = state.layerStackSpaceRect.width;
            } else {
                outWidth  = state.layerStackSpaceRect.width;
                outHeight = state.layerStackSpaceRect.height;
            }
        }

        struct Functionals {
            struct SymbolMethod {
                void *(*Open)(const char *filename, int flag) = nullptr;
                void *(*Find)(void *handle, const char *symbol) = nullptr;
                int (*Close)(void *handle) = nullptr;
            };

            size_t systemVersion = 13;

            void (*RefBase__IncStrong)(void *thiz, void *id) = nullptr;
            void (*RefBase__DecStrong)(void *thiz, void *id) = nullptr;
            void (*String8__Constructor)(void *thiz, const char *const data) = nullptr;
            void (*String8__Destructor)(void *thiz) = nullptr;
            void (*LayerMetadata__Constructor)(void *thiz) = nullptr;
            void (*LayerMetadata__setInt32)(void *thiz, uint32_t type, int32_t value) = nullptr;
            void (*SurfaceComposerClient__Constructor)(void *thiz) = nullptr;
            void (*SurfaceComposerClient__Destructor)(void *thiz) = nullptr;

            StrongPointer<void>(*SurfaceComposerClient__CreateSurface)(void *thiz, void *name, uint32_t w, uint32_t h,
                                                                        int32_t format, uint32_t flags, void *parentHandle,
                                                                        void *layerMetadata, uint32_t *outTransformHint) = nullptr;

            StrongPointer<void>(*SurfaceComposerClient__GetInternalDisplayToken)() = nullptr;
            StrongPointer<void>(*SurfaceComposerClient__GetBuiltInDisplay)(ui::DisplayType type) = nullptr;
            int32_t(*SurfaceComposerClient__GetDisplayState)(StrongPointer<void> &display, ui::DisplayState *displayState) = nullptr;
            int32_t(*SurfaceComposerClient__GetDisplayInfo)(StrongPointer<void> &display, ui::DisplayInfo *displayInfo) = nullptr;
            ui::PhysicalDisplayIdVector(*SurfaceComposerClient__GetPhysicalDisplayIds)() = nullptr;
            StrongPointer<void>(*SurfaceComposerClient__GetPhysicalDisplayToken)(ui::PhysicalDisplayId displayId) = nullptr;
            void(*SurfaceComposerClient__Transaction__Constructor)(void *thiz) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetLayer)(void *thiz, StrongPointer<void> &surfaceControl, int32_t z) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetTrustedOverlay)(void *thiz, StrongPointer<void> &surfaceControl, bool isTrustedOverlay) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetTrustedOverlay_v2)(void *thiz, StrongPointer<void> &surfaceControl, int32_t trustedOverlayState) = nullptr;
            int32_t(*SurfaceComposerClient__Transaction__Apply)(void *thiz, bool synchronous, bool oneWay) = nullptr;
            void *(*SurfaceComposerClient__Transaction__SetFlags)(void *thiz, StrongPointer<void> &surfaceControl, uint32_t flags, uint32_t mask) = nullptr;
            int32_t(*SurfaceControl__Validate)(void *thiz) = nullptr;
            StrongPointer<Surface>(*SurfaceControl__GetSurface)(void *thiz) = nullptr;
            void(*SurfaceControl__DisConnect)(void *thiz) = nullptr;

            Functionals(const SymbolMethod &symbolMethod) {
                char verBuf[PROP_VALUE_MAX]{};
                bool releaseKnown = false;
                if (__system_property_get("ro.build.version.release", verBuf) > 0) {
                    int parsed = std::atoi(verBuf);
                    if (parsed > 0) {
                        systemVersion = static_cast<size_t>(parsed);
                        releaseKnown = true;
                    }
                }
                // Pre-release / OEM release strings can be nonnumeric. SDK is
                // also needed for the Android 8–12 creation/transaction ABI.
                char sdkBuf[PROP_VALUE_MAX]{};
                if (__system_property_get("ro.build.version.sdk", sdkBuf) > 0) {
                    int sdk = std::atoi(sdkBuf);
                    size_t sdkVersion = sdk >= 36 ? 16 : sdk == 35 ? 15 : sdk == 34 ? 14 :
                                        sdk == 33 ? 13 : sdk >= 31 ? 12 : sdk == 30 ? 11 :
                                        sdk == 29 ? 10 : sdk == 28 ? 9 : sdk >= 26 ? 8 :
                                        sdk >= 24 ? 7 : 0;
                    if (sdkVersion && (!releaseKnown || systemVersion < sdkVersion))
                        systemVersion = sdkVersion;
                }
                if (systemVersion > 16)
                    systemVersion = 16;

                if (systemVersion < 8)
                    throw std::runtime_error("ANativeWindowCreator: Android < 8 is not supported");

                static std::unordered_map<size_t, std::unordered_map<void **, const char *>> patchesTable = {
                        {16, {
                                {reinterpret_cast<void **>(&LayerMetadata__Constructor),               "_ZN7android3gui13LayerMetadataC2Ev"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface),     "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                                {reinterpret_cast<void **>(&LayerMetadata__setInt32),                  "_ZN7android3gui13LayerMetadata8setInt32Eji"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__SetFlags), "_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj"},
                        }},
                        {15, {
                                {reinterpret_cast<void **>(&LayerMetadata__Constructor),               "_ZN7android3gui13LayerMetadataC2Ev"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface),     "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                                {reinterpret_cast<void **>(&LayerMetadata__setInt32),                  "_ZN7android3gui13LayerMetadata8setInt32Eji"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__SetFlags), "_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj"},
                        }},
                        {14, {
                                {reinterpret_cast<void **>(&LayerMetadata__Constructor),               "_ZN7android3gui13LayerMetadataC2Ev"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface),     "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__SetFlags), "_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj"},
                        }},
                        {12, {
                                {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__Apply),    "_ZN7android21SurfaceComposerClient11Transaction5applyEb"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__Transaction__SetFlags), "_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj"},
                        }},
                        {11, {
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataEPj"},
                                {reinterpret_cast<void **>(&SurfaceControl__GetSurface),           "_ZNK7android14SurfaceControl10getSurfaceEv"},
                        }},
                        {10, {
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface), "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlENS_13LayerMetadataE"},
                                {reinterpret_cast<void **>(&SurfaceControl__GetSurface),           "_ZNK7android14SurfaceControl10getSurfaceEv"},
                        }},
                        {9, {
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface),     "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEii"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__GetBuiltInDisplay), "_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi"},
                                {reinterpret_cast<void **>(&SurfaceControl__GetSurface),               "_ZNK7android14SurfaceControl10getSurfaceEv"},
                        }},
                        {8, {
                                {reinterpret_cast<void **>(&SurfaceComposerClient__CreateSurface),     "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijPNS_14SurfaceControlEjj"},
                                {reinterpret_cast<void **>(&SurfaceComposerClient__GetBuiltInDisplay), "_ZN7android21SurfaceComposerClient17getBuiltInDisplayEi"},
                                {reinterpret_cast<void **>(&SurfaceControl__GetSurface),               "_ZNK7android14SurfaceControl10getSurfaceEv"},
                        }},
                };

#ifdef __LP64__
                auto libgui   = symbolMethod.Open("/system/lib64/libgui.so", RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib64/libutils.so", RTLD_LAZY);
#else
                auto libgui   = symbolMethod.Open("/system/lib/libgui.so", RTLD_LAZY);
                auto libutils = symbolMethod.Open("/system/lib/libutils.so", RTLD_LAZY);
#endif

                ResolveMethod(RefBase, IncStrong, libutils, "_ZNK7android7RefBase9incStrongEPKv");
                ResolveMethod(RefBase, DecStrong, libutils, "_ZNK7android7RefBase9decStrongEPKv");
                ResolveMethod(String8, Constructor, libutils, "_ZN7android7String8C2EPKc");
                ResolveMethod(String8, Destructor, libutils, "_ZN7android7String8D2Ev");
                ResolveMethod(LayerMetadata, Constructor, libgui, "_ZN7android13LayerMetadataC2Ev");
                ResolveMethod(LayerMetadata, setInt32, libgui, "_ZN7android13LayerMetadata8setInt32Eji");
                ResolveMethod(SurfaceComposerClient, Constructor, libgui, "_ZN7android21SurfaceComposerClientC2Ev");
                ResolveMethod(SurfaceComposerClient, CreateSurface, libgui, "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjijRKNS_2spINS_7IBinderEEENS_13LayerMetadataEPj");
                ResolveMethod(SurfaceComposerClient, GetInternalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getInternalDisplayTokenEv");
                ResolveMethod(SurfaceComposerClient, GetDisplayState, libgui, "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE");
                ResolveMethod(SurfaceComposerClient, GetDisplayInfo, libgui, "_ZN7android21SurfaceComposerClient14getDisplayInfoERKNS_2spINS_7IBinderEEEPNS_11DisplayInfoE");
                ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayIds, libgui, "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv");
                ResolveMethod(SurfaceComposerClient, GetPhysicalDisplayToken, libgui, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE");
                ResolveMethod(SurfaceComposerClient__Transaction, Constructor, libgui, "_ZN7android21SurfaceComposerClient11TransactionC2Ev");
                ResolveMethod(SurfaceComposerClient__Transaction, SetLayer, libgui, "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
                ResolveMethod(SurfaceComposerClient__Transaction, SetTrustedOverlay, libgui, "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb");
                ResolveMethod(SurfaceComposerClient__Transaction, SetTrustedOverlay_v2, libgui, "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEENS_3gui14TrustedOverlayE");
                ResolveMethod(SurfaceComposerClient__Transaction, Apply, libgui, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");
                ResolveMethod(SurfaceComposerClient__Transaction, SetFlags, libgui, "_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj");
                ResolveMethod(SurfaceControl, Validate, libgui, "_ZNK7android14SurfaceControl8validateEv");
                ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZN7android14SurfaceControl10getSurfaceEv");
                ResolveMethod(SurfaceControl, DisConnect, libgui, "_ZN7android14SurfaceControl10disconnectEv");

                auto it = patchesTable.find(systemVersion);
                if (it != patchesTable.end()) {
                    for (const auto &[patchTo, signature] : it->second) {
                        void *resolved = symbolMethod.Find(libgui, signature);
                        if (resolved != nullptr)
                            *patchTo = resolved;
                    }
                }

                // Fallback for Android 16 QPR / 17+ where createSurface takes const gui::LayerMetadata&
                if (systemVersion >= 14 && SurfaceComposerClient__CreateSurface == nullptr) {
                    ResolveMethod(SurfaceComposerClient, CreateSurface, libgui,
                                  "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPj");
                }
                if (SurfaceControl__GetSurface == nullptr) {
                    ResolveMethod(SurfaceControl, GetSurface, libgui, "_ZNK7android14SurfaceControl10getSurfaceEv");
                }

                // Intentionally keep libgui.so and libutils.so loaded: benzware does not
                // link them in DT_NEEDED, so dlclose() could unmap resolved code pages.
            }

            static const Functionals &GetInstance(const SymbolMethod &symbolMethod = {.Open = dlopen, .Find = dlsym, .Close = dlclose}) {
                static Functionals functionals(symbolMethod);
                return functionals;
            }
        };

        struct String8 {
            char data[1024]{};

            String8(const char *const string) {
                if (Functionals::GetInstance().String8__Constructor)
                    Functionals::GetInstance().String8__Constructor(data, string);
            }
            ~String8() {
                if (Functionals::GetInstance().String8__Destructor)
                    Functionals::GetInstance().String8__Destructor(data);
            }
            operator void *() { return reinterpret_cast<void *>(data); }
        };

        struct LayerMetadata {
            char data[1024]{};

            LayerMetadata() {
                if (9 < Functionals::GetInstance().systemVersion && Functionals::GetInstance().LayerMetadata__Constructor)
                    Functionals::GetInstance().LayerMetadata__Constructor(data);
            }

            void setInt32(uint32_t type, int32_t value) {
                if (9 < Functionals::GetInstance().systemVersion && Functionals::GetInstance().LayerMetadata__setInt32)
                    Functionals::GetInstance().LayerMetadata__setInt32(data, type, value);
            }

            operator void *() {
                return 9 < Functionals::GetInstance().systemVersion ? reinterpret_cast<void *>(data) : nullptr;
            }
        };

        struct Surface {};

        struct SurfaceControl {
            void *data;

            SurfaceControl() : data(nullptr) {}
            SurfaceControl(void *data) : data(data) {}

            int32_t Validate() {
                if (nullptr == data || nullptr == Functionals::GetInstance().SurfaceControl__Validate) return 0;
                return Functionals::GetInstance().SurfaceControl__Validate(data);
            }

            Surface *GetSurface() {
                if (nullptr == data || nullptr == Functionals::GetInstance().SurfaceControl__GetSurface) return nullptr;
                auto result = Functionals::GetInstance().SurfaceControl__GetSurface(data);
                if (nullptr == result.pointer) return nullptr;
                return reinterpret_cast<Surface *>(reinterpret_cast<size_t>(result.pointer) + sizeof(std::max_align_t) / 2);
            }

            void DisConnect() {
                if (nullptr != data && nullptr != Functionals::GetInstance().SurfaceControl__DisConnect)
                    Functionals::GetInstance().SurfaceControl__DisConnect(data);
            }

            void DestroySurface(Surface *surface) {
                if (nullptr == data || nullptr == surface) return;
                if (Functionals::GetInstance().RefBase__DecStrong)
                    Functionals::GetInstance().RefBase__DecStrong(
                            reinterpret_cast<Surface *>(reinterpret_cast<size_t>(surface) - sizeof(std::max_align_t) / 2), this);
                DisConnect();
                if (Functionals::GetInstance().RefBase__DecStrong)
                    Functionals::GetInstance().RefBase__DecStrong(data, this);
            }
        };

        struct SurfaceComposerClientTransaction {
            alignas(std::max_align_t) char data[1024]{};

            SurfaceComposerClientTransaction() {
                if (Functionals::GetInstance().SurfaceComposerClient__Transaction__Constructor)
                    Functionals::GetInstance().SurfaceComposerClient__Transaction__Constructor(data);
            }

            void *SetLayer(StrongPointer<void> &sc, int32_t z) {
                if (nullptr == Functionals::GetInstance().SurfaceComposerClient__Transaction__SetLayer) return nullptr;
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetLayer(data, sc, z);
            }

            void *SetTrustedOverlay(StrongPointer<void> &sc, bool trusted) {
                const auto &f = Functionals::GetInstance();
                if (nullptr != f.SurfaceComposerClient__Transaction__SetTrustedOverlay)
                    return f.SurfaceComposerClient__Transaction__SetTrustedOverlay(data, sc, trusted);
                if (nullptr != f.SurfaceComposerClient__Transaction__SetTrustedOverlay_v2) {
                    // gui::TrustedOverlay: UNSET = 0, DISABLED = 1, ENABLED = 2
                    return f.SurfaceComposerClient__Transaction__SetTrustedOverlay_v2(data, sc, trusted ? 2 : 1);
                }
                return nullptr;
            }

            void *SetFlags(StrongPointer<void> &sc, uint32_t flags, uint32_t mask) {
                if (nullptr == Functionals::GetInstance().SurfaceComposerClient__Transaction__SetFlags) return nullptr;
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__SetFlags(data, sc, flags, mask);
            }

            int32_t Apply(bool synchronous, bool oneWay) {
                if (nullptr == Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply) return 0;
                if (12 >= Functionals::GetInstance().systemVersion)
                    return reinterpret_cast<int32_t (*)(void *, bool)>(
                            Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply)(data, synchronous);
                return Functionals::GetInstance().SurfaceComposerClient__Transaction__Apply(data, synchronous, oneWay);
            }
        };

        struct SurfaceComposerClient {
            char data[1024]{};

            SurfaceComposerClient() {
                if (Functionals::GetInstance().SurfaceComposerClient__Constructor)
                    Functionals::GetInstance().SurfaceComposerClient__Constructor(data);
                if (Functionals::GetInstance().RefBase__IncStrong)
                    Functionals::GetInstance().RefBase__IncStrong(data, this);
            }

            ~SurfaceComposerClient() {
                if (Functionals::GetInstance().SurfaceComposerClient__Destructor)
                    Functionals::GetInstance().SurfaceComposerClient__Destructor(data);
                else if (Functionals::GetInstance().RefBase__DecStrong)
                    Functionals::GetInstance().RefBase__DecStrong(data, this);
            }

            SurfaceControl CreateSurface(const char *name, int32_t width, int32_t height, bool hide, bool) {
                if (nullptr == Functionals::GetInstance().SurfaceComposerClient__CreateSurface)
                    return {};

                void *parentHandle = nullptr;
                String8 windowName(name);
                LayerMetadata layerMetadata;
                uint32_t flags = 0;

                if (12 <= Functionals::GetInstance().systemVersion) {
                    static void *fakeParentHandleForBinder = nullptr;
                    parentHandle = &fakeParentHandleForBinder;
                }

                StrongPointer<void> result{};
                if (Functionals::GetInstance().systemVersion <= 9) {
                    int value = hide ? 441731 : -1;
                    result = reinterpret_cast<StrongPointer<void> (*)(void *, void *, uint32_t, uint32_t, int32_t, uint32_t, void *, int32_t, int32_t)>(
                            Functionals::GetInstance().SurfaceComposerClient__CreateSurface)(data, windowName, width, height, 1, flags, nullptr, value, -1);
                } else {
                    if (hide) {
                        if (Functionals::GetInstance().systemVersion < 12)
                            layerMetadata.setInt32(2, 441731);
                        else
                            flags = overlay_capture::kSkipScreenshot;
                    }
                    result = Functionals::GetInstance().SurfaceComposerClient__CreateSurface(data, windowName, width, height, 1, flags, parentHandle, layerMetadata, nullptr);
                }

                if (12 <= Functionals::GetInstance().systemVersion && nullptr != result.get()) {
                    static SurfaceComposerClientTransaction transaction;
                    transaction.SetLayer(result, INT_MAX);
                    transaction.SetTrustedOverlay(result, true);
                    transaction.Apply(false, true);
                }

                return {result.get()};
            }

            bool GetDisplayInfo(ui::DisplayState *displayState) {
                static StrongPointer<void> cachedDisplay{};
                const auto &f = Functionals::GetInstance();

                auto acquireDisplay = [&]() -> StrongPointer<void> {
                    if (9 >= f.systemVersion) {
                        if (nullptr == f.SurfaceComposerClient__GetBuiltInDisplay) return {};
                        return f.SurfaceComposerClient__GetBuiltInDisplay(ui::DisplayType::DisplayIdMain);
                    } else if (14 > f.systemVersion) {
                        if (nullptr == f.SurfaceComposerClient__GetInternalDisplayToken) return {};
                        return f.SurfaceComposerClient__GetInternalDisplayToken();
                    } else {
                        if (nullptr == f.SurfaceComposerClient__GetPhysicalDisplayIds ||
                            nullptr == f.SurfaceComposerClient__GetPhysicalDisplayToken)
                            return {};
                        auto displayIds = f.SurfaceComposerClient__GetPhysicalDisplayIds();
                        if (displayIds.empty()) return {};
                        return f.SurfaceComposerClient__GetPhysicalDisplayToken(displayIds[0]);
                    }
                };

                if (nullptr == cachedDisplay.get())
                    cachedDisplay = acquireDisplay();
                if (nullptr == cachedDisplay.get())
                    return false;

                if (11 <= f.systemVersion) {
                    if (nullptr == f.SurfaceComposerClient__GetDisplayState) return false;
                    if (0 == f.SurfaceComposerClient__GetDisplayState(cachedDisplay, displayState))
                        return true;
                    // Re-acquire once if display token became stale
                    cachedDisplay = acquireDisplay();
                    if (nullptr == cachedDisplay.get()) return false;
                    return 0 == f.SurfaceComposerClient__GetDisplayState(cachedDisplay, displayState);
                } else {
                    if (nullptr == f.SurfaceComposerClient__GetDisplayInfo) return false;
                    ui::DisplayInfo legacyInfo{};
                    if (0 != f.SurfaceComposerClient__GetDisplayInfo(cachedDisplay, &legacyInfo))
                        return false;
                    displayState->layerStackSpaceRect.width  = static_cast<int32_t>(legacyInfo.w);
                    displayState->layerStackSpaceRect.height = static_cast<int32_t>(legacyInfo.h);
                    displayState->orientation = static_cast<ui::Rotation>(legacyInfo.orientation);
                    return true;
                }
            }
        };
    }

    class ANativeWindowCreator {
    public:
        struct DisplayInfo {
            uint32_t orientation;
            int32_t width;
            int32_t height;
        };

        static detail::SurfaceComposerClient &GetComposerInstance() {
            static detail::SurfaceComposerClient surfaceComposerClient;
            return surfaceComposerClient;
        }

        static DisplayInfo GetDisplayInfo() {
            detail::ui::DisplayState displayState{};
            if (!GetComposerInstance().GetDisplayInfo(&displayState))
                return {};

            DisplayInfo result{};
            result.orientation = static_cast<uint32_t>(displayState.orientation);
            if (detail::Functionals::GetInstance().systemVersion <= 10)
                detail::ApplyOrientationSwap(result.width, result.height, displayState);
            else {
                result.width  = displayState.layerStackSpaceRect.width;
                result.height = displayState.layerStackSpaceRect.height;
            }
            return result;
        }

        static ANativeWindow *Create(const char *name, int32_t width = -1, int32_t height = -1, bool hide = true) {
            auto &client = GetComposerInstance();

            if (-1 == width || -1 == height) {
                detail::ui::DisplayState displayState{};
                if (client.GetDisplayInfo(&displayState)) {
                    if (detail::Functionals::GetInstance().systemVersion <= 10)
                        detail::ApplyOrientationSwap(width, height, displayState);
                    else {
                        width  = displayState.layerStackSpaceRect.width;
                        height = displayState.layerStackSpaceRect.height;
                    }
                }
            }

            auto surfaceControl = client.CreateSurface(name, width, height, hide, false);
            auto nativeWindow   = reinterpret_cast<ANativeWindow *>(surfaceControl.GetSurface());
            if (nullptr != nativeWindow)
                m_cachedSurfaceControl.emplace(nativeWindow, surfaceControl);
            return nativeWindow;
        }

        enum class CaptureChange { Applied, Unavailable, Failed };

        // Android 12+ exposes SKIP_SCREENSHOT as a transaction flag. Clear
        // SECURE too when changing our own layer; never modify other windows.
        // On Android 8–11 primary-display-only is fixed at layer creation, so
        // the caller must recreate the layer instead.
        static CaptureChange SetCaptureVisible(ANativeWindow *nativeWindow, bool visible) {
            auto it = m_cachedSurfaceControl.find(nativeWindow);
            if (it == m_cachedSurfaceControl.end() || it->second.data == nullptr)
                return CaptureChange::Failed;
            const auto &f = detail::Functionals::GetInstance();
            if (f.systemVersion < 12 || !f.SurfaceComposerClient__Transaction__Constructor ||
                !f.SurfaceComposerClient__Transaction__SetFlags || !f.SurfaceComposerClient__Transaction__Apply)
                return CaptureChange::Unavailable;

            // The wrapper intentionally keeps the libgui transaction alive, as
            // elsewhere in this creator. After an error it may retain mStatus;
            // never reuse a poisoned transaction, fall back to a fresh layer.
            static bool usable = true;
            if (!usable) return CaptureChange::Unavailable;
            static detail::SurfaceComposerClientTransaction transaction;
            detail::StrongPointer<void> control{};
            control.pointer = it->second.data;
            if (!transaction.SetFlags(control, overlay_capture::flags_for_visible(visible),
                                      overlay_capture::kCaptureMask) ||
                transaction.Apply(true, false) != 0) {
                usable = false;
                return CaptureChange::Failed;
            }
            return CaptureChange::Applied;
        }

        // On Android 10/11 the legacy exclusion is encoded in metadata at
        // creation. Do not silently report a hidden layer if those symbols are
        // unavailable on an OEM build.
        static bool CanCreateCaptureExcluded() {
            const auto &f = detail::Functionals::GetInstance();
            return f.systemVersion < 10 || f.systemVersion >= 12 ||
                   (f.LayerMetadata__Constructor && f.LayerMetadata__setInt32);
        }

        static void Destroy(ANativeWindow *nativeWindow) {
            auto it = m_cachedSurfaceControl.find(nativeWindow);
            if (it == m_cachedSurfaceControl.end())
                return;
            it->second.DestroySurface(reinterpret_cast<detail::Surface *>(nativeWindow));
            m_cachedSurfaceControl.erase(it);
        }

    private:
        inline static std::unordered_map<ANativeWindow *, detail::SurfaceControl> m_cachedSurfaceControl;
    };
}

#undef ResolveMethod

#endif