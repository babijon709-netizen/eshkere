// Хост-тест скинченджера (skin_changer_memory.h).
//
// Проверяет то, что на устройстве не увидеть: перечисление ItemData.skinData[],
// запись индекса ТОЛЬКО по расхождению, возврат оригинала, поведение на
// пересинке SyncVar, смене ствола и смене сессии, и принудительную пересборку
// через WeaponPiece.Enabled.
#include "skin_changer_memory.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct FakeIO {
    std::map<uint64_t, uint8_t> mem;
    std::map<uint64_t, std::pair<std::string, std::string>> types;
    uint64_t fail_read = 0, fail_write = 0;
    unsigned writes = 0, reads = 0;
    std::vector<uint64_t> write_log;

    template<class T> void put(uint64_t a, T v) {
        auto* bytes = reinterpret_cast<const uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(v); ++i) mem[a + i] = bytes[i];
    }
    template<class T> bool read(uint64_t a, T& v) {
        ++reads;
        if (a == fail_read) return false;
        auto* bytes = reinterpret_cast<uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(v); ++i) {
            auto it = mem.find(a + i);
            if (it == mem.end()) return false;
            bytes[i] = it->second;
        }
        return true;
    }
    template<class T> bool write_heap(uint64_t a, T v) {
        if (a == fail_write) return false;
        ++writes;
        write_log.push_back(a);
        put(a, v);
        return true;
    }
    bool named(uint64_t a, const char* n, const char* ns) {
        auto it = types.find(a);
        return it != types.end() && it->second.first == n && it->second.second == ns;
    }
    // Unity-объект: klass в +0 и m_CachedPtr в +0x10 (MANAGED_CACHED_PTR).
    void object(uint64_t a, const char* n, const char* ns) {
        types[a] = {n, ns};
        put<uint64_t>(a, a + 0x800000);
        put<uint64_t>(a + go_active::MANAGED_CACHED_PTR, a + 0x900000);
    }
    // Обычный il2cpp-объект/массив: только klass в +0.
    void plain(uint64_t a) { put<uint64_t>(a, a + 0x800000); }
    uint64_t klass_of(uint64_t a) { return a + 0x800000; }
    template<class T> T value(uint64_t a) { T v{}; assert(read(a, v)); return v; }
};

namespace {
constexpr uint64_t kBase     = 0x1000000;   // база libil2cpp (для TypeInfo-слотов)
constexpr uint64_t player    = 0x10000;
constexpr uint64_t net       = 0x20000;
constexpr uint64_t fp        = 0x30000;
constexpr uint64_t fp_object = 0x38000;
constexpr uint64_t item      = 0x39000;
constexpr uint64_t item_data = 0x3A000;
constexpr uint64_t skins     = 0x3B000;     // SkinData[]
constexpr uint64_t skin0     = 0x3C000;     // WeaponObjectSkinData
constexpr uint64_t skin1     = 0x3D000;     // WeaponProgressionSkinData
constexpr uint64_t skin2     = 0x3E000;     // WeaponRecolorSkinData
constexpr uint64_t refs1     = 0x3F000;     // AssetReference[] у skin1
constexpr uint64_t pw        = 0x40000;     // PlayerWeapon
constexpr uint64_t fp_view   = 0x41000;     // PlayerFPWeaponView
constexpr uint64_t ref0      = 0x50000;     // AssetReference у skin0
constexpr uint64_t ref1      = 0x50100;     // AssetReference у skin1
constexpr uint64_t str_base  = 0x60000;

uint64_t piece() { return pw + go_active::PLAYERWEAPON_PIECE; }
uint64_t view_piece() { return fp_view + go_active::FPWEAPONVIEW_PIECE; }

// il2cpp System.String: int32 длина в +0x10, UTF-16 символы в +0x14.
uint64_t make_string(FakeIO& io, uint64_t& next, const std::wstring& text) {
    const uint64_t address = next;
    next += 0x200;
    io.plain(address);
    io.put<int32_t>(address + go_active::IL2CPP_STRING_LENGTH, (int32_t)text.size());
    for (size_t i = 0; i < text.size(); ++i)
        io.put<uint16_t>(address + go_active::IL2CPP_STRING_CHARS + i * 2, (uint16_t)text[i]);
    return address;
}

std::wstring widen(const char* s) {
    std::wstring out;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 0x80) { out.push_back((wchar_t)c); continue; }
        // Двухбайтный UTF-8: тест на не-ascii id (проверка кодировщика).
        unsigned char c2 = (unsigned char)*s++;
        out.push_back((wchar_t)(((c & 0x1F) << 6) | (c2 & 0x3F)));
    }
    return out;
}

