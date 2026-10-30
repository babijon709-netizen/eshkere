#include "process.h"
#include "game.h"
#include "maps_lookup.h"
#include "runtime_guard.h"
#include "scoped_text.h"
#include "attach_log.h"

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

#ifndef BENZWARE_BUILD_SHA
#define BENZWARE_BUILD_SHA "host"
#endif
#ifndef BENZWARE_BUILD_DATE
#define BENZWARE_BUILD_DATE "-"
#endif

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
    bool complete = true;
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            text.append(buf, (size_t)n);
            if (text.size() > (8u << 20)) { complete = false; break; }
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(fd);
    runtime_guard::observe_maps(pid, text, complete);
    uint64_t bases[4] = {};
    return maps::lookup_library_bases(text, BW_TEXT("libil2cpp.so").c_str(), bases, 4) > 0 ? 1 : 0;
}

struct PidCandidate {
    pid_t pid;
    int   rank;
    int   threads;
};

static int find_official_candidates(PidCandidate* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    int count = 0;
    DIR* dir = opendir("/proc");
    if (dir) {
        const char* pkgs[1] = {kTargetPackage};
        const pid_t self = getpid();
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            const pid_t pid = (pid_t)atoi(entry->d_name);
            if (pid <= 0 || pid == self) continue;
            const int rank = cmdline_rank(pid, pkgs, 1);
            if (rank < 0) continue;
            const int has_il2cpp = proc_libil2cpp(pid);
            if (has_il2cpp != 1 && !(esp_get_mem_backend() == ESP_MEM_DITPRO && has_il2cpp < 0)) {
                continue;
            }
            const int threads = proc_thread_count(pid, 16);
            if (count < capacity) {
                out[count++] = {pid, rank, threads};
            }
        }
        closedir(dir);
    }
    if (esp_get_mem_backend() == ESP_MEM_DITPRO && count < capacity) {
        const pid_t kpid = esp_find_kernel_pid(kTargetPackage);
        if (kpid > 0 && kpid != getpid()) {
            bool present = false;
            for (int i = 0; i < count; ++i) {
                if (out[i].pid == kpid) { present = true; break; }
            }
            if (!present) {
                const int threads = proc_thread_count(kpid, 16);
                out[count++] = {kpid, 2, threads > 0 ? threads : 16};
            }
        }
    }
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
    if (access(path, F_OK) != 0) {
        if (esp_get_mem_backend() == ESP_MEM_DITPRO &&
            esp_find_kernel_pid(kTargetPackage) == pid) {
            return true;
        }
        return false;
    }
    if (g_target_by_package && !pid_cmdline_matches(pid)) {
        if (esp_get_mem_backend() != ESP_MEM_DITPRO ||
            esp_find_kernel_pid(kTargetPackage) != pid) {
            return false;
        }
    }
    // Полное чтение /proc/<pid>/maps у Unity-игры — тысячи VMA под mmap_lock
    // процесса игры: на слабом устройстве каждое чтение притормаживает саму игру
    // (аллокатор, подгрузка), а значит и кадры, с которых оверлей читает память.
    // Раньше шло каждые 1.5 с. Смену процесса с тем же pid ловят access/cmdline
    // выше и проверка памяти (esp_alive_check); карту достаточно сверять редко.
    static int s_calls = 0;
    if ((s_calls++ % 8) != 0) return true;
    const int has_il2cpp = proc_libil2cpp(pid);
    if (has_il2cpp < 0 && esp_get_mem_backend() == ESP_MEM_DITPRO) return true;
    return has_il2cpp == 1;
}

void start_attach_thread() {
    g_attach_running.store(true);
    g_attach_thread = std::thread([]() {
        attachlog::say("сессия: сборка %s от %s, транспорт %s",
                       BENZWARE_BUILD_SHA, BENZWARE_BUILD_DATE,
                       attachlog::backend_name(esp_get_mem_backend()));
        double scan_mark = 0.0;
        double heartbeat_mark = 0.0;
        while (g_attach_running.load()) {
            if (!g_esp_attached) {
                const bool report = attachlog::due_every(5.0, scan_mark);
                pid_t attached_pid = -1;
                int attached_pass = 0;
                PidCandidate officials[16] = {};
                const int official_count = find_official_candidates(officials, 16);
                g_attach_scanned.store(true);
                if (report) {
                    attachlog::say("поиск: процессов пакета %s с libil2cpp — %d",
                                   kTargetPackage, official_count);
                    if (official_count == 0) {
                        attachlog::say("поиск: процесса с cmdline %s и libil2cpp.so в maps нет — игра не запущена или ещё не дошла до il2cpp",
                                       kTargetPackage);
                    }
                    for (int i = 0; i < official_count; ++i) {
                        attachlog::say("  кандидат: pid=%d, rank=%d, потоков=%d",
                                       (int)officials[i].pid, officials[i].rank, officials[i].threads);
                    }
                }

                // Pass 1: strict TypeInfo resolution across all official PIDs.
                for (int i = 0; i < official_count; ++i) {
                    if (!esp_init(officials[i].pid, false)) continue;
                    attached_pid = officials[i].pid;
                    attached_pass = 1;
                    g_target_by_package = true;
                    break;
                }
                // Pass 2: strict TypeInfo resolution across renamed/cloned Unity PIDs.
                if (attached_pid <= 0) {
                    pid_t candidates[16] = {};
                    const int count = find_renamed_release_candidates(candidates, 16);
                    if (report && count > 0) {
                        attachlog::say("поиск: переименованных/клон-кандидатов с libil2cpp — %d", count);
                    }
                    for (int i = 0; i < count; ++i) {
                        if (!esp_init(candidates[i], false)) continue;
                        attached_pid = candidates[i];
                        attached_pass = 2;
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
                        attached_pass = 3;
                        g_target_by_package = true;
                        break;
                    }
                }
                if (attached_pid > 0) {
                    g_target_pid = attached_pid;
                    g_esp_attached = true;
                    g_alive_check_failures = 0;
                    attachlog::say("ПОДКЛЮЧЕНО: pid=%d, пасс=%d, потоков=%d",
                                   (int)attached_pid, attached_pass,
                                   proc_thread_count(attached_pid, 64));
                    heartbeat_mark = 0.0;
                } else if (report && official_count > 0) {
                    attachlog::say("не подключено: кандидаты есть, но ни один не прошёл привязку (см. строки привязки выше)");
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
                    if (!same_process) {
                        attachlog::say("ОТВАЛ: pid=%d больше не игра (процесс умер, cmdline сменился или libil2cpp выгружен)",
                                       (int)g_target_pid);
                    } else {
                        attachlog::say("ОТВАЛ: pid=%d, память недоступна %d проверок подряд (esp_alive_check)",
                                       (int)g_target_pid, g_alive_check_failures);
                    }
                    g_esp_attached = false;
                    g_target_pid = -1;
                    g_alive_check_failures = 0;
                    esp_reset();
                } else if (attachlog::due_every(15.0, heartbeat_mark)) {
                    const EspAttachStatus st = esp_attach_status();
                    attachlog::say("жив: pid=%d, база=0x%llx, PlayerManager=%s, GameController=%s, локальный игрок=%s, чтений=%llu, ошибок чтения=%llu",
                                   (int)g_target_pid, (unsigned long long)st.il2cpp_base,
                                   st.player_manager ? "ok" : "нет",
                                   st.game_controller ? "ok" : "нет",
                                   st.local_player ? "найден" : "не найден",
                                   esp_mem_reads_total(), esp_mem_read_fails_total());
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
