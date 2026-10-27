// Compile the real draw.cpp against fake Android/EGL to test switching and
// rollback. Does not exercise an Android GPU or SurfaceFlinger.
#include "Android_draw/draw.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using Creator = anwc::ANativeWindowCreator;
extern EGLDisplay display;
extern EGLContext context;
extern EGLSurface surface;
extern ANativeWindow* native_window;

static ANativeWindow windows[32]{};
static bool released[32]{}, destroyed[32]{};
struct FakeSurface { ANativeWindow* window; bool destroyed; };
static FakeSurface surfaces[32]{};
static int window_count = 0, surface_count = 0, flag_calls = 0, swap_intervals = 0;
static bool can_exclude = true, fail_format = false, fail_window = false;
static bool fail_geometry = false, fail_surface = false, fail_current = false;
static bool last_hide = false, last_flag_target = false;
static Creator::CaptureChange change = Creator::CaptureChange::Applied;
static EGLSurface current_surface = EGL_NO_SURFACE;
static ANativeWindow* backend_window = nullptr;
static EGLDisplay fake_display = reinterpret_cast<void*>(0x1000);
static EGLConfig fake_config = reinterpret_cast<void*>(0x2000);
static EGLContext fake_context = reinterpret_cast<void*>(0x3000);

ANativeWindow* Creator::Create(const char*, int32_t w, int32_t h, bool hide) {
    assert(w == 1440 && h == 1440);
    last_hide = hide;
    if (fail_window) return nullptr;
    assert(window_count < 32);
    windows[window_count].id = window_count;
    return &windows[window_count++];
}
Creator::CaptureChange Creator::SetCaptureVisible(ANativeWindow* w, bool visible) {
    assert(w && !destroyed[w->id]);
    ++flag_calls; last_flag_target = visible;
    return change;
}
bool Creator::CanCreateCaptureExcluded() { return can_exclude; }
void Creator::Destroy(ANativeWindow* w) { assert(w && released[w->id]); destroyed[w->id] = true; }
Creator::DisplayInfo Creator::GetDisplayInfo() { return {0, 1440, 900}; }
extern "C" void ANativeWindow_acquire(ANativeWindow* w) { assert(w && !destroyed[w->id]); }
extern "C" void ANativeWindow_release(ANativeWindow* w) { assert(w && !released[w->id]); released[w->id] = true; }
extern "C" int ANativeWindow_setBuffersGeometry(ANativeWindow* w, int width, int height, int format) {
    assert(w && width == 0 && height == 0 && format == 42);
    return fail_geometry ? -1 : 0;
}
extern "C" int __system_property_get(const char*, char*) { return 0; }
float esp_game_frame_hz() { return 60.f; }
bool esp_game_tick_now(uint32_t&) { return false; }

EGLDisplay eglGetDisplay(EGLDisplay) { return fake_display; }
EGLBoolean eglInitialize(EGLDisplay d, EGLint*, EGLint*) { assert(d == fake_display); return EGL_TRUE; }
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig* c, EGLint, EGLint* n) {
    *c = fake_config; *n = 1; return EGL_TRUE;
}
EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint attr, EGLint* value) {
    assert(attr == EGL_NATIVE_VISUAL_ID);
    if (fail_format) return 0;
    *value = 42; return EGL_TRUE;
}
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*) { return fake_context; }
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, ANativeWindow* w, const EGLint*) {
    if (fail_surface) return EGL_NO_SURFACE;
    assert(surface_count < 32);
    surfaces[surface_count] = {w, false};
    return &surfaces[surface_count++];
}
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    assert(draw == read);
    if (fail_current) return 0;
    assert(ctx == fake_context || (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE));
    current_surface = draw; return EGL_TRUE;
}
EGLBoolean eglSwapInterval(EGLDisplay, EGLint interval) { assert(interval == 0); ++swap_intervals; return EGL_TRUE; }
EGLBoolean eglDestroyContext(EGLDisplay, EGLContext c) { assert(c == fake_context); return EGL_TRUE; }
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface s) {
    assert(s != current_surface); // old/candidate must not be the bound surface
    auto* fs = static_cast<FakeSurface*>(s);
    assert(fs && !fs->destroyed); fs->destroyed = true; return EGL_TRUE;
}
EGLBoolean eglTerminate(EGLDisplay d) { assert(d == fake_display); return EGL_TRUE; }
void ImGui_ImplAndroid_SetWindow(ANativeWindow* w) { backend_window = w; }
void ImGui_ImplAndroid_Shutdown() { backend_window = nullptr; }
void ImGui_ImplOpenGL3_Shutdown() {}
namespace ImGui { void DestroyContext(ImGuiContext*) {} }

