#include "vis_check.h"
#include "game_offsets.h"
#include "game_offsets_beta.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

struct LangPair {
    const char* ru;
    const char* en;
};

#include "lang_tables.inc"

void test_lang_table_sorted() {
    const size_t n = sizeof(kUiEn) / sizeof(kUiEn[0]);
    assert(n > 10);
    bool found_esp_vis = false;
    bool found_aim_vis = false;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            assert(std::strcmp(kUiEn[i - 1].ru, kUiEn[i].ru) < 0);
        }
        if (std::strcmp(kUiEn[i].ru, "Проверка видимости") == 0) {
            assert(std::strcmp(kUiEn[i].en, "Vis check") == 0);
            found_esp_vis = true;
        }
        if (std::strcmp(kUiEn[i].ru, "Только видимых") == 0) {
            assert(std::strcmp(kUiEn[i].en, "Visible only") == 0);
            found_aim_vis = true;
        }
    }
    assert(found_esp_vis);
    assert(found_aim_vis);
}

void test_offsets_consistency() {
    assert(game_offsets::PLAYER_MOUSE_LOOK == 0x70);
    assert(game_offsets::MOUSELOOK_PITCH_YAW == 0x4C);
    assert(game_offsets::PLAYER_RAYCAST_MANAGER == 0x88);
    assert(game_offsets::PLAYER_AIM_ASSIST == 0x258);
    assert(game_offsets::PLAYER_MODEL_INFO_REF == 0x260);
    assert(game_offsets::RAYCASTMAN_RAY_LENGTH == 0x38);
    assert(game_offsets::RAYCASTMAN_AIM_RAY_LENGTH == 0x3C);
    assert(game_offsets::RAYCASTMAN_SPHERE_RADIUS == 0x40);
    assert(game_offsets::RAYCASTMAN_LAYER_MASK == 0x48);
    assert(game_offsets::RAYCASTMAN_AIM_LAYER_MASK == 0x4C);
    assert(game_offsets::GUM_RAYCAST_DATA == 0x160);
    assert(game_offsets::GUM_AIM_RAYCAST == 0x168);
    assert(game_offsets::GKO_HIT_OBJECT == 0x18);
    assert(game_offsets::GKO_HIT_ENTITY == 0x20);
    assert(game_offsets::GKO_HIT_PLAYER == 0x40);
    assert(game_offsets::GKO_RAYCAST_HIT == 0x48);
    assert(game_offsets::GKO_RAY_ORIGIN == 0x74);
    assert(game_offsets::AIMASSIST_CONFIG == 0x18);
    assert(game_offsets::AIMASSIST_CAMERA == 0x28);
    assert(game_offsets::AIMASSIST_CUR_TARGET == 0x60);
    assert(game_offsets::AIMASSIST_CUR_SENSOR == 0x68);
    assert(game_offsets::AIMASSIST_DELTA_ROT == 0x108);
    assert(game_offsets::AIMSENSOR_LAST_SEEN_TIME == 0x0C);
    assert(game_offsets::AIMSENSOR_DISTANCE == 0x14);
    assert(game_offsets::GAME_CONTROLLER_MAP_MANAGER == 0x170);
    assert(game_offsets::MARKER_DATA_IS_HIT == 0x73);
    assert(game_offsets::PLAYER_HIT_TRACKER == 0x358);
    assert(game_offsets::HIT_TRACKER_HIT_DICT == 0x18);
    assert(game_offsets::NEARBY_VIEW_MODULE_CTX == 0x80);
    assert(game_offsets::NEARBY_VIEW_DATA == 0xA8);
    assert(game_offsets::NEARBY_VIEW_CACHED_HIT == 0xB1);
    assert(game_offsets::OCCLUSION_MOD_INTERVAL == 0x2C);
    assert(game_offsets::OCCLUSION_MOD_HAS_VAL == 0x3C);
    assert(game_offsets::OCCLUSION_MOD_IS_OCCLUDED == 0x3D);
    assert(game_offsets::HMJ_TYPEINFO_RVA == 0xE612F58);

    assert(game_offsets_beta::PLAYER_MOUSE_LOOK == 0xB0);
    assert(game_offsets_beta::MOUSELOOK_PITCH_YAW == game_offsets::MOUSELOOK_PITCH_YAW);
    assert(game_offsets_beta::PLAYER_RAYCAST_MANAGER == 0xC8);
    assert(game_offsets_beta::PLAYER_MODEL_INFO_REF == 0x2A0);
    assert(game_offsets_beta::GUM_AIM_RAYCAST == game_offsets::GUM_AIM_RAYCAST);
    assert(game_offsets_beta::AIMASSIST_CUR_TARGET == game_offsets::AIMASSIST_CUR_TARGET);
    assert(game_offsets_beta::AIMASSIST_DELTA_ROT == game_offsets::AIMASSIST_DELTA_ROT);
    assert(game_offsets_beta::MARKER_DATA_IS_HIT == game_offsets::MARKER_DATA_IS_HIT);
    // В бете 11982 вставка parachute-полей сдвинула трекер на +0x40;
    // внутренняя раскладка словарей осталась прежней.
    assert(game_offsets_beta::PLAYER_HIT_TRACKER == 0x398);
    assert(game_offsets_beta::HIT_TRACKER_HIT_DICT == game_offsets::HIT_TRACKER_HIT_DICT);
    assert(game_offsets_beta::NEARBY_VIEW_MODULE_CTX == game_offsets::NEARBY_VIEW_MODULE_CTX);
    assert(game_offsets_beta::NEARBY_VIEW_CACHED_HIT == game_offsets::NEARBY_VIEW_CACHED_HIT);
    assert(game_offsets_beta::OCCLUSION_MOD_IS_OCCLUDED == game_offsets::OCCLUSION_MOD_IS_OCCLUDED);
    assert(game_offsets_beta::HMJ_TYPEINFO_RVA != 0);
}

