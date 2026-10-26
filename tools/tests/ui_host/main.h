#pragma once
// Replace only the Android surface/GL entry point header in the host test.
// All menu, widget, theme, localization and ImGui code is the real source.
#include <cstdint>
#include <string>
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_internal.h"
struct HostDisplayInfo { uint32_t orientation = 0, width = 1920, height = 1080; };
extern HostDisplayInfo displayInfo;

// Слой оверлея в хост-тесте не создаётся: стример-мод только меняет флаг,
// поэтому вызов объявлен, а тело — пустышка в menu_ui_test.cpp.
void overlay_set_capture_visible(bool visible);

// Размер поверхности — те же глобалы, что в Android_draw/draw.h.
extern int native_window_screen_x;
extern int native_window_screen_y;