// TypeInfo-слот профиля: внутренняя .data-ячейка, одно разыменование = Il2CppClass*.
void put_typeinfo(FakeIO& io, uint64_t rva, uint64_t klass) {
    if (rva) io.put<uint64_t>(kBase + rva, klass);
}

struct Fixture {
    FakeIO io;
    skinchanger::Controller<FakeIO> c{io};
    uint64_t next_string = str_base;
    int skin_count = 3;
    int16_t original_skin = 0;
    uint8_t original_level = 0;

    explicit Fixture(bool full = true) {
        using namespace go_active;
        io.object(player, "PlayerManager", "Oxide");
        io.object(net, "NetworkIdentity", "Mirror");
        io.object(fp, "FPManager", "Oxide");
        io.put<uint64_t>(player + NETWORK_BEHAVIOUR_IDENTITY, net);
        io.put<uint8_t>(net + NETWORK_IDENTITY_IS_LOCAL_PLAYER, 1);
        io.put<uint64_t>(player + PLAYER_FP_MANAGER, fp);
        // PlayerWeapon висит прямо на weaponReference (первая же проба).
        io.object(pw, "PlayerWeapon", skinchanger::kWeapons);
        io.put<uint64_t>(player + PLAYER_WEAPON_REFERENCE, pw);
        io.put<uint64_t>(pw + PLAYERWEAPON_PLAYER_BACKREF, player);
        io.put<uint64_t>(pw + PLAYERWEAPON_FP_VIEW, fp_view);
        // Копия piece, из которой FP-вьюха построена: индекс совпадает с
        // исходным, то есть на старте визуал НЕ устарел.
        io.plain(fp_view);
        io.put<int16_t>(view_piece() + WEAPONPIECE_SKIN, original_skin);
        io.put<uint8_t>(view_piece() + WEAPONPIECE_SKIN_LEVEL, original_level);
        // Сам SyncVar.
        io.put<uint8_t>(piece() + WEAPONPIECE_ENABLED, 1);
        io.put<int16_t>(piece() + WEAPONPIECE_NUMBER, 114);
        io.put<int16_t>(piece() + WEAPONPIECE_SKIN, original_skin);
        io.put<uint8_t>(piece() + WEAPONPIECE_SKIN_LEVEL, original_level);
        io.put<uint8_t>(piece() + WEAPONPIECE_LOADED, 1);
        // Ручка визуала скина: model != 0 = загружен.
        io.put<uint64_t>(pw + PLAYERWEAPON_SKIN_HANDLE + SKINHANDLE_MODEL, 0xABCD0);

        // Цепочка FP: FPObject -> Item -> ItemData -> skinData[].
        io.object(fp_object, "FPHitscan", "Oxide");
        io.put<uint64_t>(fp + FPMANAGER_CURRENT_OBJECT, fp_object);
        io.put<uint64_t>(fp_object + FPOBJECT_PLAYER_BACKREF, player);
        io.put<uint64_t>(fp_object + FPOBJECT_ITEM, item);
        io.plain(item);
        io.put<uint64_t>(item + ITEM_DATA, item_data);
        io.object(item_data, "ItemData", "Oxide");
        io.put<uint64_t>(item_data + ITEMDATA_SHORTNAME,
                         make_string(io, next_string, widen("dmr")));
        if (!full) return;
        io.plain(skins);
        io.put<int32_t>(skins + IL2CPP_ARRAY_LENGTH, skin_count);
        io.put<uint64_t>(item_data + ITEMDATA_SKIN_DATA, skins);

        // skin0 — WeaponObjectSkinData, ассет уже запрашивался (ready).
        io.plain(skin0);
        put_typeinfo(io, WEAPONOBJECTSKIN_TYPEINFO_RVA, io.klass_of(skin0));
        io.put<uint64_t>(skins + IL2CPP_ARRAY_FIRST_ELEMENT + 0 * 8, skin0);
        io.put<uint64_t>(skin0 + SKINDATA_ID, make_string(io, next_string, widen("dmr_gold")));
        io.put<uint64_t>(skin0 + SKINDATA_BASE_ID, make_string(io, next_string, widen("dmr")));
        io.plain(ref0);
        io.put<uint64_t>(ref0 + ASSETREF_OPERATION, 0x777);   // handle жива
        io.put<uint64_t>(skin0 + WEAPONSKIN_FP_REF, ref0);

        // skin1 — WeaponProgressionSkinData, три уровня, ассет НЕ запрашивался.
        io.plain(skin1);
        put_typeinfo(io, WEAPONPROGRESSIONSKIN_TYPEINFO_RVA, io.klass_of(skin1));
        io.put<uint64_t>(skins + IL2CPP_ARRAY_FIRST_ELEMENT + 1 * 8, skin1);
        io.put<uint64_t>(skin1 + SKINDATA_ID,
                         make_string(io, next_string, widen("dmr_\xD0\xB7\xD0\xB2\xD0\xB5\xD0\xB7\xD0\xB4\xD0\xB0")));
        io.plain(refs1);
        io.put<int32_t>(refs1 + IL2CPP_ARRAY_LENGTH, 3);
        io.plain(ref1);
        io.put<uint64_t>(ref1 + ASSETREF_OPERATION, 0);       // handle пуста
        io.put<uint64_t>(refs1 + IL2CPP_ARRAY_FIRST_ELEMENT, ref1);
        io.put<uint64_t>(skin1 + WEAPONSKIN_PROG_FP_REFS, refs1);

        // skin2 — WeaponRecolorSkinData (перекраска: скинченджеру не цель,
        // но в списке обязан быть помечен своим типом).
        io.plain(skin2);
        put_typeinfo(io, WEAPONRECOLORSKIN_TYPEINFO_RVA, io.klass_of(skin2));
        io.put<uint64_t>(skins + IL2CPP_ARRAY_FIRST_ELEMENT + 2 * 8, skin2);
        io.put<uint64_t>(skin2 + SKINDATA_ID, make_string(io, next_string, widen("dmr_red")));
    }

