#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>

// Small, platform-independent decisions shared by the farm controller and its
// regression tests. A missing ray or a tiny fraction is not proof of death.
namespace farm_control {

inline bool health_depleted(bool readable, float health) {
    return readable && health <= 0.0f;
}

inline bool safe_to_swing(bool aim_settled, bool ray_valid, bool ray_self,
                          bool ray_foreign, bool ray_blocked, bool node_dying) {
    return aim_settled && ray_valid && ray_self && !ray_foreign &&
           !ray_blocked && !node_dying;
}

using Blacklist = std::unordered_map<std::uint64_t, double>;

inline void blacklist(Blacklist& entries, std::uint64_t id, double now, double seconds) {
    if (id && seconds > 0.0) entries[id] = now + std::max(seconds, 1.0);
}

inline void expire(Blacklist& entries, double now) {
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->second <= now) it = entries.erase(it);
        else ++it;
    }
}

// Prefer an already-harvested, still-eligible target regardless of another
// node's distance. Otherwise compare the existing distance-based scores.
inline bool better_target(bool preferred, float score,
                          bool best_preferred, float best_score) {
    if (preferred != best_preferred) return preferred;
    return score < best_score;
}

// A press is kept alive every frame, including after movement release. A
// failed write is retried; any loss of aim/ray safety releases immediately,
// even in the middle of the normal pulse. Failed releases are retried too.
class Swing {
public:
    template <typename Write>
    void stop(Write&& write) {
        remaining_ms_ = 0;
        if (down_ && write(false)) down_ = false;
    }

    template <typename Write>
    void tick(int elapsed_ms, bool allowed, bool continuous,
              int hold_ms, int idle_ms, Write&& write) {
        if (!allowed) { stop(write); return; }
        if (continuous) {
            if (write(true)) down_ = true;
            remaining_ms_ = 0;
            return;
        }

        remaining_ms_ -= std::max(elapsed_ms, 0);
        if (down_) {
            if (remaining_ms_ <= 0) {
                if (write(false)) {
                    down_ = false;
                    remaining_ms_ = std::max(idle_ms, 0);
                }
            } else {
                (void)write(true);
            }
        } else if (remaining_ms_ <= 0) {
            if (write(true)) {
                down_ = true;
                remaining_ms_ = std::max(hold_ms, 1);
            } else {
                remaining_ms_ = 0;
            }
        }
    }

    bool down() const { return down_; }

private:
    bool down_ = false;
    int remaining_ms_ = 0;
};
} // namespace farm_control
