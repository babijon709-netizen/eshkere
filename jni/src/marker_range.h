#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>

namespace marker_range {

inline bool within_limit(float distance, float limit, bool map_wide) {
    return std::isfinite(distance) && (map_wide || distance <= limit);
}

inline bool is_airdrop_name(const char* raw) {
    if (!raw || !raw[0]) return false;
    char key[96];
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)raw; *p && n + 1 < sizeof(key); ++p) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            key[n++] = (char)c;
    }
    key[n] = '\0';
    return strstr(key, "airdrop") || strstr(key, "supplydrop") ||
           strstr(key, "supplycrate") || strstr(key, "carepackage");
}

}  // namespace marker_range
