// bw_obf.cpp — рантайм-часть обфускации.
//
// НИ ОДНОГО строкового литерала в этом файле. Он целиком живёт в .text,
// а данные (g_bw_meta, непрозрачные слова) — в .bwmeta/.bss. Шифрованные
// .rodata-строки нужны уже после этого конструктора.
//
// constructor(101) → .init_array.00101: первый пользовательский приоритет
// (0..100 зарезервированы), раньше всех обычных C++ static initializers,
// которые лежат в простой .init_array. .preinit_array в этом бинаре пустой
// (проверено на arm64-артефакте: обе записи 0/-1, лоадер их пропускает).
//
// ЗДЕСЬ НЕТ mprotect И dl_iterate_phdr. Оба были в первой версии и оба стали
// причиной SIGSEGV на устройстве:
//   * dlpi_addr для главного бинаря на bionic не гарантирован как load bias —
//     прошлая версия писала XOR по адресу вида 0x1d140 вместо base+0x1d140;
//   * mprotect(R→RW→R) на 16K-страничном ядре захватывал страницы соседнего
//     R E-сегмента (сегменты с p_align=0x1000 делят 16K-страницу), снимал с
//     них X и ронял исполнение. Плюс отказ mprotect игнорировался.
// Теперь билдер САМ выставляет PF_W строковым PT_LOAD (см. elf_crypt.py),
// лоадер мапит их RW, а конструктор просто пишет — без единого syscall.

#include "bw_obf.h"

#include <sys/auxv.h>
#include <unistd.h>

#include <cstdint>

// .bwmeta: magic(16) | count(8) | meta_vaddr(8) | flags(8) | ranges[]
// meta_vaddr — линковочный vaddr первого байта маркера. По нему конструктор
// считает load bias как (&g_bw_meta[0] - meta_vaddr): это собственный адрес
// страницы в памяти, а не то, что вернул лоадер в dlpi_addr.
// flags bit0 = все строковые сегменты уже RW в program headers.
static constexpr size_t kMetaCount = 16;
static constexpr size_t kMetaVaddr = 24;
static constexpr size_t kMetaFlags = 32;
static constexpr size_t kMetaHdr   = 40;
static constexpr size_t kMaxRanges = 1024;
static constexpr size_t kMetaSize  = kMetaHdr + kMaxRanges * 16;
static constexpr uint64_t kFlagSegmentsRW = 1;
// Образ целиком ~2.5 МБ. Всё, что выходит за 256 МБ — битая метадата.
static constexpr uint64_t kImageLimit = 0x10000000ULL;

// Магические 16 байт НЕ выносим в отдельный constexpr-массив: компилятор
// положил бы его копию в .rodata, и билдер нашёл бы два маркера вместо одного.
// Секция лежит в RW и НЕ шифруется.
__attribute__((used, section(".bwmeta"), aligned(8)))
volatile uint8_t g_bw_meta[kMetaSize] = {
    0xB7, 0x53, 0x1E, 0x9A, 0x44, 0xC0, 0xF1, 0x6D,
    0x2A, 0x88, 0x37, 0xDE, 0x90, 0x0B, 0x65, 0xEC,
    // счётчик-сентенел: непатченный бинарь (собран без build.sh) просто
    // выходит из конструктора, ничего не трогая.
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

#ifdef BW_OBF_DIAGNOSTIC
extern "C" {
volatile uint64_t bw_obf_diagnostic[8] = {};
}
#endif

namespace {

uint64_t rd64(const volatile uint8_t* p) noexcept {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | static_cast<uint64_t>(p[i]);
    return v;
}

uint64_t runtime_seed() noexcept {
    uint64_t s = static_cast<uint64_t>(getpid()) * 0x9E3779B97F4A7C15ULL;
    const unsigned char* rnd =
        reinterpret_cast<const unsigned char*>(getauxval(AT_RANDOM));
    if (rnd) {
        for (int i = 0; i < 8; ++i) s = (s << 8) ^ static_cast<uint64_t>(rnd[i]);
    }
    s ^= reinterpret_cast<uintptr_t>(&runtime_seed);
    return bw::mix64(s);
}

// This runs while .rodata is still encrypted. Clang's loop vectorizer can
// otherwise place shuffle/constants for the decryption loop in that same
// .rodata, so the decoder reads ciphertext while decoding it. Keep this early
// function scalar and independent of compiler-generated constant tables.
#if defined(__clang__)
__attribute__((constructor(101), noinline, optnone))
#elif defined(__GNUC__)
__attribute__((constructor(101), noinline, optimize("O0")))
#else
__attribute__((constructor(101)))
#endif
void bw_decrypt_all() noexcept {
#ifdef BW_OBF_DIAGNOSTIC
    bw_obf_diagnostic[0] = 1;
#endif
    const uint64_t seed = runtime_seed();
    bw::g_q  = seed;
    bw::g_ok = bw::mix64(seed);

    const volatile uint8_t* meta = g_bw_meta;
    const uint64_t count = rd64(meta + kMetaCount);
#ifdef BW_OBF_DIAGNOSTIC
    bw_obf_diagnostic[1] = count;
#endif
    if (count == 0 || count > kMaxRanges) return; // не пропатчено — выходим

    const uint64_t meta_vaddr = rd64(meta + kMetaVaddr);
    const uint64_t flags = rd64(meta + kMetaFlags);
#ifdef BW_OBF_DIAGNOSTIC
    bw_obf_diagnostic[2] = meta_vaddr;
    bw_obf_diagnostic[3] = flags;
#endif
    // Строки в read-only сегменте, который билдер не смог сделать RW: писать
    // туда нельзя. Молча выходим лучше, чем падать (билдер в этом случае
    // вообще не даёт собрать артефакт — см. elf_crypt.py).
    if (!meta_vaddr || meta_vaddr > kImageLimit) return;
    if ((flags & kFlagSegmentsRW) == 0) return;

    const uintptr_t base =
        reinterpret_cast<uintptr_t>(&g_bw_meta[0]) - static_cast<uintptr_t>(meta_vaddr);
#ifdef BW_OBF_DIAGNOSTIC
    bw_obf_diagnostic[4] = base;
#endif

    for (uint64_t i = 0; i < count; ++i) {
        const volatile uint8_t* rec = meta + kMetaHdr + static_cast<size_t>(i) * 16;
        const uint64_t va = rd64(rec);
        const uint64_t sz = rd64(rec + 8);
#ifdef BW_OBF_DIAGNOSTIC
        if (i == 0) { bw_obf_diagnostic[5] = va; bw_obf_diagnostic[6] = sz; }
#endif
        if (!va || va > kImageLimit || !sz || sz > kImageLimit) continue;
        const uintptr_t addr = base + static_cast<uintptr_t>(va);
        if (addr < 0x10000u || addr + sz < addr) continue;

        uint8_t* p = reinterpret_cast<uint8_t*>(addr);
        for (uint64_t j = 0; j < sz; ++j)
            p[j] ^= bw::stream_byte(va + j);
    }
#ifdef BW_OBF_DIAGNOSTIC
    bw_obf_diagnostic[7] = 1;
#endif
}

} // namespace
