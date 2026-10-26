#include "menu.h"
#include "main.h"
#include "Android_touch/TouchHelperA.h"   // Touch_GetActive/BlockRect (фрикам)
#include "game.h"
#include "game_offsets_active.h"
#include "app_state.h"
#include "cfg.h"
#include "theme.h"
#include "ui_util.h"
#include "widgets.h"
#include "config.h"
#include "hud.h"
#include "audio.h"
#include "str.h"
#include "esp_draw.h"
#include "aim.h"
#include "aim_lead_learn.h"     // автокалибровка лида: тумблер и состояние
#include "aim_trace.h"          // запись CSV-трейса предикта
#include "media/tab_icons.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image/stb_image.h"
#include <cfloat>
#include <cmath>

static GLuint g_tabIcons[kTabCount] = {};

static GLuint LoadTexFromMemory(const unsigned char* data, int len) {
    int w, h, ch;
    unsigned char* px = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
    if (!px) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return tex;
}

void LoadTabIcons() {
    // Lucide, чёрный контур с белой обводкой: цвет зашит в пиксели, тинт не нужен.
    // Индексы: 1 Аим, 2 ESP, 3 Разное, 4 Конфиги, 5 Опции (0 «Меню» не показывается).
    g_tabIcons[1] = LoadTexFromMemory(tab_aim_png,  (int)tab_aim_png_len);
    g_tabIcons[2] = LoadTexFromMemory(tab_esp_png,  (int)tab_esp_png_len);
    g_tabIcons[3] = LoadTexFromMemory(tab_misc_png, (int)tab_misc_png_len);
    g_tabIcons[4] = LoadTexFromMemory(tab_cfg_png,  (int)tab_cfg_png_len);
    g_tabIcons[5] = LoadTexFromMemory(tab_opt_png,  (int)tab_opt_png_len);
}

static float pill_y   = -1.f;
static float pill_vel =  0.f;

struct TabRect { float sx; };
static TabRect tab_rects[kTabCount];

// ---- Акцент интерфейса: палитра стиля всего GUI ---------------------------
// 0 — классический фиолет (без оверрайда: Light/Dark как раньше), 1..7 —
// пресеты, kAccentCustom — свой цвет из g_state.ui_accent_rgb (тап открывает
// ту же палитру, что точки цвета в ESP). Цвета яркие середины — читаются и на
// светлой, и на тёмной теме: оверрайд один на обе.
static const ImVec4 kAccentCols[] = {
    {0.545f, 0.486f, 1.000f, 1.f},  // 0 фиолет (показывается, не применяется)
    {0.250f, 0.450f, 1.000f, 1.f},  // 1 синий
    {0.100f, 0.750f, 0.900f, 1.f},  // 2 голубой
    {0.200f, 0.750f, 0.350f, 1.f},  // 3 зелёный
    {0.950f, 0.750f, 0.150f, 1.f},  // 4 жёлтый
    {1.000f, 0.550f, 0.150f, 1.f},  // 5 оранжевый
    {0.950f, 0.250f, 0.300f, 1.f},  // 6 красный
    {1.000f, 0.450f, 0.700f, 1.f},  // 7 розовый
};
static constexpr int kAccentCustom = 8;
static constexpr int kAccentCount = 9;   // 8 пресетов + свой



// Calibration actions occupy full rows and must not use ImGui::Button's
// activation policy: the Android touch pipeline dispatches on release.
static void CalibrationRow(const char* id, const char* label, int mode, const char* value = nullptr) {
    const auto pos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = Layout::RowH;
    const float left = pos.x + Layout::Inset + Layout::PadX;
    const float right = pos.x + w - Layout::Inset - Layout::PadX;
    ImGui::InvisibleButton(id, {w, h});
    if (!DrawRectVisible(ImGui::GetWindowDrawList(), pos, {pos.x + w, pos.y + h})) return;
    if (WasTappedHere() && !(g_pop.visible && !g_pop.closing) && !g_sheet.visible &&
        !IsScrollDragging() && !g_input.touchConsumed) {
        g_calibMode = mode;
        menu_open = false;
        g_input.touchConsumed = true;
        PlaySound(SND_CLICK);
    }
    auto* dl = ImGui::GetWindowDrawList();
    const float fs = ImGui::GetFontSize();
    const auto vsz = ImGui::GetFont()->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0, value ? value : "");
    const float valueX = right - 24.f - vsz.x;
    if (value)
        dl->AddText(ImGui::GetFont(), fs * 0.85f, {valueX, pos.y + (h - vsz.y) * 0.5f}, C::U(C::Dim()), value);
    dl->PushClipRect({left, pos.y}, {valueX - 10.f, pos.y + h}, true);
    dl->AddText(ImGui::GetFont(), fs, {left, pos.y + (h - fs) * 0.5f}, C::U(C::Acc()), label);
    dl->PopClipRect();
    const float cy = pos.y + h * 0.5f;
    dl->AddLine({right - 8.f, cy - 5.f}, {right - 3.f, cy}, C::U(C::Acc()), 2.f);
    dl->AddLine({right - 3.f, cy}, {right - 8.f, cy + 5.f}, C::U(C::Acc()), 2.f);
}

