#pragma once
#include <cstdint>
float esp_game_frame_hz();
bool esp_game_tick_now(uint32_t& tick);
