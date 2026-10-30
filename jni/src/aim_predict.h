#pragma once
// Предикт цели: задержка позы измеряется, а не задаётся константой.
//
// Задача. Мы читаем позу удалённого игрока из памяти — это отрисованное или
// сетевой отсчёт, который отстаёт от «истины на сервере» на неизвестную
// величину: путь снапшота к нам, буфер интерполяции Mirror и сглаживание
// модели. Раньше эта величина складывалась из констант (rtt/2, bufferTime,
// возраст отсчёта) и трижды подводила — то слишком длинно, то слишком коротко.
//
// Здесь она измеряется из данных, которые игра отдаёт сама:
//   * p(t) — поза, которую мы читаем (снапшот, с задержкой d от истины);
//   * v(t) — АВТОРИТЕТНАЯ скорость того же игрока (KCC.Move.RealVelocity):
//            её считает клиент-владелец, она не выводится из нашей позы.
// Для окна W:  p(t) - p(t - W) = ∫ v от (t - W - d) до (t - d).
// Значит d — единственный сдвиг, при котором смещение, посчитанное по позе,
// совпадает с интегралом скорости. Перебираем сетку сдвигов и берём тот, где
// невязка минимальна. Метод работает на любом движении с изменением скорости
// (страйф, старт, стоп, прыжок) и не требует ни трейса, ни ручной настройки.
//
// Из оценки следует сам лид: игроку нужно попасть в позу на момент
// (сейчас + управление + полёт), а наша поза относится к (сейчас - d), поэтому
// упреждение по времени = control + flight + d.
//
// Что оценка НЕ видит. Если скорость приходит к нам тем же снапшотом, что и поза
// (обе задержаны на d), то смещение позы за окно совпадёт с интегралом скорости
// при d = 0 — сетка честно вернёт ноль и лид останется (control + flight). Это
// безопасная деградация: мы не добавляем задержку, которой не измерили.
#include <cmath>
#include <cstdint>
#include <vector>

#include "Vector.h"

