// Host/Android harness for the same blocking gate used by the production main.
// The marker is test-only and appears only after a successful verification.
#include "bw_obf.h"
#include "startup_key.h"
#include <csignal>
#ifdef STARTUP_MT_TEST
#include <fcntl.h>
#include <unistd.h>
#endif

static void ignore_user_signal(int) {}

#ifdef STARTUP_MT_TEST
// The emulator creates this file with mode 0666. It independently records the
// result even if MT's terminal closes before its final output is rendered.
static void report_mt_result(const char* result, size_t length) {
    int fd = open("/data/local/tmp/benzware-mt-result", O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd >= 0) {
        (void)write(fd, result, length);
        close(fd);
    }
}
#endif

int main() {
    BW_GUARD();
    // Let the harness interrupt a pending read/poll without terminating it.
    std::signal(SIGUSR1, ignore_user_signal);
    const auto input = startup_key::Request();
#ifdef STARTUP_MT_TEST
    if (input == startup_key::InputResult::Accepted)
        report_mt_result("accepted", 8);
    else
        report_mt_result("rejected", 8);
#endif
    if (input != startup_key::InputResult::Accepted) {
#ifdef STARTUP_MT_TEST
        sleep(1); // allow MT to render the error before the test process exits
#endif
        return 1;
    }
    std::cout << "KEY_ACCEPTED\n" << std::flush;
#ifdef STARTUP_MT_TEST
    sleep(1); // the production process also remains alive after accepting a key
#endif
    return 0;
}
