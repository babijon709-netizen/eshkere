#pragma once
// stealth.h — маскировка имени процесса и защита от отладчика.
//
// Поверхности видимости, которые закрывает модуль (всё из userspace):
//   /proc/<pid>/cmdline — фейк «vold …» поверх области argv (область
//       ограничена длиной командной строки запуска: ядро читает
//       arg_start..arg_end, растянуть её из userspace нельзя);
//   /proc/<pid>/environ — строки окружения зануляются (su/TERM и пр.);
//   /proc/<pid>/status  — Name = «vold» (PR_SET_NAME); новые потоки
//       наследуют это имя; PR_SET_DUMPABLE=0 закрывает /proc/<pid>
//       чужим не-root процессам;
//   /proc/<pid>/fd      — 0/1/2 уходят с терминала в /dev/null (TTY «?»
//       в ps после setsid), fd-лист чист (см. kernel_io/mem_io);
//   отладчики           — потомок занимает ptrace-слот (PTRACE_SEIZE):
//       gdb/lldb/Frida получают EPERM. Потомок при этом ОБЯЗАН супервизить
//       родителя (waitpid + PTRACE_CONT, переправка каждого сигнала):
//       seize без супервизии замораживает процесс на первом же сигнале
//       (signal-delivery-stop) — так GUI завис в 7caffec.
//
// Не покрывается из userspace (честно): /proc/<pid>/maps всё равно
// показывает региона с меткой «(deleted)»; ppid остаётся su/shell;
// TracerPid != 0 в status (плата за занятый ptrace-слот).
// Исполняемый файл не удаляется: запуск через _exit и аварийное завершение
// не должны оставлять каталог без бинарника.

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>

#if defined(__ANDROID__) || defined(__linux__)
#include <sys/ptrace.h>
#define STEALTH_PTRACE_OK 1
#else
#define STEALTH_PTRACE_OK 0
#endif

namespace stealth {

inline constexpr const char kComm[] = "vold";   // comm ≤ 15 символов

inline int g_daemon_pipe_wr = -1;

inline void WriteTextFile(const char* path, const char* data, size_t len) {
    if (!path || !data || !len) return;
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)::write(fd, data, len);
        ::close(fd);
    }
}

// Поднимаем полные root-привилегии, группу AID_READPROC (3009) и SELinux-домен
// без MCS-категорий (:s0). На Android 10–16 /proc смонтирован с
// hidepid=2,gid=3009: без GID 3009 в дополнительных группах и при запрете
// ptrace_may_access политикой SELinux (или из-за унаследованных от терминала
// категорий s0:cXXX,cYYY в KernelSU/APatch) процесс игры вообще не виден в
// /proc, а открытие /proc/<pid>/maps и process_vm_readv возвращают EACCES.
inline void ElevateCredentials() {
    (void)::setgid(0);
    (void)::setuid(0);

    // 0=root, 1000=system, 1003=graphics, 1004=input, 1007=log,
    // 2000=shell, 3003=inet, 3004=net_raw, 3005=net_admin, 3009=readproc
    static const gid_t kRootGroups[] = {
        0, 1000, 1003, 1004, 1007, 2000, 3003, 3004, 3005, 3009
    };
    (void)::setgroups(sizeof(kRootGroups) / sizeof(kRootGroups[0]), kRootGroups);

#if defined(SYS_capset)
    struct CapHeader {
        uint32_t version;
        int      pid;
    } hdr = {0x20080522U /* _LINUX_CAPABILITY_VERSION_3 */, 0};
    struct CapData {
        uint32_t effective;
        uint32_t permitted;
        uint32_t inheritable;
    } data[2] = {
        {0xffffffffU, 0xffffffffU, 0xffffffffU},
        {0xffffffffU, 0xffffffffU, 0xffffffffU},
    };
    (void)::syscall(SYS_capset, &hdr, data);
#endif

    char attr[128]{};
    int fd = ::open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = ::read(fd, attr, sizeof(attr) - 1);
        ::close(fd);
        if (n > 0) {
            attr[n] = '\0';
            // Если контекст сохранил MCS-категории приложения (:c...) или
            // не-root домен (untrusted_app/shell), пробуем перейти в чистый :s0.
            if (strstr(attr, ":c") != nullptr ||
                strstr(attr, "untrusted_app") != nullptr ||
                strstr(attr, ":shell:") != nullptr) {
                static const char* const kContexts[] = {
                    "u:r:magisk:s0",
                    "u:r:su:s0",
                    "u:r:kernel:s0",
                };
                for (const char* ctx : kContexts) {
                    int wfd = ::open("/proc/self/attr/current", O_WRONLY | O_CLOEXEC);
                    if (wfd < 0) break;
                    ssize_t wn = ::write(wfd, ctx, strlen(ctx));
                    ::close(wfd);
                    if (wn > 0) break;
                }
            }
        }
    }
}

