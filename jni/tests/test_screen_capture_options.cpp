#include "screen_capture_options.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace screen_capture;

int main() {
    static_assert((kVideoOptionsMask & ((1u << 18) - 1u)) == 0u,
                  "video settings must not overlap older feature flags");
    static_assert(video_config_flags({}) == 0u, "old profiles retain the old defaults");
    static_assert(video_from_config(0u).duration_seconds == 180, "old duration");
    static_assert(video_from_config(0u).bitrate_mbps == 0, "old automatic bitrate");

    const int durations[] = {60, 120, 180};
    const int bitrates[] = {0, 4, 8, 16};
    for (int i = 0; i < 3; ++i) {
        assert(duration_at(i) == durations[i]);
        assert(duration_index(durations[i]) == i);
        for (int j = 0; j < 4; ++j) {
            assert(bitrate_at(j) == bitrates[j]);
            assert(bitrate_index(bitrates[j]) == j);
            const VideoOptions selected{durations[i], bitrates[j]};
            const uint32_t flags = video_config_flags(selected);
            assert((flags & ~kVideoOptionsMask) == 0u);
            const VideoOptions loaded = video_from_config(flags | (1u << 17) | 0x12345u);
            assert(loaded.duration_seconds == selected.duration_seconds);
            assert(loaded.bitrate_mbps == selected.bitrate_mbps);
        }
    }
    assert(duration_at(-1) == 180 && duration_at(99) == 180);
    assert(bitrate_at(-1) == 0 && bitrate_at(99) == 0);
    const auto invalid = sanitize({-100, 99});
    assert(invalid.duration_seconds == 180 && invalid.bitrate_mbps == 0);
    assert(video_config_flags({-100, 99}) == 0u);
    const auto reserved = video_from_config((3u << kDurationShift) | (2u << kBitrateShift));
    assert(reserved.duration_seconds == 180 && reserved.bitrate_mbps == 8);
    std::puts("PASS: recording presets, old defaults, config flags and invalid input");
}
