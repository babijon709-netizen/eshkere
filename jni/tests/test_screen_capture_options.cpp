#include "screen_capture_options.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

using namespace screen_capture;

int main() {
    static_assert((kVideoOptionsMask & ((1u << 20) - 1u)) == 0u,
                  "bitrate must not overlap older feature flags");
    static_assert((kVideoOptionsMask & kLegacyDurationMask) == 0u,
                  "old duration bits stay reserved and ignored");
    static_assert(video_config_flags({}) == 0u, "old profiles retain automatic bitrate");
    static_assert(video_from_config(0u).bitrate_mbps == 0, "old bitrate default");

    const int bitrates[] = {0, 4, 8, 16};
    for (int i = 0; i < 4; ++i) {
        assert(bitrate_at(i) == bitrates[i]);
        assert(bitrate_index(bitrates[i]) == i);
        const uint32_t flags = video_config_flags({bitrates[i]});
        assert((flags & ~kVideoOptionsMask) == 0u);
        for (uint32_t legacy_duration = 0; legacy_duration < 4; ++legacy_duration) {
            const auto loaded = video_from_config(flags | (legacy_duration << 18) | (1u << 17) | 0x12345u);
            assert(loaded.bitrate_mbps == bitrates[i]);
        }
    }
    assert(bitrate_at(-1) == 0 && bitrate_at(99) == 0);
    assert(sanitize({99}).bitrate_mbps == 0);
    assert(video_config_flags({99}) == 0u);
    std::puts("PASS: recording bitrate, reserved duration bits and old profiles");
}
