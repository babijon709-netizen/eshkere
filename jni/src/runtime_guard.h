#pragma once
// Map-snapshot readiness and loader detection are separate concerns.
// libloader.so is recorded for diagnostics. Its presence does not establish
// whether the selected memory transport accepts a write.
// Android still requires a complete maps snapshot for the bound PID.

#include "maps_lookup.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace runtime_guard {

enum class Policy : unsigned { Unknown = 0, Clear = 1, Protected = 2 };

namespace detail {
inline std::atomic<std::uint64_t>& state() {
    static std::atomic<std::uint64_t> s{0};
    return s;
}
inline std::uint64_t pack(int pid, Policy p) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pid)) << 2) |
           static_cast<unsigned>(p);
}
inline int pid_of(std::uint64_t s) {
    return static_cast<int>(static_cast<std::uint32_t>(s >> 2));
}
inline Policy policy_of(std::uint64_t s) {
    return static_cast<Policy>(s & 3u);
}
} // namespace detail

inline bool maps_have_loader(const std::string& text) {
    return maps::lookup_library_base(text, "libloader.so") != 0;
}

// A complete maps snapshot is required to call a target clear. Truncated or
// empty input stays Unknown, which is write-blocking on Android.
inline void observe_maps(int pid, const std::string& text, bool complete = true) {
    if (pid <= 0) return;
    Policy p = Policy::Unknown;
    if (complete && !text.empty())
        p = maps_have_loader(text) ? Policy::Protected : Policy::Clear;
    detail::state().store(detail::pack(pid, p), std::memory_order_release);
}

inline void reset() { detail::state().store(0, std::memory_order_release); }

inline Policy policy(int pid) {
    const auto s = detail::state().load(std::memory_order_acquire);
    return detail::pid_of(s) == pid ? detail::policy_of(s) : Policy::Unknown;
}
inline bool is_protected(int pid) { return policy(pid) == Policy::Protected; }

inline bool snapshot_allows_writes(Policy p) {
    return p == Policy::Clear || p == Policy::Protected;
}

inline bool writes_allowed(int pid) {
    if(pid <= 0) return false;
#ifdef __ANDROID__
    return snapshot_allows_writes(policy(pid));
#else
    // Host memory rigs have no target procfs; exercise the Android snapshot
    // rule independently through snapshot_allows_writes().
    return true;
#endif
}

// Read /proc/<pid>/maps once and publish the policy. Returns false when the
// snapshot could not be read completely; callers must treat that as Unknown.
inline bool refresh_proc(int pid) {
    if (pid <= 0) return false;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    std::FILE* f = std::fopen(path, "r");
    if (!f) { observe_maps(pid, "", false); return false; }
    std::string text;
    char* line = nullptr;
    std::size_t cap = 0;
    bool complete = true;
    for (;;) {
        const auto n = ::getline(&line, &cap, f);
        if (n < 0) break;
        text.append(line, static_cast<std::size_t>(n));
        if (text.size() > (8u << 20)) { complete = false; break; }
    }
    std::free(line);
    std::fclose(f);
    observe_maps(pid, text, complete);
    return complete && !text.empty();
}

} // namespace runtime_guard
