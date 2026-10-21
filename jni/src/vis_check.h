#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vis_check {

inline constexpr float  kDesiredAimRayLength = 350.0f;
inline constexpr float  kDesiredSphereRadius = 0.02f;
inline constexpr float  kDesiredAssistDist   = 300.0f;
inline constexpr float  kNearbyMarkerMaxDist = 198.0f;
inline constexpr double kProbeTimeoutSec     = 0.060;
inline constexpr double kStateTtlSec         = 1.500;
inline constexpr double kOcclusionHoldSec    = 0.140;
inline constexpr int    kOcclusionHoldTicks  = 6;
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
    bool          is_bot              = false;
    bool          occlusion_known     = false; // NearbyPlayerScreenMarkerView -> OcclusionModule hasValue
    bool          occlusion_visible   = false; // OcclusionModule + 0x3D == 0 (Physics.Raycast to probe point)
    bool          occlusion_alt_point = false; // Probe slot != 0 (head / upper chest / left-right shoulder)
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
    int    occ_streak   = 0;
    double last_vis_at  = -1.0e9;
    double updated_at   = -1.0e9;
};

inline bool vec3_finite(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline float vec3_len_sq(const float v[3]) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

inline bool is_single_layer_bit(std::int32_t m) {
    const std::uint32_t u = static_cast<std::uint32_t>(m);
    return u != 0u && (u & (u - 1u)) == 0u;
}

// Structural validator for release hMj (TypeDefIndex: 6280) and beta 11974 gVF
// (TypeDefIndex: 6611; 25 int32 LayerMask fields):
//   +0x00 Default (1<<0), +0x04 Hitbox, +0x08 IgnoreCollision, +0x0C Construction,
//   +0x10 Deployed, +0x14 Terrain, +0x18 Water (1<<4), +0x1C ProxyEntity, +0x20 Entity,
//   +0x24 First Person, +0x28 Default|Construction|Deployed|Terrain|Water,
//   +0x2C Default|Terrain|Water, +0x30 Entity|Vehicle, +0x3C == +0x28,
//   +0x50 Doorway, +0x54 Vehicle, +0x58 VehicleBox, +0x5C 1<<d[15], +0x60 Trigger.
// Extracts the solid wall/terrain mask (Default | Construction | Terrain) and the
// non-wall prop/vehicle/trigger/no-placement strip mask so OcclusionModule::Tick's
// 4-arg Physics.Raycast (QueryTriggerInteraction.UseGlobal) hits ONLY static
// monument walls (Default), player bases (Construction), and terrain/containers
// (Terrain), while ignoring Deployed / NoPlacement / Trigger / Vehicle / VehicleBox
// volumes around server-spawned crate/barrel junkpiles and cars.
inline bool extract_hmj_solid_wall_mask(const std::int32_t f[25],
                                        std::int32_t& out_solid_mask,
                                        std::int32_t& out_strip_mask) {
    out_solid_mask = 0;
    out_strip_mask = 0;
    const std::int32_t m_default      = f[0];  // +0x00 "Default" (layer 0 -> 0x1)
    const std::int32_t m_hitbox       = f[1];  // +0x04 "Hitbox"
    const std::int32_t m_construction = f[3];  // +0x0C "Construction"
    const std::int32_t m_deployed     = f[4];  // +0x10 "Deployed"
    const std::int32_t m_terrain      = f[5];  // +0x14 "Terrain"
    const std::int32_t m_water        = f[6];  // +0x18 "Water" (layer 4 -> 0x10)
    const std::int32_t m_proxy_entity = f[7];  // +0x1C "ProxyEntity"
    const std::int32_t m_entity       = f[8];  // +0x20 "Entity"
    const std::int32_t m_first_person = f[9];  // +0x24 "First Person"
    const std::int32_t m_comp_28      = f[10]; // +0x28
    const std::int32_t m_comp_2c      = f[11]; // +0x2C
    const std::int32_t m_comp_30      = f[12]; // +0x30
    const std::int32_t m_comp_3c      = f[15]; // +0x3C
    const std::int32_t m_vehicle      = f[21]; // +0x54 "Vehicle"

    if (m_default != 0x1) return false;
    if (!is_single_layer_bit(m_construction) || m_construction == m_default ||
        !is_single_layer_bit(m_deployed) || m_deployed == m_default ||
        !is_single_layer_bit(m_terrain) || m_terrain == m_default ||
        !is_single_layer_bit(m_water) ||
        !is_single_layer_bit(m_entity) || m_entity == m_default ||
        !is_single_layer_bit(m_first_person) || m_first_person == m_default ||
        !is_single_layer_bit(m_vehicle) || m_vehicle == m_default) {
        return false;
    }
    if (m_construction == m_deployed ||
        m_construction == m_terrain ||
        m_deployed == m_terrain) {
        return false;
    }
    if (m_comp_28 != (m_default | m_construction | m_deployed | m_terrain | m_water)) return false;
    if (m_comp_2c != (m_default | m_terrain | m_water)) return false;
    if (m_comp_30 != (m_entity | m_vehicle)) return false;
    if (m_comp_3c != m_comp_28) return false;

    out_solid_mask = m_default | m_construction | m_terrain;
    const std::uint32_t keep_aim = static_cast<std::uint32_t>(
        out_solid_mask |
        (is_single_layer_bit(m_hitbox) ? m_hitbox : 0) |
        (is_single_layer_bit(m_proxy_entity) ? m_proxy_entity : 0) |
        m_entity);
    out_strip_mask = static_cast<std::int32_t>(~keep_aim & 0x7fffffffu);
    return out_solid_mask > 0 && out_strip_mask > 0;
}

// Structural validator for hMD.static_fields (TypeDefIndex: 6279, 18 int32 layer indices):
//   +0x00 Default (0), +0x04 Construction, +0x08 Deployed, +0x0C Terrain,
//   +0x10 UI (5), +0x14 First Person, +0x18 Vehicle, +0x1C VehicleBox,
//   +0x20 ProxyEntity, +0x24 Entity, +0x28 Hitbox, +0x2C IgnoreCollision,
//   +0x30 NoPlacement, +0x34 Water (4), +0x38 Doorway, +0x3C d[15],
//   +0x40 d[16], +0x44 Trigger.
inline bool extract_hmd_solid_wall_mask(const std::int32_t d[18],
                                        std::int32_t& out_solid_mask,
                                        std::int32_t& out_strip_mask) {
    out_solid_mask = 0;
    out_strip_mask = 0;
    if (d[0] != 0 || d[4] != 5 || d[13] != 4) return false;
    constexpr int kRequiredIdx[] = {1, 2, 3, 5, 6, 8, 9, 10};
    for (int idx : kRequiredIdx) {
        if (d[idx] < 1 || d[idx] > 30 || d[idx] == 4 || d[idx] == 5) return false;
    }
    if (d[1] == d[2] || d[1] == d[3] || d[2] == d[3]) return false;

    const auto bit = [](std::int32_t layer) -> std::int32_t {
        return (layer >= 0 && layer <= 30) ? (std::int32_t(1) << layer) : 0;
    };
    out_solid_mask = bit(d[0]) | bit(d[1]) | bit(d[3]);
    const std::uint32_t keep_aim = static_cast<std::uint32_t>(
        out_solid_mask | bit(d[8]) | bit(d[9]) | bit(d[10]));
    out_strip_mask = static_cast<std::int32_t>(~keep_aim & 0x7fffffffu);
    return out_solid_mask > 0 && out_strip_mask > 0;
}

inline std::int32_t sanitize_occlusion_obstacle_mask(std::int32_t cur_mask,
                                                     std::int32_t solid_mask,
                                                     std::int32_t strip_mask = 0) {
    if (solid_mask > 0) return solid_mask;
    if (strip_mask > 0) {
        const std::int32_t stripped = cur_mask & ~strip_mask;
        return stripped > 0 ? stripped : cur_mask;
    }
    return cur_mask;
}

// Computes 5 anatomical probe points on a player from Na::Tick's marker data:
//   slot 0: mid-torso (body.position + (0, 1.0, 0), default Rqs)
//   slot 1: head (head.position = Rqc - (0, 0.25, 0) — clears waist/chest railings and ridges)
//   slot 2: upper chest / neck (45% torso + 55% head — clears railings under low ceilings)
//   slot 3: left shoulder (-0.24m perpendicular to camera ray — slips past lattice/fence bars)
//   slot 4: right shoulder (+0.24m perpendicular to camera ray — slips past lattice/fence bars)
inline void compute_probe_target_point(int slot,
                                       const float base_torso[3],
                                       const float world_pos[3],
                                       bool world_pos_valid,
                                       const float cam_pos[3],
                                       bool cam_valid,
                                       float out_pt[3]) {
    out_pt[0] = base_torso[0];
    out_pt[1] = base_torso[1];
    out_pt[2] = base_torso[2];
    const int s = ((slot % 5) + 5) % 5;
    if (s == 0) return;

    float head_pt[3] = {
        world_pos_valid ? world_pos[0] : base_torso[0],
        world_pos_valid ? (world_pos[1] - 0.22f) : (base_torso[1] + 0.55f),
        world_pos_valid ? world_pos[2] : base_torso[2]
    };
    if (head_pt[1] < base_torso[1] + 0.20f) {
        head_pt[1] = base_torso[1] + 0.55f;
    }
    if (s == 1) {
        out_pt[0] = head_pt[0];
        out_pt[1] = head_pt[1];
        out_pt[2] = head_pt[2];
        return;
    }

    const float chest[3] = {
        base_torso[0] * 0.45f + head_pt[0] * 0.55f,
        base_torso[1] * 0.45f + head_pt[1] * 0.55f,
        base_torso[2] * 0.45f + head_pt[2] * 0.55f
    };
    if (s == 2) {
        out_pt[0] = chest[0];
        out_pt[1] = chest[1];
        out_pt[2] = chest[2];
        return;
    }

    float rx = 1.0f;
    float rz = 0.0f;
    if (cam_valid && vec3_finite(cam_pos)) {
        const float dx = chest[0] - cam_pos[0];
        const float dz = chest[2] - cam_pos[2];
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len > 0.1f) {
            rx = -dz / len;
            rz =  dx / len;
        }
    }
    const float side = (s == 3) ? -0.24f : 0.24f;
    out_pt[0] = chest[0] + rx * side;
    out_pt[1] = chest[1];
    out_pt[2] = chest[2] + rz * side;
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

inline bool direct_crosshair_hit_target(const RaySample& primary,
                                        const RaySample& aim,
                                        bool aim_ray_configured,
                                        const TargetGeometry& t) {
    if (primary.valid) {
        if (t.id != 0 && primary.hit_player == t.id) return true;
        if (t.entity_id != 0 && primary.hit_entity == t.entity_id) return true;
    }
    if (aim_ray_configured && aim.valid) {
        if (t.id != 0 && aim.hit_player == t.id) return true;
        if (t.entity_id != 0 && aim.hit_entity == t.entity_id) return true;
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
    if (direct_crosshair_hit_target(primary, aim, aim_ray_configured, t)) {
        return +1;
    }
    const RaySample* samples[2] = {&primary, aim_ray_configured ? &aim : nullptr};
    for (const RaySample* s : samples) {
        if (!s || !s->valid) continue;
        // Для целей без OcclusionModule проверяем попадание точки луча в 3D-цилиндр
        // цели (например, попадание AimRaycast в капсулу AimTarget у бота/игрока).
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
        if (!s || !s->valid || s->distance <= 0.35f) continue;
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
        st.occ_streak = visible ? 0 : kOcclusionHoldTicks;
        if (visible) st.last_vis_at = now;
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
                    (snap.cur_target == probe_aim_target_ &&
                     snap.hit_flag &&
                     snap.cur_last_seen_time > 0.0f) ||
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

        // 2. Any target currently held in hwR.tjb (+0x60) is confirmed visible this
        //    frame when hwR.tjN (+0xDC, snap.hit_flag) is true: hwR::ohb (0x6500304)
        //    clears tjN to 0 at the start of every frame and only sets tjN = 1 (0x65006BC)
        //    when its obstacle-masked Physics.Raycast (using hMj+0x34 wall mask) hits
        //    tjU.Collider. When blocked by a wall (0x6500818), tjN is 0.
        if (snap.hit_flag && snap.cur_target != 0 && snap.cur_last_seen_time > 0.0f &&
            (!in_flight_ || snap.cur_target != probe_aim_target_)) {
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

            // 1. Прямое попадание луча прицела с маской твёрдых стен (RaycastData /
            //    AimRaycast с Default|Construction|Terrain) в хитбокс цели: если прицел
            //    наведён прямо на видимую часть тела/головы врага (в т.ч. сквозь перила,
            //    решётку или ЛЭП), цель гарантированно видима.
            if (direct_crosshair_hit_target(primary, aim, aim_ray_configured, t)) {
                record(t.id, true, now);
                continue;
            }

            // 2. По-игроковой 3D Physics.Raycast из NearbyPlayerScreenMarkerView -> OcclusionModule
            //    с маской твёрдых стен (Default | Construction | Terrain) и 5-точечным
            //    анатомическим перебором (торс, голова, верх груди, левое/правое плечо).
            if (t.occlusion_known) {
                TargetVisState& st = states_[t.id];
                if (t.occlusion_visible) {
                    st.occ_streak = 0;
                    // Если цель подтверждена видимой на альтернативной точке (голова/плечи
                    // над перилами или сквозь опору ЛЭП), вблизи (<= 35м), либо ещё не имела
                    // сэмпла / уже была видимой — фиксируем видимость сразу.
                    if (!st.has_sample || st.visible ||
                        t.occlusion_alt_point || t.distance <= 35.0f) {
                        record(t.id, true, now);
                    } else {
                        ++st.vis_streak;
                        if (st.vis_streak >= 2) {
                            record(t.id, true, now);
                        }
                    }
                } else {
                    ++st.occ_streak;
                    if (!st.has_sample || !st.visible) {
                        // Не сбрасываем накопленный vis_streak на одиночном перекрытом
                        // тике во время перебора 5 анатомических точек (слоты 0..4).
                        if (st.occ_streak >= kOcclusionHoldTicks) {
                            record(t.id, false, now);
                        } else if (!st.has_sample) {
                            st.visible    = false;
                            st.has_sample = true;
                            st.updated_at = now;
                        }
                    } else if (st.occ_streak >= kOcclusionHoldTicks &&
                               (now - st.last_vis_at) >= kOcclusionHoldSec) {
                        // Все 5 анатомических точек перекрыты (враг зашёл за сплошную стену
                        // или контейнер) — переводим в невидимые.
                        record(t.id, false, now);
                    }
                }
                continue;
            }

            // 3. Для целей без OcclusionModule (боты, цели вплотную, игроки > 200м)
            //    проверяем стеновой луч камеры (до 350м).
            const int r = evaluate_ray_for_target(
                primary, aim, aim_ray_configured, aim_ray_len,
                cam_valid, cam_pos, cam_fwd, t);

            if (r > 0) {
                record(t.id, true, now);
            } else if (r < 0) {
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
        if (!t.is_bot && t.distance > 2.5f) {
            return false;
        }
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
