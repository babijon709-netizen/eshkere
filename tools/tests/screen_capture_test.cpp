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

// The Android wrappers use CfgDir; the rig supplies a harmless host path.
const char* CfgDir() { return "/tmp/"; }

static Event wait_for(Job& job) {
    Event event;
    const auto deadline = std::chrono::steady_clock::now() + 12s;
    do {
        if (job.poll(event)) return event;
        std::this_thread::sleep_for(20ms);
    } while (std::chrono::steady_clock::now() < deadline);
    std::fprintf(stderr, "capture supervisor timed out\n");
    std::abort();
}

static std::string contents(const fs::path& p) {
    std::ifstream file(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

static bool file_with(const fs::path& dir, const std::string& needle) {
    if (!fs::exists(dir)) return false;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file() && contents(entry.path()).find(needle) != std::string::npos)
            return true;
    return false;
}

static void wait_for_file(const fs::path& dir, const std::string& needle) {
    const auto deadline = std::chrono::steady_clock::now() + 12s;
    do {
        if (file_with(dir, needle)) return;
        std::this_thread::sleep_for(30ms);
    } while (std::chrono::steady_clock::now() < deadline);
    std::fprintf(stderr, "capture file never contained %s\n", needle.c_str());
    std::abort();
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const std::string fake = fs::absolute(argv[1]).string();
    const fs::path root = fs::absolute(argv[2]);
    const std::string shots = (root / "screenshots with spaces").string();
    const std::string videos = (root / "videos").string();
    const std::string log = (root / "capture.log").string();

    // This is how the real app runs. A direct waitpid() in the parent cannot
    // collect its children when SIGCHLD is explicitly ignored.
    std::signal(SIGCHLD, SIG_IGN);
    setenv("CAPTURE_TEST_SECRET", "must-not-leak", 1);
    const int extra = open("/dev/null", O_RDONLY);
    assert(extra >= 0 && dup2(extra, 75) == 75);
    if (extra != 75) close(extra);
    assert(fcntl(75, F_SETFD, 0) == 0); // deliberately not CLOEXEC
    Job shot, video;
    assert(shot.start(Kind::Screenshot, "/missing/screencap", shots) == StartResult::MissingCommand);
    assert(shot.start(Kind::Screenshot, fake, shots, log) == StartResult::Started);
    assert(shot.start(Kind::Screenshot, fake, shots) == StartResult::Busy);
    Event e = wait_for(shot);
    assert(e.success && e.kind == Kind::Screenshot && e.exit_code == 0);
    assert(contents(e.path) == "fake png\n");
    const std::string first_path = e.path;
    assert(shot.start(Kind::Screenshot, fake, shots, log) == StartResult::Started);
    e = wait_for(shot);
    assert(e.success && e.path != first_path && fs::exists(first_path));
    assert(contents(log).find("fake screenshot complete") != std::string::npos);

    // A real failed exec, a failed command and a zero-byte success must all be
    // reported as errors and leave no misleading empty output behind.
    const fs::path bad = root / "bad-exec";
    { std::ofstream f(bad); f << "#!/does/not/exist\n"; }
    fs::permissions(bad, fs::perms::owner_all);
    const std::string failures = (root / "failures").string();
    assert(shot.start(Kind::Screenshot, bad.string(), failures, log) == StartResult::Started);
    e = wait_for(shot); assert(!e.success && e.exit_code == 127 && !fs::exists(e.path));
    assert(contents(log).find("capture: execve failed") != std::string::npos);
    assert(shot.start(Kind::Screenshot, "/bin/false", failures) == StartResult::Started);
    e = wait_for(shot); assert(!e.success && e.exit_code == 1 && !fs::exists(e.path));
    assert(shot.start(Kind::Screenshot, "/bin/true", failures) == StartResult::Started);
    e = wait_for(shot); assert(!e.success && e.exit_code == 0 && !fs::exists(e.path));
    assert(shot.start(Kind::Screenshot, fake, (bad / "child").string()) == StartResult::StorageError);

    // A stop just after spawn is delayed until the recorder installs SIGINT;
    // even with SIGCHLD ignored, the supervisor returns the finalized result.
    assert(video.start(Kind::Video, fake, videos, log) == StartResult::Started);
    assert(video.running() && video.stop() && video.stopping() && !video.stop());
    e = wait_for(video);
    assert(e.success && e.kind == Kind::Video && e.exit_code == 0);
    assert(contents(e.path).find("video started duration=180 bitrate=auto") != std::string::npos);
    assert(contents(e.path).find("video finalized") != std::string::npos);
    assert(!video.running() && !video.stopping());

    // The forked command receives a snapshot in seconds and bits/second.
    VideoOptions selected{8};
    assert(video.start(Kind::Video, fake, videos, log, selected) == StartResult::Started);
    selected = {16};
    assert(video.stop());
    e = wait_for(video); assert(e.success);
    assert(contents(e.path).find("video started duration=180 bitrate=8000000") != std::string::npos);

    // The still and video jobs can overlap; the second supervisor must close
    // the first job's inherited socket so it cannot keep that job alive.
    assert(video.start(Kind::Video, fake, videos, log, {4}) == StartResult::Started);
    assert(shot.start(Kind::Screenshot, fake, shots, log) == StartResult::Started);
    e = wait_for(shot); assert(e.success && e.kind == Kind::Screenshot);
    assert(video.stop());
    e = wait_for(video); assert(e.success && e.kind == Kind::Video);
    assert(contents(e.path).find("video started duration=180 bitrate=4000000") != std::string::npos);

    assert(video.start(Kind::Video, fake, videos, log, {16}) == StartResult::Started);
    assert(video.stop());
    e = wait_for(video); assert(e.success);
    assert(contents(e.path).find("video started duration=180 bitrate=16000000") != std::string::npos);
    assert(video.start(Kind::Video, fake, videos, log, {999}) == StartResult::Started);
    assert(video.stop());
    e = wait_for(video); assert(e.success);
    assert(contents(e.path).find("video started duration=180 bitrate=auto") != std::string::npos);

    // Closing the control channel (normal app exit or process death) also
    // requests a graceful stop; it must not leave an orphaned recorder.
    const std::string shutdown_dir = (root / "shutdown").string();
    assert(video.start(Kind::Video, fake, shutdown_dir) == StartResult::Started);
    video.shutdown();
    wait_for_file(shutdown_dir, "video finalized");

    const std::string crash_dir = (root / "parent-exit").string();
    const pid_t parent = fork();
    assert(parent >= 0);
    if (parent == 0) {
        Job owned;
        const auto result = owned.start(Kind::Video, fake, crash_dir);
        _exit(result == StartResult::Started ? 0 : 2);
    }
    wait_for_file(crash_dir, "video finalized");

    // A broken control socket must not kill the parent with SIGPIPE.
    assert(video.start(Kind::Video, "/bin/false", videos) == StartResult::Started);
    std::this_thread::sleep_for(800ms);
    (void)video.stop();
    e = wait_for(video); assert(!e.success);
    std::puts("screen capture command lifecycle: PASS");
}
