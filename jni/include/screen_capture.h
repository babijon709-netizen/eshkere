#pragma once

#include <string>
#include "screen_capture_options.h"

// A fallback for WindowManager/MediaProjection paths which omit standalone
// SurfaceFlinger layers. These commands capture the physical display instead;
// they cannot change what a third-party recorder or an app-only capture sees.
namespace screen_capture {
enum class Kind { Screenshot, Video };
enum class StartResult { Started, Busy, MissingCommand, StorageError, SpawnError };

struct Event {
    Kind kind = Kind::Screenshot;
    bool success = false;
    int exit_code = -1;
    std::string path;
};

// One asynchronous system-command job. Exposed so the host rig can exercise
// the actual process/pipe lifecycle with fake executables and temporary paths.
class Job {
public:
    Job() = default;
    ~Job();
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    StartResult start(Kind kind, const std::string& executable,
                      const std::string& directory, const std::string& log_path = {},
                      VideoOptions options = {});
    bool stop();
    bool poll(Event& event);
    void shutdown();
    bool running() const { return result_fd_ >= 0; }
    bool stopping() const { return stopping_; }

private:
    Kind kind_ = Kind::Screenshot;
    int result_fd_ = -1;
    int control_fd_ = -1;
    bool stopping_ = false;
    std::string path_;
};

StartResult Screenshot();
StartResult StartVideo(VideoOptions options = {});
bool StopVideo();
bool VideoRunning();
bool VideoStopping();
bool Poll(Event& event);
void Shutdown();
} // namespace screen_capture
