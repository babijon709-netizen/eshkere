#pragma once
namespace record_capture {
// Runs only after exec, before any overlay, Binder, EGL or key initialization.
// fd refers to the empty file reserved by the supervisor; 0 bitrate selects the
// default. Returns nonzero on error while preserving completed MP4 fragments.
int Run(int fd, int bitrate);
}
