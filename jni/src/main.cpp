#include "main.h"
#include "game.h"
#include "Blur/Blur.h"
#include "Android_touch/TouchHelperA.h"
#include "audio.h"
#include "process.h"
#include "hud.h"
#include "esp_draw.h"
#include "aim.h"
#include "silent_aim.h"
#include "farm.h"
#include "widgets.h"
#include "menu.h"
#include "config.h"
#include "theme.h"
#include "app_state.h"
#include "overlay_geometry.h"
#include "mode_select.h"
#include "metadata_dump.h"
#include "skeleton_diag.h"
#include "mem_io.h"
#include "kernel_io.h"
#include "stealth.h"
#include "FakeLag.h"
#include <sys/socket.h>
#include <sys/utsname.h>
#include <cerrno>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <sys/stat.h>
#include <thread>
#include <iostream>
#include <string>

// Диагностика кернел-режима из терминала (Termux/adb), без оверлея и игры:
//   su -c /data/local/tmp/benzware --check-driver   — базовая проверка
//   su -c /data/local/tmp/benzware --scan-driver    — + полный скан кодов команд
//   su -c /data/local/tmp/benzware dump             — рантайм-дамп global-metadata.dat
//                                                  запущенной игры в benzware/dump/
//   su -c /data/local/tmp/benzware skel             — диагностика конвейера костей
//                                                  (почему нет скелетов)
// Печатает: DitPro KPM detection + INIT/READ self-test, семейство FT-сокета,
// RT-Dev/RT-Hook и живые пробы по собственной памяти. --scan-driver также
// печатает карту кодов FT 0..0x7FF. Коды выхода: 0 — транспорт подтверждён,
// 1 — драйвер не найден, 2 — обнаружен, но self-test/ABI не прошёл.
namespace driver_check {

static int Run(bool full_scan) {
    struct utsname un{};
    if (uname(&un) != 0) un.release[0] = 0;
    printf("== benzware: проверка кернел режима%s ==\n",
           full_scan ? " (полный скан кодов команд)" : "");
    printf("ядро устройства : %s\n", un.release);
    printf("uid / euid      : %d / %d%s\n", getuid(), geteuid(),
           geteuid() == 0 ? " (root)" : "  <-- НЕ ROOT");
    if (geteuid() != 0)
        printf("!! кернел режим требует root: запусти команду через su.\n"
               "   Запуск: su -c <путь>/benzware --check-driver\n");

    errno = 0;
    const long ditpro_probe = memio::ditpro_probe_result();
    const int ditpro_probe_errno = errno;
    printf("DitPro syscall 18: ");
    if (ditpro_probe == 131)
        printf("KPM 131 обнаружен (0x100/0x200)\n");
    else
        printf("KPM 131 не подтверждён (ответ %ld, errno %d)\n",
               ditpro_probe, ditpro_probe_errno);

    // 1) Поиск семейства сокета FT-драйвера по маячку на пробе socket(fam, 6, 0):
    //    1338 — bing-класс, 126/ENOKEY — gongchuang-класс. Раннего выхода
    //    здесь НЕТ: RT-Dev и RT-Hook не используют этот сокет, их блоки ниже проверяются
    //    независимо от этого результата.
    int beacon = 0;
    const int fam = memio::driver_find_family(&beacon);
    const bool ft_present = (fam >= 0);
    if (!ft_present) {
        printf("FT-драйвер      : не найден (маячки errno %d/%d на семействах %d..%d)\n",
               memio::kDriverProbeErrno, memio::kGongProbeErrno,
               memio::kDriverFamilyFirst, memio::kDriverFamilyLast);
    } else {
        printf("FT-драйвер      : найден, семейство сокета %d, маячок errno %d (%s)\n",
               fam, beacon,
               beacon == memio::kDriverProbeErrno ? "bing-класс" : "gongchuang-класс");
    }

    // 2) Сокеты обоих классов (какие поднимутся) + живые пробы команд по
    //    СОБСТВЕННОЙ памяти: /proc не участвует, данные сойдутся, только если
    //    ядро реально сходило по страницам нашего процесса.
    memio::KernelTransport& kt = memio::kernel_transport();
    int bfd = -1, gfd = -1;
    if (ft_present) {
        errno = 0;
        bfd = kt.connect();
        printf("сокет bing      : %s%s\n",
               bfd >= 0 ? "поднят (тип 4)" : "не поднялся",
               bfd >= 0 ? "" : (errno ? " — не bing-класс" : ""));
        errno = 0;
        gfd = kt.gong_connect();
        printf("сокет gong      : %s%s\n",
               gfd >= 0 ? "поднят (SOCK_RAW/0x090419)" : "не поднялся",
               gfd >= 0 ? "" : " — не gongchuang-класс");

        printf("пробы известных команд (своя память, игра не трогается):\n");
        const int cand[4] = { memio::kDriverCmdRead, memio::kDriverCmdWrite, 0x300, 0x303 };
        for (int i = 0; i < 4; ++i) {
            const memio::CmdProbe p = kt.classify_cmd(cand[i]);
            if (p.kind == memio::CMD_READ)
                printf("  код 0x%03X: ЧТЕНИЕ — память -> наш буфер, OK\n", cand[i]);
            else if (p.kind == memio::CMD_WRITE)
                printf("  код 0x%03X: ЗАПИСЬ — наш буфер -> память, OK\n", cand[i]);
            else if (p.err != 0)
                printf("  код 0x%03X: отказ, errno %d\n", cand[i], p.err);
            else
                printf("  код 0x%03X: принята, но данные не двигались\n", cand[i]);
        }
        if (gfd >= 0) {
            const memio::CmdProbe vm = kt.classify_gong_vm(gfd, memio::kGongCmdAccessVm);
            printf("  gong op4      : %s\n", vm.kind == memio::CMD_READ
                   ? "ACCESS_PROCESS_VM работает (копия from -> to)" : "нет");
            if (kt.gong_attach((int)getpid())) {
                const memio::CmdProbe pr = kt.classify_pte(gfd, memio::kGongCmdPteRead);
                const memio::CmdProbe pw = kt.classify_pte(gfd, memio::kGongCmdPteWrite);
                printf("  gong op5/op6  : чтение %s, запись %s\n",
                       pr.kind == memio::CMD_READ ? "OK" : "нет",
                       pw.kind == memio::CMD_WRITE ? "OK" : "нет");
            }
        }
    }

    // 2б) RT-Dev (RTkernel из загруженного архива): char-устройство со
    //     случайным именем /dev/[A-GNY]{1,6}. Проверяем R/W 0x801/0x802 по себе.
    bool rt_dev_ok = false;
    {
        memio::RtDevTransport& rt = memio::rt_dev_transport();
        const memio::CmdProbe rp = rt.classify();
        if (rp.kind == memio::CMD_READ) {
            printf("RT-Dev         : узел %s, fd %d\n", rt.device_path(), rt.device_fd());
            printf("  чтение 0x801 / запись 0x802: OK (своя память)\n");
            rt_dev_ok = true;
        } else if (rp.err == ENOENT) {
            printf("RT-Dev         : узел /dev/[A-GNY]{1,6} не найден (RTkernel не загружен)\n");
        } else if (rp.err != 0) {
            printf("RT-Dev         : RTkernel-кандидат не подтвердил ABI 0x801/0x802, errno %d\n", rp.err);
        } else {
            printf("RT-Dev         : RTkernel-кандидат не двигал данные\n");
        }
    }

    // 2в) RT-Hook (RT-hook.7z): у модуля нет ни сокета, ни /dev-узла — он
    //     подменяет sys_call_table[__NR_ioctl=29]. Проба идёт по собственной
    //     памяти через ioctl(-1, 0x259, ...): без хука ядро вернёт EBADF.
    bool rt_hook_ok = false;
    {
        memio::RtHookTransport& rt = memio::rt_hook_transport();
        const memio::CmdProbe rp = rt.classify();
        if (rp.kind == memio::CMD_READ) {
            printf("RT-Hook        : хук syscall ioctl стоит\n");
            printf("  чтение 0x259 / запись 0x25a: OK (своя память)\n");
            rt_hook_ok = true;
        } else if (rp.err == EBADF) {
            printf("RT-Hook        : хука нет (ioctl уходит в ядро без перехвата)\n");
        } else if (rp.err == EPERM) {
            printf("RT-Hook        : хук стоит, но проба без root (гейт uid/euid/capable)\n");
        } else if (rp.err != 0) {
            printf("RT-Hook        : проба: отказ, errno %d\n", rp.err);
        } else {
            printf("RT-Hook        : ioctl принят, но данные не двигались\n");
        }
    }

    // 3) Автоопределение протокола FT — ровно то, что делает чит при выборе
    //    кернела (короткий список, затем полный скан при неудаче).
    if (ft_present) {
        kt.detect_protocol();
        printf("авторизация 0x300: %s\n",
               kt.auth_sent() ? "пароль отправлен (флаг в драйвере)" : "не потребовалась");
        printf("протокол        : %s%s\n", kt.proto_name(),
               kt.map_known() ? " (подтверждён пробами)" : " (НЕ подтверждён — коды дефолтные)");
        printf("транспорт команд: %s\n",
               kt.cmd_via_ioctl() ? "ioctl(fd, код, &req) — сборка 5.10-класса"
                                  : "getsockopt(fd, pid, код, &req, &len)");
    }

    // 4) Полный скан 0..0x7FF: карта ВСЕХ кодов, которые двигают данные.
    //    Нужен, если и автоопределение ничего не нашло: тогда по этой карте
    //    видно, какими кодами говорит эта сборка драйвера.
    if (full_scan && ft_present) {
        printf("полный скан кодов 0x000..0x7FF, bing-структура {pid,addr,buf,size,magic}:\n");
        int moved = 0, silent = 0, rd = 0, wr = 0;
        long errs = 0;
        for (int c = 0; c <= 0x7FF; ++c) {
            const memio::CmdProbe p = kt.classify_cmd(c);
            if (p.kind == memio::CMD_READ) {
                printf("  код 0x%03X: ЧТЕНИЕ\n", c);
                ++moved; if (!rd) rd = c;
            } else if (p.kind == memio::CMD_WRITE) {
                printf("  код 0x%03X: ЗАПИСЬ\n", c);
                ++moved; if (!wr) wr = c;
            } else if (p.err == 0) {
                printf("  код 0x%03X: принята, данные не двигались\n", c);
                ++silent;
            } else {
                ++errs;
            }
        }
        printf("итог скана      : команд с данными %d, «молчащих» %d, отказов %ld\n",
               moved, silent, errs);
        if (gfd >= 0 && kt.gong_attach((int)getpid())) {
            printf("скан pte-структурой {vaddr,buffer,size} на gong-сокете, коды 0..15:\n");
            for (int c = 0; c <= 15; ++c) {
                const memio::CmdProbe p = kt.classify_pte(gfd, c);
                if (p.kind == memio::CMD_READ)       printf("  код 0x%02X: ЧТЕНИЕ\n", c);
                else if (p.kind == memio::CMD_WRITE) printf("  код 0x%02X: ЗАПИСЬ\n", c);
                else if (p.err == 0)                 printf("  код 0x%02X: молчит\n", c);
                else                                 printf("  код 0x%02X: errno %d\n", c, p.err);
            }
        }
    }

    // DitPro 131: additionally prove the KPM read ABI against this process's own
    // stack memory. This diagnostic neither touches the game nor hides/uninstalls.
    bool ditpro_ok = false;
    if (ditpro_probe == 131) {
        memio::KernelTransport dit;
        if (dit.select_ditpro() && dit.open(getpid())) {
            const uint64_t expected = 0xD17A0131C0DEC0DEULL;
            uint64_t observed = 0;
            errno = 0;
            const ssize_t n = dit.read_once((uint64_t)(uintptr_t)&expected,
                                            &observed, sizeof(observed));
            const int read_errno = errno;
            dit.close();
            ditpro_ok = n == (ssize_t)sizeof(observed) && observed == expected;
            if (ditpro_ok)
                printf("DitPro read ABI : OK (чтение собственной памяти)\n");
            else
                printf("DitPro read ABI : ОШИБКА (read=%zd, errno=%d, value=%#llx)\n",
                       n, read_errno, (unsigned long long)observed);
        } else {
            printf("DitPro read ABI : не удалось привязать/выполнить чтение для pid=%d\n",
                   (int)getpid());
        }
    }

    // 5) Итог: чем чит будет ходить в память игры. Классы независимы —
    //    достаточно любого подтверждённого транспорта.
    if (ditpro_ok) {
        printf("итог            : DitPro KPM 131 готов — syscall 18, чтение подтверждено;\n"
               "                  /proc не участвует\n");
        return 0;
    }
    if (kt.map_known()) {
        printf("итог            : FT готов — протокол %s, чтение кодом 0x%03X,\n"
               "                  запись кодом 0x%03X; /proc не участвует\n",
               kt.proto_name(), kt.read_cmd(), kt.write_cmd());
        return 0;
    }
    if (rt_dev_ok) {
        printf("итог            : RT-Dev готов — чтение 0x801 / запись 0x802; /proc не участвует\n");
        return 0;
    }
    if (rt_hook_ok) {
        printf("итог            : RT-Hook готов — syscall ioctl, чтение 0x259 / запись 0x25a;\n"
               "                  ни сокета, ни /dev-узла; /proc не участвует\n");
        return 0;
    }
    if (ditpro_probe == 131) {
        printf("итог            : DitPro 131 отвечает на check, но READ-тест не прошёл.\n");
        return 2;
    }
    if (ft_present) {
        printf("итог            : коды команд НЕ подтверждены. Включай кернел в меню —\n"
               "                  привязка к игре проверит транспорт по-настоящему;\n"
               "                  если откажет (тост с errno) — пришли вывод:\n"
               "                  su -c <путь>/benzware --scan-driver\n");
        return 2;
    }
    printf("итог            : НИ ОДИН класс драйвера не найден (DitPro/FT/RT-Dev/RT-Hook).\n"
           "   Прошей драйвер под СВОЮ версию ядра (uname -r) скриптом .sh\n"
           "   (drivers.7z — FT, rt dev Драйвера root cheat.zip — RT-Dev, RT-hook.7z — RT-Hook)\n"
           "   и повтори проверку. Драйверы не переживают перезагрузку —\n"
           "   после ребута прошивай заново.\n");
    const int fd = ::open("/proc/1/mem", O_RDWR | O_CLOEXEC);
    printf("экстернал       : /proc/<pid>/mem %s\n",
           fd >= 0 ? "доступен (root)" : "недоступен");
    if (fd >= 0) close(fd);
    return 1;
}

}  // namespace driver_check

