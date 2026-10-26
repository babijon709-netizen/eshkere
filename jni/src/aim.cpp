#include "aim.h"
#include "silent_aim.h"
#include "main.h"                // displayInfo, native_window_screen_*
#include "game.h"
#include "Android_touch/TouchHelperA.h"
#include "aim_learn.h"           // оценка град/px по реакции прицела
#include "app_state.h"
#include "cfg.h"
#include "menu.h"                // g_calibMode
#include "widgets.h"             // g_sheet/g_pop: меню открыто — тач-аим на паузе
#include "process.h"             // g_esp_attached
#include "ui_util.h"
#include "str.h"               // XS(): строки интерфейса зашифрованы в бинарнике
#include "hud.h"               // ShowToast: мемори-режимы сообщают о поломке тостом
#include "esp_draw.h"            // FrameBoxes, AimFovRadiusPx
#include "manes_aim_source.h"
#include "aim_control.h"
#include "armor_esp.h"          // автовыбор кости: шлем на голове или нет
#include "aim_lead_learn.h"     // автокалибровка окна упреждения по попаданиям
#include "aim_trace.h"          // CSV-трейс: метки выстрела и попадания
#include "farm_joy.h"         // farmjoy::attack_held — кнопка атаки у игры
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <thread>
#include <chrono>
#include <time.h>

static constexpr float kAimTouchDefX = 0.72f;
static constexpr float kAimTouchDefY = 0.48f;

float AimTouchFracX() {
    return (g_state.aim_tx > 0.f && g_state.aim_tx < 1.f) ? g_state.aim_tx : kAimTouchDefX;
}
float AimTouchFracY() {
    return (g_state.aim_ty > 0.f && g_state.aim_ty < 1.f) ? g_state.aim_ty : kAimTouchDefY;
}

// ============================ Aimbot ============================
//
// Touch drags a synthetic look finger; memory writes camera angles, and silent
// writes the shot axis. Target eligibility and priority are shared below.

struct AimTarget {
    bool  valid = false;
    unsigned long long id = 0;
    float yaw = 0.f, pitch = 0.f;   // degrees from crosshair (+right, +up)
    float sx = 0.f, sy = 0.f;       // screen position (px)
    float dist = 0.f;               // pixel distance from crosshair
    float world_dist = 0.f;
    // Угловая скорость этой точки вокруг камеры, град/с (см. EspBox.aim_rate_*).
    float rate_yaw = 0.f, rate_pitch = 0.f;
    float world[3] = {};            // точная мировая координата кости для silent-режима
    bool  world_valid = false;
    int   bone = -1;                // слот точки прицела (0 голова, 1 шея, 2 грудь)
    float speed = 0.f;              // горизонтальная скорость цели, м/с (вход автокалибровки лида)
    float health = -1.f;            // здоровье цели (для лога и проверки смерти)
    bool  respawning = false;       // PlayerManager.respawning (для лога)
    // Поза выбранной цели — те же габариты скелета, по которым решается
    // «мёртв». В лог они идут вместе с самой целью: иначе, когда игрок
    // жалуется «аим ведёт по трупу», нечем проверить, лежал ли тот труп
    // или его кости ещё стояли.
    int   skel_bones = 0;
    int   death_flag = -1;          // собственный флаг смерти игры
    float skel_up = -1.f, skel_flat = -1.f;
};

static void AimReleaseFinger(bool& fingerDown) {
    if (fingerDown) {
        Touch_Up();
        fingerDown = false;
    }
}

// File-scope so the auto-farm can yield the camera while the aimbot is
// actively pulling onto a player.
bool s_fingerDown = false;
// True while an aim mode owns camera or shot-axis control.
bool g_aimActive = false;
std::atomic<unsigned long long> g_aim_pulse_target{0};
std::atomic<double> g_aim_pulse_at{0.0};
std::atomic<double> g_aim_hold_at{0.0};

// Запасной град/px — из чувствительности, выставленной в настройках клиента.
//
// Игра считает поворот как «накопленный сдвиг касания × m_Sensitivity»
// (MouseLook.ZJo, поле +0x34), поэтому град/px строго пропорционален
// чувствительности, и коэффициент обязан ехать за настройкой игрока.
//
// 0.10 град/px — ИЗМЕРЕННОЕ на устройстве значение (EV «камера: коэффициент …»
// в логе автофарма 14.09.2026: поворот/сдвиг даёт 0.05..0.18, медиана 0.10,
// p10 0.078, p90 0.178). Эталон, к которому оно привязано, — та же настройка
// игрока, при которой шёл замер: 2.00 (поле m_Sensitivity, прочитанное из игры;
// строка аима «чувствительность 2.00» в my_benzware.log 16.09.2026 21:35).
//
// Раньше здесь стояло 5.0 (заводское значение из MouseLook..ctor) — и для игрока
// с настройкой 2.0 запасной коэффициент выходил 0.04 при измеренных 0.10, то
// есть шаг пальца считался в 2.5 раза больше нужного. Петля «палец -> камера ->
// ошибка» при этом расходится (L = k * 0.10 / 0.04 = 1.25 > 1 при k <= 0.5),
// и прицел трясёт: ровно то, на что жаловался игрок. Проверено на стенде
// (tools/aim/sim_loop.cpp, модель петли): с рабочим 0.04 при камере 0.10 захват
// «до 1 град» не наступает вообще, перелёт 5.5..9.8 град, СКЗ хвоста 4.5..6.1;
// с рабочим 0.10 — захват за 1.30 с и хвост 0.00.
//
// Страховка от такой ошибки в эталоне — оценка коэффициента по реакции прицела
// (aim_learn.h): она сходится к истинному значению за ~секунду и работает на
// любом устройстве, даже если настройку в игре поменяли на ходу. Запасное
// значение нужно только на первый такт, пока оценка не готова.
static constexpr float kAimRefSensitivity = 2.0f;  // настройка игрока при замере 0.10 град/px

