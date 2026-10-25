#include "target_predictor.h"
#include "aim_ballistics.h"

#include <cassert>
#include <cmath>
#include <unordered_map>

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Game side: position changes every 100 ms, we poll at 60 fps.
template <class F>
static tgtpred::Estimate run(F pos_at, double duration, double step = 0.1) {
    tgtpred::Track tr;
    double t = 0.0, held = -1.0;
    Vec3 shown = pos_at(0.0);
    for (; t <= duration; t += 1.0 / 60.0) {
        if (t - held >= step - 1e-9) { shown = pos_at(t); held = t; }
        tgtpred::update(tr, shown, t);
    }
    return tgtpred::estimate(tr, t);
}

int main() {
    using namespace tgtpred;
    // steady run 6 m/s along x: velocity recovered, flagged straight
    {
        auto e = run([](double t) { return Vec3{float(6.0 * t), 0.0f, 0.0f}; }, 1.2);
        assert(e.valid && near(e.vel.x, 6.0f, 0.6f) && near(e.vel.z, 0.0f, 0.2f));
        assert(e.straight);
        assert(std::fabs(e.accel.x) < 3.0f);
    }
    // bursty ticks above sprint speed are clamped to kMaxSpeed (6.8 m/s)
    {
        auto e = run([](double t) { return Vec3{float(12.0 * t), 0.0f, 0.0f}; }, 0.8);
        assert(e.valid && near(e.vel.x, kMaxSpeed, 0.05f));
    }
    // standing: zero velocity, no phantom lead
    {
        auto e = run([](double) { return Vec3{5.0f, 1.0f, 5.0f}; }, 1.0);
        assert(e.valid && e.vel.x == 0.0f && e.vel.z == 0.0f && !e.straight);
    }
    // acceleration from rest is picked up (accel > 0, speed below final)
    {
        auto e = run([](double t) { return Vec3{float(0.5 * 8.0 * t * t), 0.0f, 0.0f}; }, 0.7);
        assert(e.valid && e.accel.x > 2.0f && e.vel.x > 2.0f);
    }
    // strafe reversal: after the flip the estimate follows the new direction
    {
        auto e = run([](double t) {
            double x = t < 0.6 ? 6.0 * t : 3.6 - 6.0 * (t - 0.6);
            return Vec3{float(x), 0.0f, 0.0f}; }, 0.85);
        // two position updates after the flip: must already point the new way
        assert(e.valid && e.vel.x < -3.0f);
    }
    // teleport / respawn resets history instead of producing a huge velocity
    {
        Track tr;
        for (int i = 0; i < 20; ++i) update(tr, Vec3{float(i * 0.1f), 0, 0}, i * 0.1);
        update(tr, Vec3{500.0f, 0, 0}, 2.1);
        assert(tr.n == 1);
    }
    // NaN / stale handled
    {
        Track tr;
        update(tr, Vec3{NAN, 0, 0}, 0.0);
        assert(tr.n == 0);
        update(tr, Vec3{0, 0, 0}, 0.0); update(tr, Vec3{1, 0, 0}, 0.1);
        assert(!estimate(tr, 5.0).valid);
    }
    // acceleration term in the solver path model
    {
        aim_ballistics::TargetModel m{};
        m.accel = {4.0f, 0.0f, 0.0f}; m.accel_time = 0.3f;
        const Vec3 v{0.0f, 0.0f, 0.0f};
        assert(near(aim_ballistics::target_offset(v, 0.2f, m).x, 0.5f * 4.0f * 0.04f, 1e-5f));
        // after accel_time the target keeps the gained speed (a*ta)
        assert(near(aim_ballistics::target_offset(v, 0.5f, m).x,
                    0.5f * 4.0f * 0.09f + 4.0f * 0.3f * 0.2f, 1e-5f));
    }
    // prune drops idle tracks only
    {
        std::unordered_map<unsigned long long, Track> m;
        for (unsigned long long i = 0; i < 80; ++i) { m[i].seen = i < 40 ? 0.0 : 9.0; }
        prune(m, 10.0);
        assert(m.size() == 40);
    }
    return 0;
}
