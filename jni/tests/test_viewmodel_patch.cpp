// Патч полей ViewModel (FOV и восемь слотов рук) обязан писать по расхождению,
// а не каждый кадр: пока в памяти лежит наше значение, тик стоит одно чтение.
// Тест проверяет именно это — плюс то, что «уже наше» не путается с «игра
// вернула своё» и что fresh-снимок расходуется один раз.
#include "viewmodel_memory.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>

namespace {

struct Mem {
    std::map<uint64_t, float> f;
    std::map<uint64_t, viewmodel::Position> p;
    int writes = 0, reads = 0;

    bool read(uint64_t addr, float& out) {
        ++reads;
        auto it = f.find(addr);
        if (it == f.end()) return false;
        out = it->second;
        return true;
    }
    bool read(uint64_t addr, viewmodel::Position& out) {
        ++reads;
        auto it = p.find(addr);
        if (it == p.end()) return false;
        out = it->second;
        return true;
    }
    bool write(uint64_t addr, float value) { ++writes; f[addr] = value; return true; }
    bool write(uint64_t addr, const viewmodel::Position& value) { ++writes; p[addr] = value; return true; }
    uint64_t ptr(uint64_t) { return 0; }
    bool is(uint64_t, const char*, const char*) { return false; }
};

void test_float_writes_only_on_divergence() {
    constexpr uint64_t addr = 0x1000;
    Mem m;
    m.f[addr] = 74.0F;                       // игровой FOV
    viewmodel::Patch<float> fov;

    assert(fov.sample(m, addr));
    assert(fov.put(m, 90.0F));
    assert(m.writes == 1);
    assert(m.f[addr] == 90.0F);
    assert(fov.original == 74.0F);           // возвращать игру — на её значение

    for (int frame = 0; frame < 60; ++frame) {
        assert(fov.sample(m, addr));
        assert(fov.put(m, 90.0F));
    }
    assert(m.writes == 1);                   // 60 кадров — ни одной записи
    assert(m.reads == 61);                   // но чтение каждого кадра живое

    m.f[addr] = 62.0F;                       // игра переставила FOV
    assert(fov.sample(m, addr));
    assert(fov.put(m, 90.0F));
    assert(m.writes == 2);
    assert(fov.original == 62.0F);           // новое значение и есть «что вернуть»
    m.f[addr] = 90.0F;                       // наша запись на месте
    assert(fov.sample(m, addr));
    assert(fov.put(m, 90.0F));
    assert(m.writes == 2);

    // Один снимок расходуется одной проверкой: второй put без sample обязан
    // посмотреть память ещё раз, а не молча «повторить» пропуск.
    assert(fov.put(m, 90.0F));
    assert(m.writes == 3);
}

void test_restore_never_clobbers_the_game() {
    constexpr uint64_t addr = 0x2000;
    Mem m;
    m.f[addr] = 70.0F;
    viewmodel::Patch<float> fov;
    assert(fov.sample(m, addr));
    assert(fov.put(m, 100.0F));
    assert(m.writes == 1);

    m.f[addr] = 55.0F;                       // игра перехватила поле
    assert(fov.restore(m));
    assert(m.f[addr] == 55.0F);              // не затираем её своим old
    assert(m.writes == 1);
    assert(!fov.written);                    // патч снят, владения больше нет

    // Игра сама выставила ровно желаемое: писать нечего, владения нет —
    // значит и «возвращать» на выключении нечего, значение остаётся игровым.
    m.f[addr] = 100.0F;
    assert(fov.sample(m, addr));
    assert(fov.put(m, 100.0F));
    assert(m.writes == 1);
    assert(fov.restore(m));
    assert(m.writes == 1);
    assert(m.f[addr] == 100.0F);
}

void test_position_patch_same_rule() {
    constexpr uint64_t addr = 0x3000;
    Mem m;
    m.p[addr] = viewmodel::Position{0.0F, 0.0F, 0.0F};
    viewmodel::Patch<viewmodel::Position> hand;
    const viewmodel::Position want{0.12F, -0.05F, 0.03F};

    assert(hand.sample(m, addr));
    assert(hand.put(m, want));
    assert(m.writes == 1);
    for (int frame = 0; frame < 30; ++frame) {
        assert(hand.sample(m, addr));
        assert(hand.put(m, want));
    }
    assert(m.writes == 1);                   // смещение уже лежит — тишина

    m.p[addr] = viewmodel::Position{1.0F, 1.0F, 1.0F};   // игра вернула своё
    assert(hand.sample(m, addr));
    assert(hand.put(m, want));
    assert(m.writes == 2);
}

// Поле, которое игра пересчитывает каждый кадр (lerp позы, нормализованный
// вектор), в биты с нашим не совпадёт никогда: без допуска «писать по
// расхождению» на таких полях и есть «писать каждый кадр».
void test_tolerance_absorbs_game_recompute_noise() {
    constexpr uint64_t addr = 0x5000;
    Mem m;
    m.p[addr] = viewmodel::Position{0.1199998F, -0.0499999F, 0.0300001F};
    viewmodel::Patch<viewmodel::Position> hand;
    const viewmodel::Position want{0.12F, -0.05F, 0.03F};

    assert(hand.sample(m, addr));
    assert(hand.put(m, want, 0.0002F));       // шум 1e-7 — не расхождение
    assert(m.writes == 0);
    assert(hand.written == false);            // владения нет: возвращать нечего

    m.p[addr] = viewmodel::Position{0.12F, 0.5F, 0.03F};   // рука реально уехала
    assert(hand.sample(m, addr));
    assert(hand.put(m, want, 0.0002F));
    assert(m.writes == 1);
    assert(hand.written);

    // Точное сравнение (eps = 0) допуска не видит — для позы-цели это и нужно.
    Mem solo;
    solo.f[addr] = 89.9999F;
    viewmodel::Patch<float> f;
    assert(f.sample(solo, addr));
    assert(f.put(solo, 90.0F));
    assert(solo.writes == 1);
}

void test_invalid_values_are_never_written() {
    constexpr uint64_t addr = 0x4000;
    Mem m;
    m.f[addr] = 80.0F;
    viewmodel::Patch<float> fov;
    assert(fov.sample(m, addr));
    assert(!fov.put(m, 0.0F));               // FOV 0 — не валидное, пишем нельзя
    assert(!fov.put(m, 500.0F));
    assert(m.writes == 0);
    assert(m.f[addr] == 80.0F);
}

} // namespace

int main() {
    test_float_writes_only_on_divergence();
    test_restore_never_clobbers_the_game();
    test_position_patch_same_rule();
    test_tolerance_absorbs_game_recompute_noise();
    test_invalid_values_are_never_written();
    std::printf("ALL VIEWMODEL PATCH TESTS PASS\n");
    return 0;
}