// Полоса правдоподобия коэффициента (в единицах эталона): ниже 0.04 град/px
// замеров нет, выше 0.25 — тоже (это уже чувствительность 5 и больше).
static constexpr float kAimScaleMin = 0.4f;
static constexpr float kAimScaleMax = 2.5f;

// Во сколько раз настройка игрока отличается от эталонной (2.00 при замере).
// Ровно во столько же раз игра меняет град/px: 2.0 -> 0.10, 5.0 -> 0.25,
// 1.0 -> 0.05. Полоса правдоподобия едет вместе с настройкой — она отмерена при
// 2.00, и зажимать ею значение при чувствительности 5 — это прицел, который
// втрое медленнее, чем позволяет игра.
//
// Значение ДЕРЖИМ и обновляем редко. Чтение может не пройти в отдельном кадре
// (нет локального игрока, мигнул список игроков), а шаг пальца считается как
// err/коэффициент: если значение скачет между прочитанным и запасным, шаг
// меняется в разы от кадра к кадру — это и видно как «аим сильно дергает».
// Поэтому: принятое значение живёт до заметного изменения (25% и больше, то
// есть ручная правка настройки в меню игры) и читать чаще 1.5 с не пробуем.
//
// from_game = true — коэффициент выведен из настройки игры (значит, годится как
// «известный»: по нему видно квант ввода), false — запасное измеренное 0.10.
//
// Монотонные секунды. Нужны ровно для одного: «принятое значение
// чувствительности держим 1.5 с, чаще не читаем».
double MonoNow() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

float AimSensitivityScale(bool& from_game) {
    constexpr double kHoldSeconds = 1.5;    // чаще настройку руками не меняют
    constexpr float  kAdoptRatio  = 1.25f;  // мельче этого — не изменение

    static float  held           = 1.0f;
    static bool   held_from_game = false;
    static double held_at        = 0.0;

    const double now = MonoNow();
    if (held_at > 0.0 && (now - held_at) < kHoldSeconds) {
        from_game = held_from_game;
        return held;
    }
    held_at = now;

    float sensitivity = 0.f;
    if (esp_read_look_sensitivity(sensitivity)) {
        float scale = sensitivity / kAimRefSensitivity;
        if (!std::isfinite(scale) || scale <= 0.f) scale = 1.f;
        if (scale < kAimScaleMin) scale = kAimScaleMin;
        if (scale > kAimScaleMax) scale = kAimScaleMax;
        const bool changed = !(scale < held * kAdoptRatio && scale > held / kAdoptRatio);
        if (!held_from_game || changed) { held = scale; held_from_game = true; }
    } else if (!held_from_game) {
        held = 1.0f;
    }
    // Прочитанное однажды значение переживает осечку чтения: оно верное, а
    // подмена его запасным — это и есть скачок шага.
    from_game = held_from_game;
    return held;
}

float AimSensitivityGain(bool& from_game) {
    return kAimGainAtRef * AimSensitivityScale(from_game);
}

// ============================ Общее для всех режимов ========================
//
// Touch steers with screen-space drags after sharing target selection with
// memory. game.cpp projects lead into aim points for both modes.

// Each mode keeps independent target/filter state.
struct AimPick {
    unsigned long long lastId = 0;
    int lastBone = -1;
    aim_control::AngleFilter angleFilter{};
    bool filterUsesCamera = false;
    bool filterModeSet = false;
    bool switched = false;

    void reset() {
        lastId = 0;
        lastBone = -1;
        switched = false;
        filterUsesCamera = false;
        filterModeSet = false;
        angleFilter.reset();
    }
};

struct RankedAimTarget {
    AimTarget target;
    float aim_error = 0.f;
    bool range_valid = false;
    bool sticky = false;
};

static AimPick s_touchPick;

// ADS state is debounced: two agreeing reads change it, one bad frame does not.
static bool s_adsDebounced = false;
static int s_adsOnStreak = 0, s_adsOffStreak = 0;

