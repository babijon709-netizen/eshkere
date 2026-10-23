// Хост-тест скинченджера (skin_changer_memory.h).
//
// Проверяет то, что на устройстве не увидеть: перечисление ItemData.skinData[],
// запись индекса ТОЛЬКО по расхождению в ОБЕ копии WeaponPiece (SyncVar +0x110
// и рабочую +0xC0 — из второй игра собирает FP-вьюху на дроу), возврат
// оригинала, поведение на пересинке SyncVar, смене ствола и смене сессии, и
// работу с головой рабочей копии: Enabled в SyncVar не пишется НИКОГДА, а
// погашенная игрой голова (+0xC0, Enabled=0) достраивается зеркалом из
// SyncVar — погашенная голова превращает дроу в Release вьюхи вместо сборки
// (KERNEL.md §4.21, заголовок skin_changer_memory.h).
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
    // Диапазонное чтение: голова WeaponPiece берётся одним запросом (8 байт).
    bool read_range(uint64_t a, void* out, std::size_t size) {
        auto* bytes = static_cast<uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i)
            if (!read(a + i, bytes[i])) return false;
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
constexpr int32_t  kItemDataId = 4242;      // ItemData.m_Id ствола из фикстуры

uint64_t piece() { return pw + go_active::PLAYERWEAPON_PIECE; }
uint64_t piece_local() { return pw + go_active::PLAYERWEAPON_PIECE_LOCAL; }
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

// Имя Il2CppClass: klass->name (IL2CPP_CLASS_NAME) -> ascii с NUL, как в
// метаданных игры. Нужен резервному резолву типа по имени (kind_of).
uint64_t next_klass_name = 0x70000;
void put_klass_name(FakeIO& io, uint64_t obj, const char* name) {
    const uint64_t at = next_klass_name;
    next_klass_name += 0x40;
    for (size_t i = 0; i <= std::strlen(name); ++i)
        io.put<uint8_t>(at + i, (uint8_t)name[i]);
    io.put<uint64_t>(io.klass_of(obj) + go_active::IL2CPP_CLASS_NAME, at);
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
        // Голова piece читается ОДНИМ запросом на 8 байт, поэтому отображены
        // должны быть все байты, включая паддинг между Enabled (0x0) и
        // Number (0x2) — в живой памяти он есть, а в плоской карте FakeIO нет.
        for (uint64_t off = 0; off < (uint64_t)skinchanger::kPieceHead; ++off) {
            io.put<uint8_t>(view_piece() + off, 0);
            io.put<uint8_t>(piece() + off, 0);
            io.put<uint8_t>(piece_local() + off, 0);
        }
        io.put<uint8_t>(view_piece() + WEAPONPIECE_ENABLED, 1);
        io.put<int16_t>(view_piece() + WEAPONPIECE_NUMBER, 114);
        io.put<int16_t>(view_piece() + WEAPONPIECE_SKIN, original_skin);
        io.put<uint8_t>(view_piece() + WEAPONPIECE_SKIN_LEVEL, original_level);
        io.put<uint8_t>(view_piece() + WEAPONPIECE_LOADED, 1);
        // Сам SyncVar.
        io.put<uint8_t>(piece() + WEAPONPIECE_ENABLED, 1);
        io.put<int16_t>(piece() + WEAPONPIECE_NUMBER, 114);
        io.put<int16_t>(piece() + WEAPONPIECE_SKIN, original_skin);
        io.put<uint8_t>(piece() + WEAPONPIECE_SKIN_LEVEL, original_level);
        io.put<uint8_t>(piece() + WEAPONPIECE_LOADED, 1);
        // Рабочая копия (QVN +0xC0): в игре её пишет pMd из той же piece,
        // поэтому на старте обе копии совпадают.
        io.put<uint8_t>(piece_local() + WEAPONPIECE_ENABLED, 1);
        io.put<int16_t>(piece_local() + WEAPONPIECE_NUMBER, 114);
        io.put<int16_t>(piece_local() + WEAPONPIECE_SKIN, original_skin);
        io.put<uint8_t>(piece_local() + WEAPONPIECE_SKIN_LEVEL, original_level);
        io.put<uint8_t>(piece_local() + WEAPONPIECE_LOADED, 1);
        // Ручка визуала скина: model != 0 = загружен.
        io.put<uint64_t>(pw + PLAYERWEAPON_SKIN_HANDLE + SKINHANDLE_MODEL, 0xABCD0);

        // Цепочка FP: FPObject -> Item -> ItemData -> skinData[].
        io.object(fp_object, "FPHitscan", "Oxide");
        io.put<uint64_t>(fp + FPMANAGER_CURRENT_OBJECT, fp_object);
        io.put<uint64_t>(fp_object + FPOBJECT_PLAYER_BACKREF, player);
        // §4.24: PlayerEventHandler (+0xC8) жив — у здорового панциря (медленный
        // путь §4.22) и ссылка, и обработчик на месте. Быстрый путь сброса
        // срабатывает на ПОГАШЕННОМ обработчике: у FPBow.FDN на нуле NRE, а
        // гейт CreateFPWeapon зовёт FDN до очистки _currentWeapon.
        io.put<uint64_t>(fp_object + FPOBJECT_EVENT_HANDLER, 0x42000);
        io.put<uint64_t>(fp_object + FPOBJECT_ITEM, item);
        // Здоровый инвариант FPObject (§4.22): uhq==1 <=> Item!=null. Детектор
        // панциря ищет обратное — uhq==0 при Item==null десятки секунд.
        io.put<uint8_t>(fp_object + FPOBJECT_SHOWN, 1);
        io.plain(item);
        io.put<uint64_t>(item + ITEM_DATA, item_data);
        // Oxide.Item.skin (+0x30, int32) / skinLevel (+0x34) — третья точка
        // записи: дроп и сохранение несут её (KERNEL.md §4.17–4.18).
        io.put<int32_t>(item + ITEM_SKIN, original_skin);
        io.put<uint8_t>(item + ITEM_SKIN_LEVEL, original_level);
        io.object(item_data, "ItemData", "Oxide");
        io.put<uint64_t>(item_data + ITEMDATA_SHORTNAME,
                         make_string(io, next_string, widen("dmr")));
        // ItemData.m_Id: карантин «сломал сборку» запоминает ствол по нему —
        // класс общий у нескольких предметов, id свой (§4.22).
        io.put<int32_t>(item_data + ITEMDATA_ID, kItemDataId);
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
    int16_t live_skin_local() {
        return io.value<int16_t>(piece_local() + go_active::WEAPONPIECE_SKIN);
    }
    uint8_t live_level_local() {
        return io.value<uint8_t>(piece_local() + go_active::WEAPONPIECE_SKIN_LEVEL);
    }
    uint8_t live_enabled_local() {
        return io.value<uint8_t>(piece_local() + go_active::WEAPONPIECE_ENABLED);
    }
    int32_t live_item_skin() { return io.value<int32_t>(item + go_active::ITEM_SKIN); }
    uint8_t live_item_level() {
        return io.value<uint8_t>(item + go_active::ITEM_SKIN_LEVEL);
    }
    // Enabled в SyncVar не меняется никогда; в рабочей копии — только
    // зеркалом погашенной головы (§4.21): живую голову тик не трогает.
    bool enabled_untouched() const {
        for (uint64_t a : io.write_log)
            if (a == piece() + go_active::WEAPONPIECE_ENABLED ||
                a == piece_local() + go_active::WEAPONPIECE_ENABLED)
                return false;
        return true;
    }
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
    assert(ui.local_state == 0);  // рабочая копия держит тот же индекс
    std::printf("  enumeration: 3 skins, kinds/levels/ready/ids ok, weapon=\"%s\"\n", ui.weapon);
}

