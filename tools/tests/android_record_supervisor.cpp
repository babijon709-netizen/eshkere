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

int main(int argc, char** argv) {
    BW_GUARD();
    if (argc == 4 && std::strcmp(argv[1], "--capture-record") == 0) {
        char *fd_end = nullptr, *rate_end = nullptr;
        const long fd = std::strtol(argv[2], &fd_end, 10);
        const long rate = std::strtol(argv[3], &rate_end, 10);
        if (!fd_end || *fd_end || !rate_end || *rate_end || fd < 3 || fd > INT_MAX || rate < 0 || rate > INT_MAX)
            return 2;
        return record_capture::Run(static_cast<int>(fd), static_cast<int>(rate));
    }
    if (argc != 3 || (std::strcmp(argv[2], "stop") && std::strcmp(argv[2], "death"))) return 2;
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
