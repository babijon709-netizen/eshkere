#pragma once
// Target motion estimator for silent aim.
//
// The game refreshes remote positions in ~100 ms steps while we read every
// frame, so a per-frame difference is mostly zeros and spikes. This keeps a
// short history of *observed position changes* and fits
//     p(tau) = p0 + v*tau + 0.5*a*tau^2          (tau <= 0, newest sample = 0)
// per axis by recency-weighted least squares. Output: velocity, acceleration,
// fit quality and the age of the newest sample (how stale the position is).
// Header-only, no game types; unit-tested in tests/test_target_predictor.cpp.
#include "Vector.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace tgtpred {

constexpr int    kMaxSamples   = 12;
constexpr double kWindow       = 0.60;   // s of history used for the fit
constexpr double kWeightTau    = 0.22;   // s, recency weight e^(-age/tau)
constexpr double kHoldSample   = 0.25;   // s, push a "still here" sample
constexpr double kDeadAfter    = 0.70;   // s without any sample -> no estimate
constexpr float  kMinStep      = 0.015f; // m, smaller = position not updated
constexpr float  kMaxSpeed     = 6.8f;   // m/s (Oxide CharacterSpeedConfig Sprint = 6.6)
constexpr float  kMaxAccel     = 14.0f;  // m/s^2
constexpr float  kTeleportDist = 3.0f;   // m in one step ...
constexpr float  kTeleportSpeed= 25.0f;  // ... at > this m/s = respawn/teleport
constexpr float  kBadFitRms    = 0.30f;  // m, fit residual where confidence -> 0
constexpr float  kReversalMinSpeed = 1.5f;  // m/s on both sides of the flip
constexpr float  kReversalCos  = -0.2f;  // cos(angle) below this = reversal
constexpr double kAccelRidge   = 0.15;   // relative ridge on the accel term (noisy at 10 Hz)

struct Sample { Vec3 p; double t = 0.0; };

struct Track {
    Sample s[kMaxSamples];
    int    n = 0;
    double seen = 0.0;   // last time update() was called, for pruning
};

struct Estimate {
    Vec3   vel{}, accel{};
    float  rms = 0.0f;         // fit residual, m
    float  confidence = 0.0f;  // 0..1
    float  sample_age = 0.0f;  // s since the newest position change
    bool   straight = false;   // steady run: safe to lead the full flight
    bool   valid = false;
};

inline bool finite3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline void reset(Track& tr) { tr.n = 0; }

inline void push(Track& tr, const Vec3& p, double t) {
    while (tr.n > 0 && t - tr.s[0].t > kWindow) {
        for (int i = 1; i < tr.n; ++i) tr.s[i - 1] = tr.s[i];
        --tr.n;
    }
    if (tr.n == kMaxSamples) {
        for (int i = 1; i < tr.n; ++i) tr.s[i - 1] = tr.s[i];
        --tr.n;
    }
    tr.s[tr.n++] = Sample{p, t};
}

// Feed the target position every frame; samples are only stored when the game
// actually moved the target (or as a periodic "still here" when it did not).
inline void update(Track& tr, const Vec3& p, double now) {
    if (!finite3(p) || !std::isfinite(now)) return;
    tr.seen = now;
    if (tr.n == 0) { push(tr, p, now); return; }
    const Sample& last = tr.s[tr.n - 1];
    const double dt = now - last.t;
    if (dt <= 0.0) return;
    const float dx = p.x - last.p.x, dy = p.y - last.p.y, dz = p.z - last.p.z;
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (d > kTeleportDist && d / static_cast<float>(dt) > kTeleportSpeed) {
        reset(tr); push(tr, p, now); return;
    }
    if (d >= kMinStep) {
        // Direction reversal (strafe flip): drop the history that still carries the
        // old direction, keep only the turning point, so the fit follows the new
        // heading after one step instead of dragging the old one for ~0.3 s.
        if (tr.n >= 2) {
            const Sample& prev = tr.s[tr.n - 2];
            const double pdt = last.t - prev.t;
            if (pdt > 1e-3) {
                const float pvx = static_cast<float>((last.p.x - prev.p.x) / pdt);
                const float pvz = static_cast<float>((last.p.z - prev.p.z) / pdt);
                const float nvx = static_cast<float>(dx / dt), nvz = static_cast<float>(dz / dt);
                const float ps = std::sqrt(pvx * pvx + pvz * pvz);
                const float ns = std::sqrt(nvx * nvx + nvz * nvz);
                if (ps > kReversalMinSpeed && ns > kReversalMinSpeed &&
                    (pvx * nvx + pvz * nvz) < kReversalCos * ps * ns) {
                    const Sample turn = last;
                    tr.n = 0;
                    tr.s[tr.n++] = turn;
                }
            }
        }
        push(tr, p, now);
    }
    else if (dt >= kHoldSample) push(tr, last.p, now);
}