float TabContent(int tab, float dt, float cW) {
    float sY = ImGui::GetCursorPosY();

    if (tab == 1) {

        cfg::aim::enabled    = g_state.aim_touch;
        cfg::aim::vis_check  = g_state.aim_pos;
        cfg::aim::draw_fov   = g_state.aim_special;
        cfg::aim::scope_only = g_state.aim_scope_only;
        cfg::aim::fov        = g_state.gun_fov;
        cfg::aim::smoothness = g_state.gun_str;
        cfg::aim::bone       = g_state.aim_bone;
        cfg::aim::trigger_delay  = g_state.gun_trigger_delay;
        cfg::aim::fire_only      = g_state.aim_fire_only;

        // Keep the feature switch visible; settings do not enable the feature.
        const char* const modes[] = {XS("Тач"), XS("Память"), XS("Сайлент")};
        const char* const bones[] = {XS("Голова"), XS("Шея"), XS("Тело")};
        const char* const priorities[] = {XS("Умный"), XS("Ближе к прицелу"), XS("Ближе ко мне")};
        SHdr(XS("Аим"));
        CardBg(Layout::RowH);
        ToggleRow("##ta1", XS("Аим"), &g_state.aim_touch, g_state.a_aim_touch, true, true);
        SHdr(XS("Режим"));
        ChoiceRow("##aim_mode", &g_state.aim_mode, modes, 3);

        char targetSummary[160];
        snprintf(targetSummary, sizeof(targetSummary), "%s · %s",
                 bones[ImClamp(g_state.aim_bone, 0, 2)], priorities[ImClamp(g_state.aim_priority, 0, 2)]);
        if (SettingsHeader("##aim_target", XS("Выбор цели"), targetSummary)) {
            SHdr(XS("Куда целиться"), 4.f);
            if (ChoiceRow("##aim_bone", &g_state.aim_bone, bones, 3)) {
                char text[160]; snprintf(text, sizeof(text), XS("%s|%s"), XS("Куда целиться"), bones[g_state.aim_bone]);
                ShowToast(text);
            }
            SHdr(XS("Выбор цели"), 4.f);
            if (ChoiceRow("##aim_priority", &g_state.aim_priority, priorities, 3)) {
                char text[160]; snprintf(text, sizeof(text), XS("%s|%s"), XS("Выбор цели"), priorities[g_state.aim_priority]);
                ShowToast(text);
            }
        }
        char filterSummary[48];
        snprintf(filterSummary, sizeof(filterSummary), "%d / 3", int(g_state.aim_pos) +
                 int(g_state.aim_scope_only) + int(g_state.aim_fire_only));
        if (SettingsHeader("##aim_filters", XS("Условия наведения"), filterSummary)) {
            CardBg(Layout::RowH * 3.f);
            ToggleRow("##ta2", XS("Только видимых"), &g_state.aim_pos, g_state.a_aim_pos, false, true);
            ToggleRow("##ta6", XS("Только в прицеле"), &g_state.aim_scope_only, g_state.a_aim_scope, false);
            ToggleRow("##ta7", XS("Только при стрельбе"), &g_state.aim_fire_only, g_state.a_aim_fire, true);
        }
        char aimSummary[48];
        snprintf(aimSummary, sizeof(aimSummary), XS("%.0f° · %.0f"),
                 g_state.gun_fov, g_state.gun_str);
        if (SettingsHeader("##aim_tuning", XS("Наводка"), aimSummary)) {
            CardBg(Layout::SliderH * 2.f + Layout::RowH);
            SliderRow("##afov", XS("Радиус"), &g_state.gun_fov, 5.f, 180.f, XS("%.0f°"), false, true, g_state.sl_gun_fov, dt);
            SliderRow("##asmt", XS("Скорость"), &g_state.gun_str, 1.f, 10.f, "%.0f", false, false, g_state.sl_gun_str, dt);
            ToggleRow("##ta3", XS("Круг FOV"), &g_state.aim_special, g_state.a_aim_spec, true, false, &cfg::aim::fov_col);
        }

        // ---- Автокалибровка упреждения ------------------------------------
        // Масштаб окна лида подбирается сам по доле попаданий: игра отмечает
        // попадание, промах виден как тишина после выстрела. В строке заголовка
        // — что выучено сейчас и сколько это даёт попаданий.
        {
            const aimlead::State& learn = aimlead::state();
            char learnSummary[64];
            const float overall = aimlead::overall_rate();
            if (!learn.enabled) {
                snprintf(learnSummary, sizeof(learnSummary), "%s", XS("выкл"));
            } else if (overall < 0.f) {
                snprintf(learnSummary, sizeof(learnSummary), XS("×%.2f · ждём выстрелы"), learn.scale);
            } else {
                snprintf(learnSummary, sizeof(learnSummary), XS("×%.2f · %.0f%%"), learn.scale, overall * 100.f);
            }
            if (SettingsHeader("##aim_learn", XS("Автокалибровка лида"), learnSummary)) {
                CardBg(Layout::RowH * 2.f);
                bool learnOn = learn.enabled;
                if (ToggleRow("##ta8", XS("Учить по попаданиям"), &learnOn, g_state.a_aim_learn, false, true))
                    aimlead::set_enabled(learnOn);
                // Трейс: CSV в каталоге конфига (aimtrace.csv). Пишется по кадру
                // на игрока, пока включён; по нему считается реальная задержка
                // отрисовки вместо подбора константой.
                bool traceOn = aimtrace::enabled();
                if (ToggleRow("##ta9", XS("Запись трейса (CSV)"), &traceOn, g_state.a_aim_trace, true, false))
                    aimtrace::set_enabled(traceOn);
            }
        }

        if (g_state.aim_mode == AIM_MODE_SILENT &&
            SettingsHeader("##aim_silent", XS("Сайлент"), g_state.silent_auto_zone ? XS("Авто-зона") : XS("Ручная зона"))) {
            CardBg(Layout::RowH);
            ToggleRow("##tsz", XS("Авто-зона (грудь на бегу)"), &g_state.silent_auto_zone, g_state.a_silent_zone, true, true);
        }

        // ---- Тач-зона аима -------------------------------------------------
        // Где именно лежит палец, которым аимбот водит камеру. Выбирается
        // тапом по экрану — как зоны автофарма, только точка одна: меню
        // прячется, первый тап записывает её в долях экрана. В режимах записи
        // в память пальца нет вовсе, поэтому секции здесь нет.
        if (g_state.aim_mode == AIM_MODE_TOUCH &&
            SettingsHeader("##aim_touch_settings", XS("Точка пальца"),
                           g_state.aim_tx >= 0.f ? XS("Настроено") : XS("По умолчанию"))) {
            {
                const float rowH = Layout::RowH;
                const float inset = Layout::Inset, padX = Layout::PadX;
                const float avW   = ImGui::GetContentRegionAvail().x;
                auto* dl = ImGui::GetWindowDrawList();
                auto* fn = ImGui::GetFont();
                const float fs = ImGui::GetFontSize();
                const bool popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;
                const bool pointSet = (g_state.aim_tx >= 0.f);

                CardBg(rowH * (pointSet ? 2.f : 1.f));

                // Строка «точка пальца»: подпись слева, доля экрана справа.
                auto pos = ImGui::GetCursorScreenPos();
                {
                    const float cX = pos.x + inset, cW = avW - inset * 2.f;
                    const float cy = pos.y + rowH * 0.5f;
                    ImGui::InvisibleButton("##aim_pt", {avW, rowH});
                    if (WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed) {
                        g_calibMode = 3;   // калибровка: тап по экрану задаёт точку
                        PlaySound(SND_CLICK);
                        ShowToast(XS("Задай точку тапом по экрану"));
                        g_input.touchConsumed = true;
                    }

                    char val[32];
                    snprintf(val, sizeof(val), "%d%% %d%%",
                             (int)(AimTouchFracX() * 100.f + 0.5f),
                             (int)(AimTouchFracY() * 100.f + 0.5f));
                    auto vsz = fn->CalcTextSizeA(fs, FLT_MAX, 0, val);

                    dl->AddText(fn, fs * 1.15f, {cX + padX, cy - fs * 1.15f * 0.5f},
                                C::UA(C::Txt(), 1.f), XS("Точка пальца"));
                    dl->AddText(fn, fs, {cX + cW - padX - vsz.x, cy - vsz.y * 0.5f},
                                C::UA(C::Acc(), 1.f), val);

                    if (pointSet && g_state.ui_show_sep)
                        dl->AddLine({cX + padX, pos.y + rowH - 0.5f},
                                    {cX + cW - padX, pos.y + rowH - 0.5f}, C::UA(C::Sep(), 1.f), 0.8f);
                }

                // Сброс к прежней позиции (72%/48%) — только если точка задана.
                if (pointSet) {
                    auto rpos = ImGui::GetCursorScreenPos();
                    const float cX = rpos.x + inset, cW = avW - inset * 2.f;
                    ImGui::InvisibleButton("##aim_pt_rst", {avW, rowH});
                    const bool tapped = WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed;
                    if (tapped) {
                        g_state.aim_tx = g_state.aim_ty = -1.f;
                        ShowToast(XS("Точка сброшена"));
                        PlaySound(SND_CLICK);
                        g_input.touchConsumed = true;
                    }
                    const char* rt = XS("Сброс");
                    auto rsz = fn->CalcTextSizeA(fs * 1.05f, FLT_MAX, 0, rt);
                    dl->AddText(fn, fs * 1.05f,
                                {cX + (cW - rsz.x) * 0.5f, rpos.y + (rowH - rsz.y) * 0.5f},
                                C::UA(C::Dim(), 1.f), rt);
                }
            }
        }

        ImGui::Dummy({1.f, 12.f});

    } else if (tab == 2) {
        cfg::esp::box          = g_state.esp_box;
        cfg::esp::name_esp     = g_state.esp_name;
        cfg::esp::distance     = g_state.esp_wall;
        cfg::esp::weapon       = g_state.esp_weapon;
        cfg::esp::tracer       = g_state.esp_tracer;
        cfg::esp::skeleton     = g_state.esp_skeleton;
        cfg::esp::ore          = g_state.esp_ore;
        cfg::esp::animal       = g_state.esp_animal;
        cfg::esp::loot         = g_state.esp_loot;
        cfg::esp::team         = g_state.esp_team;
        cfg::esp::pickup       = g_state.esp_pickup;
        cfg::esp::bot          = g_state.esp_bots;
        cfg::esp::vehicle      = g_state.esp_vehicles;
        cfg::esp::scrap        = g_state.esp_scrap;



        auto* dl  = ImGui::GetWindowDrawList();
        auto* fn  = ImGui::GetFont();
        float avW = ImGui::GetContentRegionAvail().x;
        float fs  = ImGui::GetFontSize();
        const float inset = Layout::Inset, padX = Layout::PadX, rowH = Layout::RowH;
        bool popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;

        static const char* s_colorHoldId = nullptr;

        auto EspToggleColorRow = [&](const char* id, const char* lbl, bool* v, float* a, ImVec4* col, bool last) {
            auto pos = ImGui::GetCursorScreenPos();
            float cX = pos.x + inset, cW2 = avW - inset * 2.f;
            float cy = pos.y + rowH * 0.5f;
            auto& io2 = ImGui::GetIO();

            ImGui::InvisibleButton(id, {avW, rowH});
            if (!DrawRectVisible(dl, pos, {pos.x + avW, pos.y + rowH})) return;

            float dotR = 13.f;
            float dotX = 0.f;
            if (col) {
                auto lblSz = fn->CalcTextSizeA(fs*1.15f, FLT_MAX, 0, lbl);
                dotX = cX + padX + lblSz.x + 14.f + dotR;
            }
            bool openedColor = false;
            if (col && !popBlk && !g_pop.visible) {
                float cx0 = io2.MouseClickedPos[0].x, cy0 = io2.MouseClickedPos[0].y;
                float dx = io2.MousePos.x - cx0, dy = io2.MousePos.y - cy0;
                float moved = dx*dx + dy*dy;
                bool onDot = (cx0 - dotX)*(cx0 - dotX) + (cy0 - cy)*(cy0 - cy) <= (dotR + 18.f)*(dotR + 18.f);
                if (io2.MouseReleased[0] && onDot && PtInClip({dotX, cy}) && moved < 28.f*28.f && s_colorHoldId != id) {
                    s_colorHoldId = id;
                    PopoverOpenColor(lbl, col);
                    PlaySound(SND_CLICK);
                    g_input.touchConsumed = true;
                    openedColor = true;
                }
            }
            if (!io2.MouseDown[0] && s_colorHoldId == id) s_colorHoldId = nullptr;

            bool tapped = !openedColor && WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed;
            if (tapped) { *v = !*v; char b[160]; snprintf(b,sizeof(b),XS("%s|%s"),lbl,*v?XS("Вкл"):XS("Выкл")); ShowToast(b); PlaySound(SND_CLICK); }
            if (col) {
                ImVec4& c = *col;
                dl->AddCircleFilled({dotX, cy}, dotR, IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),255), 32);
                dl->AddCircle({dotX, cy}, dotR + 2.f, IM_COL32(0,0,0,80), 32, 2.f);
                dl->AddCircle({dotX, cy}, dotR + 1.f, IM_COL32(255,255,255,180), 32, 1.2f);
            }
            float textY = cy - fs * 1.15f * 0.5f;
            dl->AddText(fn, fs * 1.15f, {cX + padX, textY}, C::U(C::Txt()), lbl);
            DrawToggle(dl, cX + cW2 - 72.f - padX, cy, EaseInOut(*a));
            if (!last && g_state.ui_show_sep)
                dl->AddLine({cX+padX, pos.y+rowH-0.5f},{cX+cW2-padX, pos.y+rowH-0.5f}, C::UA(C::Sep(),0.35f), 0.8f);
        };

        struct ERow { const char* id; const char* lbl; bool* v; float* a; ImVec4* col; };
        auto drawRows = [&](ERow* rows, int count) {
            CardBg(rowH * count);
            for (int i = 0; i < count; ++i)
                EspToggleColorRow(rows[i].id, rows[i].lbl, rows[i].v, rows[i].a, rows[i].col, i == count - 1);
        };
        auto group = [&](const char* id, const char* label, ERow* rows, int count) {
            int active = 0;
            for (int i = 0; i < count; ++i) active += *rows[i].v ? 1 : 0;
            char summary[32]; snprintf(summary, sizeof(summary), "%d / %d", active, count);
            return SettingsHeader(id, label, summary);
        };
        SHdr(XS("Игроки"));
        ERow players[] = {
{"##vb",  XS("Боксы"),      &g_state.esp_box,          &g_state.a_esp_box,          &cfg::esp::box_col},
{"##vvs", XS("Проверка видимости"), &cfg::esp::vis_check, &g_state.a_esp_vis,       &cfg::esp::box_col_invis},
{"##v3",  XS("3D боксы"),   &g_state.esp_chams,        &g_state.a_esp_chams,        &cfg::esp::box_col_invis},
{"##vn",  XS("Ники"),       &g_state.esp_name,         &g_state.a_esp_name,         &cfg::esp::name_col},
{"##vd",  XS("Дистанция"),  &g_state.esp_wall,         &g_state.a_esp_wall,         &cfg::esp::distance_col},
{"##vw",  XS("Оружие"),     &g_state.esp_weapon,       &g_state.a_esp_weapon,       &cfg::esp::weapon_col},
{"##vtr", XS("Линии"),      &g_state.esp_tracer,       &g_state.a_esp_tracer,       &cfg::esp::tracer_col},
{"##vsk", XS("Скелеты"),    &g_state.esp_skeleton,     &g_state.a_esp_skeleton,     &cfg::esp::skeleton_col},
{"##var", XS("Броня"),      &g_state.esp_armor,        &g_state.a_esp_armor,        nullptr},
{"##vmh", XS("Мапхак"),     &g_state.esp_maphack,      &g_state.a_esp_maphack,      nullptr},
{"##vtm", XS("Союзники"),   &g_state.esp_team,         &g_state.a_esp_team,         &cfg::esp::ally_col},
        };
        if (group("##esp_players", XS("Игроки"), players, 11)) {
            drawRows(players, 11);
            SHdr(XS("Линии и боксы"), 8.f);
            CardBg(Layout::SliderH * 2.f);
            SliderRow("##esp_stretch", XS("Растяг экрана"), &g_state.screen_stretch_x,
                      1.f, 2.f, XS("×%.2f"), false, true, g_state.sl_screen_stretch, dt);
            SliderRow("##esp_thickness", XS("Толщина"), &g_state.esp_thick,
                      0.5f, 5.f, "%.1f", true, false, g_state.sl_esp_thick, dt);
        }
        SHdr(XS("Стрелки и радар 360°"));
        if (SettingsHeader("##esp_arrows", XS("Стрелки 360°"), XS("Цвет, радиус и дистанция"),
                           false, &g_state.esp_arrows, g_state.a_esp_arrows)) {
            CardBg(rowH * 3.f + Layout::SliderH);
            ColorRow("##arrow_color", XS("Цвет"), &cfg::esp::arrow_col, false);
            EspToggleColorRow("##va360o", XS("Только вне экрана"), &g_state.esp_arrows_offscreen,
                              &g_state.a_esp_arrows_off, nullptr, false);
            EspToggleColorRow("##va360d", XS("Дистанция"), &g_state.esp_arrows_dist,
                              &g_state.a_esp_arrows_dist, nullptr, false);
            SliderRow("##va360r", XS("Радиус кольца"), &g_state.esp_arrow_ring,
                      8.f, 60.f, XS("%.0f%%"), true, false, g_state.sl_esp_arrow_ring, dt);
        }
        if (SettingsHeader("##esp_radar", XS("Радар 360°"), XS("Размер и дальность"),
                           false, &g_state.esp_radar, g_state.a_esp_radar)) {
            CardBg(Layout::SliderH * 2.f);
            SliderRow("##vrd1", XS("Размер радара"), &g_state.esp_radar_size,
                      90.f, 360.f, XS("%.0f px"), false, true, g_state.sl_esp_radar_size, dt);
            SliderRow("##vrd2", XS("Дальность радара"), &g_state.esp_radar_range,
                      10.f, 400.f, XS("%.0f м"), true, false, g_state.sl_esp_radar_range, dt);
        }
        SHdr(XS("Мир"));
        if (SettingsHeader("##esp_ore", XS("Руда"), XS("Камень, железо и сера"),
                           false, &g_state.esp_ore, g_state.a_esp_ore)) {
            ERow ores[] = {
{"##orvt", XS("Камень"), &g_state.esp_ore_stone,  &g_state.a_esp_ore_stone,  &cfg::esp::ore_stone_col},
{"##orvi", XS("Железо"), &g_state.esp_ore_iron,   &g_state.a_esp_ore_iron,   &cfg::esp::ore_metal_col},
{"##orvs", XS("Сера"),   &g_state.esp_ore_sulfur, &g_state.a_esp_ore_sulfur, &cfg::esp::ore_sulfur_col},
            };
            drawRows(ores, 3);
        }
        ERow entities[] = {
{"##van", XS("Животные"), &g_state.esp_animal,       &g_state.a_esp_animal,       &cfg::esp::animal_col},
{"##vbt", XS("Боты"),     &g_state.esp_bots,         &g_state.a_esp_bots,         &cfg::esp::bot_col},
{"##vvc", XS("Машины"),   &g_state.esp_vehicles,     &g_state.a_esp_vehicles,     &cfg::esp::vehicle_col},
{"##vsl", XS("Спящие"),   &g_state.esp_sleepers,     &g_state.a_esp_sleepers,     &cfg::esp::sleeper_col},
        };
        if (group("##esp_entities", XS("Существа и транспорт"), entities, 4)) drawRows(entities, 4);
        ERow loot[] = {
{"##vlt", XS("Ящики"),    &g_state.esp_loot,         &g_state.a_esp_loot,         &cfg::esp::loot_col},
{"##vhb", XS("Ящики в домах"), &g_state.esp_house_boxes, &g_state.a_esp_house_boxes, nullptr},
{"##vsc", XS("Скрап"),    &g_state.esp_scrap,        &g_state.a_esp_scrap,        &cfg::esp::scrap_col},
{"##vpk", XS("Предметы"), &g_state.esp_pickup,       &g_state.a_esp_pickup,       &cfg::esp::pickup_col},
{"##vtu", XS("Турели"),   &g_state.esp_turrets,      &g_state.a_esp_turrets,      &cfg::esp::turret_col},
{"##vbp", XS("Рюкзаки"),  &g_state.esp_backpacks,    &g_state.a_esp_backpacks,    &cfg::esp::backpack_col},
{"##vcb", XS("Шкафы"),    &g_state.esp_cupboards,    &g_state.a_esp_cupboards,    &cfg::esp::cupboard_col},
{"##vpm", XS("Тыквы"),    &g_state.esp_pumpkins,     &g_state.a_esp_pumpkins,     &cfg::esp::pumpkin_col},
        };
        if (group("##esp_loot", XS("Предметы и постройки"), loot, 8)) drawRows(loot, 8);
        char rangeSummary[48]; snprintf(rangeSummary, sizeof(rangeSummary), XS("%.0f м"), g_state.marker_dist);
        if (SettingsHeader("##esp_distance", XS("Дальность"), rangeSummary)) {
            CardBg(Layout::SliderH);
            SliderRow("##vmd", XS("Показывать до"), &g_state.marker_dist,
                      25.f, 300.f, XS("%.0f м"), true, true, g_state.sl_marker_dist, dt);
        }

    } else if (tab == 4) {
        // Profile names stay visible; actions expand under their own profile.

        auto* dl  = ImGui::GetWindowDrawList();
        auto* fn  = ImGui::GetFont();
        float avW = ImGui::GetContentRegionAvail().x;
        const float inset = Layout::Inset;
        const float fs = ImGui::GetFontSize();

        SHdr(XS("Новый конфиг"));
        {
            const float btnH = 72.f;
            auto pos = ImGui::GetCursorScreenPos();
            float cardX0 = pos.x + inset, cardX1 = pos.x + avW - inset;

            dl->AddRectFilled({cardX0, pos.y}, {cardX1, pos.y + btnH},
                C::U(C::Card()), R::Btn);
            if (g_state.ui_show_sep)
                dl->AddRect({cardX0, pos.y}, {cardX1, pos.y + btnH},
                    C::UA(C::Sep(), 0.9f), R::Btn, 0, 1.2f);

            ImGui::InvisibleButton("##cfgcreate", {avW, btnH});
            bool popBlocking2 = (g_pop.visible && !g_pop.closing) || g_sheet.visible;
            bool tapped = WasTappedHere() && !popBlocking2 && !IsScrollDragging() && !g_input.touchConsumed;
            if (tapped) {
                g_input.touchConsumed = true;
                ConfigSave();
            }

            float cy2 = pos.y + btnH * 0.5f;
            float icX = cardX0 + 28.f + 20.f;
            dl->AddCircleFilled({icX, cy2}, 20.f, C::UA(C::Acc(), 0.15f), 32);
            dl->AddCircle({icX, cy2}, 20.f, C::UA(C::Acc(), 0.7f), 32, 1.5f);
            dl->AddLine({icX - 9.f, cy2}, {icX + 9.f, cy2}, C::U(C::Acc()), 3.f);
            dl->AddLine({icX, cy2 - 9.f}, {icX, cy2 + 9.f}, C::U(C::Acc()), 3.f);

            const char* createTxt = XS("Создать конфиг");
            float textY = pos.y + (btnH - fs * 1.25f) * 0.5f;
            dl->AddText(fn, fs * 1.25f, {icX + 32.f, textY}, C::U(C::Acc()), createTxt);
        }

        SHdr(XS("Сохранённые"));

        if (g_configCount == 0) {
            const float emptyH = 120.f;
            auto ep = ImGui::GetCursorScreenPos();
            dl->AddRectFilled({ep.x + inset, ep.y}, {ep.x + avW - inset, ep.y + emptyH},
                C::U(C::Card()), R::Card);
            if (g_state.ui_show_sep)
                dl->AddRect({ep.x + inset, ep.y}, {ep.x + avW - inset, ep.y + emptyH},
                    C::UA(C::Sep(), 0.9f), R::Card, 0, 1.2f);
            ImGui::Dummy({avW, emptyH});
            const char* emptyTxt = XS("Нет конфигов");
            auto eSz = fn->CalcTextSizeA(fs * 1.05f, FLT_MAX, 0, emptyTxt);
            dl->AddText(fn, fs * 1.05f,
                {ep.x + (avW - eSz.x) * 0.5f, ep.y + (emptyH - fs * 1.05f) * 0.5f},
                C::U(C::Dim()), emptyTxt);
        } else {
            const char* const actions[] = {XS("Загрузить"), XS("Сохранить"), XS("Удалить")};
            for (int ci = 0; ci < g_configCount; ++ci) {
                // Stable across directory reordering/deletion and language changes.
                ImGui::PushID(g_configs[ci].name);
                if (SettingsHeader("##config", g_configs[ci].name,
                                   ci == g_cfgLoadedIdx ? XS("Загружен") : XS("Конфиг"))) {
                    int action = -1;
                    if (ChoiceRow("##config_actions", &action, actions, 3)) {
                        if (action == 0) ConfigLoad(ci);
                        else if (action == 1) ConfigUpdate(ci);
                        else {
                            g_configToDelete = ci;
                            PopoverOpen(XS("Удалить конфиг?"), 3);
                        }
                    }
                }
                ImGui::PopID();
            }
        }

    } else if (tab == 5) {
        // Опции (язык + интерфейс + система) — нижняя вкладка.
        // Счётчик FPS и «Рамки карточек» убраны: рамки включены принудительно.
        // ---- Язык: русский или английский -------------------------------
        // Переводится всё, что рисует оверлей: подписи меню и вкладок, тосты и
        // подписи визуалов (оружие, предметы, животные, ящики). Переключение
        // применяется сразу, на текущем кадре. Названия самих языков пишем на
        // них самих — так строка понятна при любом выбранном языке.
        SHdr(XS("Интерфейс"));
        if (SettingsHeader("##options_language", XS("Язык"), lang::english() ? "English" : "Русский"))
        {
            auto* dl  = ImGui::GetWindowDrawList();
            auto* fn  = ImGui::GetFont();
            float avW = ImGui::GetContentRegionAvail().x;
            const float inset = Layout::Inset;
            const float rowH  = Layout::RowH;
            auto  pos = ImGui::GetCursorScreenPos();
            bool  popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;

            const char* const langNames[2] = { "Русский", "English" };
            const float gap = 10.f;
            const float halfW = (avW - inset * 2.f - gap) * 0.5f;

            for (int li = 0; li < 2; li++) {
                const bool sel = (lang::current() == (li == 0 ? lang::LANG_RU : lang::LANG_EN));
                const float x0 = pos.x + inset + li * (halfW + gap);
                const float x1 = x0 + halfW;

                dl->AddRectFilled({x0, pos.y}, {x1, pos.y + rowH}, C::U(C::Card()), R::Card);
                if (sel) {
                    dl->AddRectFilled({x0, pos.y}, {x1, pos.y + rowH},
                        C::UA(C::Acc(), g_darkTheme ? 0.16f : 0.10f), R::Card);
                    dl->AddRect({x0, pos.y}, {x1, pos.y + rowH}, C::UA(C::Acc(), 0.8f), R::Card, 0, 2.f);
                } else if (g_state.ui_show_sep) {
                    dl->AddRect({x0, pos.y}, {x1, pos.y + rowH}, C::U(C::Sep()), R::Card, 0, 1.2f);
                }

                auto lsz = fn->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, langNames[li]);
                dl->AddText(fn, ImGui::GetFontSize(),
                    {x0 + (halfW - lsz.x) * 0.5f, pos.y + (rowH - lsz.y) * 0.5f},
                    C::U(sel ? C::Acc() : C::Txt()), langNames[li]);

                char lid[16]; snprintf(lid, sizeof(lid), "##lang%d", li);
                ImGui::SetCursorScreenPos({x0, pos.y});
                ImGui::InvisibleButton(lid, {halfW, rowH});
                if (WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed && !sel) {
                    lang::set(li == 0 ? lang::LANG_RU : lang::LANG_EN);
                    // Тост — уже на выбранном языке: он и показывает, что
                    // переключение сработало в этом же кадре.
                    char _b[160];
                    snprintf(_b, sizeof(_b), "%s|%s", lang::text(XS_RU("Язык")), langNames[li]);
                    ShowToast(_b);
                    PlaySound(SND_CLICK);
                }
            }
            ImGui::SetCursorScreenPos({pos.x, pos.y + rowH});
            ImGui::Dummy({avW, 0.f});
        }

        if (SettingsHeader("##options_appearance", XS("Оформление"), XS("Тема и цвет интерфейса"))) {
            CardBg(Layout::RowH * 1);
            if (ToggleRow("##ud2", XS("Тёмная тема"), &g_state.ui_dark_mode, g_state.a_ui_dark, true, true))
                g_darkTheme = g_state.ui_dark_mode;

            // ---- Акцент: палитра стиля всего GUI -------------------------------
            // Ряд кругов: 8 пресетов + свой цвет (тап открывает палитру, как точки
            // цвета в ESP; повторный тап по выбранному «своему» открывает её снова).
            // Круги делят строку поровну: диаметр под ширину, не наоборот.
            SHdr(XS("Цвет интерфейса"));
            {
                auto* dl  = ImGui::GetWindowDrawList();
                float avW = ImGui::GetContentRegionAvail().x;
                const float inset = Layout::Inset;
                const float rowH  = Layout::RowH;
                auto  pos = ImGui::GetCursorScreenPos();
                bool  popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;

                CardBg(rowH);
                const float avail = avW - inset * 2.f;
                float d = (avail - (kAccentCount - 1) * 10.f) / kAccentCount;
                if (d > 60.f) d = 60.f;
                if (d < 28.f) d = 28.f;
                float gap = (kAccentCount > 1) ? (avail - d * kAccentCount) / (kAccentCount - 1) : 0.f;
                if (gap < 0.f) gap = 0.f;
                const float cy = pos.y + rowH * 0.5f;
                for (int i = 0; i < kAccentCount; ++i) {
                    const float cx = pos.x + inset + d * 0.5f + i * (d + gap);
                    const bool sel = (g_state.ui_accent_idx == i);
                    const ImVec4 col = (i == kAccentCustom) ? g_state.ui_accent_rgb : kAccentCols[i];
                    dl->AddCircleFilled({cx, cy}, d * 0.5f, C::U(col), 40);
                    if (sel)
                        dl->AddCircle({cx, cy}, d * 0.5f + 3.f, C::U(C::Txt()), 40, 3.f);
                    else if (g_state.ui_show_sep)
                        dl->AddCircle({cx, cy}, d * 0.5f + 1.f, C::UA(C::Sep(), 0.9f), 40, 1.2f);
                    char aid[16]; snprintf(aid, sizeof(aid), "##acc%d", i);
                    ImGui::SetCursorScreenPos({cx - d * 0.5f, cy - d * 0.5f});
                    ImGui::InvisibleButton(aid, {d, d});
                    const bool customTap = (i == kAccentCustom);
                    if (WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed &&
                        (!sel || customTap)) {
                        g_state.ui_accent_idx = i;
                        g_input.touchConsumed = true;
                        if (customTap)
                            PopoverOpenColor(XS("Свой цвет"), &g_state.ui_accent_rgb);
                        PlaySound(SND_CLICK);
                    }
                }
                ImGui::SetCursorScreenPos({pos.x, pos.y + rowH});
                ImGui::Dummy({avW, 0.f});
            }

        }

        if (SettingsHeader("##options_hud", XS("Худ"), XS("Счётчик игроков")))
        {
            const float rowH = Layout::RowH;
            const float inset = Layout::Inset, padX = Layout::PadX;
            const float avW   = ImGui::GetContentRegionAvail().x;
            auto* dl = ImGui::GetWindowDrawList();
            auto* fn = ImGui::GetFont();
            const float fs = ImGui::GetFontSize();
            const bool popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;
            const bool posSet = (g_state.hud_cnt_x >= 0.f);

            CardBg(rowH * (posSet ? 2.f : 1.f));

            auto pos = ImGui::GetCursorScreenPos();
            {
                const float cX = pos.x + inset, cW = avW - inset * 2.f;
                const float cy = pos.y + rowH * 0.5f;
                ImGui::InvisibleButton("##hud_cnt", {avW, rowH});
                if (WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed) {
                    g_calibMode = 5; // калибровка: тап по экрану ставит счётчик
                    PlaySound(SND_CLICK);
                    g_input.touchConsumed = true;
                }

                char val[32];
                if (posSet) snprintf(val, sizeof(val), "%d%% %d%%",
                             (int)(g_state.hud_cnt_x * 100.f + 0.5f),
                             (int)(g_state.hud_cnt_y * 100.f + 0.5f));
                else        snprintf(val, sizeof(val), "%s", XS("По умолчанию"));
                auto vsz = fn->CalcTextSizeA(fs, FLT_MAX, 0, val);

                dl->AddText(fn, fs * 1.15f, {cX + padX, cy - fs * 1.15f * 0.5f},
                            C::UA(C::Txt(), 1.f), XS("Счётчик игроков"));
                dl->AddText(fn, fs, {cX + cW - padX - vsz.x, cy - vsz.y * 0.5f},
                            C::UA(C::Acc(), 1.f), val);

                if (posSet && g_state.ui_show_sep)
                    dl->AddLine({cX + padX, pos.y + rowH - 0.5f},
                                {cX + cW - padX, pos.y + rowH - 0.5f}, C::UA(C::Sep(), 1.f), 0.8f);
            }

            // Сброс к верху по центру — только если положение задано.
            if (posSet) {
                auto rpos = ImGui::GetCursorScreenPos();
                const float cX = rpos.x + inset, cW = avW - inset * 2.f;
                ImGui::InvisibleButton("##hud_cnt_rst", {avW, rowH});
                const bool tapped = WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed;
                if (tapped) {
                    g_state.hud_cnt_x = g_state.hud_cnt_y = -1.f;
                    ShowToast(XS("Положение сброшено"));
                    PlaySound(SND_CLICK);
                    g_input.touchConsumed = true;
                }
                const char* rt = XS("Сброс");
                auto rsz = fn->CalcTextSizeA(fs * 1.05f, FLT_MAX, 0, rt);
                dl->AddText(fn, fs * 1.05f,
                            {cX + (cW - rsz.x) * 0.5f, rpos.y + (rowH - rsz.y) * 0.5f},
                            C::UA(C::Dim(), 1.f), rt);
            }
        }

        // ---- Положение панели вкладок: слева или снизу ------------------
        // Перенесено сюда из удалённой вкладки «Меню».
        if (SettingsHeader("##options_tabs", XS("Панель вкладок"),
                           g_state.ui_panel_left ? XS("Слева") : XS("Снизу")))
        {
            auto* dl  = ImGui::GetWindowDrawList();
            auto* fn  = ImGui::GetFont();
            float avW = ImGui::GetContentRegionAvail().x;
            const float inset = Layout::Inset;
            const float fs = ImGui::GetFontSize();

            const float cardH = 120.f;
            const float gap   = 10.f;
            auto  pos   = ImGui::GetCursorScreenPos();
            float x0    = pos.x + inset;
            float cardW = (avW - inset * 2.f - gap) * 0.5f;
            bool  popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;

            struct POpt { const char* lbl; bool left; };
            const POpt opts[2] = { { XS("Слева"), true }, { XS("Снизу"), false } };

            for (int oi = 0; oi < 2; oi++) {
                float ox0 = x0 + oi * (cardW + gap);
                float ox1 = ox0 + cardW;
                bool  sel = (g_state.ui_panel_left == opts[oi].left);

                dl->AddRectFilled({ox0, pos.y}, {ox1, pos.y + cardH}, C::U(C::Card()), R::Card);
                if (sel) {
                    dl->AddRectFilled({ox0, pos.y}, {ox1, pos.y + cardH},
                        C::UA(C::Acc(), g_darkTheme ? 0.16f : 0.10f), R::Card);
                    dl->AddRect({ox0, pos.y}, {ox1, pos.y + cardH}, C::UA(C::Acc(), 0.8f), R::Card, 0, 2.f);
                } else if (g_state.ui_show_sep) {
                    dl->AddRect({ox0, pos.y}, {ox1, pos.y + cardH}, C::U(C::Sep()), R::Card, 0, 1.2f);
                }

                // Мини-схема окна: прямоугольник с панелью слева или снизу.
                float mw = 74.f, mh = 52.f;
                float mx = ox0 + (cardW - mw) * 0.5f;
                float my = pos.y + 14.f;
                ImU32 frameCol = C::UA(sel ? C::Acc() : C::Dim(), sel ? 0.95f : 0.6f);
                dl->AddRect({mx, my}, {mx + mw, my + mh}, frameCol, 6.f, 0, 2.f);
                if (opts[oi].left)
                    dl->AddRectFilled({mx + 3.f, my + 3.f}, {mx + 3.f + 16.f, my + mh - 3.f},
                                      C::UA(sel ? C::Acc() : C::Dim(), sel ? 0.85f : 0.4f), 4.f);
                else
                    dl->AddRectFilled({mx + 3.f, my + mh - 3.f - 12.f}, {mx + mw - 3.f, my + mh - 3.f},
                                      C::UA(sel ? C::Acc() : C::Dim(), sel ? 0.85f : 0.4f), 4.f);

                auto lsz = fn->CalcTextSizeA(fs * 1.0f, FLT_MAX, 0, opts[oi].lbl);
                dl->AddText(fn, fs * 1.0f,
                    {ox0 + (cardW - lsz.x) * 0.5f, pos.y + cardH - 16.f - lsz.y},
                    C::U(sel ? C::Acc() : C::Txt()), opts[oi].lbl);

                char oid[16]; snprintf(oid, sizeof(oid), "##pstyle%d", oi);
                ImGui::SetCursorScreenPos({ox0, pos.y});
                ImGui::InvisibleButton(oid, {cardW, cardH});
                if (WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed
                    && g_state.ui_panel_left != opts[oi].left) {
                    g_state.ui_panel_left = opts[oi].left;
                    pill_y = -1.f; pill_vel = 0.f;   // «пилюля» меняет ось — сброс пружины
                    ShowToast(opts[oi].left ? XS("Панель слева") : XS("Панель снизу"));
                    PlaySound(SND_CLICK);
                }
            }
            ImGui::SetCursorScreenPos({pos.x, pos.y + cardH});
            ImGui::Dummy({avW, 0.f});
        }

        SHdr(XS("Система"));
        {
            auto* dl  = ImGui::GetWindowDrawList();
            float avW = ImGui::GetContentRegionAvail().x;
            const float rowH = Layout::RowH, inset = Layout::Inset;
            auto pos = ImGui::GetCursorScreenPos();
            dl->AddRectFilled({pos.x + inset, pos.y}, {pos.x + avW - inset, pos.y + rowH}, C::U(C::Card()), R::Card);
            if (g_state.ui_show_sep)
                dl->AddRect({pos.x + inset, pos.y}, {pos.x + avW - inset, pos.y + rowH}, C::U(C::Sep()), R::Card, 0, 1.2f);
            ImGui::InvisibleButton("##exit", {avW, rowH});
            if (WasTappedHere() && !IsScrollDragging())
                PopoverOpen(XS("Выйти?"), 2);
            const char* exitTxt = XS("Выйти");
            float exitFS = ImGui::GetFontSize() * 1.15f;
            auto  tsz = ImGui::GetFont()->CalcTextSizeA(exitFS, FLT_MAX, 0, exitTxt);
            float tx  = pos.x + (avW - tsz.x) * 0.5f;
            float ty  = pos.y + (rowH - exitFS) * 0.5f;
            dl->AddText(ImGui::GetFont(), exitFS, {tx, ty}, C::U(C::Red()), exitTxt);
        }
    } else if (tab == 3) {
        auto& vm = g_state.viewmodel;
        SHdr(XS("Камера"));
        char fovSummary[32]; snprintf(fovSummary, sizeof(fovSummary), XS("%.0f°"), vm.fov);
        if (SettingsHeader("##misc_fov", XS("Кастомный FOV"), fovSummary,
                           false, &vm.fov_enabled, g_state.a_custom_fov)) {
            CardBg(Layout::SliderH);
            SliderRow("##vmfovvalue", XS("Обзор мира"), &vm.fov,
                      50.f, 120.f, XS("%.0f°"), true, true, g_state.sl_custom_fov, dt);
        }
        if (SettingsHeader("##misc_hands", XS("Расположение рук"), XS("X / Y / Z"),
                           false, &vm.hands_enabled, g_state.a_custom_hands))
        {
            const float resetH = Layout::RowH * 0.9f;
            CardBg(Layout::SliderH * 3.f + resetH);
            SliderRow("##vmx", XS("X: влево / вправо"), &vm.x,
                      -50.f, 50.f, XS("%.0f см"), false, false, g_state.sl_hands_x, dt);
            SliderRow("##vmy", XS("Y: вниз / вверх"), &vm.y,
                      -50.f, 50.f, XS("%.0f см"), false, false, g_state.sl_hands_y, dt);
            SliderRow("##vmz", XS("Z: ближе / дальше"), &vm.z,
                      -50.f, 50.f, XS("%.0f см"), true, false, g_state.sl_hands_z, dt);
            auto* dl = ImGui::GetWindowDrawList();
            auto* fn = ImGui::GetFont();
            const float inset = Layout::Inset;
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##vmreset", {w, resetH});
            const bool popBlk = (g_pop.visible && !g_pop.closing) || g_sheet.visible;
            if (WasTappedHere() && !popBlk && !IsScrollDragging() && !g_input.touchConsumed) {
                // This button resets only FOV/hands, not the other misc features.
                const int fps = vm.fps_limit;
                const bool recoilButton = vm.no_recoil_button, ns = vm.no_spread;
                const bool fl = vm.fast_loot;
                vm = {}; vm.fps_limit = fps; vm.no_recoil_button = recoilButton; vm.no_spread = ns;
                vm.fast_loot = fl;
                g_state.sl_custom_fov = {}; g_state.sl_hands_x = {};
                g_state.sl_hands_y = {}; g_state.sl_hands_z = {};
                esp_viewmodel_tick(vm);
                PlaySound(SND_CLICK);
                g_input.touchConsumed = true;
            }
            dl->AddRectFilled({rp.x + inset, rp.y}, {rp.x + w - inset, rp.y + resetH},
                              C::U(C::TrkOff()), 6.f);
            const float tfs = ImGui::GetFontSize() * 1.05f;
            const auto tsz = fn->CalcTextSizeA(tfs, FLT_MAX, 0, XS("Сброс"));
            dl->AddText(fn, tfs,
                        {rp.x + (w - tsz.x) * 0.5f, rp.y + (resetH - tsz.y) * 0.5f},
                        C::U(C::Dim()), XS("Сброс"));
        }


        if (SettingsHeader("##misc_freecam", XS("Фрикам"), XS("Скорость, зоны и кнопка"),
                           false, &g_state.freecam_on, g_state.a_freecam_on)) {
            CardBg(Layout::RowH * 4.f + Layout::SliderH * 3.f);
            ToggleRow("##fc2", XS("Кнопка на экране"), &g_state.freecam_btn_show, g_state.a_freecam_btn_show, false, true);
            SliderRow("##fc1", XS("Скорость"), &g_state.freecam_speed,
                      1.f, 60.f, XS("%.0f м/с"), false, false, g_state.sl_freecam, dt);
            SliderRow("##fclr", XS("Радиус зоны обзора"), &g_state.freecam_look_radius,
                      0.10f, 0.48f, XS("%.2f"), false, false, g_state.sl_freecam_look_radius, dt);
            SliderRow("##fcjs", XS("Размер джойстика"), &g_state.freecam_joy_size,
                      60.f, 200.f, XS("%.0f%%"), false, false, g_state.sl_freecam_joy_size, dt);
            CalibrationRow("##fcjoy4", XS("Настроить джойстик"), 4);
            CalibrationRow("##fclook7", XS("Настроить зону обзора"), 7);
            CalibrationRow("##fcbtn6", XS("Настроить кнопку"), 6);
        }
        if (SettingsHeader("##misc_xray", XS("Иксрей"), XS("Дальность и кнопка"),
                           false, &g_state.xray_on, g_state.a_xray_on)) {
            CardBg(Layout::RowH * 2.f + Layout::SliderH);
            ToggleRow("##xrbtn", XS("Кнопка на экране"), &g_state.xray_btn_show, g_state.a_xray_btn_show, false, true);
            SliderRow("##xr1", XS("Дальность"), &g_state.xray_range,
                      1.f, 50.f, XS("%.0f м"), false, false, g_state.sl_xray, dt);
            CalibrationRow("##xrbtn9", XS("Настроить кнопку"), 9);
        }

        SHdr(XS("Оружие"));
        char weaponSummary[160];
        snprintf(weaponSummary, sizeof(weaponSummary), "%d / 3 · %s",
                 int(vm.no_recoil_button) + int(vm.no_spread) + int(g_state.fast_fire), XS("Отдача, разброс и темп"));
        if (SettingsHeader("##misc_weapon", XS("Оружие"), weaponSummary)) {
            CardBg(Layout::RowH * 3.f);
            const bool oldRecoil = vm.no_recoil_button, oldSpread = vm.no_spread;
            const bool oldFastFire = g_state.fast_fire;
            ToggleRow("##norecoil", XS("Без отдачи"), &vm.no_recoil_button, g_state.a_no_recoil, false, true);
            ToggleRow("##nospread", XS("Без разброса"), &vm.no_spread, g_state.a_no_spread, false);
            ToggleRow("##fastfire", XS("Быстрая стрельба"), &g_state.fast_fire, g_state.a_fast_fire, true);
            if (oldRecoil != vm.no_recoil_button || oldSpread != vm.no_spread || oldFastFire != g_state.fast_fire)
                esp_viewmodel_tick(vm);
        }
        SHdr(XS("Автофарм"));
        if (SettingsHeader("##misc_farm", XS("Автофарм"), XS("Ресурсы, дальность и зона огня"),
                           false, &g_state.farm_on, g_state.a_farm_on)) {
            CardBg(Layout::RowH * 5.f + Layout::SliderH);
            ToggleRow("##farm_wood", XS("Дерево"), &g_state.farm_wood, g_state.a_farm_wood, false, true);
            ToggleRow("##farm_stone", XS("Камень"), &g_state.farm_stone, g_state.a_farm_stone, false);
            ToggleRow("##farm_metal", XS("Металл"), &g_state.farm_metal, g_state.a_farm_metal, false);
            ToggleRow("##farm_sulfur", XS("Сера"), &g_state.farm_sulfur, g_state.a_farm_sulfur, false);
            SliderRow("##farm_range", XS("Искать до"), &g_state.farm_range, 10.f, 300.f,
                      XS("%.0f м"), false, false, g_state.sl_farm_range, dt);
            char fireZone[48];
            if (g_state.farm_fire_x >= 0.f)
                snprintf(fireZone, sizeof(fireZone), "%d%% %d%%", int(g_state.farm_fire_x * 100.f), int(g_state.farm_fire_y * 100.f));
            else snprintf(fireZone, sizeof(fireZone), "%s", XS("Задать"));
            CalibrationRow("##farm_fire2", XS("Зона огня"), 2, fireZone);
            if (g_state.farm_fire_x >= 0.f) {
                const char* const reset[] = {XS("Сброс")};
                int action = -1;
                if (ChoiceRow("##farm_fire_reset", &action, reset, 1)) {
                    g_state.farm_fire_x = g_state.farm_fire_y = -1.f;
                    ShowToast(XS("Зона сброшена"));
                }
            }
        }
        char worldSummary[32];
        snprintf(worldSummary, sizeof(worldSummary), "%d / 3", int(g_state.always_day) + int(g_state.no_grass) + int(vm.fast_loot));
        if (SettingsHeader("##misc_world", XS("Мир и предметы"), worldSummary)) {
            CardBg(Layout::RowH * 3.f);
            ToggleRow("##wd0", XS("Всегда день"), &g_state.always_day, g_state.a_always_day, false, true);
            ToggleRow("##wd1", XS("Без травы"), &g_state.no_grass, g_state.a_no_grass, false);
            ToggleRow("##instantloot", XS("Быстрый подбор"), &vm.fast_loot, g_state.a_fast_loot, true);
        }
        SHdr(XS("Сеть и FPS"));
        if (SettingsHeader("##misc_fakelag", XS("FakeLag"), XS("Интенсивность и кнопка"),
                           false, &g_state.fake_lag, g_state.a_fake_lag)) {
            CardBg(Layout::RowH * 2.f + Layout::SliderH);
            ToggleRow("##flbtn", XS("Кнопка на экране"), &g_state.fakelag_btn_show, g_state.a_fakelag_btn_show, false, true);
            SliderRow("##fakelagpct", XS("Интенсивность"), &g_state.fake_lag_percent,
                      1.f, 100.f, XS("%.0f%%"), false, false, g_state.sl_fake_lag, dt);
            CalibrationRow("##flbtn8", XS("Настроить кнопку FakeLag"), 8);
        }
        char fpsSummary[32];
        if (vm.fps_limit > 0) snprintf(fpsSummary, sizeof(fpsSummary), "%d FPS", vm.fps_limit);
        else snprintf(fpsSummary, sizeof(fpsSummary), "%s", XS("По умолчанию"));
        if (SettingsHeader("##misc_fps", XS("Лимит FPS"), fpsSummary)) {
            const char* const labels[] = {XS("Сброс"), "90", "120", "144"};
            const int limits[] = {0, 90, 120, 144};
            int selected = 0;
            for (int i = 1; i < 4; ++i) if (vm.fps_limit == limits[i]) selected = i;
            if (ChoiceRow("##fps_limit", &selected, labels, 4)) {
                vm.fps_limit = limits[selected];
                esp_viewmodel_tick(vm);
            }
        }

    }

    ImGui::Dummy({1.f, 12.f});
    return ImGui::GetCursorPosY() - sY;
}

