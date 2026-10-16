#include "aim_ballistics.h"

#include <cassert>
#include <cmath>

static bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

int main() {
    using namespace aim_ballistics;
    const Vec3 origin{0.0f, 1.5f, 0.0f};
    Parameters params{100.0f, 0.0f, 0.0f, 0.0f, 0.0f, true};
    Solution solution{};

    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.valid);
    assert(near(solution.flight_time, 1.0f, 0.01f));
    assert(solution.direction.z > 0.999f);
    assert(std::fabs(solution.direction.x) < 0.001f);

    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {5.0f, 0.0f, 0.0f}, {},
                 params, solution));
    assert(solution.direction.x > 0.04f && solution.direction.x < 0.06f);
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {-5.0f, 0.0f, 0.0f}, {},
                 params, solution));
    assert(solution.direction.x < -0.04f && solution.direction.x > -0.06f);

    params.gravity = -9.81f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.direction.y > 0.04f);
    assert(solution.flight_time > 1.0f);

    // Both dump versions expose BallisticIntegrator::Step with a raw signed
    // acceleration and a fixed 31.25 ms step; do not reinterpret it as a scale.
    assert(near(gravity_acceleration(-9.81f), -9.81f, 1e-5f));
    assert(near(gravity_acceleration(-3.2f), -3.2f, 1e-5f));
    assert(near(gravity_acceleration(0.65f), 0.65f, 1e-5f));

    {
        Parameters raw{100.0f, -9.81f, 0.0f, 0.0f, 0.0f, true};
        assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, raw, solution));
        assert(solution.direction.y > 0.04f);
        assert(solution.flight_time > 1.0f);
    }

    params.gravity = 0.0f;
    params.air_drag = 0.1f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.flight_time > 1.0f);

    params.air_drag = 0.0f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {3.0f, 0.0f, 0.0f},
                 params, solution));
    assert(solution.direction.x < 0.0f);

    params.gravity = -9.81f;
    params.air_drag = 0.0f;
    params.underwater_drag = 0.0f;
    assert(solve({0.0f, -2.0f, 0.0f}, {0.0f, -2.0f, 100.0f}, {}, {},
                 params, solution));
    assert(std::fabs(solution.direction.y) < 0.001f);

    // A shot that enters water should stop accumulating air gravity after the
    // surface crossing; the recovered launch direction must remain shallow.
    assert(solve({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 100.0f}, {}, {},
                 params, solution));
    assert(solution.direction.y > 0.0f && solution.direction.y < 0.05f);

    params.max_range = 50.0f;
    assert(!solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    params.max_range = 0.0f;
    assert(!solve(origin, {0.0f, 1.5f, 100.0f}, {0.0f, 0.0f, 150.0f}, {},
                  params, solution));

    params.muzzle_speed = 0.0f;
    assert(!solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    return 0;
}