// ---- 3. Запись только по расхождению, в обе копии --------------------------
void test_write_only_on_divergence() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.live_skin() == 1);
    assert(f.live_skin_local() == 1);   // рабочая копия — вторая цель записи
    assert(f.live_item_skin() == 1);    // Oxide.Item.skin — третья цель записи
    assert(f.enabled_untouched());
    const unsigned after_first = f.io.writes;
    assert(after_first > 0);
    // Установившийся режим: значение уже наше — ноль записей на тик.
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.io.writes == after_first);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1 && f.live_item_skin() == 1);
    std::printf("  steady state: %u writes once (all three copies), 0 over 10 more ticks\n",
                after_first);
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
    // То же для рабочей копии: её перезаписывает pMd (RPC/спавн).
    f.io.put<int16_t>(piece_local() + go_active::WEAPONPIECE_SKIN, 0);
    f.tick(f.settings(true, 2));
    assert(f.live_skin_local() == 2);
    assert(f.c.ui().local_state == 0);
    // То же для Oxide.Item: крафт/пересинк перезаписывает скин предмета.
    f.io.put<int32_t>(item + go_active::ITEM_SKIN, 0);
    f.tick(f.settings(true, 2));
    assert(f.live_item_skin() == 2);
    assert(f.c.ui().item_state == 0);
    std::printf("  resync: game value overwritten back to ours (all copies)\n");
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

// ---- 7. Выключение возвращает оригинал (в обе копии) -----------------------
void test_disable_restores_original() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_skin() == 2 && f.live_skin_local() == 2);
    f.tick(f.settings(false));
    assert(f.live_skin() == f.original_skin);
    assert(f.live_level() == f.original_level);
    assert(f.live_skin_local() == f.original_skin);
    assert(f.live_level_local() == f.original_level);
    assert(!f.c.has_state());
    assert(f.c.status == viewmodel::Status::Off);
    std::printf("  disable: original skin %d restored in both copies\n",
                (int)f.original_skin);
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

// ---- 10б. Запрос «переписать обе копии»: запись даже без расхождения -------
void test_rewrite_request() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 1);
    f.tick(s);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1);
    // Установившийся режим: ноль записей.
    f.tick(s);
    const unsigned before = f.io.writes;
    f.tick(s);
    assert(f.io.writes == before);
    // Запрос снимает «свежесть» слотов: следующий тик пишет все шесть полей
    // (Skin/SkinLevel в обеих копиях piece + Oxide.Item.skin/skinLevel) и
    // подтверждает каждую запись readback'ом.
    f.c.request_rebuild();
    f.tick(s);
    assert(f.io.writes == before + 6);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1 && f.live_item_skin() == 1);
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.enabled_untouched());
    std::printf("  rewrite request: 6 forced writes, Enabled untouched\n");
}

// ---- 10б'. Oxide.Item.skin — третья точка записи (дроп и сейв несут её) ----
void test_item_skin_written_and_restored() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 1, 2);   // прогрессия, уровень 2
    f.tick(s);
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.live_item_skin() == 1);        // Item.skin (int32 @ +0x30)
    assert(f.live_item_level() == 2);       // Item.skinLevel (byte @ +0x34)
    assert(f.c.ui().item_state == 0);       // Item держит индекс из piece
    // Игра перезаписала Item (крафт/пересинк) — следующий тик восстановит,
    // а новым оригиналом станет значение игры.
    f.io.put<int32_t>(item + go_active::ITEM_SKIN, 0);
    f.tick(s);
    assert(f.live_item_skin() == 1);
    f.tick(f.settings(false));
    assert(f.live_item_skin() == 0);        // возвращено значение игры
    assert(f.live_item_level() == f.original_level);
    assert(f.c.ui().item_state == -1);      // выключено — состояние сброшено
    assert(!f.c.has_state());
    std::printf("  item skin: written with level, resynced, restored on disable\n");
}

// ---- 10б''. Нерезолв не стирает записанное (мир/смерть — переходные) --------
void test_unresolved_keeps_writes() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.live_skin() == 1);
    // Игрок временно пропал (identity не локальная): прежняя версия звала здесь
    // restore_all() и стирала SyncVar/Item ровно перед спавном, которому наш
    // индекс нужен для пересборки (устройство: skin.log v12, +60 с).
    f.io.put<uint8_t>(net + go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER, 0);
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 1));
    assert(f.io.writes == before);            // ни возврата, ни записи
    assert(f.c.has_state());
    assert(f.live_skin() == 1 && f.live_item_skin() == 1);
    // Игрок вернулся — значения всё ещё наши: тик снова без записей.
    f.io.put<uint8_t>(net + go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER, 1);
    f.tick(f.settings(true, 1));
    assert(f.io.writes == before);
    // Явное выключение при живом контексте по-прежнему возвращает оригиналы.
    f.tick(f.settings(false));
    assert(f.io.writes > before);
    assert(f.live_skin() == f.original_skin);
    assert(f.live_item_skin() == f.original_skin);
    std::printf("  unresolved: slots kept, no wipe; disable still restores\n");
}

