// Хост-тест «своего тумана и неба»: чистые части fog_sky.h — clamp-и настроек,
// кодирование трейлера конфига и определение/покраска нативного буфера
// Gradient-а. Сборка/запуск (как у соседних тестов):
//   g++ -std=gnu++17 -Ijni/include -Ijni/include/ImGui jni/tests/test_fog_sky.cpp -o /tmp/t && /tmp/t
#include "fog_sky.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using fog_sky::Layout;

static void putf(std::vector<unsigned char>& b, size_t off, float v) {
    std::memcpy(&b[off], &v, sizeof(v));
}
static void putu(std::vector<unsigned char>& b, size_t off, unsigned int v) {
    std::memcpy(&b[off], &v, sizeof(v));
}
static float getf(const std::vector<unsigned char>& b, size_t off) {
    float v; std::memcpy(&v, &b[off], sizeof(v)); return v;
}
static unsigned int getu(const std::vector<unsigned char>& b, size_t off) {
    unsigned int v; std::memcpy(&v, &b[off], sizeof(v)); return v;
}

// Синтетический буфер Gradient-а: реальные раскладки отличаются порядком блоков
// (int-ы в начале или в конце), порядком int-ов (счётчики первыми или mode
// первым) и шагом цветового ключа. Логика обязана узнать любую из таблицы.
static std::vector<unsigned char> make_buffer(unsigned int ints, unsigned int keys,
                                              unsigned int stride, unsigned int n_color,
                                              unsigned int n_alpha, bool counts_first) {
    std::vector<unsigned char> buf(fog_sky::kBufMax, 0);
    const unsigned int alpha = keys + 8u * stride;
    const unsigned int need = alpha + 8u * 8u;
    const unsigned int ints_end = ints + 16u;
    const unsigned int bytes = need > ints_end ? need : ints_end;
    assert(bytes <= fog_sky::kBufMax);
    if (counts_first) {
        putu(buf, ints + 0, n_color); putu(buf, ints + 4, n_alpha);
        putu(buf, ints + 8, 0);       putu(buf, ints + 12, 0);       // mode, colorSpace
    } else {
        putu(buf, ints + 0, 0);       putu(buf, ints + 4, 0);
        putu(buf, ints + 8, n_color); putu(buf, ints + 12, n_alpha);
    }
    for (unsigned int k = 0; k < n_color; ++k) {
        const size_t c = keys + k * stride;
        putf(buf, c + 0, 0.10f + 0.01f * (float)k);
        putf(buf, c + 4, 0.20f + 0.01f * (float)k);
        putf(buf, c + 8, 0.30f + 0.01f * (float)k);
        putf(buf, c + 12, 1.f);
        putf(buf, c + 16, (float)k / (float)(n_color - 1));
    }
    for (unsigned int k = 0; k < n_alpha; ++k) {
        const size_t a = alpha + k * 8u;
        putf(buf, a + 0, 1.f);
        putf(buf, a + 4, (float)k / (float)(n_alpha - 1));
    }
    return buf;
}

