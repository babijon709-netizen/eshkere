#pragma once

#include <cstdint>
#include <sys/types.h>

#if defined(__aarch64__)
#include <asm/ptrace.h>
#include <dirent.h>
#include <elf.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#endif

namespace weapon_skin_refresh {

#if defined(__aarch64__)
inline pid_t find_unity_main(pid_t pid) {
    char path[96];
    std::snprintf(path, sizeof(path), "/proc/%d/task", static_cast<int>(pid));
    DIR* dir = ::opendir(path);
    if (!dir) return -1;
    pid_t found = -1;
    dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        const pid_t tid = static_cast<pid_t>(std::strtol(entry->d_name, nullptr, 10));
        if (tid <= 0) continue;
        char comm_path[128];
        std::snprintf(comm_path, sizeof(comm_path), "/proc/%d/task/%d/comm",
                      static_cast<int>(pid), static_cast<int>(tid));
        const int fd = ::open(comm_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        char comm[32]{};
        const ssize_t n = ::read(fd, comm, sizeof(comm) - 1);
        ::close(fd);
        if (n <= 0) continue;
        while (n > 0 && (comm[n - 1] == '\n' || comm[n - 1] == '\r')) comm[n - 1] = '\0';
        if (std::strcmp(comm, "UnityMain") == 0) {
            if (found > 0) { found = -1; break; }
            found = tid;
        }
    }
    ::closedir(dir);
    return found;
}

struct MapRange {
    uint64_t begin = 0;
    uint64_t end = 0;
    char perms[5]{};
    std::string path;
};

inline bool find_map(pid_t pid, uint64_t address, MapRange& out) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", static_cast<int>(pid));
    std::ifstream maps(path);
    if (!maps) return false;
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long begin = 0, end = 0;
        char perms[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &begin, &end, perms) != 3) continue;
        if (address < begin || address >= end) continue;
        out.begin = static_cast<uint64_t>(begin);
        out.end = static_cast<uint64_t>(end);
        std::memcpy(out.perms, perms, sizeof(out.perms));
        const std::size_t path_at = line.find('/');
        if (path_at != std::string::npos) out.path = line.substr(path_at);
        return true;
    }
    return false;
}

inline bool wait_for_stop(pid_t tid, int timeout_ms, int& status) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        const pid_t result = ::waitpid(tid, &status, __WALL | WNOHANG);
        if (result == tid) return WIFSTOPPED(status);
        if (result < 0 && errno != EINTR) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

inline bool wait_for_stop_blocking(pid_t tid, int& status) {
    pid_t result = -1;
    do { result = ::waitpid(tid, &status, __WALL); } while (result < 0 && errno == EINTR);
    return result == tid && WIFSTOPPED(status);
}
#endif

