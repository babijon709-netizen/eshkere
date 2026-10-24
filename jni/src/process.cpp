#include "process.h"
#include "game.h"
#include "maps_lookup.h"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

// Единственный поддерживаемый клиент: пакет текущего релиза игры.
static const char* const kTargetPackage = "com.catsbit.oxidesurvivalisland";

pid_t g_target_pid = -1;
bool  g_esp_attached = false;
std::atomic<bool> g_attach_scanned{false};
// Official package or a renamed/cloned package candidate. Both still pass
// esp_init's release TypeInfo validation before attachment is accepted.
static bool g_target_by_package = true;
static int g_alive_check_failures = 0;
static constexpr int kAliveCheckFailureLimit = 3;
static std::thread g_attach_thread;
static std::atomic<bool> g_attach_running{false};

// ---- Поиск процесса игры ----------------------------------------------------
// Раньше брался первый процесс, у которого cmdline совпал с пакетом. Процессов
// с таким cmdline на устройстве бывает несколько (клон приложения, рабочий
// профиль, «второе пространство», дочерний fork/crashpad), и не у каждого
// живой инициализированный libil2cpp.so: если попадался не тот, привязка
// оставалась ложной навсегда — меню рисовалось, а все функции молчали.

// 2 — основной процесс пакета, 1 — его дочерний/клон (pkg:... / *oxidesurvivalisland*), -1 — не он.
static int cmdline_rank(pid_t pid, const char* const* pkgs, int pkg_count) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char command[256] = {};
    const ssize_t n = read(fd, command, sizeof(command) - 1);
    close(fd);
    if (n <= 0) return -1;
    // Trim trailing spaces/newlines in case argv was padded.
    size_t cmd_len = strlen(command);
    while (cmd_len > 0 && (command[cmd_len - 1] == ' ' || command[cmd_len - 1] == '\n' ||
                           command[cmd_len - 1] == '\r' || command[cmd_len - 1] == '\t')) {
        command[--cmd_len] = '\0';
    }
    for (int i = 0; i < pkg_count; ++i) {
        if (!pkgs[i] || !pkgs[i][0]) continue;
        if (strcmp(command, pkgs[i]) == 0) return 2;
        const size_t len = strlen(pkgs[i]);
        if (strncmp(command, pkgs[i], len) == 0 &&
            (command[len] == ':' || command[len] == '.' || command[len] == '_')) {
            return command[len] == ':' ? 1 : 2;
        }
    }
    if (strstr(command, "oxidesurvivalisland") != nullptr) return 1;
    return -1;
}

static bool pid_cmdline_matches(pid_t pid) {
    const char* pkgs[1] = {kTargetPackage};
    return cmdline_rank(pid, pkgs, 1) >= 1;
}

// Число потоков в /proc/<pid>/task (до cap): у живого процесса Unity десятки
// потоков (UnityMain, UnityGfxDeviceW, Job.Worker, GC и т.д.), а у дочернего
// fork()-хендлера (crashpad/watchdog) — всего 1..3 потока.
static int proc_thread_count(pid_t pid, int cap = 16) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", (int)pid);
    DIR* dir = opendir(path);
    if (!dir) return 0;
    int count = 0;
    struct dirent* e;
    while (count < cap && (e = readdir(dir)) != nullptr) {
        if (e->d_name[0] >= '1' && e->d_name[0] <= '9') ++count;
    }
    closedir(dir);
    return count;
}

// 1 — libil2cpp.so отображён (включая split APK), 0 — нет, -1 — карта не читается.
static int proc_libil2cpp(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    std::string text;
    char buf[8192];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            text.append(buf, (size_t)n);
            if (text.size() > (8u << 20)) break;   // столько карта не занимает
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(fd);
    uint64_t bases[4] = {};
    return maps::lookup_library_bases(text, "libil2cpp.so", bases, 4) > 0 ? 1 : 0;
}

struct PidCandidate {
    pid_t pid;
    int   rank;
    int   threads;
};

static int find_official_candidates(PidCandidate* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    DIR* dir = opendir("/proc");
    if (!dir) return 0;
    const char* pkgs[1] = {kTargetPackage};
    const pid_t self = getpid();
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        const pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0 || pid == self) continue;
        const int rank = cmdline_rank(pid, pkgs, 1);
        if (rank < 0) continue;
        if (proc_libil2cpp(pid) != 1) continue;
        const int threads = proc_thread_count(pid, 16);
        if (count < capacity) {
            out[count++] = {pid, rank, threads};
        }
    }
    closedir(dir);
    // Сортируем: сначала многопоточные (>= 8 потоков), затем по убыванию rank,
    // затем более свежий (больший) pid.
    for (int i = 1; i < count; ++i) {
        const PidCandidate cur = out[i];
        int j = i - 1;
        while (j >= 0) {
            const bool cur_mt = cur.threads >= 8;
            const bool prv_mt = out[j].threads >= 8;
            const bool better = (cur_mt != prv_mt) ? cur_mt
                              : (cur.rank != out[j].rank) ? (cur.rank > out[j].rank)
                              : (cur.pid > out[j].pid);
            if (!better) break;
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = cur;
    }
    return count;
}

static pid_t find_game_pid() {
    PidCandidate list[16] = {};
    const int n = find_official_candidates(list, 16);
    if (n > 0) {
        g_target_by_package = true;
        return list[0].pid;
    }
    return -1;
}

