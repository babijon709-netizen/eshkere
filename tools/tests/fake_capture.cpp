// Fake /system/bin/{screencap,screenrecord} for the real process supervisor.
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static volatile sig_atomic_t stopped = 0;
static void on_stop(int) { stopped = 1; }

int main(int argc, char** argv) {
    const char* root = std::getenv("ANDROID_ROOT");
    if (!root || std::strcmp(root, "/system") != 0 || std::getenv("CAPTURE_TEST_SECRET")) return 42;
    // The host leaves fd 75 open without CLOEXEC, like a stray app resource.
    if (fcntl(75, F_GETFD) != -1 || errno != EBADF) return 43;
    if (argc == 3 && std::strcmp(argv[1], "-p") == 0) {
        FILE* f = std::fopen(argv[2], "wb");
        if (!f) return 2;
        std::fputs("fake png\n", f);
        std::fclose(f);
        std::fputs("fake screenshot complete\n", stderr);
        return 0;
    }
    if ((argc == 4 || argc == 6) && std::strcmp(argv[1], "--time-limit") == 0 &&
        (std::strcmp(argv[2], "60") == 0 || std::strcmp(argv[2], "120") == 0 ||
         std::strcmp(argv[2], "180") == 0)) {
        const char* bitrate = "auto";
        const char* output = argv[3];
        if (argc == 6) {
            if (std::strcmp(argv[3], "--bit-rate") != 0 ||
                (std::strcmp(argv[4], "4000000") != 0 && std::strcmp(argv[4], "8000000") != 0 &&
                 std::strcmp(argv[4], "16000000") != 0)) return 4;
            bitrate = argv[4];
            output = argv[5];
        }
        struct sigaction sa{};
        sa.sa_handler = on_stop;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        FILE* f = std::fopen(output, "wb");
        if (!f) return 3;
        std::fprintf(f, "video started duration=%s bitrate=%s\n", argv[2], bitrate);
        std::fflush(f);
        while (!stopped) usleep(10000);
        std::fputs("video finalized\n", f);
        std::fclose(f);
        return 0;
    }
    return 4;
}