void test_marker_occlusion_and_rays() {
    using namespace vis_check;
    Tracker tracker;

    TargetGeometry t1{};
    t1.id = 0x1000;
    t1.event_handler = 0x1100;
    t1.aim_target = 0x1200;
    t1.aim_collider = 0x1300;
    t1.feet[0] = 0.0f; t1.feet[1] = 0.0f; t1.feet[2] = 50.0f;
    t1.head[0] = 0.0f; t1.head[1] = 1.6f; t1.head[2] = 50.0f;
    t1.distance = 50.0f;
    t1.screen_dist_px = 15.0f;
    t1.occlusion_known = true;
    t1.occlusion_visible = false; // Behind wall according to OcclusionModule

    TargetGeometry t2{};
    t2.id = 0x2000;
    t2.event_handler = 0x2100;
    t2.aim_target = 0x2200;
    t2.aim_collider = 0x2300;
    t2.feet[0] = 5.0f; t2.feet[1] = 0.0f; t2.feet[2] = 65.0f;
    t2.head[0] = 5.0f; t2.head[1] = 1.6f; t2.head[2] = 65.0f;
    t2.distance = 65.2f;
    t2.screen_dist_px = 120.0f;
    t2.occlusion_known = true;
    t2.occlusion_visible = true; // Visible according to OcclusionModule

    std::vector<TargetGeometry> targets = {t1, t2};
    RaySample empty_ray{};
    const float cam_pos[3] = {0.0f, 1.5f, 0.0f};
    const float cam_fwd[3] = {0.0f, 0.0f, 1.0f};

    tracker.update_from_rays(empty_ray, empty_ray, false, 1.5f, true, cam_pos, cam_fwd, targets, 1.0);
    assert(!tracker.is_visible(t1, 1.0));
    assert(tracker.is_visible(t2, 1.0));

    // When AimRaycast is NOT wall-configured (aim_ray_configured = false) or
    // AimAssist has hit_flag = false (hwR.tjN == 0 behind a wall), t1 MUST NOT flip green!
    RaySample aim_ray{};
    aim_ray.valid = true;
    aim_ray.hit_player = t1.id;
    aim_ray.distance = 50.0f;
    aim_ray.point_valid = true;
    aim_ray.point[0] = 0.0f; aim_ray.point[1] = 1.2f; aim_ray.point[2] = 50.0f;

    AimAssistSnapshot snap_wall_linger{};
    snap_wall_linger.valid = true;
    snap_wall_linger.cur_target = t1.aim_target;
    snap_wall_linger.cur_handler = t1.event_handler;
    snap_wall_linger.cur_collider = t1.aim_collider;
    snap_wall_linger.cur_last_seen_time = 120.0f;
    snap_wall_linger.hit_flag = false; // hwR.tjN == 0 when blocked by wall in hwR::ohb

    tracker.consume_hwr_snapshot(snap_wall_linger, targets, 1.05);
    tracker.update_from_rays(empty_ray, aim_ray, false, 1.5f, true, cam_pos, cam_fwd, targets, 1.05);
    assert(!tracker.is_visible(targets[0], 1.05));

    // However, when AimRaycast IS wall-masked (aim_ray_configured = true, Default|Construction|Terrain)
    // and directly hits t1's hitbox under the crosshair (e.g. head peeking over a railing),
    // it overrides the blocked waist OcclusionModule ray and marks t1 visible!
    tracker.update_from_rays(empty_ray, aim_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.08);
    assert(tracker.is_visible(targets[0], 1.08));

    // Direct camera ray hit on a target WITHOUT OcclusionModule (e.g., bot or 200+m player) marks it visible
    targets[0].occlusion_known = false;
    tracker.update_from_rays(empty_ray, aim_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.1);
    assert(tracker.is_visible(targets[0], 1.1));

    // Wall hit at 8m along the same ray occludes t1 at 50m
    RaySample wall_ray{};
    wall_ray.valid = true;
    wall_ray.hit_player = 0;
    wall_ray.hit_entity = 0;
    wall_ray.distance = 8.0f;
    wall_ray.point_valid = true;
    wall_ray.point[0] = 0.0f; wall_ray.point[1] = 1.5f; wall_ray.point[2] = 8.0f;

    tracker.update_from_rays(empty_ray, wall_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.3);
    assert(!tracker.is_visible(targets[0], 1.3));

    // Shipping container wall hit in front of t1 (1.2m in front of player,
    // hit_player == 0 && hit_entity == 0) must stay occluded (-1), never visible!
    RaySample close_container_ray{};
    close_container_ray.valid = true;
    close_container_ray.hit_player = 0;
    close_container_ray.hit_entity = 0;
    close_container_ray.distance = 48.8f;
    close_container_ray.point_valid = true;
    close_container_ray.point[0] = 0.0f;
    close_container_ray.point[1] = 1.2f;
    close_container_ray.point[2] = 48.8f;
    assert(evaluate_ray_for_target(empty_ray, close_container_ray, true, 350.0f, true, cam_pos, cam_fwd, targets[0]) == -1);

    // Empty GKo when Fox rejects a shipping container must return 0 (inconclusive), never +1!
    assert(evaluate_ray_for_target(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets[0]) == 0);

    // 1-frame container seam blip on default torso slot (occlusion_alt_point = false) at 50m
    // must NOT flash visible; 2 consecutive visible frames transition to visible.
    targets[0].occlusion_known = true;
    targets[0].occlusion_alt_point = false;
    targets[0].occlusion_visible = true;
    tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.316);
    assert(!tracker.is_visible(targets[0], 1.316)); // 1st frame blip suppressed!

    for (int k = 0; k < kOcclusionHoldTicks; ++k) {
        targets[0].occlusion_visible = false;
        tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.332 + k * 0.016);
    }
    assert(!tracker.is_visible(targets[0], 1.430)); // back behind container

    // Now genuinely peeks out for 2 consecutive frames -> becomes visible
    targets[0].occlusion_visible = true;
    tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.450);
    assert(!tracker.is_visible(targets[0], 1.450));
    tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.466);
    assert(tracker.is_visible(targets[0], 1.466));

    // Behind a powerline lattice bar (1-3 occluded probe slots while sweeping the 5 body points),
    // t1 stays visible without flickering red; once all 5 points stay occluded behind a solid
    // container (>= kOcclusionHoldTicks and >= kOcclusionHoldSec), t1 becomes occluded.
    targets[0].occlusion_visible = false;
    tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.482);
    assert(tracker.is_visible(targets[0], 1.482)); // held across single blocked lattice bar!
    for (int k = 1; k < kOcclusionHoldTicks; ++k) {
        tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam_pos, cam_fwd, targets, 1.482 + k * 0.030);
    }
    assert(!tracker.is_visible(targets[0], 1.482 + kOcclusionHoldTicks * 0.030));
}

