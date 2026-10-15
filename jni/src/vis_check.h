#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vis_check {

inline constexpr float  kDesiredAimRayLength = 300.0f;
inline constexpr float  kDesiredSphereRadius = 0.05f;
inline constexpr float  kDesiredAssistDist   = 300.0f;
inline constexpr double kProbeTimeoutSec     = 0.060;
inline constexpr double kStateTtlSec         = 1.500;
inline constexpr double kPriorityRefreshSec  = 0.080;
inline constexpr double kHwrActiveWindowSec  = 0.250;

struct RaySample {
    bool          valid        = false;
    std::uint64_t hit_object   = 0;
    std::uint64_t hit_entity   = 0;
    std::uint64_t hit_player   = 0;
    std::uint64_t hit_collider = 0;
    float         distance     = 0.0f;
    bool          point_valid  = false;
    float         point[3]     = {};
    bool          origin_valid = false;
    float         origin[3]    = {};
};

struct AimAssistSnapshot {
    bool          valid              = false;
    std::uint64_t cur_target         = 0;
    std::uint64_t cur_handler        = 0;
    std::uint64_t cur_collider       = 0;
    float         cur_last_seen_time = 0.0f;
    float         cur_distance       = 0.0f;
    float         cur_pos[3]         = {};
    std::uint64_t prev_target        = 0;
    std::uint64_t prev_handler       = 0;
    std::uint64_t prev_collider      = 0;
    float         prev_distance      = 0.0f;
    float         prev_pos[3]        = {};
    bool          hit_flag           = false;
};

struct TargetGeometry {
    std::uint64_t id                = 0; // PlayerManager* or BotAimEnt.id
    std::uint64_t entity_id         = 0; // Mineable/hkO component for bots
    std::uint64_t event_handler     = 0; // hsN* (playerEventHandler or NPCBrain.Entity)
    std::uint64_t aim_target        = 0; // AimTarget* MonoBehaviour
    std::uint64_t aim_collider      = 0; // Collider* on same GameObject as AimTarget
    float         feet[3]           = {};
    float         head[3]           = {};
    float         distance          = 0.0f;
    float         screen_dist_px    = 1e9f;
    bool          is_bot            = false;
    bool          occlusion_known   = false; // NearbyPlayerScreenMarkerView -> OcclusionModule hasValue
    bool          occlusion_visible = false; // OcclusionModule + 0x3D == 0 (Physics.Raycast to head)
};

struct ProbeWrite {
    bool          valid              = false;
    std::uint64_t target_id          = 0;
    std::uint64_t cur_target         = 0;
    std::uint64_t cur_collider       = 0;
    float         cur_last_seen_time = -1000.0f;
    float         cur_distance       = -1.0f;
    float         cur_pos[3]         = {0.0f, 0.0f, 0.0f};
};

struct TargetVisState {
    bool   visible      = false;
    bool   has_sample   = false;
    int    vis_streak   = 0;
    double updated_at   = -1.0e9;
};