static void test_sanitize() {
    fog_sky::Settings s;
    // Значения вне диапазонов поджимаются, NaN/Inf — к дефолтам (в сборке
    // -ffast-math isfinite недостоверен, проверка идёт по битам).
    s.sky_hour = 30.f;                 assert(fog_sky::sanitize(s).sky_hour == 6.f);
    s.sky_hour = -1.f;                 assert(fog_sky::sanitize(s).sky_hour == 23.f);
    s.sky_hour = 12.5f;                assert(fog_sky::sanitize(s).sky_hour == 12.5f);
    s.sky_hour = std::nanf("");        assert(fog_sky::sanitize(s).sky_hour == 12.f);
    s = fog_sky::Settings{};
    s.fog_density = 5.f;               assert(fog_sky::sanitize(s).fog_density == 1.f);
    s.fog_density = -5.f;              assert(fog_sky::sanitize(s).fog_density == 0.f);
    s.cloud_size = 100.f;              assert(fog_sky::sanitize(s).cloud_size == 5.f);
    s.atmo_rayleigh = -2.f;            assert(fog_sky::sanitize(s).atmo_rayleigh == 0.f);
    s.sky_color[0] = 3.f;              assert(fog_sky::sanitize(s).sky_color[0] == 1.f);
    s.sky_tint = 2.f;                  assert(fog_sky::sanitize(s).sky_tint == 1.f);
    s.fog_color[2] = std::numeric_limits<float>::infinity();
    assert(fog_sky::sanitize(s).fog_color[2] == 0.62f);
    // same() — сравнение как оно есть у меню: любое отличающееся поле заметно.
    fog_sky::Settings a, b;
    assert(fog_sky::same(a, b));
    b.sky_on = true;
    assert(!fog_sky::same(a, b));
}

static void test_wire() {
    fog_sky::Settings s;
    s.fog_on = true; s.sky_on = true; s.sky_time = true;
    s.fog_density = 0.75f; s.fog_tint = 0.4f;
    s.fog_color[0] = 0.11f; s.fog_color[1] = 0.22f; s.fog_color[2] = 0.33f;
    s.sky_hour = 20.5f; s.sky_tint = 0.6f;
    s.sky_color[0] = 0.9f; s.sky_color[1] = 0.5f; s.sky_color[2] = 0.1f;
    s.cloud_coverage = 0.8f; s.cloud_brightness = 1.5f; s.cloud_saturation = 0.3f;
    s.cloud_size = 2.5f; s.atmo_rayleigh = 3.5f; s.atmo_mie = 0.25f;

    const fog_sky::Wire w = fog_sky::encode(s);
    const fog_sky::Settings r = fog_sky::decode(&w, sizeof(w));
    assert(r.fog_on && r.sky_on && r.sky_time);
    assert(r.fog_density == 0.75f && r.fog_tint == 0.4f);
    assert(r.fog_color[0] == 0.11f && r.fog_color[2] == 0.33f);
    assert(r.sky_hour == 20.5f && r.sky_tint == 0.6f);
    assert(r.sky_color[1] == 0.5f);
    assert(r.cloud_coverage == 0.8f && r.cloud_brightness == 1.5f);
    assert(r.cloud_saturation == 0.3f && r.cloud_size == 2.5f);
    assert(r.atmo_rayleigh == 3.5f && r.atmo_mie == 0.25f);
    // Яркость/контраст воздуха в трейлер v1 не едут — остаются дефолтными.
    assert(r.atmo_brightness == 1.f && r.atmo_contrast == 1.f);

    // Битая/чужая память на входе — выключенные дефолты, никогда не «включено».
    fog_sky::Wire bad = w;
    bad.magic = 0u;
    assert(!fog_sky::decode(&bad, sizeof(bad)).fog_on);
    bad = w; bad.version = 2u;
    assert(!fog_sky::decode(&bad, sizeof(bad)).sky_on);
    bad = w; bad.size = sizeof(fog_sky::Wire) - 4u;
    assert(!fog_sky::decode(&bad, sizeof(bad)).sky_time);
    bad = w; bad.flags = 0x80000000u;
    assert(!fog_sky::decode(&bad, sizeof(bad)).fog_on);
    // Усечённый прицеп (конфиг старого формата) — тоже дефолты.
    assert(!fog_sky::decode(&w, sizeof(w) - 1u).fog_on);
    assert(!fog_sky::decode(&w, 0u).sky_on);
}

