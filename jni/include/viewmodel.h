#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>

// Vertical world FOV; hands are one static position shared by all poses, cm.
// Слоты скинченджера (SkinSlot) объявлены до ViewmodelSettings: один и тот
// же массив уходит и в тик контроллера, и в конфиг (расширение v7).
struct SkinSlot {
    uint32_t hash = 0;    // fnv1a (ascii) короткого имени предмета; 0 = слот свободен
    int32_t  skin_id = 0; // желаемый № скина (OrderedSkin.skinId)
};
struct ViewmodelSettings {
    bool fov_enabled = false;
    bool hands_enabled = false;
    float fov = 90.f;
    float x = 0.f, y = 0.f, z = 0.f;
    int fps_limit = 0; // 0 = original game limit; otherwise 90 / 120 / 144
    // «Без отдачи»: гасит амплитуду в RecoilConfig хитскан-ствола в руках
    // (реализация: no_recoil_memory.h, тик из esp_viewmodel_tick).
    bool no_recoil_button = false;
    bool no_spread = false;
    bool no_ballistics = false;
    // Быстрый подбор («Забрать все» без пауз): не трогает код игры — обходит
    // раннеры плеер-лупа UniTask и обнуляет точный 0.35f размер активных
    // DelayPromise, на которых игра ждёт между предметами при сборе лута.
    // Реализация: fast_loot_memory.h, тик из esp_viewmodel_tick.
    bool fast_loot = false;
    // «Скинченджер»: переписывает skinId в локальном сторе выбранных скинов
    // (реализация: skin_memory.h, тик из esp_viewmodel_tick). Слот задаётся
    // хэшем короткого имени предмета и числом скина (№№ из ItemSkinsDTO).
    bool skin_changer = false;
    SkinSlot skin_slots[4] = {};
};

