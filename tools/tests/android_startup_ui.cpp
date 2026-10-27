// Exercise the production key keyboard on Android/Bionic + real EGL/GLES.
// This is a pbuffer render/tap test, not an MT Manager or compositor test.
#include "startup_key_ui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

static std::map<ImGuiID, ImRect> items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& box, const ImGuiLastItemData*) {
    if (id) items[id] = box;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

static constexpr int kWidth = 640, kHeight = 360;
static startup_key::Entry entry;
static ImGuiID id(const char* label) { return ImHashStr(label, 0, ImHashStr("##startup_key")); }
static startup_key_ui::Result frame(ImVec2 tap = {-100.f, -100.f}, bool released = false) {
    items.clear();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {(float)kWidth, (float)kHeight}; io.DeltaTime = 1.f / 60.f;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    io.MouseReleased[0] = released;
    io.MousePos = io.MouseClickedPos[0] = tap;
    const auto result = startup_key_ui::Draw(entry, kWidth, kHeight);
    ImGui::Render();
    glViewport(0, 0, kWidth, kHeight);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return result;
}
static startup_key_ui::Result tap(const char* label) {
    assert(items.count(id(label)));
    const ImRect r = items.at(id(label));
    return frame({(r.Min.x + r.Max.x) * 0.5f, (r.Min.y + r.Max.y) * 0.5f}, true);
}
int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr));
    assert(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config{}; EGLint count = 0;
    assert(eglChooseConfig(display, attributes, &config, 1, &count) && count == 1);
    const EGLint pbuffer[] = {EGL_WIDTH, kWidth, EGL_HEIGHT, kHeight, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer);
    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    assert(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, surface, surface, context));

    ImGui::CreateContext();
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    ImGui::GetIO().IniFilename = nullptr;
    assert(ImGui_ImplOpenGL3_Init("#version 300 es"));
    assert(frame() == startup_key_ui::Result::Waiting);
    std::vector<unsigned char> pixels(kWidth * kHeight * 4);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    assert(glGetError() == GL_NO_ERROR);
    int colored = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] || pixels[i + 1] || pixels[i + 2]) ++colored;
    assert(colored > 1000); // the UI was actually drawn, not only laid out
    for (char c : std::string("t.me/benzware")) {
        char label[] = "##key_x"; label[6] = c;
        assert(tap(label) == startup_key_ui::Result::Waiting);
    }
    assert(tap("##submit") == startup_key_ui::Result::Accepted);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    std::printf("android startup UI: PASS (%d colored pixels)\n", colored);
}