// ---- «Только при стрельбе» --------------------------------------------------
// Аим ведёт камеру, пока игрок держит кнопку огня: прицел не уезжает на цель
// заранее и не досаживает после того, как огонь отпущен.
//
// Первый признак — защёлка атаки у хендлера ввода (PlayerInputHandler+0x68,
// farmjoy::attack_held): она единица ровно пока кнопка нажата, и ставит её сама
// игра — и палец на экране, и мышь. Наведение начинается первым же кадром
// нажатия, а не тогда, когда пуля кого-то задела.
//
// Второй признак — отметка попадания игры (PlayerEventHandler.LastHitTime,
// esp_mem_aim_last_shot). КАК ПЕРВЫЙ ПРИЗНАК ОНА НЕГОДНА: пишется при
// попадании, поэтому аим стартует с задержкой на полёт пули (на это и жаловались).
// Здесь она только продлевает окно на kFireHoldSeconds: полуавтомат за один тап
// успевает отпустить кнопку, и без хвоста аим гасил бы наведение между выстрелами.
//
// Окно держится kFireHoldSeconds — столько, чтобы аим не бросал цель между
// патронами очереди (при темпе 0.1 с наш кадр чтения легко попадает в паузу).
// Больше 0.5 с ставить нельзя: аим начнёт дотягивать прицел уже после того, как
// игрок кончил стрелять.
//
// Автофарм — исключение: кнопку на 0x68 в это время держит он, и прицелом на
// фарме рулит сам фарм. Два писателя в одну ось дают маятник, поэтому на фарме
// режим «только при стрельбе» аимбот НЕ ведёт (счётчик остаётся -1: это не наш
// случай, а не «не удалось прочитать»).
//
// Читается раз на кадр в UpdateAim и для всех трёх режимов сразу: у тач-аима
// свой вход (он не проходит AimBegin), а состояние общее.
static constexpr double kFireHoldSeconds = 0.30;
static double s_fireUntil = -1e9;
static float  s_hitMark = 0.f;
static bool   s_hitMarkSeen = false;
static bool   s_fireGateAllow = true;
// Огонь и отметка попадания читаются РАЗ на кадр и обслуживают две вещи:
// гейт «только при стрельбе» и автокалибровку окна упреждения. Второй
// независимый читатель стоил бы лишних syscall'ов в кадре.
static bool   s_attackHeldLast = false;
static float  s_lastTargetSpeed = 0.f;   // горизонтальная скорость цели, м/с

static void AimFireGateUpdate(double now) {
    bool held = false;
    const bool heldNow = farmjoy::attack_held(held) && held;
    const bool heldEdge = heldNow && !s_attackHeldLast;
    s_attackHeldLast = heldNow;
    float mark = 0.f;
    bool markChanged = false;
    if (esp_mem_aim_last_shot(mark)) {
        if (!s_hitMarkSeen) {
            // Первая удачная читка только задаёт точку отсчёта: «значение
            // изменилось» без предыдущего значения ничего не значит.
            s_hitMarkSeen = true;
            s_hitMark = mark;
        } else if (mark != s_hitMark) {
            s_hitMark = mark;
            markChanged = true;
        }
    }
    // Автокалибровка: выстрел ушёл под нашим прицелом и игра подтвердила
    // попадание — это и есть размеченный пример, на котором учится масштаб
    // окна упреждения (aim_lead_learn.h). На автофарме не учимся: там огнём
    // командует фарм, и выстрел ничего не говорит о наведении.
    if (markChanged) { aimlead::hit(now); aimtrace::hit(now); }
    if (heldEdge) aimtrace::shot(now);
    if (heldEdge && g_aimActive && !g_state.farm_on)
        aimlead::shot(now, s_lastTargetSpeed);
    aimlead::tick(now);

    if (!g_state.aim_fire_only) {
        s_fireGateAllow = true;
        s_fireUntil = -1e9;
        return;
    }
    if (g_state.farm_on) {
        s_fireGateAllow = false;
        return;
    }
    // Прочитали — окно открывает само нажатие. Не прочитали (хендлёр ввода не
    // нашёлся) — остаётся только хвост от отметки попадания: ведём то, что
    // уже началось, и не стартуем вслепую.
    if (heldNow) s_fireUntil = now + kFireHoldSeconds;
    if (markChanged && now + kFireHoldSeconds > s_fireUntil)
        s_fireUntil = now + kFireHoldSeconds;
    s_fireGateAllow = now <= s_fireUntil;
}

static bool AimScreenSize(float& sw, float& sh) {
    sw = (float)native_window_screen_x;
    sh = (float)native_window_screen_y;
    if (displayInfo.width > displayInfo.height && displayInfo.width >= 100 && displayInfo.height >= 100) {
        sw = (float)displayInfo.width;  sh = (float)displayInfo.height;
    } else if (displayInfo.height > displayInfo.width && displayInfo.height >= 100 && displayInfo.width >= 100) {
        sw = (float)displayInfo.height;  sh = (float)displayInfo.width;
    }
    return sw >= 100.f && sh >= 100.f;
}

