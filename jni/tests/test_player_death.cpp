#include "player_death.h"
#include <cassert>
#include <cstdio>
using namespace playerdeath;
int main() {
    Verdict v;
    assert(due(v, 1.0));
    assert(!update(v, Signal::Unknown, 1.0));            // нечитаемое — не смерть
    assert(!due(v, 1.01)); assert(due(v, 1.05));
    assert(!update(v, Signal::Dead, 1.05));               // один «мёртв» — ещё жив
    assert(!update(v, Signal::Unknown, 1.10));            // сбой не сбрасывает голос
    assert(update(v, Signal::Dead, 1.15));                // второй подряд — мёртв
    assert(update(v, Signal::Unknown, 1.20));             // сбой труп не воскрешает
    assert(update(v, Signal::Alive, 1.25));               // один «жив» — пока мёртв
    assert(!update(v, Signal::Alive, 1.30));              // второй — респаун
    assert(!update(v, Signal::Dead, 1.35));               // одиночный глюк не гасит живого
    assert(!update(v, Signal::Alive, 1.40));
    assert(combine(Signal::Alive, Signal::Dead, Signal::Unknown) == Signal::Dead);
    assert(combine(Signal::Unknown, Signal::Alive, Signal::Unknown) == Signal::Alive);
    assert(combine(Signal::Unknown, Signal::Unknown, Signal::Unknown) == Signal::Unknown);
    puts("ok"); return 0;
}
