#pragma once
// bw_obf.h — публичная часть обфускации.
//
// Две независимые вещи:
//   1) keystream — общий для C++ и tools/obf/elf_crypt.py. Билдер шифрует
//      строковые .rodata-секции ЭТИМ потоком, ранний init_array-конструктор
//      в bw_obf.cpp снимает шифровку тем же потоком до первого конструктора
//      игры/оверлея. Ключ — случайный на каждую сборку, bw_build_key.h
//      генерируется build.sh (фоллбэк ниже нужен только для хостовых тестов).
//   2) opaque() / BW_GUARD() / BW_CONST() — ветвления и константы, которые
//      компилятор не может свернуть: предикат читает ДВА volatile-слова,
//      связь между ними устанавливается в рантайме. Ложная ветка не пустая
//      (decoy), поэтому -O3/LTO её не выкидывает.
//
// Важно: bw_obf.cpp НЕ содержит ни одного строкового литерала, иначе
// конструктор прочитал бы ещё зашифрованную строку.

#include <cstddef>
#include <cstdint>

#if __has_include("bw_build_key.h")
#include "bw_build_key.h"
#else
// Хостовая сборка без build.sh: ключ фиксированный, шифрования бинаря нет.
#define BW_BUILD_KEY 0x6A09E667F3BCC909ULL
#endif

namespace bw {

constexpr uint64_t kKey = static_cast<uint64_t>(BW_BUILD_KEY);

// splitmix64. Один байт потока на каждый байт адреса: одинаковые строки,
// лежащие по разным адресам, шифруются по-разному — по всему .rodata нет
// повторов, по которым восстанавливают XOR-ключ.
constexpr uint64_t mix64(uint64_t x) noexcept {
    uint64_t z = x + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline uint8_t stream_byte(uint64_t vaddr) noexcept {
    const uint64_t seed = vaddr ^ kKey ^ ((kKey << 17) | (kKey >> 47));
    return static_cast<uint8_t>(mix64(seed) & 0xFFu);
}

// ---- непрозрачный предикат ------------------------------------------------
// Инвариант: g_ok == mix64(g_q). Он выполняется и в начальных значениях, и
// после bw_decrypt_all(). Компилятор видит два независимых volatile-чтения и
// не может доказать связь между ними.
//
// Всё inline: заголовок подключается и хостовыми тестами, у которых нет
// bw_obf.cpp в линковке. C++17 inline-переменные дают ровно один объект.
constexpr uint64_t kInitQ     = mix64(kKey ^ 0x2545F4914F6CDD1DULL);
constexpr uint64_t kConstSeed = mix64(kKey ^ 0x5DEECE66DULL);

inline volatile uint64_t g_q     = kInitQ;
inline volatile uint64_t g_ok    = mix64(kInitQ);
inline volatile uint64_t g_sink  = 0;
inline volatile uint64_t g_cseed = kConstSeed;

__attribute__((noinline)) inline void decoy(uint64_t seed) noexcept {
    volatile uint64_t x = seed ^ 0xA5A5A5A5A5A5A5A5ULL;
    for (int i = 0; i < 5; ++i)
        x = mix64(x + static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL);
    g_sink = x;
}

inline bool opaque() noexcept { return (mix64(g_q) ^ g_ok) == 0u; }

// Значение, которое в рантайме ВСЕГДА ноль, но компилятору это неизвестно.
// Используется для BW_CONST, чтобы зашифрованный литерал не склеился с нулём.
inline uint64_t opaque_zero() noexcept { return mix64(g_q) ^ g_ok; }

// V не лежит в .text immediates: в коде есть только E = V ^ K, а K считается
// рантаймом из volatile g_cseed. K одинаков в билдере и в бинаре (kConstSeed).
template <uint64_t V, uint64_t C>
struct EncConst {
    static constexpr uint64_t kE = V ^ mix64(kConstSeed ^ C);
    static inline uint64_t get() noexcept {
        return kE ^ mix64(g_cseed ^ C) ^ opaque_zero();
    }
};

} // namespace bw

// Ставится в точках, где логику стоит размазать: сравнение ключа, резолв
// оффсетов, ABI драйвера, запись конфига, установка iptables. Цена — два
// volatile-чтения и один предсказуемый переход.
#define BW_GUARD() do { if (!::bw::opaque()) ::bw::decoy(static_cast<uint64_t>(__COUNTER__)); } while (0)

// Восстановить константу рантаймом из зашифрованного литерала. Только для
// обычного кода: в constexpr / static_assert / размере массива не годится.
#define BW_CONST(v) (static_cast<decltype(v)>( \
    ::bw::EncConst<static_cast<uint64_t>(v), static_cast<uint64_t>(__COUNTER__)>::get()))