    void tick(const ViewmodelSettings& s) { c.tick(1, kBase, player, s); }
    ViewmodelSettings settings(bool on = true, int index = 1, int level = 0) const {
        ViewmodelSettings s;
        s.skin_enabled = on;
        s.skin_index = index;
        s.skin_level = level;
        return s;
    }
    int16_t live_skin() { return io.value<int16_t>(piece() + go_active::WEAPONPIECE_SKIN); }
    uint8_t live_level() { return io.value<uint8_t>(piece() + go_active::WEAPONPIECE_SKIN_LEVEL); }
    uint8_t live_enabled() { return io.value<uint8_t>(piece() + go_active::WEAPONPIECE_ENABLED); }
};

// ---- 1. Выключено — ни одной записи ---------------------------------------
void test_disabled_writes_nothing() {
    Fixture f;
    f.tick(f.settings(false));
    assert(f.io.writes == 0);
    assert(f.c.status == viewmodel::Status::Off);
    assert(!f.c.has_state());
    std::printf("  disabled: 0 writes, status off\n");
}

// ---- 2. Перечисление скинов предмета --------------------------------------
void test_enumeration() {
    Fixture f;
    f.tick(f.settings(true, 0));
    assert(f.c.status == viewmodel::Status::Active);
    viewmodel::SkinEntryView entries[skinchanger::kMaxSkins];
    const int n = f.c.entries(entries, skinchanger::kMaxSkins);
    assert(n == 3);
    assert(entries[0].index == 0 && entries[0].kind == viewmodel::SkinKind::WeaponObject);
    assert(entries[0].levels == 1 && entries[0].ready);
    assert(!std::strcmp(entries[0].id, "dmr_gold"));
    assert(!std::strcmp(entries[0].base_id, "dmr"));
    assert(entries[1].index == 1 && entries[1].kind == viewmodel::SkinKind::WeaponProgression);
    assert(entries[1].levels == 3 && !entries[1].ready);
    assert(entries[2].index == 2 && entries[2].kind == viewmodel::SkinKind::WeaponRecolor);
    assert(!viewmodel::skin_kind_is_object(entries[2].kind));
    assert(viewmodel::skin_kind_is_object(entries[0].kind));
    // Не-ascii id переживает кодировщик (UTF-16 -> UTF-8).
    assert(!std::strcmp(entries[1].id, "dmr_\xD0\xB7\xD0\xB2\xD0\xB5\xD0\xB7\xD0\xB4\xD0\xB0"));
    const viewmodel::SkinUiInfo ui = f.c.ui();
    assert(ui.count == 3 && ui.stage == 0);
    assert(!std::strcmp(ui.weapon, "dmr"));
    assert(ui.handle_live && !ui.fp_stale);
    std::printf("  enumeration: 3 skins, kinds/levels/ready/ids ok, weapon=\"%s\"\n", ui.weapon);
}

