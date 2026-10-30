#pragma once
#include <cstdint>

namespace farm_tool {
inline constexpr std::int32_t kKnownPurposes = 1 | 2 | 4;
// Unity can serialize a Flags field with Everything selected as all bits set.
// Do not confuse that exact sentinel with a failed read or accept other
// unknown bits. Keep the raw value in diagnostics, normalize only for matching.
inline bool valid_purposes(std::int32_t raw) {
    return raw == -1 || (raw > 0 && (raw & ~kKnownPurposes) == 0);
}
inline std::int32_t effective_purposes(std::int32_t raw) {
    return valid_purposes(raw) ? raw & kKnownPurposes : 0;
}
}
