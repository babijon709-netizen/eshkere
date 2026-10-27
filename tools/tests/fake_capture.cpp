// Fake screencap and continuous recorder for the real process supervisor.
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
    if (fcntl(75, F_GETFD) != -1 || errno != EBADF) return 43;
    if (argc == 3 && std::strcmp(argv[1], "-p") == 0) {
        FILE* f = std::fopen(argv[2], "wb");
        if (!f) return 2;
        std::fputs("fake png\n", f); std::fclose(f);
        std::fputs("fake screenshot complete\n", stderr);
        return 0;
    }
    if (argc == 4 && std::strcmp(argv[1], "--capture-record") == 0) {
        char* end = nullptr;
        const long fd = std::strtol(argv[2], &end, 10);
        if (!end || *end || fd < 3 || fd > 1000000) return 4;
        if (std::strcmp(argv[3], "0") && std::strcmp(argv[3], "4000000") &&
            std::strcmp(argv[3], "8000000") && std::strcmp(argv[3], "16000000")) return 4;
        struct sigaction sa{};
        sa.sa_handler = on_stop; sigemptyset(&sa.sa_mask); sigaction(SIGINT, &sa, nullptr);
        char fd_name[64], path[4096];
        std::snprintf(fd_name, sizeof(fd_name), "/proc/self/fd/%ld", fd);
        const ssize_t n = readlink(fd_name, path, sizeof(path) - 1);
        if (n < 0) return 3;
        path[n] = '\0';
        FILE* f = fdopen(static_cast<int>(fd), "w");
        if (!f) return 3;
        std::fprintf(f, "video started bitrate=%s\n", std::strcmp(argv[3], "0") ? argv[3] : "auto");
        std::fflush(f);
        if (std::strstr(path, "fake-fail")) { std::fclose(f); return 9; }
        if (std::strstr(path, "fake-early")) { std::fclose(f); return 0; }
        while (!stopped) usleep(10000);
        std::fputs("video finalized\n", f); std::fclose(f);
        return 0;
    }
    return 4;
}
