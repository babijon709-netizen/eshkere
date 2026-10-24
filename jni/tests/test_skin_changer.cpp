// Хост-тест скинченджера (skin_changer_memory.h).
//
// Проверяет то, что на устройстве не увидеть: перечисление ItemData.skinData[],
// запись индекса ТОЛЬКО по расхождению в ОБЕ копии WeaponPiece (SyncVar +0x130
// и рабочую +0xD0 — из второй игра собирает FP-вьюху на дроу; релиз 11978,
// в 11966 было +0x110/+0xC0), возврат
// оригинала, поведение на пересинке SyncVar, смене ствола и смене сессии, и
// работу с головой рабочей копии: Enabled в SyncVar не пишется НИКОГДА, а
// погашенная игрой голова (+0xD0, Enabled=0) достраивается зеркалом из
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
constexpr uint64_t item2     = 0x44000;     // второй экземпляр Item (ротация)
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
        // §4.39: механизмы прибиты kill-switches (production OFF); тесты их
        // включают — поведение запинено, прод молчит.
        c.set_kjw_adopt(true);
        c.set_ditem_fill(true);
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
        // Рабочая копия (wDT +0xD0; 11966: QVN +0xC0): в игре её пишет pMd из
        // той же piece, поэтому на старте обе копии совпадают.
        io.put<uint8_t>(piece_local() + WEAPONPIECE_ENABLED, 1);
        io.put<int16_t>(piece_local() + WEAPONPIECE_NUMBER, 114);
        io.put<int16_t>(piece_local() + WEAPONPIECE_SKIN, original_skin);
        io.put<uint8_t>(piece_local() + WEAPONPIECE_SKIN_LEVEL, original_level);
        io.put<uint8_t>(piece_local() + WEAPONPIECE_LOADED, 1);
        // Ручка визуала скина: model != 0 = загружен.
        io.put<uint64_t>(pw + PLAYERWEAPON_SKIN_HANDLE + SKINHANDLE_MODEL, 0xABCD0);

        // Цепочка FP: FPObject -> Item -> ItemData -> skinData[].
        io.object(fp_object, "FPHitscan", "Oxide");
        // §4.26: имя класса обязано читаться и ИЗ ПАМЯТИ (klass->name), а не
        // только из таблицы named(): срез печатает класс полусобранного
        // объекта через klass_name_read.
        put_klass_name(io, fp_object, "FPHitscan");
        io.put<uint64_t>(fp + FPMANAGER_CURRENT_OBJECT, fp_object);
        // §4.25: uzL (+0x68) — копия piece последней сборки: совпадает с
        // головой в руках (фикстура здорова, застревания нет).
        io.put<uint8_t>(fp + FPMANAGER_UZL + WEAPONPIECE_ENABLED, 1);
        io.put<int16_t>(fp + FPMANAGER_UZL + WEAPONPIECE_NUMBER, 114);
        io.put<int16_t>(fp + FPMANAGER_UZL + WEAPONPIECE_SKIN, original_skin);
        // §4.26: поля среза — CTS/CTS2/UTCS (маркеры вызова Kxi/LTI), флаги
        // uzj/uhF, гейт дроу, живой слот дроу (11978: wDY @0x150 — его читает
        // drawSV) и FPManager со стороны PW. Значения произвольны, но стабильны:
        // срез в тестах обязан читаться целиком.
        io.put<uint64_t>(fp + FPMANAGER_CTS, 0xC750);
        io.put<uint64_t>(fp + FPMANAGER_CTS2, 0xC760);
        io.put<uint64_t>(fp + FPMANAGER_UTCS, 0xC7D0);
        io.put<uint8_t>(fp + FPMANAGER_UZJ, 0);
        io.put<uint8_t>(fp + FPMANAGER_UHF, 1);
        io.put<uint32_t>(pw + PLAYERWEAPON_DRAW_GATE, 1);
        io.put<uint32_t>(pw + PLAYERWEAPON_DRAW_SLOT, 5);
        // Кэш дроу pw+0x120: у здорового ствола там ТОТ ЖЕ экземпляр Item,
        // что в FP-цепи — копия 4 (skin_item2_) не активируется.
        io.put<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM, item);
        io.put<uint64_t>(pw + PLAYERWEAPON_FP_MANAGER, fp);
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
    int16_t live_view_skin() {
        return io.value<int16_t>(view_piece() + go_active::WEAPONPIECE_SKIN);
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
// ---- §4.28: инвентарь для зонда (не в дефолтной фикстуре!) ------------------
// Поясной массив в PlayerInventoryData+0x30: наш стабильный экземпляр (skin -1
// сервера) + чужой ствол. Дефолтная фикстура без инвентаря — старые тесты
// счётчиков записей не трогаем, зонд там спит.
constexpr uint64_t inv        = 0x46000;
constexpr uint64_t inv_data   = 0x47000;
constexpr uint64_t inv_client = 0x48000;
constexpr uint64_t belt       = 0x49000;
constexpr uint64_t belt_item  = 0x4A000;
constexpr uint64_t belt_other = 0x4B000;
constexpr uint64_t other_data = 0x4C000;
constexpr uint64_t belt_item2 = 0x4D000;   // ротация пояса (ресинк)
void add_inventory(Fixture& f, int32_t skin = -1) {
    using namespace go_active;
    f.io.object(inv, "PlayerInventory", "Oxide");
    f.io.object(inv_data, "PlayerInventoryData", "Oxide");
    f.io.object(inv_client, "PlayerInventoryClient", "Oxide");
    f.io.put<uint64_t>(player + PLAYER_INVENTORY, inv);
    f.io.put<uint64_t>(inv + INV_PLAYER_INVENTORY_DATA, inv_data);
    f.io.put<uint64_t>(inv + INV_PLAYER_INVENTORY_CLIENT, inv_client);
    f.io.plain(belt);
    f.io.put<int32_t>(belt + IL2CPP_ARRAY_LENGTH, 2);
    f.io.put<uint64_t>(inv_data + 0x30, belt);
    f.io.plain(belt_item);
    f.io.put<uint64_t>(belt_item + ITEM_DATA, item_data);
    f.io.put<int32_t>(belt_item + ITEM_SKIN, skin);
    f.io.put<uint8_t>(belt_item + ITEM_SKIN_LEVEL, 0);
    f.io.plain(other_data);
    f.io.plain(belt_other);
    f.io.put<uint64_t>(belt_other + ITEM_DATA, other_data);
    f.io.put<int32_t>(belt_other + ITEM_SKIN, 7);
    f.io.put<uint8_t>(belt_other + ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(belt + IL2CPP_ARRAY_FIRST_ELEMENT + 0, belt_item);
    f.io.put<uint64_t>(belt + IL2CPP_ARRAY_FIRST_ELEMENT + 8, belt_other);
}

// ---- §4.28 v2: только прямой массив пояса player+0x1B8 ----------------------
// Data/Client инвентаря в фикстуре НЕТ: зонд обязан найти пояс по warr-якорю
// (сессия 17: полевое v1 за 77 с по Data/Client не нашло ничего).
constexpr uint64_t warr = 0x4E000;
void add_warr_only(Fixture& f) {
    using namespace go_active;
    f.io.plain(warr);
    f.io.put<int32_t>(warr + IL2CPP_ARRAY_LENGTH, 1);
    f.io.plain(belt_item);
    f.io.put<uint64_t>(belt_item + ITEM_DATA, item_data);
    f.io.put<int32_t>(belt_item + ITEM_SKIN, -1);
    f.io.put<uint8_t>(belt_item + ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(warr + IL2CPP_ARRAY_FIRST_ELEMENT, belt_item);
    f.io.put<uint64_t>(player + PLAYER_WEAPONS_ARRAY, warr);
}

// ---- §4.28 v2: пояс в контейнере второго уровня -----------------------------
// В полях Data прямого Item/массива нет: Data+0x50 — контейнер, массив пояса —
// в его поле +0x20. Одноуровневый зонд v1 такое не видит.
constexpr uint64_t belt_box = 0x4F000;
void add_inventory_l2(Fixture& f) {
    using namespace go_active;
    f.io.object(inv, "PlayerInventory", "Oxide");
    f.io.object(inv_data, "PlayerInventoryData", "Oxide");
    f.io.object(inv_client, "PlayerInventoryClient", "Oxide");
    f.io.put<uint64_t>(player + PLAYER_INVENTORY, inv);
    f.io.put<uint64_t>(inv + INV_PLAYER_INVENTORY_DATA, inv_data);
    f.io.put<uint64_t>(inv + INV_PLAYER_INVENTORY_CLIENT, inv_client);
    f.io.plain(belt_box);
    f.io.put<uint64_t>(inv_data + 0x50, belt_box);
    f.io.plain(belt);
    f.io.put<int32_t>(belt + IL2CPP_ARRAY_LENGTH, 1);
    f.io.put<uint64_t>(belt_box + 0x20, belt);
    f.io.plain(belt_item);
    f.io.put<uint64_t>(belt_item + ITEM_DATA, item_data);
    f.io.put<int32_t>(belt_item + ITEM_SKIN, -1);
    f.io.put<uint8_t>(belt_item + ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(belt + IL2CPP_ARRAY_FIRST_ELEMENT, belt_item);
}

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
    // Запрос снимает «свежесть» слотов: следующий тик пишет все восемь полей
    // (Skin/SkinLevel в обеих копиях piece + Oxide.Item.skin/skinLevel +
    // Skin/SkinLevel piece вьюхи) и подтверждает каждую запись readback'ом.
    f.c.request_rebuild();
    f.tick(s);
    assert(f.io.writes == before + 8);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1 && f.live_item_skin() == 1);
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.enabled_untouched());
    std::printf("  rewrite request: 8 forced writes, Enabled untouched\n");
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
    assert(go_active::PLAYERWEAPON_DRAW_ITEM == 0x120);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0x108);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x40);
    assert(go_active::ITEMDATA_SKIN_DATA == 0xD0);
    assert(go_active::ITEM_SKIN == 0x30);          // Oxide.Item идентичен релизу
    assert(go_active::ITEM_SKIN_LEVEL == 0x34);
    assert(go_active::WEAPONOBJECTSKIN_TYPEINFO_RVA == 0xE64B9E0);
    assert(go_active::EG_TYPEINFO_RVA == 0);   // в бете резолвится по имени
    // Релиз 11978 сошёлся с бетой по раскладке WeaponPiece/PlayerWeapon —
    // различаются только TypeInfo-слоты (dump11978 против dumpbeta11977).
    go_active::Select(go_active::GameVersion::Release);
    assert(go_active::WEAPONPIECE_SIZE == 0x18);
    assert(go_active::WEAPONPIECE_EMPTY_HANDS == 0x10);
    assert(go_active::PLAYERWEAPON_PIECE == 0x130);
    assert(go_active::PLAYERWEAPON_PIECE_LOCAL == 0xD0);
    assert(go_active::PLAYERWEAPON_DRAW_ITEM == 0x120);
    assert(go_active::PLAYERWEAPON_FP_VIEW == 0x108);
    assert(go_active::FPWEAPONVIEW_CANCELLATION == 0x40);
    assert(go_active::ITEM_SKIN == 0x30);
    assert(go_active::ITEM_SKIN_LEVEL == 0x34);
    assert(go_active::EG_TYPEINFO_RVA == 0xE673558);
    std::printf("  profiles: release 0x18 == beta 0x18, TypeInfo slots pinned\n");
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
    // на дроу hbr.pMZ читает её напрямую из PW +0xD0 (11966: +0xC0), минуя цепочку FPObject,
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