// ---- 10б'''. Мёртвый контекст: владение снимается БЕЗ записи ----------------
void test_dead_context_released_without_restore() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 1, 2);
    f.tick(s);
    assert(f.live_skin() == 1 && f.live_item_skin() == 1 && f.live_item_level() == 2);
    // PlayerWeapon умер (игрок покинул мир): запись по старому адресу — порча
    // освобождённой кучи. Piece-слоты снимают владение без записи; Item жив
    // (лежит в инвентаре) — его оригинал возвращается игре.
    f.io.put<uint64_t>(pw, 0xDEAD);
    f.tick(s);                                // нерезолв: слоты не тронуты
    assert(f.c.has_state());
    const unsigned before = f.io.writes;
    f.tick(f.settings(false));                // выключение: restore_all с life()
    assert(f.io.writes == before + 2);        // только Item.skin/skinLevel
    assert(f.live_skin() == 1);               // мёртвый PW не тронут
    assert(f.live_item_skin() == f.original_skin &&
           f.live_item_level() == f.original_level);
    assert(!f.c.has_state());
    std::printf("  dead PW: piece released without writes, live item restored\n");
}

// ---- 10в. Enabled не пишется никогда ---------------------------------------
void test_enabled_never_written() {
    Fixture f;
    ViewmodelSettings s = f.settings(true, 2);
    for (int i = 0; i < 6; ++i) f.tick(s);
    f.c.request_rebuild();
    f.tick(s);
    assert(f.live_enabled() == 1 && f.live_enabled_local() == 1);
    assert(f.enabled_untouched());
    f.tick(f.settings(false));
    assert(f.live_enabled() == 1 && f.live_enabled_local() == 1);
    assert(f.enabled_untouched());
    std::printf("  Enabled stays 1 in both copies through apply/rewrite/disable\n");
}

// ---- 10г. Голова рабочей копии не читается / не WeaponPiece: стадии 20/21 --
void test_local_copy_bad_head() {
    {
        Fixture f;
        f.io.fail_read = piece_local() + go_active::WEAPONPIECE_ENABLED;
        f.tick(f.settings(true, 1));
        assert(f.c.status == viewmodel::Status::Failed);
        assert(f.c.stage() == 20);
        assert(viewmodel::skin_block(20) == viewmodel::SkinBlock::Unreadable);
        // SyncVar-копия к этому моменту уже записана — половина применения
        // видна в логе стадией, а не молчаливым «не сработало».
        assert(f.live_skin() == 1);
        std::printf("  local head unreadable: stage 20, sync copy applied\n");
    }
    {
        Fixture f;
        f.io.put<uint8_t>(piece_local() + go_active::WEAPONPIECE_ENABLED, 0x5A);
        f.tick(f.settings(true, 1));
        assert(f.c.stage() == 21);
        assert(viewmodel::skin_block(21) == viewmodel::SkinBlock::WrongLayout);
        // Отклонённая голова видна в снимке сырыми байтами — «стадия 21» без
        // них не отличает мусор от неканонических Loaded/Number.
        assert(f.c.ui().local_head[0] != '\0');
        assert(std::strstr(f.c.ui().local_head, "5a") != nullptr);
        std::printf("  local head not a WeaponPiece: stage 21, raw head shown\n");
    }
}

// ---- 10е. Щадящая валидация рабочей копии ----------------------------------
void test_local_copy_relaxed_head() {
    {
        Fixture f;
        // Не-bool Loaded в рабочей копии: собственный путь дроу игры смотрит
        // только Enabled (pMZ -> pFz -> Kxi -> CreateFPWeapon), поэтому строгая
        // валидация здесь парковала исправный билд на стадии 21 навсегда
        // (устройство это показало: SyncVar живой, вьюха живая).
        f.io.put<uint8_t>(piece_local() + go_active::WEAPONPIECE_LOADED, 0x7F);
        f.tick(f.settings(true, 1));
        assert(f.c.stage() == 0);
        assert(f.c.status == viewmodel::Status::Active);
        assert(f.live_skin() == 1 && f.live_skin_local() == 1);
        assert(f.c.ui().local_state == 0);
        assert(f.c.ui().local_head[0] != '\0');   // неканоничность видна в логе
        assert(f.enabled_untouched());
    }
    {
        Fixture f;
        // Мусорный Number — тоже не повод отказывать: индекс пишется в
        // Skin/SkinLevel, Number мы не трогаем.
        f.io.put<int16_t>(piece_local() + go_active::WEAPONPIECE_NUMBER, -7);
        f.tick(f.settings(true, 1));
        assert(f.c.stage() == 0);
        assert(f.live_skin_local() == 1);
        assert(f.io.value<int16_t>(piece_local() + go_active::WEAPONPIECE_NUMBER) == -7);
    }
    {
        Fixture f;
        // SyncVar-копия остаётся СТРОГОЙ: не-bool Loaded там — по-прежнему
        // «не WeaponPiece» (стадия 15), раскладка не та.
        f.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED, 0x7F);
        f.tick(f.settings(true, 1));
        assert(f.c.stage() == 15);
        assert(f.io.writes == 0);
    }
    std::printf("  relaxed local head: accepted via Enabled, sync stays strict\n");
}

