#include "record_mp4.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

static bool read_all(void* data, size_t size) {
    return std::fread(data, 1, size, stdin) == size;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    const int crash_after = argc == 3 ? std::atoi(argv[2]) : 0;
    const int fd = open(argv[1], O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) { std::perror("open"); return 2; }
    uint32_t width = 0, height = 0, csd0_size = 0, csd1_size = 0;
    if (!read_all(&width, 4) || !read_all(&height, 4) || !read_all(&csd0_size, 4) || !read_all(&csd1_size, 4) ||
        csd0_size > 1024 * 1024 || csd1_size > 1024 * 1024) return 2;
    std::vector<uint8_t> csd0(csd0_size), csd1(csd1_size);
    if (!read_all(csd0.data(), csd0.size()) || !read_all(csd1.data(), csd1.size())) return 2;
    record_mp4::Writer writer(fd);
    if (!writer.start(width, height, csd0.data(), csd0.size(), csd1.data(), csd1.size())) {
        std::fprintf(stderr, "start: %s\n", writer.error()); return 3;
    }
    int count = 0;
    for (;;) {
        int64_t pts = 0; uint32_t flags = 0, size = 0;
        const size_t got = std::fread(&pts, 1, sizeof(pts), stdin);
        if (got == 0 && std::feof(stdin)) break;
        if (got != sizeof(pts) || !read_all(&flags, 4) || !read_all(&size, 4) || size > 64 * 1024 * 1024) return 2;
        std::vector<uint8_t> frame(size);
        if (!read_all(frame.data(), frame.size())) return 2;
        if (!writer.sample(frame.data(), frame.size(), pts, (flags & 1) != 0)) {
            std::fprintf(stderr, "sample: %s\n", writer.error()); (void)writer.finish(); return 4;
        }
        if (++count == crash_after) _exit(42);
    }
    if (!writer.finish()) { std::fprintf(stderr, "finish: %s\n", writer.error()); return 5; }
    std::printf("frames=%llu\n", static_cast<unsigned long long>(writer.committed_frames()));
    close(fd);
    return 0;
}