namespace viewmodel {
// The Android build uses -ffast-math; do not rely on isfinite under that flag.
inline bool finite(float v) {
    uint32_t bits; std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
inline float bounded(float v, float lo, float hi, float fallback) {
    return !finite(v) ? fallback : (v < lo ? lo : (v > hi ? hi : v));
}
inline int sanitize_fps_limit(int fps) {
    return fps == 90 || fps == 120 || fps == 144 ? fps : 0;
}
inline ViewmodelSettings sanitize(ViewmodelSettings s) {
    s.fps_limit = sanitize_fps_limit(s.fps_limit);
    s.fov = bounded(s.fov, 50.f, 120.f, 90.f);
    s.x = bounded(s.x, -50.f, 50.f, 0.f);
    s.y = bounded(s.y, -50.f, 50.f, 0.f);
    s.z = bounded(s.z, -50.f, 50.f, 0.f);
    // Слот без хэша не несёт смысла, а число скина без хэша не применяется.
    for (auto& slot : s.skin_slots) {
        if (slot.hash == 0) slot.skin_id = 0;
        if (!(slot.skin_id >= 0 && slot.skin_id <= 1023))
            slot.skin_id = std::max(0, std::min(1023, slot.skin_id));
    }
    return s;
}
// fnv1a по байтам ascii-короткого имени; используется и контроллером скинченджера,
// и конфигом (v7) — хэщат одну и ту же строку.
inline uint32_t skin_hash(const char* ascii, size_t length) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < length && ascii[i]; ++i)
        h = (h ^ (unsigned char)ascii[i]) * 0x01000193u;
    return h;
}
// Optional trailer after the existing v4 config. Old readers ignore it;
// missing/truncated/unknown trailers load disabled defaults, never stale state.
struct ConfigExtension {
    uint32_t magic = 0x31574d56u; // VMW1
    // v4 added aim lead, v5 kept the 36-byte layout, v6 assigns bit 11
    // to no-ballistics, and v7 (68 bytes) appends the four skinchanger slots.
    // Bit 10 remains retired. v1–v3 were 32 bytes.
    uint32_t version = 7;
    uint32_t size = 68;
    uint32_t flags = 0;
    float fov = 90.f, x = 0.f, y = 0.f, z = 0.f;
    float aim_lead_ms = 0.f;   // v4: aim prediction lead, 0..150 ms (0 = off)
    // v7: скинченджер — четыре слота «хэш предмета + № скина».
    uint32_t skin_hash[4] = {};  // 0 = слот свободен
    int32_t  skin_id[4] = {};    // применяется только с ненулевым хэшем
};
static_assert(sizeof(ConfigExtension) == 68, "stable config extension");
inline ConfigExtension encode(ViewmodelSettings s) {
    s = sanitize(s);
    ConfigExtension e;
    // Bits 2..3: 0 = default, 1 = 144 (legacy toggle), 2 = 90, 3 = 120.
    const uint32_t fps_mode = s.fps_limit == 144 ? 1u : s.fps_limit == 90 ? 2u : s.fps_limit == 120 ? 3u : 0u;
    e.flags = (s.fov_enabled ? 1u : 0u) | (s.hands_enabled ? 2u : 0u) | (fps_mode << 2) |
              (s.fast_loot ? 16u : 0u) | (s.no_recoil_button ? 32u : 0u) |
              (s.no_spread ? 64u : 0u) | (s.no_ballistics ? 2048u : 0u) |
              (s.skin_changer ? 4096u : 0u);
    e.fov = s.fov; e.x = s.x; e.y = s.y; e.z = s.z;
    // Скинченджер (v7): слоты сохраняются всегда, бит 12 — тумблер.
    for (int i = 0; i < 4; ++i) {
        e.skin_hash[i] = s.skin_slots[i].hash;
        e.skin_id[i] = s.skin_slots[i].skin_id;
    }
    return e;
}
inline ViewmodelSettings decode(const void* bytes, size_t size) {
    ViewmodelSettings s;
    // 32 = v1–v3 layout (no aim lead); 36 = v4–v6; 68 = v7 (skin slots).
    // Короткая запись v4–v6 влезает в 68-байтовую структуру, слоты скинов
    // остаются нулевыми.
    if (size != 32 && size != 36 && size != sizeof(ConfigExtension)) return s;
    ConfigExtension e; std::memcpy(&e, bytes, size);
    // v1: discard retired flags 4..6. v2 keeps bit 5 as the UI-only recoil
    // toggle and bit 6 for no-spread. v3 assigns bit 4 to fast pickup; an
    // older v1/v2 profile carrying retired bit 4 must not enable it.
    const uint32_t allowed_flags = e.version >= 7 ? 4095u | 4096u : e.version >= 6 ? 4095u : 2047u;
    if (e.magic != 0x31574d56u || (e.version < 1 || e.version > 7) ||
        e.size != size || (e.flags & ~allowed_flags)) return s;
    s.fov_enabled = (e.flags & 1u) != 0;
    s.hands_enabled = (e.flags & 2u) != 0;
    const int fps_modes[] = {0, 144, 90, 120};
    s.fps_limit = fps_modes[(e.flags >> 2) & 3u];
    s.no_recoil_button = e.version >= 2 && (e.flags & 32u) != 0;
    s.no_spread = e.version >= 2 && (e.flags & 64u) != 0;
    s.no_ballistics = e.version >= 6 && (e.flags & 2048u) != 0;
    s.fast_loot = e.version >= 3 && (e.flags & 16u) != 0;
    // Скинченджер: тумблер — бит 12, слоты — только в 68-байтовой записи v7.
    s.skin_changer = e.version >= 7 && (e.flags & 4096u) != 0;
    if (e.version >= 7 && size == sizeof(ConfigExtension)) {
        for (int i = 0; i < 4; ++i) {
            s.skin_slots[i].hash = e.skin_hash[i];
            s.skin_slots[i].skin_id = e.skin_id[i];
        }
    }
    s.fov = e.fov; s.x = e.x; s.y = e.y; s.z = e.z;
    return sanitize(s);
}
// Aim prediction lead from a v4 trailer, in milliseconds. Old (v1–v3) or
// malformed trailers read as 0 — prediction off, never a stale value.
inline float decode_aim_lead_ms(const void* bytes, size_t size) {
    if (size != sizeof(ConfigExtension) && size != 36) return 0.f;
    ConfigExtension e; std::memcpy(&e, bytes, size);
    if (e.magic != 0x31574d56u || (e.version < 4 || e.version > 7) || e.size != size) return 0.f;
    return (e.aim_lead_ms >= 0.f && e.aim_lead_ms <= 150.f) ? e.aim_lead_ms : 0.f;
}
enum class Status { Off, Waiting, Active, ADS, Failed };
struct Diagnostics {
    Status fov = Status::Off, hands = Status::Off;
    Status no_spread = Status::Off;
    Status fast_fire = Status::Off;
    Status skins = Status::Off;
};
// Скинченджер: снимок стора выбранных скинов для меню. Заполняется контроллером
// при последнем тике; обращений к памяти игры из меню не делается.
constexpr int kSkinDiagEntries = 16;
struct SkinDiagEntry {
    char item[48] = {};     // короткое имя из стора
    uint32_t hash = 0;
    int32_t skin_id = -1;   // текущий skinId первой позиции orderedSkin
    int32_t count = 0;      // позиций (OrderedSkin) на этот предмет
};
struct SkinDiag {
    Status status = Status::Off;
    uint64_t store = 0;         // адрес SelectedSkins (слишек lhb)
    int entries = 0;            // записей всего
    int resolved = 0;           // нет(ok) записей с читаемым коротким именем
    int matched = 0;            // записей, попавших в слоты игрока
    int patched = 0;            // перезаписанных skinId
    int owned = 0;              // хранимых оригиналов (откат)
    int stage = 0;              // стадия сбоя (0 = ок; значения в skin_memory.h)
    int entry_count = 0;
    SkinDiagEntry entry[kSkinDiagEntries];
};
// Снимок состояния limiting FPS для меню: снимается контроллером при последнем
// тике (до 100 мс назад), дополнительных обращений к памяти игры не делает.
struct FpsUiInfo {
    int requested = 0;               // последний обработанный режим (0 = обычный)
    Status status = Status::Off;
    int field_value = 0;             // фактическое значение backing-поля targetFrameRate
    int pace_kind = 0;               // тип Android frame pacing (0 = заглушен)
    uint64_t build_id = 0;           // build ID найденной libunity (0 = mapping'а нет/не прочитан)
};
}

// Main-thread tick, independent of ESP toggles. No background writer.
void esp_viewmodel_tick(const ViewmodelSettings& settings);
viewmodel::Diagnostics esp_viewmodel_diagnostics();
// Снимок скинченджера для меню.
viewmodel::SkinDiag esp_skin_diagnostics();
// Снимок состояния разблокировки FPS для меню (выбран режим / применён / поле).
viewmodel::FpsUiInfo esp_viewmodel_fps_ui();
// restore=true only for a live connection (exit/build switch).
// restore=false on lost process/session: forget old addresses without writes.
void esp_viewmodel_reset(bool restore);