// ---- 10д. Отказ записи в рабочую копию: стадия 18/19 -----------------------
void test_local_copy_write_failure() {
    Fixture f;
    f.io.fail_write = piece_local() + go_active::WEAPONPIECE_SKIN;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Failed);
    assert(f.c.stage() == 18);
    assert(f.live_skin() == 1);              // SyncVar-копия записана
    assert(f.live_skin_local() == f.original_skin);
    std::printf("  local copy write rejected: stage 18\n");
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
    // Отказ в первой копии останавливает тик: вторая не тронута.
    assert(f.live_skin_local() == f.original_skin);
    std::printf("  rejected write: failed(16), both fields untouched\n");
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
    assert(go_active::PLAYERWEAPON_PIECE == 0x130);
    assert(go_active::PLAYERWEAPON_PIECE_LOCAL == 0xD0);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0x108);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x40);
    assert(go_active::ITEMDATA_SKIN_DATA == 0xD0);
    assert(go_active::ITEM_SKIN == 0x30);          // Oxide.Item идентичен релизу
    assert(go_active::ITEM_SKIN_LEVEL == 0x34);
    assert(go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0xE64B9E0);
    assert(go_active::EG_TYPEINFO_RVA == 0);   // в бете резолвится по имени
    go_active::Select(go_active::GameVersion::Release);
    assert(go_active::WEAPONPIECE_SIZE == 0x10);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0);
    assert(go_active::PLAYERWEAPON_PIECE == 0x110);
    assert(go_active::PLAYERWEAPON_PIECE_LOCAL == 0xC0);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0xF0);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x38);
    assert(go_active::ITEM_SKIN == 0x30);
    assert(go_active::ITEM_SKIN_LEVEL == 0x34);
    assert(go_active::EG_TYPEINFO_RVA == 0xDE9ACA0);
    std::printf("  profiles: release 0x10 / beta 0x18, TypeInfo slots pinned\n");
}
}  // namespace

// ---- Отрицательный Skin в памяти: «скина нет», а не «не читается» ----------
void test_negative_skin_is_accepted() {
    Fixture f;
    // У ствола без надетого скина в SyncVar лежит отрицательное значение.
    // Range-проверка прочитанного значения резала его, и скинченджер вставал на
    // «поле скина не читается» на полностью исправном билде.
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, -1);
    f.tick(f.settings(true, 1));
    assert(f.c.stage() == 0);
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.live_skin() == 1);
    assert(f.c.ui().piece_read);
    // Оригинал возвращается тем значением, которое было, а не нулём —
    // в обеих копиях piece и в Oxide.Item.
    f.tick(f.settings(false));
    assert(f.live_skin() == -1);
    assert(f.live_item_skin() == f.original_skin);
    std::printf("  original skin=-1: applied, then restored as -1\n");
}

// ---- Значение больше старого предела 63 тоже легитимно --------------------
void test_large_skin_value_in_memory() {
    Fixture f;
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, 3000);
    f.tick(f.settings(true, 2));
    assert(f.c.stage() == 0);
    assert(f.live_skin() == 2);
    f.tick(f.settings(false));
    assert(f.live_skin() == 3000);
    std::printf("  original skin=3000: accepted, restored\n");
}

// ---- Голова piece не читается транспортом: стадия 14 ----------------------
void test_piece_unreadable() {
    Fixture f;
    f.io.fail_read = piece() + go_active::WEAPONPIECE_ENABLED;   // первый байт головы
    f.tick(f.settings(true, 1));
    assert(f.c.stage() == 14);
    assert(f.c.status == viewmodel::Status::Failed);
    assert(viewmodel::skin_block(14) == viewmodel::SkinBlock::Unreadable);
    const viewmodel::SkinUiInfo ui = f.c.ui();
    assert(!ui.piece_read && ui.skin == -1);
    assert(f.io.writes == 0);           // не пишем, пока не прочитали
    std::printf("  piece unreadable: stage 14, 0 writes\n");
}

// ---- Прочиталось, но это не WeaponPiece: стадия 15 ------------------------
void test_piece_wrong_layout() {
    Fixture f;
    // Enabled обязан быть bool: мусор на его месте означает, что
    // PLAYERWEAPON_PIECE указывает не на WeaponPiece (оффсет не от этого билда).
    f.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_ENABLED, 0x5A);
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, 1234);
    f.tick(f.settings(true, 1));
    assert(f.c.stage() == 15);
    assert(viewmodel::skin_block(15) == viewmodel::SkinBlock::WrongLayout);
    const viewmodel::SkinUiInfo ui = f.c.ui();
    // Сырое значение показывается: мусорный Skin в логе видно глазами, и это
    // отличает «оффсет не от билда» от «транспорт не читает».
    assert(ui.piece_read && ui.skin == 1234);
    assert(f.io.writes == 0);
    std::printf("  not a WeaponPiece: stage 15, raw skin=%d shown\n", ui.skin);
}

// ---- Number вне диапазона — та же причина --------------------------------
void test_piece_bad_number() {
    Fixture f;
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_NUMBER, -7);
    f.tick(f.settings(true, 1));
    assert(f.c.stage() == 15);
    assert(f.io.writes == 0);
    std::printf("  negative item number: rejected as wrong layout\n");
}