static bool AimBegin(float& dt, float& sw, float& sh) {
    g_aimActive = false;
    // Calibration owns the physical touch used to select a screen point.
    // Freecam owns the camera and cannot share its look controller with aim.
    bool active = g_state.aim_touch && g_esp_attached &&
                  g_calibMode == 0 && !g_state.freecam_on;

    // "Только с прицелом": only steer while the local player is ADS.
    if (g_state.aim_scope_only && g_esp_attached) {
        const bool rawAds = esp_local_player_is_aiming();
        if (rawAds) {
            if (s_adsOnStreak < 2) ++s_adsOnStreak;
            s_adsOffStreak = 0;
            if (s_adsOnStreak >= 2) s_adsDebounced = true;
        } else {
            if (s_adsOffStreak < 2) ++s_adsOffStreak;
            s_adsOnStreak = 0;
            if (s_adsOffStreak >= 2) s_adsDebounced = false;
        }
        if (active && !s_adsDebounced) active = false;
    } else {
        s_adsDebounced = false;
        s_adsOnStreak = s_adsOffStreak = 0;
    }
    // «Только при стрельбе»: состояние посчитано в UpdateAim (оно общее для
    // всех трёх режимов); без выстрелов аим не ведёт.
    if (active && !s_fireGateAllow) active = false;
    if (!active) return false;

    if (dt <= 0.f || !std::isfinite(dt)) dt = 1.f / 60.f;
    if (dt > 0.1f) dt = 0.1f;

    if (!AimScreenSize(sw, sh)) return false;
    return true;
}