// ---- 3. Запись только по расхождению --------------------------------------
void test_write_only_on_divergence() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.live_skin() == 1);
    const unsigned after_first = f.io.writes;
    assert(after_first > 0);
    // Установившийся режим: значение уже наше — ноль записей на тик.
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.io.writes == after_first);
    assert(f.live_skin() == 1);
    std::printf("  steady state: %u writes once, 0 writes over 10 more ticks\n", after_first);
}

// ---- 4. Игра вернула своё — запись возвращается ---------------------------
void test_resync_is_reapplied() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_skin() == 2);
    const unsigned before = f.io.writes;
    // Пересинк SyncVar: сервер положил свой индекс.
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, 0);
    f.tick(f.settings(true, 2));
    assert(f.io.writes > before);
    assert(f.live_skin() == 2);
    std::printf("  resync: game value overwritten back to ours\n");
}

// ---- 5. Индекса нет в массиве предмета — записи нет -----------------------
void test_unknown_index_rejected() {
    Fixture f;
    f.tick(f.settings(true, 9));
    assert(f.c.status == viewmodel::Status::Failed);
    assert(f.c.stage() == 13);
    assert(f.io.writes == 0);
    assert(f.live_skin() == f.original_skin);
    std::printf("  index 9 not in skinData: failed(13), 0 writes\n");
}

// ---- 6. Уровень зажимается числом уровней скина ---------------------------
void test_level_clamped_to_progression() {
    Fixture f;
    f.tick(f.settings(true, 1, 2));
    assert(f.live_skin() == 1 && f.live_level() == 2);
    // Вне диапазона массива уровней — берём последний существующий.
    f.tick(f.settings(true, 1, 7));
    assert(f.live_level() == 2);
    // У скина без уровней уровень всегда 0.
    f.tick(f.settings(true, 0, 5));
    assert(f.live_skin() == 0 && f.live_level() == 0);
    std::printf("  level clamped: progression<=2, object skin==0\n");
}

// ---- 7. Выключение возвращает оригинал ------------------------------------
void test_disable_restores_original() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_skin() == 2);
    f.tick(f.settings(false));
    assert(f.live_skin() == f.original_skin);
    assert(f.live_level() == f.original_level);
    assert(!f.c.has_state());
    assert(f.c.status == viewmodel::Status::Off);
    std::printf("  disable: original skin %d restored\n", (int)f.original_skin);
}

// ---- 8. Возврат не затирает чужое обновление ------------------------------
void test_restore_does_not_clobber_game() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_skin() == 2);
    // Игра сама сменила скин (другой ствол/пересинк) — возвращать наш
    // оригинал поверх её значения нельзя.
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, 1);
    f.tick(f.settings(false));
    assert(f.live_skin() == 1);
    assert(!f.c.has_state());
    std::printf("  restore skipped over game's own update\n");
}

// ---- 9. Смена сессии: адреса забываются БЕЗ записи ------------------------
void test_session_change_forgets_without_write() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.c.has_state());
    const unsigned before = f.io.writes;
    f.c.tick(2, kBase, player, f.settings(true, 2));   // другая сессия
    // reset(false) не пишет ничего: процесс мог умереть, запись опаснее.
    assert(f.io.writes == before);
    std::printf("  session change: forgotten without a write\n");
}

