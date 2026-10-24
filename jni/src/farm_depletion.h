#pragma once

#include <cmath>

namespace farm_depletion {

// The controller stops issuing swings at this point already. Treat the same
// terminal-health signal as depletion so it doesn't keep steering toward the
// stale node while fractionRemaining catches up or the registry rescans.
inline bool terminal_health(float health, float maximum) {
    return std::isfinite(health) && std::isfinite(maximum) &&
           health >= 0.0f && maximum > 0.0f && health <= 0.02f * maximum;
}

}  // namespace farm_depletion