static uint32_t g_menu_orient = 0xFFFFFFFFu;
static int g_menu_dw = 0, g_menu_dh = 0;


void CenterMenuOnDisplay() {
    float dw = 0.f, dh = 0.f;
    VisibleScreen(dw, dh);
    if (dw < 100.f || dh < 100.f) return;
    if (g_win.w > dw - 16.f) g_win.w = ImMax(160.f, dw - 16.f);
    if (g_win.h > dh - 16.f) g_win.h = ImMax(160.f, dh - 16.f);
    g_win.pos.x = (dw - g_win.w) * 0.5f;
    g_win.pos.y = (dh - g_win.h) * 0.5f;
    g_win.tpos = g_win.pos;
    g_win.follow.reset(g_win.pos.x, g_win.pos.y);
    g_win.dragging = false;
    g_win.resizing = false;
}



// Названия вкладок — одни и те же в шапке контента и в обеих панелях.
// Русские оригиналы лежат здесь (они же ключи таблицы переводов), а язык
// подставляется на каждом обращении: подпись вкладки нужна и для отрисовки, и
// для расчёта ширины панели, а язык может смениться на ходу — массив со
// строками, собранный один раз при запуске, остался бы на языке запуска.
static const char* const kTabTitlesRu[kTabCount] = {
    XS_RU("Меню"), XS_RU("Аим"), XS_RU("ESP"), XS_RU("Разное"), XS_RU("Конфиги"), XS_RU("Опции")
};
static const char* TabTitle(int k) {
    if (k < 0 || k >= kTabCount) return "";
    return lang::text(kTabTitlesRu[k]);
}

