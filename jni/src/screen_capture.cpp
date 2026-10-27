#include "screen_capture.h"
#include "config.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace screen_capture {
namespace {

// Android's screenrecord enforces a maximum of 180 seconds per invocation.
// A video session rolls over internally and is remuxed after stop.
#ifdef SCREEN_CAPTURE_TEST_SEGMENT_SECONDS
constexpr int kSegmentSeconds = SCREEN_CAPTURE_TEST_SEGMENT_SECONDS;
#else
constexpr int kSegmentSeconds = 180;
#endif
static_assert(kSegmentSeconds >= 1 && kSegmentSeconds <= 180, "invalid screenrecord segment size");
constexpr int kPartDigits = 10;
constexpr char kFirstPartSuffix[] = "-part0000000001.mp4";
static_assert(sizeof(kFirstPartSuffix) == kPartDigits + sizeof("-part.mp4"), "part suffix size");

bool ensure_directory(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    for (size_t pos = 1; ; ++pos) {
        pos = path.find('/', pos);
        const std::string component = path.substr(0, pos);
        if (!component.empty()) {
            if (mkdir(component.c_str(), 0775) != 0 && errno != EEXIST) return false;
            struct stat st{};
            if (stat(component.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
        }
        if (pos == std::string::npos) return true;
    }
}

bool reserve_path(const std::string& directory, Kind kind, std::string& path,
                  std::string& first_part, struct stat& reservation) {
    const time_t now = time(nullptr);
    struct tm tm{};
    if (!localtime_r(&now, &tm)) return false;
    char timestamp[32];
    if (!strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm)) return false;
    const char* suffix = kind == Kind::Screenshot ? ".png" : ".mp4";
    for (unsigned i = 0; i < 1000; ++i) {
        char name[112];
        const int length = snprintf(name, sizeof(name), "/benzware-%s-%d-%03u%s",
                                    timestamp, static_cast<int>(getpid()), i, suffix);
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(name)) return false;
        path = directory + name;
        // Reserve both names. Neither a prior result nor a recoverable part
        // from an interrupted session may be overwritten.
        const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
        if (fd < 0) { if (errno == EEXIST) continue; return false; }
        (void)fchmod(fd, 0644);
        const bool stated = fstat(fd, &reservation) == 0;
        close(fd);
        if (!stated) { (void)unlink(path.c_str()); return false; }
        if (kind == Kind::Screenshot) return true;
        first_part = path.substr(0, path.size() - sizeof(".mp4") + 1) + kFirstPartSuffix;
        const int part_fd = open(first_part.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
        if (part_fd >= 0) {
            (void)fchmod(part_fd, 0644);
            close(part_fd);
            return true;
        }
        const int error = errno;
        (void)unlink(path.c_str());
        if (error != EEXIST) return false;
    }
    return false;
}

long max_open_files() {
    struct rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return 65536;
    // The subprocess must not retain the overlay's Binder, uinput, or another
    // job's control sockets. This upper bound also covers Android's usual limit.
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur > 1048576) return 1048576;
    return static_cast<long>(limit.rlim_cur);
}

// Runs only in the single-threaded fork child. Do not call C++ allocation,
// stdio, or application code here: the parent has EGL/Binder/audio threads.
void close_inherited(long max_fd, int keep1, int keep2, int keep3) {
    for (int fd = 3; fd < max_fd; ++fd)
        if (fd != keep1 && fd != keep2 && fd != keep3) close(fd);
}

void set_signal(int sig, void (*handler)(int)) {
    struct sigaction sa{};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(sig, &sa, nullptr);
}

long long monotonic_ms() {
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

// Read stop requests without blocking the render thread. Once stopped, still
// sleep between waitpid checks instead of spinning on a permanently closed fd.
void check_control(int fd, bool& stop_requested, int timeout_ms) {
    if (stop_requested) {
        if (timeout_ms > 0) (void)poll(nullptr, 0, timeout_ms);
        return;
    }
    struct pollfd pfd{fd, POLLIN | POLLHUP, 0};
    const int ready = poll(&pfd, 1, timeout_ms);
    if (ready < 0) {
        if (errno != EINTR) stop_requested = true;
        return;
    }
    if (ready == 0) return;
    if (pfd.revents & POLLNVAL) { stop_requested = true; return; }
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
        char buf[16];
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n >= 0 || (errno != EAGAIN && errno != EINTR)) stop_requested = true;
    }
}

