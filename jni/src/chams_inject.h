#pragma once
// chams_inject — разовый инжектор пейлоада чамсов (libimghelper.so) в процесс
// игры + синхронизация тумблера «Чамсы (хук)» с файлом-контроллером пейлоада.
//
// Механика: ptrace-инжект (remote dlopen) одним коротким захватом:
//   1. копируем .so из /data/local/tmp (или рядом с бинарём) в
//      /data/data/<pkg>/files/ с владельцем uid пакета — там файлу
//      app_data_file-контекст, его можно открыть из процесса игры;
//   2. PTRACE_ATTACH на главный поток, один remote-вызов mmap() для
//      строкового буфера, POKEDATA пути, remote-вызов dlopen(),
//   3. восстановление регистров и PTRACE_DETACH — окно захвата ~единицы мс.
// Никаких изменений кода игры здесь НЕТ: патчит текст игры только сам пейлоад
// и только при включённом тумблере (файл kCtlPath = «1»).
//
// Правила (KERNEL.md §4.4 — исключение из запрета ptrace): ptrace живёт
// ТОЛЬКО в этом разовом хелпере (CLI `chams-inject` и форк-поток из GUI на
// включение тумблера), процесс чит-оверлея память игры через ptrace/proc-mem
// НИКОГДА не трогает. Карта /proc/<pid>/maps читается — allowed (§4.4).
//
// FILL/статус (не проверено на устройстве): SELinux может отказать в dlopen
// файла из files/ (тогда chown+restorecon вручную), ptrace — EPERM без root
// или при активном чужом трейсере; таймаут восстановления регистров при
// мёртвой блокировке внутри игры ограничен kCallTimeoutMs.

#include <atomic>
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <thread>

namespace chams_inject {

// ============================== конфиг ====================================
namespace cfg {

// Пакет и пути пейлоада — ДОЛЖНЫ совпадать с chams_cfg в
// jni/src/chams/payload.cpp (там kPkgName/kCtlPath/kDiagPath).
inline constexpr const char* kPkgName  = "com.catsbit.oxidesurvivalisland";
inline constexpr const char* kCtlPath   = "/data/data/com.catsbit.oxidesurvivalisland/files/.imgh_state";
inline constexpr const char* kDiagPath  = "/data/data/com.catsbit.oxidesurvivalisland/files/.imgh_diag";
inline constexpr const char* kLibName   = "libimghelper.so";

// Откуда берём .so: (1) аргумент CLI, (2) рядом с нашим бинаром, (3) стандартный
// путь раскладки (adb push). В `PayloadSource()` резолвится по порядку.
inline constexpr const char* kSrcFallback = "/data/local/tmp/libimghelper.so";

inline constexpr int kCallTimeoutMs  = 4000;   // один remote-вызов (mmap/dlopen)
inline constexpr int kPollEveryMs    = 3000;   // период ретраев инжекта из GUI
inline constexpr int kMaxInjectTries = 10;     // дальше — только руками CLI

}  // namespace cfg

// ============================== мелочи ====================================
namespace detail {

inline uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

inline bool read_small(const char* path, char* out, size_t cap, size_t* n_out = nullptr) {
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = ::read(fd, out, cap - 1);
    ::close(fd);
    if (n < 0) n = 0;
    out[n] = '\0';
    if (n_out) *n_out = (size_t)n;
    return n >= 0;
}

// PID главного процесса игры: первое совпадение cmdline == пакету (без «:»).
inline pid_t find_game_pid() {
    DIR* d = ::opendir("/proc");
    if (!d) return -1;
    pid_t found = -1;
    while (dirent* e = ::readdir(d)) {
        char* end = nullptr;
        long pid = strtol(e->d_name, &end, 10);
        if (!end || *end || pid <= 1) continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
        char cmd[128];
        size_t n = 0;
        if (!read_small(path, cmd, sizeof(cmd), &n) || n == 0) continue;
        size_t pkg_len = strlen(cfg::kPkgName);
        if (n >= pkg_len && memcmp(cmd, cfg::kPkgName, pkg_len) == 0 &&
            (n == pkg_len || cmd[pkg_len] == '\0')) {
            found = (pid_t)pid;
            break;
        }
    }
    ::closedir(d);
    return found;
}

// Уже загружен ли пейлоад (ищем имя файла в карте игры).
inline bool payload_mapped(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "re");
    if (!f) return false;
    char line[512];
    bool hit = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, cfg::kLibName) != nullptr) { hit = true; break; }
    }
    fclose(f);
    return hit;
}