// Группировка стадий в причины (viewmodel.h). Её берут и меню, и автолог
// skin.log, поэтому проверяем не «как-то работает», а что все 21 стадия куда-то
// попадают, что у каждой из 9 причин свой непустой текст и что стадии одного
// смысла не разъехались по разным причинам.
void test_skin_block_grouping() {
    using viewmodel::SkinBlock;
    assert(viewmodel::skin_block(0) == SkinBlock::None);
    assert(viewmodel::skin_block(28) != SkinBlock::None);   // вне диапазона — не «порядок»
    assert(viewmodel::skin_block(-1) != SkinBlock::None);

    const char* texts[16] = {};
    int distinct = 0;
    for (int stage = 1; stage <= 27; ++stage) {
        const SkinBlock b = viewmodel::skin_block(stage);
        assert(b != SkinBlock::None);
        const char* text = viewmodel::skin_block_text(b);
        assert(text && text[0]);
        bool seen = false;
        for (int i = 0; i < distinct; ++i) if (texts[i] == text) { seen = true; break; }
        if (!seen) {
            assert(distinct < (int)(sizeof(texts) / sizeof(texts[0])));
            texts[distinct++] = text;
            for (int i = 0; i < distinct - 1; ++i)
                assert(std::strcmp(texts[i], text) != 0);   // формулировки не дублируются
        }
    }
    assert(distinct == 11);     // ровно 11 причин, ни одна стадия не потерялась

    // Конкретные соответствия, которые видит пользователь.
    assert(viewmodel::skin_block(5)  == SkinBlock::NoWeapon);
    // «не читается» и «прочиталось, но это не WeaponPiece» — разные причины:
    // первая про транспорт, вторая про оффсеты не от этого билда. Пара есть
    // у каждой копии piece: 14/15 — SyncVar, 20/21 — рабочая.
    assert(viewmodel::skin_block(14) == SkinBlock::Unreadable);
    assert(viewmodel::skin_block(15) == SkinBlock::WrongLayout);
    assert(viewmodel::skin_block(20) == SkinBlock::Unreadable);
    assert(viewmodel::skin_block(21) == SkinBlock::WrongLayout);
    assert(viewmodel::skin_block(14) != viewmodel::skin_block(15));
    assert(viewmodel::skin_block(13) == SkinBlock::BadIndex);
    assert(viewmodel::skin_block(10) == SkinBlock::NoItemData);
    // Чтение и запись — разные причины: «не прочиталось» и «не записалось»
    // лечатся по-разному, смешивать их нельзя. Записи обеих копий (16..19) —
    // одна причина: лечится одинаково.
    assert(viewmodel::skin_block(16) == viewmodel::skin_block(17));
    assert(viewmodel::skin_block(18) == viewmodel::skin_block(16));
    assert(viewmodel::skin_block(19) == viewmodel::skin_block(16));
    // Запись Oxide.Item (22/23) и зеркало головы рабочей копии (24/25) — та
    // же причина: «запись не подтвердилась».
    assert(viewmodel::skin_block(22) == viewmodel::skin_block(16));
    assert(viewmodel::skin_block(23) == viewmodel::skin_block(16));
    assert(viewmodel::skin_block(24) == viewmodel::skin_block(16));
    assert(viewmodel::skin_block(25) == viewmodel::skin_block(16));
    assert(viewmodel::skin_block(16) != viewmodel::skin_block(14));
    // Пустые руки и «нет скинов у предмета» — тоже разные вещи.
    assert(viewmodel::skin_block(6) == SkinBlock::EmptyHands);
    assert(viewmodel::skin_block(11) == SkinBlock::NoSkins);
    assert(viewmodel::skin_block(6) != viewmodel::skin_block(11));
    // Нет игрока — одна причина на все стадии поиска игрока.
    for (int stage = 1; stage <= 4; ++stage)
        assert(viewmodel::skin_block(stage) == SkinBlock::NoPlayer);
    // §4.22: карантин — своя причина. «Не применится, потому что этот скин
    // ломал сборку» лечится иначе, чем «запись не подтвердилась»: там жди
    // пересборки, здесь выбирай другой индекс.
    assert(viewmodel::skin_block(26) == SkinBlock::Quarantined);
    assert(viewmodel::skin_block(26) != SkinBlock::WriteFailed);
    assert(std::strcmp(viewmodel::skin_block_text(SkinBlock::Quarantined),
                       "Ствол в карантине: скин ломал сборку вьюхи") == 0);
    // §4.23: «длиннее предела списка» и «скинов нет» — разные причины: первая
    // лечится поднятием предела, вторая — констатация факта. Смешивать их
    // значит врать меню и логу на популярных стволах.
    assert(viewmodel::skin_block(27) == SkinBlock::TooManySkins);
    assert(viewmodel::skin_block(27) != SkinBlock::NoSkins);
    assert(viewmodel::skin_block(12) == SkinBlock::NoSkins);
    assert(std::strcmp(viewmodel::skin_block_text(SkinBlock::TooManySkins),
                       "У предмета больше скинов, чем вмещает список") == 0);
    assert(std::strcmp(viewmodel::skin_block_text(SkinBlock::None), "") != 0);
    std::printf("  skin_block: 27 stages -> 11 distinct causes, texts unique\n");
}

// ---- 26. Слоты TypeInfo разъехались: тип даёт точное имя класса ------------
void test_kind_by_exact_name_when_slots_stale() {
    Fixture f;
    // База libil2cpp не от живого образа (перезапуск игры через exec, старый
    // «(deleted)» образ, статика NULL во время загрузки): слоты указывают на
    // чужие классы, а полевые оффсеты и цепочка целы. Именно так выглядел
    // skin.log с устройства: все типы стали «other» при той же версии игры,
    // а запись индекса продолжала подтверждаться.
    put_typeinfo(f.io, go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA, 0xBAD00);
    put_typeinfo(f.io, go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA, 0xBAD10);
    put_typeinfo(f.io, go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA, 0xBAD20);
    put_klass_name(f.io, skin0, "WeaponObjectSkinData");
    put_klass_name(f.io, skin1, "WeaponProgressionSkinData");
    put_klass_name(f.io, skin2, "WeaponRecolorSkinData");
    f.tick(f.settings(true, 0));
    viewmodel::SkinEntryView entries[skinchanger::kMaxSkins];
    const int n = f.c.entries(entries, skinchanger::kMaxSkins);
    assert(n == 3);
    assert(entries[0].kind == viewmodel::SkinKind::WeaponObject && entries[0].ready);
    assert(entries[1].kind == viewmodel::SkinKind::WeaponProgression && entries[1].levels == 3);
    assert(entries[2].kind == viewmodel::SkinKind::WeaponRecolor && !entries[2].ready);
    for (int i = 0; i < n; ++i)
        assert(viewmodel::skin_kind_identified(entries[i].kind));
    std::printf("  stale slots: kinds/levels/ready recovered by exact class name\n");
}

