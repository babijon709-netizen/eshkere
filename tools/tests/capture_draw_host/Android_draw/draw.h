#pragma once
// Minimal Android/EGL declarations for compiling the real draw.cpp on a host.
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <android/native_window.h>
#include "overlay_capture.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "Android_touch/TouchHelperA.h"

using EGLDisplay = void*;
using EGLConfig = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLint = int;
using EGLBoolean = int;
constexpr EGLDisplay EGL_NO_DISPLAY = nullptr;
constexpr EGLSurface EGL_NO_SURFACE = nullptr;
constexpr EGLContext EGL_NO_CONTEXT = nullptr;
constexpr EGLDisplay EGL_DEFAULT_DISPLAY = nullptr;
constexpr EGLBoolean EGL_TRUE = 1;
constexpr EGLint EGL_SURFACE_TYPE = 1, EGL_WINDOW_BIT = 2, EGL_RENDERABLE_TYPE = 3;
constexpr EGLint EGL_OPENGL_ES2_BIT = 4, EGL_BLUE_SIZE = 5, EGL_GREEN_SIZE = 6, EGL_RED_SIZE = 7;
constexpr EGLint EGL_BUFFER_SIZE = 8, EGL_DEPTH_SIZE = 9, EGL_STENCIL_SIZE = 10;
constexpr EGLint EGL_NONE = 11, EGL_CONTEXT_CLIENT_VERSION = 12, EGL_NATIVE_VISUAL_ID = 13;
EGLDisplay eglGetDisplay(EGLDisplay);
EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*);
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint, EGLint*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, ANativeWindow*, const EGLint*);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
EGLBoolean eglSwapInterval(EGLDisplay, EGLint);
EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface);
EGLBoolean eglDestroyContext(EGLDisplay, EGLContext);
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface);
EGLBoolean eglTerminate(EGLDisplay);
constexpr unsigned GL_BLEND = 1, GL_SRC_ALPHA = 2, GL_ONE_MINUS_SRC_ALPHA = 3, GL_COLOR_BUFFER_BIT = 4;
void glEnable(unsigned);
void glBlendFunc(unsigned, unsigned);
void glClear(unsigned);

namespace anwc {
class ANativeWindowCreator {
public:
    struct DisplayInfo { uint32_t orientation; int32_t width, height; };
    enum class CaptureChange { Applied, Unavailable, Failed };
    static ANativeWindow* Create(const char*, int32_t, int32_t, bool);
    static CaptureChange SetCaptureVisible(ANativeWindow*, bool);
    static bool CanCreateCaptureExcluded();
    static void Destroy(ANativeWindow*);
    static DisplayInfo GetDisplayInfo();
};
}
extern anwc::ANativeWindowCreator::DisplayInfo displayInfo;
extern bool g_Initialized;
extern ImGuiWindow* g_window;
extern int native_window_screen_x, native_window_screen_y;
bool init_egl(uint32_t, uint32_t, bool = false);
bool initGUI_draw(uint32_t, uint32_t, bool = false);
bool ImGui_init();
void screen_config();
void drawBegin();
void drawEnd();
void shutdown();
bool overlay_capture_visible();
bool overlay_set_capture_visible(bool);