// Select a target for touch, memory, and silent modes; memory additionally filters its angles.
static bool AimSelectTarget(float sw, float sh, float dt, AimPick& pick, AimTarget& best) {
    const std::vector<EspBox>& boxes = FrameBoxes(sw, sh);

    const float crossX = sw * 0.5f, crossY = sh * 0.5f;
    const float fovR = AimFovRadiusPx(sw, sh);
    // Read the current menu/config selection on every target-selection pass.
    // Slots are [0] head, [1] neck, and [2] chest.
    int wantBone = g_state.aim_bone;
    if (wantBone < 0 || wantBone > 3) wantBone = 1;
    constexpr int kAutoBone = 3;

    // ---- choose target ----
    static std::vector<RankedAimTarget> candidates;
    candidates.clear();
    if (candidates.capacity() < boxes.size()) candidates.reserve(boxes.size());
    bool haveNearestRange = false;
    float nearestRange = 0.f;
    const bool cameraAim = g_state.aim_mode == AIM_MODE_MEMORY ||
                           g_state.aim_mode == AIM_MODE_TOUCH;
    const float fovRadiusDeg = manes_aim_source::fov_radius_degrees(cfg::aim::fov);
    for (const EspBox& b : boxes) {
        // На союзников аим не работает НИКОГДА, а не только пока в визуалах
        // включён их показ: проверка «g_state.esp_team && b.ally» означала,
        // что со скрытыми союзниками (показ выключен) аим работал по ним
        // как по врагам (вопрос 19.09). Признак своего — из групп/кланов
        // (game.cpp, groups_are_allied), он считается независимо от визуалов.
        // Боты — цели, только пока включено их ЕСП: выключил «Боты» —
        // аим их не трогает. Действует в обоих режимах (выбор цели общий).
        if (b.is_bot && !g_state.esp_bots) continue;
        if (b.ally) continue;
        // Мёртвых не берём: труп ещё несколько секунд остаётся в списке
        // игроков с корректным скелетом, и прицел уезжал на него (19.09).
        if (b.dead) continue;
        if (cfg::aim::vis_check && !b.visible) continue;
        AimTarget t;
        // Silent auto-zone: head/neck hitboxes are a few cm; on a runner or at range
        // any position error misses the body. Use the chest (largest zone) there.
        constexpr float kSilentZoneSpeed = 2.0f;   // m/s
        constexpr float kSilentZoneRange = 60.0f;  // m
        // Авто: голова открыта — ведём в неё, шлем надет — в корпус. Броня
        // читается тем же обходом рига, что и в ESP (armor_esp.h), поэтому
        // выбор стоит на реальном шлеме, а не на догадке по модели.
        int baseBone = wantBone;
        if (baseBone == kAutoBone)
            baseBone = armoresp::valid_icon(b.armor_icons[armoresp::Head]) ? 2 : 0;
        int candidateBone = baseBone;
        if (g_state.aim_mode == AIM_MODE_SILENT && g_state.silent_auto_zone && baseBone < 2 &&
            (b.aim_speed > kSilentZoneSpeed || b.distance > kSilentZoneRange))
            candidateBone = 2;
        int usedBone = aim_control::configured_bone_slot(candidateBone, b.aim_valid);
        if (usedBone < 0) {
            for (int fb = 0; fb < 3; ++fb) {
                if (b.aim_valid[fb]) { usedBone = fb; break; }
            }
        }
        if (usedBone >= 0) {
            t.bone = usedBone;
            t.yaw = b.aim_yaw[usedBone];  t.pitch = b.aim_pitch[usedBone];
            t.sx  = b.aim_pts[usedBone][0]; t.sy = b.aim_pts[usedBone][1];
            t.valid = std::isfinite(t.yaw) && std::isfinite(t.pitch) &&
                      std::isfinite(t.sx) && std::isfinite(t.sy);
            t.world_valid = b.aim_world_valid[usedBone];
            for (int i = 0; i < 3; ++i) {
                t.world[i] = b.aim_world[usedBone][i];
                t.world_valid = t.world_valid && std::isfinite(t.world[i]);
            }
        } else {
            continue;
        }
        if (!t.valid) continue;
        t.id = b.id;
        t.speed = b.aim_speed;
        t.health = b.health; t.respawning = b.respawning;
        t.rate_yaw = b.aim_rate_yaw[usedBone];
        t.rate_pitch = b.aim_rate_pitch[usedBone];
        t.skel_bones = b.skel_bones; t.skel_up = b.skel_up; t.skel_flat = b.skel_flat;
        t.death_flag = b.death_flag;
        const bool sticky = (pick.lastId != 0 && b.id == pick.lastId);
        if (t.sx < -sw || t.sx > sw * 2.f || t.sy < -sh || t.sy > sh * 2.f) continue;
        if (g_state.aim_pos && !sticky && (t.sx < 0.f || t.sy < 0.f || t.sx > sw || t.sy > sh)) continue;
        float dx = t.sx - crossX, dy = t.sy - crossY;
        t.dist = sqrtf(dx * dx + dy * dy);
        t.world_dist = b.distance;
        const float angularDistance = cameraAim
            ? manes_aim_source::angular_distance_degrees(t.yaw, t.pitch)
            : 0.f;
        const bool inScreenFov = t.dist <= (sticky ? fovR * 2.f : fovR);
        const bool inFov = cameraAim
            ? (inScreenFov || manes_aim_source::inside_angular_fov(
                  t.yaw, t.pitch, cfg::aim::fov, sticky ? fovRadiusDeg : 0.f))
            : inScreenFov;
        if (!inFov) continue;

        const float pixelTerm = t.dist / (fovR > 1.f ? fovR : 1.f);
        const float angleTerm = angularDistance /
            (fovRadiusDeg > 1.f ? fovRadiusDeg : 1.f);
        const float aimError = cameraAim ? angleTerm : pixelTerm;
        const bool haveRange = t.world_dist > 0.1f && std::isfinite(t.world_dist);
        candidates.push_back({t, aimError, haveRange, sticky});
        if (haveRange && (!haveNearestRange || t.world_dist < nearestRange)) {
            nearestRange = t.world_dist;
            haveNearestRange = true;
        }
    }

    bool haveBest = false;
    float bestScore = 1e18f;
    for (const RankedAimTarget& candidate : candidates) {
        if (haveNearestRange && !candidate.range_valid) continue;
        if (haveNearestRange && g_state.aim_priority == 2 && !candidate.sticky &&
            !aim_control::target_within_range_window(
                candidate.target.world_dist, nearestRange)) continue;
        const float score = aim_control::target_priority_score(
            g_state.aim_priority, candidate.aim_error,
            candidate.target.world_dist, candidate.range_valid, candidate.sticky);
        if (!haveBest || score < bestScore) {
            bestScore = score;
            best = candidate.target;
            haveBest = true;
        }
    }

    if (!haveBest) return false;

    const bool targetChanged = best.id != pick.lastId;
    const bool selectionChanged = targetChanged || best.bone != pick.lastBone;
    if (selectionChanged) pick.angleFilter.reset();
    if (g_state.aim_mode == AIM_MODE_MEMORY) {
        float filteredYaw = best.yaw;
        float filteredPitch = best.pitch;
        float cameraYaw = 0.0f, cameraPitch = 0.0f;
        const bool haveCamera = esp_camera_angles(cameraYaw, cameraPitch) &&
                                std::isfinite(cameraYaw) && std::isfinite(cameraPitch);
        if (!pick.filterModeSet || pick.filterUsesCamera != haveCamera) {
            pick.angleFilter.reset();
            pick.filterUsesCamera = haveCamera;
            pick.filterModeSet = true;
        }
        float timeConstant = 0.010f;
        if (best.world_dist > 0.0f && best.world_dist < 12.0f) timeConstant *= 0.75f;
        if (haveCamera) {
            filteredYaw = WrapDeg180(cameraYaw + filteredYaw);
            filteredPitch += cameraPitch;
            pick.angleFilter.update(filteredYaw, filteredPitch, dt, timeConstant);
            best.yaw = WrapDeg180(filteredYaw - cameraYaw);
            best.pitch = filteredPitch - cameraPitch;
        } else {
            pick.angleFilter.update(filteredYaw, filteredPitch, dt, timeConstant);
            best.yaw = filteredYaw;
            best.pitch = filteredPitch;
        }
    }
    // Цель взята — с этого такта камера (или ось выстрела) наша: автофарм ей
    // больше не командует.
    g_aimActive = true;
    // Состояние выбранной цели — чтобы по логу проверить признак смерти:
    // игрок жаловался, что аим ведёт по мёртвым, а по respawning (0x208) в
    // логе 14:55 не сработало ни разу. Печатаем здоровье и оба флага.
    pick.switched = selectionChanged;
    const double now = MonoNow();
    if (targetChanged) {
        g_aim_pulse_target.store(best.id, std::memory_order_relaxed);
        g_aim_pulse_at.store(now, std::memory_order_relaxed);
    }
    g_aim_hold_at.store(now, std::memory_order_relaxed);
    // Скорость цели на этот такт — вход для автокалибровки лида: по стоящей
    // цели выстрел ничего не говорит о масштабе окна упреждения.
    s_lastTargetSpeed = best.speed;
    pick.lastId = best.id; pick.lastBone = best.bone;
    return true;
}

