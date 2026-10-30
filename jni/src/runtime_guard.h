#pragma once
// Map-snapshot readiness and loader detection are separate concerns.
// libloader.so is recorded for diagnostics. Its presence does not establish
// whether the selected memory transport accepts a write.
// Android still requires a complete maps snapshot for the bound PID.

#include "maps_lookup.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

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
inline void observe_loader(int pid, bool has_loader, bool complete = true) {
    if (pid <= 0) return;
    Policy p = Policy::Unknown;
    if (complete) p = has_loader ? Policy::Protected : Policy::Clear;
    detail::state().store(detail::pack(pid, p), std::memory_order_release);
}

inline void observe_maps(int pid, const std::string& text, bool complete = true) {
    if (pid <= 0) return;
    const bool usable = complete && !text.empty();
    observe_loader(pid, usable && maps_have_loader(text), usable);
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

namespace detail {
// Строка карты: «start-end perms offset dev inode путь». Нужен только последний
// сегмент пути — с той же строгостью, что maps::lookup_library_base:
// «mylibloader.so», «libloader.so.bak» и «libloaderXso» — это НЕ он.
inline bool line_is_lib_loader(const char* line, std::size_t len) {
    static const char kLib[] = "libloader.so";
    constexpr std::size_t kLibLen = sizeof(kLib) - 1;
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' '))
        --len;
    static const char kDeleted[] = " (deleted)";
    constexpr std::size_t kDeletedLen = sizeof(kDeleted) - 1;
    if (len > kDeletedLen && std::memcmp(line + len - kDeletedLen, kDeleted, kDeletedLen) == 0)
        len -= kDeletedLen;
    if (len < kLibLen) return false;
    if (std::memcmp(line + len - kLibLen, kLib, kLibLen) != 0) return false;
    if (len > kLibLen) {
        const char prev = line[len - kLibLen - 1];
        if (prev != '/' && prev != '!') return false;
    }
    return true;
}
} // namespace detail

// Read /proc/<pid>/maps once and publish the policy. Returns false when the
// snapshot could not be read completely; callers must treat that as Unknown.
//
// Читаем ПОТОКОВО, а не «весь файл в std::string». Карта игры — сотни килобайт
// (у Unity это тысячи отображений: куча, стеки сотни потоков, dex, asset-бандлы),
// и прежний вариант собирал её через getline + append в растущую строку: на
// каждый вызов это десятки реаллокаций и копий, то есть единицы-десятки
// миллисекунд на потоке кадра (плюс до 8 МБ памяти). Здесь один 64-килобайтный
// буфер и построчный разбор на месте: ни аллокаций, ни копий.
inline bool refresh_proc(int pid) {
    if (pid <= 0) return false;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { observe_maps(pid, "", false); return false; }

    static constexpr std::size_t kChunk = 64u << 10;
    static constexpr std::size_t kLineMax = 1024;   // длиннее — не наша строка
    char chunk[kChunk];
    char line[kLineMax];
    std::size_t line_len = 0;
    bool line_overflow = false;
    bool found = false;
    bool any = false;
    bool complete = true;
    std::size_t total = 0;
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            complete = false;
            break;
        }
        if (n == 0) break;
        any = true;
        total += static_cast<std::size_t>(n);
        if (total > (8u << 20)) { complete = false; break; }
        for (ssize_t i = 0; i < n; ++i) {
            const char c = chunk[i];
            if (c == '\n') {
                if (!found && !line_overflow && detail::line_is_lib_loader(line, line_len))
                    found = true;
                line_len = 0;
                line_overflow = false;
                continue;
            }
            if (line_len < kLineMax) line[line_len++] = c;
            else line_overflow = true;
        }
    }
    if (complete && !found && !line_overflow && line_len > 0 &&
        detail::line_is_lib_loader(line, line_len))
        found = true;   // последняя строка без '\n'
    ::close(fd);
    if (!complete) { observe_maps(pid, "", false); return false; }
    observe_loader(pid, found, any);
    return any;
}

} // namespace runtime_guard
