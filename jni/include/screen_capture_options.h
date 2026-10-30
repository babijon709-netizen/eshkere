#pragma once

#include <cstdint>

namespace screen_capture {
// A zero bitrate selects the recorder's default (20 Mbit/s).
struct VideoOptions {
    int bitrate_mbps = 0;
};

constexpr int bitrate_index(int mbps) {
    return mbps == 4 ? 1 : mbps == 8 ? 2 : mbps == 16 ? 3 : 0;
}
constexpr int bitrate_at(int index) {
    return index == 1 ? 4 : index == 2 ? 8 : index == 3 ? 16 : 0;
}
constexpr VideoOptions sanitize(VideoOptions options) {
    return {bitrate_at(bitrate_index(options.bitrate_mbps))};
}

// Bits 18–19 were the old overall time limit and stay reserved/ignored: every
// profile now records until stopped. Bits 20–21 retain the bitrate setting,
// with zero preserving the default setting in profiles from before this UI.
constexpr uint32_t kLegacyDurationMask = 3u << 18;
constexpr uint32_t kBitrateShift = 20;
constexpr uint32_t kVideoOptionsMask = 3u << kBitrateShift;
constexpr uint32_t video_config_flags(VideoOptions options) {
    return static_cast<uint32_t>(bitrate_index(options.bitrate_mbps)) << kBitrateShift;
}
constexpr VideoOptions video_from_config(uint32_t flags) {
    return {bitrate_at(static_cast<int>((flags >> kBitrateShift) & 3u))};
}
} // namespace screen_capture
