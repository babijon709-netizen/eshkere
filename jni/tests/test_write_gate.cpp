// Host tests for the write gate: when may the cheat write into game memory?
//
// Правило (runtime_guard::gate_allows_writes): подтверждённый снимок
// /proc/<pid>/maps ИЛИ транспорт, который сам проверяет отображение адреса в
// таблицах страниц цели. Второе важно не «на всякий случай», а потому что на
// клиентах с закрытым procfs (RASP/Appdome-прошивки, hidepid, часть политик
// SELinux) карта не читается никогда, привязка идёт через драйвер, чтения
// работают — а fail-closed по Unknown убивал каждую запись: ESP рисуется,
// автофарм «не добывает». Тест фиксирует обе половины правила и то, что
// способ доступа объявляет свою проверку явно.
#include "kernel_io.h"
#include "mem_io.h"
#include "runtime_guard.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Транспорт без собственной проверки отображения (по умолчанию) и с ней.
class BlindTransport : public memio::Transport {
public:
    const char* name() const override { return "fake/blind"; }
    bool open(pid_t) override { return true; }
    void close() override {}
    // Проба привязки читает ELF-заголовок по адресу probe: отдаём его по
    // любому адресу, дальше Reader проверит только магию.
    ssize_t read_once(uint64_t, void* out, size_t len) override {
        if (len > sizeof(kBytes)) len = sizeof(kBytes);
        std::memcpy(out, kBytes, len);
        return (ssize_t)len;
    }
    ssize_t write_once(uint64_t, const void*, size_t len) override { writes += len; return (ssize_t)len; }
    unsigned long long writes = 0;
    static constexpr unsigned char kBytes[64] = {0x7F, 'E', 'L', 'F'};
};

class ValidatingTransport : public BlindTransport {
public:
    const char* name() const override { return "fake/validating"; }
    bool validates_writes() const override { return true; }
};

} // namespace

int main() {
    // ---- правило ворот ---------------------------------------------------
    // Нет снимка карты и транспорт ничего не проверяет — писать нельзя.
    assert(!runtime_guard::gate_allows_writes(runtime_guard::Policy::Unknown, false));
    // Снимок подтверждён — можно (clear и protected: libloader.so сам по себе
    // запись не запрещает).
    assert(runtime_guard::gate_allows_writes(runtime_guard::Policy::Clear, false));
    assert(runtime_guard::gate_allows_writes(runtime_guard::Policy::Protected, false));
    // Ядерный транспорт проверяет адрес сам — можно и без снимка карты.
    assert(runtime_guard::gate_allows_writes(runtime_guard::Policy::Unknown, true));
    assert(runtime_guard::gate_allows_writes(runtime_guard::Policy::Clear, true));

    // ---- кто именно «проверяет сам» --------------------------------------
    // Экстернал (process_vm_writev) отображение не проверяет: на нём
    // Unknown-политика обязана останавливать запись.
    assert(!memio::procfs_transport().validates_writes());
    // Ядерные транспорты копируют через таблицы страниц цели (-EFAULT на
    // неотображённый адрес) — им снимок карты для записи не нужен.
    assert(memio::kernel_transport().validates_writes());
    assert(memio::rt_dev_transport().validates_writes());
    assert(memio::rt_hook_transport().validates_writes());
    // Новый транспорт по умолчанию — «не проверяет», то есть fail-closed.
    BlindTransport blind;
    ValidatingTransport validating;
    assert(!blind.validates_writes());
    assert(validating.validates_writes());

    // ---- флаг транспорта доходит до Reader --------------------------------
    // Снимок карты подтверждён: запись проходит по обычному правилу.
    runtime_guard::reset();
    runtime_guard::observe_maps(100,
        "7000000000-7000001000 r-xp 00000000 fd:00 11 /data/app/lib/arm64/libil2cpp.so\n");
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Clear);

    memio::Reader reader;
    reader.set_transport(&validating);
    assert(reader.transport() == &validating);
    assert(reader.transport()->validates_writes());
    assert(reader.bind(100, 0x10000));            // проба: ELF-заголовок из kBytes
    assert(reader.bound());
    assert(reader.writes_permitted());
    char value[2] = {0, 0};
    assert(reader.write(0x10000, value, sizeof(value)));
    assert(validating.writes == sizeof(value));      // запись дошла до транспорта
    assert(reader.guard_bypassed_total() == 0);      // снимок карты был
    assert(reader.read(0x10000, value, sizeof(value)));
    assert(value[0] == 0x7F && value[1] == 'E');
    reader.unbind();

    // Экстернал с тем же pid: правило то же (на хосте ворота всегда открыты —
    // procfs цели нет), поэтому проверяем именно выбор транспорта и счётчики.
    reader.set_transport(&blind);
    assert(!reader.transport()->validates_writes());
    assert(reader.bind(100, 0x10000));
    assert(reader.writes_permitted());
    reader.unbind();
    assert(!reader.writes_permitted());     // без привязки писать некуда

    // Тот же кадр, но политики нет (procfs закрыт): запись всё равно проходит,
    // потому что отображение подтвердил транспорт, — и это видно в счётчике.
    runtime_guard::reset();
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Unknown);
    reader.set_transport(&validating);
    assert(reader.bind(100, 0x10000));
    assert(reader.writes_permitted());
    assert(reader.write(0x10000, value, sizeof(value)));
    assert(reader.guard_bypassed_total() == 1);
    assert(reader.guard_blocks_total() == 0);
    reader.unbind();
    std::puts("write gate (maps snapshot vs transport validation): PASS");
    return 0;
}