// ---- 27. Ни слотов, ни имён: тип не выдуман, ready не утверждается ---------
void test_kind_other_when_slots_and_names_unreadable() {
    Fixture f;
    put_typeinfo(f.io, go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA, 0xBAD00);
    put_typeinfo(f.io, go_active::WEAPONPROGRESSIONSKIN_TYPEINFO_RVA, 0xBAD10);
    put_typeinfo(f.io, go_active::WEAPONRECOLORSKIN_TYPEINFO_RVA, 0xBAD20);
    // Имён классов в памяти нет (klass->name не отображён): резерв тоже не сработал.
    f.tick(f.settings(true, 0));
    viewmodel::SkinEntryView entries[skinchanger::kMaxSkins];
    const int n = f.c.entries(entries, skinchanger::kMaxSkins);
    assert(n == 3);
    for (int i = 0; i < n; ++i) {
        assert(entries[i].kind == viewmodel::SkinKind::Other);
        assert(!viewmodel::skin_kind_identified(entries[i].kind));
        assert(!entries[i].ready);   // «не измерено», а не «ассета нет»
    }
    // Запись индекса от нераспознанного типа не зависит: SyncVar тот же.
    f.tick(f.settings(true, 1));
    assert(f.io.value<int16_t>(piece() + go_active::WEAPONPIECE_SKIN) == 1);
    std::printf("  stale slots + no names: kind Other, ready not asserted, write works\n");
}

// ---- 27. §4.21: погашенная голова рабочей копии — зеркало из SyncVar -------
void test_blanked_head_refilled_from_syncvar() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(!f.c.ui().local_refill);           // голова канонична — зеркало спит
    // Ствол убран: игра гасит голову рабочей копии (Enabled=0, Number=-1,
    // Skin=-1, Loaded=0). SyncVar остаётся каноничным и держит наш индекс.
    const uint64_t blank_lo = 0x0000FFFFFFFF0000ull;   // 00 00 ff ff ff ff 00 00
    f.io.put<uint64_t>(piece_local(), blank_lo);
    f.io.put<uint64_t>(piece_local() + 8, 0xDEAD);     // хвост (Mods) рабочей
    f.io.put<uint64_t>(piece() + 8, 0xBEEF);           // хвост SyncVar
    f.tick(f.settings(true, 2));
    const viewmodel::SkinUiInfo ui = f.c.ui();
    assert(ui.status == viewmodel::Status::Active && ui.stage == 0);
    assert(ui.local_refill);
    // Голова зеркалится ЦЕЛИКОМ, как это делает pMd: Enabled=1, Number=114,
    // наш новый индекс, и хвост совпадает с SyncVar-хвостом.
    assert(f.live_enabled_local() == 1);
    assert(f.io.value<int16_t>(piece_local() + go_active::WEAPONPIECE_NUMBER) == 114);
    assert(f.live_skin_local() == 2);
    assert(f.io.value<uint64_t>(piece_local() + 8) == 0xBEEF);
    // Enabled в SyncVar по-прежнему не написан никем.
    for (uint64_t a : f.io.write_log)
        assert(a != piece() + go_active::WEAPONPIECE_ENABLED);
    std::printf("  blanked head: mirrored from SyncVar (Enabled/Number/skin/Mods)\n");
}

// ---- 28. §4.21: зеркало возвращается при выключении -------------------------
void test_refill_restored_on_disable() {
    Fixture f;
    f.tick(f.settings(true, 1));
    const uint64_t blank_lo = 0x0000FFFFFFFF0000ull;
    f.io.put<uint64_t>(piece_local(), blank_lo);
    f.io.put<uint64_t>(piece_local() + 8, 0);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().local_refill);
    assert(f.live_enabled_local() == 1 && f.live_skin_local() == 1);
    // Выключение: голова рабочей копии возвращается байт-в-байт погашенной,
    // SyncVar/Item — к оригиналам фикстуры.
    f.tick(f.settings(false));
    assert(!f.c.has_state() && !f.c.ui().local_refill);
    assert(f.io.value<uint64_t>(piece_local()) == blank_lo);
    assert(f.live_skin_local() == (int16_t)-1);
    assert(f.live_skin() == f.original_skin && f.live_item_skin() == f.original_skin);
    std::printf("  refill restore: disable returns the blanked head byte-exact\n");
}

// ---- 29. §4.21: дострой головы в нерезолве (дроу без FPObject) --------------
void test_stage9_refills_blank_head() {
    Fixture f;
    f.tick(f.settings(true, 1));
    // Игрок временно пропал (нерезолв, «В руках пусто»), а голова погашена:
    // на дроу hbr.pMZ читает её напрямую из PW +0xC0, минуя цепочку FPObject,
    // поэтому дострой обязан случиться и здесь — фоном, без смены статуса.
    const uint64_t blank_lo = 0x0000FFFFFFFF0000ull;
    f.io.put<uint64_t>(piece_local(), blank_lo);
    f.io.put<uint8_t>(net + go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER, 0);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Waiting);
    assert(f.c.ui().stage != 0);
    assert(f.live_enabled_local() == 1);      // голова достроена
    assert(f.live_skin_local() == 1);         // и держит наш индекс
    // Мёртвый PW зеркалить нельзя: запись в освобождённую кучу — порча памяти.
    Fixture d;
    d.tick(d.settings(true, 1));
    d.io.put<uint64_t>(pw, 0xDEAD);           // klass мёртв -> life != Alive
    d.io.put<uint64_t>(piece_local(), blank_lo);
    const unsigned before = d.io.writes;
    d.tick(d.settings(true, 1));
    assert(d.io.writes == before);            // ни одной записи по мёртвому PW
    assert(d.io.value<uint64_t>(piece_local()) == blank_lo);
    std::printf("  stage-9 refill: background mirror, dead PW untouched\n");
}

