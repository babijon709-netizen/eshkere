#include "silent_aim_core.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {
struct Memory {
    std::unordered_map<uint64_t, uint64_t> pointers;
    uint64_t last_write = 0;
    uint64_t written_address = 0;   // куда легла последняя запись (ось выстрела)
    float value[3] = {};
    int writes = 0;

    bool read(uint64_t address, void* out, size_t size) {
        // Ось выстрела читается так же, как в игре: те же 12 байт, что и пишем.
        if (size == sizeof(value) && address == written_address) {
            std::memcpy(out, value, size);
            return true;
        }
        const auto it = pointers.find(address);
        if (size != sizeof(uint64_t) || it == pointers.end()) return false;
        std::memcpy(out, &it->second, size);
        return true;
    }
    bool write(uint64_t address, const void* in, size_t size) {
        if (size != sizeof(value)) return false;
        last_write = address;
        written_address = address;
        std::memcpy(value, in, size);
        ++writes;
        return true;
    }
};

silent_aim::Command command() {
    silent_aim::Command c;
    c.pid = 123;
    c.target = 0x10000;
    c.player = 0x11000;
    c.handler = 0x12000;
    c.look = 0x13000;
    c.player_class = 0x21000;
    c.handler_class = 0x22000;
    c.look_class = 0x23000;
    c.player_handler_offset = 0x78;
    c.handler_player_offset = 0x58;
    c.look_offset = 0x140;
    c.value_offset = 0x20;
    c.eye[0] = 1.f; c.eye[1] = 2.f; c.eye[2] = 3.f;
    c.aim[0] = 1.f; c.aim[1] = 2.f; c.aim[2] = 7.f;
    return c;
}

Memory guarded_memory(const silent_aim::Command& c) {
    Memory memory;
    memory.pointers[c.player] = c.player_class;
    memory.pointers[c.player + c.player_handler_offset] = c.handler;
    memory.pointers[c.handler] = c.handler_class;
    memory.pointers[c.handler + c.handler_player_offset] = c.player;
    memory.pointers[c.handler + c.look_offset] = c.look;
    memory.pointers[c.look] = c.look_class;
    return memory;
}
}

int main() {
    const auto c = command();

    // No FPObject/FPHitscan fields are needed: the shared player look chain is
    // sufficient for projectile weapons and remains guarded on every write.
    assert(silent_aim::valid_command(c));
    auto memory = guarded_memory(c);
    assert(silent_aim::tick(memory, c) == silent_aim::Result::written);
    assert(memory.last_write == c.look + c.value_offset);
    assert(std::fabs(memory.value[0]) < 0.00001f);
    assert(std::fabs(memory.value[1]) < 0.00001f);
    assert(std::fabs(memory.value[2] - 1.f) < 0.00001f);

    memory = guarded_memory(c);
    memory.pointers[c.handler + c.look_offset] = 0x14000;
    assert(silent_aim::tick(memory, c) == silent_aim::Result::guard_failed);

    // Пишем по расхождению. Тик сайлента идёт каждый кадр, пока ведётся цель,
    // поэтому второй раз тот же вектор класть в чужую память незачем — это и
    // есть единственный лишний след функции. Игра повернула камеру (значение
    // сдвинулось) — запись возвращается.
    memory = guarded_memory(c);
    assert(silent_aim::tick(memory, c) == silent_aim::Result::written);
    assert(memory.writes == 1);
    assert(silent_aim::tick(memory, c) == silent_aim::Result::written);
    assert(silent_aim::tick(memory, c) == silent_aim::Result::written);
    assert(memory.writes == 1);
    memory.value[2] = -1.0F;
    assert(silent_aim::tick(memory, c) == silent_aim::Result::written);
    assert(memory.writes == 2);
    assert(std::fabs(memory.value[2] - 1.0F) < 0.00001f);

    assert(!silent_aim::wants_no_spread(false, true, false));
    assert(!silent_aim::wants_no_spread(true, false, false));
    assert(!silent_aim::wants_no_spread(true, true, true));
    assert(silent_aim::wants_no_spread(true, true, false));
    std::puts("ALL SILENT-AIM TESTS PASS");
    return 0;
}