// ============================ Тач-аим ======================================
static void UpdateAimTouch(float dt) {
    static float s_fx = 0.f, s_fy = 0.f;

    const bool menuOpen = g_sheet.visible || (g_pop.visible && !g_pop.closing);
    bool active = g_state.aim_touch && g_esp_attached && !menuOpen &&
                  g_calibMode == 0 && !g_state.freecam_on;
    if (active && g_state.aim_scope_only && !esp_local_player_is_aiming())
        active = false;
    if (active && !s_fireGateAllow)   // «Только при стрельбе» (см. AimFireGateUpdate)
        active = false;

    if (dt <= 0.f || !std::isfinite(dt)) dt = 1.f / 60.f;
    if (dt > 0.1f) dt = 0.1f;

    if (!active) {
        AimReleaseFinger(s_fingerDown);
        g_aimActive = false;
        s_touchPick.reset();
        return;
    }

    float sw = (float)native_window_screen_x;
    float sh = (float)native_window_screen_y;
    if (displayInfo.width > displayInfo.height && displayInfo.width >= 100 && displayInfo.height >= 100) {
        sw = (float)displayInfo.width;
        sh = (float)displayInfo.height;
    } else if (displayInfo.height > displayInfo.width && displayInfo.height >= 100 && displayInfo.width >= 100) {
        sw = (float)displayInfo.height;
        sh = (float)displayInfo.width;
    }
    if (sw < 100.f || sh < 100.f) {
        AimReleaseFinger(s_fingerDown);
        g_aimActive = false;
        s_touchPick.reset();
        return;
    }

    const float ptX = AimTouchFracX() * sw;
    const float ptY = AimTouchFracY() * sh;
    AimTarget best;
    if (!AimSelectTarget(sw, sh, dt, s_touchPick, best)) {
        g_aimActive = false;
        s_touchPick.reset();
        AimReleaseFinger(s_fingerDown);
        return;
    }

    const float crossX = sw * 0.5f, crossY = sh * 0.5f;
    const float ex = best.sx - crossX, ey = best.sy - crossY;
    // Палец нужен и когда прицел уже стоит на цели, но цель едет: скорость
    // точки тянет камеру feed-forward-ом, и без касания петля снова отстаёт,
    // пока ошибка не вырастет до порога. Отпускаем только стоящую цель.
    const bool point_moving = fabsf(best.rate_yaw) > 1.0f || fabsf(best.rate_pitch) > 1.0f;
    if (!s_fingerDown) {
        if (!point_moving && fabsf(ex) < 1.2f && fabsf(ey) < 1.2f) return;
        s_fx = ptX;
        s_fy = ptY;
        Touch_Down(s_fx, s_fy);
        s_fingerDown = true;
    }

    float sm = g_state.gun_str;
    if (!(sm >= 1.f)) sm = 1.f;
    if (sm > 10.f) sm = 10.f;
    const float rate = 6.f + (sm - 1.f) * (22.f / 9.f);
    float kx = 1.f - expf(-rate * dt);
    if (kx > 0.65f) kx = 0.65f;
    // Vertical axis gets kicked every shot by weapon recoil; keep vertical loop
    // gain damped below the 1-frame-delay oscillation threshold so firing never
    // jerks the camera up and down.
    float ky = kx * 0.65f;
    if (ky > 0.42f) ky = 0.42f;

    // Скорость точки в пикселях пальца: град/с * dt / (град/px). Коэффициент
    // тот же, что у запасного град/px (0.10 при эталонной чувствительности) —
    // иначе feed-forward ехал бы в других единицах, чем остальная петля.
    bool gain_from_game = false;
    const float deg_per_px = AimSensitivityGain(gain_from_game);
    float ffx = 0.f, ffy = 0.f;
    if (deg_per_px > 1e-4f) {
        ffx = best.rate_yaw * dt / deg_per_px;
        ffy = -best.rate_pitch * dt / deg_per_px;   // палец вниз = камера вниз
    }
    float maxStep = (2500.f + (sm - 1.f) * 500.f) * dt;
    if (maxStep > 150.f) maxStep = 150.f;
    float dx = ((fabsf(ex) < 0.8f) ? 0.f : (ex * kx)) + ffx;
    float dy = ((fabsf(ey) < 1.2f) ? 0.f : (ey * ky)) + ffy;
    if (dx > maxStep) dx = maxStep;
    if (dx < -maxStep) dx = -maxStep;
    if (dy > maxStep) dy = maxStep;
    if (dy < -maxStep) dy = -maxStep;

    // Shift the archived touch zone with the configured base point.
    const float xOffset = ptX - 0.72f * sw;
    const float yOffset = ptY - 0.48f * sh;
    float xMin = 0.55f * sw + xOffset, xMax = 0.95f * sw + xOffset;
    float yMin = 0.20f * sh + yOffset, yMax = 0.80f * sh + yOffset;
    xMin = std::max(xMin, 0.02f * sw);
    xMax = std::min(xMax, 0.98f * sw);
    yMin = std::max(yMin, 0.02f * sh);
    yMax = std::min(yMax, 0.98f * sh);

    s_fx += dx;
    s_fy += dy;
    if (s_fx < xMin || s_fx > xMax || s_fy < yMin || s_fy > yMax) {
        AimReleaseFinger(s_fingerDown);
        return;
    }
    Touch_Move(s_fx, s_fy);
}


