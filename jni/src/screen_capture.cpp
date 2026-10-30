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
                  int& output_fd, struct stat& reservation) {
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
        int fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (fd < 0) { if (errno == EEXIST) continue; return false; }
        (void)fchmod(fd, 0644);
        if (fstat(fd, &reservation) != 0) { close(fd); (void)unlink(path.c_str()); return false; }
        if (kind == Kind::Screenshot) close(fd);
        else {
            // stdio may be closed by a launcher; keep the output away from
            // fd 0/1/2 so log redirection cannot clobber it before exec.
            if (fd < 3) {
                const int copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
                close(fd);
                if (copy < 0) { (void)unlink(path.c_str()); return false; }
                fd = copy;
            }
            output_fd = fd; // Pass the reserved inode through exec, without reopening by path.
        }
        return true;
    }
    return false;
}

void remove_empty_reservation(const char* path, const struct stat& reservation) {
    struct stat current{};
    if (lstat(path, &current) == 0 && S_ISREG(current.st_mode) && current.st_size == 0 &&
        current.st_dev == reservation.st_dev && current.st_ino == reservation.st_ino)
        (void)unlink(path);
}

long max_open_files() {
    struct rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return 65536;
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur > 1048576) return 1048576;
    return static_cast<long>(limit.rlim_cur);
}

// This runs only in the fork child of the multithreaded overlay. Do not call
// allocation, stdio or media libraries until after a fresh exec.
void close_inherited(long max_fd, int keep1, int keep2, int keep3, int keep4) {
    for (int fd = 3; fd < max_fd; ++fd)
        if (fd != keep1 && fd != keep2 && fd != keep3 && fd != keep4) close(fd);
}
void set_signal(int sig, void (*handler)(int)) {
    struct sigaction sa{};
    sa.sa_handler = handler; sigemptyset(&sa.sa_mask);
    (void)sigaction(sig, &sa, nullptr);
}
long long monotonic_ms() {
    struct timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}
void check_control(int fd, bool& stopped, int timeout_ms) {
    if (stopped) { if (timeout_ms > 0) (void)poll(nullptr, 0, timeout_ms); return; }
    struct pollfd pfd{fd, POLLIN | POLLHUP, 0};
    const int ready = poll(&pfd, 1, timeout_ms);
    if (ready < 0) { if (errno != EINTR) stopped = true; return; }
    if (ready == 0) return;
    if (pfd.revents & POLLNVAL) { stopped = true; return; }
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
        char buf[16];
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n >= 0 || (errno != EAGAIN && errno != EINTR)) stopped = true;
    }
}
void redirect_log(int log_fd) {
    if (log_fd >= 0) {
        (void)dup2(log_fd, STDOUT_FILENO); (void)dup2(log_fd, STDERR_FILENO);
        if (log_fd > STDERR_FILENO) close(log_fd);
    }
}

// The overlay intentionally wipes its environment. A freshly exec'd system
// command or recorder needs a minimal Android environment instead.
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
    const ssize_t ignored = write(STDERR_FILENO, failed, sizeof(failed) - 1);
    (void)ignored;
    _exit(127);
}

