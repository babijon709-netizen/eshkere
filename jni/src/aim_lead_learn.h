#pragma once
// Автокалибровка упреждения по попаданиям.
//
// Сигнал у нас бинарный: игра подтверждает попадание отметкой
// (PlayerEventHandler.LastHitTime, esp_mem_aim_last_shot), а промах виден как
// «окно после выстрела прошло, отметки нет». Направление промаха из этого не
// достать, поэтому масштаб упреждения ищется поиском по одному параметру
// (1-D pattern search): пробуем масштаб, копим долю попаданий на серии
// выстрелов, удачная проба продолжается с более крупным шагом, неудачная —
// разворачивает направление с уменьшенным вдвое шагом. Сходится к лучшему
// масштабу за десятки выстрелов и не накапливает смещение: параметр один,
// диапазон ограничен.
//
// Что учится: масштаб ОКНА упреждения (возраст снапшота + кадр управления).
// Полёт пули не масштабируется — это физика, её считает solver.
//
// Считать выстрелы имеет смысл только там, где прицел вёл чит: entry points
// (aim.cpp) кормят обучение лишь когда цель выбрана и огонь нажат после этого.
// Стоящая цель не даёт информации — такие выстрелы не считаются.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aimlead {

constexpr float kScaleMin = 0.35F;
constexpr float kScaleMax = 2.00F;
constexpr float kStepStart = 0.25F;
// Шаг не опускается в ноль: цель у нас — не «зафиксировать число», а держать
// долю попаданий. Пинг и тик сервера дрейфуют, поэтому поиск продолжает
// пробовать соседние масштабы вокруг лучшего, а не защёлкивается на нём.
constexpr float kStepMin = 0.08F;
constexpr float kStepMax = 0.30F;
constexpr int   kShotsPerTrial = 7;
// Окно подтверждения попадания после выстрела. Больше — начнём приписывать
// попадание следующему выстрелу; меньше — не доживём до отметки на 150 м с
// винтовкой (полёт 0.27 с плюс путь до сервера).
constexpr double kHitWindow = 0.60;
constexpr float  kMinTargetSpeed = 0.8F;   // м/с: ниже упреждения нет вовсе
constexpr float  kImprove = 0.10F;         // прирост доли попаданий, который считаем удачей

struct State {
    bool  enabled = true;
    float scale = 1.0F;      // масштаб, который применяется к лиду сейчас
    float best = 1.0F;       // лучший найденный
    float best_rate = -1.0F; // доля попаданий на нём (затухает, чтобы ехать за пингом)
    float step = kStepStart;
    int   direction = 1;
    int   trial_shots = 0, trial_hits = 0;
    bool  trial_open = false;   // хоть один информативный выстрел в серии
    bool  pending = false;      // выстрел ждёт подтверждения попадания
    double pending_at = 0.0;
    int   probe = 0;            // 0 — проверяем сам лучший, иначе соседа
    int   total_shots = 0, total_hits = 0;
};

inline State& state() {
    static State s;
    return s;
}

inline float clamp_scale(float value) {
    if (!std::isfinite(value)) return 1.0F;
    return value < kScaleMin ? kScaleMin : (value > kScaleMax ? kScaleMax : value);
}

inline void set_enabled(bool on) {
    State& s = state();
    s.enabled = on;
    s.pending = false;
}

inline float scale() {
    const State& s = state();
    return s.enabled ? clamp_scale(s.scale) : 1.0F;
}

// Доля попаданий на текущем масштабе, 0..1; при пустой серии — -1.
inline float trial_rate() {
    const State& s = state();
    return s.trial_shots > 0
        ? (float)s.trial_hits / (float)s.trial_shots : -1.0F;
}

inline float overall_rate() {
    const State& s = state();
    return s.total_shots > 0
        ? (float)s.total_hits / (float)s.total_shots : -1.0F;
}

// Масштаб в «миллисекундах окна» для сохранения в конфиге (легаси-слот
// aim_lead_ms расширения прицела). 1.0 -> 50 мс.
inline float learned_ms() {
    return clamp_scale(state().best) * 50.0F;
}

inline void restore_ms(float ms) {
    // Старые конфиги держат в этом слоте 0 — это не «масштаб 0», а «не задано».
    if (!(ms > 5.0F) || ms > 150.0F) return;
    State& s = state();
    s.best = clamp_scale(ms / 50.0F);
    s.scale = s.best;
    s.step = kStepMin;
}

inline void reset() {
    State& s = state();
    s.scale = s.best = 1.0F;
    s.best_rate = -1.0F;
    s.step = kStepStart;
    s.direction = 1;
    s.trial_shots = s.trial_hits = 0;
    s.trial_open = false;
    s.pending = false;
    s.probe = 0;
    s.total_shots = s.total_hits = 0;
}

// Закрыть серию: сравнить с лучшей и выбрать следующий масштаб.
inline void close_trial() {
    State& s = state();
    if (!s.trial_open) return;
    const float rate = (float)s.trial_hits / (float)s.trial_shots;
    // Лучшая доля медленно сползает: пинг, тик сервера и тип боя меняются, и
    // «идеал» пятиминутной давности не обязан быть идеалом сейчас. Из-за
    // затухания лучший масштаб обязан снова себя доказывать.
    if (s.best_rate >= 0.0F) s.best_rate *= 0.90F;
    if (s.best_rate < 0.0F || rate > s.best_rate + kImprove) {
        s.best = s.scale;
        s.best_rate = rate;
        s.step = std::min(s.step * 1.3F, kStepMax);
    } else {
        s.direction = -s.direction;
        s.step = std::max(s.step * 0.7F, kStepMin);
    }
    s.trial_shots = s.trial_hits = 0;
    s.trial_open = false;
    // Каждая третья серия — контрольная на самом лучшем масштабе, чтобы
    // выученное число не жило вечно без подтверждения.
    s.probe = (s.probe + 1) % 3;
    s.scale = s.probe == 0
        ? clamp_scale(s.best)
        : clamp_scale(s.best + (float)s.direction * s.step);
}

// Выстрел: пора запомнить масштаб, при котором он ушёл. speed — горизонтальная
// скорость выбранной цели, м/с; по стоящей цели упреждения нет и учиться не на
// чем.
inline void shot(double now, float target_speed) {
    State& s = state();
    if (!s.enabled) return;
    if (!std::isfinite(target_speed) || target_speed < kMinTargetSpeed) return;
    if (!std::isfinite(now)) return;
    // Учёт выстрелов идёт в момент выстрела; попадание приходит отдельно.
    // Прошлый выстрел без ответа остаётся промахом в доле — здесь только
    // снимаем ожидание, чтобы отметка нового попадания не приписалась ему.
    s.pending = true;
    s.pending_at = now;
    ++s.trial_shots;
    ++s.total_shots;
    s.trial_open = true;
    if (s.trial_shots >= kShotsPerTrial) close_trial();
}

// Игра подтвердила попадание — оно принадлежит последнему выстрелу, если тот
// ещё в окне.
inline void hit(double now) {
    State& s = state();
    if (!s.enabled || !s.pending) return;
    if (!std::isfinite(now) || now - s.pending_at > kHitWindow) {
        s.pending = false;
        return;
    }
    ++s.trial_hits;
    ++s.total_hits;
    s.pending = false;
}

// Раз в кадр: окно подтверждения истекло — выстрел был промахом.
inline void tick(double now) {
    State& s = state();
    if (!s.enabled || !s.pending) return;
    if (!std::isfinite(now) || now - s.pending_at > kHitWindow) s.pending = false;
}

} // namespace aimlead