// ====================== Мемори-аим: «память» ===============================
//
// Memory-aim работает напрямую через накопитель MouseLook. Новый шаг всегда
// направлен к выбранной кости, но ограничен и уменьшается вместе с остатком,
// чтобы лок не дёргался у цели.
//
// Знаки накопителя (замер в логах): [0] rotationX = минус тангаж (вверх —
// меньше), [1] rotationY = рысканье (вправо — больше).

// Плавный жёсткий лок: контроллер держит выбранную кость, но не кладёт в
// MouseLook полный шум одного снимка рига. Мёртвая зона меньше одного пикселя
// на обычном FOV; за ней шаг плавно ограничен скоростью. Поэтому лок остаётся
// на кости, а микроколебания анимации/чтения не превращаются в дёрганье камеры.
// ACK по накопителю: расхождение с нашей записью, в пределах которого игра
// приняла значение. Это только диагностика отклика в меню.
static constexpr float kMemAckEps = 0.10f;

// Convert remaining error into a smooth lock step. Use a small deadband near
// the bone; farther away, acquisition speed depends on sensitivity and range.
static float mem_axis_step(float err, float dt, float range) {
    return aim_control::memory_step(err, dt, g_state.gun_str, range);
}

static void UpdateAimMemory(float dt) {
    static AimPick pick;
    static int s_missFrames = 0;
    // Последняя запись в накопитель (в тех же завёрнутых координатах, что
    // вернёт чтение): по совпадению видно, приняла ли игра запись.
    static float s_lastWriteX = 0.f, s_lastWriteY = 0.f;
    static bool  s_haveLastWrite = false;
    static float s_baseAccX = 0.f, s_baseAccY = 0.f;
    static float s_baseErrY = 0.f, s_baseErrP = 0.f;
    static float s_unconsumedT = 0.f;
    static float s_trimY = 0.f, s_trimX = 0.f;
    static bool  s_haveTrim = false;
    float sw = 0.f, sh = 0.f;
    if (!AimBegin(dt, sw, sh)) {
        pick.reset(); s_missFrames = 0; s_haveLastWrite = false;
        s_unconsumedT = 0.f; s_haveTrim = false;
        return;
    }

    // MouseLook нужен не ради чувствительности (накопитель уже в градусах), а
    // как проверка «объект тот»: m_Sensitivity в правдоподобных пределах —
    // значит это действительно MouseLook локального игрока.
    float sens = 0.f; bool invert = false, gyro = false;
    const bool haveParams = esp_mem_aim_look_params(sens, invert, gyro);
    if (!haveParams) {
        if (++s_missFrames > 6) { pick.reset(); s_haveTrim = false; }
        s_haveLastWrite = false;
        s_unconsumedT = 0.f;
        return;
    }

    // Текущий накопитель углов камеры. Запасная запись (без базы углов)
    // считается от него; абсолютной базе он не нужен, но прочитать его всё
    // равно необходимо: это самая дешёвая проверка, что объект жив.
    float accX = 0.f, accY = 0.f;
    if (!esp_mem_aim_read_angles(accX, accY)) {
        // Накопитель не читается: цель может быть, писать некуда.
        if (++s_missFrames > 6) { pick.reset(); s_haveTrim = false; }
        s_haveLastWrite = false;
        s_unconsumedT = 0.f;
        return;
    }

    AimTarget best;
    if (!AimSelectTarget(sw, sh, dt, pick, best)) {
        if (++s_missFrames > 6) { pick.reset(); s_haveTrim = false; }
        s_haveLastWrite = false;
        s_unconsumedT = 0.f;
        return;
    }
    s_missFrames = 0;
    if (pick.switched) {
        // Другая цель: подтверждение прошлой записи и дрейф по прежней кости
        // к ней не относятся.
        s_haveLastWrite = false;
        s_unconsumedT = 0.f;
    }

    // ---- ACK по накопителю --------------------------------------------------
    bool matchedLastWrite = false;
    if (s_haveLastWrite) {
        matchedLastWrite =
            fabsf(WrapDeg180(accY - s_lastWriteY)) < kMemAckEps &&
            fabsf(WrapDeg180(accX - s_lastWriteX)) < kMemAckEps;
    }

    // ---- камера: расхождение и отклик ---------------------------------------
    // ---- плавный жёсткий лок --------------------------------------------------
    // Команда = собственная скорость точки + поправка по ошибке. Из одной
    // ошибки выходит постоянное отставание: ровный стрей даёт остаток v/k, где
    // k — гейн петли, и на 20 м это сантиметры тридцать. Со слагаемым скорости
    // остаток стремится к нулю: камера уже едет со скоростью цели, а петля
    // доделывает только то, что не угадала.
    const float offYaw   = mem_axis_step(best.yaw, dt, best.world_dist) +
                           best.rate_yaw * dt;
    const float offPitch = mem_axis_step(best.pitch, dt, best.world_dist) +
                           best.rate_pitch * dt;
    if (offYaw == 0.f && offPitch == 0.f) {
        s_haveLastWrite = false;   // подтверждения больше нет — записи нет
        s_unconsumedT = 0.f;
        return;
    }

    // Запись от абсолютных углов камеры вместо слепого прибавления к накопителю:
    // так исключается накопление ошибки и вертикальная раскачка при отдаче,
    // когда оверлей крутится быстрее игры или отдача дёргает MouseLook между
    // кадрами матрицы вида.
    float cameraYaw = 0.0f, cameraPitch = 0.0f;
    const bool haveCameraReference = esp_camera_angles(cameraYaw, cameraPitch) &&
                                     std::isfinite(cameraYaw) && std::isfinite(cameraPitch);
    if (haveCameraReference) {
        if (!s_haveTrim || !s_haveLastWrite || !matchedLastWrite) {
            const float dy = WrapDeg180(accY - cameraYaw);
            const float dx = WrapDeg180(accX + cameraPitch);
            if (fabsf(dy) > 8.0f || fabsf(dx) > 8.0f) {
                s_trimY = dy;
                s_trimX = dx;
            } else {
                s_trimY = 0.f;
                s_trimX = 0.f;
            }
            s_haveTrim = true;
        }
        cameraYaw = WrapDeg180(cameraYaw + s_trimY);
        cameraPitch = WrapDeg180(cameraPitch - s_trimX);
    } else {
        const bool errMoved = fabsf(best.yaw - s_baseErrY) > 0.25f ||
                              fabsf(best.pitch - s_baseErrP) > 0.25f;
        if (!s_haveLastWrite || !matchedLastWrite || errMoved || s_unconsumedT >= 0.035f) {
            s_baseAccX = accX;
            s_baseAccY = accY;
            s_baseErrY = best.yaw;
            s_baseErrP = best.pitch;
            s_unconsumedT = 0.f;
        } else {
            s_unconsumedT += dt;
        }
    }

    float writeY = 0.0f, writeX = 0.0f;
    aim_control::memory_command_angles(haveCameraReference ? accY : s_baseAccY,
                                       haveCameraReference ? accX : s_baseAccX,
                                       haveCameraReference,
                                       cameraYaw, cameraPitch, offYaw, offPitch,
                                       writeY, writeX);

    // Градусы -> накопитель игры: X — «наклон вниз» (вверх — меньше),
    // Y — рысканье (вправо — больше).
    if (!esp_mem_aim_write_angles(writeX, writeY)) {
        return;
    }
    // Запомнить записанное — в обёрнутом виде: чтение возвращает обёрнутые
    // углы, и сравнение на следующем такте обязано быть в тех же координатах.
    s_lastWriteX = writeX; s_lastWriteY = writeY;
    s_haveLastWrite = true;
}