// Хук инициализации (пустая заглушка — точка расширения).
namespace prot {
static void Init() {}
}

float g_sw = 1920.f;
float g_sh = 1080.f;
std::atomic<bool> main_thread_flag{true};
std::atomic<bool> g_frame_done{true};
InputState g_input;
AppState g_state;
static FakeLag g_fakeLag;

static void ArmShutdownDeadline() {
    static std::atomic<bool> armed{false};
    if (armed.exchange(true)) return;
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(8));
        // A kernel transport call can be uninterruptible. Once the visible
        // Surface is gone and critical cleanup had its deadline, terminate the
        // process instead of leaving background workers alive for a minute.
        // iptables-правило лага и захват тача/uinput снимаем до _exit без join
        // и без аллокаций (не перехватить только SIGKILL и SIGSTOP).
        Touch_EmergencyRelease();
        if (g_fakeLag.active()) FakeLag::unblockRaw();
        _exit(0);
    }).detach();
}

namespace {
struct sigaction g_crash_old[5]{};
const int kCrashSignals[5] = { SIGQUIT, SIGABRT, SIGSEGV, SIGBUS, SIGILL };

void CrashCleanupHandler(int sig) {
    // Снимаем всё установленное вне процесса (EVIOCGRAB, UI_DEV_CREATE,
    // iptables), если процесс падает по фатальному сигналу (не перехватить
    // только SIGKILL и SIGSTOP).
    Touch_EmergencyRelease();
    FakeLag::unblockRaw();
    for (size_t i = 0; i < sizeof(kCrashSignals) / sizeof(kCrashSignals[0]); ++i) {
        if (kCrashSignals[i] == sig) {
            sigaction(sig, &g_crash_old[i], nullptr);
            break;
        }
    }
    raise(sig);
}

void InstallCrashSignalCleanup() {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = CrashCleanupHandler;
    sa.sa_flags = SA_RESETHAND;
    for (size_t i = 0; i < sizeof(kCrashSignals) / sizeof(kCrashSignals[0]); ++i) {
        sigaction(kCrashSignals[i], &sa, &g_crash_old[i]);
    }
}
} // namespace

