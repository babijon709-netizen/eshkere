#pragma once
#include "network_aa.h"

namespace netaa {
enum class ExternalState : int { Off, Waiting, Written, Raced, ReadFailed, WriteFailed, WrongBuild };

// Memory adapter uses the project's selected transport, with uncached reads.
// Only the 24-byte movement/look/yaw prefix is writable: no bools or Tick.
class External {
    static constexpr size_t kPrefix = offsetof(PlayerInput, jump);
    PlayerInput original_{}, last_{};
    uint64_t address_ = 0;
    bool owned_ = false;
    PhaseCache phase_{};
    uint64_t last_now_ = 0, tick_at_ = 0;
    uint32_t tick_ = 0;
    bool have_tick_ = false;
    uint32_t writes_ = 0, restores_ = 0, races_ = 0;
    static bool prefix_equal(const PlayerInput& a, const PlayerInput& b) {
        return a.tick == b.tick && std::memcmp(&a, &b, kPrefix) == 0;
    }
public:
    ExternalState state = ExternalState::Off;
    bool pending() const { return owned_; }
    uint32_t writes() const { return writes_; }
    uint32_t restores() const { return restores_; }
    uint32_t races() const { return races_; }
    // Changing PID/base/local-player/handler invalidates ownership. Never
    // restore an old process's snapshot into a new session/address.
    void forget() { owned_ = false; address_ = 0; phase_ = {}; have_tick_ = false; state = ExternalState::Off; }

    template<class Memory>
    void update(Memory& mem, uint64_t address, bool enabled,
                const Command& config, uint64_t now) {
        if (address_ != address) forget();
        address_ = address;
        if (now < last_now_) phase_ = {};
        last_now_ = now;
        if (!enabled && !owned_) { state = ExternalState::Off; return; }
        PlayerInput observed{};
        if (!mem.read(address, &observed, sizeof(observed))) {
            state = ExternalState::ReadFailed; return;
        }
        if (!have_tick_ || observed.tick != tick_ || now < tick_at_) {
            tick_ = observed.tick; tick_at_ = now; have_tick_ = true;
        }
        // A stopped input producer is not a live outgoing packet stream.
        if (now >= tick_at_ && now-tick_at_ > 250) enabled = false;
        const bool ours = owned_ && prefix_equal(observed, last_);
        // A fresh game write is authoritative, even at the same Tick.
        if (!ours) owned_ = false;
        PlayerInput baseline = observed;
        if (ours) std::memcpy(&baseline, &original_, kPrefix);
        PlayerInput desired = baseline;
        if (enabled && !rewrite(baseline, config, desired, phase_.phase(baseline.tick, now)))
            enabled = false;
        if (!enabled && !ours) { state = ExternalState::Off; return; }
        if (std::memcmp(&observed, &desired, kPrefix) == 0) {
            state = enabled ? ExternalState::Written : ExternalState::Off; return;
        }
        // Narrow the TOCTOU window; this is NOT a remote atomic compare/exchange.
        PlayerInput check{};
        if (!mem.read(address, &check, sizeof(check))) { state = ExternalState::ReadFailed; return; }
        if (std::memcmp(&check, &observed, sizeof(check)) != 0) {
            ++races_; owned_ = false; state = ExternalState::Raced; return;
        }
        if (!mem.write(address, &desired, kPrefix)) {
            state = ExternalState::WriteFailed; return;
        }
        if (enabled) {
            original_ = baseline; last_ = desired; owned_ = true; ++writes_;
            state = ExternalState::Written;
        } else {
            owned_ = false; ++restores_; state = ExternalState::Off;
        }
    }
};
} // namespace netaa
