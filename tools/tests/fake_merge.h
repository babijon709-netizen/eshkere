#pragma once

// The supervisor re-execs its own executable in private helper mode. These
// lifecycle tests use text captures; media remuxing itself has a separate rig.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

inline int fake_merge_helper(int argc, char** argv) {
    if (argc != 4 || std::strcmp(argv[1], "--capture-merge") != 0) return -1;
    const char* root = std::getenv("ANDROID_ROOT");
    if (!root || std::strcmp(root, "/system") || std::getenv("CAPTURE_TEST_SECRET")) return 42;
    if (fcntl(75, F_GETFD) != -1 || errno != EBADF) return 43;
    std::string part = argv[2], output = argv[3];
    const size_t digits = part.rfind("-part");
    if (digits == std::string::npos || digits + 19 != part.size()) return 2;
    const std::string stem = part.substr(0, digits);
    if (output != stem + ".mp4") return 2;
    const unsigned long count = std::strtoul(part.substr(digits + 5, 10).c_str(), nullptr, 10);
    if (!count || count > 1000) return 2;
    if (output.find("fake-merge-fail") != std::string::npos) {
        std::fputs("fake merge failure\n", stderr);
        return 8;
    }
    if (output.find("fake-merge-slow") != std::string::npos) {
        std::fputs("fake merger entered\n", stderr);
        std::fflush(stderr);
        sleep(3);
    }
    const std::string temporary = output + ".merging";
    const int fd = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) return 3;
    close(fd);
    std::ofstream merged(temporary, std::ios::binary | std::ios::trunc);
    std::vector<std::string> parts;
    for (unsigned long i = 1; i <= count; ++i) {
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), "-part%010lu.mp4", i);
        const std::string current = stem + suffix;
        std::ifstream input(current, std::ios::binary);
        if (!input) { merged.close(); unlink(temporary.c_str()); return 4; }
        merged << input.rdbuf();
        parts.push_back(current);
    }
    merged.close();
    if (!merged || rename(temporary.c_str(), output.c_str()) != 0) {
        unlink(temporary.c_str());
        return 5;
    }
    for (const auto& path : parts) (void)unlink(path.c_str());
    return 0;
}