bool file_nonempty(const char* path) {
    struct stat st{};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

// Called after fork with no allocator or stdio. The initial path is already
// unique, and its fixed-width decimal suffix groups the session's parts.
bool reserve_next_part(char* path, char* digits) {
    bool carried = true;
    for (int i = kPartDigits - 1; i >= 0; --i) {
        if (digits[i] != '9') { ++digits[i]; carried = false; break; }
        digits[i] = '0';
    }
    if (carried) return false;
    const int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    (void)fchmod(fd, 0644);
    close(fd);
    return true;
}

void previous_part(char* digits) {
    for (int i = kPartDigits - 1; i >= 0; --i) {
        if (digits[i] != '0') { --digits[i]; return; }
        digits[i] = '9';
    }
}

void redirect_log(int log_fd) {
    if (log_fd >= 0) {
        (void)dup2(log_fd, STDOUT_FILENO);
        (void)dup2(log_fd, STDERR_FILENO);
        if (log_fd > STDERR_FILENO) close(log_fd);
    }
}

// The parent wipes its environment. Both system media commands and our
// freshly exec'd merge helper need this minimal Android environment.
void exec_media(const char* executable, char* const argv[]) {
    char* const env[] = {
        const_cast<char*>("PATH=/system/bin:/system/xbin"),
        const_cast<char*>("ANDROID_ROOT=/system"),
        const_cast<char*>("ANDROID_ASSETS=/system/app"),
        const_cast<char*>("ANDROID_DATA=/data"),
        const_cast<char*>("ANDROID_STORAGE=/storage"),
        const_cast<char*>("ANDROID_ART_ROOT=/apex/com.android.art"),
        const_cast<char*>("ANDROID_I18N_ROOT=/apex/com.android.i18n"),
        const_cast<char*>("ANDROID_TZDATA_ROOT=/apex/com.android.tzdata"),
        const_cast<char*>("EXTERNAL_STORAGE=/sdcard"),
        const_cast<char*>("TMPDIR=/data/local/tmp"), nullptr
    };
    execve(executable, argv, env);
    static constexpr char failed[] = "capture: execve failed\n";
    (void)write(STDERR_FILENO, failed, sizeof(failed) - 1);
    _exit(127);
}

// The app deliberately ignores SIGCHLD, so it cannot waitpid() the recorder.
// This supervisor owns the child and rolls video into 180-second segments.
// On stop or parent death it execs a clean helper to finalize one MP4. All work
// before exec uses fixed buffers and syscalls, never media APIs after fork.
[[noreturn]] void supervise(Kind kind, int result_fd, int control_fd, int log_fd,
                            long max_fd, const char* executable, char* const argv[],
                            char* part_path, char* part_digits, const char* final_path,
                            struct stat reservation) {
    close_inherited(max_fd, result_fd, control_fd, log_fd);
    set_signal(SIGCHLD, SIG_DFL);
    set_signal(SIGPIPE, SIG_IGN);

    bool stop_requested = false;
    int status = -1;
    for (;;) {
        const pid_t child = fork();
        if (child == 0) {
            close(result_fd);
            close(control_fd);
            redirect_log(log_fd);
            set_signal(SIGINT, SIG_DFL);
            set_signal(SIGTERM, SIG_DFL);
            set_signal(SIGHUP, SIG_DFL);
            set_signal(SIGPIPE, SIG_DFL);
            exec_media(executable, argv);
        }
        if (child < 0) {
            if (kind == Kind::Video && !file_nonempty(part_path)) (void)unlink(part_path);
            status = -1;
            break;
        }

        status = -1;
        const long long started = monotonic_ms();
        long long signalled = 0;
        bool force_killed = false;
        for (;;) {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) break;
            if (waited < 0 && errno != EINTR) { status = -1; break; }

            check_control(control_fd, stop_requested, 100);
            const long long now = monotonic_ms();
            // screenrecord installs its SIGINT handler only after exec. Give it
            // time to start so a quick stop can finalize the current MP4.
            if (stop_requested && !signalled &&
                (kind == Kind::Screenshot || now - started >= 1000)) {
                (void)kill(child, kind == Kind::Video ? SIGINT : SIGTERM);
                signalled = now ? now : 1;
            }
            if (signalled && !force_killed && now - signalled >= 5000) {
                (void)kill(child, SIGKILL);
                force_killed = true;
            }
        }
        if (kind == Kind::Screenshot) break;

        // If stop arrived as the child exited, do not start another segment.
        check_control(control_fd, stop_requested, 0);
        const bool nonempty = file_nonempty(part_path);
        if (!nonempty) (void)unlink(part_path);
        if (stop_requested || status != 0 || !nonempty) {
            if (status == 0 && !nonempty) status = -1;
            break;
        }

        // A zero exit long before the requested time may be an encoder EOS or
        // OEM failure, not the time limit. Do not silently retry forever.
        const long long elapsed = monotonic_ms() - started;
        if (started <= 0 || elapsed < static_cast<long long>(kSegmentSeconds) * 900LL) {
            static constexpr char early[] = "capture: recorder ended before segment limit\n";
            if (log_fd >= 0) (void)write(log_fd, early, sizeof(early) - 1);
            status = -1;
            break;
        }
        if (!reserve_next_part(part_path, part_digits)) {
            static constexpr char failed[] = "capture: cannot reserve next segment\n";
            if (log_fd >= 0) (void)write(log_fd, failed, sizeof(failed) - 1);
            status = -1;
            break;
        }
        // A stop/parent death between parts must not leave an empty placeholder.
        check_control(control_fd, stop_requested, 0);
        if (stop_requested) {
            (void)unlink(part_path);
            previous_part(part_digits);
            break;
        }
        if (log_fd >= 0) {
            static constexpr char next[] = "[capture] next segment: ";
            (void)write(log_fd, next, sizeof(next) - 1);
            (void)write(log_fd, part_path, strlen(part_path));
            (void)write(log_fd, "\n", 1);
        }
    }
    if (kind == Kind::Video && status == 0) {
        if (log_fd >= 0) {
            static constexpr char merging[] = "[capture] finalizing one MP4\n";
            (void)write(log_fd, merging, sizeof(merging) - 1);
        }
        const pid_t helper = fork();
        if (helper == 0) {
            close(result_fd);
            close(control_fd);
            redirect_log(log_fd);
            set_signal(SIGINT, SIG_DFL);
            set_signal(SIGTERM, SIG_DFL);
            set_signal(SIGHUP, SIG_DFL);
            set_signal(SIGPIPE, SIG_DFL);
            char* const merge_args[] = {const_cast<char*>("/proc/self/exe"),
                const_cast<char*>("--capture-merge"), part_path,
                const_cast<char*>(final_path), nullptr};
            exec_media("/proc/self/exe", merge_args);
        }
        status = -1;
        if (helper > 0) {
            pid_t waited;
            do { waited = waitpid(helper, &status, 0); } while (waited < 0 && errno == EINTR);
            if (waited != helper) status = -1;
        }
    }
    if (kind == Kind::Video && status != 0) {
        // Also clean up when the app has already exited, without touching a
        // replaced or nonempty file. Numbered source segments remain intact.
        struct stat current{};
        if (lstat(final_path, &current) == 0 && S_ISREG(current.st_mode) && current.st_size == 0 &&
            current.st_dev == reservation.st_dev && current.st_ino == reservation.st_ino)
            (void)unlink(final_path);
    }
    if (log_fd >= 0) close(log_fd);
    // One small write is atomic for a pipe; the parent reads it non-blocking.
    (void)write(result_fd, &status, sizeof(status));
    close(result_fd);
    close(control_fd);
    _exit(0);
}