// ============================ Сайлент-аим ====================================
static void UpdateAimSilent(float dt) {
    static AimPick pick;
    float sw = 0.f, sh = 0.f;
    if (!AimBegin(dt, sw, sh)) {
        pick.reset();
        silent_aim::clear();
        return;
    }

    AimTarget best;
    if (!AimSelectTarget(sw, sh, dt, pick, best) || !best.world_valid) {
        g_aimActive = false;
        pick.reset();
        silent_aim::clear();
        return;
    }

    // Writer получает мировую точку упреждения/баллистического решения;
    // экранные поправки touch/memory-режимов не меняют направление выстрела.
    if (!esp_silent_publish(best.id, best.world)) {
        g_aimActive = false;
        silent_aim::clear();
    }
}

// Сюда управление приходит после меню и фрикама. Если в середине кадра режим
// сменился, новый кадр не оставит writer с последним направлением до lease timeout.
void AimEndFrame() {
    const bool keep = g_state.aim_touch && g_state.aim_mode == AIM_MODE_SILENT &&
                      g_aimActive && !g_state.freecam_on;
    if (!keep) silent_aim::clear();
}

// Диспетчер: какой режим ведёт камеру в этом кадре.
void UpdateAim(float dt) {
    const int mode = g_state.aim_mode;
    // Гейт «только при стрельбе» считается ДО диспетчера и один раз на кадр:
    // он общий для всех трёх режимов, а после смены режима меню могло успеть
    // снять тумблер — тогда в этом же кадре аим уже не ведёт.
    AimFireGateUpdate(MonoNow());
    static int previousMode = -1;
    if (mode != previousMode) {
        if (previousMode == AIM_MODE_TOUCH) {
            AimReleaseFinger(s_fingerDown);
            g_aimActive = false;
        }
        s_touchPick.reset();
        previousMode = mode;
    }
    if (mode == AIM_MODE_MEMORY) UpdateAimMemory(dt);
    else if (mode == AIM_MODE_SILENT) UpdateAimSilent(dt);
    else                              UpdateAimTouch(dt);
}

// ============================ Автофарм =============================
//
// Синтетические тачи: два пальца (0 — джойстик движения, 2 — удар). Камера
// ведётся через накопитель MouseLook, тем же путём, что мемори-аим: абсолютная
// запись угла, без пальца и без коэффициента град/px. Слой памяти (game.cpp)
// отдаёт точку прицела в градусах от оси камеры; здесь — только автоматы.
//