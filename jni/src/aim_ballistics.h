#pragma once

#include "Vector.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace aim_ballistics {

constexpr float kIntegratorStep = 0.03125f;
// BallisticIntegrator.Step (RVA 0xCA6AFA4, file offset 0xCA66FA4) and
// ShaftedProjectile.Move (RVA 0x6DC8A60, file offset 0x6DC4A60) multiply
// Vector3.downVector (0, -1, 0) directly by p.Gravity * dt.
constexpr float kGravityY = -1.0f;
// ShaftedProjectile's game-side simulation limit is 400 integrator ticks.
constexpr int kMaxSteps = 400;
constexpr float kMaxFlightSeconds = kMaxSteps * kIntegratorStep;

struct Parameters {
    float muzzle_speed = 0.0f;
    float gravity = 0.0f;
    float air_drag = 0.0f;
    float underwater_drag = 0.0f;
    float max_range = 0.0f;
    bool valid = false;
};

struct Solution {
    Vec3 direction{};
    float flight_time = 0.0f;
    bool valid = false;
};

// Target path model. Default-constructed = legacy constant-velocity line, so
// existing callers/tests behave exactly as before.
//   tau      horizontal velocity decay, s. offset = v * tau * (1 - e^(-t/tau)).
//            Players change direction; a straight line over a long flight
//            overshoots every strafe reversal. 0 = off (straight line).
//   horizon  stop extrapolating after this many s (0 = no cap).
//   gravity  m/s^2 for an airborne target (|vy| >= airborne_vy): jump goes up,
//            peaks and lands back at launch height instead of flying off
//            linearly. 0 = off. FILL: measure from jump logs (apex time).
//   max_drop cap on predicted fall below the sample, m (ground height unknown).
//   latency  s added to the target's travel time on top of bullet flight: the
//            shot is resolved against the server-side position, which is ahead
//            of the sample we render by network delay + interpolation buffer.
//            0 = off (legacy: target only moves for the bullet flight time).
struct TargetModel {
    float tau = 0.0f;
    float horizon = 0.0f;
    float gravity = 0.0f;
    float airborne_vy = 2.0f;
    float max_drop = 3.0f;
    float latency = 0.0f;
    // Horizontal acceleration (m/s^2) applied for at most accel_time seconds,
    // after which the target keeps the velocity it reached. 0 = off.
    Vec3  accel{};
    float accel_time = 0.0f;
};

