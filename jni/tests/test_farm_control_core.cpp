#include "farm_control_core.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using namespace farm_control;
    // Even tiny positive HP is alive. Fraction and ray loss alone cannot
    // declare a resource harvested; unknown HP is never a death signal.
    assert(!health_depleted(true, 0.00001f));
    assert(!health_depleted(false, 0.f));
    assert(health_depleted(true, 0.f));
    assert(health_depleted(true, -0.1f));

    assert(safe_to_swing(true, true, true, false, false, false));
    assert(!safe_to_swing(false, true, true, false, false, false));
    assert(!safe_to_swing(true, false, true, false, false, false));
    assert(!safe_to_swing(true, true, false, false, false, false));
    assert(!safe_to_swing(true, true, true, true, false, false));
    assert(!safe_to_swing(true, true, true, false, true, false));
    assert(!safe_to_swing(true, true, true, false, false, true));

    // A partially harvested target wins over a newly closer sibling; ordinary
    // selection still picks the best score if neither candidate is pinned.
    assert(better_target(true, 12.f, false, 0.5f));
    assert(!better_target(false, 0.5f, true, 12.f));
    assert(better_target(false, 1.f, false, 2.f));
    assert(!better_target(false, 2.f, false, 1.f));

    // One identity can contain multiple components. Their cooldowns are
    // independent and expire by monotonic time, not the number of frames.
    Blacklist list;
    blacklist(list, 101, 10.0, 20.0);
    assert(list.count(101) && !list.count(102));
    for (int i = 0; i < 1000; ++i) expire(list, 29.999);
    assert(list.count(101));
    expire(list, 30.0);
    assert(!list.count(101));
    blacklist(list, 102, 40.0, 0.1);
    expire(list, 40.9);
    assert(list.count(102));
    expire(list, 41.0);
    assert(!list.count(102));

    // Normal-tool press is maintained on every safe frame, including if a
    // movement release has cleared the actual byte. A missing/foreign ray
    // immediately ends the hold, without waiting for the pulse to expire.
    Swing swing;
    std::vector<bool> writes;
    bool attack_byte = false;
    bool fail_press = false, fail_release = false;
    auto write = [&](bool value) {
        writes.push_back(value);
        if (value && fail_press) return false;
        if (!value && fail_release) return false;
        attack_byte = value;
        return true;
    };
    swing.tick(16, true, false, 100, 500, write);
    assert(swing.down() && attack_byte && writes.back());
    attack_byte = false; // farmjoy::release() also releases attack
    swing.tick(16, true, false, 100, 500, write);
    assert(swing.down() && attack_byte && writes.back());
    swing.tick(16, false, false, 100, 500, write);
    assert(!swing.down() && !attack_byte && !writes.back());

    // A rejected press is retried, and a rejected release is retried until
    // it succeeds instead of forgetting a potentially held attack.
    fail_press = true;
    swing.tick(16, true, false, 100, 500, write);
    assert(!swing.down());
    fail_press = false;
    swing.tick(16, true, false, 100, 500, write);
    assert(swing.down() && attack_byte);
    fail_release = true;
    swing.tick(16, false, false, 100, 500, write);
    assert(swing.down() && attack_byte);
    fail_release = false;
    swing.tick(16, false, false, 100, 500, write);
    assert(!swing.down() && !attack_byte);

    // The pulse cools down normally and a continuous tool is also stopped
    // as soon as ownership disappears.
    swing.tick(16, true, false, 40, 200, write);
    swing.tick(40, true, false, 40, 200, write);
    assert(!swing.down() && !attack_byte);
    const auto count = writes.size();
    swing.tick(100, true, false, 40, 200, write);
    assert(writes.size() == count);
    swing.tick(100, true, false, 40, 200, write);
    assert(swing.down() && attack_byte);
    swing.stop(write);
    swing.tick(16, true, true, 40, 200, write);
    assert(swing.down());
    swing.tick(16, false, true, 40, 200, write);
    assert(!swing.down() && !attack_byte);
}