// Ячейка вкладки в левой панели: иконка слева и крупная подпись справа.
// Иконка 60 px — в 1.5 раза больше прежних 40. Подпись — обычным начертанием
// (шрифт в сборке один, полужирного нет и подделывать его мы не будем) в два
// раза крупнее базового кегля панели. Ширина панели считается так, чтобы
// влезли и крупная иконка, и такая подпись (см. RailTabsW).
static const float kRailIcon = 60.f;    // сторона иконки вкладки
static const float kRailFS   = 1.55f;   // кегль подписи от базового (0.88 от кегля меню)
static const float kRailPadL = 14.f;    // отступ иконки от края панели
static const float kRailPadR = 10.f;    // отступ текста от края панели
static const float kRailGap  = 12.f;    // зазор между иконкой и подписью

// Сколько места нужно ячейкам левой панели: отступ + иконка + зазор + самая
// длинная подпись (вкладка 0 «Меню» не показывается, она не в счёт) + отступ.
static float RailTabsW() {
    const float fs = ImGui::GetFontSize() * 0.88f * kRailFS;
    float worst = 0.f;
    for (int k = 1; k < kTabCount; ++k)
        worst = ImMax(worst, MeasureUiTextScaled(TabTitle(k), fs).x + 4.f);
    return kRailPadL + kRailIcon + kRailGap + worst + kRailPadR;
}

// Метрики левой панели.
//
// Кружок-аватарка убран: его прежнее место пока зарезервировано, чтобы
// ширина панели вкладок не менялась.
static const float kAvatarR    = 87.75f; // радиус кружка (диаметр 175.5)
static const float kAvatarPad  = 20.f;   // отступ по бокам внутри панели

// Ширина левой панели вкладок: прежний резерв под аватарку сохранён,
// чтобы не менять геометрию меню при удалении декоративных кнопок.
static float AvatarRailW(float winW) {
    const float forAvatar = (kAvatarR + kAvatarPad) * 2.f;
    return ImMin(ImMax(forAvatar, RailTabsW()), ImMax(120.f, winW * 0.45f));
}

// Полоса шапки контента содержит только заголовок вкладки.
static float MenuHeaderH(bool) { return Layout::HeaderH; }

// ====================== Фрикам: управление полётом ========================
//
// Пока функция включена, поверх игры лежат три независимых органа управления,
// каждый со СВОИМ пальцем:
//
//   * джойстик слева (вперёд/назад, влево/вправо),
//   * свайп-зона обзора справа (куда смотрит камера),
//   * две кнопки высоты у правого края (вверх/вниз).
//
// Палец, коснувшийся зоны, ПРИВЯЗЫВАЕТСЯ к ней по id касания и ведётся до
// отпускания, где бы он ни оказался: уехавший за круг джойстика палец стик не
// роняет, а даёт полное отклонение в свою сторону. Обзор при этом живёт на
// другом пальце и джойстику не мешает (и наоборот).
//
// Тач-слой отдаёт все пальцы сразу (Touch_GetActive); ImGui MousePos видит
// только последний, поэтому для фрикама он не используется — только как
// запасной путь, когда тачскрина нет вовсе (хост/эмулятор).
//
// Только касания, начавшиеся внутри кругов управления, не уходят в игру:
// иначе палец на джойстике ещё и ходил бы персонажем. За кругами игровые
// кнопки остаются доступными даже при включённом фрикаме.
//
// Геометрия — в пикселях видимого экрана (VisibleScreen, как пилюли и меню),
// а не в квадрате ImGui DisplaySize: иначе привязка к правому/нижнему краю
// (кнопки высоты) уехала бы за видимую область.
static float  s_fc_retry = 0.f;
static bool   s_fc_toasted = false;

// Какой палец что держит (-1 = свободно). Id — из Touch_GetActive, живёт от
// касания до отпускания; s_fc_prev_* отличают НОВЫЕ касания от удерживаемых,
// чтобы скользнувший из одной зоны в другую палец не перехватывался.
static int   s_fc_joy_id = -1;
static int   s_fc_look_id = -1;
static float s_fc_look_x = 0.f, s_fc_look_y = 0.f;
static int   s_fc_up_id = -1, s_fc_down_id = -1;
static int   s_fc_prev_ids[12];
static int   s_fc_prev_n = 0;
// Радиус стика фрикама: база 130 px на короткой стороне 1080, дальше —
// пропорциональный масштаб под экран (S) и пользовательский размер из
// «Разное → Фрикам» (60..200%, 100 = прежние 130 px). Одно место для
// полёта и для превью в калибровке — круг не разъедется с хит-тестами.
static float FreecamJoyRadius(float vw, float vh) {
    const float S = ImClamp(ImMin(vw, vh) / 1080.f, 0.6f, 2.5f);
    const float scale = ImClamp(g_state.freecam_joy_size, 60.f, 200.f) / 100.f;
    return 130.f * S * scale;
}

// Грейс сорванного пальца: снимок Touch_GetActive иногда не видит удерживаемый
// палец один-два кадра (тач-поток пишет состояние без лока — см. TypeA), и без
// грейса захват на этих кадрах отпускался и тут же подхватывался заново: рывки
// «стоп/дальше» у движения и куски у обзора. Держим захват kFcGrace кадров на
// последнем положении; по-настоящему поднятый палец отпускает как раньше.
static int   s_fc_joy_miss = 0, s_fc_look_miss = 0;
static int   s_fc_up_miss = 0, s_fc_down_miss = 0;
static const int kFcGrace = 3;
static float s_fc_joy_gx = 0.f, s_fc_joy_gy = 0.f;   // стик на грейсе

// Здоровье ввода за секунду (строка «фрикам: ввод» в логе): кадры, самый
// длинный кадр, сбросы захватов, спасения грейсом, отбросы дельты обзора.
static int    s_fc_in_frames = 0;
static float  s_fc_in_maxdt = 0.f;
static int    s_fc_in_joyfree = 0, s_fc_in_lookfree = 0;
static int    s_fc_in_grace = 0, s_fc_in_lookdrop = 0;
static double s_fc_in_last = -1e9;

static void Freecam_ResetGrabs() {
    s_fc_joy_id = s_fc_look_id = s_fc_up_id = s_fc_down_id = -1;
    s_fc_joy_miss = s_fc_look_miss = s_fc_up_miss = s_fc_down_miss = 0;
    s_fc_prev_n = 0;
}

