// Compile the real supervisor with a two-second segment for fast rollover tests.
#include "screen_capture.h"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace screen_capture;
using namespace std::chrono_literals;

const char* CfgDir() { return "/tmp/"; }

static std::string contents(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

static Event wait_for(Job& job) {
    Event event;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    do {
        if (job.poll(event)) return event;
        std::this_thread::sleep_for(20ms);
    } while (std::chrono::steady_clock::now() < deadline);
    std::fprintf(stderr, "recording session timed out\n");
    std::abort();
}

static fs::path wait_for_part(const fs::path& dir, const char* suffix, const char* needle) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    do {
        if (fs::exists(dir)) for (const auto& entry : fs::directory_iterator(dir)) {
            const auto name = entry.path().filename().string();
            if (name.find(suffix) != std::string::npos && contents(entry.path()).find(needle) != std::string::npos)
                return entry.path();
        }
        std::this_thread::sleep_for(20ms);
    } while (std::chrono::steady_clock::now() < deadline);
    std::fprintf(stderr, "part %s never contained %s\n", suffix, needle);
    std::abort();
}

static size_t file_count(const fs::path& dir) {
    size_t count = 0;
    if (fs::exists(dir)) for (const auto& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file()) ++count;
    return count;
}

static fs::path next_path(const fs::path& first) {
    std::string path = first.string();
    const auto pos = path.rfind("-part0000000001.mp4");
    assert(pos != std::string::npos);
    path.replace(pos, sizeof("-part0000000001.mp4") - 1, "-part0000000002.mp4");
    return path;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const std::string fake = fs::absolute(argv[1]).string();
    const fs::path root = fs::absolute(argv[2]);
    const std::string log = (root / "capture.log").string();
    std::signal(SIGCHLD, SIG_IGN);
    setenv("CAPTURE_TEST_SECRET", "must-not-leak", 1);
    const int extra = open("/dev/null", O_RDONLY);
    assert(extra >= 0 && dup2(extra, 75) == 75);
    if (extra != 75) close(extra);
    assert(fcntl(75, F_SETFD, 0) == 0);

    // No calls to Job::poll are made while three parts roll over: the
    // supervisor, not the overlay render loop, owns the continuous session.
    const auto dir = root / "rolling with spaces";
    Job video;
    VideoOptions selected{8};
    assert(video.start(Kind::Video, fake, dir.string(), log, selected) == StartResult::Started);
    selected = {16};
    const auto first = wait_for_part(dir, "-part0000000001.mp4", "duration=2 bitrate=8000000");
    const auto second = wait_for_part(dir, "-part0000000002.mp4", "duration=2 bitrate=8000000");
    const auto third = wait_for_part(dir, "-part0000000003.mp4", "duration=2 bitrate=8000000");
    assert(second == next_path(first));
    assert(contents(first).find("video finalized") != std::string::npos);
    assert(contents(second).find("video finalized") != std::string::npos);
    Event event;
    assert(video.running() && !video.poll(event));
    assert(video.stop() && !video.stop());
    event = wait_for(video);
    assert(event.success && event.kind == Kind::Video && event.path == first.string());
    assert(contents(third).find("video finalized") != std::string::npos);
    assert(file_count(dir) >= 3);
    for (const auto& entry : fs::directory_iterator(dir)) {
        const auto part = contents(entry.path());
        assert(part.find("duration=2 bitrate=8000000") != std::string::npos);
        assert(part.find("video finalized") != std::string::npos);
    }
    assert(contents(log).find("[capture] next segment:") != std::string::npos);

    // A pre-existing next part must never be overwritten; previous video stays.
    const auto collision_dir = root / "collision";
    assert(video.start(Kind::Video, fake, collision_dir.string(), log) == StartResult::Started);
    const auto collision_first = wait_for_part(collision_dir, "-part0000000001.mp4", "video started");
    const auto collision_next = next_path(collision_first);
    { std::ofstream sentinel(collision_next); sentinel << "do not overwrite"; }
    event = wait_for(video);
    assert(!event.success && event.path == collision_first.string());
    assert(contents(collision_first).find("video finalized") != std::string::npos);
    assert(contents(collision_next) == "do not overwrite" && file_count(collision_dir) == 2);
    assert(contents(log).find("cannot reserve next segment") != std::string::npos);

    // An unexpected successful exit far earlier than the limit is a failure,
    // not an excuse to create files indefinitely in a tight restart loop.
    const auto early_dir = root / "fake-early";
    assert(video.start(Kind::Video, fake, early_dir.string(), log) == StartResult::Started);
    event = wait_for(video);
    assert(!event.success && fs::exists(event.path));
    assert(file_count(early_dir) == 1);
    assert(contents(log).find("recorder ended before segment limit") != std::string::npos);

    // A failure in a later part leaves the completed first part in place and
    // removes the empty failing placeholder, without starting another one.
    const auto failed_dir = root / "fake-fail-second";
    assert(video.start(Kind::Video, fake, failed_dir.string(), log) == StartResult::Started);
    event = wait_for(video);
    assert(!event.success && event.exit_code == 9);
    assert(contents(event.path).find("video finalized") != std::string::npos);
    assert(file_count(failed_dir) == 1);

    // Stop during the boundary and ensure no empty extra file remains.
    const auto boundary_dir = root / "boundary";
    assert(video.start(Kind::Video, fake, boundary_dir.string(), log) == StartResult::Started);
    wait_for_part(boundary_dir, "-part0000000001.mp4", "video finalized");
    assert(video.stop());
    event = wait_for(video);
    assert(event.success);
    for (const auto& entry : fs::directory_iterator(boundary_dir))
        assert(fs::file_size(entry.path()) > 0);

    // Closing the control channel during a later part finalizes it and prevents
    // another rollover, even when the app disappears rather than pressing stop.
    const auto parent_dir = root / "parent-death";
    int ready[2]; assert(pipe(ready) == 0);
    const pid_t parent = fork();
    assert(parent >= 0);
    if (parent == 0) {
        close(ready[0]);
        Job owned;
        const auto result = owned.start(Kind::Video, fake, parent_dir.string());
        const char ok = result == StartResult::Started ? '1' : '0';
        (void)write(ready[1], &ok, 1);
        close(ready[1]);
        for (;;) pause();
    }
    close(ready[1]);
    char ok = 0; assert(read(ready[0], &ok, 1) == 1 && ok == '1'); close(ready[0]);
    wait_for_part(parent_dir, "-part0000000002.mp4", "video started");
    assert(kill(parent, SIGKILL) == 0);
    wait_for_part(parent_dir, "-part0000000002.mp4", "video finalized");
    std::this_thread::sleep_for(2300ms);
    assert(file_count(parent_dir) == 2);
    std::puts("PASS: continuous rollover, stop/death, collisions, failures and early exit");
}