// Другой FPObject -> другой Item -> другой ItemData, в котором один скин.
void switch_to_spear(Fixture& f) {
    constexpr uint64_t item2 = 0x70000, data2 = 0x71000, arr2 = 0x72000, skin_x = 0x73000;
    constexpr uint64_t player_ = player;
    using namespace go_active;
    {
        f.io.object(0x78000, "FPSpear", "Oxide");
        f.io.put<uint64_t>(fp + FPMANAGER_CURRENT_OBJECT, 0x78000);
        f.io.put<uint64_t>(0x78000 + FPOBJECT_PLAYER_BACKREF, player_);
        f.io.put<uint64_t>(0x78000 + FPOBJECT_ITEM, item2);
        f.io.plain(item2);
        f.io.put<uint64_t>(item2 + ITEM_DATA, data2);
        f.io.object(data2, "ItemData", "Oxide");
        f.io.plain(arr2);
        f.io.put<int32_t>(arr2 + IL2CPP_ARRAY_LENGTH, 1);
        f.io.put<uint64_t>(arr2 + IL2CPP_ARRAY_FIRST_ELEMENT, skin_x);
        f.io.put<uint64_t>(data2 + ITEMDATA_SKIN_DATA, arr2);
        f.io.plain(skin_x);
        put_typeinfo(f.io, WEAPONOBJECTSKIN_TYPEINFO_RVA, f.io.klass_of(skin_x));
        f.io.put<uint64_t>(skin_x + SKINDATA_ID, f.next_string);
        f.io.plain(f.next_string);
        const char* id = "spear";
        const int id_len = (int)std::strlen(id);
        f.io.put<int32_t>(f.next_string + IL2CPP_STRING_LENGTH, id_len);
        for (int i = 0; i < id_len; ++i)
            f.io.put<uint16_t>(f.next_string + IL2CPP_STRING_CHARS + i * 2, (uint16_t)id[i]);
    }
}

// ---- 10. Смена ствола: старый возвращаем, новый перехватываем -------------
void test_weapon_switch() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_skin() == 2);
    switch_to_spear(f);
    f.tick(f.settings(true, 2));
    // Индекс 2 у нового предмета отсутствует — запись отклонена, не мусор.
    assert(f.c.stage() == 13);
    viewmodel::SkinEntryView entries[skinchanger::kMaxSkins];
    assert(f.c.entries(entries, skinchanger::kMaxSkins) == 1);
    assert(!std::strcmp(entries[0].id, "spear"));
    f.tick(f.settings(true, 0));
    assert(f.live_skin() == 0);
    std::printf("  weapon switch: list re-read, out-of-range index rejected\n");
}

// ---- 10б. Смена ствола ВО ВРЕМЯ пересборки: ствол не должен остаться убранным
void test_switch_during_rebuild() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 1);
    f.tick(s);
    f.c.request_rebuild();
    f.tick(s);
    assert(f.live_enabled() == 0);
    assert(f.c.rebuild_busy());
    switch_to_spear(f);
    f.tick(f.settings(true, 0));
    // Enabled старого ствола возвращён, отсчёт не перенесён на новый.
    assert(f.live_enabled() == 1);
    assert(!f.c.rebuild_busy());
    assert(f.live_skin() == 0);
    std::printf("  switch during rebuild: Enabled returned, countdown dropped\n");
}

// ---- 11. Принудительная пересборка через Enabled --------------------------
void test_forced_rebuild() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 1);
    f.tick(s);
    assert(f.live_skin() == 1);
    assert(f.live_enabled() == 1);
    assert(!f.c.rebuild_busy());
    f.c.request_rebuild();
    f.tick(s);
    // Ствол убран, индекс уже наш — дроу соберёт вьюху из него.
    assert(f.live_enabled() == 0);
    assert(f.c.rebuild_busy());
    assert(f.live_skin() == 1);
    // Держится kRebuildHoldTicks тиков, возвращается на следующем.
    for (int i = 0; i < skinchanger::kRebuildHoldTicks; ++i) {
        f.tick(s);
        assert(f.live_skin() == 1);         // индекс не теряется, пока ствол убран
    }
    assert(f.live_enabled() == 1);
    assert(!f.c.rebuild_busy());
    assert(f.c.status == viewmodel::Status::Active);
    std::printf("  rebuild: Enabled 1->0 for %d ticks ->1, skin applied before draw\n",
                skinchanger::kRebuildHoldTicks);
}

// ---- 12. Автопересборка при смене индекса ---------------------------------
void test_auto_rebuild_on_change() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 1);
    s.skin_rebuild = true;
    f.tick(s);
    // Первое применение — тоже смена, поэтому ствол убирается.
    assert(f.live_enabled() == 0);
    for (int i = 0; i < skinchanger::kRebuildHoldTicks; ++i) f.tick(s);
    assert(f.live_enabled() == 1);
    // Установившийся режим: пересборок больше нет.
    const unsigned before = f.io.writes;
    f.tick(s);
    assert(f.live_enabled() == 1);
    assert(f.io.writes == before);
    std::printf("  auto rebuild: fires on change only, silent in steady state\n");
}