namespace aimpredict {

constexpr int    kMaxSamples    = 240;    // ~4 s при 60 Гц
constexpr double kWindow         = 0.15;  // окно сравнения, s
constexpr double kMinWindow      = 0.10;
constexpr double kMaxWindow      = 0.22;
constexpr double kDelayMax       = 0.30;  // верх сетки, s
constexpr double kDelayStep      = 0.01;  // шаг сетки, s
constexpr double kMinMoveForFit  = 0.12;  // м смещения в окне — иначе окно не информативно
constexpr float  kMaxSpeed       = 30.0f; // м/с: выше — телепорт/респаун, отсчёт не пишем
constexpr double kStaleSeconds   = 0.50;  // отсчёты старше — трек недействителен

inline bool finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 scale(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float length(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

struct Sample {
    double t = 0.0;
    Vec3   p{};    // поза, как её видим мы
    Vec3   v{};    // авторитетная скорость игрока в этот момент
};

struct Track {
    Sample s[kMaxSamples];
    int    n = 0;
    double delay = 0.0;        // оценка задержки позы, s
    double confidence = 0.0;   // 0..1: насколько чётко минимум отделён от соседей
    double updated = -1e9;

    void clear() { n = 0; delay = 0.0; confidence = 0.0; updated = -1e9; }

    void push(const Vec3& p, const Vec3& v, double now) {
        if (!finite(p) || !finite(v) || !std::isfinite(now)) return;
        if (n > 0 && now <= s[n - 1].t) return;          // один отсчёт на момент времени
        // Телепорт (респаун, посадка в транспорт, смена владельца пула): поза
        // прыгнула, скорость осталась прежней — трек начинается заново, иначе
        // невязка разъедется на весь прыжок.
        if (n >= 1) {
            const Sample& last = s[n - 1];
            const double dt = now - last.t;
            const float jump = length(sub(p, last.p));
            if (dt > 1e-3 && jump > 2.0f && jump / (float)dt > kMaxSpeed) { n = 0; }
            if (dt > 0.5) { n = 0; }
        }
        if (n == kMaxSamples) {
            for (int i = 1; i < n; ++i) s[i - 1] = s[i];
            --n;
        }
        s[n].t = now; s[n].p = p; s[n].v = v;
        ++n;
    }

    // Смещение позы за окно W, заканчивающееся в отсчёте index.
    bool pose_step(int index, double window, Vec3& out) const {
        if (index < 1) return false;
        const double t1 = s[index].t;
        for (int i = index - 1; i >= 0; --i) {
            if (t1 - s[i].t >= window) {
                out = sub(s[index].p, s[i].p);
                return true;
            }
        }
        return false;
    }

    // Интеграл авторитетной скорости на отрезке [from, to] по отсчётам трека.
    Vec3 integrate(double from, double to) const {
        Vec3 sum{};
        if (n < 2 || !(to > from)) return sum;
        for (int i = 1; i < n; ++i) {
            const double a = s[i - 1].t > from ? s[i - 1].t : from;
            const double b = s[i].t < to ? s[i].t : to;
            if (b <= a) continue;
            // Средняя скорость на интервале: привязка к одному концу давала
            // систематический сдвиг в полшага (на сетке 10 мс — ровно +10 мс).
            const Vec3 mid = scale(s[i - 1].v, 0.5f);
            const Vec3 mid2 = scale(s[i].v, 0.5f);
            const Vec3 vmid{mid.x + mid2.x, mid.y + mid2.y, mid.z + mid2.z};
            sum.x += vmid.x * (float)(b - a);
            sum.y += vmid.y * (float)(b - a);
            sum.z += vmid.z * (float)(b - a);
        }
        return sum;
    }

    // Оценка задержки: минимум невязки по сетке сдвигов.
    void estimate_delay(double now) {
        updated = now;
        if (n < 8) return;
        if (now - s[n - 1].t > kStaleSeconds) return;

        // Окно берём по фактическому шагу отсчётов: на 30 Гц окно из трёх-четырёх
        // отсчётов честнее, чем фиксированные 0.15 s из двух.
        double window = kWindow;
        if (n >= 2) {
            const double span = s[n - 1].t - s[0].t;
            if (span < 1.0) window = span * 0.5;
        }
        if (window < kMinWindow) window = kMinWindow;
        if (window > kMaxWindow) window = kMaxWindow;

        const int steps = (int)(kDelayMax / kDelayStep) + 1;
        double cost[64] = {};
        if (steps > 64) return;
        const int first = n - 1;
        int used = 0;
        for (int index = first; index >= 1; --index) {
            Vec3 observed{};
            if (!pose_step(index, window, observed)) break;
            // Окно без движения ничего не говорит о задержке: смещение ноль при
            // любой скорости = любая задержка подходит.
            if (length(observed) < kMinMoveForFit) continue;
            const double t1 = s[index].t;
            for (int k = 0; k < steps; ++k) {
                const double d = k * kDelayStep;
                const Vec3 predicted = integrate(t1 - window - d, t1 - d);
                const Vec3 err = sub(observed, predicted);
                cost[k] += length(err);
            }
            ++used;
        }
        if (used < 4) return;

        int best = 0;
        for (int k = 1; k < steps; ++k) if (cost[k] < cost[best]) best = k;
        // Уверенность: насколько дно глубже среднего по сетке. Плоское дно —
        // движение без смен скорости, оценка ничего не стоит, и лид берётся
        // прежний.
        double mean = 0.0;
        for (int k = 0; k < steps; ++k) mean += cost[k];
        mean /= (double)steps;
        const double depth = mean > 1e-6 ? (mean - cost[best]) / mean : 0.0;
        confidence = depth < 0.0 ? 0.0 : (depth > 1.0 ? 1.0 : depth);
        if (confidence < 0.15) return;            // дно не выражено — не обновляем
        const double fresh = best * kDelayStep;
        // Сглаживание: одиночный выброс не должен дёргать прицел.
        delay = delay > 0.0 ? delay * 0.7 + fresh * 0.3 : fresh;
    }

    // Точка, в которой цель окажется через `ahead` секунд от НАСТОЯЩЕГО момента
    // (то есть от серверного времени сейчас, а не от нашей задержанной позы).
    Vec3 predict_after(double now, float ahead) const {
        if (n == 0) return {};
        const Sample& last = s[n - 1];
        // Скорость берём последнюю авторитетную, подтянутую к «сейчас»: между
        // последним отсчётом и сейчас игрок мог остановиться, а мы про это не
        // знаем — короткое окно честнее любого продолжения.
        const float age = (float)(now - last.t);
        Vec3 v = last.v;
        if (age > 0.25f) v = scale(v, 0.0f);
        return {last.p.x + v.x * ahead, last.p.y + v.y * ahead, last.p.z + v.z * ahead};
    }
};

template <class Map>
inline void prune(Map& map, double now, double max_idle = 5.0, size_t soft_cap = 64) {
    if (map.size() <= soft_cap) return;
    for (auto it = map.begin(); it != map.end();) {
        if (now - it->second.updated > max_idle) it = map.erase(it);
        else ++it;
    }
}

} // namespace aimpredict
