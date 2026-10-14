#pragma once
// «Свой туман и своё небо»: настройки и чистая (хост-тестируемая) часть фичи.
//
// Исполнение — game.cpp (тик tod_env_tick рядом с «Всегда день»), меню —
// menu.cpp (вкладка «Разное»), конфиг — трейлер CSK1 в config.cpp. Здесь только
// структуры, clamp-и, кодирование трейлера и раскладка нативного буфера
// Gradient-а: ни ImGui, ни Android — поэтому всё это проверяется хост-тестом
// jni/tests/test_fog_sky.cpp.
//
// Куда пишем. В сцене живёт ассет Time Of Day: инстанс TOD_Sky находит
// структурный скан (g_day_tod в game.cpp, смещения — TOD_* в game_offsets.h;
// сверено по обоим дампам, Release 11949 и Beta 11959 — смещения параметров
// совпадают байт в байт). Дальше:
//   * Туман: плотность -> TOD_Atmosphere.Fogginess, цвет -> ключи Gradient-а
//     FogColor у TOD_DayParameters и TOD_NightParameters.
//   * Небо: время суток -> Cycle.Hour, облака -> TOD_CloudParameters, воздух
//     -> TOD_AtmosphereParameters, цвет -> ключи SkyColor (Day/Night).
//
// Три категории полей — три способа записи (подробности в game.cpp):
//   1. Поля, которые игра переписывает сама каждый тик (Cycle.Hour — часы;
//      Clouds.Coverage/Brightness и Atmosphere.Fogginess — система погоды),
//      держит фоновый писатель с периодом ~2 мс: тот же приём, которым уже
//      работает «Всегда день».
//   2. Поля, которых игра не трогает (размер и насыщенность облаков, рэлей/ми/
//      яркость/контраст воздуха), пишутся один раз и возвращаются при
//      выключении: оригиналы сохраняются в памяти процесса.
//   3. Цвет ключей — содержимое нативного буфера Gradient-а. Ссылки на
//      Gradient-объекты игра переставляет сама (погодный блендер
//      BiomeDayParametersMixer собирает свой кэш и подменяет им SkyColor/
//      FogColor), поэтому «свой цвет» живёт в буфере ключей (Gradient.m_Ptr), а
//      не в ссылке: пере-привязка ссылок такому цвету не мешает, а оригинал
//      буфера сохраняется побайтно и возвращается при выключении.
//
// «Своё время» сильнее «Всегда день»: если включены оба, час берётся из
// sky_hour.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fog_sky {

// Значения одного цвета — как float-тройка: заголовок не знает про ImVec4
// (цвет в меню хранится отдельно, в AppState, и оттуда копируется сюда).
struct Settings {
    // Туман.
    bool  fog_on      = false;
    float fog_density = 0.35f;                            // 0..1 -> Fogginess
    float fog_color[3] = {0.62f, 0.64f, 0.68f};           // цвет ключей FogColor
    float fog_tint    = 0.f;                              // 0..1, доля своего цвета

    // Небо.
    bool  sky_on        = false;
    bool  sky_time      = false;                          // держать своё время суток
    float sky_hour      = 12.f;                           // 0..24
    float sky_color[3]  = {0.36f, 0.55f, 1.00f};          // цвет ключей SkyColor
    float sky_tint      = 0.f;                            // 0..1
    float cloud_coverage   = 0.45f;                       // 0..1   (перебивает погода)
    float cloud_brightness = 1.0f;                        // 0..2   (перебивает погода)
    float cloud_saturation = 1.0f;                        // 0..2
    float cloud_size       = 1.0f;                        // 0.5..5
    float atmo_rayleigh    = 1.0f;                        // 0..4, синь неба
    float atmo_mie         = 1.0f;                        // 0..4, дымка у горизонта
    float atmo_brightness  = 1.0f;                        // 0..2
    float atmo_contrast    = 1.0f;                        // 0..2
};