// Вывод процесса и всех его потоков из cgroup приложения (MT Manager /
// терминала) в листовую cgroup uid_0/pid_<pid> и защита от Android LMK
// (oom_score_adj = -1000). На Android 12–16 (cgroup v2) у корневой
// /sys/fs/cgroup включён cgroup.subtree_control, поэтому прямая запись в
// /sys/fs/cgroup/cgroup.procs отклоняется ядром с EBUSY (no internal process
// constraint), и процесс оставался в /sys/fs/cgroup/uid_<mt>/pid_<mt>, где его
// убивал PhantomProcessList / killProcessGroup (SIGKILL) или замораживал
// cgroup.freeze при сворачивании/закрытии MT Manager.
inline void ProtectProcess(pid_t pid) {
    if (pid <= 0) return;
    if (pid == getpid()) ElevateCredentials();
    char pid_str[32]{};
    int len = snprintf(pid_str, sizeof(pid_str), "%d\n", (int)pid);
    if (len <= 0) return;

    // Создаём собственные листовые cgroup под uid_0, чтобы миграция из
    // uid_<app>/pid_<app> проходила на cgroup v2 (Android 12–16) и /acct.
    (void)::mkdir("/sys/fs/cgroup/uid_0", 0755);
    char cg_leaf_dir[96]{};
    snprintf(cg_leaf_dir, sizeof(cg_leaf_dir), "/sys/fs/cgroup/uid_0/pid_%d", (int)pid);
    (void)::mkdir(cg_leaf_dir, 0755);

    char cg_leaf_procs[128]{};
    snprintf(cg_leaf_procs, sizeof(cg_leaf_procs), "%s/cgroup.procs", cg_leaf_dir);
    WriteTextFile(cg_leaf_procs, pid_str, (size_t)len);

    char cg_leaf_freeze[128]{};
    snprintf(cg_leaf_freeze, sizeof(cg_leaf_freeze), "%s/cgroup.freeze", cg_leaf_dir);
    WriteTextFile(cg_leaf_freeze, "0\n", 2);

    (void)::mkdir("/acct/uid_0", 0755);
    char acct_leaf_dir[96]{};
    snprintf(acct_leaf_dir, sizeof(acct_leaf_dir), "/acct/uid_0/pid_%d", (int)pid);
    (void)::mkdir(acct_leaf_dir, 0755);

    char acct_leaf_procs[128]{};
    snprintf(acct_leaf_procs, sizeof(acct_leaf_procs), "%s/cgroup.procs", acct_leaf_dir);
    WriteTextFile(acct_leaf_procs, pid_str, (size_t)len);

    char acct_leaf_tasks[128]{};
    snprintf(acct_leaf_tasks, sizeof(acct_leaf_tasks), "%s/tasks", acct_leaf_dir);
    WriteTextFile(acct_leaf_tasks, pid_str, (size_t)len);

    static const char* const kCgroupFiles[] = {
        "/sys/fs/cgroup/cgroup.procs",
        "/acct/cgroup.procs",
        "/acct/tasks",
        "/dev/cpuctl/cgroup.procs",
        "/dev/cpuctl/tasks",
        "/dev/cpuset/foreground/cgroup.procs",
        "/dev/cpuset/foreground/tasks",
        "/dev/cpuset/top-app/cgroup.procs",
        "/dev/cpuset/top-app/tasks",
        "/dev/cpuset/cgroup.procs",
        "/dev/cpuset/tasks",
        "/dev/stune/foreground/cgroup.procs",
        "/dev/stune/foreground/tasks",
        "/dev/stune/top-app/cgroup.procs",
        "/dev/stune/top-app/tasks",
        "/dev/stune/cgroup.procs",
        "/dev/stune/tasks",
        "/dev/freezer/cgroup.procs",
        "/dev/freezer/tasks",
    };
    for (const char* path : kCgroupFiles) {
        WriteTextFile(path, pid_str, (size_t)len);
    }

    static const char kMinOomScore[] = "-1000\n";
    static const char kMinOomAdj[]   = "-17\n";
    char oom_path[64]{};
    snprintf(oom_path, sizeof(oom_path), "/proc/%d/oom_score_adj", (int)pid);
    WriteTextFile(oom_path, kMinOomScore, sizeof(kMinOomScore) - 1);
    snprintf(oom_path, sizeof(oom_path), "/proc/%d/oom_adj", (int)pid);
    WriteTextFile(oom_path, kMinOomAdj, sizeof(kMinOomAdj) - 1);

    if (pid == getpid()) {
        DIR* td = ::opendir("/proc/self/task");
        if (td) {
            struct dirent* de = nullptr;
            while ((de = ::readdir(td)) != nullptr) {
                if (de->d_name[0] < '1' || de->d_name[0] > '9') continue;
                char tid_str[32]{};
                int tlen = snprintf(tid_str, sizeof(tid_str), "%s\n", de->d_name);
                if (tlen <= 0) continue;
                WriteTextFile(acct_leaf_tasks, tid_str, (size_t)tlen);
                WriteTextFile("/dev/cpuctl/tasks", tid_str, (size_t)tlen);
                WriteTextFile("/dev/cpuset/foreground/tasks", tid_str, (size_t)tlen);
                WriteTextFile("/dev/stune/foreground/tasks", tid_str, (size_t)tlen);
                char toom[96]{};
                snprintf(toom, sizeof(toom), "/proc/self/task/%s/oom_score_adj", de->d_name);
                WriteTextFile(toom, kMinOomScore, sizeof(kMinOomScore) - 1);
            }
            ::closedir(td);
        }
    }
}

