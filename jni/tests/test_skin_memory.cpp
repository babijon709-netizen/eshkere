// Host tests for the skinchanger controller (jni/src/skin_memory.h).
//
// Тонкая логика, за которую тест отвечает целиком:
//   * резолв цепочки стора (VNB → static_fields → lhb → список) и отказные
//     стадии по отдельности;
//   * маппинг записей стора по fnv1a-хэшу короткого имени;
//   * запись только при значении, отличном от желаемого, с хранением
//     ОРИГИНАЛА под откат (контроллер не должен «держать» уже своё значение);
//   * восстановление при выключении и проверка read-back;
//   * мусорные границы (нулевые указатели → отказная стадия, не крах).
#include "skin_memory.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct FakeIO {
    std::map<uint64_t, uint8_t> mem;
    // Объекты: адрес → {имя, namespace}. named()/named_klass() сверяют
    // зарегистрированные пары — контроль «тот ли объект», глубину цепочки
    // подтверждает сама последовательность указателей.
    std::map<uint64_t, std::pair<std::string, std::string>> named_objects;

    template<class T> void put(uint64_t address, T value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) mem[address + i] = bytes[i];
    }
    template<class T> bool read(uint64_t address, T& value) const {
        uint8_t tmp[sizeof(T)]{};
        for (size_t i = 0; i < sizeof(T); ++i) {
            const auto it = mem.find(address + i);
            if (it == mem.end()) return false;
            tmp[i] = it->second;
        }
        std::memcpy(&value, tmp, sizeof(T));
        return true;
    }
    uint64_t ptr(uint64_t address) const {
        uint64_t v = 0;
        return read(address, v) ? v : 0;
    }
    template<class T> bool write_heap(uint64_t address, const T& value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) mem[address + i] = bytes[i];
        return true;
    }
    bool match(uint64_t object, const char* name, const char* namespaze) const {
        const auto it = named_objects.find(object);
        if (it == named_objects.end()) return false;
        return it->second.first == name && it->second.second == namespaze;
    }
    bool named_klass(uint64_t klass, const char* name, const char* namespaze) {
        return klass && match(klass, name, namespaze);
    }
    bool named(uint64_t object, const char* name, const char* namespaze) {
        return object && match(object, name, namespaze);
    }
};

// Каркас стора: объектная сеть managed-класса SelectedSkins со списком записей.
struct StoreFixture {
    FakeIO io;
    static constexpr uint64_t kBase = 0x100000000ull;   // il2cpp base
    static constexpr uint64_t kKlass = 0x200000000ull;
    static constexpr uint64_t kStatics = 0x300000000ull;
    static constexpr uint64_t kStore = 0x400000000ull;
    static constexpr uint64_t kList = 0x500000000ull;
    static constexpr uint64_t kItems = 0x600000000ull;
    uint64_t entries[4] = {};
    uint64_t ordered_items[4] = {};
    uint64_t count = 0;

    uint64_t add_entry(const char* ascii_name, const std::vector<int32_t>& skins) {
        using namespace go_active;
        const uint64_t i = count++;
        const uint64_t entry = 0x700000000ull + i * 0x100;
        const uint64_t name_obj = 0x800000000ull + i * 0x100;
        const uint64_t ord_obj = 0x900000000ull + i * 0x100;
        const uint64_t ord_items = 0xA000000000ull + i * 0x100;

        io.put(name_obj + IL2CPP_STRING_LENGTH, (int32_t)(int)std::strlen(ascii_name));
        for (int c = 0; ascii_name[c]; ++c)
            io.put(name_obj + IL2CPP_STRING_CHARS + c * 2, (uint16_t)ascii_name[c]);

        io.put(ord_obj + IL2CPP_LIST_ITEMS, ord_items);
        io.put(ord_obj + IL2CPP_LIST_SIZE, (int32_t)skins.size());
        for (size_t s = 0; s < skins.size(); ++s)
            io.put(ord_items + IL2CPP_ARRAY_FIRST_ELEMENT + s * 8 + ORDERED_SKIN_SKIN_ID,
                   skins[s]);

        io.put(entry + SELECTED_SKIN_SHORT_NAME, name_obj);
        io.put(entry + SELECTED_SKIN_ORDERED, ord_obj);
        entries[i] = entry;
        ordered_items[i] = ord_items;
        return entry;
    }

    // Связывает все адреса в цепочку стора. Вызывать после add_entry.
    void finish() {
        using namespace go_active;
        io.put(kBase + SKIN_STORE_TYPEINFO_RVA, kKlass); // слот TypeInfo из RVA
        io.put(kKlass + IL2CPP_CLASS_STATIC_FIELDS_OFFSET, kStatics);
        io.put(kStatics + SKIN_STORE_SELECTED_SKINS, kStore);
        io.put(kStore + SKIN_STORE_LIST, kList);
        io.put(kList + IL2CPP_LIST_ITEMS, kItems);
        io.put(kList + IL2CPP_LIST_SIZE, (int32_t)count);
        for (uint64_t i = 0; i < count; ++i)
            io.put(kItems + IL2CPP_ARRAY_FIRST_ELEMENT + i * 8, entries[i]);
        io.named_objects[kKlass] = {skin_memory::kStoreClass, skin_memory::kStoreNamespace};
        io.named_objects[kStore] = {skin_memory::kSkinsClass, skin_memory::kSkinsNamespace};
    }
};

