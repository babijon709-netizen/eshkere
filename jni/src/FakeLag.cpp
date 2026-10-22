#include "FakeLag.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* BLOCK   = "iptables -I INPUT ! -i lo -p udp -m length --length 80:65535 -j DROP";
static const char* UNBLOCK = "iptables -D INPUT ! -i lo -p udp -m length --length 80:65535 -j DROP 2>/dev/null";
// То же снятие, но одной командой и без popen: для аварийного порядка, где
// malloc/stdio уже недопустимы.
static const char* UNBLOCK_RAW =
    "su -c 'iptables -D INPUT ! -i lo -p udp -m length --length 80:65535 -j DROP' 2>/dev/null;"
    " su -c 'iptables -D INPUT ! -i lo -p udp -m length --length 80:65535 -j DROP' 2>/dev/null";

// Выполняет команду без popen/malloc в многопоточном процессе: если уже root
// (euid == 0), вызываем sh напрямую без лишнего su-процесса.
static void executeRoot(const char* cmd) {
    const pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (geteuid() == 0) {
            execl("/system/bin/sh", "sh", "-c", cmd, (char*)nullptr);
        } else {
            char buf[512];
            snprintf(buf, sizeof(buf), "su -c '%s'", cmd);
            execl("/system/bin/sh", "sh", "-c", buf, (char*)nullptr);
        }
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
}

namespace {

// iptables-правило переживает процесс: ни SIGKILL, ни наш дедлайн-выход, ни
// падение по SIGSEGV не проходят через FakeLag::apply(false). Пока это было
// единственной дорогой, «забыл выключить лаг» означало глобальный DROP на
// входящий UDP до перезагрузки устройства — то есть функция уровня системы,
// а не чита. Поэтому: (1) перед установкой снимем висящее с прошлого раза
// правило, (2) на всё ловуемое (TERM/QUIT/ABRT/SEGV/BUS/ILL; SIGHUP и SIGINT
// игнорируются, чтобы закрытие терминала MT Manager не убивало процесс)
// снимаем сами, (3) дедлайн-выход в main.cpp зовёт unblockRaw().
// Ловить нельзя ровно два — SIGKILL и SIGSTOP.
std::atomic<bool> g_guards_on{false};
struct sigaction g_old_actions[5];
const int kGuarded[5] = { SIGQUIT, SIGABRT, SIGSEGV, SIGBUS, SIGILL };

void guard_handler(int sig) {
    // Повторный сигнал внутри обработчика (например, второй SIGSEGV на
    // испорченном стеке) обязан вести себя как раньше, а не зациклиться.
    if (g_guards_on.exchange(false)) FakeLag::unblockRaw();
    for (size_t i = 0; i < sizeof(kGuarded) / sizeof(kGuarded[0]); ++i)
        sigaction(kGuarded[i], &g_old_actions[i], nullptr);
    raise(sig);
}

void guards_on() {
    if (g_guards_on.exchange(true)) return;
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = guard_handler;
    sa.sa_flags = SA_RESTART;   // чужие блокирующие вызовы (ввод, транспорт) трогать незачем
    for (size_t i = 0; i < sizeof(kGuarded) / sizeof(kGuarded[0]); ++i)
        sigaction(kGuarded[i], &sa, &g_old_actions[i]);
}

void guards_off() {
    if (!g_guards_on.exchange(false)) return;
    for (size_t i = 0; i < sizeof(kGuarded) / sizeof(kGuarded[0]); ++i)
        sigaction(kGuarded[i], &g_old_actions[i], nullptr);
}

} // namespace

// Снять правило, не полагаясь на то, что процесс ещё жив: ни malloc (его зовёт
// popen), ни join потока, ни даже наш собственный стек здесь не гарантированы.
// Один fork+exec, без ожидания: ребёнок переживает _exit'нувшего родителя, а
// SIGCHLD игнорируется маскировкой (stealth.h), так что зомби не остаётся.
void FakeLag::unblockRaw() {
    const pid_t pid = fork();
    if (pid > 0) return;
    if (pid < 0) {
        // Вилки нет (исчерпан лимит) — пробуем хотя бы из этого же образа:
        // хуже уже было, правило всё равно осталось бы висеть.
        executeRoot(UNBLOCK);
        return;
    }
    execl("/system/bin/sh", "sh", "-c", UNBLOCK_RAW, (char*)nullptr);
    _exit(127);
}

FakeLag::FakeLag()
    : m_enabled(false)
    , m_percent(45)
    , m_blocked(false)
    , m_running(false)
{}

FakeLag::~FakeLag() {
    destroy();
}

void FakeLag::setPercent(int percent) {
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    m_percent.store(percent);
}

void FakeLag::apply(bool enable) {
    if (enable) {
        if (m_running.load()) return;   // уже запущен
        // Чистим след прошлого запуска ДО установки своего: иначе два
        // одинаковых правила в цепочке (убитый прошлый процесс) снимаются
        // «выключением» только наполовину, и UDP-DROP остаётся живым.
        executeRoot(UNBLOCK);
        m_enabled.store(true);
        m_running.store(true);
        m_blocked = false;
        guards_on();
        m_thread = std::thread(&FakeLag::loop, this);
    } else {
        guards_off();
        m_enabled.store(false);
        m_running.store(false);
        if (m_thread.joinable()) m_thread.join();
        // DROP отбрасывает пакет сразу — он не копится, поэтому после
        // выключения пинг не улетает пачкой. Делаем два -D подряд чтобы
        // гарантированно снять правило даже если в цепочке оказалось два
        // одинаковых (креш/дубль), лишние падают в 2>/dev/null.
        executeRoot(UNBLOCK);
        executeRoot(UNBLOCK);
        m_blocked = false;
    }
}

void FakeLag::destroy() {
    apply(false);
}

void FakeLag::loop() {
    while (m_running.load()) {
        if (!m_enabled.load()) break;

        int percent = m_percent.load();

        if (percent >= 90) {
            // постоянный блок, проверяем раз в секунду
            if (!m_blocked) {
                executeRoot(BLOCK);
                m_blocked = true;
            }
            sleepMs(1000);
            continue;
        }

        int blockMs   = (1000 * percent) / 100;
        int unblockMs = 1000 - blockMs;

        if (m_blocked) {
            executeRoot(UNBLOCK);
            m_blocked = false;
            sleepMs(unblockMs);
        } else {
            executeRoot(BLOCK);
            m_blocked = true;
            sleepMs(blockMs);
        }
    }
}

void FakeLag::sleepMs(int ms) {
    // спим по 50мс кускам чтобы быстро реагировать на apply(false)
    while (ms > 0 && m_running.load()) {
        int step = ms > 50 ? 50 : ms;
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        ms -= step;
    }
}