void test_multi_point_railing_and_long_range() {
    using namespace vis_check;
    const float torso[3] = {10.0f, 1.0f, 80.0f};
    const float world[3] = {10.0f, 1.80f, 80.0f};
    const float cam[3]   = {10.0f, 1.6f, 0.0f};
    float pt0[3]{}, pt1[3]{}, pt2[3]{}, pt3[3]{}, pt4[3]{};

    compute_probe_target_point(0, torso, world, true, cam, true, pt0);
    compute_probe_target_point(1, torso, world, true, cam, true, pt1);
    compute_probe_target_point(2, torso, world, true, cam, true, pt2);
    compute_probe_target_point(3, torso, world, true, cam, true, pt3);
    compute_probe_target_point(4, torso, world, true, cam, true, pt4);

    // Slot 0 = torso (1.0m), Slot 1 = head (~1.58m), Slot 2 = upper chest (~1.32m),
    // Slot 3 & 4 = left/right shoulders (+/-0.24m perpendicular to camera ray).
    assert(std::fabs(pt0[1] - 1.0f) < 1e-4f);
    assert(pt1[1] > 1.50f);
    assert(pt2[1] > pt0[1] && pt2[1] < pt1[1]);
    assert(std::fabs(pt3[0] - (10.0f + 0.24f)) < 1e-3f || std::fabs(pt3[0] - (10.0f - 0.24f)) < 1e-3f);
    assert(std::fabs(pt3[0] - pt4[0]) > 0.45f);

    // Enemy behind a waist-high Train Station railing at 90m:
    // Slot 0 (torso) is blocked, Slot 1 (head, occlusion_alt_point = true) is clear -> immediately visible!
    Tracker tracker;
    TargetGeometry rail_enemy{};
    rail_enemy.id = 0x5000;
    rail_enemy.feet[0] = 10.0f; rail_enemy.feet[1] = 0.0f; rail_enemy.feet[2] = 90.0f;
    rail_enemy.head[0] = 10.0f; rail_enemy.head[1] = 1.6f; rail_enemy.head[2] = 90.0f;
    rail_enemy.distance = 90.0f;
    rail_enemy.occlusion_known = true;
    rail_enemy.occlusion_visible = false;
    rail_enemy.occlusion_alt_point = false;

    std::vector<TargetGeometry> targets = {rail_enemy};
    RaySample empty_ray{};
    const float cam_fwd[3] = {0.0f, 0.0f, 1.0f};
    tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam, cam_fwd, targets, 5.0);
    assert(!tracker.is_visible(targets[0], 5.0));

    // Next frame, probe steps to Slot 1 (Head) and clears the railing:
    targets[0].occlusion_visible = true;
    targets[0].occlusion_alt_point = true;
    tracker.update_from_rays(empty_ray, empty_ray, true, 350.0f, true, cam, cam_fwd, targets, 5.016);
    assert(tracker.is_visible(targets[0], 5.016));

    // Distant enemy at 240m (> 200m Na::Tick marker limit, so occlusion_known == false):
    // Must stay occluded (false, red) off-crosshair so players behind buildings/hills at 180+m
    // are never highlighted green, and become visible (true, green) when confirmed by the
    // 350m wall-masked AimRaycast.
    TargetGeometry far_enemy{};
    far_enemy.id = 0x6000;
    far_enemy.feet[0] = 10.0f; far_enemy.feet[1] = 0.0f; far_enemy.feet[2] = 240.0f;
    far_enemy.head[0] = 10.0f; far_enemy.head[1] = 1.6f; far_enemy.head[2] = 240.0f;
    far_enemy.distance = 240.0f;
    far_enemy.occlusion_known = false;
    assert(!tracker.is_visible(far_enemy, 5.016));

    std::vector<TargetGeometry> far_targets = {far_enemy};
    RaySample far_hit{};
    far_hit.valid = true;
    far_hit.hit_player = far_enemy.id;
    far_hit.distance = 240.0f;
    tracker.update_from_rays(empty_ray, far_hit, true, 350.0f, true, cam, cam_fwd, far_targets, 5.032);
    assert(tracker.is_visible(far_enemy, 5.032));
}

