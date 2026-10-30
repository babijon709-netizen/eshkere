#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace magicfarm {
constexpr std::uint64_t kMagic = 0x314d52414657424dULL;
constexpr std::uint32_t kAbi = 1;
constexpr std::uint64_t kLeaseNs = 200000000ULL;
enum class State : std::uint32_t {
    Idle, Loading, Ready, Sent, OutOfReach, Blocked, NoTool, NoTarget,
    WrongBuild, NoAccess, LoadFailed, HookFailed, InvalidCommand, Exception,
    NoProgress, Moving, Stuck, Unavailable, ProtectedClient
};
struct Vec { float x{}, y{}, z{}; };
struct Command {
    std::uint64_t deadline_ns{};
    std::uint32_t net_id{}, enabled{};
    Vec point{};
    std::uint32_t reserved{};
};
struct alignas(8) Shared {
    std::uint64_t magic{kMagic};
    std::uint32_t abi{kAbi}, size{sizeof(Shared)};
    alignas(4) std::uint32_t sequence{};
    Command command{};
    alignas(4) std::uint32_t state{static_cast<std::uint32_t>(State::Idle)};
    std::uint32_t sent{}, last_net_id{};
    float hit_distance{};
};
static_assert(sizeof(Command) == 32 && offsetof(Shared, command) == 24);
inline bool finite(float f) {
    std::uint32_t b; std::memcpy(&b, &f, sizeof(b));
    return (b & 0x7f800000u) != 0x7f800000u;
}
inline bool valid(const Command& c, std::uint64_t now) {
    return c.enabled == 1 && c.net_id && c.deadline_ns > now &&
           c.deadline_ns - now <= kLeaseNs &&
           finite(c.point.x) && finite(c.point.y) && finite(c.point.z) &&
           std::fabs(c.point.x) < 100000.f && std::fabs(c.point.y) < 100000.f &&
           std::fabs(c.point.z) < 100000.f;
}
inline bool ready(State s) {
    return s == State::Ready || s == State::Sent || s == State::OutOfReach ||
           s == State::Blocked || s == State::NoTarget || s == State::NoTool ||
           s == State::Idle || s == State::InvalidCommand;
}
// A failed or interrupted writer leaves an odd sequence, which readers reject.
// Recover it by committing an empty command before publishing another lease.
template<class Memory>
bool publish_command(Memory& memory,std::uintptr_t address,std::uint32_t sequence,const Command& command) {
    const auto seq_address=address+offsetof(Shared,sequence);
    const auto cmd_address=address+offsetof(Shared,command);
    const Command empty{};
    if (sequence&1) {
        const auto recovered=sequence+1;
        if (!memory.write(cmd_address,&empty,sizeof(empty)) ||
            !memory.write(seq_address,&recovered,sizeof(recovered))) return false;
        sequence=recovered;
    }
    const auto odd=sequence+1,even=odd+1;
    if (!memory.write(seq_address,&odd,sizeof(odd))) return false;
    if (!memory.write(cmd_address,&command,sizeof(command))) {
        if (memory.write(cmd_address,&empty,sizeof(empty)))
            (void)memory.write(seq_address,&even,sizeof(even));
        return false;
    }
    return memory.write(seq_address,&even,sizeof(even));
}
inline void movement(float yaw, float magnitude, float& x, float& y) {
    if (!finite(yaw) || !finite(magnitude)) { x = y = 0; return; }
    const float radians = yaw * 0.01745329251994329577f;
    magnitude = std::fmax(0.f, std::fmin(magnitude, 1.f));
    x = std::sin(radians) * magnitude; y = std::cos(radians) * magnitude;
}
} // namespace magicfarm