// ---- 30. §4.22: панцирь сбрасывается, скин уходит в карантин ----------------
void test_shell_reset_and_quarantine() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().fp_build_resets == 0 && f.c.ui().quarantine_skin == -1);
    // Исключение в сборке: ASSEMBLY уже сделала _currentWeapon = новый FPWeapon,
    // но On_Draw (uhq=1, Item=аргумент) не отработал — панцирь. Резолв честно
    // падает на стадии 9 (Item==null), меню показало бы «в руках пусто».
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    // Здоровая ПЕРВАЯ закачка ассета выглядит так же (On_Draw приходит только
    // в async-колбэке) — детектор обязан выждать порог, а не лечить сразу.
    for (int i = 0; i < 449; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 0);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == fp_object);
    f.tick(f.settings(true, 1));   // 450-й тик: вердикт «отравление»
    assert(f.c.ui().fp_build_resets == 1);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    // Сброс шёл ровно в _currentWeapon, а не куда-то ещё.
    bool reset_written = false;
    for (uint64_t a : f.io.write_log)
        if (a == fp + go_active::FPMANAGER_CURRENT_OBJECT) reset_written = true;
    assert(reset_written);
    // Карантин: ствол вернулся к родному скину во всех трёх точках записи.
    assert(f.c.ui().quarantine_skin == 1);
    assert(f.live_skin() == f.original_skin);
    assert(f.live_skin_local() == f.original_skin);
    assert(f.live_item_skin() == f.original_skin);
    // Сброс одноразовый на эпизод: игрок ещё не убирал ствол, _currentWeapon
    // пуст, резолв падает на стадии 7 — второго сброса быть не должно.
    for (int i = 0; i < 600; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 1);
    // Игрок убрал и достал ствол: игра собрала свежий FPObject (Item и uhq на
    // месте). Тот же индекс на том же стволе — карантин держит, стадия 26.
    f.io.put<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT, fp_object);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, item);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 1);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Failed && f.c.ui().stage == 26);
    assert(viewmodel::skin_block(26) == viewmodel::SkinBlock::Quarantined);
    assert(f.live_skin() == f.original_skin);   // ничего не пишем
    // Другой индекс — карантин снят, ствол применяется заново.
    f.tick(f.settings(true, 2));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().quarantine_skin == -1);
    assert(f.live_skin() == 2);
    // Успешный резолв взводит сброс снова: второй эпизод отравления лечится.
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 450; ++i) f.tick(f.settings(true, 2));
    assert(f.c.ui().fp_build_resets == 2);
    assert(f.c.ui().quarantine_skin == 2);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    assert(f.live_skin() == f.original_skin);
    std::printf("  shell: 450-tick threshold, one-shot reset, quarantine by index\n");
}

// ---- 31. §4.22: здоровая вьюха и мимолётный панцирь не сбрасываются ---------
void test_healthy_build_never_resets() {
    Fixture f;
    f.tick(f.settings(true, 1));
    // Вьюха здорова всю сессию (uhq==1, Item!=null): сигнатуры панциря нет,
    // детектор молчит хоть тысячу тиков.
    for (int i = 0; i < 1200; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 0 && f.c.ui().quarantine_skin == -1);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == fp_object);
    assert(f.c.status == viewmodel::Status::Active);
    // Мимолётный панцирь: первая закачка ассета держит ту же сигнатуру, пока
    // async-колбэк не позовёт On_Draw. Прошла до порога — сброса нет.
    Fixture t;
    t.tick(t.settings(true, 1));
    t.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    t.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 100; ++i) t.tick(t.settings(true, 1));
    t.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, item);
    t.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 1);
    for (int i = 0; i < 400; ++i) t.tick(t.settings(true, 1));
    assert(t.c.ui().fp_build_resets == 0);
    assert(t.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == fp_object);
    assert(t.c.status == viewmodel::Status::Active);
    std::printf("  healthy/transient shell: threshold never reached, no reset\n");
}

// ---- 32. §4.22: карантин per-ствол (ItemData.m_Id), не per-класс ------------
void test_quarantine_is_per_weapon_id() {
    Fixture f;
    f.tick(f.settings(true, 1));
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 450; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().quarantine_skin == 1);
    // Дроу: свежий FPObject, тот же ствол (m_Id тот же) — стадия 26.
    f.io.put<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT, fp_object);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, item);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 1);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().stage == 26);
    // Другой ствол того же класса (в игре — второй Томпсон с другим m_Id):
    // тот же индекс обязан примениться — «сломал сборку» свойство ствола,
    // а класс ItemData общий у нескольких предметов.
    f.io.put<int32_t>(item_data + go_active::ITEMDATA_ID, kItemDataId + 1);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().quarantine_skin == -1);
    assert(f.live_skin() == 1);
    std::printf("  quarantine: keyed by ItemData.m_Id, other weapon retries\n");
}

// ---- 33. §4.22: сброс не через слот — освобождённый панцирь не возвращается -
void test_reset_is_not_slot_restore() {
    Fixture f;
    f.tick(f.settings(true, 1));
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 450; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    // Ни выключение, ни повторное включение не возвращают указатель панциря
    // обратно: сброс сделан сырой записью, владения слота у него нет, и
    // restore_all() возвращает только индексы скинов, не _currentWeapon.
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    // Счётчик сбросов остаётся в снимке для меню/лога, пока жив контроллер:
    // пользователь должен видеть, что чит починил сборку, и после лечения.
    assert(f.c.ui().fp_build_resets == 1);
    std::printf("  reset: raw write, never restored back by slots\n");
}

// ---- 34. §4.23: skinData длиннее прежнего предела 24 — это НЕ «нет скинов» --
void test_long_skin_array_is_not_no_skins() {
    Fixture f;
    // Томпсон/Дезерт Игл: позиций больше 24, но в пределах нового предела.
    // Резолв обязан пройти, список — собрать доступные строки фикстуры.
    f.io.put<int32_t>(skins + go_active::IL2CPP_ARRAY_LENGTH, 30);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().count == 3);          // в фикстуре 3 живых указателя
    assert(f.live_skin() == 1);
    // За пределами нового предела (64) — честная стадия 27 и ноль записей,
    // а не «у предмета нет скинов» (нерезолв — Waiting, причину несёт стадия).
    Fixture g;
    g.io.put<int32_t>(skins + go_active::IL2CPP_ARRAY_LENGTH, 65);
    g.tick(g.settings(true, 1));
    assert(g.c.status == viewmodel::Status::Waiting);
    assert(g.c.ui().stage == 27);
    assert(viewmodel::skin_block(27) == viewmodel::SkinBlock::TooManySkins);
    assert(g.io.writes == 0);
    // Пустой массив — по-прежнему стадия 12: у предмета правда нет скинов.
    Fixture h;
    h.io.put<int32_t>(skins + go_active::IL2CPP_ARRAY_LENGTH, 0);
    h.tick(h.settings(true, 1));
    assert(h.c.ui().stage == 12);
    assert(viewmodel::skin_block(12) == viewmodel::SkinBlock::NoSkins);
    assert(h.io.writes == 0);
    std::printf("  long skinData: 30 resolves, >64 is stage 27 (not NoSkins), empty is 12\n");
}

