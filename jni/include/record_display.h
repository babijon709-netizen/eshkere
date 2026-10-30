#pragma once

#include <memory>
#include <string>

struct ANativeWindow;
namespace record_display {
struct State { int width = 0, height = 0, orientation = 0; unsigned layer_stack = 0; };

// Private SurfaceFlinger ABI is resolved at runtime in the fresh helper. All
// resources belong to that process; secure layers still obey system policy.
class Display {
public:
    Display();
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    bool initialize();
    bool state(State& output);
    bool attach(ANativeWindow* surface, const State& state, int width, int height);
    bool update(const State& state, int width, int height);
    void detach();
    const char* error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace record_display
