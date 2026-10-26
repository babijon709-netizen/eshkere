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

// Размер поверхности — те же глобалы, что в Android_draw/draw.h: по ним
// хост-тест собирает реальные farm.cpp и aim.cpp.
extern int native_window_screen_x;
extern int native_window_screen_y;