// ---- 39. §4.25: застревание дроу — uzL разошёлся с Number в руках ----------
void test_draw_stall_detector() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().stall_seconds == 0);
    // Дроу перестал доходить до сборки: uzL (+0x6A) застыл на старом Number,
    // а piece в руках (SyncVar) обновлён игрой. Резолв при этом здоров
    // («применено», стадия 0) — сбрасывать нечего, только счёт и лог.
    f.io.put<int16_t>(fp + go_active::FPMANAGER_UZL + go_active::WEAPONPIECE_NUMBER, 999);
    for (int i = 0; i < 99; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().stall_seconds == 9);
    f.tick(f.settings(true, 1));                  // 100-й тик: 10 с
    assert(f.c.ui().stall_seconds == 10);
    assert(f.c.ui().stall_hand_number == 114);
    assert(f.c.ui().stall_view_number == 999);
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().fp_build_resets == 0);        // записей в память игры нет
    // Сборка дошла (uzL догнал piece в руках) — счёт обнуляется.
    f.io.put<int16_t>(fp + go_active::FPMANAGER_UZL + go_active::WEAPONPIECE_NUMBER, 114);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().stall_seconds == 0);
    std::printf("  draw stall: uzL vs hand Number counted, heal zeroes\n");
}

// ---- 40. §4.26: сырой срез FP-состояния — триггеры, интервал, сердцебиение --
void test_fp_dump_watch() {
    Fixture f;
    f.tick(f.settings(true, 1));    // применение: has_state() станет true здесь
    assert(f.c.ui().dump_seq == 0); // срез строится со следующего тика
    f.tick(f.settings(true, 1));
    // Первый срез: триггерные поля отличаются от сентинелей — базовая линия.
    assert(f.c.ui().dump_seq == 1);
    assert(std::strstr(f.c.ui().dump_text, "cur=") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "FPHitscan") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "uzL=1/114/") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "SV=1/114/") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "CTS=c750/c760 UTCS=c7d0") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "uzj=0 uhF=1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "gate=1 slot=5") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "hnd=abcd0") != nullptr);
    const int seq = f.c.ui().dump_seq;
    // Ничего не меняется — срезов нет до сердцебиения.
    for (int i = 0; i < 50; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().dump_seq == seq);
    // Сменился ствол в руках (SyncVar Number) — срез сразу (интервал истёк).
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_NUMBER, 228);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().dump_seq == seq + 1);
    assert(std::strstr(f.c.ui().dump_text, "SV=1/228/") != nullptr);
    // Интервал: быстрая смена не даёт второй срез раньше 20 тиков.
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_NUMBER, 229);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().dump_seq == seq + 1);         // интервал ещё не истёк
    for (int i = 0; i < 19; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().dump_seq == seq + 2);         // триггер не потерялся
    assert(std::strstr(f.c.ui().dump_text, "SV=1/229/") != nullptr);
    // Сердцебиение: 600 тиков — срез даже без изменений полей.
    const int seq2 = f.c.ui().dump_seq;
    for (int i = 0; i < 600; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().dump_seq == seq2 + 1);
    std::printf("  fp dump: baseline, trigger+cooldown, heartbeat\n");
}