// Ранняя демонизация ДО создания любых потоков (Binder/EGL/Touch/Audio) и ДО
// ApplyTracerGuard: в MT Manager процесс стартует лидером группы (pid == pgid),
// из-за чего setsid() без предварительного fork() всегда возвращает EPERM, а
// процесс остаётся в группе и сессии терминала MT Manager.
// Родитель ждёт сигнала от ApplyRuntime() (чтобы стартовые сообщения успели
// вывестись в терминал), после чего завершается, а рабочий процесс остаётся
// сиротой у init (ppid = 1) в собственной сессии.
inline void DaemonizeEarly() {
    signal(SIGHUP,  SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    int pfd[2]{-1, -1};
    if (pipe(pfd) != 0) return;

    const pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return;
    }
    if (pid > 0) {
        // Родитель: ждём, пока потомок поднимет оверлей и тач (или упадёт до
        // ApplyRuntime), и тихо выходим.
        close(pfd[1]);
        char ch = 0;
        while (read(pfd[0], &ch, 1) < 0 && errno == EINTR) {}
        close(pfd[0]);
        _exit(0);
    }

    // Потомок: гарантированно не лидер группы -> setsid() создаёт новую сессию
    // и отвязывает от управляющего терминала MT Manager.
    close(pfd[0]);
    g_daemon_pipe_wr = pfd[1];
    fcntl(g_daemon_pipe_wr, F_SETFD, FD_CLOEXEC);
    setsid();
    chdir("/");
    ProtectProcess(getpid());
}