inline bool finite(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline bool finite(const Vec3& value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

// Fade stale velocity instead of extrapolating an old strafe at full speed.
// Remote positions normally refresh within ~100 ms; after 350 ms the estimate
// is too old to steer from and is treated as stationary.
inline float motion_confidence(float age_seconds) {
    if (!finite(age_seconds) || age_seconds < 0.0f) return 0.0f;
    constexpr float kFreshSeconds = 0.12f;
    constexpr float kStaleSeconds = 0.35f;
    if (age_seconds <= kFreshSeconds) return 1.0f;
    if (age_seconds >= kStaleSeconds) return 0.0f;
    return (kStaleSeconds - age_seconds) / (kStaleSeconds - kFreshSeconds);
}

// BallisticProfile.cQi (RVA 0x7BF07D8, file offset 0x7BEC7D8) and
// ShaftedProjectile.GetBallisticParams (RVA 0x6DCD320, file offset 0x6DC9320)
// pass their gravity field (5.0 for firearms, 2.5 for signal flare, 1.0..3.5
// for arrows/bolts/spears/rockets) directly into BallisticParams.Gravity.
inline float gravity_multiplier(float field_value) {
    if (!finite(field_value)) return 0.0f;
    return field_value < 0.0f ? -field_value : field_value;
}

inline Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 scale(const Vec3& value, float amount) {
    return {value.x * amount, value.y * amount, value.z * amount};
}

inline float length(const Vec3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

inline Vec3 target_offset(const Vec3& v, float t, const TargetModel& m) {
    float te = t + (m.latency > 0.0f ? m.latency : 0.0f);
    if (m.horizon > 0.0f && te > m.horizon) te = m.horizon;
    const float s = m.tau > 1.0e-3f ? m.tau * (1.0f - std::exp(-te / m.tau)) : te;
    Vec3 off{v.x * s, v.y * s, v.z * s};
    if (m.accel_time > 0.0f) {
        const float ta = te < m.accel_time ? te : m.accel_time;
        const float tail = te - ta;
        off.x += 0.5f * m.accel.x * ta * ta + m.accel.x * ta * tail;
        off.z += 0.5f * m.accel.z * ta * ta + m.accel.z * ta * tail;
    }
    if (m.gravity > 0.0f && std::fabs(v.y) >= m.airborne_vy) {
        if (v.y > 0.0f) {
            // Jump: lands back at launch height after 2*vy/g.
            const float land = 2.0f * v.y / m.gravity;
            const float tt = te < land ? te : land;
            off.y = v.y * tt - 0.5f * m.gravity * tt * tt;
        } else {
            off.y = v.y * te - 0.5f * m.gravity * te * te;
            if (off.y < -m.max_drop) off.y = -m.max_drop;
        }
    }
    return off;
}

inline float drag_scale(float drag, float dt) {
    const float value = 1.0f - drag * dt;
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

inline bool parameters_valid(const Parameters& params) {
    return params.valid && finite(params.muzzle_speed) &&
           params.muzzle_speed >= 1.0f && params.muzzle_speed <= 2000.0f &&
           finite(params.gravity) && params.gravity >= -20.0f && params.gravity <= 20.0f &&
           finite(params.air_drag) && params.air_drag >= 0.0f && params.air_drag <= 100.0f &&
           finite(params.underwater_drag) && params.underwater_drag >= 0.0f &&
           params.underwater_drag <= 100.0f && finite(params.max_range) &&
           params.max_range >= 0.0f && params.max_range <= 5000.0f;
}

inline float residual(float time, float factor, float drop,
                      const Vec3& origin, const Vec3& target,
                      const Vec3& target_velocity, const Vec3& inherited_velocity,
                      const Parameters& params, const TargetModel& model,
                      Vec3* required = nullptr) {
    const Vec3 moving_target = add(target, target_offset(target_velocity, time, model));
    Vec3 needed = sub(moving_target, origin);
    needed.y -= drop;
    needed = sub(needed, scale(inherited_velocity, factor));
    if (required) *required = needed;
    return length(needed) - params.muzzle_speed * factor;
}

inline bool solve_with_medium_path(const Vec3& origin, const Vec3& target,
                                   const Vec3& target_velocity,
                                   const Vec3& inherited_velocity,
                                   const Parameters& params, const Vec3& guess_direction,
                                   const TargetModel& model, Solution& out) {
    // Simulate the guessed trajectory and its scalar launch-speed / gravity
    // responses together. The surface state selects drag/gravity exactly as in
    // the game integrator; stopping at the first intercept avoids simulating
    // the unused tail of the 400-step lifetime.
    Vec3 position = origin;
    Vec3 velocity = add(scale(guess_direction, params.muzzle_speed), inherited_velocity);
    float factor = 0.0f;
    float factor_velocity = 1.0f;
    float drop = 0.0f;
    float drop_velocity = 0.0f;
    float previous_time = 0.0f;
    float previous_factor = 0.0f;
    float previous_drop = 0.0f;
    float previous_residual = residual(0.0f, 0.0f, 0.0f, origin, target,
                                       target_velocity, inherited_velocity, params, model);

    const bool both_above_surface = origin.y >= 0.0f && target.y >= 0.0f;
    for (int index = 1; index <= kMaxSteps; ++index) {
        const float dt = kIntegratorStep;
        const bool above_surface = both_above_surface || position.y >= 0.0f;
        const float drag = above_surface ? params.air_drag : params.underwater_drag;
        const float damping = drag_scale(drag, dt);

        // BallisticIntegrator.Step (RVA 0xCA6AFA4, file offset 0xCA66FA4) and
        // ShaftedProjectile.Move (RVA 0x6DC8A60, file offset 0x6DC4A60) save
        // old_v BEFORE adding Vector3.downVector * (gravity * dt), compute
        // damped = (old_v + g_delta) * damping, set state.velocity = 0.5f *
        // (old_v + damped), and advance state.position += state.velocity * dt.
        const Vec3 old_velocity = velocity;
        Vec3 with_gravity = old_velocity;
        if (above_surface) with_gravity.y += kGravityY * params.gravity * dt;
        const Vec3 damped_velocity = scale(with_gravity, damping);
        velocity = scale(add(old_velocity, damped_velocity), 0.5f);
        position = add(position, scale(velocity, dt));
        if (!finite(position) || !finite(velocity)) return false;

        const float damped_factor_velocity = factor_velocity * damping;
        const float next_factor_velocity =
            (factor_velocity + damped_factor_velocity) * 0.5f;
        const float next_factor = factor + next_factor_velocity * dt;
        const float gravity_delta = above_surface ? kGravityY * params.gravity * dt : 0.0f;
        const float damped_drop_velocity = (drop_velocity + gravity_delta) * damping;
        const float next_drop_velocity =
            (drop_velocity + damped_drop_velocity) * 0.5f;
        const float next_drop = drop + next_drop_velocity * dt;
        const float time = static_cast<float>(index) * dt;
        const float current_residual = residual(time, next_factor, next_drop,
            origin, target, target_velocity, inherited_velocity, params, model);

        if (previous_residual >= 0.0f && current_residual <= 0.0f) {
            float low = previous_time;
            float high = time;
            for (int iteration = 0; iteration < 20; ++iteration) {
                const float middle = (low + high) * 0.5f;
                const float alpha = (middle - previous_time) / dt;
                const float middle_factor = previous_factor + (next_factor - previous_factor) * alpha;
                const float middle_drop = previous_drop + (next_drop - previous_drop) * alpha;
                const float middle_residual = residual(middle, middle_factor, middle_drop,
                    origin, target, target_velocity, inherited_velocity, params, model);
                if (middle_residual > 0.0f) low = middle;
                else high = middle;
            }
            const float flight_time = (low + high) * 0.5f;
            const float alpha = (flight_time - previous_time) / dt;
            const float flight_factor = previous_factor + (next_factor - previous_factor) * alpha;
            const float flight_drop = previous_drop + (next_drop - previous_drop) * alpha;
            Vec3 needed{};
            residual(flight_time, flight_factor, flight_drop, origin, target,
                     target_velocity, inherited_velocity, params, model, &needed);
            const float distance = length(needed);
            const float travelled = params.muzzle_speed * flight_factor;
            // HitscanWeaponConfig.LJQ (0x7BDB4D8) allows projectiles to step up
            // to MaxDistance * 1.25f before expiration.
            if (!finite(distance) || distance < 0.01f || !finite(travelled) ||
                (params.max_range > 0.0f && travelled > params.max_range * 1.25f)) return false;
            out.direction = scale(needed, 1.0f / distance);
            out.flight_time = flight_time;
            out.valid = finite(out.direction) && flight_time > 0.0f;
            return out.valid;
        }

        previous_time = time;
        previous_factor = next_factor;
        previous_drop = next_drop;
        previous_residual = current_residual;
        factor = next_factor;
        factor_velocity = next_factor_velocity;
        drop = next_drop;
        drop_velocity = next_drop_velocity;
        if (!finite(previous_residual) || !finite(factor) || !finite(drop)) return false;
    }
    return false;
}

inline bool solve(const Vec3& origin, const Vec3& target,
                  const Vec3& target_velocity, const Vec3& inherited_velocity,
                  const Parameters& params, Solution& out,
                  const TargetModel& model = TargetModel{}) {
    out = {};
    if (!parameters_valid(params) || !finite(origin) || !finite(target) ||
        !finite(target_velocity) || !finite(inherited_velocity)) return false;

    const float target_distance = length(sub(target, origin));
    const float inherited_speed = length(inherited_velocity);
    const float target_speed = length(target_velocity);
    if (!finite(target_distance) || target_distance < 0.01f ||
        !finite(inherited_speed) || inherited_speed > 200.0f ||
        !finite(target_speed) || target_speed > 200.0f) return false;

    float estimate = target_distance / params.muzzle_speed;
    if (estimate > kMaxFlightSeconds) estimate = kMaxFlightSeconds;
    Vec3 guess = sub(add(target, target_offset(target_velocity, estimate, model)), origin);
    if (origin.y >= 0.0f && target.y >= 0.0f && params.gravity > 0.0f) {
        guess.y -= 0.25f * kGravityY * params.gravity * estimate * estimate;
    }
    float guess_length = length(guess);
    if (!finite(guess_length) || guess_length < 0.01f) return false;
    guess = scale(guess, 1.0f / guess_length);

    Solution candidate{};
    constexpr int kMaxPathIterations = 16;
    constexpr float kDirectionErrorSquared = 1.0e-8f;
    for (int iteration = 0; iteration < kMaxPathIterations; ++iteration) {
        if (!solve_with_medium_path(origin, target, target_velocity, inherited_velocity,
                                    params, guess, model, candidate)) return false;
        const Vec3 change = sub(candidate.direction, guess);
        const float error_squared = change.x * change.x + change.y * change.y +
                                    change.z * change.z;
        out = candidate;
        if (finite(error_squared) && error_squared <= kDirectionErrorSquared) return true;

        // Relax direction updates when the guessed air/water path flips near
        // the surface; the raw fixed-point iteration can otherwise alternate
        // between two valid but inconsistent medium sequences.
        const Vec3 relaxed = add(scale(guess, 0.25f), scale(candidate.direction, 0.75f));
        const float relaxed_length = length(relaxed);
        if (!finite(relaxed_length) || relaxed_length < 0.01f) return false;
        guess = scale(relaxed, 1.0f / relaxed_length);
    }
    return out.valid;
}

} // namespace aim_ballistics
