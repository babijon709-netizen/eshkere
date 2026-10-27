#pragma once
struct ANativeWindow { int id; };
extern "C" void ANativeWindow_acquire(ANativeWindow* window);
extern "C" void ANativeWindow_release(ANativeWindow* window);
extern "C" int ANativeWindow_setBuffersGeometry(ANativeWindow* window, int width, int height, int format);
