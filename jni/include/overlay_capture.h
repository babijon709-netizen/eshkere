#pragma once

#include <cstdint>

// SurfaceFlinger layer flags (not the game window's flags). Android 12+ can
// change these at runtime; older releases need a new layer to change the
// creation-only WINDOW_TYPE_DONT_SCREENSHOT setting.
namespace overlay_capture {
constexpr uint32_t kSkipScreenshot = 0x40u;
constexpr uint32_t kSecure = 0x80u;
constexpr uint32_t kCaptureMask = kSkipScreenshot | kSecure;
constexpr uint32_t flags_for_visible(bool visible) {
    return visible ? 0u : kSkipScreenshot;
}

// Bit 17 in CfgExt2Trailer is inverted so old configs (where it is zero)
// retain the original behavior: the overlay is eligible for full-screen capture.
constexpr uint32_t kConfigExclude = 1u << 17;
constexpr uint32_t config_flag(bool visible) { return visible ? 0u : kConfigExclude; }
constexpr bool visible_from_config(uint32_t flags) { return (flags & kConfigExclude) == 0; }

// Track the last successfully requested policy. A failed update must not make
// the switch claim success; leave the old layer and its state intact instead.
class Mode {
public:
    bool visible() const { return visible_; }
    void reset() { visible_ = true; }

    template <typename UpdateFlags, typename RecreateLayer>
    bool set(bool visible, UpdateFlags update_flags, RecreateLayer recreate_layer) {
        if (visible == visible_) return true;
        if (!update_flags(visible) && !recreate_layer(visible)) return false;
        visible_ = visible;
        return true;
    }

private:
    bool visible_ = true;
};
} // namespace overlay_capture