namespace startup_key {
// Это только базовая заставка-пароль: строка публичная, поэтому не является
// защитой от анализа или обхода бинаря.
static constexpr char kLaunchKey[] = "t.me/benzware";

static bool Verify() {
    std::cout << "Введите ключ для запуска: " << std::flush;
    std::string entered;
    if (!std::getline(std::cin, entered)) {
        std::cerr << "Не удалось прочитать ключ из окна запуска MT Manager.\n";
        return false;
    }
    if (entered != kLaunchKey) {
        std::cerr << "Неверный ключ. Запуск отменён.\n";
        return false;
    }
    return true;
}
}  // namespace startup_key

int main(int argc, char* argv[]) {
    // Проверка драйвера из терминала (Termux/adb): su -c ./benzware --check-driver
    // (базовая) или --scan-driver (полный скан кодов команд) — без GUI,
    // до запуска оверлея и игры.
    // dump — рантайм-дамп global-metadata.dat из памяти запущенной игры
    // (metadata_dump.h): тоже без GUI и маскировки, короткоживущий процесс
    // с терминальным выводом; транспорт — аргумент → .mode → трейлер
    // конфига, изучение — явным шагом до bind, без переключений в сессии.
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check-driver") == 0) return driver_check::Run(false);
        if (strcmp(argv[i], "--scan-driver") == 0) return driver_check::Run(true);
        if (strcmp(argv[i], "dump") == 0 || strcmp(argv[i], "--dump") == 0)
            return metadump::Run(argc - i, argv + i);
        if (strcmp(argv[i], "skel") == 0 || strcmp(argv[i], "--skel") == 0)
            return skeldiag::Run(argc - i, argv + i);
    }

    // Ввод выполняем до отделения от MT Manager, пока доступен его поток ввода.
    // Диагностические команды выше остаются отдельным CLI-режимом без оверлея.
    if (!startup_key::Verify()) return 1;

    // Ранняя демонизация (fork + setsid + перенос из cgroup MT Manager +
    // oom_score_adj = -1000) до создания любых потоков и стража-трейсера.
    stealth::DaemonizeEarly();

    // Страж-трейсер САМЫМ ПЕРВЫМ делом после разбора аргументов: процесс ещё
    // однопоточен (чистый fork), а PTRACE_SEIZE обязан случиться ДО
    // PR_SET_DUMPABLE=0 в ApplyEarly — не-root среда иначе не даст потомку
    // захватить родителя (на устройстве мы root и захват пройдёт в любом
    // порядке, но держим единый порядок). Потомок занимает ptrace-слот и
    // ОБЯЗАТЕЛЬНО супервизит родителя — переправляет каждый сигнал дальше
    // (без супервизии первое же сигнал замораживает GUI, см. stealth.h).
    stealth::ApplyTracerGuard();
    // Маскировка имени процесса: затираем cmdline/environ и ставим comm
    // «vold». Строго после разбора аргументов (строки argv уничтожаем) и
    // до создания потоков (наследуют comm). --check-driver запускается без
    // маскировки: это секундная диагностика из терминала, ей нужен вывод.
    stealth::ApplyEarly(argc, argv);

    // SIGHUP, SIGINT и SIGTERM приходят от закрытия терминала/вкладки или
    // Process.destroy() в MT Manager — игнорируем их (выход из оверлея штатно
    // идёт через кнопку закрытия в меню).
    signal(SIGHUP,  SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    InstallCrashSignalCleanup();

    prot::Init();
    screen_config();

    mkdir(CfgDir(), 0777);
    const OverlayGeometry overlay =
        MakeOverlayGeometry(displayInfo.width, displayInfo.height);

    g_sw = static_cast<float>(overlay.scene_width);
    g_sh = static_cast<float>(overlay.scene_height);

    native_window_screen_x = overlay.scene_width;
    native_window_screen_y = overlay.scene_height;
    // Surface оставляем квадратным, как в old_source. Android композитор сам
    // поворачивает/обрезает overlay при смене ориентации; прямоугольный
    // landscape Surface (max x min) в portrait показывал только его верхний
    // фрагмент. Квадрат max x max инвариантен к 0/90/180/270 градусам, а
    // видимая геометрия меню всё равно ограничивается реальными width/height.
    if (!initGUI_draw(overlay.surface_side, overlay.surface_side, true)) return -1;
    Blur::Init();
    CfgWatchInit();
    AudioInit();
    if (!Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, false))
        Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, true);
    // Полное скрытие процесса: оверлей и тач подняты — дальше живём
    // «невидимкой»: отвязка от терминала и ptrace-слот, занятый потомком
    // (attach отладчиков запрещён). Бинарник остаётся на диске.
    stealth::ApplyRuntime();
    // Если полноценный тач не поднялся с первого раза (гонка за /dev/uinput
    // или grab на старте — обычное дело сразу после запуска игры), чит раньше
    // навсегда оставался в read-only: автофарм «просто не идёт», пока не
    // перезапустишь. Теперь фоновый поток раз в 3 секунды пробует поднять
    // инъекцию заново, пока не получится.
    static std::atomic<bool> s_touchRetryRun{true};
    std::thread([]() {
        while (s_touchRetryRun.load() && main_thread_flag.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (Touch_CanInject()) continue;
            if (Touch_IsInitialized()) {
                Touch_TryUpgradeInject();
                continue;
            }
            if (!Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, false))
                Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, true);
        }
    }).detach();
    // Версия выбирается до транспорта и до первого bind: весь reader видит
    // один согласованный набор оффсетов на протяжении сессии.
    if (!modesel::RunVersionSelect() || !modesel::RunModeSelect()) {
        // Приложение закрыли, не выбрав режим: выходим тем же порядком,
        // что и обычный выход (привязки и чит-конвейеров ещё нет — их
        // остановка здесь пустая, но последовательность та же).
        // Arm the deadline before touching EGL: a wedged compositor must not
        // keep the last overlay frame on screen indefinitely.
        ArmShutdownDeadline();
        g_fakeLag.destroy();
        Blur::Free();
        shutdown();
        Touch_Close();
        silent_aim::shutdown();
        esp_freecam_set(false);
        stop_attach_thread();
        esp_viewmodel_reset(true);
        process_detach();
        CfgWatchFree();
        AudioFree();
        _exit(0);
    }
    start_attach_thread();
    stealth::ProtectProcess(getpid());
    LoadTabIcons();
    ApplyTheme();
    RestoreLang();          // язык из прошлого запуска (конфиг ниже может его переписать)
    RestoreAccent();        // акцент интерфейса из прошлого запуска
    RestoreLayout();        // кнопки и тач-зоны из прошлого запуска (точку аима конфиг ниже может переписать)
    CfgScanDir();
    ConfigLoadLast();
    ApplyTheme();
    CenterMenuOnDisplay();
    g_menuFadeIn = 0.f;

    unsigned long frame = 0;
    while (main_thread_flag) {
        ++frame;
        if ((frame % 300UL) == 1UL) {
            stealth::ProtectProcess(getpid());
        }
        g_frame_done.store(false);
        // Вскрытие тормозов оверлея (лог 07:37: кадр встаёт на 200–400 мс
        // пачками, писатель при этом ровный — стоит именно main). Если кадр
        // дольше 100 мс — пишем, в какой стадии он стоял и сколько из этого
        // наш cpu: мало cpu = поток не работал, а ждал чужого (своп/GPU или
        // чужую блокировку). Гладкие 60 fps (17 мс) молчат. Замеры — vdso,
        // на кадр стоят сотни наносекунд.
        const auto slowWall0 = std::chrono::steady_clock::now();
        struct timespec slowCpu0{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &slowCpu0);
        double slowT[7]; int slowI = 0;
        auto slowMark = [&]() {
            slowT[slowI++] = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - slowWall0).count();
        };
        slowMark();   // 0: начало кадра
        drawBegin();

        esp_mem_frame_begin();   // кадр начался: кэш блоков памяти игры сброшен
        esp_viewmodel_tick(g_state.viewmodel); // independent of ESP/aim/farm
        // FakeLag: дроп UDP через iptables (DROP без очереди — пакеты не копятся,
        // после выключения пинг сразу нормализуется). Управляется тумблером в
        // «Разное → Сеть» и отдельной кнопкой на экране.
        {
            static bool lastOn = false;
            static int lastPct = -1;
            if (g_state.fake_lag != lastOn) {
                if (g_state.fake_lag) {
                    g_fakeLag.setPercent((int)g_state.fake_lag_percent);
                    g_fakeLag.apply(true);
                } else {
                    g_fakeLag.apply(false);
                }
                lastOn = g_state.fake_lag;
                lastPct = (int)g_state.fake_lag_percent;
            } else if (g_state.fake_lag) {
                int cur = (int)g_state.fake_lag_percent;
                if (cur != lastPct) {
                    g_fakeLag.setPercent(cur);
                    lastPct = cur;
                }
            }
        }
        DrawEspOverlay();
        slowMark();   // 1: после ESP
        UpdateAim(ImGui::GetIO().DeltaTime);
        slowMark();   // 2: после аима
        UpdateFarm(ImGui::GetIO().DeltaTime);
        slowMark();   // 3: после фарма
        // Индикация привязки (тосты «игра найдена / не найдена») убрана:
        // состояние видно по счётчику игроков в пилюле.
        RenderMenu();
        // The close button is handled inside RenderMenu. Do not submit one
        // final frame after it: eglSwapBuffers can block while Android is
        // tearing the Surface down, leaving the last overlay image frozen.
        if (!main_thread_flag.load(std::memory_order_acquire)) {
            g_frame_done.store(true, std::memory_order_release);
            break;
        }
        slowMark();   // 4: после меню
        UpdateFreecam(ImGui::GetIO().DeltaTime);   // фрикам: полёт камеры
        AimEndFrame(); // закрыть lease, если меню/фрикам выключили сайлент позднее
        slowMark();   // 5: после фрикама
        if (!main_thread_flag.load(std::memory_order_acquire)) {
            g_frame_done.store(true, std::memory_order_release);
            break;
        }
        drawEnd();
        slowMark();   // 6: после свопа
        if (slowT[6] - slowT[0] > 100.0) {
            struct timespec slowCpu1{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &slowCpu1);
            const double slowCpu = (slowCpu1.tv_sec - slowCpu0.tv_sec) * 1000.0 +
                                   (slowCpu1.tv_nsec - slowCpu0.tv_nsec) * 1e-6;
        }
        g_frame_done.store(true);
    }
    // Start the deadline before EGL teardown as well: the compositor itself
    // may be the blocked component holding the last overlay frame on screen.
    ArmShutdownDeadline();
    g_fakeLag.destroy();
    Blur::Free();
    shutdown();
    Touch_Close();
    while (!g_frame_done.load()) {}
    // Сначала останавливаем фонового writer: во время восстановления камеры и
    // object-data он уже не должен менять ось выстрела.
    silent_aim::shutdown();
    // Камеру возвращаем на место ДО отвязки: во фрикаме она улетела от тела,
    // а после выхода чита возвращать её уже некому.
    esp_freecam_set(false);
    stop_attach_thread();
    esp_viewmodel_reset(true);
    process_detach();
    CfgWatchFree();
    AudioFree();
    _exit(0);
}