template<class IO>
bool invoke_fp_manager_reload(IO& io, pid_t pid, uint64_t module_base, uint64_t function_rva,
                              uint64_t manager, uint64_t piece_address, int32_t slot, int16_t skin) {
#if !defined(__aarch64__)
    (void)io; (void)pid; (void)module_base; (void)function_rva;
    (void)manager; (void)piece_address; (void)slot; (void)skin;
    return false;
#else
    if (pid <= 0 || module_base < 0x10000 || function_rva < 0x10000 ||
        manager < 0x10000 || piece_address < 0x10000 || slot < -1 || slot > 255)
        return false;

    const pid_t tid = find_unity_main(pid);
    if (tid <= 0) return false;
    const uint64_t target = module_base + function_rva;
    MapRange code_map{};
    if (target < module_base || !find_map(pid, target, code_map) ||
        code_map.perms[2] != 'x' || code_map.path.find("libil2cpp.so") == std::string::npos)
        return false;

    constexpr uint64_t kReturnFault = 0x1000;
    constexpr uint64_t kStackReserve = 0x2000;
    constexpr uint64_t kPieceOffset = 0x400;
    constexpr std::size_t kWeaponPieceBytes = 0x18;

    if (::ptrace(PTRACE_SEIZE, tid, nullptr, nullptr) != 0) return false;
    bool attached = true;
    bool stopped = false;
    bool saved = false;
    bool fp_saved = false;
    user_pt_regs original{};
    uint8_t fp_state[1024]{};
    std::size_t fp_state_size = 0;
    auto detach = [&](int signal) {
        if (!attached) return;
        if (stopped && saved) {
            iovec regs_iov{&original, sizeof(original)};
            (void)::ptrace(PTRACE_SETREGSET, tid, reinterpret_cast<void*>(NT_PRSTATUS), &regs_iov);
            if (fp_saved && fp_state_size) {
                iovec fp_iov{fp_state, fp_state_size};
                (void)::ptrace(PTRACE_SETREGSET, tid, reinterpret_cast<void*>(NT_PRFPREG), &fp_iov);
            }
        }
        if (stopped) {
            (void)::ptrace(PTRACE_DETACH, tid, nullptr,
                           reinterpret_cast<void*>(static_cast<intptr_t>(signal)));
            attached = false;
            stopped = false;
        }
    };

    if (::ptrace(PTRACE_INTERRUPT, tid, nullptr, nullptr) != 0) {
        (void)::ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
        return false;
    }
    int status = 0;
    if (!wait_for_stop(tid, 250, status)) {
        (void)::ptrace(PTRACE_INTERRUPT, tid, nullptr, nullptr);
        if (wait_for_stop_blocking(tid, status)) {
            stopped = true;
            detach(0);
        } else {
            attached = false; // the target thread exited while being seized
        }
        return false;
    }
    stopped = true;

    iovec regs_iov{&original, sizeof(original)};
    if (::ptrace(PTRACE_GETREGSET, tid, reinterpret_cast<void*>(NT_PRSTATUS), &regs_iov) != 0 ||
        regs_iov.iov_len < sizeof(original)) {
        detach(0);
        return false;
    }
    saved = true;
    iovec fp_iov{fp_state, sizeof(fp_state)};
    if (::ptrace(PTRACE_GETREGSET, tid, reinterpret_cast<void*>(NT_PRFPREG), &fp_iov) != 0 ||
        fp_iov.iov_len == 0 || fp_iov.iov_len > sizeof(fp_state)) {
        detach(0);
        return false;
    }
    fp_state_size = fp_iov.iov_len;
    fp_saved = true;

    MapRange stack_map{};
    if (!find_map(pid, original.sp, stack_map) || stack_map.perms[1] != 'w' ||
        original.sp < stack_map.begin + kStackReserve + kPieceOffset + kWeaponPieceBytes) {
        detach(0);
        return false;
    }
    const uint64_t remote_sp = (original.sp - kStackReserve) & ~uint64_t(0xF);
    const uint64_t remote_piece = remote_sp + kPieceOffset;
    if (remote_sp < stack_map.begin || remote_piece + kWeaponPieceBytes > original.sp) {
        detach(0);
        return false;
    }

    uint8_t piece[kWeaponPieceBytes]{};
    if (!io.read_bytes(piece_address, piece, sizeof(piece))) {
        detach(0);
        return false;
    }
    std::memcpy(piece + 0x04, &skin, sizeof(skin));
    if (!io.write_bytes(remote_piece, piece, sizeof(piece))) {
        detach(0);
        return false;
    }

    user_pt_regs call = original;
    call.regs[0] = manager;
    call.regs[1] = remote_piece; // WeaponPiece is a 24-byte value passed by reference on AArch64.
    call.regs[2] = static_cast<uint64_t>(static_cast<int64_t>(slot));
    call.regs[3] = 0; // non-generic IL2CPP method-info argument
    call.regs[30] = kReturnFault;
    call.sp = remote_sp;
    call.pc = target;
    iovec call_iov{&call, sizeof(call)};
    if (::ptrace(PTRACE_SETREGSET, tid, reinterpret_cast<void*>(NT_PRSTATUS), &call_iov) != 0 ||
        ::ptrace(PTRACE_CONT, tid, nullptr, nullptr) != 0) {
        detach(0);
        return false;
    }
    stopped = false;

    bool returned = false;
    int stop_signal = 0;
    if (wait_for_stop(tid, 200, status)) {
        stopped = true;
        if (WSTOPSIG(status) == SIGSEGV) {
            user_pt_regs after{};
            iovec after_iov{&after, sizeof(after)};
            if (::ptrace(PTRACE_GETREGSET, tid, reinterpret_cast<void*>(NT_PRSTATUS), &after_iov) == 0 &&
                after.pc == kReturnFault) {
                returned = true;
            } else {
                stop_signal = SIGSEGV;
            }
        } else if (!(WSTOPSIG(status) == SIGTRAP && (status >> 16) == PTRACE_EVENT_STOP)) {
            stop_signal = WSTOPSIG(status);
        }
    } else {
        // Bound normal latency, then interrupt and recover the original context
        // if the async request unexpectedly takes longer than expected.
        (void)::ptrace(PTRACE_INTERRUPT, tid, nullptr, nullptr);
        if (wait_for_stop_blocking(tid, status)) {
            stopped = true;
            if (WSTOPSIG(status) == SIGTRAP && (status >> 16) == PTRACE_EVENT_STOP)
                stop_signal = 0;
            else if (WSTOPSIG(status) != SIGSTOP)
                stop_signal = WSTOPSIG(status);
        }
    }
    if (!stopped) { attached = false; return false; }
    detach(stop_signal);
    return returned;
#endif
}

} // namespace weapon_skin_refresh