static void test_probe_and_tint() {
    // Шесть раскладок (int-ы в начале или в конце, три шага ключа) и оба
    // порядка int-ов (счётчики первыми или mode первым).
    const struct { unsigned int ints, keys, stride; bool counts_first; } cases[] = {
        {0x000, 0x010, 0x14, false}, {0x000, 0x010, 0x18, false}, {0x000, 0x010, 0x20, false},
        {0x0e0, 0x000, 0x14, false}, {0x100, 0x000, 0x18, false}, {0x140, 0x000, 0x20, false},
        {0x000, 0x010, 0x14, true},  {0x000, 0x010, 0x20, true},  {0x140, 0x000, 0x20, true},
    };
    for (const auto& c : cases) {
        const std::vector<unsigned char> buf = make_buffer(c.ints, c.keys, c.stride, 4, 3, c.counts_first);
        Layout l{};
        const bool ok = fog_sky::probe(buf.data(), (unsigned int)buf.size(), l);
        assert(ok);
        assert(l.ints == c.ints && l.keys == c.keys && l.stride == c.stride);
        assert(l.n_color == 4 && l.n_alpha == 3);

        std::vector<unsigned char> out = buf;
        const float rgb[3] = {0.25f, 0.50f, 0.75f};
        fog_sky::tint_keys(out.data(), l, rgb, 0.5f);
        for (unsigned int k = 0; k < l.n_color; ++k) {
            const size_t p = l.keys + k * l.stride;
            for (int i = 0; i < 3; ++i) {
                const float orig = getf(buf, p + 4 * i);
                const float want = orig + (rgb[i] - orig) * 0.5f;
                assert(getf(out, p + 4 * i) == want);
            }
            // Альфа ключа и время не тронуты.
            assert(getf(out, p + 12) == getf(buf, p + 12));
            assert(getf(out, p + 16) == getf(buf, p + 16));
        }
        // int-ы не тронуты (для int-ов в конце они лежат после ключей).
        assert(getu(out, l.ints + 0) == getu(buf, l.ints + 0));
        assert(getu(out, l.ints + 12) == getu(buf, l.ints + 12));
        // Повторная покраска от ОРИГИНАЛА не накапливает оттенок.
        std::vector<unsigned char> again = buf;
        fog_sky::tint_keys(again.data(), l, rgb, 0.5f);
        assert(std::memcmp(again.data(), out.data(), l.bytes) == 0);
        // Сила 0 — буфер не меняется вовсе.
        std::vector<unsigned char> zero = buf;
        fog_sky::tint_keys(zero.data(), l, rgb, 0.f);
        assert(std::memcmp(zero.data(), buf.data(), l.bytes) == 0);
    }

    // Нулевой буфер и мусор раскладкой не признаются.
    std::vector<unsigned char> zeros(fog_sky::kBufMax, 0);
    Layout l{};
    assert(!fog_sky::probe(zeros.data(), (unsigned int)zeros.size(), l));
    std::vector<unsigned char> junk(fog_sky::kBufMax, 0x7f);
    assert(!fog_sky::probe(junk.data(), (unsigned int)junk.size(), l));
    // Ключи в порядке, но цвета вне [0,1] — раскладка не признаётся (защита от
    // ложного срабатывания на чужой структуре).
    std::vector<unsigned char> wild = make_buffer(0x010, 0x000, 0x14, 3, 3, false);
    putf(wild, 0x010 + 0, 9.f);
    assert(!fog_sky::probe(wild.data(), (unsigned int)wild.size(), l));
    // Счётчики ключей вне 2..8 — тоже отказ.
    std::vector<unsigned char> bad_counts = make_buffer(0x010, 0x000, 0x14, 2, 2, false);
    putu(bad_counts, 0x010 + 8, 9u);
    assert(!fog_sky::probe(bad_counts.data(), (unsigned int)bad_counts.size(), l));
    // Буфер короче раскладки — отказ, а не чтение за границей.
    const std::vector<unsigned char> small = make_buffer(0x140, 0x000, 0x20, 2, 2, false);
    assert(!fog_sky::probe(small.data(), 0x40, l));
}

int main() {
    test_sanitize();
    test_wire();
    test_probe_and_tint();
    puts("ok");
    return 0;
}