// ---- 1) маскировка имени: cmdline/environ/comm/dumpable -------------------
// Вызывать строго ПОСЛЕ разбора аргументов (строки argv затираются) и до
// создания потоков (наследуют comm).
inline void ApplyEarly(int argc, char* argv[]) {
    // Закрываем только унаследованные от MT Manager / терминала дескрипторы
    // (/dev/pts/*, /data/*, pipe/socket от шелла), не трогая системные fd
    // bionic/линкера (/dev/__properties__ и т.д.).
    for (int fd = 3; fd < 256; ++fd) {
        if (fd == g_daemon_pipe_wr) continue;
        char path[64]{}, target[256]{};
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        const ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = '\0';
        if (strncmp(target, "/dev/pts/", 9) == 0 ||
            strncmp(target, "/data/", 6) == 0 ||
            strncmp(target, "pipe:", 5) == 0) {
            close(fd);
        }
    }

    if (!argv || !argv[0]) return;

    // Границы области cmdline: строки argv лежат в стеке подряд, ядро
    // показывает /proc/<pid>/cmdline ровно от arg_start до arg_end.
    char* begin = argv[0];
    char* end = begin + strlen(begin) + 1;
    for (int i = 1; i < argc; ++i) {
        if ((char*)argv[i] > begin && (char*)argv[i] < begin + 0x8000) {
            const char* e = argv[i] + strlen(argv[i]) + 1;
            if (e > end) end = (char*)e;
        }
    }
    const size_t cap = (size_t)(end - begin);

    // Окружение зануляем: /proc/<pid>/environ читается root'ом и выдаёт
    // следы запуска (su, TERM, путь шелла). getenv в коде не используется.
    // (environ объявлен глобально в <unistd.h>.)
    if (environ)
        for (char** e = environ; *e; ++e) memset(*e, 0, strlen(*e));

    // Фейк cmdline: самый длинный из влезающих в область (область = длина
    // командной строки запуска). Остаток — пробелы: ни кусков настоящей
    // строки, ни пустых «аргументов» из нулей в хвосте.
    static const char kFakeLong[] =
        "/system/bin/vold --blkidcontext=u:r:blkid:s0 --fsckcontext=u:r:fsck:s0";
    static const char kFakeMid[]  = "/system/bin/vold";
    static const char kFakeMin[]  = "vold";
    if (cap >= 2) {
        const char* fake = (sizeof(kFakeLong) <= cap) ? kFakeLong
                        : (sizeof(kFakeMid)  <= cap) ? kFakeMid
                        : (sizeof(kFakeMin)  <= cap) ? kFakeMin : nullptr;
        memset(begin, ' ', cap);
        begin[cap - 1] = 0;
        if (fake) memcpy(begin, fake, strlen(fake));
    }

    prctl(PR_SET_NAME, (unsigned long)kComm, 0, 0, 0);
    // /proc/<pid> — только root; core-дампы не собираются.
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
}

// ---- 2) защита от не-root отладчиков без ptrace-stop рисков ---------------
//
// PR_SET_DUMPABLE=0 под uid 0 уже запрещает ptrace любым не-root процессам
// (включая процесс игры и античит). Отдельный дочерний процесс с PTRACE_SEIZE
// не используется: на Android 12–16 фоновые демоны/чистильщики OEM (MIUI
// PowerKeeper/Joyose, ColorOS Hans, Samsung Freecess) замораживают или убивают
// спящий в waitpid дочерний процесс, из-за чего основной процесс чита
// навсегда застревает в TASK_TRACED (signal-delivery-stop) или завершается.
inline void ApplyTracerGuard() {
    signal(SIGCHLD, SIG_IGN);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
}

// ---- 3) рантайм-скрытие: терминал -----------------------------------------
// Вызывать после успешного подъёма оверлея и тача, до экрана выбора режима.
inline void ApplyRuntime() {
    // Отвязка от терминала: без сессии ps показывает TTY «?». Отказ не
    // критичен (если мы уже лидер сессии после DaemonizeEarly).
    setsid();
    ProtectProcess(getpid());

    // 0/1/2 больше не указывают на /dev/pts: в fd-листе чита нет терминала,
    // из которого его запустили.
    const int nul = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nul >= 0) {
        dup2(nul, 0); dup2(nul, 1); dup2(nul, 2);
        if (nul > 2) close(nul);
    }

    // Отпускаем родительский процесс в терминале MT Manager: оверлей и тач
    // уже подняты, стартовые строки выведены, дальше работаем полностью
    // автономно (ppid = 1).
    if (g_daemon_pipe_wr >= 0) {
        const char done = 1;
        (void)write(g_daemon_pipe_wr, &done, 1);
        close(g_daemon_pipe_wr);
        g_daemon_pipe_wr = -1;
    }

    // ptrace-слот занимает ApplyTracerGuard (ранний fork, с супервизией
    // сигналов) — здесь больше ничего трогать не нужно.
}

}  // namespace stealth