void test_hwr_offaxis_probe() {
    using namespace vis_check;
    Tracker tracker;

    TargetGeometry bot{};
    bot.id = 0x3000;
    bot.entity_id = 0x3050;
    bot.event_handler = 0x3100;
    bot.aim_target = 0x3200;
    bot.aim_collider = 0x3300;
    bot.feet[0] = 12.0f; bot.feet[1] = 0.0f; bot.feet[2] = 40.0f;
    bot.head[0] = 12.0f; bot.head[1] = 1.6f; bot.head[2] = 40.0f;
    bot.distance = 41.8f;
    bot.screen_dist_px = 45.0f;
    bot.is_bot = true;

    std::vector<TargetGeometry> targets = {bot};

    const ProbeWrite pw = tracker.select_next_probe(targets, 2.0);
    assert(pw.valid);
    assert(pw.target_id == bot.id);
    assert(pw.cur_target == bot.aim_target);
    assert(pw.cur_collider == bot.aim_collider);
    assert(pw.cur_last_seen_time < -500.0f);
    assert(pw.cur_distance < 0.0f);

    // Case 1: Unity hwR::ohr -> hwR::ohe blocked by wall -> clears tjb to 0
    AimAssistSnapshot snap_blocked{};
    snap_blocked.valid = true;
    snap_blocked.cur_target = 0;
    snap_blocked.cur_collider = bot.aim_collider;
    snap_blocked.cur_last_seen_time = -1000.0f;
    assert(tracker.consume_hwr_snapshot(snap_blocked, targets, 2.016));
    assert(!tracker.is_visible(bot, 2.016));

    // Re-probe and simulate visible hit (Unity sets LastSeenTime = Time.time > 0 and hit_flag = true)
    const ProbeWrite pw2 = tracker.select_next_probe(targets, 2.15);
    assert(pw2.valid);
    AimAssistSnapshot snap_vis{};
    snap_vis.valid = true;
    snap_vis.cur_target = bot.aim_target;
    snap_vis.cur_collider = bot.aim_collider;
    snap_vis.cur_last_seen_time = 154.25f;
    snap_vis.cur_distance = 41.8f;
    snap_vis.hit_flag = true;
    assert(tracker.consume_hwr_snapshot(snap_vis, targets, 2.166));
    assert(tracker.is_visible(bot, 2.166));
}