// База первого (низшего) маппинга файла в карте игры.
inline uint64_t file_base_in_maps(pid_t pid, const char* basename) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "re");
    if (!f) return 0;
    char line[512];
    uint64_t best = 0;
    while (fgets(line, sizeof(line), f)) {
        uint64_t s = 0, e = 0;
        char perm[8] = {};
        char file[256] = {};
        if (sscanf(line, "%llx-%llx %7s %*s %*s %*s %255[^\n]",
                   (unsigned long long*)&s, (unsigned long long*)&e, perm, file) < 3)
            continue;
        // file может начинаться с пробелов (path после поля) — sscanf %[^ ] их съест.
        const char* trimmed = file;
        while (*trimmed == ' ') ++trimmed;
        size_t bl = strlen(basename);
        size_t tl = strlen(trimmed);
        if (tl < bl || strcmp(trimmed + (tl - bl), basename) != 0) continue;
        if (tl > bl && trimmed[tl - bl - 1] != '/') continue;
        if (!best || s < best) best = s;
    }
    fclose(f);
    return best;
}

// Локальный символ → (файл, смещение) → адрес в чужом процессе: тот же билд
// системы (одни и те же libdl/libc на устройстве), разный ASLR. dlopen может
// жить в libdl, mmap — в libc; берём, что dladdr реально указал.
inline uint64_t target_sym(pid_t pid, void* local_sym) {
    Dl_info info{};
    if (!dladdr(local_sym, &info) || !info.dli_fname || !info.dli_fbase) return 0;
    uint64_t off = (uint64_t)local_sym - (uint64_t)info.dli_fbase;
    const char* base_name = strrchr(info.dli_fname, '/');
    base_name = base_name ? base_name + 1 : info.dli_fname;
    uint64_t map_base = file_base_in_maps(pid, base_name);
    if (!map_base) return 0;
    return map_base + off;
}

// ---- ptrace remote-call: выставляем regs, pc=fn, lr=0 → возврат в 0 даёт
// SIGSEGV, читаем x0, возвращаем исходные регистры.
struct Arm64Regs {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif
#ifndef PTRACE_GETREGSET
#define PTRACE_GETREGSET 0x4204
#endif
#ifndef PTRACE_SETREGSET
#define PTRACE_SETREGSET 0x4205
#endif

inline bool get_regs(pid_t pid, Arm64Regs* r) {
    iovec iov{r, sizeof(*r)};
    return ptrace(PTRACE_GETREGSET, pid, (void*)(uintptr_t)NT_PRSTATUS, &iov) == 0;
}
inline bool set_regs(pid_t pid, const Arm64Regs* r) {
    iovec iov{const_cast<Arm64Regs*>(r), sizeof(*r)};
    return ptrace(PTRACE_SETREGSET, pid, (void*)(uintptr_t)NT_PRSTATUS, &iov) == 0;
}

inline bool wait_stop(pid_t pid, int* sig, int timeout_ms) {
    uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
    for (;;) {
        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            if (WIFSTOPPED(status)) { *sig = WSTOPSIG(status); return true; }
            return false;  // процесс умер
        }
        if (now_ms() >= deadline) return false;
        ::usleep(2 * 1000);
    }
}

// Вызов fn(args..) в адресном пространстве игры. false = отказ/таймаут
// (регистры восстановлены, открепились).
inline bool remote_call(pid_t pid, uint64_t fn, const uint64_t args[6],
                        uint64_t* out_x0, int timeout_ms) {
    if (!fn) return false;
    Arm64Regs orig{};
    if (!get_regs(pid, &orig)) return false;
    Arm64Regs r = orig;
    r.sp &= ~0xFULL;
    for (int i = 0; i < 6; ++i) r.regs[i] = args[i];
    r.pc = fn;
    r.regs[30] = 0;  // lr = 0 → возврат ловится как SIGSEGV@0
    if (!set_regs(pid, &r)) return false;
    if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) != 0) {
        set_regs(pid, &orig);
        return false;
    }
    uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
    bool ok = false;
    bool timed_out = false;
    for (;;) {
        int sig = 0;
        int remain = (int)(deadline > now_ms() ? deadline - now_ms() : 1);
        if (!wait_stop(pid, &sig, remain)) { timed_out = true; break; }
        Arm64Regs cur{};
        if (!get_regs(pid, &cur)) break;
        if (sig == SIGSEGV && cur.pc == 0) {  // возврат по lr=0: штатный исход
            if (out_x0) *out_x0 = cur.regs[0];
            ok = true;
            break;
        }
        if (sig == SIGSEGV || sig == SIGILL || sig == SIGBUS || sig == SIGSYS) {
            break;  // фолт внутри вызова — чинить нечем, выходим
        }
        // Посторонний стоп (SIGCHLD/SIGSTOP/...): возвращаем исполнение.
        if (ptrace(PTRACE_CONT, pid, nullptr, (void*)(uintptr_t)(sig == SIGTRAP ? 0 : sig)) != 0) break;
    }
    if (timed_out) {
        // Вызов висит (например, мёртвая блокировка внутри игры): останавливаем
        // поток, возвращаем исходные регистры — игра продолжается с того же PC.
        (void)::kill(pid, SIGSTOP);
        int s2 = 0;
        (void)wait_stop(pid, &s2, 500);
    }
    set_regs(pid, &orig);
    return ok;
}

