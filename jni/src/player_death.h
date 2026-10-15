#pragma once
// Ранний вердикт «игрок мёртв»: единый источник для счётчика игроков, стрелок
// и радара. Чистая логика (без чтения памяти) — покрыта хост-тестом
// jni/tests/test_player_death.cpp.
//
// Почему отдельно от box.dead. Прежнее решение жило внутри блока бокса, после
// отсева по экрану: игрок за спиной или за кромкой отсеивался раньше, чем
// читался флаг смерти, — и счётчик/стрелка считали труп живым. Теперь вердикт
// берётся до счётчика и пеленга, по тем же трём признакам игры.
#include <cstdint>

namespace playerdeath {

constexpr double kReadIntervalSeconds = 0.04;  // ~25 Гц: смерть не требует 60 Гц
constexpr int    kConfirmReads        = 2;     // подряд одинаковых чтений для смены вердикта

enum class Signal : uint8_t { Unknown, Alive, Dead };

struct Verdict {
    double  read_at = 0.0;
    uint8_t dead_votes = 0;
    uint8_t alive_votes = 0;
    bool    dead = false;
};

// Пора ли перечитывать признаки (иначе действует прошлый вердикт).
inline bool due(const Verdict& v, double now) {
    return v.read_at == 0.0 || now - v.read_at >= kReadIntervalSeconds;
}

// Сводит три признака в один: хоть один «мёртв» — мёртв; «жив» только если
// хоть один читается и ни один не сказал «мёртв»; ничего не прочитано — Unknown.
inline Signal combine(Signal a, Signal b, Signal c) {
    if (a == Signal::Dead || b == Signal::Dead || c == Signal::Dead) return Signal::Dead;
    if (a == Signal::Alive || b == Signal::Alive || c == Signal::Alive) return Signal::Alive;
    return Signal::Unknown;
}

// Один отсчёт. Сорванное чтение вердикт не меняет — это не свидетельство ни
// смерти, ни респауна. Смена вердикта — только после kConfirmReads подряд:
// рваное чтение указателя цепочки не должно гасить живого игрока на кадр.
inline bool update(Verdict& v, Signal s, double now) {
    v.read_at = now;
    if (s == Signal::Dead) {
        v.alive_votes = 0;
        if (v.dead_votes < 255) ++v.dead_votes;
        if (v.dead_votes >= kConfirmReads) v.dead = true;
    } else if (s == Signal::Alive) {
        v.dead_votes = 0;
        if (v.alive_votes < 255) ++v.alive_votes;
        if (v.alive_votes >= kConfirmReads) v.dead = false;
    }
    return v.dead;
}

} // namespace playerdeath