// The app ignores SIGCHLD. This isolated supervisor owns one child and never
// restarts it: stopping or parent death signals that same continuous recorder.
[[noreturn]] void supervise(Kind kind, int result_fd, int control_fd, int log_fd,
                            int output_fd, long max_fd, const char* executable,
                            char* const argv[], const char* path, struct stat reservation) {
    close_inherited(max_fd, result_fd, control_fd, log_fd, output_fd);
    set_signal(SIGCHLD, SIG_DFL);
    set_signal(SIGPIPE, SIG_IGN);
    const pid_t child = fork();
    if (child == 0) {
        close(result_fd); close(control_fd);
        redirect_log(log_fd);
        set_signal(SIGINT, SIG_DFL); set_signal(SIGTERM, SIG_DFL);
        set_signal(SIGHUP, SIG_DFL); set_signal(SIGPIPE, SIG_DFL);
        if (output_fd >= 0 && fcntl(output_fd, F_SETFD, 0) != 0) _exit(126);
        exec_media(executable, argv);
    }
    if (output_fd >= 0) close(output_fd);
    bool stop_requested = false;
    int status = -1;
    if (child > 0) {
        const long long started = monotonic_ms();
        long long signalled = 0;
        bool force_killed = false;
        for (;;) {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) break;
            if (waited < 0 && errno != EINTR) { status = -1; break; }
            check_control(control_fd, stop_requested, 100);
            const long long now = monotonic_ms();
            // Give the exec'd helper time to install its signal handler. This
            // also makes an immediate stop produce a useful short recording.
            if (stop_requested && !signalled &&
                (kind == Kind::Screenshot || now - started >= 1000)) {
                (void)kill(child, kind == Kind::Video ? SIGINT : SIGTERM);
                signalled = now ? now : 1;
            }
            if (signalled && !force_killed && now - signalled >= 5000) {
                (void)kill(child, SIGKILL); force_killed = true;
            }
        }
        check_control(control_fd, stop_requested, 0);
        if (kind == Kind::Video && !stop_requested && status == 0) {
            static constexpr char early[] = "capture: recorder stopped unexpectedly\n";
            if (log_fd >= 0) { const ssize_t ignored = write(log_fd, early, sizeof(early) - 1); (void)ignored; }
            status = -1;
        }
    }
    // Only remove our empty reservation; nonempty output can contain complete
    // recoverable fragments even when the encoder or disk failed.
    remove_empty_reservation(path, reservation);
    if (log_fd >= 0) close(log_fd);
    ssize_t sent;
    do { sent = write(result_fd, &status, sizeof(status)); } while (sent < 0 && errno == EINTR);
    close(result_fd); close(control_fd);
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
    int output_fd = -1;
    struct stat reservation{};
    if (!ensure_directory(directory) || !reserve_path(directory, kind, path_, output_fd, reservation))
        return StartResult::StorageError;

    int results[2] = {-1, -1}, control[2] = {-1, -1};
    if (pipe2(results, O_CLOEXEC) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, control) != 0) {
        if (results[0] >= 0) close(results[0]);
        if (results[1] >= 0) close(results[1]);
        if (output_fd >= 0) close(output_fd);
        remove_empty_reservation(path_.c_str(), reservation); path_.clear();
        return StartResult::SpawnError;
    }
    const int flags = fcntl(results[0], F_GETFL, 0);
    if (flags < 0 || fcntl(results[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        close(results[0]); close(results[1]); close(control[0]); close(control[1]);
        if (output_fd >= 0) close(output_fd);
        remove_empty_reservation(path_.c_str(), reservation); path_.clear();
        return StartResult::SpawnError;
    }
    int log_fd = -1;
    if (!log_path.empty()) {
        log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (log_fd >= 0) (void)dprintf(log_fd, "\n[capture] %s -> %s\n", executable.c_str(), path_.c_str());
    }
    options = sanitize(options);
    char fd_arg[24], bitrate[24];
    std::snprintf(fd_arg, sizeof(fd_arg), "%d", output_fd);
    std::snprintf(bitrate, sizeof(bitrate), "%d", options.bitrate_mbps * 1000000);
    char* args[5] = {const_cast<char*>(executable.c_str()), nullptr, nullptr, nullptr, nullptr};
    if (kind == Kind::Screenshot) {
        args[1] = const_cast<char*>("-p"); args[2] = path_.data();
    } else {
        args[1] = const_cast<char*>("--capture-record"); args[2] = fd_arg; args[3] = bitrate;
    }
    const long max_fd = max_open_files();
    const pid_t supervisor = fork();
    if (supervisor == 0) {
        close(results[0]); close(control[0]);
        supervise(kind, results[1], control[1], log_fd, output_fd, max_fd, executable.c_str(),
                  args, path_.c_str(), reservation);
    }
    close(results[1]); close(control[1]);
    if (output_fd >= 0) close(output_fd);
    if (log_fd >= 0) close(log_fd);
    if (supervisor < 0) {
        close(results[0]); close(control[0]);
        remove_empty_reservation(path_.c_str(), reservation); path_.clear();
        return StartResult::SpawnError;
    }
    kind_ = kind; result_fd_ = results[0]; control_fd_ = control[0]; stopping_ = false;
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
    event.kind = kind_; event.path = path_;
    event.exit_code = n == sizeof(status) && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    event.success = event.exit_code == 0 && nonempty;
    shutdown();
    return true;
}

void Job::shutdown() {
    // EOF requests a graceful stop even after parent death. Do not block the
    // rendering/shutdown thread while the helper drains and finalizes media.
    if (control_fd_ >= 0) close(control_fd_);
    if (result_fd_ >= 0) close(result_fd_);
    control_fd_ = result_fd_ = -1; stopping_ = false; path_.clear();
}

StartResult Screenshot() {
    return g_screenshot.start(Kind::Screenshot, "/system/bin/screencap",
                              "/sdcard/Pictures/Benzware", capture_log_path());
}
StartResult StartVideo(VideoOptions options) {
    return g_video.start(Kind::Video, "/proc/self/exe",
                         "/sdcard/Movies/Benzware", capture_log_path(), options);
}
bool StopVideo() { return g_video.stop(); }
bool VideoRunning() { return g_video.running(); }
bool VideoStopping() { return g_video.stopping(); }
bool Poll(Event& event) { return g_screenshot.poll(event) || g_video.poll(event); }
void Shutdown() { g_screenshot.shutdown(); g_video.shutdown(); }
} // namespace screen_capture