int32_t read_skin(const StoreFixture& fx, uint64_t index, int slot) {
    using namespace go_active;
    int32_t v = 0;
    fx.io.read(fx.ordered_items[index] + IL2CPP_ARRAY_FIRST_ELEMENT +
               slot * 8 + ORDERED_SKIN_SKIN_ID, v);
    return v;
}

} // namespace

int main() {
    using namespace skin_memory;

    // ---- Отказные стадии -----------------------------------------------------
    {
        FakeIO io;
        skin_memory::Controller<FakeIO> ctl(io);
        SkinSlot slots[1] = {{1u, 2}};
        ctl.tick(1, 0, true, slots, 1);                  // база = 0
        assert(ctl.stage() == kStageNoBase);
        assert(ctl.diag.status == viewmodel::Status::Failed);
    }
    {
        StoreFixture fx;
        fx.add_entry("weapon.ak", {0});
        fx.finish();
        fx.io.named_objects.erase(StoreFixture::kKlass);  // класс «не тот»
        skin_memory::Controller<FakeIO> ctl(fx.io);
        SkinSlot slots[1] = {{1u, 2}};
        ctl.tick(1, StoreFixture::kBase, true, slots, 1);
        assert(ctl.stage() == kStageStoreClass);
        assert(ctl.diag.status == viewmodel::Status::Failed);
    }
    {
        StoreFixture fx;
        fx.add_entry("weapon.ak", {0});
        fx.finish();
        fx.io.mem.erase(StoreFixture::kStatics +
                        go_active::SKIN_STORE_SELECTED_SKINS); // нет lhb
        skin_memory::Controller<FakeIO> ctl(fx.io);
        SkinSlot slots[1] = {{1u, 2}};
        ctl.tick(1, StoreFixture::kBase, true, slots, 1);
        assert(ctl.stage() == kStageStore);
        assert(ctl.diag.status == viewmodel::Status::Failed);
    }

    // ---- Маппинг и запись ----------------------------------------------------
    {
        StoreFixture fx;
        fx.add_entry("weapon.ak", {0, 1, 2});
        fx.add_entry("weapon.mp5", {0, 3});
        fx.finish();

        skin_memory::Controller<FakeIO> ctl(fx.io);
        const uint32_t hash_ak = viewmodel::skin_hash("weapon.ak", 48);
        const uint32_t hash_mp5 = viewmodel::skin_hash("weapon.mp5", 48);
        assert(hash_ak != hash_mp5 && hash_ak != 0);
        SkinSlot slots[2] = {{hash_ak, 2}, {hash_mp5, 3}};
        ctl.tick(1, StoreFixture::kBase, true, slots, 2);

        assert(ctl.stage() == kStageOk);
        assert(ctl.diag.status == viewmodel::Status::Active);
        assert(ctl.diag.entry_count == 2);
        assert(ctl.diag.resolved == 2);
        assert(ctl.diag.matched == 2);
        // weapon.ak: позиции 0..2 → все 2
        assert(read_skin(fx, 0, 0) == 2);
        assert(read_skin(fx, 0, 1) == 2);
        assert(read_skin(fx, 0, 2) == 2);
        // weapon.mp5: позиции 0..1 → 3
        assert(read_skin(fx, 1, 0) == 3);
        assert(read_skin(fx, 1, 1) == 3);
        // Пишутся только позиции, у которых старое значение отличалось:
        // weapon.ak 0→2 и 1→2 (позиция 2 уже 2 — пропущена), weapon.mp5 0→3.
        assert(ctl.diag.patched == 3);
        assert(ctl.has_state());

        // Повторный тик: значения уже наши — записей быть не должно.
        ctl.tick(1, StoreFixture::kBase, true, slots, 2);
        assert(ctl.stage() == kStageOk);
        assert(ctl.diag.patched == 0);
        // Откат: выключение возвращает ОРИГИНАЛЫ каждой позиции.
        ctl.reset(true);
        assert(read_skin(fx, 0, 0) == 0);
        assert(read_skin(fx, 0, 1) == 1);
        assert(read_skin(fx, 0, 2) == 2);
        assert(read_skin(fx, 1, 0) == 0);
        assert(read_skin(fx, 1, 1) == 3);
        assert(!ctl.has_state());
    }

    // ---- Чужой хэш: ничего не трогает ---------------------------------------
    {
        StoreFixture fx;
        fx.add_entry("weapon.pistol", {0});
        fx.finish();
        skin_memory::Controller<FakeIO> ctl(fx.io);
        const uint32_t hash_strange = viewmodel::skin_hash("weapon.different", 48);
        SkinSlot slots[1] = {{hash_strange, 7}};
        ctl.tick(1, StoreFixture::kBase, true, slots, 1);
        assert(ctl.stage() == kStageOk);
        assert(ctl.diag.matched == 0);
        assert(ctl.diag.patched == 0);
        assert(read_skin(fx, 0, 0) == 0);
        assert(!ctl.has_state());
    }

    std::printf("ALL SKIN TESTS PASS\n");
    return 0;
}
