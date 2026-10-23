// Хост-тест skin_name.h: технические id скинов из SkinData.Id превращаются в
// читаемые имена для меню скинченджера. Память игры и ImGui не задействованы.
#include "skin_name.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

bool gives(const char* id, const char* base_id, const char* want) {
    char out[96];
    const bool ok = skinname::humanize(id, base_id, out, sizeof(out));
    if (!want) return !ok;
    return ok && !strcmp(out, want);
}

void test_marker_and_prefixes() {
    // Токен «skin» отбрасывается вместе с префиксом ствола перед ним.
    assert(gives("deagle.skin.rusty.event", "deagle", "Rusty Event"));
    assert(gives("ak.skin.military", "", "Military"));
    assert(gives("smg-skin/woodland camo", "", "Woodland Camo"));
    assert(gives("skin.gold", "", "Gold"));
    // «skin» в конце — не маркер, а часть имени.
    assert(gives("gold_skin", "", "Gold Skin"));
    std::printf("  marker and prefixes: ok\n");
}

void test_base_id_prefix() {
    // Составной base_id отрезается целиком, даже без маркера «skin».
    assert(gives("pistol_deagle_default", "pistol_deagle", "Default"));
    assert(gives("dmr_gold", "dmr", "Gold"));
    // base_id не префикс — токены остаются.
    assert(gives("gold_dark", "dmr", "Gold Dark"));
    std::printf("  base_id prefix: ok\n");
}

void test_weapon_table_prefix() {
    // Без base_id префикс rozpoзнаётся таблицей оружия (weapon_name.h).
    assert(gives("dmr_gold", "", "Gold"));
    assert(gives("thompson_woodland", "", "Woodland"));
    assert(gives("smg.snow", "", "Snow"));
    std::printf("  weapon table prefix: ok\n");
}

void test_title_case() {
    // Регистр не важен на входе: первый символ токена вверх, остальные вниз.
    assert(gives("RUSTY_EVENT", "", "Rusty Event"));
    assert(gives("DeAgLe.SKIN.Gold", "", "Gold"));
    assert(gives("ww2.veteran", "", "Ww2 Veteran"));
    // Кириллица: строчная в заглавную, заглавная хвостом в строчную.
    assert(gives("skin.\xd0\xb7\xd0\xb8\xd0\xbc\xd0\xb0", "", "\xd0\x97\xd0\xb8\xd0\xbc\xd0\xb0"));
    assert(gives("\xd0\xa0\xd0\x96\xd0\x90\xd0\x92\xd0\xab\xd0\x99", "", "\xd0\xa0\xd0\xb6\xd0\xb0\xd0\xb2\xd1\x8b\xd0\xb9"));
    std::printf("  title case: ok\n");
}

void test_empty_results() {
    // id целиком состоит из префиксов — имени нет, меню покажет номер.
    assert(gives("deagle", "deagle", nullptr));
    assert(gives("dmr", "dmr", nullptr));       // префикс по base_id съел всё
    assert(gives("", "", nullptr));
    assert(gives("...", "", nullptr));
    assert(gives(nullptr, nullptr, nullptr));
    // Последнего токена отбор префикса не касается: «bow» остаётся именем.
    assert(gives("bow", "", "Bow"));
    std::printf("  empty results: ok\n");
}

void test_buffer_safety() {
    // Короткий буфер — обрезание без выхода за границы и без мусора.
    char small[6];
    assert(skinname::humanize("deagle.skin.rusty.event", "", small, sizeof(small)));
    assert(strlen(small) < sizeof(small));
    // cap < 2 — честный отказ.
    char one[1];
    assert(!skinname::humanize("gold", "", one, 0));
    std::printf("  buffer safety: ok\n");
}

}  // namespace

int main() {
    test_marker_and_prefixes();
    test_base_id_prefix();
    test_weapon_table_prefix();
    test_title_case();
    test_empty_results();
    test_buffer_safety();
    std::printf("OK\n");
    return 0;
}
