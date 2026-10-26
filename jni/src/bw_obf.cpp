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

#include "bw_obf.h"

#include <elf.h>
#include <link.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>

namespace {

// Таблица диапазонов. Билдер (tools/obf/elf_crypt.py) находит эти 16 байт в
// файле и пишет сразу за ними: count (u64 LE), затем count пар
// (vaddr u64, size u64). Секция .bwmeta лежит в RW и НЕ шифруется.
constexpr size_t   kMaxRanges = 1024;
constexpr size_t   kMetaSize  = 24 + kMaxRanges * 16;

} // namespace

// Магические 16 байт НЕ выносим в отдельный constexpr-массив: компилятор
// положил бы его копию в .rodata, и билдер нашёл бы два маркера вместо одного.
__attribute__((used, section(".bwmeta"), aligned(8)))
volatile uint8_t g_bw_meta[kMetaSize] = {
    0xB7, 0x53, 0x1E, 0x9A, 0x44, 0xC0, 0xF1, 0x6D,
    0x2A, 0x88, 0x37, 0xDE, 0x90, 0x0B, 0x65, 0xEC,
    // счётчик-сентенел: непатченный бинарь (собран без build.sh) просто
    // выходит из конструктора, ничего не трогая.
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

namespace {

struct PhdrView {
    const ElfW(Phdr)* ph   = nullptr;
    ElfW(Half)         n   = 0;
    uintptr_t          bias = 0;
};

int phdr_cb(struct dl_phdr_info* info, size_t, void* out) noexcept {
    PhdrView* v = static_cast<PhdrView*>(out);
    v->ph   = info->dlpi_phdr;
    v->n    = static_cast<ElfW(Half)>(info->dlpi_phnum);
    v->bias = static_cast<uintptr_t>(info->dlpi_addr);
    return 1; // первый callback — главный исполняемый файл
}

int prot_of(const PhdrView& v, uintptr_t va) noexcept {
    for (ElfW(Half) i = 0; i < v.n; ++i) {
        const ElfW(Phdr)& p = v.ph[i];
        if (p.p_type != PT_LOAD) continue;
        const uintptr_t lo = v.bias + static_cast<uintptr_t>(p.p_vaddr);
        const uintptr_t hi = lo + static_cast<uintptr_t>(p.p_memsz);
        if (va < lo || va >= hi) continue;
        int r = 0;
        if (p.p_flags & PF_R) r |= PROT_READ;
        if (p.p_flags & PF_W) r |= PROT_WRITE;
        if (p.p_flags & PF_X) r |= PROT_EXEC;
        return r ? r : PROT_READ;
    }
    return PROT_READ | PROT_WRITE;
}

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

// mprotect нужен только если строки попали в read-only PT_LOAD (обычный
// случай: lld кладёт .rodata в R/RX-сегмент). Пишем через raw syscall, чтобы
// не светить импорт лишний раз. Страницы, в которых лежит ХОТЯ БЫ ОДИН байт
// .text, билдер из шифрования исключает — то есть во время RW-окна наш же
// код на этих страницах не исполняется.
__attribute__((constructor(101))) void bw_decrypt_all() noexcept {
    const uint64_t seed = runtime_seed();
    bw::g_q  = seed;
    bw::g_ok = bw::mix64(seed);

    const volatile uint8_t* meta = g_bw_meta;
    const uint64_t count = rd64(meta + 16);
    if (count == 0 || count > kMaxRanges) return; // не пропатчено — выходим

    PhdrView view{};
    dl_iterate_phdr(phdr_cb, &view);
    if (!view.ph) return;

    uintptr_t page = static_cast<uintptr_t>(getauxval(AT_PAGESZ));
    if (page < 4096 || (page & (page - 1)) != 0) page = 4096;

    for (uint64_t i = 0; i < count; ++i) {
        const volatile uint8_t* rec = meta + 24 + static_cast<size_t>(i) * 16;
        const uint64_t va = rd64(rec);
        const uint64_t sz = rd64(rec + 8);
        if (!va || !sz || sz > (1u << 28)) continue;

        uint8_t* p = reinterpret_cast<uint8_t*>(view.bias + static_cast<uintptr_t>(va));
        const uintptr_t lo = reinterpret_cast<uintptr_t>(p) & ~(page - 1);
        const uintptr_t hi = (reinterpret_cast<uintptr_t>(p) + static_cast<size_t>(sz)
                              + page - 1) & ~(page - 1);
        const int orig = prot_of(view, reinterpret_cast<uintptr_t>(p));
        const bool need_rw = (orig & PROT_WRITE) == 0;
        if (need_rw)
            (void)syscall(SYS_mprotect, reinterpret_cast<void*>(lo),
                          static_cast<size_t>(hi - lo), PROT_READ | PROT_WRITE);
        for (uint64_t j = 0; j < sz; ++j)
            p[j] ^= bw::stream_byte(va + j);
        if (need_rw)
            (void)syscall(SYS_mprotect, reinterpret_cast<void*>(lo),
                          static_cast<size_t>(hi - lo), orig);
    }
}

} // namespace