// ---- 41. Копия 4: экземпляр Item из кэша дроу pw+0x120 (RE 11978) ---------
// via(kJw) (Slot 32) строит SyncVar из кэшированного Item, а не из того, что
// в FP-цепи. Серверное обновление экземпляра разводит цепи: без копии 4 дроу
// вечно читал бы дефолтный скин (симптом устройства 11978 — «на некоторых
// оружиях скины применяются только если скидываю и подбираю оружие»).
void test_draw_item_divergence_written_and_restored() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.live_item_skin() == 1);
    // Сервер обновил экземпляр Item: в кэше дроу новый, в FP-цепи прежний.
    f.io.plain(item2);
    f.io.put<uint64_t>(item2 + go_active::ITEM_DATA, item_data);
    f.io.put<int32_t>(item2 + go_active::ITEM_SKIN, -1);
    f.io.put<uint8_t>(item2 + go_active::ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_DRAW_ITEM, item2);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == 1);
    assert(f.io.value<uint8_t>(item2 + go_active::ITEM_SKIN_LEVEL) == 0);
    assert(f.live_item_skin() == 1);   // прежний экземпляр тоже поддержан
    // Сервер затёр новый экземпляр — следующий тик восстанавливает.
    f.io.put<int32_t>(item2 + go_active::ITEM_SKIN, -1);
    f.tick(f.settings(true, 1));
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == 1);
    // Выключение: оригиналы возвращены в оба экземпляра (в новый -1, в прежний 0).
    f.tick(f.settings(false, 1));
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == -1);
    assert(f.live_item_skin() == f.original_skin);
    std::printf("  draw-item divergence: both written, resync healed, restored\n");
}

// Чужой ствол в кэше дроу (иной ItemData) наш индекс получить не должен:
// в окне перехода pw+0x120 хранит Item ПРОШЛОГО ствола.
void test_draw_item_foreign_data_ignored() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    f.io.plain(item2);
    f.io.put<uint64_t>(item2 + go_active::ITEM_DATA, 0x3A800);
    f.io.put<int32_t>(item2 + go_active::ITEM_SKIN, -1);
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_DRAW_ITEM, item2);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == -1);
    std::printf("  draw-item foreign ItemData: ignored\n");
}

// ---- 42. §4.27: блэкаут дроу — серверная переписка лечится до чтения TAIL --
// Холстер роняет резолв (стадия 9, It=0), а подъём переписывает SyncVar и
// рабочую копию серверной правдой (-1) до чтения головой сборки. Без записей
// в блэкауте вьюха строится из -1, а тик после подъёма переписывает уже
// построенное (skin.log: SV/wk -1 пойманы на uhq=1, вьюха мёрзлая).
void test_blackout_rewrite_heals_server_wipe() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1 && f.live_item_skin() == 1);
    // Холстер: It=0 (стадия 9), сервер переписал обе копии в -1 с живой головой.
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, -1);
    f.io.put<int16_t>(piece_local() + go_active::WEAPONPIECE_SKIN, -1);
    f.io.put<int32_t>(item + go_active::ITEM_SKIN, -1);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Waiting);   // резолв всё ещё пуст
    assert(f.c.stage() == 9);                           // стадия резолва не тронута
    assert(f.live_skin() == 1);                         // переписка залечена
    assert(f.live_skin_local() == 1);
    assert(f.live_item_skin() == 1);
    // Погашенная холстером голова — не повод молчать: байты скина пишутся и в неё.
    f.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_ENABLED, 0);
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_NUMBER, -1);
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, -1);
    f.io.put<uint8_t>(piece_local() + go_active::WEAPONPIECE_ENABLED, 0);
    f.io.put<int16_t>(piece_local() + go_active::WEAPONPIECE_NUMBER, -1);
    f.io.put<int16_t>(piece_local() + go_active::WEAPONPIECE_SKIN, -1);
    f.tick(f.settings(true, 1));
    assert(f.live_skin() == 1 && f.live_skin_local() == 1);
    assert(f.live_enabled() == 0 && f.live_enabled_local() == 0);  // голову не чиним
    // Кэш дроу разошёлся (ресинк без пересборки) — четвёртая точка тоже держится.
    f.io.plain(item2);
    f.io.put<uint64_t>(item2 + go_active::ITEM_DATA, item_data);
    f.io.put<int32_t>(item2 + go_active::ITEM_SKIN, -1);
    f.io.put<uint8_t>(item2 + go_active::ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_DRAW_ITEM, item2);
    f.tick(f.settings(true, 1));
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == 1);
    // Подъём: цепочка сошлась, вьюха строилась бы из нашего индекса.
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, item);
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 1);
    f.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_ENABLED, 1);
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_NUMBER, 114);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1);
    std::printf("  blackout: server wipe healed pre-TAIL, head untouched, copy 4 held\n");
}

// ---- 43. §4.27: блэкаут молчит на мёртвом стволе и ранних стадиях ----------
// Писать можно только в кэш подтверждённого свежим резолвом ствола (стадии
// 7–9 — цепь ПОСЛЕ ствола). Ранние стадии (игрок/мир) и мёртвый PW — чужое.
void test_blackout_skips_dead_weapon_and_early_stages() {
    Fixture f;
    f.tick(f.settings(true, 1));
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);   // стадия 9...
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    f.io.put<uint64_t>(pw, 0xDEAD);                                // ...но PW мёртв
    f.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, -1);
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 1));
    assert(f.io.writes == before);               // ни одной записи по мёртвому PW
    assert(f.io.value<int16_t>(piece() + go_active::WEAPONPIECE_SKIN) == -1);
    // Ранняя стадия (игрок не локален, стадия 4): переписка НЕ лечится.
    Fixture g;
    g.tick(g.settings(true, 1));
    g.io.put<uint8_t>(net + go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER, 0);
    g.io.put<int16_t>(piece() + go_active::WEAPONPIECE_SKIN, -1);
    const unsigned gbefore = g.io.writes;
    g.tick(g.settings(true, 1));
    assert(g.c.stage() == 4);
    assert(g.io.writes == gbefore);
    assert(g.io.value<int16_t>(piece() + go_active::WEAPONPIECE_SKIN) == -1);
    std::printf("  blackout: silent on dead PW and early stages\n");
}

