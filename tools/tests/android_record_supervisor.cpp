#include "bw_obf.h"
#include "record_capture.h"
#include "screen_capture.h"

#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <unistd.h>

const char* CfgDir() { return "/data/local/tmp/"; }

#ifdef BW_OBF_DIAGNOSTIC
extern "C" volatile uint64_t bw_obf_diagnostic[8];
#if defined(__clang__)
__attribute__((noinline, optnone))
#elif defined(__GNUC__)
__attribute__((noinline, optimize("O0")))
#endif
static void print_hex(uint64_t value) {
    char line[17];
    for (int i = 15; i >= 0; --i) {
        const unsigned digit = value & 15;
        line[i] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value >>= 4;
    }
    line[16] = '\n';
    const ssize_t ignored = write(STDERR_FILENO, line, sizeof(line));
    (void)ignored;
}
#endif

int main(int argc, char** argv) {
#ifdef BW_OBF_DIAGNOSTIC
    for (int i = 0; i < 8; ++i) print_hex(bw_obf_diagnostic[i]);
    print_hex(bw::kKey);
#endif
    BW_GUARD();
    if (argc == 4 && std::strcmp(argv[1], "--capture-record") == 0) {
        char *fd_end = nullptr, *rate_end = nullptr;
        const long fd = std::strtol(argv[2], &fd_end, 10);
        const long rate = std::strtol(argv[3], &rate_end, 10);
        if (!fd_end || *fd_end || !rate_end || *rate_end || fd < 3 || fd > INT_MAX || rate < 0 || rate > INT_MAX)
            return 2;
        return record_capture::Run(static_cast<int>(fd), static_cast<int>(rate));
    }
    if (argc != 3 || (std::strcmp(argv[2], "stop") && std::strcmp(argv[2], "death"))) {
        std::fprintf(stderr, "supervisor args: argc=%d arg1=%s arg2=%s\n", argc,
                     argc > 1 ? argv[1] : "<missing>", argc > 2 ? argv[2] : "<missing>");
        return 2;
    }
    std::signal(SIGCHLD, SIG_IGN);
    const std::string directory = argv[1];
    screen_capture::Job job;
    if (job.start(screen_capture::Kind::Video, "/proc/self/exe", directory, directory + "/capture.log", {4}) !=
        screen_capture::StartResult::Started) return 3;
    sleep(4);
    if (!std::strcmp(argv[2], "death")) _exit(0);
    if (!job.stop()) return 4;
    for (int i = 0; i < 200; ++i) {
        screen_capture::Event event;
        if (job.poll(event)) {
            std::printf("RESULT %d %d %s\n", event.success, event.exit_code, event.path.c_str());
            return event.success ? 0 : 5;
        }
        usleep(100000);
    }
    return 6;
}