inline bool poke_str(pid_t pid, uint64_t addr, const char* s) {
    size_t len = strlen(s) + 1;
    size_t words = (len + 7) / 8;
    for (size_t i = 0; i < words; ++i) {
        uint64_t w = 0;
        memcpy(&w, s + i * 8, (i * 8 + 8 <= len) ? 8 : (len - i * 8));
        if (ptrace(PTRACE_POKEDATA, pid, (void*)(uintptr_t)(addr + i * 8),
                   (void*)(uintptr_t)w) != 0)
            return false;
    }
    return true;
}

// Путь к нашему же бинару (для поиска .so рядом с ним).
inline void own_dir(char* out, size_t cap) {
    char exe[256] = {};
    ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) { out[0] = '\0'; return; }
    exe[n] = '\0';
    char* slash = strrchr(exe, '/');
    if (!slash) { out[0] = '\0'; return; }
    *slash = '\0';
    snprintf(out, cap, "%s", exe);
}

}  // namespace detail

// Откуда взять пейлоад: явный аргумент > рядом с бинаром > /data/local/tmp.
inline const char* payload_source(const char* explicit_path, char* buf, size_t cap) {
    if (explicit_path && explicit_path[0]) return explicit_path;
    char dir[256];
    detail::own_dir(dir, sizeof(dir));
    if (dir[0]) {
        snprintf(buf, cap, "%s/%s", dir, cfg::kLibName);
        if (::access(buf, R_OK) == 0) return buf;
    }
    snprintf(buf, cap, "%s", cfg::kSrcFallback);
    return buf;
}

// Копия .so в каталог приложения (uid пакета, права 0755) под путём dlopen.
inline bool deploy_payload(pid_t pid, const char* src) {
    (void)pid;
    char dir[256];
    snprintf(dir, sizeof(dir), "/data/data/%s", cfg::kPkgName);
    struct stat st{};
    if (::stat(dir, &st) != 0) return false;
    uid_t uid = st.st_uid;
    gid_t gid = st.st_gid;
    char files[300];
    snprintf(files, sizeof(files), "%s/files", dir);
    if (::stat(files, &st) != 0) {
        if (::mkdir(files, 0755) != 0 && errno != EEXIST) return false;
        (void)::chown(files, uid, gid);
    }
    int in = ::open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) return false;
    char dst[320];
    snprintf(dst, sizeof(dst), "%s/%s", files, cfg::kLibName);
    int out = ::open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
    if (out < 0) { ::close(in); return false; }
    char buf[64 * 1024];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(in, buf, sizeof(buf))) > 0) {
        if (::write(out, buf, (size_t)n) != n) { ok = false; break; }
    }
    ::close(in);
    (void)::fchown(out, uid, gid);
    (void)::fchmod(out, 0755);
    ::close(out);
    if (!ok) return false;
    return true;
}

