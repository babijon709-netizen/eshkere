#pragma once

#include <atomic>
#include <thread>
#include <chrono>

class FakeLag {
public:
    FakeLag();
    ~FakeLag();

    // процент intensity, 1–100 (default 45)
    // задать до или во время работы — читается атомарно в loop
    void setPercent(int percent);

    // enable=true  → запускает поток с toggle-циклом
    // enable=false → останавливает поток, убирает iptables-правило
    void apply(bool enable);

    // то же что apply(false), вызывается из деструктора
    void destroy();

    // Идемпотентно и безопасно из обработчика сигнала: одно fork+exec,
    // который снимет DROP-правило даже если наш процесс уже умирает.
    static void unblockRaw();

    // Лаг «ведётся» — по этому флагу выход решают убрать правило жёстко.
    bool active() const { return m_enabled.load(); }

private:
    void loop();
    void sleepMs(int ms);

    std::atomic<bool> m_enabled;
    std::atomic<bool> m_running;
    std::atomic<int>  m_percent;
    bool              m_blocked;   // только внутри потока loop, без гонок
    std::thread       m_thread;
};
