#pragma once
// «Скинченджер»: переписывает skinId внутри ЛОКАЛЬНОГО стора выбранных скинов.
//
// Сервер не трогается: стор (VNB.lhb — тот же, что игра держит в файле
// SelectedSkinsKey.json) читается игрой при сборке вида стволов, поэтому новое
// значение применяется при следующем пересборе вида (смена ствола в руках,
// респавн). Записи через write_heap, оригиналы держим под откат — та же
// техника откатных контроллеров, что у «без баллистики» (no_ballistics_memory.h):
// восстановление при выключении, смена сессии,elist отвал процесса.
//
// Чего контроллер НЕ делает: не добавляет записи в стор, у которого их ещё нет
// (List не пересобирается — переписываются только существующие int32), и
// проверяет правдоподобие прочитанного, а не «лечит» мусор.
#include "viewmodel.h"
#include "game_offsets_active.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace skin_memory {

// Имя/пространство классов стора. Обфусцированное имя VNB РОТАЦИРУЕТСЯ между
// сборками игры — при смене дампа пересчитывается TypeInfo-RVA в
// game_offsets.h; полевые смещения GUISystem стабильны по dump.cs.
inline constexpr const char* kStoreClass      = "VNB";
inline constexpr const char* kStoreNamespace  = "";
inline constexpr const char* kSkinsClass      = "SelectedSkins";
inline constexpr const char* kSkinsNamespace  = "Oxide.GUISystem";

// Границы правдоподобия чтений (мусор отсекается, не «лечится»).
inline constexpr int kStoreEntryLimit = 64;   // записей в сторе
inline constexpr int kOrderedLimit    = 16;   // позиций (OrderedSkin) на предмет
inline constexpr int kStringLimit     = 40;   // длина короткого имени
inline constexpr int kMaxOwned        = 48;   // ёмкость откатных снимков
inline constexpr int kMaxWantedSlots  = 4;    // = размер SkinSlot в настройках

// Стадии сбоя для диагностики (0 = ок).
enum Stage {
    kStageOk = 0,
    kStageNoBase = 1,        // нет il2cpp-базы
    kStageStoreClass = 2,    // класс VNB не подошёл
    kStageStaticFields = 3,  // нет static_fields
    kStageStore = 4,         // нет SelectedSkins в статике
    kStageList = 5,          // нет List<SelectedSkin> или мусор
    kStageReadFail = 6,      // читка/запись/подтверждение сорвались
};

// Одна перестановка игрока: предмет по хэшу имени → желаемый № скина.
struct Wanted {
    uint32_t hash = 0;
    int32_t skin_id = 0;
};

struct Owned {
    uint64_t address = 0;
    int32_t original = 0;
    bool owned = false;
};