void UpdateFreecam(float dt) {
    if (g_state.freecam_on != esp_freecam_active()) {
        s_fc_retry -= dt;
        if (s_fc_retry <= 0.f) {
            s_fc_retry = 0.5f;
            if (esp_freecam_set(g_state.freecam_on)) {
                s_fc_toasted = false;
                if (g_state.freecam_on) ShowToast(XS("Фрикам включён"));
            } else if (g_state.freecam_on && !s_fc_toasted) {
                s_fc_toasted = true;
                ShowToast(XS("Фрикам: камера не найдена"));
            }
        }
    } else {
        s_fc_retry = 0.f;
        s_fc_toasted = false;
    }
    // Геометрия — видимый экран (как меню и пилюли), а не квадрат DisplaySize.
    float vw = 0.f, vh = 0.f;
    VisibleScreen(vw, vh);
    if (vw < 100.f || vh < 100.f) return;

    {
        static double s_last = -1e9;
        double now = ImGui::GetTime();
        if (now - s_last >= 5.0) {
            s_last = now;
        }
    }
    // Здоровье ввода за секунду полёта: видно просадки кадров (макс dt),
    // моргания захватов (сбросы/грейс) и заморозки обзора (отбросы дельты).
    if (esp_freecam_active()) {
        ++s_fc_in_frames;
        if (dt > s_fc_in_maxdt) s_fc_in_maxdt = dt;
        const double nowIn = ImGui::GetTime();
        if (nowIn - s_fc_in_last >= 1.0) {
            s_fc_in_last = nowIn;
            s_fc_in_frames = 0; s_fc_in_maxdt = 0.f;
            s_fc_in_joyfree = s_fc_in_lookfree = s_fc_in_grace = s_fc_in_lookdrop = 0;
        }
    }

    if (!esp_freecam_active()) {
        Touch_BlockRect(false, 0.f, 0.f, 0.f, 0.f);
        Freecam_ResetGrabs();   // залипший id не должен уехать в следующий полёт
        return;
    }

    // ---- геометрия -------------------------------------------------------
    // Масштаб тач-зоны под экран: база — короткая сторона 1080 px (на ней все
    // размеры как были), на остальных экранах — пропорционально. Иначе на
    // планшете стик с кнопками крошечные, а на маленьком экране — полдисплея.
    const float S = ImClamp(ImMin(vw, vh) / 1080.f, 0.6f, 2.5f);
    const float R = FreecamJoyRadius(vw, vh);   // ход стика (= радиус круглого основания)
    float cx, cy;                          // центр джойстика
    if (g_state.freecam_joy_x >= 0.f && g_state.freecam_joy_y >= 0.f) {
        cx = g_state.freecam_joy_x * vw;
        cy = g_state.freecam_joy_y * vh;
    } else {
        cx = 30.f * S + R;
        cy = 100.f * S + R;
    }
    // Центр не даём увести за край и под верхнюю полосу (там пилюли меню).
    cx = ImClamp(cx, R + 16.f * S, vw - R - 16.f * S);
    cy = ImClamp(cy, R + 100.f * S, vh - R - 16.f * S);

    // Отдельная круглая зона обзора. Центр настраивается тем же экраном,
    // что и джойстик; радиус хранится как доля короткой стороны.
    float lookCx = (g_state.freecam_look_x >= 0.f ? g_state.freecam_look_x : 0.72f) * vw;
    float lookCy = (g_state.freecam_look_y >= 0.f ? g_state.freecam_look_y : 0.50f) * vh;
    const float lookFrac = ImClamp(g_state.freecam_look_radius, 0.10f, 0.48f);
    const float lookR = lookFrac * ImMin(vw, vh);
    lookCx = ImClamp(lookCx, lookR + 8.f * S, vw - lookR - 8.f * S);
    lookCy = ImClamp(lookCy, lookR + 90.f * S, vh - lookR - 8.f * S);

    const float kBtnR  = 62.f * S;          // кнопки высоты у правого края
    const float kBtnX  = vw - kBtnR - 30.f * S;
    const float kUpY   = vh * 0.5f - kBtnR - 26.f * S;
    const float kDownY = vh * 0.5f + kBtnR + 26.f * S;

    // Забираем только сами органы управления. Касания, начавшиеся за кругами,
    // продолжают уходить в игру — во фрикаме можно нажимать хотбар, инвентарь
    // и прочие кнопки, даже если они находятся рядом с зоной обзора.
    const float blockX[4] = {cx, lookCx, kBtnX, kBtnX};
    const float blockY[4] = {cy, lookCy, kUpY, kDownY};
    const float blockR[4] = {R + 70.f * S, lookR, kBtnR + 20.f * S, kBtnR + 20.f * S};
    Touch_BlockCircles(true, blockX, blockY, blockR, 4);

    // ---- снимок пальцев ----------------------------------------------------
    float f_x[12], f_y[12];
    int   f_id[12];
    int nf = Touch_GetActive(f_x, f_y, f_id, 12);
    if (nf <= 0 && ImGui::GetIO().MouseDown[0]) {
        // Запасной путь без тачскрина (хост/эмулятор): один «палец»-мышь.
        f_x[0] = ImGui::GetIO().MousePos.x;
        f_y[0] = ImGui::GetIO().MousePos.y;
        f_id[0] = -1000000;
        nf = 1;
    }
    auto findTouch = [&](int id, float& ox, float& oy) -> bool {
        for (int i = 0; i < nf; ++i)
            if (f_id[i] == id) { ox = f_x[i]; oy = f_y[i]; return true; }
        return false;
    };
    auto isFreshTouch = [&](int id) -> bool {
        for (int i = 0; i < s_fc_prev_n; ++i)
            if (s_fc_prev_ids[i] == id) return false;
        return true;
    };
    // Зону занимает только свежее касание ниже верхней полосы, которое ещё
    // никто не держит. Пока открыто меню, новых захватов нет вовсе: тапы по
    // меню не должны дёргать камеру (уже зажатое продолжает вестись).
    const bool canGrab = !menu_open;

    float ax = 0.f, ay = 0.f;              // оси стика (-1..1)
    float knobDx = 0.f, knobDy = 0.f;      // ручка для отрисовки
    bool  joyHeld = false;

    // ---- джойстик: ведём привязанный палец ---------------------------------
    auto stickVec = [&](float px, float py) {
        const float dx = px - cx, dy = py - cy;
        const float len = sqrtf(dx * dx + dy * dy);
        const float dead = R * (12.f / 130.f);  // мёртвая зона — та же доля стика, что и раньше
        if (len > dead) {
            float mag = (len - dead) / (R - dead);
            if (mag > 1.f) mag = 1.f;
            // Отклик мягче у центра — точнее доводка, резче на краю.
            mag = mag * (0.35f + 0.65f * mag);
            ax = (dx / len) * mag;
            ay = -(dy / len) * mag;
        } else {
            ax = ay = 0.f;
        }
        const float cl = (len > R) ? R / len : 1.f;
        knobDx = dx * cl;
        knobDy = dy * cl;
    };
    if (s_fc_joy_id != -1) {
        float px, py;
        if (findTouch(s_fc_joy_id, px, py)) {
            if (s_fc_joy_miss > 0) { s_fc_joy_miss = 0; ++s_fc_in_grace; }
            joyHeld = true;
            s_fc_joy_gx = px; s_fc_joy_gy = py;
            stickVec(px, py);
        } else if (++s_fc_joy_miss <= kFcGrace) {
            // Палец моргнул в снимке: держим последнее отклонение, движение
            // не останавливается.
            joyHeld = true;
            stickVec(s_fc_joy_gx, s_fc_joy_gy);
        } else {
            s_fc_joy_id = -1; s_fc_joy_miss = 0;   // палец поднят — стик свободен
            ++s_fc_in_joyfree;
        }
    }
    if (s_fc_joy_id == -1 && canGrab && nf > 0) {
        // Захват щедрый (круг шире видимого), а дальше палец ведётся где
        // угодно: вышедший за круг даёт полное отклонение в свою сторону.
        const float grab = R + 70.f * S;
        for (int i = 0; i < nf; ++i) {
            if (f_id[i] == s_fc_look_id || f_id[i] == s_fc_up_id || f_id[i] == s_fc_down_id) continue;
            if (!isFreshTouch(f_id[i]) || f_y[i] < 90.f) continue;
            const float dx = f_x[i] - cx, dy = f_y[i] - cy;
            if (dx * dx + dy * dy <= grab * grab) {
                s_fc_joy_id = f_id[i];
                s_fc_joy_miss = 0;
                s_fc_joy_gx = f_x[i]; s_fc_joy_gy = f_y[i];
                joyHeld = true;
                stickVec(f_x[i], f_y[i]);
                break;
            }
        }
    }

    // ---- кнопки высоты ------------------------------------------------------
    bool upHeld = false, downHeld = false;
    auto holdButton = [&](int& slot, int& miss, float bx, float by, bool& held) {
        if (slot == -1) return;
        float px, py;
        if (!findTouch(slot, px, py)) {
            // Моргание снимка: держим кнопку на грейсе, как стик.
            if (++miss <= kFcGrace) { held = true; return; }
            slot = -1; miss = 0; return;
        }
        if (miss > 0) { miss = 0; ++s_fc_in_grace; }
        // Держится, пока палец в расширенном круге: съехавший чуть вбок
        // палец кнопку не роняет.
        const float dx = px - bx, dy = py - by;
        const float keep = kBtnR + 60.f * S;
        if (dx * dx + dy * dy <= keep * keep) held = true;
        else { slot = -1; miss = 0; }   // съехал далеко — отпустил сам
    };
    holdButton(s_fc_up_id, s_fc_up_miss, kBtnX, kUpY, upHeld);
    holdButton(s_fc_down_id, s_fc_down_miss, kBtnX, kDownY, downHeld);
    if (canGrab && nf > 0 && (s_fc_up_id == -1 || s_fc_down_id == -1)) {
        const float grab = kBtnR + 20.f * S;
        for (int i = 0; i < nf; ++i) {
            if (f_id[i] == s_fc_joy_id || f_id[i] == s_fc_look_id) continue;
            if (!isFreshTouch(f_id[i]) || f_y[i] < 90.f) continue;
            const float dx = f_x[i] - kBtnX;
            const float du = f_y[i] - kUpY, dd = f_y[i] - kDownY;
            if (s_fc_up_id == -1 && dx * dx + du * du <= grab * grab) {
                s_fc_up_id = f_id[i]; s_fc_up_miss = 0; upHeld = true; continue;
            }
            if (s_fc_down_id == -1 && dx * dx + dd * dd <= grab * grab) {
                s_fc_down_id = f_id[i]; s_fc_down_miss = 0; downHeld = true; continue;
            }
        }
    }
    const float hAxis = (upHeld ? 1.f : 0.f) - (downHeld ? 1.f : 0.f);

    // ---- обзор: свайп на своём пальце ---------------------------------------
    float yaw_delta = 0.f, pitch_delta = 0.f;
    bool lookHeld = false;
    float lookPx = 0.f, lookPy = 0.f;
    if (s_fc_look_id != -1) {
        float px, py;
        if (findTouch(s_fc_look_id, px, py)) {
            if (s_fc_look_miss > 0) { s_fc_look_miss = 0; ++s_fc_in_grace; }
            lookHeld = true; lookPx = px; lookPy = py;
            const float dx = px - s_fc_look_x, dy = py - s_fc_look_y;
            // Скачок больше четверти экрана за кадр — не свайп, а смена слота:
            // камеру он дёргать не должен. Но после пропуска кадров (просадка,
            // грейс) палец за один апдейт честно проезжает больше, и статичный
            // порог замораживал бы обзор на каждом пропуске. Поэтому порог
            // растёт со временем кадра (взбесившийся палец всё равно быстрее
            // 4000 px/с не бывает), а сверху прибит, чтобы смена слота после
            // долгой паузы не крутила камеру.
            const float jump = fminf(fmaxf(250.f * S, 4000.f * S * dt), 600.f * S);
            if (fabsf(dx) < jump && fabsf(dy) < jump) {
                const float sens = 0.22f / S;   // градусы на пиксель: чем плотнее экран, тем меньше
                yaw_delta = dx * sens;
                pitch_delta = -dy * sens;
            } else {
                ++s_fc_in_lookdrop;
            }
            s_fc_look_x = px; s_fc_look_y = py;
        } else if (++s_fc_look_miss <= kFcGrace) {
            // Якорь НЕ двигаем: движение за пропуск накопится в следующую
            // дельту, обзор не встанет.
            lookHeld = true; lookPx = s_fc_look_x; lookPy = s_fc_look_y;
        } else {
            s_fc_look_id = -1; s_fc_look_miss = 0;
            ++s_fc_in_lookfree;
        }
    }
    if (s_fc_look_id == -1 && canGrab && nf > 0) {
        const float keepOut = R + 40.f * S; // рядом со стиком обзор не стартует
        const float btnOut = kBtnR + 20.f * S;
        for (int i = 0; i < nf; ++i) {
            if (f_id[i] == s_fc_joy_id || f_id[i] == s_fc_up_id || f_id[i] == s_fc_down_id) continue;
            if (!isFreshTouch(f_id[i]) || f_y[i] < 90.f) continue;
            const float lx = f_x[i] - lookCx, ly = f_y[i] - lookCy;
            if (lx * lx + ly * ly > lookR * lookR) continue;
            const float jx = f_x[i] - cx, jy = f_y[i] - cy;
            if (jx * jx + jy * jy <= keepOut * keepOut) continue;
            const float bx = f_x[i] - kBtnX;
            const float bu = f_y[i] - kUpY, bd = f_y[i] - kDownY;
            if (bx * bx + bu * bu <= btnOut * btnOut) continue;
            if (bx * bx + bd * bd <= btnOut * btnOut) continue;
            s_fc_look_id = f_id[i];
            s_fc_look_miss = 0;
            s_fc_look_x = f_x[i]; s_fc_look_y = f_y[i];
            lookHeld = true; lookPx = f_x[i]; lookPy = f_y[i];
            break;
        }
    }

    // Запоминаем пальцы кадра — в следующем отличим новые касания.
    s_fc_prev_n = (nf > 12) ? 12 : nf;
    for (int i = 0; i < s_fc_prev_n; ++i) s_fc_prev_ids[i] = f_id[i];

    // ---- движение -----------------------------------------------------------
    if (dt > 0.25f) dt = 0.25f;            // первый кадр не должен телепортить; просадки до 4 к/с скорость не режут
    float speed = g_state.freecam_speed;
    if (!(speed >= 1.f)) speed = 1.f;
    if (speed > 60.f) speed = 60.f;
    const float step = speed * dt;
    if (ax != 0.f || ay != 0.f || hAxis != 0.f)
        esp_freecam_move(ay * step, ax * step, hAxis * step);
    if (yaw_delta != 0.f || pitch_delta != 0.f)
        esp_freecam_look(yaw_delta, pitch_delta);

    // ---- отрисовка: прямо на foreground, без окон ImGui ----------------------
    // Окна с InvisibleButton здесь не нужны (хит-тесты свои, по пальцам) и
    // вредны: невидимая кнопка обзора на пол-экрана висела бы поверх меню.
    auto* fg = ImGui::GetForegroundDrawList();
    // Граница зоны обзора видна, но не перекрывает игру. После захвата палец
    // остаётся привязан до отпускания, даже если свайп вышел за круг.
    fg->AddCircleFilled({lookCx, lookCy}, lookR,
                        lookHeld ? IM_COL32(0,180,255,24) : IM_COL32(20,20,20,14), 72);
    fg->AddCircle({lookCx, lookCy}, lookR,
                  lookHeld ? IM_COL32(0,200,255,150) : IM_COL32(255,255,255,55),
                  72, 2.f);
    {
        // Основание круглое (залитый круг + кольцо), без квадратной рамки.
        fg->AddCircleFilled({cx, cy}, R, IM_COL32(20,20,20,200), 64);
        fg->AddCircle({cx, cy}, R,
                      joyHeld ? IM_COL32(0,200,255,180) : IM_COL32(255,255,255,120),
                      64, 3.f);
        fg->AddLine({cx - R*0.15f, cy}, {cx + R*0.15f, cy}, IM_COL32(255,255,255,60), 1.f);
        fg->AddLine({cx, cy - R*0.15f}, {cx, cy + R*0.15f}, IM_COL32(255,255,255,60), 1.f);
        // Ручка крупнее: 0.42R вместо 0.34R (при R=130 — радиус ~55 px).
        fg->AddCircleFilled({cx + knobDx, cy + knobDy}, R * 0.42f,
                            joyHeld ? IM_COL32(0,200,255,255) : IM_COL32(0,180,255,200), 48);
    }
    auto drawHeightBtn = [&](float bx, float by, bool held, bool isUp) {
        fg->AddCircleFilled({bx, by}, kBtnR,
                            held ? IM_COL32(0,180,255,230) : IM_COL32(20,20,20,200), 40);
        fg->AddCircle({bx, by}, kBtnR,
                      held ? IM_COL32(255,255,255,220) : IM_COL32(255,255,255,80),
                      40, 2.5f);
        const float s = 22.f * S;
        ImVec2 p1, p2, p3;
        if (isUp) { p1 = {bx, by - s}; p2 = {bx - s * 0.9f, by + s * 0.6f}; p3 = {bx + s * 0.9f, by + s * 0.6f}; }
        else      { p1 = {bx, by + s}; p2 = {bx - s * 0.9f, by - s * 0.6f}; p3 = {bx + s * 0.9f, by - s * 0.6f}; }
        fg->AddTriangleFilled(p1, p2, p3, IM_COL32(255,255,255,230));
    };
    drawHeightBtn(kBtnX, kUpY, upHeld, true);
    drawHeightBtn(kBtnX, kDownY, downHeld, false);
    if (lookHeld) {
        // Метка пальца обзора — видно, какой палец крутит камеру.
        fg->AddCircle({lookPx, lookPy}, 34.f * S, IM_COL32(0,200,255,160), 32, 2.5f);
        fg->AddCircleFilled({lookPx, lookPy}, 6.f * S, IM_COL32(0,200,255,160), 20);
    }
}

// Состояние -> тема (каждый кадр: дёшево, а палитра пишет цвет непрерывно,
// пока её тянут) + сохранение с дебаунсом, чтобы не долбить файл.
// Пресеты kAccentCols живут выше, перед TabContent: их же рисует секция
// «Цвет интерфейса» на вкладке опций.
static void SyncAccentToTheme() {
    int idx = g_state.ui_accent_idx;
    if (idx < 0 || idx > kAccentCustom) { idx = 0; g_state.ui_accent_idx = 0; }
    if (idx == kAccentCustom) {
        g_accent = g_state.ui_accent_rgb; g_accent.w = 1.f; g_accentOn = true;
    } else if (idx == 0) {
        g_accentOn = false;
    } else {
        g_accent = kAccentCols[idx]; g_accentOn = true;
    }
}

// RAII-скобка на кадр появления: подменяет прямоугольник окна анимированным,
// а в конце кадра (включая ранние return) возвращает настоящий. Сама кривая —
// готовая из интернета (anim_web.h), это только подмена-и-возврат.
struct WinRectAnim {
    WindowState* w = nullptr;
    ImVec2 p = {0, 0};
    float sw = 0.f, sh = 0.f;
    bool armed = false;
    void arm(WindowState& ws, ImVec2 ap, float aw, float ah) {
        w = &ws; p = ws.pos; sw = ws.w; sh = ws.h;
        ws.pos = ap; ws.w = aw; ws.h = ah;
        armed = true;
    }
    ~WinRectAnim() { if (armed && w) { w->pos = p; w->w = sw; w->h = sh; } }
};