std::string capture_log_path() { return std::string(CfgDir()) + "capture.log"; }
Job g_screenshot;
Job g_video;
} // namespace

Job::~Job() { shutdown(); }

StartResult Job::start(Kind kind, const std::string& executable,
                       const std::string& directory, const std::string& log_path,
                       VideoOptions options) {
    if (running()) return StartResult::Busy;
    if (executable.empty() || executable[0] != '/' || access(executable.c_str(), X_OK) != 0)
        return StartResult::MissingCommand;
    std::string first_part;
    struct stat reservation{};
    if (!ensure_directory(directory) || !reserve_path(directory, kind, path_, first_part, reservation))
        return StartResult::StorageError;

    int results[2] = {-1, -1};
    int control[2] = {-1, -1};
    if (pipe2(results, O_CLOEXEC) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, control) != 0) {
        if (results[0] >= 0) close(results[0]);
        if (results[1] >= 0) close(results[1]);
        unlink(path_.c_str());
        if (!first_part.empty()) unlink(first_part.c_str());
        path_.clear();
        return StartResult::SpawnError;
    }
    const int read_flags = fcntl(results[0], F_GETFL, 0);
    if (read_flags < 0 || fcntl(results[0], F_SETFL, read_flags | O_NONBLOCK) < 0) {
        close(results[0]); close(results[1]); close(control[0]); close(control[1]);
        unlink(path_.c_str());
        if (!first_part.empty()) unlink(first_part.c_str());
        path_.clear();
        return StartResult::SpawnError;
    }

    int log_fd = -1;
    if (!log_path.empty()) {
        log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (log_fd >= 0)
            (void)dprintf(log_fd, "\n[capture] %s -> %s\n", executable.c_str(), path_.c_str());
    }
    // Prepare arguments before forking. Changes in the menu while recording
    // affect only the next video; Auto omits --bit-rate entirely.
    options = sanitize(options);
    char duration[16];
    char bitrate[16];
    std::snprintf(duration, sizeof(duration), "%d", kSegmentSeconds);
    std::snprintf(bitrate, sizeof(bitrate), "%d", options.bitrate_mbps * 1000000);
    char* args[7] = {};
    int arg = 0;
    args[arg++] = const_cast<char*>(executable.c_str());
    if (kind == Kind::Screenshot) {
        args[arg++] = const_cast<char*>("-p");
    } else {
        args[arg++] = const_cast<char*>("--time-limit");
        args[arg++] = duration;
        if (options.bitrate_mbps != 0) {
            args[arg++] = const_cast<char*>("--bit-rate");
            args[arg++] = bitrate;
        }
    }
    char* const part_path = kind == Kind::Video ? first_part.data() : path_.data();
    char* const part_digits = kind == Kind::Video ? part_path + first_part.size() - kPartDigits - 4 : nullptr;
    args[arg] = part_path;
    const long max_fd = max_open_files();
    const pid_t supervisor = fork();
    if (supervisor == 0) {
        close(results[0]);
        close(control[0]);
        supervise(kind, results[1], control[1], log_fd, max_fd, executable.c_str(), args,
                  part_path, part_digits, path_.c_str(), reservation);
    }
    close(results[1]);
    close(control[1]);
    if (log_fd >= 0) close(log_fd);
    if (supervisor < 0) {
        close(results[0]); close(control[0]);
        unlink(path_.c_str());
        if (!first_part.empty()) unlink(first_part.c_str());
        path_.clear();
        return StartResult::SpawnError;
    }
    kind_ = kind;
    result_fd_ = results[0];
    control_fd_ = control[0];
    stopping_ = false;
    return StartResult::Started;
}

