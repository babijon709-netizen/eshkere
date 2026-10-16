#include "aim_ballistics.h"

#include <cassert>
#include <cmath>
#include <initializer_list>

static bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

static float simulated_miss_distance(const Vec3& origin,
                                     const Vec3& target,
                                     const Vec3& target_velocity,
                                     const Vec3& inherited_velocity,
                                     const aim_ballistics::Parameters& params,
                                     const aim_ballistics::Solution& solution) {
    using namespace aim_ballistics;
    Vec3 position = origin;
    Vec3 velocity = add(scale(solution.direction, params.muzzle_speed), inherited_velocity);
    float best = 1.0e30f;
    float elapsed = 0.0f;
    while (elapsed < solution.flight_time) {
        const float full_dt = kIntegratorStep;
        const float dt = std::min(full_dt, solution.flight_time - elapsed);
        const Vec3 old_velocity = velocity;
        if (position.y >= 0.0f) velocity.y += kGravityY * params.gravity * full_dt;
        const float drag = position.y >= 0.0f ? params.air_drag : params.underwater_drag;
        velocity = scale(velocity, drag_scale(drag, full_dt));
        const Vec3 full_next = add(position, scale(add(old_velocity, velocity), 0.5f * full_dt));
        const Vec3 next = add(position, scale(sub(full_next, position), dt / full_dt));
        const Vec3 r0 = sub(position, add(target, scale(target_velocity, elapsed)));
        const Vec3 r1 = sub(next, add(target, scale(target_velocity, elapsed + dt)));
        const Vec3 delta = sub(r1, r0);
        const float denom = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        float t = denom > 1.0e-8f
            ? -(r0.x * delta.x + r0.y * delta.y + r0.z * delta.z) / denom : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        best = std::min(best, length(add(r0, scale(delta, t))));
        position = full_next;
        elapsed += dt;
    }
    return best;
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

    // Prediction is solved from flight time at every distance, not selected
    // from a fixed-distance table or scaled by a manual lead setting.
    params.gravity = 0.0f;
    for (float distance : {25.0f, 100.0f, 250.0f}) {
        assert(solve(origin, {0.0f, 1.5f, distance}, {5.0f, 0.0f, 0.0f}, {},
                     params, solution));
        assert(near(solution.flight_time, distance / params.muzzle_speed, 0.01f));
        assert(near(solution.direction.x / solution.direction.z, 0.05f, 0.002f));
    }

    params.gravity = 1.0f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.direction.y > 0.04f);
    assert(solution.flight_time > 1.0f);

    // Check the solver against a second, forward-simulated game trajectory at
    // short and long range with drag, falling motion, target motion and owner
    // velocity all active together.
    Parameters stress{150.0f, 0.65f, 0.025f, 0.75f, 0.0f, true};
    const Vec3 moving{4.0f, 0.2f, -1.5f};
    const Vec3 owner{2.0f, 0.0f, 0.5f};
    for (float distance : {30.0f, 120.0f, 300.0f}) {
        const Vec3 point{0.0f, 1.5f, distance};
        assert(solve(origin, point, moving, owner, stress, solution));
        assert(simulated_miss_distance(origin, point, moving, owner, stress, solution) < 0.35f);
    }
    Parameters slow_projectile{40.0f, 0.4f, 0.01f, 0.0f, 1000.0f, true};
    const Vec3 far_target{0.0f, 1.5f, 250.0f};
    assert(solve(origin, far_target, {}, {}, slow_projectile, solution));
    assert(solution.flight_time > 4.0f && solution.flight_time < kMaxFlightSeconds);
    assert(simulated_miss_distance(origin, far_target, {}, {}, slow_projectile, solution) < 0.35f);

    assert(near(motion_confidence(0.05f), 1.0f, 1e-6f));
    assert(motion_confidence(0.2f) > 0.0f && motion_confidence(0.2f) < 1.0f);
    assert(near(motion_confidence(0.4f), 0.0f, 1e-6f));
    assert(motion_age_lead(0.2f) > 0.12f && motion_age_lead(0.2f) < 0.2f);

    // The game passes a gravity multiplier into BallisticIntegrator. Profile
    // data is already a multiplier; projectile prefabs may store 9.81 instead.
    assert(near(gravity_multiplier(0.65f), 0.65f, 1e-5f));
    assert(near(gravity_multiplier(-0.65f), 0.65f, 1e-5f));
    assert(near(gravity_multiplier(9.81f), 1.0f, 1e-3f));
    assert(near(gravity_multiplier(-9.81f), 1.0f, 1e-3f));
    assert(near(gravity_multiplier(19.62f), 2.0f, 1e-3f));

    {
        Parameters profile{100.0f, 0.65f, 0.0f, 0.0f, 0.0f, true};
        Parameters projectile = profile;
        projectile.gravity = gravity_multiplier(9.81f);
        Solution from_profile{}, from_projectile{};
        assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, profile, from_profile));
        assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, projectile, from_projectile));
        assert(from_profile.direction.y < from_projectile.direction.y);
    }

    params.gravity = 0.0f;
    params.air_drag = 0.1f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.flight_time > 1.0f);

    params.air_drag = 0.0f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {3.0f, 0.0f, 0.0f},
                 params, solution));
    assert(solution.direction.x < 0.0f);

    params.gravity = 1.0f;
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
