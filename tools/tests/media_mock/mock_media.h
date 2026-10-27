#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct MockFrame {
    int64_t pts = 0;
    uint32_t flags = 0;
    std::vector<uint8_t> data;
};
struct MockVideo {
    std::string mime = "video/avc";
    int32_t width = 1280, height = 720, rotation = 0;
    std::vector<uint8_t> csd[3] = {{0, 0, 0, 1, 0x67}, {0, 0, 0, 1, 0x68}, {}};
    std::vector<MockFrame> frames;
};
bool write_mock_video(const std::string& path, const MockVideo& video);
bool read_mock_video(const std::string& path, MockVideo& video);