// Solve the 3x3 system A x = b (Gaussian elimination, partial pivoting).
inline bool solve3(double A[3][3], const double b[3], double x[3]) {
    double M[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) M[i][j] = A[i][j];
        M[i][3] = b[i];
    }
    for (int c = 0; c < 3; ++c) {
        int piv = c;
        for (int r = c + 1; r < 3; ++r)
            if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
        if (std::fabs(M[piv][c]) < 1e-12) return false;
        if (piv != c) for (int j = 0; j < 4; ++j) { double t = M[c][j]; M[c][j] = M[piv][j]; M[piv][j] = t; }
        for (int r = c + 1; r < 3; ++r) {
            const double f = M[r][c] / M[c][c];
            for (int j = c; j < 4; ++j) M[r][j] -= f * M[c][j];
        }
    }
    for (int i = 2; i >= 0; --i) {
        double v = M[i][3];
        for (int j = i + 1; j < 3; ++j) v -= M[i][j] * x[j];
        x[i] = v / M[i][i];
    }
    return true;
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline Estimate estimate(const Track& tr, double now) {
    Estimate e{};
    if (tr.n < 2) return e;
    const Sample& newest = tr.s[tr.n - 1];
    const double age = now - newest.t;
    if (!(age >= 0.0) || age > kDeadAfter) return e;
    e.sample_age = static_cast<float>(age);

    // Per-axis weighted LS on basis [1, tau, tau^2/2]; tau = t_i - t_newest.
    double c[3][3] = {};   // [axis][0..2] = p0, v, a
    double sq = 0.0, sw = 0.0;
    const bool quad = tr.n >= 4;
    for (int axis = 0; axis < 3; ++axis) {
        double A[3][3] = {}, b[3] = {};
        for (int i = 0; i < tr.n; ++i) {
            const double tau = tr.s[i].t - newest.t;
            const double w = std::exp(tau / kWeightTau);     // tau <= 0
            const double f[3] = {1.0, tau, 0.5 * tau * tau};
            const double p = axis == 0 ? tr.s[i].p.x : (axis == 1 ? tr.s[i].p.y : tr.s[i].p.z);
            for (int r = 0; r < 3; ++r) {
                b[r] += w * f[r] * p;
                for (int k = 0; k < 3; ++k) A[r][k] += w * f[r] * f[k];
            }
        }
        double x[3] = {};
        bool ok = false;
        if (quad) {
            A[2][2] *= 1.0 + kAccelRidge;                    // damp accel noise
            ok = solve3(A, b, x);
        }
        if (!ok) {                                           // linear fallback
            double A2[3][3] = {{A[0][0], A[0][1], 0}, {A[1][0], A[1][1], 0}, {0, 0, 1}};
            double b2[3] = {b[0], b[1], 0};
            ok = solve3(A2, b2, x);
            x[2] = 0.0;
        }
        if (!ok) return e;
        for (int k = 0; k < 3; ++k) c[axis][k] = x[k];
        for (int i = 0; i < tr.n; ++i) {
            const double tau = tr.s[i].t - newest.t;
            const double w = std::exp(tau / kWeightTau);
            const double p = axis == 0 ? tr.s[i].p.x : (axis == 1 ? tr.s[i].p.y : tr.s[i].p.z);
            const double r = p - (x[0] + x[1] * tau + 0.5 * x[2] * tau * tau);
            sq += w * r * r;
            if (axis == 0) sw += w;
        }
    }
    e.rms = static_cast<float>(std::sqrt(sq / (sw * 3.0 + 1e-9)));
    e.vel   = {static_cast<float>(c[0][1]), static_cast<float>(c[1][1]), static_cast<float>(c[2][1])};
    e.accel = {static_cast<float>(c[0][2]), static_cast<float>(c[1][2]), static_cast<float>(c[2][2])};
    if (!finite3(e.vel) || !finite3(e.accel)) return Estimate{};

    float conf = 1.0f - e.rms / kBadFitRms;
    conf = clampf(conf, 0.0f, 1.0f);
    if (tr.n < 3) conf *= 0.6f;
    e.confidence = conf;

    // Horizontal speed cap (keeps direction), vertical handled by the jump model.
    const float hs = std::sqrt(e.vel.x * e.vel.x + e.vel.z * e.vel.z);
    if (hs > kMaxSpeed) { const float k = kMaxSpeed / hs; e.vel.x *= k; e.vel.z *= k; }
    if (hs < 0.3f) { e.vel.x = 0.0f; e.vel.z = 0.0f; }
    e.accel.y = 0.0f;
    const float ha = std::sqrt(e.accel.x * e.accel.x + e.accel.z * e.accel.z);
    if (ha > kMaxAccel) { const float k = kMaxAccel / ha; e.accel.x *= k; e.accel.z *= k; }
    if (conf < 0.5f) { e.accel.x = 0.0f; e.accel.z = 0.0f; }

    const float speed = std::sqrt(e.vel.x * e.vel.x + e.vel.z * e.vel.z);
    e.straight = conf > 0.7f && speed > 2.0f && ha < 1.5f;
    e.valid = true;
    return e;
}

template <class Map>
inline void prune(Map& m, double now, double max_idle = 5.0, size_t soft_cap = 64) {
    if (m.size() <= soft_cap) return;
    for (auto it = m.begin(); it != m.end();) {
        if (now - it->second.seen > max_idle) it = m.erase(it);
        else ++it;
    }
}

} // namespace tgtpred
