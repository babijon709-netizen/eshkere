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
    assert(game_offsets::RAYCASTMAN_RAW_HIT == 0x50);
    assert(game_offsets::RAYCASTMAN_AIM_RAW_HIT == 0xA8);
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
    assert(game_offsets::OCCLUSION_MOD_INTERVAL == 0x2C);
    assert(game_offsets::OCCLUSION_MOD_HAS_VAL == 0x3C);
    assert(game_offsets::OCCLUSION_MOD_IS_OCCLUDED == 0x3D);

    assert(game_offsets_beta::PLAYER_MOUSE_LOOK == game_offsets::PLAYER_MOUSE_LOOK);
    assert(game_offsets_beta::MOUSELOOK_PITCH_YAW == game_offsets::MOUSELOOK_PITCH_YAW);
    assert(game_offsets_beta::PLAYER_RAYCAST_MANAGER == game_offsets::PLAYER_RAYCAST_MANAGER);
    assert(game_offsets_beta::PLAYER_MODEL_INFO_REF == game_offsets::PLAYER_MODEL_INFO_REF);
    assert(game_offsets_beta::GUM_AIM_RAYCAST == game_offsets::GUM_AIM_RAYCAST);
    assert(game_offsets_beta::AIMASSIST_CUR_TARGET == game_offsets::AIMASSIST_CUR_TARGET);
    assert(game_offsets_beta::AIMASSIST_DELTA_ROT == game_offsets::AIMASSIST_DELTA_ROT);
    assert(game_offsets_beta::MARKER_DATA_IS_HIT == game_offsets::MARKER_DATA_IS_HIT);
    assert(game_offsets_beta::OCCLUSION_MOD_IS_OCCLUDED == game_offsets::OCCLUSION_MOD_IS_OCCLUDED);
}

void test_marker_occlusion_and_rays() {
    using namespace vis_check;
    Tracker tracker;

    TargetGeometry t1{};
    t1.id = 0x1000;
    t1.event_handler = 0x1100;
    t1.aim_target = 0x1200;
    t1.aim_collider = 0x1300;
    t1.feet[0] = 0.0f; t1.feet[1] = 0.0f; t1.feet[2] = 20.0f;
    t1.head[0] = 0.0f; t1.head[1] = 1.6f; t1.head[2] = 20.0f;
    t1.distance = 20.0f;
    t1.screen_dist_px = 15.0f;
    t1.occlusion_known = true;
    t1.occlusion_visible = false; // Behind wall according to OcclusionModule

    TargetGeometry t2{};
    t2.id = 0x2000;
    t2.event_handler = 0x2100;
    t2.aim_target = 0x2200;
    t2.aim_collider = 0x2300;
    t2.feet[0] = 5.0f; t2.feet[1] = 0.0f; t2.feet[2] = 35.0f;
    t2.head[0] = 5.0f; t2.head[1] = 1.6f; t2.head[2] = 35.0f;
    t2.distance = 35.4f;
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

    // Direct camera AimRaycast hit on t1's PlayerManager overrides occlusion immediately
    RaySample aim_ray{};
    aim_ray.valid = true;
    aim_ray.hit_player = t1.id;
    aim_ray.distance = 20.0f;
    aim_ray.point_valid = true;
    aim_ray.point[0] = 0.0f; aim_ray.point[1] = 1.2f; aim_ray.point[2] = 20.0f;

    targets[0].occlusion_known = false;
    tracker.update_from_rays(empty_ray, aim_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.1);
    assert(tracker.is_visible(targets[0], 1.1));

    // Wall hit at 8m along the same ray occludes t1 at 20m
    RaySample wall_ray{};
    wall_ray.valid = true;
    wall_ray.hit_player = 0;
    wall_ray.hit_entity = 0;
    wall_ray.distance = 8.0f;
    wall_ray.point_valid = true;
    wall_ray.point[0] = 0.0f; wall_ray.point[1] = 1.5f; wall_ray.point[2] = 8.0f;

    tracker.update_from_rays(empty_ray, wall_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.3);
    assert(!tracker.is_visible(targets[0], 1.3));

    // Shipping container wall hit right in front of t1 (0.5m in front of player,
    // hit_player == 0 && hit_entity == 0) must stay occluded (-1), never visible!
    RaySample close_container_ray{};
    close_container_ray.valid = true;
    close_container_ray.hit_player = 0;
    close_container_ray.hit_entity = 0;
    close_container_ray.distance = 19.5f;
    close_container_ray.point_valid = true;
    close_container_ray.point[0] = 0.0f;
    close_container_ray.point[1] = 1.2f;
    close_container_ray.point[2] = 19.5f;
    assert(evaluate_ray_for_target(empty_ray, close_container_ray, true, 300.0f, true, cam_pos, cam_fwd, targets[0]) == -1);

    // Empty GKo when Fox rejects a shipping container must return 0 (inconclusive), never +1!
    assert(evaluate_ray_for_target(empty_ray, empty_ray, true, 300.0f, true, cam_pos, cam_fwd, targets[0]) == 0);

    // 1-frame container seam blip (occlusion_visible=true for 1 frame while occluded)
    // must NOT flash visible; 2 consecutive visible frames transition to visible.
    targets[0].occlusion_known = true;
    targets[0].occlusion_visible = true;
    tracker.update_from_rays(empty_ray, empty_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.316);
    assert(!tracker.is_visible(targets[0], 1.316)); // 1st frame blip suppressed!

    targets[0].occlusion_visible = false;
    tracker.update_from_rays(empty_ray, empty_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.332);
    assert(!tracker.is_visible(targets[0], 1.332)); // back behind container

    // Now genuinely peeks out for 2 consecutive frames -> becomes visible
    targets[0].occlusion_visible = true;
    tracker.update_from_rays(empty_ray, empty_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.348);
    assert(!tracker.is_visible(targets[0], 1.348));
    tracker.update_from_rays(empty_ray, empty_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.364);
    assert(tracker.is_visible(targets[0], 1.364));

    // And the instant he steps back behind the container, 0-frame lag -> occluded immediately
    targets[0].occlusion_visible = false;
    tracker.update_from_rays(empty_ray, empty_ray, true, 300.0f, true, cam_pos, cam_fwd, targets, 1.380);
    assert(!tracker.is_visible(targets[0], 1.380));
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

    // Re-probe and simulate visible hit (Unity sets LastSeenTime = Time.time > 0)
    const ProbeWrite pw2 = tracker.select_next_probe(targets, 2.15);
    assert(pw2.valid);
    AimAssistSnapshot snap_vis{};
    snap_vis.valid = true;
    snap_vis.cur_target = bot.aim_target;
    snap_vis.cur_collider = bot.aim_collider;
    snap_vis.cur_last_seen_time = 154.25f;
    snap_vis.cur_distance = 41.8f;
    assert(tracker.consume_hwr_snapshot(snap_vis, targets, 2.166));
    assert(tracker.is_visible(bot, 2.166));
}

} // namespace

int main() {
    test_lang_table_sorted();
    test_offsets_consistency();
    test_marker_occlusion_and_rays();
    test_hwr_offaxis_probe();
    return 0;
}