// ---- 44. §4.27: ротация Item — тот же ствол, без restore и рескана ---------
// Дроу линкует свежий экземпляр Item того же ствола (тот же ItemData, тот же
// skinData). Это не смена ствола: список и оригиналы те же, слот Item просто
// пересэмпливается на новый адрес. Старый экземпляр отвязан игрой — запись в
// нём остаётся (безвредно), оригинал нового — его серверное значение.
void test_rotation_keeps_list_without_restore() {
    Fixture f;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().count == 3);
    // Дроу: свежий экземпляр Item с серверным -1, тот же ItemData.
    f.io.plain(item2);
    f.io.put<uint64_t>(item2 + go_active::ITEM_DATA, item_data);
    f.io.put<int32_t>(item2 + go_active::ITEM_SKIN, -1);
    f.io.put<uint8_t>(item2 + go_active::ITEM_SKIN_LEVEL, 5);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, item2);
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_DRAW_ITEM, item2);
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    // Ровно две записи — skin/level нового экземпляра. Полный путь смены
    // контекста дал бы restore_all (моргание оригиналом в SyncVar) + rewrite.
    assert(f.io.writes == before + 2);
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == 1);
    assert(f.live_skin() == 1 && f.live_skin_local() == 1);   // SyncVar не моргал
    assert(f.live_item_skin() == 1);   // прежний экземпляр: запись осталась
    assert(f.c.ui().count == 3);       // список не перечитывался зря
    // Выключение: оригиналы — серверные значения каждого экземпляра.
    f.tick(f.settings(false));
    assert(f.live_skin() == f.original_skin);
    assert(f.io.value<int32_t>(item2 + go_active::ITEM_SKIN) == -1);
    // Другой ствол (иной ItemData) — по-прежнему полный путь с ресканом.
    switch_to_spear(f);
    f.tick(f.settings(true, 2));
    assert(f.c.stage() == 13);         // список перечитан, индекс 2 чужой
    std::printf("  rotation: same gun, no restore/rescan, new instance owned\n");
}

// ---- 45. §4.28: зонд находит пояс и держит его ------------------------------
// Усыновление — дважды подтверждённым (первый тик только собирает
// кандидатов), чужой ствол в том же массиве не трогается, выключение
// возвращает серверное значение.
void test_inv_probe_adopts_and_writes() {
    Fixture f;
    add_inventory(f);
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Active);
    assert(f.c.ui().inv_count == 0);   // зонд спит до 5-го тика
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == -1);
    for (int i = 0; i < 4; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 0);   // 5-й тик — первый проход, кандидаты
    for (int i = 0; i < 5; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 1);   // 10-й тик — усыновление + запись
    assert(f.c.ui().inv_state == 1);   // был чужой (-1 сервера)
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    assert(f.io.value<int32_t>(belt_other + go_active::ITEM_SKIN) == 7);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_state == 0);   // устоялось — наш индекс
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 1));
    assert(f.io.writes == before);     // steady state: ноль записей
    f.tick(f.settings(false));
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == -1);
    assert(!f.c.has_state());
    std::printf("  inv probe: belt adopted on 10th tick, held, restored on disable\n");
}

// ---- 46. §4.28: мусор и транзиенты не усыновляются --------------------------
// В массиве только FP-экземпляр (его ведёт копия 3) + битый указатель:
// целей ноль, записей сверх штатных трёх нет.
void test_inv_ignores_garbage_and_transients() {
    Fixture f;
    add_inventory(f);
    // Поясной слот затираем транзиентом FP-цепи, второй — мусором.
    f.io.put<uint64_t>(belt + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + 0, item);
    f.io.put<uint64_t>(belt + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + 8, 0x99999);
    f.io.put<uint64_t>(inv_data + 0x48, 0x88888);   // прямое поле-мусор
    f.tick(f.settings(true, 1));
    const unsigned standard = f.io.writes;
    assert(standard == 4);             // SV/рабочая/FP-Item/вьюха, как без зонда
    for (int i = 0; i < 11; ++i) f.tick(f.settings(true, 1));   // два прохода
    assert(f.c.ui().inv_count == 0);
    assert(f.c.ui().inv_state == -1);
    assert(f.io.writes == standard);
    assert(f.live_item_skin() == 1);   // транзиент ведётся копией 3, как раньше
    std::printf("  inv probe: garbage and FP transient ignored, 0 extra writes\n");
}

// ---- 47. §4.28: ротация пояса и ресинк лечатся без перезонда ----------------
// Ссылка (слот массива) та же — новый экземпляр подхватывается сам, оригинал
// нового — его серверное значение. Переписка на месте — тоже следующий тик.
void test_inv_rotation_and_resync_healed() {
    Fixture f;
    add_inventory(f);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    // Сервер подменил экземпляр в том же слоте (skin -1).
    f.io.plain(belt_item2);
    f.io.put<uint64_t>(belt_item2 + go_active::ITEM_DATA, item_data);
    f.io.put<int32_t>(belt_item2 + go_active::ITEM_SKIN, -1);
    f.io.put<uint8_t>(belt_item2 + go_active::ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(belt + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + 0, belt_item2);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 1);   // та же ссылка, перезонда нет
    assert(f.io.value<int32_t>(belt_item2 + go_active::ITEM_SKIN) == 1);
    // Сервер переписал значение на месте — следующий тик возвращает наше.
    f.io.put<int32_t>(belt_item2 + go_active::ITEM_SKIN, -1);
    f.tick(f.settings(true, 1));
    assert(f.io.value<int32_t>(belt_item2 + go_active::ITEM_SKIN) == 1);
    assert(f.c.ui().inv_state == 1);   // строка борьбы видна в логе
    f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_state == 0);
    // Оригинал нового экземпляра — его -1, не старый.
    f.tick(f.settings(false));
    assert(f.io.value<int32_t>(belt_item2 + go_active::ITEM_SKIN) == -1);
    std::printf("  inv probe: rotation re-owned, resync healed, original per instance\n");
}

// ---- 48. §4.28: смерть ссылки — снять молча, появление — прозондировать ----
// Слот обнулился (контейнер уехал): цель снимается без записи по мёртвому
// адресу; слот вернулся — усыновление снова за два тика.
void test_inv_drop_and_reprobe() {
    Fixture f;
    add_inventory(f);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 1);
    f.io.put<uint64_t>(belt + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + 0, 0);
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 0);
    assert(f.c.ui().inv_state == -1);
    assert(f.io.writes == before);     // по мёртвой ссылке — ни записи
    f.io.put<uint64_t>(belt + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + 0, belt_item);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));   // два прохода
    assert(f.c.ui().inv_count == 1);   // снова ведём
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    std::printf("  inv probe: dead ref dropped silently, re-adopted in 2 passes\n");
}

// ---- 49. §4.28: пояс ведётся и в блэкауте -----------------------------------
// Пояс стабилен и от FP-цепи не зависит: стадия 9 не останавливает ведение.
void test_inv_maintained_in_blackout() {
    Fixture f;
    add_inventory(f);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    f.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);   // стадия 9
    f.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    f.io.put<int32_t>(belt_item + go_active::ITEM_SKIN, -1);        // ресинк в блэкауте
    f.tick(f.settings(true, 1));
    assert(f.c.status == viewmodel::Status::Waiting);
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    assert(f.c.ui().inv_count == 1);
    std::printf("  inv probe: maintained through stage-9 blackout\n");
}