void test_hmj_solid_wall_mask_extraction() {
    using namespace vis_check;
    std::int32_t f[25]{};
    f[0]  = 1 << 0;  // +0x00 Default (static monument walls/buildings)
    f[1]  = 1 << 16; // +0x04 Hitbox
    f[2]  = 1 << 17; // +0x08 IgnoreCollision
    f[3]  = 1 << 8;  // +0x0C Construction (Unity user layer 8: player bases/walls)
    f[4]  = 1 << 9;  // +0x10 Deployed (Unity user layer 9: crate/barrel junkpiles, 1-4m zones)
    f[5]  = 1 << 10; // +0x14 Terrain (Unity user layer 10: terrain, rocks, shipping containers)
    f[6]  = 1 << 4;  // +0x18 Water
    f[7]  = 1 << 14; // +0x1C ProxyEntity
    f[8]  = 1 << 15; // +0x20 Entity
    f[9]  = 1 << 11; // +0x24 First Person (Unity user layer 11)
    f[20] = 1 << 19; // +0x50 Doorway
    f[21] = 1 << 12; // +0x54 Vehicle
    f[22] = 1 << 13; // +0x58 VehicleBox
    f[23] = 1 << 4;  // +0x5C Water
    f[24] = 1 << 21; // +0x60 Trigger

    f[10] = f[0] | f[3] | f[4] | f[5] | f[6]; // +0x28 Default|Construction|Deployed|Terrain|Water
    f[11] = f[0] | f[5] | f[6];               // +0x2C Default|Terrain|Water
    f[12] = f[8] | f[21];                     // +0x30 Entity|Vehicle
    f[15] = f[10];                            // +0x3C == +0x28

    std::int32_t solid = 0;
    std::int32_t strip = 0;
    assert(extract_hmj_solid_wall_mask(f, solid, strip));
    assert(solid == ((1 << 0) | (1 << 8) | (1 << 10))); // Default | Construction | Terrain
    assert((solid & f[0]) != 0);  // Default preserved (monument walls)
    assert((solid & f[3]) != 0);  // Construction preserved (player walls)
    assert((solid & f[5]) != 0);  // Terrain preserved (terrain/containers)
    assert((solid & f[4]) == 0);  // Deployed excluded
    assert((solid & f[21]) == 0); // Vehicle excluded
    assert((solid & f[22]) == 0); // VehicleBox excluded
    assert((solid & f[24]) == 0); // Trigger excluded

    const std::int32_t m_no_placement = 1 << 18; // d[12] NoPlacement (in hMD, not in hMj)
    const std::int32_t m_sleeper      = 1 << 20; // d[16] (in hMD, not in hMj)
    const std::int32_t raw_occ_mask = f[10] | f[21] | f[22] | f[24] | m_no_placement | m_sleeper;
    const std::int32_t sanitized = sanitize_occlusion_obstacle_mask(raw_occ_mask, solid, strip);
    assert(sanitized == solid);
    assert((sanitized & f[4]) == 0);
    assert((sanitized & f[21]) == 0);
    assert((sanitized & f[22]) == 0);
    assert((sanitized & f[24]) == 0);
    assert((sanitized & m_no_placement) == 0);
    assert((sanitized & m_sleeper) == 0);
    assert((strip & f[1]) == 0);  // Hitbox preserved in AimRaycast
    assert((strip & f[7]) == 0);  // ProxyEntity preserved in AimRaycast
    assert((strip & f[8]) == 0);  // Entity preserved in AimRaycast
    assert((strip & f[4]) != 0);  // Deployed stripped in AimRaycast
    assert((strip & m_no_placement) != 0); // NoPlacement stripped in AimRaycast

    // Also test hMD.static_fields (18 layer indices, where d[15] is a custom layer != 4):
    std::int32_t d[18] = {
        0,  // Default
        8,  // Construction
        9,  // Deployed
        10, // Terrain
        5,  // UI
        11, // First Person
        12, // Vehicle
        13, // VehicleBox
        14, // ProxyEntity
        15, // Entity
        16, // Hitbox
        17, // IgnoreCollision
        18, // NoPlacement
        4,  // Water
        19, // Doorway
        22, // d[15] custom layer
        20, // d[16] custom layer
        21  // Trigger
    };
    std::int32_t d_solid = 0;
    std::int32_t d_strip = 0;
    assert(extract_hmd_solid_wall_mask(d, d_solid, d_strip));
    assert(d_solid == solid);
    assert(sanitize_occlusion_obstacle_mask(raw_occ_mask, d_solid, d_strip) == solid);

    // Before hMj/hMD is resolved (solid == 0), cur_mask must remain untouched:
    assert(sanitize_occlusion_obstacle_mask(raw_occ_mask, 0, 0) == raw_occ_mask);

    // Corrupted table must be rejected:
    f[12] ^= 1;
    assert(!extract_hmj_solid_wall_mask(f, solid, strip));
}

} // namespace

int main() {
    test_lang_table_sorted();
    test_offsets_consistency();
    test_marker_occlusion_and_rays();
    test_multi_point_railing_and_long_range();
    test_hwr_offaxis_probe();
    test_hmj_solid_wall_mask_extraction();
    return 0;
}
