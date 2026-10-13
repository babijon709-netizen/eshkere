#pragma once
// Допуски кадрового барьера матрицы вида (game.cpp: frame_view_accepted).
// Зависят от времени между ПРИНЯТЫМИ кадрами: после отброшенного кадра dt
// растёт, и настоящий быстрый доворот проходит со следующего кадра.
// Хост-тест: jni/tests/test_view_guard.cpp.
#include <cmath>

namespace viewguard {

constexpr float  kBaseStepMeters = 3.0F;     // камера не сдвигается дальше за кадр
constexpr float  kMaxSpeed       = 130.0F;   // м/с: коптер — верхняя граница
constexpr float  kPosCapMeters   = 30.0F;    // прежний потолок
constexpr float  kBaseTurnDeg    = 10.0F;
constexpr float  kMaxTurnRate    = 1500.0F;  // °/с: быстрый свайп прицела
constexpr float  kTurnCapDeg     = 60.0F;    // прежний потолок

inline float clamp_dt(double dt) {
    if (!(dt > 0.004)) return 0.004F;
    if (dt > 0.25) return 0.25F;
    return (float)dt;
}
inline float max_step_m(double dt) {
    const float v = kBaseStepMeters + kMaxSpeed * clamp_dt(dt);
    return v > kPosCapMeters ? kPosCapMeters : v;
}
// Минимальное скалярное произведение осей forward между кадрами.
inline float min_forward_dot(double dt) {
    float deg = kBaseTurnDeg + kMaxTurnRate * clamp_dt(dt);
    if (deg > kTurnCapDeg) deg = kTurnCapDeg;
    return cosf(deg * 3.14159265358979F / 180.0F);
}

} // namespace viewguard