// ---- 50. §4.28: смена ствола — пояс возвращён, новый зондируется ------------
// Старый поясной экземпляр чужому ItemData нового ствола не подходит:
// restore_all возвращает ему оригинал, цели забываются.
void test_inv_restored_on_switch() {
    Fixture f;
    add_inventory(f);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    switch_to_spear(f);                // новый ствол, пояса у него в фикстуре нет
    f.tick(f.settings(true, 0));
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == -1);
    assert(f.c.ui().inv_count == 0);   // копьё без пояса — зонд молча спит
    assert(f.c.ui().inv_state == -1);
    std::printf("  inv probe: old belt restored on switch, new gun silent\n");
}

// ---- 51. §4.28 v2: пояс находится по прямому массиву player+0x1B8 -----------
// Data/Client нет вовсе — warr-якорь (route 4 имён оружия, game.cpp) находит
// поясной Item сам.
void test_inv_probe_finds_warr_array() {
    Fixture f;
    add_warr_only(f);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 1);
    assert(f.c.ui().inv_state == 1);   // тик усыновления видел чужой -1
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_state == 0);
    std::printf("  inv probe: player+0x1B8 belt array adopted\n");
}

// ---- 52. §4.28 v2: пояс в контейнере второго уровня --------------------------
// Первый уровень молчит (контейнер — не Item и не массив), второй находит
// массив пояса. v1 такое не видел — регресс-тест на слепоту сессии 17.
void test_inv_probe_finds_l2_container() {
    Fixture f;
    add_inventory_l2(f);
    for (int i = 0; i < 5; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 0);   // первый проход — кандидаты
    for (int i = 0; i < 5; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 1);
    assert(f.c.ui().inv_state == 1);   // тик усыновления видел чужой -1
    assert(f.io.value<int32_t>(belt_item + go_active::ITEM_SKIN) == 1);
    f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_state == 0);
    std::printf("  inv probe: L2 container belt adopted\n");
}

// ---- 53. §4.29: piece вьюхи красится и возвращается -------------------------
// Краска — после read_live_state: первый тик видит собранное игрой
// (fp_stale=true), красит; второй тик расхождения уже нет. Выключение
// возвращает оригинал вьюхи.
void test_view_piece_painted_and_restored() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_view_skin() == 2);
    assert(f.c.ui().view_state == 1);   // красили поверх чужого оригинала
    assert(f.c.ui().fp_stale);          // детектор видел собранное игрой
    f.tick(f.settings(true, 2));
    assert(f.c.ui().view_state == 0);
    assert(!f.c.ui().fp_stale);
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 2));
    assert(f.io.writes == before);      // steady state: ноль записей
    f.tick(f.settings(false));
    assert(f.live_view_skin() == f.original_skin);
    assert(!f.c.has_state());
    std::printf("  view piece: painted after stale check, restored on disable\n");
}

// ---- 54. §4.29: страж Number — чужую вьюху не красим ------------------------
// Вьюха показывает другой ствол (Number 999): ни записи, view_state -1.
// Нулевой указатель вьюхи — то же, без падения.
void test_view_piece_guard_number() {
    Fixture f;
    f.io.put<int16_t>(view_piece() + go_active::WEAPONPIECE_NUMBER, 999);
    f.tick(f.settings(true, 2));
    assert(f.live_view_skin() == f.original_skin);
    assert(f.c.ui().view_state == -1);
    assert(f.c.status == viewmodel::Status::Active);   // остальное работает
    Fixture g;
    g.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_FP_VIEW, 0);
    g.tick(g.settings(true, 2));
    assert(g.c.ui().view_state == -1);
    assert(g.c.status == viewmodel::Status::Active);
    std::printf("  view piece: foreign/empty view untouched\n");
}

// ---- 55. §4.29: пересборка вьюхи перекрашивается, руки не трогаем -----------
// Игра запекла серверное (-1) в piece вьюхи нашего ствола: следующий тик
// возвращает наш индекс, оригинал нового запекания — его -1. А погашенная
// SyncVar (Number -1, руки) при вьюхе рук краску не получает.
void test_view_piece_rebake_and_hands() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.live_view_skin() == 2);
    // Пересборка: то же Number, запечённое серверное.
    f.io.put<int16_t>(view_piece() + go_active::WEAPONPIECE_SKIN, -1);
    f.tick(f.settings(true, 2));
    assert(f.live_view_skin() == 2);
    assert(f.c.ui().view_state == 1);   // строка борьбы видна в логе
    f.tick(f.settings(true, 2));
    assert(f.c.ui().view_state == 0);
    // Оригинал нового запекания — его -1, не старый.
    f.tick(f.settings(false));
    assert(f.live_view_skin() == -1);
    // Руки в блэкауте: SyncVar погашена (Number -1), вьюха рук — красить
    // нечего, хотя Number'ы формально совпали (-1 == -1).
    Fixture h;
    h.tick(h.settings(true, 2));
    assert(h.live_view_skin() == 2);
    h.io.put<uint64_t>(fp_object + go_active::FPOBJECT_ITEM, 0);   // стадия 9
    h.io.put<uint8_t>(fp_object + go_active::FPOBJECT_SHOWN, 0);
    h.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_ENABLED, 0);
    h.io.put<int16_t>(piece() + go_active::WEAPONPIECE_NUMBER, -1);
    h.io.put<int16_t>(view_piece() + go_active::WEAPONPIECE_NUMBER, -1);
    h.io.put<int16_t>(view_piece() + go_active::WEAPONPIECE_SKIN, -1);
    h.tick(h.settings(true, 2));
    assert(h.c.status == viewmodel::Status::Waiting);
    assert(h.live_view_skin() == -1);   // вьюху рук не красим
    assert(h.c.ui().view_state == -1);
    std::printf("  view piece: rebake repainted, hands view untouched\n");
}

// ---- 56. §4.30: Loaded сбрасывается, пока хэндл мёртв -----------------------
// Живой хэндл (дефолт фикстуры) — флаги не трогаем вообще. Мёртвый —
// сбрасываем 1->0 во всех трёх копиях; выключение возвращает запечённые 1.
// Резолв хэндла игрой останавливает сбросы (домучивать нечего).
void test_loaded_cleared_while_handle_dead() {
    Fixture f;
    f.tick(f.settings(true, 2));
    assert(f.io.value<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED) == 1);
    assert(f.io.value<uint8_t>(piece_local() + go_active::WEAPONPIECE_LOADED) == 1);
    assert(f.io.value<uint8_t>(view_piece() + go_active::WEAPONPIECE_LOADED) == 1);
    // Хэндл умер — следующий тик сбрасывает все три флага.
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_SKIN_HANDLE +
                       go_active::SKINHANDLE_MODEL, 0);
    f.tick(f.settings(true, 2));
    assert(f.io.value<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED) == 0);
    assert(f.io.value<uint8_t>(piece_local() + go_active::WEAPONPIECE_LOADED) == 0);
    assert(f.io.value<uint8_t>(view_piece() + go_active::WEAPONPIECE_LOADED) == 0);
    // Игра резолвнула хэндл и подняла флаги — руки прочь, ноль записей.
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_SKIN_HANDLE +
                       go_active::SKINHANDLE_MODEL, 0xBEEF0);
    f.io.put<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED, 1);
    f.io.put<uint8_t>(piece_local() + go_active::WEAPONPIECE_LOADED, 1);
    f.io.put<uint8_t>(view_piece() + go_active::WEAPONPIECE_LOADED, 1);
    const unsigned before = f.io.writes;
    f.tick(f.settings(true, 2));
    assert(f.io.writes == before);
    assert(f.io.value<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED) == 1);
    std::printf("  loaded: cleared while handle dead, released when live\n");
}

