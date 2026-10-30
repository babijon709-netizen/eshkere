#pragma once
#include "magic_farm_protocol.h"
#include <cstdint>

namespace magicfarm {
struct Loaded { std::uintptr_t shared{}; State state{State::LoadFailed}; };
Loaded load_module(int pid, std::uintptr_t game_base);
}