// ---- 35. §4.24: панцирь с погашенным обработчиком — быстрый сброс (лук) ----
void test_peh_null_shell_fast_reset() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().fp_build_resets == 0);
    // Исключение в окне геттеров ASSEMBLY: _currentWeapon уже хранит новый
    // FPObject (dump11966 0x68F00B0), ссылка на игрока уже наша (0x68F015C),
    // а PlayerEventHandler ещё не поставлен (0x68F01E4). Для FPBow это
    // отравление всех дроу: FPBow.FDN (0x68C4970) делает NRE на [this+0xC8]==0,
    // а гейт зовёт FDN на _currentWeapon до очистки (0x68EFA0C).
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_EVENT_HANDLER, 0);
    for (int i = 0; i < 19; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 0);        // 19 тиков — ещё ждём
    assert(f.c.ui().shell_peh_null);
    assert(f.c.ui().shell_back == 1);
    assert(f.c.ui().shell_watch == 1);            // ~2 с наблюдения в ui
    f.tick(f.settings(true, 1));                  // 20-й тик: быстрый вердикт
    assert(f.c.ui().fp_build_resets == 1);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    assert(f.c.ui().quarantine_skin == 1);
    assert(f.live_skin() == f.original_skin);     // индекс возвращён к родному
    std::printf("  PEH-null shell: fast reset at 20 ticks, quarantine set\n");
}

// ---- 36. §4.24: нулевая обратная ссылка — детектор больше не слепнет ------
void test_backref_null_shell_is_seen() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    // Исключение МЕЖДУ хранениями: _currentWeapon уже новый FPObject, а ни
    // Player (+0xC0), ни PlayerEventHandler (+0xC8) ещё не поставлены.
    // Старый детектор (ссылка==игрок была частью сигнатуры) такую панцирь
    // не видел вовсе — отравление молчало до ротации лога.
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_PLAYER_BACKREF, 0);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_EVENT_HANDLER, 0);
    for (int i = 0; i < 19; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 0);
    assert(f.c.ui().shell_back == 0);             // нулевая ссылка видна в ui
    assert(f.c.ui().shell_peh_null);
    f.tick(f.settings(true, 1));                  // быстрый путь: PEH погашен
    assert(f.c.ui().fp_build_resets == 1);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    std::printf("  backref-null shell: seen (fast path via PEH), no longer blind\n");
}

// ---- 37. §4.24: живой обработчик — быстрый путь не срабатывает ------------
void test_peh_alive_keeps_slow_path() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    // Сигнатура Томпсона §4.22: панцирь с нашей ссылкой и ЖИВЫМ обработчиком —
    // здоровая первая закачка ассета выглядит так же, поэтому вердикт только
    // после долгого наблюдения (450 тиков), быстрый путь молчит.
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 100; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 0);        // быстрый путь не fired
    assert(!f.c.ui().shell_peh_null);
    for (int i = 0; i < 349; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().fp_build_resets == 0);        // 449 тиков — ещё ждём
    f.tick(f.settings(true, 1));                  // 450-й: медленный вердикт
    assert(f.c.ui().fp_build_resets == 1);
    assert(f.io.value<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT) == 0);
    std::printf("  PEH alive: fast path silent, slow 450-tick path unchanged\n");
}

// ---- 38. §4.24: зомби-резолв — _currentWeapon пуст, цепочка через кэш uzN --
void test_zombie_resolve_via_cache() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().cache_seconds == 0);
    // Игра уронила сборку до хранения FPObject: _currentWeapon (+0x50) пуст,
    // но типизированный кэш uzN (+0x58) держит живой объект прежней сборки —
    // резолв «проходит» (стадия 0, «применено»), дедуп блока молчит. Форму
    // видно только по счётчику: устройство v16 провело в ней 1–2 минуты, и
    // лог не напечатал ни строки.
    f.io.put<uint64_t>(fp + go_active::FPMANAGER_CURRENT_OBJECT, 0);
    f.io.put<uint64_t>(fp + go_active::FPMANAGER_CURRENT_WEAPON, fp_object);
    for (int i = 0; i < 100; ++i) f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);   // резолв через кэш
    assert(f.c.ui().cache_seconds == 10);              // 100 тиков × 100 мс
    assert(f.c.ui().fp_build_resets == 0);             // записей в память игры нет
    // Ствол не держится (SyncVar Enabled=0) — пустой _currentWeapon законен
    // (холстер), зомби-счёт останавливается.
    f.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_ENABLED, 0);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().cache_seconds == 0);
    std::printf("  zombie resolve: counted via uzN cache, empty hands not counted\n");
}

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
    test_rewrite_request();
    test_item_skin_written_and_restored();
    test_unresolved_keeps_writes();
    test_dead_context_released_without_restore();
    test_enabled_never_written();
    test_local_copy_bad_head();
    test_local_copy_relaxed_head();
    test_local_copy_write_failure();
    test_fp_stale_flag();
    test_no_skin_array();
    test_not_local_player();
    test_write_failure_is_reported();
    test_sanitize_bounds();
    test_beta_profile_layout();
    test_negative_skin_is_accepted();
    test_large_skin_value_in_memory();
    test_piece_unreadable();
    test_piece_wrong_layout();
    test_piece_bad_number();
    test_skin_block_grouping();
    test_kind_by_exact_name_when_slots_stale();
    test_kind_other_when_slots_and_names_unreadable();
    test_blanked_head_refilled_from_syncvar();
    test_refill_restored_on_disable();
    test_stage9_refills_blank_head();
    test_shell_reset_and_quarantine();
    test_healthy_build_never_resets();
    test_quarantine_is_per_weapon_id();
    test_reset_is_not_slot_restore();
    test_long_skin_array_is_not_no_skins();
    test_peh_null_shell_fast_reset();
    test_backref_null_shell_is_seen();
    test_peh_alive_keeps_slow_path();
    test_zombie_resolve_via_cache();
    std::printf("OK\n");
    return 0;
}