// ---- 57. §4.30: сброс возвращается, чужая вьюха не трогается ----------------
// Оригинал сброшенного флага — запечённая игрой 1. Вьюха чужого ствола под
// сброс не попадает (тот же страж Number, что у краски).
void test_loaded_restored_and_guarded() {
    Fixture f;
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_SKIN_HANDLE +
                       go_active::SKINHANDLE_MODEL, 0);
    f.tick(f.settings(true, 2));
    assert(f.io.value<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED) == 0);
    f.tick(f.settings(false));
    assert(f.io.value<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED) == 1);
    assert(f.io.value<uint8_t>(piece_local() + go_active::WEAPONPIECE_LOADED) == 1);
    assert(f.io.value<uint8_t>(view_piece() + go_active::WEAPONPIECE_LOADED) == 1);
    assert(!f.c.has_state());
    Fixture g;
    g.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_SKIN_HANDLE +
                       go_active::SKINHANDLE_MODEL, 0);
    g.io.put<int16_t>(view_piece() + go_active::WEAPONPIECE_NUMBER, 999);
    g.tick(g.settings(true, 2));
    assert(g.io.value<uint8_t>(piece() + go_active::WEAPONPIECE_LOADED) == 0);
    assert(g.io.value<uint8_t>(view_piece() + go_active::WEAPONPIECE_LOADED) == 1);
    std::printf("  loaded: restored on disable, foreign view guarded\n");
}

// ---- 58. §4.30: срез несёт триплет L= ---------------------------------------
void test_dump_carries_loaded_triplet() {
    Fixture f;
    f.io.put<uint64_t>(pw + go_active::PLAYERWEAPON_SKIN_HANDLE +
                       go_active::SKINHANDLE_MODEL, 0);
    f.tick(f.settings(true, 2));   // сбросы легли
    f.tick(f.settings(true, 2));   // срез собрался
    assert(std::strstr(f.c.ui().dump_text, " L=000") != nullptr);
    std::printf("  dump: L= triplet reflects loaded flags\n");
}

// ---- 59. §4.31: срез несёт форму прохода зонда -------------------------------
// add_inventory: warr нет (w-1), один L1-массив (a1); o2/m2 — метаданные
// object() в полях Data/Client (два L2-пуша, один лишний почти-промах плюс
// чужой ствол массива).
// add_warr_only: warr длины 1 (w1), он же массив (a1), промахов нет.
// ---- §4.32: kJw-форма + гейты via в срезе -----------------------------------
// Живой kJw (Data+0x38 -> слот: +0x10 наш Item, +0x18 слот 25) и пустой
// (Data+0x40 -> слот: +0x10 null, +0x18 слот 26), u93/flg/nc выставлены
// руками. Срез обязан показать k1z1, ks=-1 (замер до усыновления), u93=1,
// flg=7, nc=-1 и 4-й флаг inv= (pw жив).
constexpr uint64_t kjw_slot = 0x54000;
constexpr uint64_t kjw_null = 0x55000;
constexpr uint64_t gate_a = 0x56000;
constexpr uint64_t gate_b = 0x57000;
void test_dump_carries_kjw_shape() {
    Fixture f;
    add_inventory(f);
    using namespace go_active;
    f.io.plain(kjw_slot);
    f.io.put<uint64_t>(kjw_slot + KJW_ITEM, belt_item);
    f.io.put<uint32_t>(kjw_slot + KJW_SLOT, 25);
    f.io.put<uint64_t>(inv_data + 0x38, kjw_slot);
    f.io.plain(kjw_null);
    f.io.put<uint64_t>(kjw_null + KJW_ITEM, 0);
    f.io.put<uint32_t>(kjw_null + KJW_SLOT, 26);
    f.io.put<uint64_t>(inv_data + 0x40, kjw_null);
    f.io.put<uint8_t>(item_data + ITEMDATA_UNLOCK_GATE, 1);
    f.io.put<uint64_t>(player + PLAYER_GATE_A, gate_a);
    f.io.put<uint64_t>(gate_a + PLAYER_GATE_B, gate_b);
    f.io.put<uint8_t>(gate_b + PLAYER_GATE_C, 7);
    f.io.put<int16_t>(pw + PLAYERWEAPON_NUMBER_CACHE, -1);
    // §4.33: живой слот = 26 — пустой слот kjw_null матчится (zh=1a, za),
    // а живой kjw_slot (слот 25) в zh не попадает.
    f.io.put<uint32_t>(pw + PLAYERWEAPON_DRAW_SLOT, 26);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(std::strstr(f.c.ui().dump_text, "inv=1:1:0:1:") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "k1z1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "ks=-1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "u93=1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "flg=7") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "nc=-1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "zh=1a") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "zhm=1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "za=55000") != nullptr);
    std::printf("  dump: kjw shape k/z + ks/u93/flg/nc gate fields\n");
}

