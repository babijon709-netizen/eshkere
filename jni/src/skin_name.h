#pragma once
// Читаемые имена скинов для меню. Игра хранит в SkinData.Id технические
// идентификаторы («deagle.skin.rusty.event», «dmr_gold») — пользователю
// расшифровывать их незачем. Преобразование чистое, без памяти игры и без
// ImGui, поэтому прогоняется хост-тестом (jni/tests/test_skin_name.cpp) —
// как weapon_name.h.
//
// Правила: id делится на токены разделителями '.', '_', '-', '/' и пробел;
// токен «skin» отбрасывается вместе с префиксом ствола перед ним (если после
// «skin» ещё есть токены); иначе префикс отбрасывается, пока совпадает с
// base_id или с таблицей оружия (weapon_name.h). Оставшиеся токены
// капитализируются (латиница и кириллица) и соединяются пробелами. Если не
// осталось ничего (id и есть имя ствола), возвращается false — меню
// подписывает позицию номером («Скин 3»).
#include <cstddef>
#include <cstring>

#include "weapon_name.h"   // normalize(), find() — таблица оружия для префиксов

namespace skinname {

// Регистронезависимое сравнение токена со словом в нижнем регистре.
inline bool token_is(const char* tok, size_t len, const char* word) {
    if (len != strlen(word)) return false;
    for (size_t i = 0; i < len; ++i) {
        char c = tok[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != word[i]) return false;
    }
    return true;
}

// Сдвиг двухбайтовой кириллицы по регистру (UTF-8): а..п D0 B0..BF <-> А..П
// D0 90..9F, р..я D1 80..8F <-> Р..Я D0 A0..AF, ё D1 91 <-> Ё D0 81.
// false — это не кириллическая буква, байты копируются как есть.
inline bool cyr_shift(unsigned char c0, unsigned char c1, bool up,
                      unsigned char& o0, unsigned char& o1) {
    if (up) {
        if (c0 == 0xD0 && c1 >= 0xB0 && c1 <= 0xBF) { o0 = 0xD0; o1 = (unsigned char)(c1 - 0x20); return true; }
        if (c0 == 0xD1 && c1 >= 0x80 && c1 <= 0x8F) { o0 = 0xD0; o1 = (unsigned char)(c1 + 0x20); return true; }
        if (c0 == 0xD1 && c1 == 0x91)               { o0 = 0xD0; o1 = 0x81;                      return true; }
        return false;
    }
    if (c0 == 0xD0 && c1 >= 0x90 && c1 <= 0x9F) { o0 = 0xD0; o1 = (unsigned char)(c1 + 0x20); return true; }
    if (c0 == 0xD0 && c1 >= 0xA0 && c1 <= 0xAF) { o0 = 0xD1; o1 = (unsigned char)(c1 - 0x20); return true; }
    if (c0 == 0xD0 && c1 == 0x81)               { o0 = 0xD1; o1 = 0x91;                      return true; }
    return false;
}

// Токен в out: первый символ в верхний регистр, остальные — в нижний.
inline void append_title(const char* tok, size_t len, char* out, size_t cap, size_t& n) {
    for (size_t i = 0; i < len; ) {
        const unsigned char c = (unsigned char)tok[i];
        if ((c == 0xD0 || c == 0xD1) && i + 1 < len) {
            const unsigned char c1 = (unsigned char)tok[i + 1];
            unsigned char o0 = 0, o1 = 0;
            if (cyr_shift(c, c1, i == 0, o0, o1)) {
                if (n + 2 < cap) { out[n++] = (char)o0; out[n++] = (char)o1; }
            } else if (n + 2 < cap) {
                out[n++] = (char)c; out[n++] = (char)c1;
            }
            i += 2;
            continue;
        }
        unsigned char o = c;
        if (i == 0) { if (o >= 'a' && o <= 'z') o = (unsigned char)(o - 'a' + 'A'); }
        else        { if (o >= 'A' && o <= 'Z') o = (unsigned char)(o - 'A' + 'a'); }
        if (n + 1 < cap) out[n++] = (char)o;
        ++i;
    }
}

// Предел токенов: сами id не длиннее 48 символов (SkinEntryView.id).
constexpr int kMaxTokens = 24;

// true — out содержит читаемое имя; false — из id нечего собрать (пусто или
// целиком состоит из префиксов), вызывающий показывает позицию номером.
inline bool humanize(const char* id, const char* base_id, char* out, size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = '\0';
    if (!id || !id[0]) return false;

    const char* tok[kMaxTokens];
    size_t len[kMaxTokens];
    int nt = 0;
    for (const char* p = id; *p && nt < kMaxTokens; ) {
        while (*p && strchr("._-/ ", *p)) ++p;
        if (!*p) break;
        const char* s = p;
        while (*p && !strchr("._-/ ", *p)) ++p;
        tok[nt] = s;
        len[nt] = (size_t)(p - s);
        ++nt;
    }
    if (nt <= 0) return false;

    int first = 0;
    // «skin» — надёжный маркер: всё перед ним префикс ствола. Маркер работает,
    // только когда после него есть имя («gold_skin» — не маркер, а хвост).
    for (int i = 0; i + 1 < nt; ++i)
        if (token_is(tok[i], len[i], "skin")) { first = i + 1; break; }

    if (first == 0 && base_id && base_id[0]) {
        // Маркера нет — отрезаем префикс, равный base_id (нормализованно;
        // base_id бывает составным: «pistol_deagle»).
        char want[80], acc[80];
        weaponname::normalize(base_id, want, sizeof(want));
        size_t an = 0;
        acc[0] = '\0';
        for (int i = 0; i < nt && an + 1 < sizeof(acc) && want[0]; ++i) {
            for (size_t k = 0; k < len[i] && an + 1 < sizeof(acc); ++k) {
                char c = tok[i][k];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) acc[an++] = c;
            }
            acc[an] = '\0';
            if (!strcmp(acc, want)) { first = i + 1; break; }
            if (strncmp(acc, want, an)) break;   // разошлись — base_id не префикс
        }
    }
    // Последний шанс — таблица оружия: «dmr_gold» без base_id не должен нести
    // имя ствола в названии скина. Последний токен не отбираем.
    while (nt - first > 1) {
        char key[64];
        const size_t m = len[first] < sizeof(key) - 1 ? len[first] : sizeof(key) - 1;
        memcpy(key, tok[first], m);
        key[m] = '\0';
        char norm[64];
        weaponname::normalize(key, norm, sizeof(norm));
        if (!norm[0] || !weaponname::find(norm)) break;
        ++first;
    }
    if (nt - first <= 0) return false;

    size_t n = 0;
    for (int i = first; i < nt; ++i) {
        if (i > first && n + 1 < cap) out[n++] = ' ';
        append_title(tok[i], len[i], out, cap, n);
    }
    out[n] = '\0';
    return n > 0;
}

}  // namespace skinname
