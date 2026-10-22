#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
// Пределы индекса/уровня скина живут в таблице оффсетов (game_offsets.h, блок
// «Скины»): и санитайзер, и контроллер обязаны брать их оттуда, а не держать
// свою копию числа.
#include "game_offsets_active.h"

// Vertical world FOV; hands are one static position shared by all poses, cm.
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
    // Скинченджер: подмена ИНДЕКСА скина в SyncVar Oxide.WeaponPiece ствола в
    // руках (реализация: skin_changer_memory.h, тик из esp_viewmodel_tick).
    // skin_index — индекс в ItemData.skinData[] ЭТОГО предмета, а не глобальный
    // id скина: игра сама индексирует свой массив этим числом (EN.XLy,
    // RVA 0x7CBC768: ldr x8,[ItemData,#0xD0]; [x8 + Skin*8 + 0x20]).
    bool skin_enabled = false;
    int  skin_index = 0;
    // Уровень прогрессии (WeaponProgressionSkinData хранит AssetReference[]
    // по уровням). Для скинов без уровней игра его просто не читает.
    int  skin_level = 0;
    // Принудительно пересобрать визуал. Запись индекса сама по себе картинку
    // не меняет: FP-вьюха собирается из piece на холстер/дроу, а не каждым
    // кадром (у PlayerFPWeaponView нет Update, только MjH/hIq/xM/XZE/MxP
    // (WeaponPiece, EV, Action)). Опция гасит WeaponPiece.Enabled на один тик
    // и возвращает — получается настоящий дроу уже с нашим индексом. Локально:
    // Enabled — SyncVar server->client, наружу запись не уходит.
    bool skin_rebuild = false;
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
// Целые пределы (индекс и уровень скина): границы берутся из таблицы оффсетов,
// потому что за ними — запись мусора в SyncVar Oxide.WeaponPiece.
inline int bounded(int v, int lo, int hi, int fallback) {
    return (v < lo || v > hi) ? fallback : v;
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
    s.skin_index = bounded(s.skin_index, 0, go_active::SKIN_INDEX_MAX, 0);
    s.skin_level = bounded(s.skin_level, 0, go_active::SKIN_LEVEL_MAX, 0);
    return s;
}
// Optional trailer after the existing v4 config. Old readers ignore it;
// missing/truncated/unknown trailers load disabled defaults, never stale state.
struct ConfigExtension {
    uint32_t magic = 0x31574d56u; // VMW1
    // v4 added aim lead, v5 kept the 36-byte layout, and v6 assigns bit 11
    // to no-ballistics. Bit 10 remains retired. v1–v3 were 32 bytes. v7 took
    // bit 12 for no-fall-damage; the feature is gone, the bit stays allowed so
    // profiles written back then still load (their trailer decodes to the same
    // settings minus that one toggle). The layout stays 36 bytes.
    uint32_t version = 7;
    uint32_t size = 36;
    uint32_t flags = 0;
    float fov = 90.f, x = 0.f, y = 0.f, z = 0.f;
    float aim_lead_ms = 0.f;   // legacy v4 field; retained for config binary compatibility, ignored
};
static_assert(sizeof(ConfigExtension) == 36, "stable config extension");
inline ConfigExtension encode(ViewmodelSettings s) {
    s = sanitize(s);
    ConfigExtension e;
    // Bits 2..3: 0 = default, 1 = 144 (legacy toggle), 2 = 90, 3 = 120.
    const uint32_t fps_mode = s.fps_limit == 144 ? 1u : s.fps_limit == 90 ? 2u : s.fps_limit == 120 ? 3u : 0u;
    e.flags = (s.fov_enabled ? 1u : 0u) | (s.hands_enabled ? 2u : 0u) | (fps_mode << 2) |
              (s.fast_loot ? 16u : 0u) | (s.no_recoil_button ? 32u : 0u) |
              (s.no_spread ? 64u : 0u) | (s.no_ballistics ? 2048u : 0u);
    e.fov = s.fov; e.x = s.x; e.y = s.y; e.z = s.z;
    return e;
}
inline ViewmodelSettings decode(const void* bytes, size_t size) {
    ViewmodelSettings s;
    // 32 = v1–v3 layout; 36 = v4+. A short record memcpy's into the struct;
    // the trailing legacy lead field is ignored by the automatic aim solver.
    if (size != 32 && size != sizeof(ConfigExtension)) return s;
    ConfigExtension e; std::memcpy(&e, bytes, size);
    // v1: discard retired flags 4..6. v2 keeps bit 5 as the UI-only recoil
    // toggle and bit 6 for no-spread. v3 assigns bit 4 to fast pickup; an
    // older v1/v2 profile carrying retired bit 4 must not enable it.
    const uint32_t allowed_flags = e.version >= 7 ? 8191u : (e.version >= 6 ? 4095u : 2047u);
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
    s.fov = e.fov; s.x = e.x; s.y = e.y; s.z = e.z;
    return sanitize(s);
}
// Decode the legacy v4 prediction setting for compatibility tests/tools only.
// Runtime aim ignores it and computes prediction from target motion + ballistics.
inline float decode_aim_lead_ms(const void* bytes, size_t size) {
    if (size != sizeof(ConfigExtension)) return 0.f;
    ConfigExtension e; std::memcpy(&e, bytes, sizeof(e));
    if (e.magic != 0x31574d56u || (e.version < 4 || e.version > 7) || e.size != sizeof(e)) return 0.f;
    return (e.aim_lead_ms >= 0.f && e.aim_lead_ms <= 150.f) ? e.aim_lead_ms : 0.f;
}
enum class Status { Off, Waiting, Active, ADS, Failed };
struct Diagnostics {
    Status fov = Status::Off, hands = Status::Off;
    Status no_spread = Status::Off;
    Status fast_fire = Status::Off;
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

// ---- Скинченджер: что видно меню и one-shot CLI ----------------------------
//
// Тип скина — по классу ассета SkinData (иерархия из dump.cs, см. блок «Скины»
// в game_offsets.h). Скинченджеру нужны OBJECT-классы: они дают ДРУГОЙ префаб
// (WeaponObjectSkinData.FPPrefabRef / rightHandPrefabRef), то есть настоящую
// модель. Recolor-классы только подменяют материал — это перекраска, не скин,
// и в списке они помечаются отдельно, чтобы меню честно показывало, что
// выбрано.
enum class SkinKind : int {
    Unknown = 0,
    WeaponObject = 1,        // WeaponObjectSkinData — другой префаб ствола
    WeaponProgression = 2,   // WeaponProgressionSkinData — префабы по уровням
    WeaponPreview = 3,       // WeaponPreviewPrefabSkinData — только превью
    WeaponRecolor = 4,       // WeaponRecolorSkinData — перекраска материалом
    EquipObject = 5,         // EquipmentObjectSkinData — одежда/руки
    EquipRecolor = 6,        // EquipmentRecolorSkinData — перекраска одежды
    Other = 7,               // BuildingSkinData / Interactable* — не оружие
};
inline const char* skin_kind_name(SkinKind kind) {
    switch (kind) {
        case SkinKind::WeaponObject:      return "object";
        case SkinKind::WeaponProgression: return "progression";
        case SkinKind::WeaponPreview:     return "preview";
        case SkinKind::WeaponRecolor:     return "recolor";
        case SkinKind::EquipObject:       return "equip-object";
        case SkinKind::EquipRecolor:      return "equip-recolor";
        case SkinKind::Other:             return "other";
        default:                          return "unknown";
    }
}
// true для скинов, которые меняют модель, а не материал.
inline bool skin_kind_is_object(SkinKind kind) {
    return kind == SkinKind::WeaponObject || kind == SkinKind::WeaponProgression ||
           kind == SkinKind::EquipObject;
}

// Предел списка: массив ItemData.skinData у предмета короткий, а больше меню
// и не нужно. Совпадает с skinchanger::kMaxSkins.
constexpr int kSkinEntryMax = 24;

// Одна строка списка: скин предмета, который сейчас в руках.
struct SkinEntryView {
    int      index = -1;             // значение, которое пишется в WeaponPiece.Skin
    SkinKind kind = SkinKind::Unknown;
    int      levels = 1;             // >1 у WeaponProgressionSkinData (массивы по уровням)
    bool     ready = false;          // AssetReference.m_Operation непуста: ассет уже запрашивался
    char     id[48] = {};            // SkinData.Id (UTF-8)
    char     base_id[32] = {};       // SkinData.baseId — id базового предмета
};

// Почему не применилось — УКРУПНЁННО, для меню и автолога skin.log. Стадий у
// контроллера девятнадцать (skin_changer_memory.h), но игроку нужно не «стадия
// 13», а что делать. Unreadable и WrongLayout разделены намеренно: первое —
// транспорт не читает голову WeaponPiece, второе — читает, но это не
// WeaponPiece, то есть оффсеты не от этого билда (нужен дамп новой версии). Детальная расшифровка для CLI и разработчика —
// skin_change_stop_reason() в game_viewmodel.cpp; группировка живёт здесь,
// чтобы меню и лог брали одну и ту же формулировку, а не две свои.
enum class SkinBlock : int {
    None = 0,        // порядок, скин применён
    NoPlayer,        // нет локального игрока
    NoWeapon,        // компонент оружия не найден
    EmptyHands,      // в руках пусто
    NoItemData,      // нет данных предмета
    NoSkins,         // у предмета нет массива скинов
    BadIndex,        // выбранный индекс не подходит этому стволу
    Unreadable,      // WeaponPiece не читается транспортом
    WrongLayout,     // читается, но это не WeaponPiece: оффсеты не от этого билда
    WriteFailed,     // запись не подтвердилась чтением обратно
    Stuck,           // ствол не вернулся из холстера
};
inline SkinBlock skin_block(int stage) {
    switch (stage) {
        case 0:                                   return SkinBlock::None;
        case 1:  case 2:  case 3:  case 4:        return SkinBlock::NoPlayer;
        case 5:                                   return SkinBlock::NoWeapon;
        case 6:  case 7:  case 8:  case 9:        return SkinBlock::EmptyHands;
        case 10:                                  return SkinBlock::NoItemData;
        case 11: case 12:                         return SkinBlock::NoSkins;
        case 13:                                  return SkinBlock::BadIndex;
        case 14:                                  return SkinBlock::Unreadable;
        case 15:                                  return SkinBlock::WrongLayout;
        case 16: case 17: case 19:                return SkinBlock::WriteFailed;
        case 18:                                  return SkinBlock::Stuck;
        default:                                  return SkinBlock::NoPlayer;
    }
}
// Русские строки: меню пропускает их через XS() (перевод в lang_tables.inc),
// автолог пишет как есть — весь остальной вывод чита тоже на русском.
inline const char* skin_block_text(SkinBlock block) {
    switch (block) {
        case SkinBlock::None:        return "Применено";
        case SkinBlock::NoPlayer:    return "Нет локального игрока";
        case SkinBlock::NoWeapon:    return "Оружие не найдено";
        case SkinBlock::EmptyHands:  return "В руках пусто";
        case SkinBlock::NoItemData:  return "Нет данных предмета";
        case SkinBlock::NoSkins:     return "У предмета нет скинов";
        case SkinBlock::BadIndex:    return "Индекс не подходит стволу";
        case SkinBlock::Unreadable:  return "WeaponPiece не читается";
        case SkinBlock::WrongLayout: return "Оффсеты WeaponPiece не от этого билда";
        case SkinBlock::WriteFailed: return "Запись не подтвердилась";
        case SkinBlock::Stuck:       return "Ствол не вернулся";
        default:                     return "";
    }
}

// Снимок состояния для меню: заполняется контроллером (skin_changer_memory.h)
// и не делает дополнительных обращений к памяти игры на каждый кадр меню.
struct SkinUiInfo {
    int  count = 0;             // сколько скинов у предмета в руках (0 = нет/не привязаны)
    int  skin = -1;             // WeaponPiece.Skin, который лежит в памяти сейчас
    int  level = -1;            // WeaponPiece.SkinLevel сейчас
    bool fp_stale = false;      // PlayerFPWeaponView собран из ДРУГОГО индекса
    bool handle_live = false;   // PlayerWeapon.skinVisualHandle._model != 0
    bool rebuild_busy = false;  // идёт принудительный холстер/дроу
    bool piece_read = false;    // skin/level действительно прочитаны из памяти
    int  stage = 0;             // 0 = порядок; расшифровка — skin_change_stop_reason()
    Status status = Status::Off;
    char weapon[48] = {};       // ItemData.m_ShortName/m_Name предмета в руках
};
// Подробности выбранного скина (id, тип, уровни, готовность ассета) в снимок не
// дублируются: и меню, и автолог берут их из esp_skin_entries() по индексу —
// один источник, а не два, которые могут разойтись.
}