inline bool vec3_finite(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline float vec3_len_sq(const float v[3]) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

inline bool point_hits_target_cylinder(const float pt[3],
                                       const TargetGeometry& t,
                                       float radius = 0.65f) {
    if (!vec3_finite(pt) || !vec3_finite(t.feet) || !vec3_finite(t.head)) return false;
    const float min_y = (t.feet[1] < t.head[1] ? t.feet[1] : t.head[1]) - 0.25f;
    const float max_y = (t.feet[1] > t.head[1] ? t.feet[1] : t.head[1]) + 0.25f;
    if (pt[1] < min_y || pt[1] > max_y) return false;
    const float cx = (t.feet[0] + t.head[0]) * 0.5f;
    const float cz = (t.feet[2] + t.head[2]) * 0.5f;
    const float dx = pt[0] - cx;
    const float dz = pt[2] - cz;
    return (dx * dx + dz * dz) <= radius * radius;
}

inline bool ray_passes_through_target(const float cam_pos[3],
                                      const float cam_fwd[3],
                                      const TargetGeometry& t,
                                      float max_ray_len,
                                      float& out_proj_dist) {
    out_proj_dist = 0.0f;
    if (!vec3_finite(cam_pos) || !vec3_finite(cam_fwd) ||
        !vec3_finite(t.feet) || !vec3_finite(t.head)) {
        return false;
    }
    const float fwd_len_sq = vec3_len_sq(cam_fwd);
    if (fwd_len_sq < 0.25f) return false;
    const float inv_len = 1.0f / std::sqrt(fwd_len_sq);
    const float fx = cam_fwd[0] * inv_len;
    const float fy = cam_fwd[1] * inv_len;
    const float fz = cam_fwd[2] * inv_len;

    // Test both chest/center and head against the ray cone.
    const float pts[2][3] = {
        {(t.feet[0] + t.head[0]) * 0.5f,
         t.feet[1] * 0.35f + t.head[1] * 0.65f,
         (t.feet[2] + t.head[2]) * 0.5f},
        {t.head[0], t.head[1], t.head[2]}
    };
    for (int i = 0; i < 2; ++i) {
        const float vx = pts[i][0] - cam_pos[0];
        const float vy = pts[i][1] - cam_pos[1];
        const float vz = pts[i][2] - cam_pos[2];
        const float proj = vx * fx + vy * fy + vz * fz;
        if (proj < 0.5f || proj > max_ray_len) continue;
        const float px = vx - fx * proj;
        const float py = vy - fy * proj;
        const float pz = vz - fz * proj;
        const float perp_sq = px * px + py * py + pz * pz;
        const float cone_r = (i == 0) ? 0.50f : 0.35f;
        if (perp_sq <= cone_r * cone_r) {
            out_proj_dist = proj;
            return true;
        }
    }
    return false;
}

inline int evaluate_ray_for_target(const RaySample& primary,
                                   const RaySample& aim,
                                   bool aim_ray_configured,
                                   float aim_ray_len,
                                   bool cam_valid,
                                   const float cam_pos[3],
                                   const float cam_fwd[3],
                                   const TargetGeometry& t) {
    // Returns: +1 = confirmed visible, -1 = confirmed occluded, 0 = inconclusive
    const RaySample* samples[2] = {&primary, &aim};
    for (const RaySample* s : samples) {
        if (!s->valid) continue;
        if (t.id != 0 && s->hit_player == t.id) return +1;
        if (t.entity_id != 0 && s->hit_entity == t.entity_id) return +1;
        // Для целей с работающим OcclusionModule (игроки <= 200м) проверяем
        // только прямое попадание в хитбокс (hit_player == t.id). Для ботов и
        // целей без OcclusionModule проверяем попадание точки луча в 3D-цилиндр
        // цели (например, попадание AimRaycast в капсулу AimTarget).
        if (!t.occlusion_known &&
            s->point_valid && point_hits_target_cylinder(s->point, t)) {
            return +1;
        }
    }

    if (!cam_valid) return 0;
    const float max_len = aim_ray_configured && aim_ray_len > 10.0f ? aim_ray_len : 1.5f;
    float proj_dist = 0.0f;
    if (!ray_passes_through_target(cam_pos, cam_fwd, t, max_len, proj_dist)) {
        return 0;
    }

    // Если луч упёрся в препятствие заметно ближе цели (и точка удара лежит
    // вне цилиндра самой цели) — цель перекрыта.
    for (const RaySample* s : samples) {
        if (!s->valid || s->distance <= 0.35f) continue;
        if (s->hit_player == 0 && s->hit_entity == 0 &&
            s->distance + 0.85f < proj_dist &&
            (!s->point_valid || !point_hits_target_cylinder(s->point, t))) {
            return -1;
        }
    }
    return 0;
}

class Tracker {
public:
    void reset() {
        states_.clear();
        in_flight_       = false;
        probe_target_id_ = 0;
        probe_aim_target_= 0;
        probe_started_at_= 0.0;
        last_hwr_tick_at_= -1.0e9;
        rr_cursor_       = 0;
        prioritize_turn_ = false;
    }

    void record(std::uint64_t id, bool visible, double now) {
        if (!id) return;
        TargetVisState& st = states_[id];
        st.visible    = visible;
        st.has_sample = true;
        st.vis_streak = visible ? 2 : 0;
        st.updated_at = now;
    }

    bool consume_hwr_snapshot(const AimAssistSnapshot& snap,
                              const std::vector<TargetGeometry>& targets,
                              double now) {
        if (!snap.valid) return false;
        bool ticked = false;

        // 1. Check our round-robin off-axis probe in hwR.tjb (+0x60) + tjU (+0x68).
        if (in_flight_ && probe_target_id_ != 0 && probe_aim_target_ != 0) {
            const bool cur_pos_written = vec3_finite(snap.cur_pos) &&
                                         vec3_len_sq(snap.cur_pos) > 1.0f;
            const bool prev_pos_written = vec3_finite(snap.prev_pos) &&
                                          vec3_len_sq(snap.prev_pos) > 1.0f;
            const bool unity_evaluated =
                (snap.cur_target != probe_aim_target_) ||
                (snap.cur_last_seen_time > 0.0f) ||
                cur_pos_written ||
                prev_pos_written;
            if (unity_evaluated) {
                const bool vis =
                    (snap.cur_target == probe_aim_target_ && snap.cur_last_seen_time > 0.0f) ||
                    (snap.prev_target == probe_aim_target_);
                record(probe_target_id_, vis, now);
                in_flight_        = false;
                probe_target_id_  = 0;
                probe_aim_target_ = 0;
                last_hwr_tick_at_ = now;
                ticked            = true;
            } else if (now - probe_started_at_ >= kProbeTimeoutSec) {
                in_flight_        = false;
                probe_target_id_  = 0;
                probe_aim_target_ = 0;
            }
        }

        // 2. Any target currently held in hwR.tjb (+0x60) with a valid
        //    LastSeenTime (> 0) is verified visible this frame by hwR::ohr() -> hwR::ohe().
        if (snap.cur_target != 0 &&
            (!in_flight_ || snap.cur_target != probe_aim_target_ || snap.cur_last_seen_time > 0.0f)) {
            for (const TargetGeometry& t : targets) {
                if ((t.aim_target != 0 && t.aim_target == snap.cur_target) ||
                    (t.event_handler != 0 && t.event_handler == snap.cur_handler)) {
                    record(t.id, true, now);
                    last_hwr_tick_at_ = now;
                    break;
                }
            }
        }
        return ticked;
    }

    void update_from_rays(const RaySample& primary,
                          const RaySample& aim,
                          bool aim_ray_configured,
                          float aim_ray_len,
                          bool cam_valid,
                          const float cam_pos[3],
                          const float cam_fwd[3],
                          const std::vector<TargetGeometry>& targets,
                          double now) {
        for (const TargetGeometry& t : targets) {
            if (!t.id) continue;
            const int r = evaluate_ray_for_target(
                primary, aim, aim_ray_configured, aim_ray_len,
                cam_valid, cam_pos, cam_fwd, t);

            // Прямое попадание луча камеры в саму цель.
            if (r > 0) {
                record(t.id, true, now);
                continue;
            }

            // По-игроковой 3D Physics.Raycast из NearbyPlayerScreenMarkerView -> OcclusionModule.
            if (t.occlusion_known) {
                if (!t.occlusion_visible) {
                    // Уход за укрытие (контейнер/стену) фиксируем мгновенно без задержки.
                    record(t.id, false, now);
                } else {
                    // Выход из-за контейнера: если цель уже числилась за преградой,
                    // требуем 2 подряд видимых сэмпла (~16 мс), чтобы одиночный
                    // проскок луча через стык контейнеров на бегу не давал вспышку.
                    TargetVisState& st = states_[t.id];
                    if (!st.has_sample || st.visible) {
                        record(t.id, true, now);
                    } else {
                        ++st.vis_streak;
                        if (st.vis_streak >= 2) {
                            record(t.id, true, now);
                        }
                    }
                }
                continue;
            }

            if (r < 0) {
                record(t.id, false, now);
            }
        }
    }

    bool is_visible(const TargetGeometry& t, double now) const {
        auto it = states_.find(t.id);
        if (it != states_.end() && it->second.has_sample &&
            (now - it->second.updated_at) <= kStateTtlSec) {
            return it->second.visible;
        }
        if (t.occlusion_known) return t.occlusion_visible;
        if (t.aim_target != 0 && t.aim_collider != 0 &&
            (now - last_hwr_tick_at_) <= kHwrActiveWindowSec) {
            return false;
        }
        return true;
    }

    ProbeWrite select_next_probe(const std::vector<TargetGeometry>& targets,
                                 double now) {
        ProbeWrite pw{};
        if (in_flight_ || targets.empty()) return pw;

        // Alternate between highest-priority crosshair target needing refresh
        // and round-robin over all probeable targets. Skip targets already
        // confirmed visible this frame by NearbyPlayerScreenMarkerView's
        // OcclusionModule head raycast.
        int chosen = -1;
        prioritize_turn_ = !prioritize_turn_;
        if (prioritize_turn_) {
            float best_px = 1e18f;
            for (size_t i = 0; i < targets.size(); ++i) {
                const TargetGeometry& t = targets[i];
                if (!t.id || !t.aim_target || !t.aim_collider) continue;
                if (t.occlusion_known && t.occlusion_visible) continue;
                if (t.distance > kDesiredAssistDist) continue;
                auto it = states_.find(t.id);
                const bool stale = (it == states_.end()) ||
                                   !it->second.has_sample ||
                                   (now - it->second.updated_at) >= kPriorityRefreshSec;
                if (stale && t.screen_dist_px < best_px) {
                    best_px = t.screen_dist_px;
                    chosen  = static_cast<int>(i);
                }
            }
        }

        if (chosen < 0) {
            const size_t n = targets.size();
            for (size_t step = 0; step < n; ++step) {
                const size_t idx = (rr_cursor_ + step) % n;
                const TargetGeometry& t = targets[idx];
                if (!t.id || !t.aim_target || !t.aim_collider) continue;
                if (t.occlusion_known && t.occlusion_visible) continue;
                if (t.distance > kDesiredAssistDist) continue;
                chosen = static_cast<int>(idx);
                rr_cursor_ = (idx + 1) % n;
                break;
            }
        }

        if (chosen < 0) return pw;
        const TargetGeometry& pick = targets[static_cast<size_t>(chosen)];
        in_flight_        = true;
        probe_target_id_  = pick.id;
        probe_aim_target_ = pick.aim_target;
        probe_started_at_ = now;

        pw.valid              = true;
        pw.target_id          = pick.id;
        pw.cur_target         = pick.aim_target;
        pw.cur_collider       = pick.aim_collider;
        pw.cur_last_seen_time = -1000.0f;
        pw.cur_distance       = -1.0f;
        pw.cur_pos[0]         = 0.0f;
        pw.cur_pos[1]         = 0.0f;
        pw.cur_pos[2]         = 0.0f;
        return pw;
    }

    void prune(const std::vector<TargetGeometry>& targets, double now) {
        if (states_.size() <= 128) return;
        for (auto it = states_.begin(); it != states_.end();) {
            bool keep = (now - it->second.updated_at) <= kStateTtlSec;
            if (!keep) {
                for (const TargetGeometry& t : targets) {
                    if (t.id == it->first) { keep = true; break; }
                }
            }
            if (!keep) it = states_.erase(it);
            else ++it;
        }
    }

    bool in_flight() const { return in_flight_; }
    std::uint64_t probe_target_id() const { return probe_target_id_; }

private:
    std::unordered_map<std::uint64_t, TargetVisState> states_;
    bool          in_flight_        = false;
    std::uint64_t probe_target_id_  = 0;
    std::uint64_t probe_aim_target_ = 0;
    double        probe_started_at_ = 0.0;
    double        last_hwr_tick_at_ = -1.0e9;
    std::size_t   rr_cursor_        = 0;
    bool          prioritize_turn_  = false;
};

} // namespace vis_check
