#include "view_guard.h"
#include <cassert>
#include <cstdio>
using namespace viewguard;
int main() {
    assert(max_step_m(0.016) < 6.0F && max_step_m(0.016) > 4.5F);
    assert(max_step_m(10.0) == kPosCapMeters);            // длинная пауза — старый потолок
    const float d16 = min_forward_dot(0.016);
    assert(d16 > 0.80F && d16 < 0.86F);                   // ~34° на кадре 60 Гц
    assert(min_forward_dot(0.25) <= 0.5001F);             // потолок 60°
    assert(min_forward_dot(0.0) > min_forward_dot(0.016)); // кадр короче — допуск уже
    puts("ok"); return 0;
}