// Список скинов предмета в руках (кэш контроллера, не чтение памяти на вызов).
// Возвращает число заполненных строк.
int  esp_skin_entries(viewmodel::SkinEntryView* out, int max);
// Снимок состояния скинченджера для меню и one-shot CLI.
viewmodel::SkinUiInfo esp_skin_ui();
// Расшифровка стадии остановки для лога/меню (стадии — в skin_changer_memory.h).
const char* skin_change_stop_reason(viewmodel::Status status, int stage);
// Одноразовый запрос принудительной пересборки визуала (холстер/дроу): ставится
// меню («Пересобрать сейчас») и one-shot CLI, снимается контроллером на
// следующем тике оружия. Сам по себе тумблер skin_rebuild означает «пересобирать
// автоматически при смене индекса».
void esp_skin_request_rebuild();
// Одноразовый запрос перечитать список скинов. Ставится меню, пока раздел
// раскрыт, и CLI `skins`: перечитывание читает строки посимвольно, поэтому по
// запросу, а не каждый тик. Работает и при выключенном тумблере — список
// показывается до включения, записей при этом нет.
void esp_skin_request_scan();

// Main-thread tick, independent of ESP toggles. No background writer.
void esp_viewmodel_tick(const ViewmodelSettings& settings);
viewmodel::Diagnostics esp_viewmodel_diagnostics();
// Снимок состояния разблокировки FPS для меню (выбран режим / применён / поле).
viewmodel::FpsUiInfo esp_viewmodel_fps_ui();
// restore=true only for a live connection (exit/build switch).
// restore=false on lost process/session: forget old addresses without writes.
void esp_viewmodel_reset(bool restore);