void RenderMenu() {
    // Метка до всего остального: пока в CRASH висит «menu: popover» (последняя
    // метка прошлого кадра), падение в голове функции — SyncAccentToTheme,
    // CfgWatchTick (inotify + опендир конфигов) или блок Tick.
    auto& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    if (dt > 0.1f) dt = 0.1f;
    SyncAccentToTheme();   // акцент палитры -> тема (и дебаунс-сохранение)

    if (g_menu_orient != displayInfo.orientation || g_menu_dw != (int)displayInfo.width || g_menu_dh != (int)displayInfo.height) {
        g_menu_orient = displayInfo.orientation;
        g_menu_dw = (int)displayInfo.width;
        g_menu_dh = (int)displayInfo.height;
        CenterMenuOnDisplay();
    }

    g_menuFadeIn += dt * 1.2f;
    if (g_menuFadeIn > 1.f) g_menuFadeIn = 1.f;

    CfgWatchTick();
    cfg::aim::vis_check = g_state.aim_pos;
    Tick(g_themeT, g_darkTheme, dt, 6.f);
    Tick(g_state.a_aim_touch,  g_state.aim_touch,          dt);
    Tick(g_state.a_aim_pos,    g_state.aim_pos,            dt);
    Tick(g_state.a_esp_vis,    cfg::esp::vis_check,        dt);
    Tick(g_state.a_aim_head,   g_state.aim_bone == 0,      dt);
    Tick(g_state.a_aim_chest,  g_state.aim_bone == 1,      dt);
    Tick(g_state.a_aim_pelvis, g_state.aim_bone == 2,      dt);
    Tick(g_state.a_aim_spec,   g_state.aim_special,        dt);
    Tick(g_state.a_aim_scope,  g_state.aim_scope_only,     dt);
    Tick(g_state.a_aim_fire,   g_state.aim_fire_only,      dt);
    Tick(g_state.a_aim_learn,  aimlead::state().enabled,   dt);
    Tick(g_state.a_aim_trace,  aimtrace::enabled(),        dt);
    Tick(g_state.a_silent_zone, g_state.silent_auto_zone,  dt);
    Tick(g_state.a_esp_box,    g_state.esp_box,            dt);
    Tick(g_state.a_esp_name,   g_state.esp_name,           dt);
    Tick(g_state.a_esp_wall,   g_state.esp_wall,           dt);
    Tick(g_state.a_esp_chams,  g_state.esp_chams,          dt);
    Tick(g_state.a_esp_weapon, g_state.esp_weapon,         dt);
    Tick(g_state.a_esp_ore,    g_state.esp_ore,            dt);
    Tick(g_state.a_esp_ore_stone,  g_state.esp_ore_stone,  dt);
    Tick(g_state.a_esp_ore_iron,   g_state.esp_ore_iron,   dt);
    Tick(g_state.a_esp_ore_sulfur, g_state.esp_ore_sulfur, dt);
    Tick(g_state.a_esp_animal, g_state.esp_animal,         dt);
    Tick(g_state.a_esp_loot,   g_state.esp_loot,           dt);
    Tick(g_state.a_esp_pickup, g_state.esp_pickup,         dt);
    Tick(g_state.a_esp_bots,   g_state.esp_bots,           dt);
    Tick(g_state.a_esp_arrows, g_state.esp_arrows,         dt);
    Tick(g_state.a_esp_radar,  g_state.esp_radar,          dt);
    Tick(g_state.a_esp_maphack, g_state.esp_maphack,       dt);
    Tick(g_state.a_esp_arrows_off, g_state.esp_arrows_offscreen, dt);
    Tick(g_state.a_esp_arrows_dist, g_state.esp_arrows_dist, dt);
    Tick(g_state.a_esp_vehicles, g_state.esp_vehicles,     dt);
    Tick(g_state.a_esp_scrap,  g_state.esp_scrap,          dt);
    Tick(g_state.a_esp_house_boxes, g_state.esp_house_boxes, dt);
    Tick(g_state.a_esp_turrets, g_state.esp_turrets,       dt);
    Tick(g_state.a_esp_backpacks, g_state.esp_backpacks,   dt);
    Tick(g_state.a_esp_sleepers, g_state.esp_sleepers,     dt);
    Tick(g_state.a_esp_cupboards, g_state.esp_cupboards,   dt);
    Tick(g_state.a_esp_pumpkins, g_state.esp_pumpkins,     dt);
    Tick(g_state.a_esp_armor,    g_state.esp_armor,        dt);
    Tick(g_state.a_always_day,  g_state.always_day,         dt);
    Tick(g_state.a_no_grass,    g_state.no_grass,            dt);
    Tick(g_state.a_esp_team,   g_state.esp_team,           dt);
    Tick(g_state.a_aim_pr0,    g_state.aim_priority == 0,  dt);
    Tick(g_state.a_aim_pr1,    g_state.aim_priority == 1,  dt);
    Tick(g_state.a_aim_pr2,    g_state.aim_priority == 2,  dt);
    Tick(g_state.a_aim_mode0,  g_state.aim_mode == AIM_MODE_TOUCH,  dt);
    Tick(g_state.a_aim_mode1,  g_state.aim_mode == AIM_MODE_MEMORY, dt);
    Tick(g_state.a_aim_mode2,  g_state.aim_mode == AIM_MODE_SILENT, dt);
    Tick(g_state.a_esp_tracer, g_state.esp_tracer,         dt);
    Tick(g_state.a_esp_skeleton, g_state.esp_skeleton,     dt);
    Tick(g_state.a_ui_dark,    g_state.ui_dark_mode,       dt);
    Tick(g_state.a_farm_on,     g_state.farm_on,     dt);
    Tick(g_state.a_farm_wood,   g_state.farm_wood,   dt);
    Tick(g_state.a_farm_stone,  g_state.farm_stone,  dt);
    Tick(g_state.a_farm_metal,  g_state.farm_metal,  dt);
    Tick(g_state.a_farm_sulfur, g_state.farm_sulfur, dt);
    Tick(g_state.a_custom_fov, g_state.viewmodel.fov_enabled, dt);
    Tick(g_state.a_custom_hands, g_state.viewmodel.hands_enabled, dt);
    Tick(g_state.a_no_recoil, g_state.viewmodel.no_recoil_button, dt);
    Tick(g_state.a_no_spread, g_state.viewmodel.no_spread, dt);
    Tick(g_state.a_no_ballistics, g_state.viewmodel.no_ballistics, dt);
    Tick(g_state.a_fast_loot, g_state.viewmodel.fast_loot, dt);
    Tick(g_state.a_fast_fire, g_state.fast_fire, dt);
    Tick(g_state.a_fast_reload, g_state.fast_reload, dt);
    Tick(g_state.a_fake_lag, g_state.fake_lag, dt);
    Tick(g_state.a_fps_90,  g_state.viewmodel.fps_limit == 90,  dt);
    Tick(g_state.a_fps_120, g_state.viewmodel.fps_limit == 120, dt);
    Tick(g_state.a_fps_144, g_state.viewmodel.fps_limit == 144, dt);
    Tick(g_state.a_fps_rst, g_state.viewmodel.fps_limit == 0,   dt);
    Tick(g_state.a_xray_on,     g_state.xray_on,     dt);
    Tick(g_state.a_xray_btn_show, g_state.xray_btn_show, dt);
    Tick(g_state.a_freecam_on,  g_state.freecam_on,  dt);
    Tick(g_state.a_freecam_btn_show, g_state.freecam_btn_show, dt);
    Tick(g_state.a_fakelag_btn_show, g_state.fakelag_btn_show, dt);
    ApplyTheme();

    // Держим cfg::aim и cfg::esp синхронными с g_state на каждом кадре, а не
    // только при открытии вкладок 1 и 2 (иначе после старта/загрузки конфига
    // до первого захода во вкладку аим/ЕСП читают устаревшие зеркала).
    cfg::aim::enabled       = g_state.aim_touch;
    cfg::aim::vis_check     = g_state.aim_pos;
    cfg::aim::draw_fov      = g_state.aim_special;
    cfg::aim::scope_only    = g_state.aim_scope_only;
    cfg::aim::fov           = g_state.gun_fov;
    cfg::aim::smoothness    = g_state.gun_str;
    cfg::aim::bone          = g_state.aim_bone;
    cfg::aim::trigger_delay = g_state.gun_trigger_delay;
    cfg::aim::fire_only     = g_state.aim_fire_only;

    cfg::esp::box           = g_state.esp_box;
    cfg::esp::name_esp      = g_state.esp_name;
    cfg::esp::distance      = g_state.esp_wall;
    cfg::esp::weapon        = g_state.esp_weapon;
    cfg::esp::tracer        = g_state.esp_tracer;
    cfg::esp::skeleton      = g_state.esp_skeleton;
    cfg::esp::ore           = g_state.esp_ore;
    cfg::esp::animal        = g_state.esp_animal;
    cfg::esp::loot          = g_state.esp_loot;
    cfg::esp::team          = g_state.esp_team;
    cfg::esp::pickup        = g_state.esp_pickup;
    cfg::esp::bot           = g_state.esp_bots;
    cfg::esp::vehicle       = g_state.esp_vehicles;
    cfg::esp::scrap         = g_state.esp_scrap;
    cfg::esp::pumpkin       = g_state.esp_pumpkins;

    if (g_cfgLoadedIdx >= 0 && g_cfgLoadedIdx < kMaxConfigs)
        g_cfgLoadAnim[g_cfgLoadedIdx] += (1.f - g_cfgLoadAnim[g_cfgLoadedIdx]) * 10.f * dt;

    bool anyOverlayOpen = g_sheet.visible || (g_pop.visible && !g_pop.closing);

    if (io.MouseClicked[0] && anyOverlayOpen) g_input.touchConsumed = true;

    bool anyOverlayVisible = g_sheet.visible || g_pop.visible;
    if (!io.MouseDown[0] && !anyOverlayVisible) g_input.touchConsumed = false;

    // Худ отдельной стадией: пилюля счётчика зовёт FrameBoxes и скан ботов
    // (esp_nearby_bot_count), и падение в них в логе выглядело как stage=menu.
    // Лог 15:35:17: SIGABRT на render-потоке, stage=menu, note=- — игра была
    // не привязана, поэтому ни одной заметки не возникло, и внутри меню
    // локализовать негде. Метки идут каждый кадр и перекрывают друг друга:
    // в CRASH видна последняя, т.е. секция, в которой мы упали.
    DrawWatermark(dt);
    DrawToast(dt);

    // ---- Режим калибровки зон вводом с экрана ------------------------------
    // Меню спрятано; первый тап по экрану записывает позицию (в долях экрана):
    // зону джойстика/огня автофарма, точку, из которой аимбот водит палец,
    // центр джойстика фрикама (4) или счётчика игроков (5).
    if (g_calibMode != 0) {
        float dw = 0.f, dh = 0.f;
        VisibleScreen(dw, dh);
        if (dw < 100.f || dh < 100.f) { g_calibMode = 0; return; }
        Touch_AddBlockRect(0.f, 0.f, dw, dh);
        auto* fg = ImGui::GetForegroundDrawList();

        // Затемнение + рамка-акцент.
        fg->AddRectFilled({0, 0}, {dw, dh}, IM_COL32(0, 0, 0, 90));
        fg->AddRect({4.f, 4.f}, {dw - 4.f, dh - 4.f}, C::UA(C::Acc(), 0.9f), 10.f, 0, 3.f);

        // Подпись, что тапать.
        auto* fn = ImGui::GetFont();
        // Режим 1 (зона джойстика движения) снят: стик ходит через память
        // игры, калибровать больше нечего — кнопка огня осталась (2).
        const char* title = (g_calibMode == 2)
            ? XS("Тапни по кнопке огня / атаки")
            : (g_calibMode == 4) ? XS("Тапни куда поставить джойстик фрикама")
            : (g_calibMode == 5) ? XS("Тапни куда поставить счётчик игроков")
            : (g_calibMode == 6) ? XS("Тапни куда поставить кнопку фрикама")
            : (g_calibMode == 7) ? XS("Тапни куда поставить зону обзора")
            : (g_calibMode == 8) ? XS("Тапни куда поставить кнопку FakeLag")
            : (g_calibMode == 9) ? XS("Тапни куда поставить кнопку Иксрей")
                                 : XS("Тапни по точке, где аим водит палец");
        const char* sub = XS("Тап записывает зону. Меню откроется само.");
        float tfs = ImGui::GetFontSize() * 1.5f;
        auto tsz = fn->CalcTextSizeA(tfs, FLT_MAX, 0, title);
        auto ssz = fn->CalcTextSizeA(tfs * 0.62f, FLT_MAX, 0, sub);
        float ty = dh * 0.16f;
        // Плашка под текстом, чтобы читалось на любом фоне.
        float px0 = (dw - ImMax(tsz.x, ssz.x)) * 0.5f - 26.f;
        float px1 = (dw + ImMax(tsz.x, ssz.x)) * 0.5f + 26.f;
        fg->AddRectFilled({px0, ty - 18.f}, {px1, ty + tsz.y + 10.f + ssz.y + 18.f},
                          C::UA(C::Card(), 0.92f), 18.f);
        fg->AddText(fn, tfs, {(dw - tsz.x) * 0.5f, ty}, C::U(C::Txt()), title);
        fg->AddText(fn, tfs * 0.62f, {(dw - ssz.x) * 0.5f, ty + tsz.y + 10.f}, C::U(C::Dim()), sub);

        // Пульсирующий маркер текущей сохранённой точки (если есть).
        {
            float zx = -1.f, zy = -1.f;
            if (g_calibMode == 2 && g_state.farm_fire_x >= 0.f) { zx = g_state.farm_fire_x * dw; zy = g_state.farm_fire_y * dh; }
            if (g_calibMode == 4) {
                if (g_state.freecam_joy_x >= 0.f) { zx = g_state.freecam_joy_x * dw; zy = g_state.freecam_joy_y * dh; }
                else { zx = dw * 0.12f; zy = dh * 0.82f; } // дефолт слева внизу
            }
            if (g_calibMode == 7) {
                zx = (g_state.freecam_look_x >= 0.f ? g_state.freecam_look_x : 0.72f) * dw;
                zy = (g_state.freecam_look_y >= 0.f ? g_state.freecam_look_y : 0.50f) * dh;
            }
            // У точки аима маркер виден всегда: пока она не задана, показываем
            // ту позицию, из которой аим водит палец по умолчанию.
            if (g_calibMode == 3) { zx = AimTouchFracX() * dw; zy = AimTouchFracY() * dh; }
            // Счётчик: сохранённый центр или тот, где пилюля стоит сейчас.
            if (g_calibMode == 5) {
                if (g_state.hud_cnt_x >= 0.f) { zx = g_state.hud_cnt_x * dw; zy = g_state.hud_cnt_y * dh; }
                else { const HudCounterGeom& hg = HudCounterLast(); zx = hg.bX + hg.bW * 0.5f; zy = hg.bY + hg.bH * 0.5f; }
            }
            // Кнопка фрикама: сохранённый центр или дефолт справа по центру.
            if (g_calibMode == 6) {
                if (g_state.freecam_btn_x >= 0.f) { zx = g_state.freecam_btn_x * dw; zy = g_state.freecam_btn_y * dh; }
                else { zx = kFcBtnDefX * dw; zy = kFcBtnDefY * dh; }
            }
            if (g_calibMode == 8) {
                if (g_state.fakelag_btn_x >= 0.f) { zx = g_state.fakelag_btn_x * dw; zy = g_state.fakelag_btn_y * dh; }
                else { zx = kFlBtnDefX * dw; zy = kFlBtnDefY * dh; }
            }
            if (g_calibMode == 9) {
                if (g_state.xray_btn_x >= 0.f) { zx = g_state.xray_btn_x * dw; zy = g_state.xray_btn_y * dh; }
                else { zx = kXrBtnDefX * dw; zy = kXrBtnDefY * dh; }
            }
            if (zx >= 0.f) {
                if (g_calibMode == 7) {
                    const float rr = ImClamp(g_state.freecam_look_radius, 0.10f, 0.48f) * ImMin(dw, dh);
                    fg->AddCircleFilled({zx, zy}, rr, C::UA(C::Acc(), 0.10f), 72);
                    fg->AddCircle({zx, zy}, rr, C::UA(C::Acc(), 0.75f), 72, 3.f);
                }
                // Джойстик фрикама: круг ровно того размера, каким он будет
                // в полёте (слайдер «Размер джойстика» учитывается).
                if (g_calibMode == 4) {
                    const float jr = FreecamJoyRadius(dw, dh);
                    fg->AddCircleFilled({zx, zy}, jr, C::UA(C::Acc(), 0.10f), 64);
                    fg->AddCircle({zx, zy}, jr, C::UA(C::Acc(), 0.75f), 64, 3.f);
                }
                float pr = 34.f + 6.f * sinf((float)ImGui::GetTime() * 4.f);
                fg->AddCircle({zx, zy}, pr, C::UA(C::Acc(), 0.85f), 40, 3.f);
                fg->AddCircleFilled({zx, zy}, 7.f, C::U(C::Acc()), 20);
            }
        }

        // Режим 5 — живое превью: пока палец на экране, пилюля следует за ним;
        // у середины (|доля − 0.5| < 4%) липнет ровно к центру по каждой оси
        // отдельно — с направляющими. Та же липкость — при записи ниже.
        if (g_calibMode == 5 && io.MouseDown[0]) {
            float fx = io.MousePos.x / dw, fy = io.MousePos.y / dh;
            const bool snapX = fabsf(fx - 0.5f) < 0.04f;
            const bool snapY = fabsf(fy - 0.5f) < 0.04f;
            if (snapX) fx = 0.5f;
            if (snapY) fy = 0.5f;
            if (snapX) fg->AddLine({dw * 0.5f, 0.f}, {dw * 0.5f, dh}, C::UA(C::Acc(), 0.55f), 2.f);
            if (snapY) fg->AddLine({0.f, dh * 0.5f}, {dw, dh * 0.5f}, C::UA(C::Acc(), 0.55f), 2.f);
            const HudCounterGeom& hg = HudCounterLast();
            if (hg.bW > 1.f && dw > hg.bW + 16.f && dh > hg.bH + 16.f) {
                float px = ImClamp(fx * dw - hg.bW * 0.5f, 8.f, dw - hg.bW - 8.f);
                float py = ImClamp(fy * dh - hg.bH * 0.5f, 8.f, dh - hg.bH - 8.f);
                fg->AddRectFilled({px, py}, {px + hg.bW, py + hg.bH}, C::UA(C::Card(), 0.85f), 46.f);
                fg->AddRect({px, py}, {px + hg.bW, py + hg.bH}, C::UA(C::Acc(), 0.9f), 46.f, 0, 2.5f);
            }
        }

        // Режим 6 — то же превью для кнопки фрикама: квадрат следует за
        // пальцем, у середины липнет к центру по каждой оси с направляющими.
        if (g_calibMode == 6 && io.MouseDown[0]) {
            float fx = io.MousePos.x / dw, fy = io.MousePos.y / dh;
            const bool snapX = fabsf(fx - 0.5f) < 0.04f;
            const bool snapY = fabsf(fy - 0.5f) < 0.04f;
            if (snapX) fx = 0.5f;
            if (snapY) fy = 0.5f;
            if (snapX) fg->AddLine({dw * 0.5f, 0.f}, {dw * 0.5f, dh}, C::UA(C::Acc(), 0.55f), 2.f);
            if (snapY) fg->AddLine({0.f, dh * 0.5f}, {dw, dh * 0.5f}, C::UA(C::Acc(), 0.55f), 2.f);
            if (dw > kFcBtnSide + 16.f && dh > kFcBtnSide + 16.f) {
                float px = ImClamp(fx * dw - kFcBtnSide * 0.5f, 8.f, dw - kFcBtnSide - 8.f);
                float py = ImClamp(fy * dh - kFcBtnSide * 0.5f, 8.f, dh - kFcBtnSide - 8.f);
                fg->AddRectFilled({px, py}, {px + kFcBtnSide, py + kFcBtnSide}, C::UA(C::Card(), 0.85f), kFcBtnRadius);
                fg->AddRect({px, py}, {px + kFcBtnSide, py + kFcBtnSide}, C::UA(C::Acc(), 0.9f), kFcBtnRadius, 0, 2.5f);
            }
        }
        // Режим 8 — превью кнопки FakeLag
        if (g_calibMode == 8 && io.MouseDown[0]) {
            float fx = io.MousePos.x / dw, fy = io.MousePos.y / dh;
            const bool snapX = fabsf(fx - 0.5f) < 0.04f;
            const bool snapY = fabsf(fy - 0.5f) < 0.04f;
            if (snapX) fx = 0.5f;
            if (snapY) fy = 0.5f;
            if (snapX) fg->AddLine({dw * 0.5f, 0.f}, {dw * 0.5f, dh}, C::UA(C::Acc(), 0.55f), 2.f);
            if (snapY) fg->AddLine({0.f, dh * 0.5f}, {dw, dh * 0.5f}, C::UA(C::Acc(), 0.55f), 2.f);
            if (dw > kFlBtnSide + 16.f && dh > kFlBtnSide + 16.f) {
                float px = ImClamp(fx * dw - kFlBtnSide * 0.5f, 8.f, dw - kFlBtnSide - 8.f);
                float py = ImClamp(fy * dh - kFlBtnSide * 0.5f, 8.f, dh - kFlBtnSide - 8.f);
                fg->AddRectFilled({px, py}, {px + kFlBtnSide, py + kFlBtnSide}, C::UA(C::Card(), 0.85f), kFlBtnRadius);
                fg->AddRect({px, py}, {px + kFlBtnSide, py + kFlBtnSide}, C::UA(C::Acc(), 0.9f), kFlBtnRadius, 0, 2.5f);
            }
        }
        // Режим 9 — превью кнопки Иксрей
        if (g_calibMode == 9 && io.MouseDown[0]) {
            float fx = io.MousePos.x / dw, fy = io.MousePos.y / dh;
            const bool snapX = fabsf(fx - 0.5f) < 0.04f;
            const bool snapY = fabsf(fy - 0.5f) < 0.04f;
            if (snapX) fx = 0.5f;
            if (snapY) fy = 0.5f;
            if (snapX) fg->AddLine({dw * 0.5f, 0.f}, {dw * 0.5f, dh}, C::UA(C::Acc(), 0.55f), 2.f);
            if (snapY) fg->AddLine({0.f, dh * 0.5f}, {dw, dh * 0.5f}, C::UA(C::Acc(), 0.55f), 2.f);
            if (dw > kXrBtnSide + 16.f && dh > kXrBtnSide + 16.f) {
                float px = ImClamp(fx * dw - kXrBtnSide * 0.5f, 8.f, dw - kXrBtnSide - 8.f);
                float py = ImClamp(fy * dh - kXrBtnSide * 0.5f, 8.f, dh - kXrBtnSide - 8.f);
                fg->AddRectFilled({px, py}, {px + kXrBtnSide, py + kXrBtnSide}, C::UA(C::Card(), 0.85f), kXrBtnRadius);
                fg->AddRect({px, py}, {px + kXrBtnSide, py + kXrBtnSide}, C::UA(C::Acc(), 0.9f), kXrBtnRadius, 0, 2.5f);
            }
        }

        // Тап (отпускание пальца) — записываем зону в долях видимого экрана.
        // Фрикам тоже здесь: его геометрия считается от VisibleScreen, как и
        // всё остальное, поэтому отдельная шкала ему больше не нужна.
        if (io.MouseReleased[0]) {
            float rx = io.MousePos.x / dw, ry = io.MousePos.y / dh;
            if (rx > 0.f && rx < 1.f && ry > 0.f && ry < 1.f) {
                const bool farm = (g_calibMode == 2);
                const bool hud  = (g_calibMode == 5);
                const bool fcb  = (g_calibMode == 6);
                const bool fcj  = (g_calibMode == 4);
                const bool fcl  = (g_calibMode == 7);
                const bool flb  = (g_calibMode == 8);
                const bool xrb  = (g_calibMode == 9);
                if (g_calibMode == 2) {
                    g_state.farm_fire_x = rx; g_state.farm_fire_y = ry;
                    ShowToast(XS("Зона огня сохранена"));
                } else if (g_calibMode == 4) {
                    g_state.freecam_joy_x = rx; g_state.freecam_joy_y = ry;
                    ShowToast(XS("Джойстик фрикама сохранён"));
                } else if (fcl) {
                    g_state.freecam_look_x = rx; g_state.freecam_look_y = ry;
                    ShowToast(XS("Зона обзора фрикама сохранена"));
                } else if (hud) {
                    // Та же липкость к центру, что в превью: по каждой оси.
                    if (fabsf(rx - 0.5f) < 0.04f) rx = 0.5f;
                    if (fabsf(ry - 0.5f) < 0.04f) ry = 0.5f;
                    g_state.hud_cnt_x = rx; g_state.hud_cnt_y = ry;
                    ShowToast(XS("Положение счётчика сохранено"));
                } else if (fcb) {
                    // Та же липкость к центру, что в превью: по каждой оси.
                    if (fabsf(rx - 0.5f) < 0.04f) rx = 0.5f;
                    if (fabsf(ry - 0.5f) < 0.04f) ry = 0.5f;
                    g_state.freecam_btn_x = rx; g_state.freecam_btn_y = ry;
                    ShowToast(XS("Кнопка фрикама сохранена"));
                } else if (flb) {
                    if (fabsf(rx - 0.5f) < 0.04f) rx = 0.5f;
                    if (fabsf(ry - 0.5f) < 0.04f) ry = 0.5f;
                    g_state.fakelag_btn_x = rx; g_state.fakelag_btn_y = ry;
                    ShowToast(XS("Кнопка FakeLag сохранена"));
                } else if (xrb) {
                    if (fabsf(rx - 0.5f) < 0.04f) rx = 0.5f;
                    if (fabsf(ry - 0.5f) < 0.04f) ry = 0.5f;
                    g_state.xray_btn_x = rx; g_state.xray_btn_y = ry;
                    ShowToast(XS("Кнопка Иксрей сохранена"));
                } else {
                    g_state.aim_tx = rx; g_state.aim_ty = ry;
                    ShowToast(XS("Точка пальца сохранена"));
                }
                PlaySound(SND_CLICK);
                g_calibMode = 0;
                menu_open = true;
                // Return to the owning inline group, not the retired farm popover.
                if (hud)             g_state.cur_tab = 5;
                else if (farm || fcb || fcj || fcl || flb || xrb) g_state.cur_tab = 3;
                else                 g_state.cur_tab = 1;
                g_input.touchConsumed = true; // этот тап уже отработал
            }
        }
        return; // пока калибруемся, меню не рисуем
    }

    // Появление из кнопки / улетание в неё. В кадре открытия запоминаем центр
    // пилюли-счётчика (это она открывает меню тапом) и запускаем раскрытие;
    // в кадре закрытия — то же, но кривая идёт задом наперёд: окно сначала
    // слегка раздувается (перелёт back-кривой) и втягивается в пилюлю.
    {
        static bool s_wasOpen = false;
        auto pillCenter = [&]() -> ImVec2 {
            const HudCounterGeom& hg = HudCounterLast();
            if (hg.bW > 0.f && hg.bH > 0.f)
                return {hg.bX + hg.bW * 0.5f, hg.bY + hg.bH * 0.5f};
            return {g_win.pos.x + g_win.w * 0.5f, g_win.pos.y};
        };
        if (menu_open && !s_wasOpen) {
            g_win.openFrom = pillCenter();
            // Разворот посреди закрытия: продолжаем с текущего кадра, а не с нуля.
            g_win.openAnim = g_win.closing ? g_win.closeAnim : 0.f;
            g_win.closing = false;
            g_win.tpos = g_win.pos;
            g_win.follow.reset(g_win.pos.x, g_win.pos.y);
        }
        if (!menu_open && s_wasOpen) {
            g_win.openFrom = pillCenter();
            g_win.closing = true;
            g_win.closeAnim = 1.f;
            g_win.dragging = false;
            g_win.resizing = false;
            g_win.tpos = g_win.pos;
            g_win.follow.reset(g_win.pos.x, g_win.pos.y);
        }
        s_wasOpen = menu_open;
    }

    if (!menu_open && !g_win.closing) return;

    float WW = g_win.w, WH = g_win.h;
    const float hH_ = Layout::HeaderH;

    {
        bool inResize = io.MousePos.x >= g_win.pos.x + g_win.w - R::Card - 14.f
                     && io.MousePos.x <= g_win.pos.x + g_win.w + 14.f
                     && io.MousePos.y >= g_win.pos.y + g_win.h - R::Card - 14.f
                     && io.MousePos.y <= g_win.pos.y + g_win.h + 14.f;

        if (!io.MouseDown[0]) g_win.resizing = false;

        bool anyOverlay = g_sheet.visible || (g_pop.visible && !g_pop.closing);
        if (!anyOverlay && io.MouseClicked[0] && inResize && g_win.openAnim >= 1.f && !g_win.closing) {
            g_win.resizing         = true;
            g_win.resizeTouchStart = io.MousePos;
            g_win.sizeStart        = {g_win.w, g_win.h};
        }

        if (g_win.resizing && io.MouseDown[0]) {
            float dx = io.MousePos.x - g_win.resizeTouchStart.x;
            float dy = io.MousePos.y - g_win.resizeTouchStart.y;
            g_win.w = ImMax(WW_MIN, g_win.sizeStart.x + dx);
            g_win.h = ImMax(WH_MIN, g_win.sizeStart.y + dy);
        }
    }

    {
        if (!io.MouseDown[0]) g_win.dragging = false;

        if (!g_win.dragging && io.MouseDown[0] && !(g_sheet.visible || (g_pop.visible && !g_pop.closing)) && !g_win.resizing && g_win.openAnim >= 1.f && !g_win.closing) {
            float tx = io.MouseClickedPos[0].x, ty = io.MouseClickedPos[0].y;
            // Тащим окно за верхнюю шапку (по всей ширине).
            bool inHdr = tx >= g_win.pos.x && tx < g_win.pos.x + g_win.w
                      && ty >= g_win.pos.y && ty < g_win.pos.y + hH_;
            if (inHdr) {
                float dx = io.MousePos.x - io.MouseClickedPos[0].x;
                float dy = io.MousePos.y - io.MouseClickedPos[0].y;
                if (fabsf(dx) + fabsf(dy) > 8.f) {
                    g_win.dragging   = true;
                    g_win.touchStart = io.MouseClickedPos[0];
                    g_win.posStart   = g_win.pos;
                    g_win.tpos       = g_win.pos;
                    g_win.follow.reset(g_win.pos.x, g_win.pos.y);
                }
            }
        }

        if (g_win.dragging && io.MouseDown[0]) {
            float dx   = io.MousePos.x - g_win.touchStart.x;
            float dy   = io.MousePos.y - g_win.touchStart.y;
            g_win.tpos.x = g_win.posStart.x + dx;
            g_win.tpos.y = g_win.posStart.y + dy;
        }
        // Кламп цели — только когда палец отпущен. Пока тащим, tpos может
        // уехать за край: рисуемая позиция сжимается rubber-band'ом.
        if (!g_win.dragging) {
            float dw = 0.f, dh = 0.f;
            VisibleScreen(dw, dh);
            if (dw > 100.f && dh > 100.f) {
                g_win.tpos.x = ImClamp(g_win.tpos.x, 0.f, ImMax(0.f, dw - g_win.w));
                g_win.tpos.y = ImClamp(g_win.tpos.y, 0.f, ImMax(0.f, dh - g_win.h));
            }
        }
    }

    // Появление из кнопки / желе-следование. Кривые — готовые из интернета
    // (см. anim_web.h), здесь только обвязка: что анимируем и куда.
    const bool popping = g_win.openAnim < 1.f || g_win.closing;
    if (g_win.openAnim < 1.f && !g_win.closing)
        g_win.openAnim = ImMin(1.f, g_win.openAnim + dt / 0.40f);
    if (g_win.closing) {
        g_win.closeAnim -= dt / 0.40f;
        if (g_win.closeAnim <= 0.f) { g_win.closing = false; return; }
    }
    if (g_win.tpos.x < 0.f) { g_win.tpos = g_win.pos; g_win.follow.reset(g_win.pos.x, g_win.pos.y); }

    WinRectAnim winAnim;
    if (popping) {
        // Выскакивание из кнопки: окно растёт easeOutBack (easings.net) из точки
        // открытия. Высота идёт чуть раньше ширины — выходит сквош-и-стретч:
        // окно деформируется и принимает свою форму с лёгким перелётом.
        // При закрытии та же кривая идёт назад — окно втягивается в пилюлю.
        const ImVec2 o = g_win.openFrom;
        const float t = g_win.closing ? g_win.closeAnim : g_win.openAnim;
        const float sw = NetEaseOutBack(t);
        const float sh = NetEaseOutBack(ImMin(1.f, t * 1.12f));
        const float aw = ImMax(1.f, g_win.w * ImMax(0.f, sw));
        const float ah = ImMax(1.f, g_win.h * ImMax(0.f, sh));
        const ImVec2 tc = {g_win.pos.x + g_win.w * 0.5f, g_win.pos.y + g_win.h * 0.5f};
        const ImVec2 cc = {o.x + (tc.x - o.x) * sw, o.y + (tc.y - o.y) * sw};
        winAnim.arm(g_win, {cc.x - aw * 0.5f, cc.y - ah * 0.5f}, aw, ah);
        g_win.follow.moveInstant(g_win.pos.x, g_win.pos.y);
    } else {
        // Apple: пока палец держит — окно 1:1 (с rubber-band за краем экрана).
        // Отпустил — критическая пружина (z=1, без желе) доезжает с той же
        // скоростью, что была у пальца. Всё окно одним прямоугольником.
        const float fdt = dt > 0.f ? dt : 1e-4f;
        float dw2 = 0.f, dh2 = 0.f;
        VisibleScreen(dw2, dh2);
        auto rubberPos = [&](float tx, float ty) {
            float x = tx, y = ty;
            if (dw2 > 100.f && dh2 > 100.f) {
                const float xMax = ImMax(0.f, dw2 - g_win.w);
                const float yMax = ImMax(0.f, dh2 - g_win.h);
                if (tx < 0.f)       x = -NetRubberBand(-tx);
                else if (tx > xMax) x = xMax + NetRubberBand(tx - xMax);
                if (ty < 0.f)       y = -NetRubberBand(-ty);
                else if (ty > yMax) y = yMax + NetRubberBand(ty - yMax);
            }
            return ImVec2{x, y};
        };
        if (g_win.dragging) {
            const ImVec2 rp = rubberPos(g_win.tpos.x, g_win.tpos.y);
            const float vx = (rp.x - g_win.pos.x) / fdt;
            const float vy = (rp.y - g_win.pos.y) / fdt;
            g_win.pos = rp;
            g_win.follow.setState(rp.x, rp.y, vx, vy);
        } else {
            g_win.follow.update(fdt, g_win.tpos.x, g_win.tpos.y);
            g_win.pos.x = g_win.follow.posX;
            g_win.pos.y = g_win.follow.posY;
        }
    }
    WW = g_win.w; WH = g_win.h;
    if (g_sheet.visible || (g_pop.visible && !g_pop.closing)) {
        float bsw = 0.f, bsh = 0.f;
        VisibleScreen(bsw, bsh);
        Touch_AddBlockRect(0.f, 0.f, bsw, bsh);
    } else {
        Touch_AddBlockRect(g_win.pos.x - 8.f, g_win.pos.y - 8.f,
                           g_win.pos.x + WW + 16.f, g_win.pos.y + WH + 16.f);
    }

    ImGui::SetNextWindowSize({g_win.w, g_win.h}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(g_win.pos, ImGuiCond_Always);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoScrollbar
                        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground
                        | ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoNav;

    ImGui::Begin("##menu", nullptr, wf);
    {
        auto  bgP  = g_win.pos;
        auto* bgDL = ImGui::GetWindowDrawList();
        bgDL->AddRectFilled(bgP, {bgP.x + WW, bgP.y + WH}, C::U(C::Bg()), R::Card);
    }

    ImVec2 wp = g_win.pos;
    // Панель вкладок: слева (вертикальный столбец) или снизу (строка по
    // центру) — выбирается в «Опциях». Контент занимает остальную площадь.
    const bool  panelLeft = g_state.ui_panel_left;
    const float botH = Layout::BottomH;
    // Панель вкладок шире стандартной (128 px), если в ней стоит кружок: он
    // должен целиком помещаться в панели, иначе накрывает содержимое вкладок.
    const float railW = panelLeft ? AvatarRailW(g_win.w) : Layout::RailW;
    const float cH = panelLeft ? g_win.h : g_win.h - botH;
    const float cW = panelLeft ? g_win.w - railW : g_win.w;
    const float cX0 = panelLeft ? railW : 0.f;

    g_state.tab_alpha += (1.f - g_state.tab_alpha) * 5.f * dt;
    if (g_state.tab_alpha > .999f) g_state.tab_alpha = 1.f;
    SpringTick(g_state.tab_slide, g_state.tab_slide_vel, 0.f, dt);

    // Короткие и понятные названия вкладок (индекс = id вкладки) — общий список.
    // Подпись вкладки берётся на текущем языке: и в шапке, и в панели, и в
    // расчёте ширины ячейки.
    auto tabNames = [](int k) { return TabTitle(k); };
    // Вкладка «Меню» (id 0) удалена: её функционал переехал в «Опции».
    // id контента вкладок не меняются — конфиги и логика остаются как были.
    static constexpr int kTabShown = 5;
    static constexpr int kTabOrder[kTabShown] = {1, 2, 3, 4, 5};

    // Пропорции ячейки левой панели: подпись как можно крупнее (цель — вдвое
    // крупнее базовых 0.88 от кегля меню) и жирная, иконка — слева.
    //
    // Место считается по самой длинной подписи: у «Конфиги» при двойном кегле
    // ширина ~179 px, а в панели после иконки остаётся ~120 px, поэтому берём
    // максимальный кегль, который влезает, поджав иконку не ниже 40 px. Жирность
    // набирается проходами текста со сдвигом (см. DrawTabText) — у Roboto в
    // сборке одно начертание, полужирного файла нет.
    struct RailCell { float icon = kRailIcon, fs = 20.f, padL = kRailPadL, gap = kRailGap,
                      padR = kRailPadR; };
    auto RailMetrics = [&](float railW) -> RailCell {
        RailCell m;
        const float baseFS = ImGui::GetFontSize() * 0.88f;   // базовый кегль панели
        const float want   = baseFS * kRailFS;               // цель — крупнее базового
        // Если панель уже, чем посчитано в RailTabsW (узкий экран), кегль и
        // иконка ужимаются: сначала меньше становится иконка, потом подпись.
        const float measureSize = ImGui::GetFontSize();
        float measuredWorst = 0.f;
        for (int k = 0; k < kTabShown; ++k)
            measuredWorst = ImMax(measuredWorst, MeasureUiTextScaled(tabNames(kTabOrder[k]), measureSize).x);
        auto widest = [&](float fs) { return measuredWorst * (fs / measureSize) + 4.f; };
        for (float fs = want; fs > baseFS; fs -= 1.f) {
            for (float icon = kRailIcon; icon >= 45.f; icon -= 5.f) {
                const float avail = railW - m.padL - m.padR - icon - m.gap;
                if (avail > 0.f && widest(fs) <= avail) { m.icon = icon; m.fs = fs; return m; }
            }
        }
        m.icon = 45.f; m.fs = baseFS;      // узкая панель — базовый кегль
        return m;
    };

    // Ячейка вкладки: иконка + подпись. В нижней панели — иконка сверху,
    // подпись под ней по центру; в левой панели — подпись СПРАВА от иконки
    // (иконки стоят на одной вертикали, подписи — в одну колонку, как в
    // боковых панелях macOS). Размеры подгоняются под ширину ячейки, поэтому
    // длинные подписи («Конфиги») не вылезают за панель.
    auto DrawTabCell = [&](ImDrawList* fdl, int i, float cellX, float cellY,
                           float cellW, float cellH, float iconSize, float lblFS,
                           bool horiz = false,
                           float padL0 = 0.f, float padR0 = 0.f, float gap0 = 0.f) {
        const bool   active = (i == g_state.cur_tab);
        const ImVec4 col    = active ? C::Acc() : C::Dim();
        auto*  font = ImGui::GetFont();

        float  icoW = iconSize, icoH = iconSize, icoX = 0.f, icoY = 0.f;
        float  fs   = lblFS;
        ImVec2 lpos{};

        if (horiz) {
            const float padL = padL0 > 0.f ? padL0 : 16.f;
            const float padR = padR0 > 0.f ? padR0 : 12.f;
            const float gap  = gap0  > 0.f ? gap0  : 12.f;
            const float measureSize = ImGui::GetFontSize();
            const float measuredWidth = MeasureUiTextScaled(tabNames(i), measureSize).x;
            auto labelW = [&](float f) { return measuredWidth * (f / measureSize) + 4.f; };
            // Подгонка — только на совсем узкой панели: обычно размеры уже
            // посчитаны в RailMetrics под самую длинную подпись.
            const float avail = cellW - padL - padR - gap;
            for (int it = 0; it < 4 && icoW + labelW(fs) > avail; ++it) {
                const float k = ImMax(0.6f, avail / ImMax(1.f, icoW + labelW(fs)));
                icoW = ImMax(24.f, icoW * k);
                fs   = ImMax(12.f, fs * ImMax(0.75f, k));
            }
            icoH = icoW;
            icoX = cellX + padL;
            icoY = cellY + (cellH - icoH) * 0.5f;
            const auto lsz = font->CalcTextSizeA(fs, FLT_MAX, 0, tabNames(i));
            lpos = {icoX + icoW + gap, cellY + (cellH - lsz.y) * 0.5f};
        } else {
            const auto  tsz    = font->CalcTextSizeA(fs, FLT_MAX, 0, tabNames(i));
            const float blockH = icoH + 6.f + tsz.y;
            icoX = cellX + cellW * 0.5f - icoW * 0.5f;
            icoY = cellY + (cellH - blockH) * 0.5f;
            lpos = {cellX + cellW * 0.5f - tsz.x * 0.5f, icoY + icoH + 6.f};
        }

        if (g_tabIcons[i]) {
            ImVec2 iMin = {icoX, icoY};
            ImVec2 iMax = {icoX + icoW, icoY + icoH};
            fdl->AddImageRounded((ImTextureID)(intptr_t)g_tabIcons[i], iMin, iMax,
                {0,0}, {1,1}, IM_COL32(255, 255, 255, active ? 255 : 150), 9.f);
        } else {
            // Заглушка: скруглённый квадрат с первой буквой вкладки.
            ImVec2 iMin = {icoX, icoY};
            ImVec2 iMax = {icoX + icoW, icoY + icoH};
            fdl->AddRectFilled(iMin, iMax, C::UA(C::Acc(), active ? 0.9f : 0.35f), 9.f);
            char letter[8] = {};
            int gl = 0;
            letter[gl++] = tabNames(i)[0];
            if ((unsigned char)tabNames(i)[0] >= 0xC0) letter[gl++] = tabNames(i)[1];
            float gfs = icoW * 0.55f;
            auto gsz = font->CalcTextSizeA(gfs, FLT_MAX, 0, letter);
            fdl->AddText(font, gfs,
                {icoX + icoW * 0.5f - gsz.x * 0.5f, icoY + (icoH - gsz.y) * 0.5f},
                IM_COL32(255, 255, 255, active ? 255 : 200), letter);
        }
        fdl->AddText(font, fs, lpos, C::U(col), tabNames(i));
    };

    auto TabTap = [&](int i) {
        const bool modalBlk = g_sheet.visible || (g_pop.visible && !g_pop.closing);
        if (!modalBlk && WasTappedHere() && !IsScrollDragging() && !g_input.touchConsumed && g_state.cur_tab != i) {
            g_state.cur_tab       = i;
            g_state.tab_alpha     = 0.f;
            g_state.tab_slide     = 50.f;
            g_state.tab_slide_vel = 0.f;
            g_scrollMain          = {};
            PlaySound(SND_CLICK);
        }
    };

    if (panelLeft) {
        // ---- Левая панель вкладок (вертикальный столбец по центру) ------
        ImGui::SetCursorPos({0, 0});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##lp", {railW, WH}, false, ImGuiWindowFlags_NoScrollbar);
        auto  lpPos = ImGui::GetWindowPos();
        auto* ldl   = ImGui::GetWindowDrawList();
        ldl->AddRectFilled(lpPos, {lpPos.x + railW, lpPos.y + WH}, C::U(C::LeftBg()), R::Card,
                           ImDrawFlags_RoundCornersLeft);
        ldl->AddLine({lpPos.x + railW, lpPos.y + 12.f}, {lpPos.x + railW, lpPos.y + WH - 12.f},
                     C::UA(C::Sep(), 0.8f), 1.f);


        // Размеры подписи и иконки в этой панели — из ширины панели.
        const RailCell railM = RailMetrics(railW);

        // Столбец вкладок начинается у верхнего края панели; при нехватке
        // места высота ячеек поджимается, чтобы последняя вкладка не уехала вниз.
        const float colTop = 12.f;
        const float avail  = ImMax(64.f * kTabShown, WH - colTop - 6.f);
        const float tabH   = ImMin(Layout::TabHV, avail / (float)kTabShown);
        const float startY = colTop;

        for (int s = 0; s < kTabShown; s++) {
            const int i = kTabOrder[s];
            ImGui::SetCursorPos({0, startY + s * tabH});
            auto pos2 = ImGui::GetCursorScreenPos();
            tab_rects[i] = {pos2.y};
            char tabId[16];
            snprintf(tabId, sizeof(tabId), "##tab%d", i);
            ImGui::InvisibleButton(tabId, {railW, tabH});
            TabTap(i);
        }

        // Пружинная «пилюля» активной вкладки скользит по вертикали.
        {
            float targetRel = tab_rects[g_state.cur_tab].sx - wp.y;
            if (pill_y < 0.f) { pill_y = targetRel; pill_vel = 0.f; }
            SpringTick(pill_y, pill_vel, targetRel, dt);
        }
        float pillY = wp.y + pill_y;

        {
            auto*       fdl = ImGui::GetForegroundDrawList();
            const float pR  = R::Pill;
            float px0 = lpPos.x + 9.f,  px1 = lpPos.x + railW - 9.f;
            float py0 = pillY + 7.f,    py1 = pillY + tabH - 7.f;
            fdl->AddRectFilled({px0, py0}, {px1, py1}, C::U(C::Card()), pR);
            fdl->AddRectFilled({px0, py0}, {px1, py1}, C::UA(C::Acc(), g_darkTheme ? 0.16f : 0.10f), pR);
            fdl->AddRect({px0, py0}, {px1, py1}, C::UA(C::Acc(), 0.5f), pR, 0, 1.5f);
            // Короткая акцентная полоска на левом краю окна.
            float icy = (py0 + py1) * 0.5f;
            fdl->AddRectFilled({lpPos.x - 1.f, icy - 16.f}, {lpPos.x + 3.f, icy + 16.f}, C::U(C::Acc()), 2.f);
        }

        {
            auto* fdl = ImGui::GetForegroundDrawList();
            for (int s = 0; s < kTabShown; s++) {
                const int i = kTabOrder[s];
                // Иконка слева, подпись справа — как в боковых панелях macOS.
                // Кегль и иконка посчитаны под ширину панели (RailMetrics).
                DrawTabCell(fdl, i, lpPos.x, tab_rects[i].sx, railW, tabH,
                            railM.icon, railM.fs, true,
                            railM.padL, railM.padR, railM.gap);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        // ---- Нижняя панель вкладок (строка по центру) --------------------
        ImGui::SetCursorPos({0, cH});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##lp", {WW, botH}, false, ImGuiWindowFlags_NoScrollbar);
        auto  lpPos = ImGui::GetWindowPos();
        auto* ldl   = ImGui::GetWindowDrawList();
        ldl->AddRectFilled(lpPos, {lpPos.x + WW, lpPos.y + botH}, C::U(C::LeftBg()), R::Card,
                           ImDrawFlags_RoundCornersBottom);
        ldl->AddLine({lpPos.x + 14.f, lpPos.y}, {lpPos.x + WW - 14.f, lpPos.y},
                     C::UA(C::Sep(), 0.8f), 1.f);

        // Ряд вкладок строго по центру панели.
        const float tabW   = Layout::TabW;
        const float rowW   = tabW * kTabShown;
        const float startX = (WW - rowW) * 0.5f;

        for (int s = 0; s < kTabShown; s++) {
            const int i = kTabOrder[s];   // id вкладки в этой позиции панели
            ImGui::SetCursorPos({startX + s * tabW, 0});
            auto pos2 = ImGui::GetCursorScreenPos();
            tab_rects[i] = {pos2.x};
            char tabId[16];
            snprintf(tabId, sizeof(tabId), "##tab%d", i);
            ImGui::InvisibleButton(tabId, {tabW, botH});
            TabTap(i);
        }

        // Пружинная «пилюля» активной вкладки скользит по горизонтали.
        {
            float targetRelX = tab_rects[g_state.cur_tab].sx - wp.x;
            if (pill_y < 0.f) { pill_y = targetRelX; pill_vel = 0.f; }
            SpringTick(pill_y, pill_vel, targetRelX, dt);
        }
        float pillX = wp.x + pill_y;
        float barY  = lpPos.y;

        {
            auto*       fdl = ImGui::GetForegroundDrawList();
            const float pR  = R::Pill;
            float px0 = pillX + 8.f,        px1 = pillX + tabW - 8.f;
            float py0 = barY + 12.f,        py1 = barY + botH - 14.f;
            fdl->AddRectFilled({px0, py0}, {px1, py1}, C::U(C::Card()), pR);
            fdl->AddRectFilled({px0, py0}, {px1, py1}, C::UA(C::Acc(), g_darkTheme ? 0.16f : 0.10f), pR);
            fdl->AddRect({px0, py0}, {px1, py1}, C::UA(C::Acc(), 0.5f), pR, 0, 1.5f);
            // Короткая акцентная полоска сверху — указывает на активную вкладку.
            float icx = (px0 + px1) * 0.5f;
            fdl->AddRectFilled({icx - 16.f, barY - 1.f}, {icx + 16.f, barY + 3.f}, C::U(C::Acc()), 2.f);
        }

        {
            auto* fdl = ImGui::GetForegroundDrawList();
            for (int s = 0; s < kTabShown; s++) {
                const int i = kTabOrder[s];
                DrawTabCell(fdl, i, tab_rects[i].sx, barY, tabW, botH, 62.f,
                            ImGui::GetFontSize() * 0.95f);
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::SetCursorPos({cX0, 0});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##cp", {cW, cH}, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Шапка контента содержит только заголовок и не резервирует место под декор.
    const float hdrH = MenuHeaderH(panelLeft);

    {
        // Шапка: заголовок вкладки по центру.
        const char* headerText = XS("t.me/benzware");
        auto*  cdl = ImGui::GetWindowDrawList();
        auto   hp  = ImGui::GetWindowPos();
        const float hH = hdrH;
        const float titleFS = ImGui::GetFontSize() * 1.45f;
        auto tsz = ImGui::GetFont()->CalcTextSizeA(titleFS, FLT_MAX, 0, headerText);
        cdl->AddText(ImGui::GetFont(), titleFS,
            {hp.x + (cW - tsz.x) * 0.5f, hp.y + (hH - tsz.y) * 0.5f}, C::U(C::Txt()), headerText);
        ImGui::Dummy({cW, hH});
    }

    {
        const float areaH = cH - hdrH;
        auto cpPos  = ImGui::GetWindowPos(); cpPos.y += hdrH;
        const float cpRight = cpPos.x + cW;

        bool sheetBlocking = g_sheet.visible || (g_pop.visible && !g_pop.closing);
        static float s_maxScroll = 0.f;

        bool clickInRight = (io.MouseClickedPos[0].x >= cpPos.x)
                         && (io.MouseClickedPos[0].x <= cpRight)
                         && (io.MouseClickedPos[0].y >= cpPos.y)
                         && (io.MouseClickedPos[0].y <= cpPos.y + areaH);
        bool mIn = clickInRight
            && io.MousePos.x >= cpPos.x  && io.MousePos.x <= cpRight
            && io.MousePos.y >= cpPos.y  && io.MousePos.y <= cpPos.y + areaH;

        auto* cdl = ImGui::GetWindowDrawList();
        cdl->PushClipRect(cpPos, {cpRight, cpPos.y + areaH - 4.f}, true);

        float slideOffset = g_state.tab_slide;
        ImGui::SetCursorPosY(hdrH - g_scrollMain.off + slideOffset);
        ImGui::SetCursorPosX(12.f);

        if (sheetBlocking) ImGui::BeginDisabled(true);
        float contentH = TabContent(g_state.cur_tab, dt, cW);
        if (sheetBlocking) ImGui::EndDisabled();

        if (g_state.tab_alpha < 0.999f) {
            float fadeA = 1.f - g_state.tab_alpha;
            cdl->AddRectFilled(cpPos, {cpRight, cpPos.y + areaH},
                C::UA(C::Bg(), fadeA), R::Card, ImDrawFlags_RoundCornersTop);
        }

        s_maxScroll = ImMax(0.f, contentH - (areaH - 4.f));
        ScrollTick(g_scrollMain, mIn, sheetBlocking, s_maxScroll, dt);

        if (s_maxScroll > 0.f) {
            const float sbPad  = 5.f;
            const float sbVPad = 8.f;
            const float sbMinH = 36.f;
            const float sbW    = 8.f;

            float ratio     = (areaH * 0.28f) / (areaH + s_maxScroll);
            float sbHTarget = ImMax(sbMinH, areaH * ratio);
            float sbTrack   = areaH - sbHTarget - sbVPad * 2.f;
            float sbTTarget = (s_maxScroll > 0.f) ? (g_scrollMain.off / s_maxScroll) * sbTrack : 0.f;

            g_scrollMain.sb_w  = sbW;
            g_scrollMain.sb_y += (sbTTarget - g_scrollMain.sb_y) * 10.f * dt;
            g_scrollMain.sb_h += (sbHTarget - g_scrollMain.sb_h) * 10.f * dt;

            float sbX0 = cpRight - sbPad - sbW;
            float sbX1 = sbX0 + sbW;
            float sbY0 = cpPos.y + sbVPad + g_scrollMain.sb_y;
            float sbY1 = sbY0 + g_scrollMain.sb_h;

            ImU32 thumbCol = C::UA(C::Dim(), 0.55f);

            auto* fdl = ImGui::GetForegroundDrawList();
            fdl->PushClipRect(cpPos, {cpRight, cpPos.y + areaH}, true);
            fdl->AddRectFilled({sbX0, sbY0}, {sbX1, sbY1}, thumbCol, sbW * 0.5f);
            fdl->PopClipRect();
        }

        cdl->PopClipRect();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    {
        static constexpr float kResizePad       = 22.f;
        static constexpr float kResizeStroke     = 10.f;
        static constexpr float kResizeGlowStroke = 6.f;
        static constexpr float kResizeGlowAlpha  = 0.13f;
        static constexpr float kArcStroke        = 14.f;
        static constexpr float kArcA0            = -0.25f;
        static constexpr float kArcA1            = IM_PI * 0.5f + 0.25f;
        static constexpr float kCornerOffset     = 10.f;

        auto* rdl = ImGui::GetForegroundDrawList();

        if (g_win.resizing) {
            const float pad = kResizePad, rr = R::Card + pad;
            float t = EaseInOut(g_themeT);
            ImVec4 bc = {Lerpf(1.f, C::AccDark().x, t), Lerpf(1.f, C::AccDark().y, t), Lerpf(1.f, C::AccDark().z, t), Lerpf(0.9f, 0.8f, t)};
            rdl->AddRect({wp.x - pad,       wp.y - pad},
                         {wp.x + g_win.w + pad, wp.y + g_win.h + pad},
                         C::U(bc), rr, 0, kResizeStroke);
            rdl->AddRect({wp.x - pad - 8.f,       wp.y - pad - 8.f},
                         {wp.x + g_win.w + pad + 8.f, wp.y + g_win.h + pad + 8.f},
                         C::UA(bc, kResizeGlowAlpha), rr + 8.f, 0, kResizeGlowStroke);
        }

        float cx = wp.x + g_win.w - R::Card - kCornerOffset;
        float cy = wp.y + g_win.h - R::Card - kCornerOffset;

        ImU32       col   = g_darkTheme ? IM_COL32(200, 200, 210, 255) : IM_COL32(22, 22, 24, 255);
        const float r     = R::Card;
        const float thick = kArcStroke;
        const float a0    = kArcA0;
        const float a1    = kArcA1;

        rdl->PathArcTo({cx, cy}, r, a0, a1, 48);
        rdl->PathStroke(col, 0, thick);

        float capR = thick * 0.5f;
        rdl->AddCircleFilled({cx + r * cosf(a0), cy + r * sinf(a0)}, capR, col, 24);
        rdl->AddCircleFilled({cx + r * cosf(a1), cy + r * sinf(a1)}, capR, col, 24);
    }

    DrawSheet(dt, wp, WW, WH);
    // vis= важен: DrawPopover при закрытом популяре выходит сразу, и падение
    // после него — это уже ImGui::End() или затемнение, а не сам популяр.
    DrawPopover(dt, wp, WW, WH);
    ImGui::End();
    if (g_menuFadeIn < 1.f) {
        float mA = 1.f - EaseInOut(g_menuFadeIn);
        auto* ofg = ImGui::GetForegroundDrawList();
        ImVec4 bgC = C::Bg();
        ofg->AddRectFilled(g_win.pos, {g_win.pos.x + WW, g_win.pos.y + WH},
            IM_COL32(int(bgC.x*255), int(bgC.y*255), int(bgC.z*255), int(mA * 255)), R::Card);
    }
}


// Калибровка зон вводом с экрана: 0 — нет, 1 — ждём тап по джойстику
// автофарма, 2 — по кнопке огня, 3 — по точке, из которой аимбот водит палец,
// 4 — по центру джойстика фрикама, 5 — куда поставить счётчик игроков,
// 6 — куда поставить кнопку фрикама. Пока калибровка активна, меню скрыто
// и первый тап по экрану пишет позицию (в долях экрана) в g_state.
int  g_calibMode = 0;