template<class IO> class Controller {
    IO& io;
    uint64_t session_ = 0, base_ = 0;
    std::array<Owned, kMaxOwned> owned_{};
    int owned_count_ = 0;
    int stage_ = kStageOk;

    bool fail(int stage) {
        stage_ = stage;
        diag.status = viewmodel::Status::Failed;
        diag.stage = stage;
        return false;
    }
    void clear_failures() {
        stage_ = kStageOk;
        diag.stage = kStageOk;
    }

    // Стор резолвится каждый тик: путь короче (класс → static_fields → lhb),
    // и он сам является проверкой «объект тот» — кэша с лайфчеком не нужно.
    bool resolve_store(uint64_t& store) {
        using namespace go_active;
        if (!base_) return fail(kStageNoBase);
        const uint64_t klass = io.ptr(base_ + SKIN_STORE_TYPEINFO_RVA);
        if (!klass || !io.named_klass(klass, kStoreClass, kStoreNamespace))
            return fail(kStageStoreClass);
        const uint64_t statics = io.ptr(klass + IL2CPP_CLASS_STATIC_FIELDS_OFFSET);
        if (!statics) return fail(kStageStaticFields);
        store = io.ptr(statics + SKIN_STORE_SELECTED_SKINS);
        if (!store || !io.named(store, kSkinsClass, kSkinsNamespace))
            return fail(kStageStore);
        return true;
    }

    // utf16-строка из heap → ascii (cap 47 байт) + длина. Строки стора —
    // ascii-короткие имена оружия; не-ascii заменяется '_', но длина сохраняется,
    // чтобы хэш стабильного имени не «прыгал» от цели.
    bool read_ascii_name(uint64_t string_obj, char* out, size_t out_cap, int& length) {
        length = 0;
        if (!io.read(string_obj + go_active::IL2CPP_STRING_LENGTH, length)) return false;
        if (length < 0 || length > kStringLimit) return false;
        if (out_cap < size_t(length) + 1) return false;
        for (int i = 0; i < length; ++i) {
            uint16_t ch = 0;
            if (!io.read(string_obj + go_active::IL2CPP_STRING_CHARS + i * 2, ch)) return false;
            out[i] = (ch >= 0x20 && ch < 0x7f) ? static_cast<char>(ch) : '_';
        }
        out[length] = 0;
        return true;
    }

    int find_owned(uint64_t address) const {
        for (int i = 0; i < owned_count_; ++i)
            if (owned_[i].owned && owned_[i].address == address) return i;
        return -1;
    }
    bool remember_owned(uint64_t address, int32_t original) {
        if (find_owned(address) >= 0) return true;
        if (owned_count_ >= kMaxOwned) return false;
        Owned snapshot{}; snapshot.address = address; snapshot.original = original; snapshot.owned = true;
        owned_[owned_count_++] = snapshot;
        return true;
    }

    // Возвращает своё: только «мы ещё держим адрес и значение отличается» —
    // чужую перезаписанную игрунную дурь не переписываем.
    bool restore_all() {
        bool ok = true;
        for (int i = 0; i < owned_count_; ++i) {
            Owned& o = owned_[i];
            if (!o.owned) continue;
            int32_t now = 0;
            if (!io.read(o.address, now)) { fail(kStageReadFail); ok = false; continue; }
            if (now == o.original) { o = Owned{}; continue; }
            if (!io.write_heap(o.address, o.original)) { fail(kStageReadFail); ok = false; continue; }
            if (!io.read(o.address, now) || now != o.original) { fail(kStageReadFail); ok = false; continue; }
            o = Owned{};
        }
        if (ok) clear_failures();
        return ok;
    }

public:
    viewmodel::SkinDiag diag{};
    explicit Controller(IO& io_ref) : io(io_ref) {}
    bool has_state() const { return owned_count_ > 0; }
    int stage() const { return stage_; }

    void reset(bool restore_originals) {
        if (restore_originals) restore_all();
        Owned empty{};
        for (auto& slot : owned_) slot = empty;
        owned_count_ = 0;
        diag = viewmodel::SkinDiag{};
        stage_ = kStageOk;
        session_ = base_ = 0;
    }

    // Один тик: читает стор и применяет/сверяет перестановки. Темп задаётся
    // внешним вызовом (вleaning esp_viewmodel_tick — не чаще ~250 мс).
    void tick(uint64_t session, uint64_t base, bool enabled,
              const SkinSlot* slots, int slot_count) {
        using namespace go_active;
        // Смена сессии/базы (рестарт оверлея, переподвес): снимки без отката
        // чистим — старые адреса писать дальше нельзя, а вернуться к «до нас»
        // в другом процессе нечем.
        if (session != session_ || base != base_) {
            reset(false);
            session_ = session;
            base_ = base;
        }
        diag = viewmodel::SkinDiag{};
        if (!enabled) {
            diag.status = viewmodel::Status::Off;
            if (owned_count_ > 0 && !restore_all())
                diag.status = viewmodel::Status::Failed;
            return;
        }

        uint64_t store = 0;
        if (!resolve_store(store)) return;
        diag.store = store;
        diag.status = viewmodel::Status::Active;

        // Слоты перестановок: массив из настроек (sanitize уже чистил,
        // здесь повторно фильтруем на всякий случай).
        Wanted wanted[kMaxWantedSlots];
        int wanted_count = 0;
        if (slots) {
            for (int i = 0; i < slot_count && wanted_count < kMaxWantedSlots; ++i) {
                if (slots[i].hash == 0) continue;
                const int32_t skin = slots[i].skin_id;
                if (!(skin >= 0 && skin <= 1023)) continue;
                bool duplicate = false;
                for (int j = 0; j < wanted_count; ++j)
                    if (wanted[j].hash == slots[i].hash) duplicate = true;
                if (!duplicate) {
                    Wanted w{}; w.hash = slots[i].hash; w.skin_id = skin;
                    wanted[wanted_count++] = w;
                }
            }
        }

        const uint64_t list = io.ptr(store + SKIN_STORE_LIST);
        if (!list) { fail(kStageList); return; }
        int32_t count = 0;
        if (!io.read(list + IL2CPP_LIST_SIZE, count)) { fail(kStageList); return; }
        if (count < 0) count = 0;
        if (count > kStoreEntryLimit) count = kStoreEntryLimit;
        const uint64_t items = io.ptr(list + IL2CPP_LIST_ITEMS);
        if (!items) { fail(kStageList); return; }

        int snapshot_entries = 0;
        for (int i = 0; i < count; ++i) {
            const uint64_t entry = io.ptr(items + IL2CPP_ARRAY_FIRST_ELEMENT + i * 8);
            if (!entry) continue;
            const uint64_t name_obj = io.ptr(entry + SELECTED_SKIN_SHORT_NAME);
            char name[48] = {};
            int name_len = 0;
            if (!name_obj || !read_ascii_name(name_obj, name, sizeof(name), name_len))
                continue;
            ++diag.resolved;
            const uint32_t hash = viewmodel::skin_hash(name, 48);

            const uint64_t ordered = io.ptr(entry + SELECTED_SKIN_ORDERED);
            if (!ordered) { stage_ = kStageList; continue; }
            int32_t ordered_count = 0;
            if (!io.read(ordered + IL2CPP_LIST_SIZE, ordered_count)) { stage_ = kStageList; continue; }
            if (ordered_count < 0) ordered_count = 0;
            if (ordered_count > kOrderedLimit) ordered_count = kOrderedLimit;
            const uint64_t ordered_items = io.ptr(ordered + IL2CPP_LIST_ITEMS);
            if (!ordered_items) { stage_ = kStageList; continue; }

            // Снимок для меню — первые kSkinDiagEntries записей стора.
            if (snapshot_entries < viewmodel::kSkinDiagEntries) {
                viewmodel::SkinDiagEntry& e = diag.entry[snapshot_entries++];
                for (int c = 0; c < 47 && name[c]; ++c) e.item[c] = name[c];
                e.item[47] = 0;
                e.hash = hash;
                e.count = ordered_count;
                io.read(ordered_items + IL2CPP_ARRAY_FIRST_ELEMENT + ORDERED_SKIN_SKIN_ID, e.skin_id);
            }

            // Своя перестановка на этот предмет?
            int slot_index = -1;
            for (int s = 0; s < wanted_count; ++s)
                if (wanted[s].hash == hash) { slot_index = s; break; }
            if (slot_index < 0) continue;
            ++diag.matched;

            for (int j = 0; j < ordered_count; ++j) {
                const uint64_t addr =
                    ordered_items + IL2CPP_ARRAY_FIRST_ELEMENT + j * 8 + ORDERED_SKIN_SKIN_ID;
                int32_t now = 0;
                if (!io.read(addr, now)) continue;
                if (now == wanted[slot_index].skin_id) continue;
                if (!remember_owned(addr, now)) { fail(kStageReadFail); return; }
                if (!io.write_heap(addr, wanted[slot_index].skin_id)) { fail(kStageReadFail); return; }
                int32_t after = 0;
                if (!io.read(addr, after) || after != wanted[slot_index].skin_id) {
                    fail(kStageReadFail);
                    return;
                }
                ++diag.patched;
            }
        }
        diag.entry_count = count;
        if (stage_ == kStageOk) clear_failures();
    }
};

} // namespace skin_memory