// ---- 13. fp_stale: FP-вьюха собрана из другого индекса --------------------
void test_fp_stale_flag() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_skin() == 2);
    viewmodel::SkinUiInfo ui = f.c.ui();
    assert(ui.fp_stale);            // вьюха всё ещё из original_skin
    assert(ui.skin == 2);
    // Игра пересобрала вьюху — расхождение пропало.
    f.io.put<int16_t>(view_piece() + go_active::WEAPONPIECE_SKIN, 2);
    f.tick(f.settings(true, 2));
    ui = f.c.ui();
    assert(!ui.fp_stale);
    std::printf("  fp_stale tracks PlayerFPWeaponView's built-from index\n");
}

// ---- 14. Нет массива скинов — честная стадия, записи нет ------------------
void test_no_skin_array() {
    Fixture f(false);   // без skinData
    f.tick(f.settings(true, 1));
    // Нерезолв — это ожидание, а не отказ (как у no_recoil/weapon_tuning):
    // статус Waiting, а причину несёт стадия, её и показывает меню.
    assert(f.c.status == viewmodel::Status::Waiting);
    assert(f.c.stage() == 11);
    assert(f.c.ui().stage == 11);
    assert(f.io.writes == 0);
    assert(f.c.entries(nullptr, 0) == 0);
    std::printf("  no skinData array: waiting + stage 11, 0 writes\n");
}

// ---- 15. Не локальный игрок — записи нет ----------------------------------
void test_not_local_player() {
    Fixture f;
    f.io.put<uint8_t>(net + go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER, 0);
    f.tick(f.settings(true, 1));
    assert(f.c.status != viewmodel::Status::Active);
    assert(f.io.writes == 0);
    std::printf("  not local player: no writes\n");
}

// ---- 16. Отказ записи не оставляет половину состояния --------------------
void test_write_failure_is_reported() {
    Fixture f;
    f.io.fail_write = piece() + go_active::WEAPONPIECE_SKIN;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Failed);
    assert(f.c.stage() == 16);
    assert(f.live_skin() == f.original_skin);
    std::printf("  rejected write: failed(16), field untouched\n");
}

// ---- 17. Санитайзер не выпускает индекс/уровень за пределы ---------------
void test_sanitize_bounds() {
    ViewmodelSettings s;
    s.skin_index = 5000;
    s.skin_level = 90;
    s = viewmodel::sanitize(s);
    assert(s.skin_index == 0);      // вне диапазона — откат в дефолт
    assert(s.skin_level == 0);
    s.skin_index = 7; s.skin_level = 3;
    s = viewmodel::sanitize(s);
    assert(s.skin_index == 7 && s.skin_level == 3);
    s.skin_index = -4;
    s = viewmodel::sanitize(s);
    assert(s.skin_index == 0);
    std::printf("  sanitize: index/level clamped to the offset-table limits\n");
}

// ---- 18. Бета-профиль: структура 0x18, поле EmptyHands --------------------
void test_beta_profile_layout() {
    go_active::Select(go_active::GameVersion::Beta);
    assert(go_active::WEAPONPIECE_SIZE == 0x18);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0x10);
    assert(go_active::WEAPONPIECE_SKIN == 0x4);
    assert(go_active::WEAPONPIECE_SKIN_LEVEL == 0x6);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0x108);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x40);
    assert(go_active::ITEMDATA_SKIN_DATA == 0xD0);
    assert(go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0xE64B9E0);
    assert(go_active::EG_TYPEINFO_RVA == 0);   // в бете резолвится по имени
    go_active::Select(go_active::GameVersion::Release);
    assert(go_active::WEAPONPIECE_SIZE == 0x10);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0xF0);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x38);
    assert(go_active::EG_TYPEINFO_RVA == 0xDE9ACA0);
    std::printf("  profiles: release 0x10 / beta 0x18, TypeInfo slots pinned\n");
}
}  // namespace

int main() {
    std::printf("skin changer:\n");
    test_disabled_writes_nothing();
    test_enumeration();
    test_write_only_on_divergence();
    test_resync_is_reapplied();
    test_unknown_index_rejected();
    test_level_clamped_to_progression();
    test_disable_restores_original();
    test_restore_does_not_clobber_game();
    test_session_change_forgets_without_write();
    test_weapon_switch();
    test_switch_during_rebuild();
    test_forced_rebuild();
    test_auto_rebuild_on_change();
    test_fp_stale_flag();
    test_no_skin_array();
    test_not_local_player();
    test_write_failure_is_reported();
    test_sanitize_bounds();
    test_beta_profile_layout();
    std::printf("OK\n");
    return 0;
}
