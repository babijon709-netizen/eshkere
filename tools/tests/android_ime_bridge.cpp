// Production bridge with the same early daemon/stealth signal behavior as main.
// The final marker is written before the daemon pipe closes.
#include "startup_ime.h"
#include "stealth.h"
#include <cstdio>
#include <csignal>
#include <unistd.h>

int main() {
    startup_key::FdReader input;
    startup_key::PrintPrompt();
    auto initial = input.Poll(STDIN_FILENO, 100);
    if (initial == startup_key::InputResult::Accepted) {
        std::puts("IME_RESULT=accepted"); return 0;
    }
    if (initial == startup_key::InputResult::Invalid) {
        std::puts("IME_RESULT=invalid"); return 1;
    }
    stealth::DaemonizeEarly();
    stealth::ApplyTracerGuard(); // in particular, SIGCHLD is ignored here
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    const startup_ime::Result result = startup_ime::Request(input);
    const char* label = result == startup_ime::Result::Accepted ? "accepted" :
                        result == startup_ime::Result::Cancelled ? "cancelled" :
                        result == startup_ime::Result::Invalid ? "invalid" : "unavailable";
    std::printf("IME_RESULT=%s\n", label);
    std::fflush(stdout);
    return 0;
}
