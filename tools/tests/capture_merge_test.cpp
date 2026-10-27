#include "capture_merge.h"
#include "media_mock/mock_media.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
static void write_bytes(const fs::path& p, const std::string& value) {
    std::ofstream out(p, std::ios::binary);
    out << value;
    assert(out.good());
}
static MockVideo video(int64_t start, size_t count = 3) {
    MockVideo result;
    result.rotation = 90;
    for (size_t i = 0; i < count; ++i)
        result.frames.push_back({start + static_cast<int64_t>(i) * 33333,
                                 i == 0 ? 1u : 0u,
                                 {static_cast<uint8_t>(i + 11), static_cast<uint8_t>(start % 251), 99}});
    return result;
}
struct Paths {
    fs::path dir, first, second, third, final, temp;
    explicit Paths(const fs::path& root, const std::string& name) {
        dir = root / name;
        assert(fs::create_directories(dir));
        first = dir / "benzware-part0000000001.mp4";
        second = dir / "benzware-part0000000002.mp4";
        third = dir / "benzware-part0000000003.mp4";
        final = dir / "benzware.mp4";
        temp = dir / "benzware.mp4.merging";
        write_bytes(final, "");
    }
};

static void assert_failure_keeps(const Paths& p, const std::string& first, const std::string& second) {
    assert(capture_merge::Run(p.second.c_str(), p.final.c_str()) != 0);
    assert(bytes(p.first) == first && bytes(p.second) == second);
    assert(fs::exists(p.final) && fs::file_size(p.final) == 0);
    assert(!fs::exists(p.temp));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path root = fs::absolute(argv[1]);
    assert(fs::create_directories(root));

    // Three independently timestamped tracks are rebased and copied in order.
    // One frame exceeds the initial extractor buffer to exercise API 24 growth.
    {
        Paths p(root, "three parts with spaces");
        auto a = video(5000000), b = video(9000000, 2), c = video(12300000, 1);
        b.frames[1].data.assign(900000, 0x7a);
        assert(write_mock_video(p.first, a) && write_mock_video(p.second, b) && write_mock_video(p.third, c));
        assert(capture_merge::Run(p.third.c_str(), p.final.c_str()) == 0);
        assert(fs::exists(p.final) && !fs::exists(p.first) && !fs::exists(p.second) &&
               !fs::exists(p.third) && !fs::exists(p.temp));
        MockVideo output;
        assert(read_mock_video(p.final, output));
        assert(output.width == 1280 && output.height == 720 && output.rotation == 90);
        assert(output.frames.size() == 6);
        assert(output.frames[0].pts == 0 && output.frames[1].pts == 33333 &&
               output.frames[2].pts == 66666 && output.frames[3].pts == 99999 &&
               output.frames[4].pts == 133332 && output.frames[5].pts == 166665);
        assert(output.frames[0].data == a.frames[0].data && output.frames[3].data == b.frames[0].data &&
               output.frames[4].data == b.frames[1].data && output.frames[5].data == c.frames[0].data);
    }
    // A short recording is published directly, without a second copy or muxer.
    {
        Paths p(root, "single");
        write_bytes(p.first, "single original mp4 data");
        setenv("MOCK_MEDIA_FAILURE", "muxer", 1);
        assert(capture_merge::Run(p.first.c_str(), p.final.c_str()) == 0);
        unsetenv("MOCK_MEDIA_FAILURE");
        assert(bytes(p.final) == "single original mp4 data" && !fs::exists(p.first) && !fs::exists(p.temp));
    }
    // Failed muxing, extraction, writes, trailer or verification must never
    // remove either source, even when a temporary output has been created.
    for (const char* mode : {"muxer", "extractor", "format", "select", "orientation", "add", "start",
                             "write", "stop", "corrupt", "drop", "read"}) {
        Paths p(root, std::string("error-") + mode);
        assert(write_mock_video(p.first, video(1000000)) && write_mock_video(p.second, video(2000000)));
        const std::string a = bytes(p.first), b = bytes(p.second);
        setenv("MOCK_MEDIA_FAILURE", mode, 1);
        assert_failure_keeps(p, a, b);
        unsetenv("MOCK_MEDIA_FAILURE");
    }
    // Incompatible/unsafe tracks are recoverable parts, never a false success.
    for (const char* mode : {"width", "height", "rotation", "csd", "mime", "missing-csd", "out-of-order",
                             "duplicate-pts", "no-keyframe", "encrypted", "partial", "zero-size", "no-frames"}) {
        Paths p(root, std::string("invalid-") + mode);
        auto a = video(1000000), b = video(2000000);
        const std::string which = mode;
        if (which == "width") ++b.width;
        else if (which == "height") ++b.height;
        else if (which == "rotation") b.rotation = 0;
        else if (which == "csd") b.csd[0].back() ^= 1;
        else if (which == "mime") b.mime = "video/hevc";
        else if (which == "missing-csd") b.csd[0].clear();
        else if (which == "out-of-order") b.frames[2].pts = b.frames[1].pts - 1;
        else if (which == "duplicate-pts") b.frames[2].pts = b.frames[1].pts;
        else if (which == "no-keyframe") b.frames[0].flags = 0;
        else if (which == "encrypted") b.frames[1].flags = 2;
        else if (which == "partial") b.frames[1].flags = 4;
        else if (which == "zero-size") b.frames[1].data.clear();
        else if (which == "no-frames") b.frames.clear();
        assert(write_mock_video(p.first, a) && write_mock_video(p.second, b));
        assert_failure_keeps(p, bytes(p.first), bytes(p.second));
    }
    // Existing temporary/final files, missing parts and symlinks are not overwritten.
    {
        Paths p(root, "collision");
        assert(write_mock_video(p.first, video(1000000)) && write_mock_video(p.second, video(2000000)));
        const auto a = bytes(p.first), b = bytes(p.second);
        write_bytes(p.temp, "keep temporary");
        assert(capture_merge::Run(p.second.c_str(), p.final.c_str()) != 0);
        assert(bytes(p.temp) == "keep temporary" && bytes(p.first) == a && bytes(p.second) == b);
        fs::remove(p.temp);
        write_bytes(p.final, "keep prior final");
        assert(capture_merge::Run(p.second.c_str(), p.final.c_str()) != 0);
        assert(bytes(p.final) == "keep prior final" && bytes(p.first) == a && bytes(p.second) == b);
    }
    {
        Paths p(root, "missing");
        assert(write_mock_video(p.first, video(1000000)));
        const auto a = bytes(p.first);
        assert(capture_merge::Run(p.second.c_str(), p.final.c_str()) != 0);
        assert(bytes(p.first) == a && !fs::exists(p.temp) && fs::file_size(p.final) == 0);
    }
    {
        Paths p(root, "symlink");
        const auto target = p.dir / "another.mp4";
        assert(write_mock_video(target, video(1000000)) && write_mock_video(p.second, video(2000000)));
        fs::create_symlink(target, p.first);
        const auto a = bytes(target), b = bytes(p.second);
        assert(capture_merge::Run(p.second.c_str(), p.final.c_str()) != 0);
        assert(fs::is_symlink(p.first) && bytes(target) == a && bytes(p.second) == b && !fs::exists(p.temp));
    }
    std::puts("PASS: remux timestamps, samples, compatibility, verification, collision and failure recovery");
}
