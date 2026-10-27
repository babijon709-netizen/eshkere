#pragma once

#include <cstdint>

namespace screen_capture {
// Values passed to Android's screenrecord. A zero bitrate lets the device
// choose its own default; Android 8–16 all accept a 1–3 minute time limit.
struct VideoOptions {
    int duration_seconds = 180;
    int bitrate_mbps = 0;
};

constexpr int duration_index(int seconds) {
    return seconds == 60 ? 0 : seconds == 120 ? 1 : 2;
}
constexpr int duration_at(int index) {
    return index == 0 ? 60 : index == 1 ? 120 : 180;
}
constexpr int bitrate_index(int mbps) {
    return mbps == 4 ? 1 : mbps == 8 ? 2 : mbps == 16 ? 3 : 0;
}
constexpr int bitrate_at(int index) {
    return index == 1 ? 4 : index == 2 ? 8 : index == 3 ? 16 : 0;
}
constexpr VideoOptions sanitize(VideoOptions options) {
    return {duration_at(duration_index(options.duration_seconds)),
            bitrate_at(bitrate_index(options.bitrate_mbps))};
}

// Spare bits in CfgExt2Trailer.flags. Zero must mean the old defaults:
// 180 seconds and the device's automatic bitrate. An unknown duration code
// also falls back to 180 rather than starting an unlimited recording.
constexpr uint32_t kDurationShift = 18;
constexpr uint32_t kBitrateShift = 20;
constexpr uint32_t kVideoOptionsMask = (3u << kDurationShift) | (3u << kBitrateShift);
constexpr uint32_t video_config_flags(VideoOptions options) {
    const unsigned duration = options.duration_seconds == 60 ? 1u :
                              options.duration_seconds == 120 ? 2u : 0u;
    return (duration << kDurationShift) |
           (static_cast<uint32_t>(bitrate_index(options.bitrate_mbps)) << kBitrateShift);
}
constexpr VideoOptions video_from_config(uint32_t flags) {
    const unsigned duration = (flags >> kDurationShift) & 3u;
    const int bitrate = static_cast<int>((flags >> kBitrateShift) & 3u);
    return {duration == 1u ? 60 : duration == 2u ? 120 : 180, bitrate_at(bitrate)};
}
} // namespace screen_capture