int main() {
    assert(!overlay_set_capture_visible(false)); // no EGL yet
    assert(init_egl(1440, 1440));
    assert(native_window == &windows[0] && !last_hide && last_flag_target);
    assert(flag_calls == 1 && overlay_capture_visible());
    assert(surface == current_surface && context == fake_context);
    g_Initialized = true;
    backend_window = native_window;
    ANativeWindow* old_window = native_window;
    EGLSurface old_surface = surface;

    assert(overlay_set_capture_visible(false)); // modern transaction, no recreation
    assert(!overlay_capture_visible() && !last_flag_target && window_count == 1);
    assert(overlay_set_capture_visible(true));
    assert(overlay_capture_visible() && last_flag_target && window_count == 1);

    change = Creator::CaptureChange::Unavailable;
    can_exclude = false;
    assert(!overlay_set_capture_visible(false) && overlay_capture_visible() && window_count == 1);
    can_exclude = true;
    fail_format = true;
    assert(!overlay_set_capture_visible(false) && window_count == 1);
    fail_format = false;
    fail_window = true;
    assert(!overlay_set_capture_visible(false) && window_count == 1);
    fail_window = false;
    fail_geometry = true;
    assert(!overlay_set_capture_visible(false));
    assert(released[1] && destroyed[1]);
    fail_geometry = false;
    fail_surface = true;
    assert(!overlay_set_capture_visible(false));
    assert(released[2] && destroyed[2]);
    fail_surface = false;
    fail_current = true;
    assert(!overlay_set_capture_visible(false));
    assert(released[3] && destroyed[3] && surfaces[1].destroyed);
    fail_current = false;
    assert(overlay_capture_visible() && native_window == old_window && surface == old_surface);
    assert(current_surface == old_surface && backend_window == old_window && !destroyed[0]);

    assert(overlay_set_capture_visible(false)); // legacy/new-layer fallback
    assert(last_hide && !overlay_capture_visible() && native_window == &windows[4]);
    assert(released[0] && destroyed[0] && surfaces[0].destroyed);
    assert(current_surface == surface && backend_window == native_window && context == fake_context);
    assert(swap_intervals == 2); // reapply to the new EGL surface
    old_window = native_window; old_surface = surface;
    change = Creator::CaptureChange::Failed;
    assert(overlay_set_capture_visible(true)); // failed transaction also falls back
    assert(!last_hide && overlay_capture_visible() && native_window == &windows[5]);
    assert(released[4] && destroyed[4] && static_cast<FakeSurface*>(old_surface)->destroyed);
    assert(current_surface == surface && backend_window == native_window && context == fake_context);
    assert(swap_intervals == 3);

    old_window = native_window; old_surface = surface;
    shutdown();
    assert(!g_Initialized && !native_window && overlay_capture_visible());
    assert(released[old_window->id] && destroyed[old_window->id]);
    assert(static_cast<FakeSurface*>(old_surface)->destroyed && current_surface == EGL_NO_SURFACE);
    std::puts("PASS: real EGL switch, no-op, failure rollback, legacy recreation and teardown (mocked platform)");
}