// A release APK may be installed through an app clone/work profile with a
// renamed applicationId. Find its Unity process as a candidate; esp_init then
// accepts it only when the fixed release TypeInfo offsets resolve the expected
// game classes, so this does not introduce another supported build.
static int find_renamed_release_candidates(pid_t* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    DIR* dir = opendir("/proc");
    if (!dir) return 0;
    const pid_t self = getpid();
    PidCandidate temp[16] = {};
    int count = 0;
    struct dirent* entry;
    while (count < 16 && (entry = readdir(dir))) {
        const pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0 || pid == self) continue;
        char path[32];
        snprintf(path, sizeof(path), "/proc/%d", pid);
        struct stat st{};
        if (stat(path, &st) != 0 || st.st_uid < 10000) continue;
        if (proc_libil2cpp(pid) != 1) continue;
        temp[count++] = {pid, 0, proc_thread_count(pid, 16)};
    }
    closedir(dir);
    for (int i = 1; i < count; ++i) {
        const PidCandidate cur = temp[i];
        int j = i - 1;
        while (j >= 0) {
            const bool cur_mt = cur.threads >= 8;
            const bool prv_mt = temp[j].threads >= 8;
            const bool better = (cur_mt != prv_mt) ? cur_mt : (cur.pid > temp[j].pid);
            if (!better) break;
            temp[j + 1] = temp[j];
            --j;
        }
        temp[j + 1] = cur;
    }
    const int written = count < capacity ? count : capacity;
    for (int i = 0; i < written; ++i) out[i] = temp[i].pid;
    return written;
}

// Процесс всё ещё тот самый? Одно чтение cmdline вместо полного перебора /proc:
// перебор каждые 1.5 с виден в ядре, а привязку проверять нужно.
static bool pid_still_game(pid_t pid) {
    char path[32];
    snprintf(path, sizeof(path), "/proc/%d", pid);
    if (access(path, F_OK) != 0) return false;
    if (g_target_by_package && !pid_cmdline_matches(pid)) return false;
    // Полное чтение /proc/<pid>/maps у Unity-игры — тысячи VMA под mmap_lock
    // процесса игры: на слабом устройстве каждое чтение притормаживает саму игру
    // (аллокатор, подгрузка), а значит и кадры, с которых оверлей читает память.
    // Раньше шло каждые 1.5 с. Смену процесса с тем же pid ловят access/cmdline
    // выше и проверка памяти (esp_alive_check); карту достаточно сверять редко.
    static int s_calls = 0;
    if ((s_calls++ % 8) != 0) return true;
    return proc_libil2cpp(pid) == 1;
}

void start_attach_thread() {
    g_attach_running.store(true);
    g_attach_thread = std::thread([]() {
        while (g_attach_running.load()) {
            if (!g_esp_attached) {
                pid_t attached_pid = -1;
                PidCandidate officials[16] = {};
                const int official_count = find_official_candidates(officials, 16);
                g_attach_scanned.store(true);

                // Pass 1: strict TypeInfo resolution across all official PIDs.
                for (int i = 0; i < official_count; ++i) {
                    if (!esp_init(officials[i].pid, false)) continue;
                    attached_pid = officials[i].pid;
                    g_target_by_package = true;
                    break;
                }
                // Pass 2: strict TypeInfo resolution across renamed/cloned Unity PIDs.
                if (attached_pid <= 0) {
                    pid_t candidates[16] = {};
                    const int count = find_renamed_release_candidates(candidates, 16);
                    for (int i = 0; i < count; ++i) {
                        if (!esp_init(candidates[i], false)) continue;
                        attached_pid = candidates[i];
                        g_target_by_package = false;
                        break;
                    }
                }
                // Pass 3: early-loading fallback for the main multi-threaded
                // official process while TypeInfo statics are still null.
                if (attached_pid <= 0) {
                    for (int i = 0; i < official_count; ++i) {
                        if (officials[i].rank < 2 || officials[i].threads < 8) continue;
                        if (!esp_init(officials[i].pid, true)) continue;
                        attached_pid = officials[i].pid;
                        g_target_by_package = true;
                        break;
                    }
                }
                if (attached_pid > 0) {
                    g_target_pid = attached_pid;
                    g_esp_attached = true;
                    g_alive_check_failures = 0;
                }
            } else {
                const bool same_process = pid_still_game(g_target_pid);
                bool memory_alive = true;
                if (same_process) {
                    if (esp_alive_check()) g_alive_check_failures = 0;
                    else {
                        ++g_alive_check_failures;
                        memory_alive = g_alive_check_failures < kAliveCheckFailureLimit;
                    }
                }
                if (!same_process || !memory_alive) {
                    // Process death is immediate; isolated /proc/<pid>/mem EIO
                    // failures are tolerated. They are common under load and
                    // must not make every memory feature blink off together.
                    g_esp_attached = false;
                    g_target_pid = -1;
                    g_alive_check_failures = 0;
                    esp_reset();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    });
}

void stop_attach_thread() {
    g_attach_running.store(false);
    if (g_attach_thread.joinable()) g_attach_thread.join();
}
void process_detach() {
    esp_reset();
    g_esp_attached = false;
    g_target_pid = -1;
    g_alive_check_failures = 0;
}

// CLI-команда dump: тонкие обёртки над теми же поисками, что у потока
// привязки (process.h), — без состояния потока и без чтения памяти.
pid_t process_find_game_pid() { return find_game_pid(); }
int process_find_unity_candidates(pid_t* out, int capacity) {
    return find_renamed_release_candidates(out, capacity);
}

bool process_game_process_running() {
    DIR* dir = opendir("/proc");
    // Fail closed: without a reliable process scan do not touch the live
    // game's PlayerPrefs file.
    if (!dir) return true;
    const char* pkgs[1] = {kTargetPackage};
    const pid_t self = getpid();
    bool found = false;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        const pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0 || pid == self) continue;
        if (cmdline_rank(pid, pkgs, 1) >= 0) { found = true; break; }
    }
    closedir(dir);
    return found;
}
