#include "attach_log.h"
#include "scoped_text.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace attachlog {
namespace {

std::mutex  g_mu;
bool        g_started = false;
std::string g_last;
int         g_repeat = 0;

void stamp(char* out, size_t cap) {
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv{};
    localtime_r(&ts.tv_sec, &tmv);
    snprintf(out, cap, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
             (int)(ts.tv_nsec / 1000000));
}

void append(const std::string& line) {
    const int fd = open(BW_TEXT("/data/local/benzware/attach.log").c_str(),
                        O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd < 0) return;
    const ssize_t n = write(fd, line.data(), line.size());
    (void)n;
    close(fd);
}

// Первая запись за процесс: прошлую сессию в attach.prev.log, текущий файл — с нуля.
void start_session_locked() {
    g_started = true;
    const std::string dir = BW_TEXT("/data/local/benzware/").c_str();
    mkdir(dir.c_str(), 0777);
    const std::string cur = dir + BW_TEXT("attach.log").c_str();
    const std::string prev = dir + BW_TEXT("attach.prev.log").c_str();
    rename(cur.c_str(), prev.c_str());
}

void flush_repeat_locked() {
    if (g_repeat > 0) {
        char line[96];
        snprintf(line, sizeof(line), "             (повторено %d раз)\n", g_repeat);
        append(line);
        g_repeat = 0;
    }
}

} // namespace

void say(const char* fmt, ...) {
    char body[480];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_started) start_session_locked();
    if (g_last == body) { ++g_repeat; return; }
    flush_repeat_locked();

    char ts[32];
    stamp(ts, sizeof(ts));
    std::string line = "[";
    line += ts;
    line += "] ";
    line += body;
    line += '\n';
    append(line);
    g_last = body;
}

bool due_every(double seconds, double& mark) {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    if (mark > 0.0 && now - mark < seconds) return false;
    mark = now;
    return true;
}

const char* backend_name(int backend) {
    switch (backend) {
        case 0: return "прямое чтение (/proc/pid/mem)";
        case 1: return "FT-драйвер";
        case 2: return "RT-Dev";
        case 3: return "RT-Hook";
        case 4: return "ditPro";
        default: return "неизвестно";
    }
}

} // namespace attachlog
