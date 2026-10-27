// Host/Android harness for the same blocking gate used by the production main.
// The marker is test-only and appears only after a successful verification.
#include "bw_obf.h"
#include "startup_key.h"
#include <csignal>

static void ignore_user_signal(int) {}

int main() {
    BW_GUARD();
    // Let the harness interrupt a pending read/poll without terminating it.
    std::signal(SIGUSR1, ignore_user_signal);
    if (startup_key::Request() != startup_key::InputResult::Accepted) return 1;
    std::cout << "KEY_ACCEPTED\n" << std::flush;
    return 0;
}