// ---- §4.34: kJw-инъекция -----------------------------------------------------
// Пустая запись своего слота (Data+0x40 -> слот: +0x10 null, +0x18 = живой
// слот 26): два прохода зонда усыновляют, слот+0x10 получает живой FP-Item,
// срез показывает qj=1; выключение возвращает null.
constexpr uint64_t kjw_inj = 0x59000;
void add_kjw_null_only(Fixture& f, uint32_t slot = 26) {
    using namespace go_active;
    f.io.object(inv, "PlayerInventory", "Oxide");
    f.io.object(inv_data, "PlayerInventoryData", "Oxide");
    f.io.object(inv_client, "PlayerInventoryClient", "Oxide");
    f.io.put<uint64_t>(player + PLAYER_INVENTORY, inv);
    f.io.put<uint64_t>(inv + INV_PLAYER_INVENTORY_DATA, inv_data);
    f.io.put<uint64_t>(inv + INV_PLAYER_INVENTORY_CLIENT, inv_client);
    f.io.plain(kjw_inj);
    f.io.put<uint64_t>(kjw_inj + KJW_ITEM, 0);
    f.io.put<uint32_t>(kjw_inj + KJW_SLOT, slot);
    f.io.put<uint64_t>(inv_data + 0x40, kjw_inj);
    f.io.put<uint32_t>(pw + PLAYERWEAPON_DRAW_SLOT, slot);
    // Поясной Item для триггера позднего среза: adoption на тике 10 (в том же
    // проходе, что kJw) дёргает inv_count — срез видит shapes застывшими на
    // пред-инжект проходе (zh/yh), а qj уже живым.
    f.io.plain(belt);
    f.io.put<int32_t>(belt + IL2CPP_ARRAY_LENGTH, 1);
    f.io.put<uint64_t>(inv_data + 0x30, belt);
    f.io.plain(belt_item);
    f.io.put<uint64_t>(belt_item + ITEM_DATA, item_data);
    f.io.put<int32_t>(belt_item + ITEM_SKIN, -1);
    f.io.put<uint8_t>(belt_item + ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(belt + IL2CPP_ARRAY_FIRST_ELEMENT, belt_item);
}
void test_kjw_inject_null_fill() {
    Fixture f;
    add_kjw_null_only(f);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    // Два прохода (тики 5 и 10) — усыновление и инъекция живого FP-Item.
    // Срез застыл на пред-инжект проходе (zh=1a), qj живой (=1).
    assert(f.io.value<uint64_t>(kjw_inj + go_active::KJW_ITEM) == item);
    assert(std::strstr(f.c.ui().dump_text, "k0z1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "zh=1a") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "qj=1") != nullptr);
    // Выключение — оригинал (null) возвращён.
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(kjw_inj + go_active::KJW_ITEM) == 0);
    std::printf("  kjw: null-fill injects live FP-Item, restores null\n");
}

// Stale-запись своего слота (+0x10 чужой указатель): overwrite живым
// FP-Item, срез показывает qj=2 и yh-адрес; выключение возвращает stale.
constexpr uint64_t stale_ptr = 0x5B000;
void test_kjw_inject_stale_overwrite() {
    Fixture f;
    add_kjw_null_only(f);
    using namespace go_active;
    f.io.plain(stale_ptr);
    f.io.put<uint64_t>(kjw_inj + KJW_ITEM, stale_ptr);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    // Пред-инжект формы в срезе (k0z0, yhm=1), qj живой (=2).
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    assert(std::strstr(f.c.ui().dump_text, "k0z0") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "yhm=1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "yh=59000") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "qj=2") != nullptr);
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == stale_ptr);
    std::printf("  kjw: stale-overwrite injects live FP-Item, restores stale\n");
}

// ---- §4.35: пиннинг kJw через холстер ----------------------------------------
// Холстер (FP мёртв, блэкаут): пин ДЕРЖИМ, а не снимаем — иначе следующий
// дроу строится из null (§4.34, сессия 34). Ревайв — тот же пин, без
// пере-усыновления.
void test_kjw_pin_survives_holster() {
    Fixture f;
    add_kjw_null_only(f);
    using namespace go_active;
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    // Холстер: цепь FP мертва.
    f.io.put<uint64_t>(fp_object + FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 5; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    // Ревайв: пин на месте, пере-усыновления не было.
    f.io.put<uint64_t>(fp_object + FPOBJECT_ITEM, item);
    f.io.put<uint8_t>(fp_object + FPOBJECT_SHOWN, 1);
    for (int i = 0; i < 10; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    assert(std::strstr(f.c.ui().dump_text, "qj=1") != nullptr);
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == 0);
    std::printf("  kjw: pin survives holster, restores on disable\n");
}

// Ресинк ротировал в запись свежий -1 экземпляр (тот же ItemData): пин
// держим (НЕ перевливаем живой), свежий перекрашиваем прямо в записи.
void test_kjw_pin_repaints_resync() {
    Fixture f;
    add_kjw_null_only(f);
    using namespace go_active;
    f.io.plain(item2);
    f.io.put<uint64_t>(item2 + ITEM_DATA, item_data);
    f.io.put<int32_t>(item2 + ITEM_SKIN, -1);
    f.io.put<uint8_t>(item2 + ITEM_SKIN_LEVEL, 0);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    // Ресинк: игра положила в запись свежий экземпляр.
    f.io.put<uint64_t>(kjw_inj + KJW_ITEM, item2);
    for (int i = 0; i < 3; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item2);
    assert(f.io.value<int32_t>(item2 + ITEM_SKIN) == 1);
    std::printf("  kjw: pin keeps resync instance, repaints it\n");
}

// Нечитаемая запись (умирающий мир): снимаем молча БЕЗ записи, оригинал
// не трогаем — выключение после этого в запись не пишет.
void test_kjw_pin_drops_unreadable() {
    Fixture f;
    add_kjw_null_only(f);
    using namespace go_active;
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    f.io.fail_read = kjw_inj + KJW_ITEM;
    for (int i = 0; i < 3; ++i) f.tick(f.settings(true, 1));
    f.io.fail_read = 0;
    // Владение снято: restore при выключении писать не будет.
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == item);
    std::printf("  kjw: unreadable pin drops without write\n");
}

void test_dump_carries_probe_shape() {
    Fixture f;
    add_inventory(f);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.c.ui().inv_count == 1);
    assert(std::strstr(f.c.ui().dump_text, "c1t1/w-1a1o2m2") != nullptr);
    Fixture g;
    add_warr_only(g);
    for (int i = 0; i < 25; ++i) g.tick(g.settings(true, 1));
    assert(g.c.ui().inv_count == 1);
    assert(std::strstr(g.c.ui().dump_text, "c1t1/w1a1o0m0") != nullptr);
    std::printf("  dump: probe shape /w/a/o/m reflects pass stats\n");
}

// ---- §4.36: fm/cw-форма настоящего kJw в срезе -------------------------------
// za-декой доказан (§4.35, сессия 39 #6->#7: дроу под пином построил -1).
// Охота за настоящим kJw (аргумент via, x20): кандидаты — сам FPManager
// (+0x10 Item?, +0x18 слот?) и FPWeaponBase (кэш FPM+0x58). Фикстура кладёт
// null+слот=26 в оба: срез обязан показать fm10=null fm18=1a cw10=null cw18=1a.
constexpr uint64_t cw_obj = 0x5C000;
void test_dump_carries_fm_cw_shape() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);   // поясной Item: adoption дёргает inv_count = поздний срез
    // fp+0x10 НЕ трогаем: там cachedPtr (capture unity=true роняет резолв на
    // нуле — fail 6, срезов нет). fm10-ветвь проверяется hex-значением кэша.
    f.io.plain(cw_obj);
    f.io.put<uint64_t>(cw_obj + KJW_ITEM, 0);
    f.io.put<uint32_t>(cw_obj + KJW_SLOT, 26);
    f.io.put<uint64_t>(fp + FPMANAGER_CURRENT_WEAPON, cw_obj);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    // fp — Unity-объект: fm10 = cachedPtr (hex), fm18 не отображён (x).
    // cw_obj — null+слот=26: cw10=null cw18=1a.
    assert(std::strstr(f.c.ui().dump_text, "fm10=930000") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "fm10=null") == nullptr);
    assert(std::strstr(f.c.ui().dump_text, "fm18=x") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "cw10=null") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "cw18=1a") != nullptr);
    std::printf("  dump: fm/cw kjw-shape fields\n");
}

