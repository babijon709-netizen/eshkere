#pragma once
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace netaa {
namespace off {
// dump11978.7z: read-only version fingerprint; no executable-memory writes.
constexpr uintptr_t kPlayerInputWriter = 0x7494290;
constexpr uint8_t kPrologue[] = {0xfe,0x0f,0x1e,0xf8,0xf4,0x4f,0x01,0xa9,
    0x20,0x04,0x40,0x2d,0xf3,0x03,0x01,0xaa,0xe1,0x03,0x1f,0xaa,
    0xf4,0x03,0x00,0xaa,0x10,0xbd,0x51,0x95,0x60,0x06,0x41,0x2d};
}
constexpr float kDefaultSpeed = 360.f;
enum class YawMode : uint32_t { Spin, Jitter, Backwards };
enum class PitchMode : uint32_t { Keep, Up, Down };

// Explicit bytes for managed bools; native padding is preserved in the copy.
struct PlayerInput {
    float movement[2];
    float look[3];
    float yaw;
    uint8_t jump, sprint, crouch, aim, ready;
    uint8_t padding[3];
    uint32_t tick;
    uint8_t too_close;
    uint8_t tail[3];
};
static_assert(std::is_trivially_copyable<PlayerInput>::value, "packet copy");
static_assert(sizeof(PlayerInput) == 0x28 && offsetof(PlayerInput, look) == 8 &&
    offsetof(PlayerInput, yaw) == 0x14 && offsetof(PlayerInput, tick) == 0x20 &&
    offsetof(PlayerInput, too_close) == 0x24, "11978 PlayerInput ABI");
struct Command {
    uint32_t flags = 0; // enabled, reverse; all other bits rejected
    float speed = kDefaultSpeed;
    YawMode yaw_mode = YawMode::Spin;
    PitchMode pitch_mode = PitchMode::Keep;
};
// Reuses the previous 16-byte SPN1 profile trailer without shifting old data.
// Bits 2..3: yaw mode; bits 4..5: pitch mode. Legacy files have both zero.
struct Profile {
    uint32_t magic = 0x314e5053u, version = 1, flags = 0;
    float degrees_per_second = kDefaultSpeed;
};
static_assert(sizeof(Profile) == 16, "stable profile trailer");
inline bool valid(const Profile& value) {
    uint32_t bits; std::memcpy(&bits, &value.degrees_per_second, 4);
    return value.magic == 0x314e5053u && value.version == 1 &&
        (value.flags & ~63u) == 0 && ((value.flags >> 2) & 3u) <= 2 &&
        ((value.flags >> 4) & 3u) <= 2 && (bits & 0x7f800000u) != 0x7f800000u &&
        value.degrees_per_second >= 30.f && value.degrees_per_second <= 1440.f;
}
inline bool finite(float value) {
    uint32_t bits; std::memcpy(&bits, &value, 4);
    return (bits & 0x7f800000u) != 0x7f800000u;
}
inline float speed(float value) {
    return finite(value) && value >= 30.f && value <= 1440.f ? value : kDefaultSpeed;
}
inline bool valid(const Command& c) {
    return (c.flags & ~3u) == 0 && finite(c.speed) && c.speed >= 30.f && c.speed <= 1440.f &&
        static_cast<uint32_t>(c.yaw_mode) <= 2 && static_cast<uint32_t>(c.pitch_mode) <= 2;
}
inline float wrap(float angle) {
    float v = std::fmod(angle + 180.f, 360.f);
    return (v < 0.f ? v + 360.f : v) - 180.f;
}

// Cache first-seen monotonic time per tick so retransmits have the same phase.
// Bounded storage; old entries expire across reconnects and tick rollover.
struct PhaseCache {
    struct Entry { uint32_t tick = 0; uint64_t at = 0; bool valid = false; };
    Entry entries[128]{};
    uint64_t phase(uint32_t tick, uint64_t now) {
        Entry& e = entries[tick % 128];
        if (!e.valid || e.tick != tick || now < e.at || now-e.at > 1000) {
            e.tick = tick; e.at = now; e.valid = true;
        }
        return e.at;
    }
};

// Pure data transform. Movement is rotated into the fake-yaw basis to preserve its
// world-space direction if the server uses PlayerInput.YRotation as the basis.
inline bool rewrite(const PlayerInput& input, const Command& c, PlayerInput& output, uint64_t phase_ms) {
    if (!valid(c) || !(c.flags & 1u) || input.ready != 1 || input.aim != 0 ||
        input.too_close != 0 || input.jump > 1 || input.sprint > 1 || input.crouch > 1 ||
        !finite(input.yaw) || std::fabs(input.yaw) > 3600.f) return false;
    for (float v : input.look) if (!finite(v)) return false;
    for (float v : input.movement) if (!finite(v) || std::fabs(v) > 1.1f) return false;
    const float movement_norm = input.movement[0]*input.movement[0] + input.movement[1]*input.movement[1];
    if (movement_norm > 1.001f) return false;
    const float norm = input.look[0]*input.look[0] + input.look[1]*input.look[1] + input.look[2]*input.look[2];
    if (!finite(norm) || norm < 0.5f || norm > 1.5f) return false;
    constexpr double radians = 0.017453292519943295;
    float yaw;
    if (c.yaw_mode == YawMode::Spin) {
        const double phase = static_cast<double>(phase_ms) * (1.0 / 1000.0) * c.speed;
        yaw = wrap(static_cast<float>(std::fmod((c.flags & 2u) ? -phase : phase, 360.0)));
    } else if (c.yaw_mode == YawMode::Jitter) {
        yaw = wrap(input.yaw + ((input.tick & 1u) ? 90.f : -90.f));
    } else yaw = wrap(input.yaw + 180.f);
    std::memcpy(&output, &input, sizeof(output));
    // Preserve the real elevation unless explicitly overridden.
    float elevation = std::asin(std::max(-1.f, std::min(1.f, input.look[1] / std::sqrt(norm))));
    if (c.pitch_mode != PitchMode::Keep)
        elevation = static_cast<float>((c.pitch_mode == PitchMode::Up ? 89.0 : -89.0) * radians);
    const float y = static_cast<float>(yaw * radians);
    output.look[0] = std::sin(y) * std::cos(elevation);
    output.look[1] = std::sin(elevation);
    output.look[2] = std::cos(y) * std::cos(elevation);
    const float delta = static_cast<float>((input.yaw - yaw) * radians);
    const float cs = std::cos(delta), sn = std::sin(delta);
    output.movement[0] = input.movement[0]*cs + input.movement[1]*sn;
    output.movement[1] = -input.movement[0]*sn + input.movement[1]*cs;
    output.yaw = yaw;
    return true;
}
} // namespace netaa
