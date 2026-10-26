#include "aim_predict.h"
#include <cstdio>
#include <random>
#include <vector>

// Истина: игрок бежит по X, в определённые моменты разворачивается.
struct Truth {
    double t0;
    float pos(double t) const {
        // треугольная волна скорости: +6, потом -6 и т.д., период 2 с
        const double period = 2.0;
        const double phase = std::fmod(t, period);
        const double cycles = std::floor(t / period);
        // интегрируем кусочно, чтобы получить позицию
        double x = 0.0;
        const int whole = (int)cycles;
        x += whole * (6.0 * 1.0 - 6.0 * 1.0);   // за целый период смещение 0
        if (phase < 1.0) x += 6.0 * phase;
        else x += 6.0 * 1.0 - 6.0 * (phase - 1.0);
        return (float)x;
    }
    Vec3 vel(double t) const {
        const double phase = std::fmod(t, 2.0);
        const float vx = phase < 1.0 ? 6.0f : -6.0f;
        return {vx, 0.0f, 0.0f};
    }
};

// vel_also_delayed: скорость приходит тем же снапшотом, что и поза. Тогда
// измеримой разницы между позой и скоростью нет, и оценка обязана вернуть ноль,
// а не выдумать задержку из шума.
static void run(double truth_delay, double fps, bool vel_also_delayed = false) {
    aimpredict::Track tr;
    Truth truth;
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> noise(-0.5, 0.5);
    const double dt = 1.0 / fps;
    double t = 0.0;
    for (int i = 0; i < 400; ++i) {
        t += dt;
        // читаем задержанную позу и «сейчаснюю» скорость
        Vec3 p{truth.pos(t - truth_delay) + (float)(noise(rng) * 0.001), 0.0f, 0.0f};
        tr.push(p, truth.vel(vel_also_delayed ? t - truth_delay : t), t);
        if (i > 40 && (i % 5) == 0) tr.estimate_delay(t);
    }
    printf("truth %4.0f ms (%.0f fps)%s: learned %4.0f ms, confidence %.2f\n",
           truth_delay * 1000.0, fps, vel_also_delayed ? " +delayed vel" : "",
           tr.delay * 1000.0, tr.confidence);
}

int main() {
    for (double d : {0.0, 0.03, 0.06, 0.10, 0.15, 0.22}) {
        run(d, 60.0);
        run(d, 30.0);
        run(d, 60.0, true);
    }
}