// ---- §4.37: массивы не кандидаты в kJw --------------------------------------
// Массив длины N (bounds=null, length=N) совпадает с kJw-формой null+слот —
// зонд считал их за zh и вливал Item в bounds (безвредный, но декой). Теперь
// кандидат живого слота проверяется по классу: имя на "]" = массив —
// отклонить (rej=1), не усыновлять (qj=0, bounds untouched).
constexpr uint64_t kjw_arr = 0x5D000;
void test_kjw_array_rejected() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);   // пояс-триггер среза + гейт 26; декой перезапишем
    f.io.plain(kjw_arr);
    put_klass_name(f.io, kjw_arr, "Item[]");
    f.io.put<uint64_t>(kjw_arr + KJW_ITEM, 0);       // bounds = null
    f.io.put<uint32_t>(kjw_arr + KJW_SLOT, 26);      // length = живой слот
    f.io.put<uint64_t>(inv_data + 0x40, kjw_arr);    // вместо записи — массив
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(std::strstr(f.c.ui().dump_text, "rej=1") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "zh=x") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "qj=0") != nullptr);
    assert(f.io.value<uint64_t>(kjw_arr + KJW_ITEM) == 0);   // bounds цел
    std::printf("  kjw: array-shaped candidate rejected, not adopted\n");
}

// ---- §4.38: заливка ditem ---------------------------------------------------
// Населённый сервер: pw+0x120 (ditem, Item-кэш via(kJw)) пуст — дроу печёт
// -1 из null. Заливаем наш FP-Item (перекрашенный): дроу печёт want.
// Выключение — возврат null.
void test_ditem_fill_and_restore() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);   // пояс-триггер позднего среза
    f.io.put<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM, 0);   // населённый: кэш пуст
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == item);
    assert(std::strstr(f.c.ui().dump_text, "df=1") != nullptr);
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == 0);
    std::printf("  ditem: fills empty draw cache, restores null\n");
}

// Валидный чужой (ресинк-ротация серверным: тот же ItemData, skin -1):
// НЕ трогаем (состояние сервера целее — аттачменты дроу берёт оттуда же),
// красит его копия 4; на выключении чужой возвращается в -1, кэш не наш.
// Битый чужой (чужой деф) — перезаписываем своим (дроу целее).
constexpr uint64_t ditem_foreign = 0x5E000;
void test_ditem_leaves_valid_foreign() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);
    f.io.plain(ditem_foreign);
    f.io.put<uint64_t>(ditem_foreign + ITEM_DATA, item_data);
    f.io.put<int32_t>(ditem_foreign + ITEM_SKIN, -1);
    f.io.put<uint8_t>(ditem_foreign + ITEM_SKIN_LEVEL, 0);
    f.io.put<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM, ditem_foreign);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == ditem_foreign);
    assert(f.io.value<int32_t>(ditem_foreign + ITEM_SKIN) == 1);
    assert(std::strstr(f.c.ui().dump_text, "df=2") != nullptr);
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == ditem_foreign);
    assert(f.io.value<int32_t>(ditem_foreign + ITEM_SKIN) == -1);
    std::printf("  ditem: leaves valid foreign to copy 4\n");
}

constexpr uint64_t ditem_broken = 0x5F000;
void test_ditem_overwrites_broken() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);
    f.io.plain(ditem_broken);
    f.io.put<uint64_t>(ditem_broken + ITEM_DATA, other_data);   // чужой деф
    f.io.put<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM, ditem_broken);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == item);
    f.tick(f.settings(false));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == ditem_broken);
    std::printf("  ditem: overwrites broken foreign, restores as-found\n");
}

// Холстер (цепь FP мертва): ditem обязан вернуться в null, а не висеть на
// освобождённом FP-Item (висячий ditem = краш дроу).
void test_ditem_clears_on_fp_death() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);
    f.io.put<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM, 0);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == item);
    f.io.put<uint64_t>(fp_object + FPOBJECT_ITEM, 0);
    f.io.put<uint8_t>(fp_object + FPOBJECT_SHOWN, 0);
    for (int i = 0; i < 5; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == 0);
    std::printf("  ditem: clears cache on FP death\n");
}

// ---- §4.39: механизмы выключены по умолчанию ---------------------------------
// kJw-adoption (qj=2 overwrite роняет игру: порча ItemData belt-Item при
// совпадении ammo==slot, сессия 32) и ditem-fill (теория мертва: uzL=-1 при
// залитом ditem, сессия 32) — kill-switches, production OFF. Фикстура выше
// включает их (механизмы запинены), здесь — OFF = тишина: ни заливки ditem,
// ни инъекции, qj=0 df=0 в срезе.
void test_kill_switches_default_off() {
    Fixture f;
    using namespace go_active;
    add_kjw_null_only(f);
    f.c.set_kjw_adopt(false);
    f.c.set_ditem_fill(false);
    f.io.put<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM, 0);
    for (int i = 0; i < 25; ++i) f.tick(f.settings(true, 1));
    assert(f.io.value<uint64_t>(pw + PLAYERWEAPON_DRAW_ITEM) == 0);
    assert(f.io.value<uint64_t>(kjw_inj + KJW_ITEM) == 0);
    assert(std::strstr(f.c.ui().dump_text, "qj=0") != nullptr);
    assert(std::strstr(f.c.ui().dump_text, "df=0") != nullptr);
    std::printf("  killswitch: adopt+fill off by default\n");
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
    test_draw_stall_detector();
    test_fp_dump_watch();
    test_draw_item_divergence_written_and_restored();
    test_draw_item_foreign_data_ignored();
    test_blackout_rewrite_heals_server_wipe();
    test_blackout_skips_dead_weapon_and_early_stages();
    test_rotation_keeps_list_without_restore();
    test_inv_probe_adopts_and_writes();
    test_inv_ignores_garbage_and_transients();
    test_inv_rotation_and_resync_healed();
    test_inv_drop_and_reprobe();
    test_inv_maintained_in_blackout();
    test_inv_restored_on_switch();
    test_inv_probe_finds_warr_array();
    test_inv_probe_finds_l2_container();
    test_view_piece_painted_and_restored();
    test_view_piece_guard_number();
    test_view_piece_rebake_and_hands();
    test_loaded_cleared_while_handle_dead();
    test_loaded_restored_and_guarded();
    test_dump_carries_loaded_triplet();
    test_dump_carries_probe_shape();
    test_dump_carries_kjw_shape();
    test_kjw_inject_null_fill();
    test_kjw_inject_stale_overwrite();
    test_kjw_pin_survives_holster();
    test_kjw_pin_repaints_resync();
    test_kjw_pin_drops_unreadable();
    test_dump_carries_fm_cw_shape();
    test_kjw_array_rejected();
    test_ditem_fill_and_restore();
    test_ditem_leaves_valid_foreign();
    test_ditem_overwrites_broken();
    test_ditem_clears_on_fp_death();
    test_kill_switches_default_off();
    std::printf("OK\n");
    return 0;
}
