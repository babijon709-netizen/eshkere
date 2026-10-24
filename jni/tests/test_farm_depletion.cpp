#include "farm_depletion.h"

#include <cassert>
#include <limits>

int main() {
    using farm_depletion::terminal_health;

    assert(terminal_health(0.0f, 100.0f));
    assert(terminal_health(2.0f, 100.0f));
    assert(!terminal_health(2.01f, 100.0f));
    assert(!terminal_health(1.0f, 0.0f));
    assert(!terminal_health(-1.0f, 100.0f));
    assert(!terminal_health(std::numeric_limits<float>::quiet_NaN(), 100.0f));
    assert(!terminal_health(0.0f, std::numeric_limits<float>::infinity()));
}
