// Host/Android CLI harness for the real nonblocking startup reader. The full
// Android application draws the overlay on the Waiting/Closed path; this
// harness reports that path and models the fork/setsid before further input.
#include "bw_obf.h"
#include "startup_key.h"
#include <sys/wait.h>
#include <unistd.h>

static int result(startup_key::InputResult r) {
    using startup_key::InputResult;
    if (r == InputResult::Accepted) return 0;
    if (r == InputResult::Invalid) {
        std::cerr << "Invalid key. Launch cancelled.\n";
        return 1;
    }
    std::cerr << "GUI fallback required\n";
    return 2;
}

int main() {
    BW_GUARD();
    startup_key::FdReader reader;
    startup_key::PrintPrompt();
    auto input = reader.Poll(STDIN_FILENO, 100);
    if (input == startup_key::InputResult::Accepted || input == startup_key::InputResult::Invalid)
        return result(input);
    std::cerr << "GUI fallback ready\n" << std::flush;
    if (input == startup_key::InputResult::Closed) return result(input);

    const pid_t child = fork();
    if (child < 0) return 3;
    if (child > 0) {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        return WIFEXITED(status) ? WEXITSTATUS(status) : 3;
    }
    (void)setsid();
    do { input = reader.Poll(STDIN_FILENO, 100); }
    while (input == startup_key::InputResult::Waiting);
    const int code = result(input);
    std::cerr.flush();
    _exit(code);
}
