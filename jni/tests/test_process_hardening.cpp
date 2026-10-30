#include "process_hardening.h"
#include <cassert>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    // The irreversible limit belongs in a child, not the test runner.
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        if (!process_hardening::Apply()) _exit(1);
        rlimit limit{};
        if (getrlimit(RLIMIT_CORE, &limit) != 0 || limit.rlim_cur || limit.rlim_max) _exit(2);
        if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) != 0) _exit(3);
        // Credential operations may restore dumpability. Reapplying must
        // actually reset the kernel state, rather than trusting a cached flag.
        if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) != 0) _exit(4);
        if (!process_hardening::Apply()) _exit(5);
        if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0) != 0) _exit(6);
        _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