// Полный цикл: копия + ptrace-dlopen. true = пейлоад в карте игры.
inline bool inject_once(const char* src_hint, bool log) {
    pid_t pid = detail::find_game_pid();
    if (pid <= 0) { if (log) printf("chams-inject: игра (%s) не запущена\n", cfg::kPkgName); return false; }
    if (detail::payload_mapped(pid)) {
        if (log) printf("chams-inject: пейлоад уже загружен (pid %d)\n", pid);
        return true;
    }
    char buf[320];
    const char* src = payload_source(src_hint, buf, sizeof(buf));
    if (::access(src, R_OK) != 0) {
        if (log) printf("chams-inject: нет файла пейлоада: %s\n", src);
        return false;
    }
    if (!deploy_payload(pid, src)) {
        if (log) printf("chams-inject: не удалось развернуть .so в каталог игры (root?)\n");
        return false;
    }
    char dst[320];
    snprintf(dst, sizeof(dst), "/data/data/%s/files/%s", cfg::kPkgName, cfg::kLibName);

    // Символы в целевом процессе: тот же системный билд → своё dladdr + база
    // соответствующего файла в карте игры.
    uint64_t t_mmap   = detail::target_sym(pid, (void*)(uintptr_t)&::mmap);
    uint64_t t_dlopen = detail::target_sym(pid, (void*)(uintptr_t)&::dlopen);
    if (!t_mmap || !t_dlopen) {
        if (log) printf("chams-inject: не резолвятся mmap/dlopen в процессе игры\n");
        return false;
    }

    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) != 0) {
        if (log) printf("chams-inject: ptrace attach: %s (root? другой трейсер?)\n", strerror(errno));
        return false;
    }
    int sig = 0;
    if (!detail::wait_stop(pid, &sig, 3000)) {
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        if (log) printf("chams-inject: нет стопа после attach\n");
        return false;
    }

    bool ok = false;
    do {
        uint64_t args[6] = {0, 0x1000, 3 /*RW*/, 0x22 /*PRIV|ANON*/, (uint64_t)-1, 0};
        uint64_t buf_addr = 0;
        if (!detail::remote_call(pid, t_mmap, args, &buf_addr, cfg::kCallTimeoutMs) || buf_addr < 0x10000) {
            if (log) printf("chams-inject: remote mmap не прошёл\n");
            break;
        }
        if (!detail::poke_str(pid, buf_addr, dst)) {
            if (log) printf("chams-inject: poke пути не прошёл\n");
            break;
        }
        uint64_t dargs[6] = {buf_addr, 2 /*RTLD_NOW*/, 0, 0, 0, 0};
        uint64_t handle = 0;
        if (!detail::remote_call(pid, t_dlopen, dargs, &handle, cfg::kCallTimeoutMs) || handle == 0) {
            if (log) printf("chams-inject: dlopen вернул 0 (SELinux/restorecon?)\n");
            break;
        }
        ok = true;
    } while (false);

    ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
    if (!ok) (void)::kill(pid, SIGCONT);  // снять возможный SIGSTOP с таймаут-восстановления

    if (ok) {
        // Дать конструктору отработать и убедиться, что .so в карте.
        for (int i = 0; i < 25 && !detail::payload_mapped(pid); ++i) ::usleep(20 * 1000);
        ok = detail::payload_mapped(pid);
    }
    if (log) {
        if (ok) printf("chams-inject: ок, пейлоад загружен (pid %d)\n", pid);
        else    printf("chams-inject: инжект не удался\n");
    }
    return ok;
}

// ====================== синхронизация тумблера ============================
namespace detail {
inline std::atomic<bool>  g_ever{false};
inline std::atomic<bool>  g_want{false};
inline std::atomic<bool>  g_busy{false};
inline int                g_tries = 0;
inline uint64_t           g_next_try_ms = 0;

inline bool write_ctl(char c) {
    int fd = ::open(cfg::kCtlPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    bool ok = ::write(fd, &c, 1) == 1;
    ::close(fd);
    return ok;
}
}  // namespace detail

// Разовый CLI: `benzware chams-inject [путь к libimghelper.so]`.
// Файл-контроллер НЕ трогает — активность решает тумблер в меню.
inline int Run(int argc, char** argv) {
    const char* src = (argc >= 2 && argv[1] && argv[1][0] && argv[1][0] != '-') ? argv[1] : nullptr;
    printf("chams-inject: контрольный файл %s (писать будет тумблер меню)\n", cfg::kCtlPath);
    bool ok = inject_once(src, true);
    char diag = '?';
    size_t n = 0;
    if (detail::read_small(cfg::kDiagPath, &diag, 2, &n) && n == 1)
        printf("chams-inject: статус пейлоада: '%c' (0 простой,2 чужая сборка,3 хук стоит,4 ждём выключение,5 шейдер,6 кадр ок)\n", diag);
    return ok ? 0 : 1;
}

// Вызывается каждый кадр главного цикла GUI. Дёшево: пока тумблер не менялся
// и инжект не нужен — только сравнение и monotonic-часы.
inline void SyncFromState(bool want) {
    using namespace detail;
    if (!want && !g_ever.load(std::memory_order_acquire)) return;  // старт без тумблера = ноль действий
    bool prev = g_want.exchange(want, std::memory_order_acq_rel);
    if (want != prev) {
        g_ever.store(true, std::memory_order_release);
        g_tries = 0;
        g_next_try_ms = 0;
        write_ctl(want ? '1' : '0');
        if (!want) return;  // выключение пейлоад разберётся сам (кадр → восстановление → unhook)
    }
    if (!want) return;
    if (g_busy.load(std::memory_order_acquire)) return;
    uint64_t now = now_ms();
    if (now < g_next_try_ms) return;
    g_next_try_ms = now + (uint64_t)cfg::kPollEveryMs;
    if (g_tries >= cfg::kMaxInjectTries) return;
    pid_t pid = find_game_pid();
    if (pid <= 0) return;              // игра ещё не запущена — попробуем позже
    if (payload_mapped(pid)) return;   // уже загружен
    ++g_tries;
    g_busy.store(true, std::memory_order_release);
    std::thread([] {
        inject_once(nullptr, false);
        g_busy.store(false, std::memory_order_release);
    }).detach();
}

// Выход из GUI: выключить пейлоад, даже если игра останется жить.
inline void Shutdown() {
    if (!detail::g_ever.load(std::memory_order_acquire)) return;
    detail::write_ctl('0');
}

}  // namespace chams_inject