bool Job::stop() {
    if (!running() || stopping_) return false;
    const char command = 's';
    ssize_t sent;
    do { sent = send(control_fd_, &command, 1, MSG_NOSIGNAL); } while (sent < 0 && errno == EINTR);
    if (sent != 1) return false;
    stopping_ = true;
    return true;
}

bool Job::poll(Event& event) {
    if (!running()) return false;
    int status = -1;
    ssize_t n;
    do { n = read(result_fd_, &status, sizeof(status)); } while (n < 0 && errno == EINTR);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;

    struct stat st{};
    const bool nonempty = stat(path_.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
    event.kind = kind_;
    event.path = path_;
    event.exit_code = n == sizeof(status) && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    event.success = event.exit_code == 0 && nonempty;
    // Only remove our empty placeholder. Preserve nonempty partial captures for
    // diagnosis rather than deleting a potentially recoverable recording.
    if (!event.success && !nonempty) (void)unlink(path_.c_str());
    shutdown();
    return true;
}

void Job::shutdown() {
    // EOF tells the supervisor to stop and finalize a running recording; don't
    // block the render/shutdown thread on MediaCodec or the media scanner.
    if (control_fd_ >= 0) close(control_fd_);
    if (result_fd_ >= 0) close(result_fd_);
    control_fd_ = result_fd_ = -1;
    stopping_ = false;
    path_.clear();
}

StartResult Screenshot() {
    return g_screenshot.start(Kind::Screenshot, "/system/bin/screencap",
                              "/sdcard/Pictures/Benzware", capture_log_path());
}
StartResult StartVideo(VideoOptions options) {
    return g_video.start(Kind::Video, "/system/bin/screenrecord",
                         "/sdcard/Movies/Benzware", capture_log_path(), options);
}
bool StopVideo() { return g_video.stop(); }
bool VideoRunning() { return g_video.running(); }
bool VideoStopping() { return g_video.stopping(); }
bool Poll(Event& event) { return g_screenshot.poll(event) || g_video.poll(event); }
void Shutdown() { g_screenshot.shutdown(); g_video.shutdown(); }
} // namespace screen_capture