// Сравнение по полям, а не memcmp: у структуры есть padding после bool-ов, и
// его байты у копий из разных мест могут отличаться — memcmp давал бы
// «настройки сменились» на ровном месте (лишние переприменения каждый кадр).
inline bool same(const Settings& a, const Settings& b) {
    if (a.fog_on != b.fog_on || a.sky_on != b.sky_on || a.sky_time != b.sky_time)
        return false;
    return a.fog_density == b.fog_density && a.fog_tint == b.fog_tint &&
           a.sky_hour == b.sky_hour && a.sky_tint == b.sky_tint &&
           a.cloud_coverage == b.cloud_coverage && a.cloud_brightness == b.cloud_brightness &&
           a.cloud_saturation == b.cloud_saturation && a.cloud_size == b.cloud_size &&
           a.atmo_rayleigh == b.atmo_rayleigh && a.atmo_mie == b.atmo_mie &&
           a.atmo_brightness == b.atmo_brightness && a.atmo_contrast == b.atmo_contrast &&
           std::memcmp(a.fog_color, b.fog_color, sizeof(a.fog_color)) == 0 &&
           std::memcmp(a.sky_color, b.sky_color, sizeof(a.sky_color)) == 0;
}

// Android-сборка идёт с -ffast-math: isfinite недостоверен, проверяем биты
// (тот же приём, что в viewmodel.h).
inline bool finite(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
inline float clampf(float v, float lo, float hi, float fallback) {
    if (!finite(v)) return fallback;
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float wrap_hour(float v) {
    if (!finite(v)) return 12.f;
    v -= 24.f * (float)((int)(v / 24.f));   // 0..24 без знака
    if (v < 0.f) v += 24.f;
    return (v >= 24.f) ? 0.f : v;
}

inline Settings sanitize(const Settings& s) {
    Settings r = s;
    for (int i = 0; i < 3; ++i) {
        r.fog_color[i] = clampf(s.fog_color[i], 0.f, 1.f, 0.62f);
        r.sky_color[i] = clampf(s.sky_color[i], 0.f, 1.f, 0.55f);
    }
    r.fog_density  = clampf(s.fog_density, 0.f, 1.f, 0.35f);
    r.fog_tint     = clampf(s.fog_tint, 0.f, 1.f, 0.f);
    r.sky_hour     = wrap_hour(s.sky_hour);
    r.sky_tint     = clampf(s.sky_tint, 0.f, 1.f, 0.f);
    r.cloud_coverage   = clampf(s.cloud_coverage, 0.f, 1.f, 0.45f);
    r.cloud_brightness = clampf(s.cloud_brightness, 0.f, 2.f, 1.f);
    r.cloud_saturation = clampf(s.cloud_saturation, 0.f, 2.f, 1.f);
    r.cloud_size       = clampf(s.cloud_size, 0.5f, 5.f, 1.f);
    r.atmo_rayleigh    = clampf(s.atmo_rayleigh, 0.f, 4.f, 1.f);
    r.atmo_mie         = clampf(s.atmo_mie, 0.f, 4.f, 1.f);
    r.atmo_brightness  = clampf(s.atmo_brightness, 0.f, 2.f, 1.f);
    r.atmo_contrast    = clampf(s.atmo_contrast, 0.f, 2.f, 1.f);
    return r;
}

// ---- Трейлер конфига -------------------------------------------------------
// Отдельная Wire-структура: порядок полей в .cfg не должен зависеть от порядка
// полей Settings, а размер — меняться от добавления новых полей в середину.
// Трейлер пишется ПОСЛЕ CfgUiTrailer; читатели, которые до него не доходят,
// его игнорируют (как и трейлер UI).
struct Wire {
    std::uint32_t magic   = 0x314b5343u;   // "CSK1"
    std::uint32_t version = 1;
    std::uint32_t size    = sizeof(Wire);
    std::uint32_t flags   = 0;             // 1 туман, 2 небо, 4 своё время
    float v[16] = {};
};
static_assert(sizeof(Wire) == 80, "stable fog/sky trailer");

constexpr std::uint32_t kWireMagic = 0x314b5343u;
constexpr std::uint32_t kWireVersion = 1;
constexpr std::uint32_t kFlagFog = 1u, kFlagSky = 2u, kFlagSkyTime = 4u;

inline Wire encode(const Settings& s) {
    const Settings r = sanitize(s);
    Wire w;
    w.flags = (r.fog_on ? kFlagFog : 0u) | (r.sky_on ? kFlagSky : 0u) |
              (r.sky_time ? kFlagSkyTime : 0u);
    w.v[0] = r.fog_density;
    w.v[1] = r.fog_color[0]; w.v[2] = r.fog_color[1]; w.v[3] = r.fog_color[2];
    w.v[4] = r.fog_tint;
    w.v[5] = r.sky_hour;
    w.v[6] = r.sky_color[0]; w.v[7] = r.sky_color[1]; w.v[8] = r.sky_color[2];
    w.v[9] = r.sky_tint;
    w.v[10] = r.cloud_coverage;   w.v[11] = r.cloud_brightness;
    w.v[12] = r.cloud_saturation; w.v[13] = r.cloud_size;
    w.v[14] = r.atmo_rayleigh;    w.v[15] = r.atmo_mie;
    return w;
}

// Недостаток места (старый конфиг) или битый трейлер — выключенные дефолты,
// никогда не «состояние из прошлого запуска».
inline Settings decode(const void* bytes, std::size_t size) {
    Settings s;
    if (size != sizeof(Wire)) return sanitize(s);
    Wire w;
    std::memcpy(&w, bytes, sizeof(w));
    if (w.magic != kWireMagic || w.version != kWireVersion ||
        w.size != sizeof(Wire) || (w.flags & ~(kFlagFog | kFlagSky | kFlagSkyTime)))
        return sanitize(s);
    s.fog_on = (w.flags & kFlagFog) != 0;
    s.sky_on = (w.flags & kFlagSky) != 0;
    s.sky_time = (w.flags & kFlagSkyTime) != 0;
    s.fog_density = w.v[0];
    for (int i = 0; i < 3; ++i) { s.fog_color[i] = w.v[1 + i]; s.sky_color[i] = w.v[6 + i]; }
    s.fog_tint = w.v[4];
    s.sky_hour = w.v[5];
    s.sky_tint = w.v[9];
    s.cloud_coverage = w.v[10];  s.cloud_brightness = w.v[11];
    s.cloud_saturation = w.v[12]; s.cloud_size = w.v[13];
    s.atmo_rayleigh = w.v[14];   s.atmo_mie = w.v[15];
    // Остальное (яркость/контраст воздуха) в трейлере v1 не едет: у w.v[16]
    // места нет, а расширять Wire до версии 2 ради двух слайдеров пока не за
    // чем — значения остаются дефолтными.
    return sanitize(s);
}

// ---- Gradient: раскладка нативного буфера ----------------------------------
// Unity держит ключи Gradient-а в неуправляемой памяти (Gradient.m_Ptr, 0x10):
// до 8 цветовых ключей (ColorRGBA 0x10 + time) и 8 альфа-ключей (alpha+time),
// плюс 4 int. Точный порядок блоков и шаг ключа в разных версиях Unity
// отличались, поэтому раскладка НЕ хардкодится, а определяется по инвариантам
// живого буфера (probe). Инварианты: два int-а — mode (0..2) и colorSpace
// (0..1), два — количества ключей (2..8); времена ключей конечны, лежат в
// [0,1] и не убывают; каналы цвета/альфы в [0,1]. На реальном буфере проходит
// ровно один кандидат из таблицы — остальные не проходят по цветам/временам.
constexpr std::uint32_t kMaxKeys = 8;
constexpr std::uint32_t kBufMax  = 0x180;   // с запасом под самый крупный кандидат

struct Layout {
    std::uint32_t ints    = 0;   // блок 4 int (mode/colorSpace/кол-ва)
    std::uint32_t counts  = 0;   // смещение пары количеств ключей внутри int-блока
    std::uint32_t keys    = 0;   // первый цветовой ключ
    std::uint32_t stride  = 0;   // шаг цветового ключа (0x14/0x18/0x20)
    std::uint32_t n_color = 0;
    std::uint32_t n_alpha = 0;
    std::uint32_t bytes   = 0;   // сколько байт буфера покрывает раскладка
};

namespace detail {

inline std::uint32_t u32(const std::uint8_t* p) {
    std::uint32_t v; std::memcpy(&v, p, sizeof(v)); return v;
}
inline float f32(const std::uint8_t* p) {
    float v; std::memcpy(&v, p, sizeof(v)); return v;
}
inline bool in01(float v) { return finite(v) && v >= -0.002f && v <= 1.002f; }

inline bool fits(const std::uint8_t* buf, std::uint32_t len, const Layout& l) {
    if (l.n_color < 2 || l.n_color > kMaxKeys) return false;
    if (l.n_alpha < 2 || l.n_alpha > kMaxKeys) return false;
    const std::uint32_t alpha     = l.keys + kMaxKeys * l.stride;
    const std::uint32_t need      = alpha + kMaxKeys * 8u;
    const std::uint32_t ints_end  = l.ints + 16u;
    if (len < (need > ints_end ? need : ints_end)) return false;
    // Цветовые ключи: цвет (RGB+альфа) и время.
    float prev = -1.f;
    for (std::uint32_t k = 0; k < l.n_color; ++k) {
        const std::uint8_t* c = buf + l.keys + k * l.stride;
        for (int i = 0; i < 4; ++i) if (!in01(f32(c + 4 * i))) return false;
        const float t = f32(c + 0x10);
        if (!finite(t) || t < -0.002f || t > 1.002f || t < prev) return false;
        prev = t;
    }
    // Альфа-ключи: alpha и время.
    prev = -1.f;
    for (std::uint32_t k = 0; k < l.n_alpha; ++k) {
        const std::uint8_t* a = buf + alpha + k * 8u;
        if (!in01(f32(a)) || !in01(f32(a + 4))) return false;
        const float t = f32(a + 4);
        if (t < prev) return false;
        prev = t;
    }
    return true;
}

} // namespace detail

// Определяет раскладку буфера. Ложное срабатывание исключено инвариантами;
// на незнакомом (будущем) буфере вернёт false — писатель такой Gradient просто
// не трогает и ничего не портит.
inline bool probe(const std::uint8_t* buf, std::uint32_t len, Layout& out) {
    // {ints, keys, stride}: блок int стоит либо в начале (тогда ключи с 0x10),
    // либо в самом конце (тогда ключи с 0x00). Пересекающихся вариантов
    // (ключи с 0x00 и int-ы на 0x10) не бывает — блоки не накладываются.
    static const std::uint32_t kCand[][3] = {
        {0x000, 0x010, 0x14}, {0x000, 0x010, 0x18}, {0x000, 0x010, 0x20},
        {0x0e0, 0x000, 0x14}, {0x100, 0x000, 0x18}, {0x140, 0x000, 0x20},
    };
    for (const auto& c : kCand) {
        const std::uint8_t* ip = buf + c[0];
        const std::uint32_t a = detail::u32(ip), b = detail::u32(ip + 4);
        const std::uint32_t cc = detail::u32(ip + 8), d = detail::u32(ip + 12);
        // Порядок int-ов в разных версиях: (mode, space, nColor, nAlpha) или
        // (nColor, nAlpha, mode, space) — количества ищем по диапазону 2..8.
        std::uint32_t counts = 0, n_color = 0, n_alpha = 0;
        if (a <= 2 && b <= 2 && cc >= 2 && cc <= kMaxKeys && d >= 2 && d <= kMaxKeys) {
            counts = 8; n_color = cc; n_alpha = d;      // (mode, space, nColor, nAlpha)
        } else if (cc <= 2 && d <= 2 && a >= 2 && a <= kMaxKeys && b >= 2 && b <= kMaxKeys) {
            counts = 0; n_color = a; n_alpha = b;       // (nColor, nAlpha, mode, space)
        } else {
            continue;
        }
        Layout l;
        l.ints = c[0]; l.counts = counts; l.keys = c[1]; l.stride = c[2];
        l.n_color = n_color; l.n_alpha = n_alpha;
        const std::uint32_t alpha = l.keys + kMaxKeys * l.stride;
        const std::uint32_t need = alpha + kMaxKeys * 8u;
        const std::uint32_t ints_end = l.ints + 16u;
        l.bytes = need > ints_end ? need : ints_end;
        if (l.bytes > len) continue;
        if (!detail::fits(buf, len, l)) continue;
        out = l;
        return true;
    }
    return false;
}

// Красит ключи: color.rgb = lerp(исходный, target, strength). Исходник — копия
// оригинального буфера (game.cpp хранит её), поэтому повторное применение не
// накапливает цвет. Альфа и времена не трогаются.
inline void tint_keys(std::uint8_t* buf, const Layout& l, const float rgb[3], float strength) {
    const float t = clampf(strength, 0.f, 1.f, 0.f);
    if (t <= 0.f) return;
    for (std::uint32_t k = 0; k < l.n_color && k < kMaxKeys; ++k) {
        std::uint8_t* c = buf + l.keys + k * l.stride;
        for (int i = 0; i < 3; ++i) {
            const float orig = detail::f32(c + 4 * i);
            const float v = orig + (rgb[i] - orig) * t;
            std::memcpy(c + 4 * i, &v, sizeof(v));
        }
    }
}

} // namespace fog_sky
