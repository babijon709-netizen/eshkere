#pragma once

#include <sys/resource.h>
#include <sys/prctl.h>

namespace process_hardening {
// Kernel core dumps are separate from ptrace or privileged memory access.
// A root debugger or kernel module is outside this control's guarantees.
inline bool DisableCoreFiles() noexcept {
    const rlimit limit{0, 0};
    if (::setrlimit(RLIMIT_CORE, &limit) != 0) return false;
    rlimit actual{};
    return ::getrlimit(RLIMIT_CORE, &actual) == 0 &&
           actual.rlim_cur == 0 && actual.rlim_max == 0;
}

// Apply before accepting input and again after credential setup, which can
// reset dumpability. This constrains ordinary OS access, not CAP_SYS_PTRACE
// holders, kernel drivers, or a modified copy of the executable.
inline bool Apply() noexcept {
    if (!DisableCoreFiles()) return false;
    if (::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) return false;
    return ::prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) == 0;
}
} // namespace process_hardening
