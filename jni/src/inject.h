// inject.h — скрытый one-shot инжект payload в процесс игры (arm64).
//
// Задача: попасть кодом в игру, не оставив НИ ОДНОГО классического следа
// инжекта. Что видят проверки и чего здесь НЕТ:
//
//   /proc/<игра>/maps        — новых именованных отображений нет (никаких
//                              «наших» .so); payload лежит либо в нулевом
//                              хвосте существующего R-X сегмента libil2cpp,
//                              либо в готовой anon-регионе rwxp (JIT-пул).
//   soinfo/dl_iterate_phdr   — dlopen не зовём вовсе, в solist грузиться
//                              нечем.
//   TracerPid                — ptrace не используется ни на одном шаге;
//                              все записи хост делает выбранным транспортом
//                              (FT / RT-Dev / RT-Hook / procfs), как и весь
//                              остальной чит (KERNEL.md §2).
//   /proc/<игра>/fd          — файл payload открывается только в процессе
//                              чита и сразу закрывается; в игре fd не
//                              появляется.
//   новые потоки             — payload исполняется НА ПОТОКЕ ИГРЫ: вход —
//                              разовый увод GOT-слота часто вызываемой
//                              функции (по умолчанию strlen) на наш код;
//                              слот возвращается на место сразу же, вызов
//                              доходит до оригинала (см. контракт payload).
//
// Как это работает (путь «текст», если нет anon-rwxp региона):
//
//   1. в нулевом хвосте R-X сегмента libil2cpp ищется кэв ≥ 0x100;
//   2. туда пишется стаб (arm64, слова ниже собраны keystone и сверены
//      capstone: сохранить x0-x8/x16-x18/x29-x30 → mmap(R|X, anon, 1 МБ) →
//      базу в scratch → ограниченное ожидание флага → восстановить регистры
//      → br на payload; без payload или по таймауту — br в оригинал);
//   3. GOT-слот один раз указывает на стаб и СРАЗУ возвращается на место:
//      следующий вызов strlen в игре уходит в стаб ровно один раз;
//   4. хост ждёт базу от стаба, пишет payload+заголовок в свежий R-X регион
//      (записью транспорта, которой page protection не мешает), ставит
//      флаг go — стаб прыгает в payload;
//   5. payload отрабатывает (см. контракт), машинка продолжает жить как ни в
//      чём не бывало; стаб затирается нулями.
//
// Путь «rwx» (если в карте есть anon-регион rwxp): стаб не нужен — payload
// пишется прямо в нулевой хвост того региона, GOT-слот один раз указывает на
// entry. Текст libil2cpp не модифицируется вовсе (не боится CRC по .text).
//
// Контракт payload (blob: [0x40 заголовок][код], entry = +0x40):
//   +0x00 u64 magic    0x494a5055 ("IJPU")
//   +0x08 u64 orig_fn  адрес оригинальной функции из GOT-слота
//   +0x10 u64 flags    1 = старт; payload ставит 2 атомарным обменом
//   +0x20 u64 patch_addr / u32 patch_value @+0x28 / u32 patch_en @+0x2c
//   Вход: x0-x7 — аргументы перехваченного вызова, LR — живой вызывающий
//   игры. Payload ОБЯЗАН: защититься от повторного входа (атомарный обмен
//   flags 1→2), сделать своё дело и br в orig_fn с исходными регистрами.
//   Образец — payload/inject_demo.c.
//
// Готовый payload строится так: payload/build_payload.sh (нужен NDK).
// Хост-тест логики (разбор ELF/GOT, кэвы, оркестрация) — jni/tests/test_inject.cpp.
//
// Футган по выбору места: текстовый кэв ловится CRC-чекером по .text, если
// игра такой ведёт; rwx-регион игры (JIT-пул) — риск наткнуться на аллокацию
// JIT в том же нуле. По умолчанию rwx предпочтительнее.

#pragma once

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "mem_io.h"
#include "maps_lookup.h"

namespace inject {

// ---- константы раскладки (общие с payload/link.ld и payload/inject_demo.c) --
constexpr size_t   kStubBytes     = 0x100;  // стаб целиком: 0xB8 кода + scratch на 0xC0
constexpr size_t   kStubScratch   = 0xC0;   // данные стаба внутри его образа
constexpr size_t   kPayloadHdr    = 0x40;   // заголовок payload, entry = +0x40
constexpr uint64_t kMagic         = 0x494a5055ULL;               // "IJPU"
constexpr uint64_t kFlagGo        = 1;
constexpr uint64_t kFlagDone      = 2;
constexpr uint64_t kFlagMissed    = 0xFFFF0000ULL;               // u32 в scratch
constexpr size_t   kMmapSize      = 0x100000;                    // стаб просит 1 МБ
constexpr size_t   kMaxPayload    = 0xF0000;                     // запас в 1 МБ регионе

// Офсеты в scratch стаба (kStubScratch + ...).
constexpr size_t kScrBase  = 0x00;   // u64: база mmap (пишет стаб; <0 — errno)
constexpr size_t kScrFlags = 0x08;   // u32: 1 = go, 0xFFFF0000 = не дождались
constexpr size_t kScrEntry = 0x10;   // u64: адрес payload entry
constexpr size_t kScrOrig  = 0x18;   // u64: оригинал GOT-слота (диагностика)

// ---- стаб: 46 инструкций arm64 ---------------------------------------------
//
// Собрано keystone, сверено capstone. Раскладка фиксирована: код 0x00..0xB8,
// scratch — на 0xC0. Адреса целей ветвлений зашиты в словах:
//   0x3c adr x16,0xC0   0x4c cbz x15,0x84 (missed)   0x54 cbz w17,0x48 (spin)
//   0x5c cbz x17,0xB0 (orig)   0x7c adr x16,0xC0     0xac adr x16,0xC0
inline constexpr uint32_t kStubWords[46] = {
    0xa9bf7bfd,  // 0x00 stp x29, x30, [sp, #-16]!
    0xa9bf4be8,  // 0x04 stp x8, x18,  [sp, #-16]!
    0xa9bf1fe6,  // 0x08 stp x6, x7,   [sp, #-16]!
    0xa9bf17e4,  // 0x0c stp x4, x5,   [sp, #-16]!
    0xa9bf0fe2,  // 0x10 stp x2, x3,   [sp, #-16]!
    0xa9bf07e0,  // 0x14 stp x0, x1,   [sp, #-16]!
    0xa9bf47f0,  // 0x18 stp x16, x17, [sp, #-16]!
    0xd2801bc8,  // 0x1c mov x8, #222        (__NR_mmap)
    0xaa1f03e0,  // 0x20 mov x0, xzr         (addr)
    0xd2a00201,  // 0x24 mov x1, #0x100000   (len = 1 МБ)
    0xd28000a2,  // 0x28 mov x2, #5          (PROT_READ|PROT_EXEC)
    0xd2800443,  // 0x2c mov x3, #0x22       (MAP_PRIVATE|MAP_ANONYMOUS)
    0x92800004,  // 0x30 mov x4, #-1         (fd)
    0xaa1f03e5,  // 0x34 mov x5, xzr         (offset)
    0xd4000001,  // 0x38 svc #0
    0x10000430,  // 0x3c adr x16, 0xC0       (scratch)
    0xf9000200,  // 0x40 str x0, [x16]       (база mmap в scratch)
    0xd2a1000f,  // 0x44 mov x15, #0x08000000 (счётчик спина ~1 с)
    0xd10005ef,  // 0x48 sub x15, x15, #1     <- spin
    0xb40001cf,  // 0x4c cbz x15, 0x84        (сдаться -> missed)
    0xb9400a11,  // 0x50 ldr w17, [x16, #8]   (flags)
    0x34ffffb1,  // 0x54 cbz w17, 0x48        (spin)
    0xf9400a11,  // 0x58 ldr x17, [x16, #16]  (entry)
    0xb40002b1,  // 0x5c cbz x17, 0xB0        (нет payload -> orig)
    0xa8c147f0,  // 0x60 ldp x16, x17, [sp], #16
    0xa8c107e0,  // 0x64 ldp x0, x1,   [sp], #16
    0xa8c10fe2,  // 0x68 ldp x2, x3,   [sp], #16
    0xa8c117e4,  // 0x6c ldp x4, x5,   [sp], #16
    0xa8c11fe6,  // 0x70 ldp x6, x7,   [sp], #16
    0xa8c14be8,  // 0x74 ldp x8, x18,  [sp], #16
    0xa8c17bfd,  // 0x78 ldp x29, x30, [sp], #16
    0x10000230,  // 0x7c adr x16, 0xC0
    0xf9400a11,  // 0x80 ldr x17, [x16, #16]  (entry)
    0xd61f0220,  // 0x84 br x17               <- missed тоже приходит сюда
    0x52bffff1,  // 0x88 mov w17, #0xFFFF0000
    0xb9000a11,  // 0x8c str w17, [x16, #8]   (flags = kFlagMissed)
    0xa8c147f0,  // 0x90 ldp x16, x17, [sp], #16
    0xa8c107e0,  // 0x94 ldp x0, x1,   [sp], #16
    0xa8c10fe2,  // 0x98 ldp x2, x3,   [sp], #16
    0xa8c117e4,  // 0x9c ldp x4, x5,   [sp], #16
    0xa8c11fe6,  // 0xa0 ldp x6, x7,   [sp], #16
    0xa8c14be8,  // 0xa4 ldp x8, x18,  [sp], #16
    0xa8c17bfd,  // 0xa8 ldp x29, x30, [sp], #16
    0x100000b0,  // 0xac adr x16, 0xC0
    0xf9400e11,  // 0xb0 ldr x17, [x16, #24]  (orig_fn)
    0xd61f0220,  // 0xb4 br x17               <- orig
};

// Полный образ стаба (код + нулевой scratch). Статик-ассерты прикалывают
// ключевые слова к их адресам — при правке kStubWords сборка упадёт здесь.
inline void build_stub(uint8_t out[kStubBytes]) {
    static_assert(kStubWords[0x38 / 4] == 0xd4000001, "svc #0 сдвинулся");
    static_assert(kStubWords[0x3c / 4] == 0x10000430, "adr x16,scratch@0xC0 сдвинулся");
    static_assert(kStubWords[0x4c / 4] == 0xb40001cf, "cbz x15,missed@0x84 сдвинулся");
    static_assert(kStubWords[0x54 / 4] == 0x34ffffb1, "cbz w17,spin@0x48 сдвинулся");
    static_assert(kStubWords[0x5c / 4] == 0xb40002b1, "cbz x17,orig@0xB0 сдвинулся");
    static_assert(kStubWords[0xac / 4] == 0x100000b0, "adr x16,scratch@0xC0 (2) сдвинулся");
    static_assert(kStubWords[0xb0 / 4] == 0xf9400e11, "ldr x17,[x16,#0x18] сдвинулся");
    static_assert(kStubWords[0xb4 / 4] == 0xd61f0220, "br x17 (orig) сдвинулся");
    memset(out, 0, kStubBytes);
    memcpy(out, kStubWords, sizeof(kStubWords));
}

// ---- карта процессов --------------------------------------------------------

struct Region {
    uint64_t    start = 0, end = 0, file_off = 0;
    char        perms[5] = {};
    std::string path;
};

inline bool parse_regions(const std::string& text, std::vector<Region>* out) {
    if (!out) return false;
    out->clear();
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol = text.find('\n', pos);
        const size_t line_end = (eol == std::string::npos) ? text.size() : eol;
        std::string line = text.substr(pos, line_end - pos);
        pos = line_end + 1;

        Region r;
        {
            unsigned long long s = 0, e = 0, off = 0;
            char p[5] = {};
            if (sscanf(line.c_str(), "%llx-%llx %4s %llx", &s, &e, p, &off) != 4) continue;
            r.start = s; r.end = e; r.file_off = off;
            memcpy(r.perms, p, sizeof(r.perms));
        }
        const size_t slash = line.find('/');
        if (slash != std::string::npos) {
            std::string path = line.substr(slash);
            while (!path.empty() && (path.back() == '\r' || path.back() == ' '))
                path.pop_back();
            static const char kDeleted[] = " (deleted)";
            const size_t dl = sizeof(kDeleted) - 1;
            if (path.size() > dl && path.compare(path.size() - dl, dl, kDeleted) == 0)
                path.resize(path.size() - dl);
            r.path.swap(path);
        }
        out->push_back(r);
    }
    return !out->empty();
}

inline bool read_maps(pid_t pid, std::string* out) {
    if (pid <= 0 || !out) return false;
    char path[40];
    snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);   // карта, не память — можно всегда
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    out->clear();
    char buf[0x2000];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) out->append(buf, (size_t)n);
    const bool ok = (n == 0 && !out->empty());
    close(fd);
    return ok;
}

// ---- разбор ELF-образа в чужой памяти ---------------------------------------

struct Image {
    uint64_t exec_va = 0, exec_filesz = 0;   // наибольший R-X PT_LOAD
    uint64_t dyn_va = 0, dyn_filesz = 0;     // PT_DYNAMIC
};

inline bool parse_image(memio::Reader& m, uint64_t base, Image* out) {
    if (!base || !out) return false;
    Elf64_Ehdr eh;
    if (!m.read_quiet(base, &eh, sizeof(eh))) return false;
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) return false;
    if (eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_ident[EI_DATA] != ELFDATA2LSB) return false;
    if (eh.e_phentsize != sizeof(Elf64_Phdr) || !eh.e_phnum || eh.e_phnum > 32) return false;

    Elf64_Phdr ph[32];
    if (!m.read_quiet(base + eh.e_phoff, ph, sizeof(Elf64_Phdr) * eh.e_phnum)) return false;

    for (unsigned i = 0; i < eh.e_phnum; ++i) {
        const Elf64_Phdr& p = ph[i];
        if (p.p_type == PT_LOAD && (p.p_flags & PF_X) && p.p_filesz > out->exec_filesz) {
            out->exec_va = base + p.p_vaddr;
            out->exec_filesz = p.p_filesz;
        }
        if (p.p_type == PT_DYNAMIC && !out->dyn_va) {
            out->dyn_va = base + p.p_vaddr;
            out->dyn_filesz = p.p_filesz;
        }
    }
    return out->exec_va && out->dyn_va;
}

// GOT-слот символа: JUMP_SLOT из .rela.plt, иначе GLOB_DAT из .rela.dyn.
// bionic связывает eagerly, поэтому текущее значение слота — уже адрес
// оригинальной функции, его и возвращаем как cur_value.
inline bool find_got_slot(memio::Reader& m, uint64_t base, const char* symbol,
                          uint64_t* slot_va, uint64_t* cur_value) {
    Image img;
    if (!symbol || !symbol[0] || !parse_image(m, base, &img)) return false;

    uint64_t strtab = 0, symtab = 0, jmprel = 0, rela = 0;
    uint64_t strsz = 0, pltrelsz = 0, relasz = 0;
    {
        uint8_t buf[16 * 512];
        const uint64_t total = img.dyn_filesz;
        uint64_t off = 0;
        while (off < total) {
            const uint64_t chunk = (total - off) < sizeof(buf) ? (total - off) : sizeof(buf);
            if (!m.read_quiet(img.dyn_va + off, buf, (size_t)chunk)) return false;
            for (size_t i = 0; i + sizeof(Elf64_Dyn) <= chunk; i += sizeof(Elf64_Dyn)) {
                Elf64_Dyn d;
                memcpy(&d, buf + i, sizeof(d));
                switch (d.d_tag) {
                    case DT_STRTAB:   strtab = d.d_un.d_ptr; break;
                    case DT_SYMTAB:   symtab = d.d_un.d_ptr; break;
                    case DT_STRSZ:    strsz = d.d_un.d_val; break;
                    case DT_JMPREL:   jmprel = d.d_un.d_ptr; break;
                    case DT_PLTRELSZ: pltrelsz = d.d_un.d_val; break;
                    case DT_RELA:     rela = d.d_un.d_ptr; break;
                    case DT_RELASZ:   relasz = d.d_un.d_val; break;
                    default: break;
                }
            }
            off += chunk;
        }
    }
    if (!strtab || !symtab) return false;
    strtab += base; symtab += base;
    if (jmprel) jmprel += base;
    if (rela)   rela += base;

    // Имя символа из dynsym по индексу.
    auto name_of = [&](uint32_t idx, char* out, size_t cap) -> bool {
        if (!strsz || cap == 0) return false;
        // Грубая страховка от мусорного индекса: symtab лежит до strtab.
        if (symtab < strtab && (uint64_t)idx * sizeof(Elf64_Sym) >= strtab - symtab) return false;
        Elf64_Sym sym;
        if (!m.read_quiet(symtab + (uint64_t)idx * sizeof(Elf64_Sym), &sym, sizeof(sym)))
            return false;
        if (sym.st_name >= strsz) return false;
        const uint64_t va = strtab + sym.st_name;
        const size_t take = (cap - 1) < 96 ? (cap - 1) : 96;
        if (!m.read_quiet(va, out, take)) return false;
        out[take] = 0;
        return true;
    };

    auto scan = [&](uint64_t va, uint64_t size, uint64_t want_type) -> bool {
        if (!va || !size || size > 0x100000) return false;
        std::vector<uint8_t> buf((size_t)size);
        if (!m.read_quiet(va, buf.data(), buf.size())) return false;
        for (size_t off = 0; off + sizeof(Elf64_Rela) <= buf.size(); off += sizeof(Elf64_Rela)) {
            Elf64_Rela r;
            memcpy(&r, buf.data() + off, sizeof(r));
            if ((r.r_info & 0xffffffffULL) != want_type) continue;
            char name[128];
            if (!name_of((uint32_t)(r.r_info >> 32), name, sizeof(name))) continue;
            if (strcmp(name, symbol) != 0) continue;
            *slot_va = base + r.r_offset;
            return m.read_quiet(*slot_va, cur_value, sizeof(*cur_value));
        }
        return false;
    };

    // Сначала JUMP_SLOT (вызовы через PLT), затем GLOB_DAT (взятие адреса).
    if (jmprel && pltrelsz && scan(jmprel, pltrelsz, R_AARCH64_JUMP_SLOT)) return true;
    if (rela && relasz && scan(rela, relasz, R_AARCH64_GLOB_DAT)) return true;
    return false;
}

// ---- кэвы -------------------------------------------------------------------

// Ищет run из нулей длиной >= need в [va, va+span), читая хвостами по 2 КБ.
// Предпочитает самый поздний (ближе к концу сегмента) — там меньше шансов
// встретить чужие данные. Возвращает выровненный на 16 байт адрес.
inline bool find_zero_cave(memio::Reader& m, uint64_t va, uint64_t span,
                           size_t need, uint64_t* cave_va) {
    if (!va || !span || need < 16 || span > 0x10000000) return false;

    uint8_t buf[0x800];
    uint64_t cur_start = 0, cur_len = 0, best = 0;
    uint64_t off = 0;
    while (off < span) {
        const uint64_t chunk = (span - off) < sizeof(buf) ? (span - off) : sizeof(buf);
        if (!m.read_quiet(va + off, buf, (size_t)chunk)) break;
        for (uint64_t i = 0; i < chunk; ++i) {
            if (buf[i] == 0) {
                if (!cur_len) cur_start = va + off + i;
                ++cur_len;
                if (cur_len >= (uint64_t)need) best = cur_start;
            } else {
                cur_len = 0;
            }
        }
        off += chunk;
    }
    if (!best) return false;
    best = (best + 15) & ~(uint64_t)15;
    // После выравнивания хвост всё ещё обязан помещаться.
    if (best + (uint64_t)need > va + span) return false;
    *cave_va = best;
    return true;
}

// ---- оркестрация -------------------------------------------------------------

enum Status {
    kOk = 0,
    kErrPayload = 1,   // blob плохой
    kErrNoImage = 2,   // нет libil2cpp/ELF по базе
    kErrNoSite  = 3,   // нет кэва или GOT-слота
    kErrTimeout = 4,   // payload не исполнился за wait_ms
    kErrMmap    = 5,   // mmap стаба в игре вернул -errno (detail_errno)
    kErrWrite   = 6,   // транспорт не смог писать
};

enum Site { kSiteAuto = 0, kSiteRwx = 1, kSiteText = 2 };

struct Options {
    const char* got_symbol = "strlen";
    const uint8_t* payload = nullptr;    // blob: [0x40 header][код]
    size_t payload_size = 0;
    uint64_t il2cpp_base = 0;            // 0 — берём из карты по il2cpp_name
    const char* il2cpp_name = "libil2cpp.so";
    std::string maps_text;               // можно подать готовый текст карты
    uint64_t patch_addr = 0;             // демо-поля заголовка (см. шапку)
    uint32_t patch_value = 0;
    int wait_ms = 3000;                  // должен перекрывать спин стаба (~1 с)
    bool wipe = true;
    Site site = kSiteAuto;
};

struct Result {
    Status   status = kErrNoSite;
    uint64_t cave = 0;        // куда легла запись (стаб или payload)
    uint64_t payload_va = 0;  // где payload в итоге живёт
    uint64_t slot = 0;        // GOT-слот
    uint64_t orig_fn = 0;     // что было в слоте
    bool     rwx = false;     // путь rwx (без записи в текст)
    int      detail_errno = 0;
};

inline double now_s() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

inline void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, nullptr);
}

inline bool poll_done(memio::Reader& m, uint64_t flags_va, double deadline) {
    while (now_s() < deadline) {
        uint64_t flags = 0;
        if (m.read_quiet(flags_va, &flags, sizeof(flags)) && flags == kFlagDone) return true;
        sleep_ms(2);
    }
    return false;
}

inline Result run(memio::Reader& m, const Options& o) {
    Result res;

    // ---- payload: размер ------------------------------------------------------
    if (!o.payload || o.payload_size < kPayloadHdr + 16 || o.payload_size > kMaxPayload) {
        res.status = kErrPayload;
        return res;
    }

    // ---- карта, база il2cpp ---------------------------------------------------
    std::string maps_text = o.maps_text;
    if (maps_text.empty() && !read_maps(m.pid(), &maps_text)) {
        res.status = kErrNoImage;
        return res;
    }
    uint64_t base = o.il2cpp_base;
    if (!base) base = maps::lookup_library_base(maps_text, o.il2cpp_name);
    if (!base) {
        res.status = kErrNoImage;
        return res;
    }

    Image img;
    if (!parse_image(m, base, &img)) {
        res.status = kErrNoImage;
        return res;
    }

    // ---- GOT-слот и оригинал ----------------------------------------------------
    std::vector<Region> regions;
    parse_regions(maps_text, &regions);
    uint64_t slot = 0, orig = 0;
    if (!find_got_slot(m, base, o.got_symbol, &slot, &orig) || !orig) {
        res.status = kErrNoSite;
        return res;
    }
    // Оригинал обязан лежать в исполняемом регионе карты — иначе выловили не тот слот.
    bool orig_exec = false;
    for (const Region& r : regions) {
        if (r.perms[2] == 'x' && orig >= r.start && orig < r.end) { orig_exec = true; break; }
    }
    if (!orig_exec) {
        res.status = kErrNoSite;
        return res;
    }
    res.slot = slot;
    res.orig_fn = orig;

    // ---- заголовок payload ------------------------------------------------------
    // Буфер кадра: [0x40 header][код payload]. Пишется одним m.write.
    std::vector<uint8_t> frame(o.payload, o.payload + o.payload_size);
    {
        uint64_t v;
        v = kMagic;       memcpy(&frame[0x00], &v, 8);
        v = orig;         memcpy(&frame[0x08], &v, 8);
        v = 0;            memcpy(&frame[0x10], &v, 8);   // flags: 0 до go
        v = o.patch_addr; memcpy(&frame[0x20], &v, 8);
        uint32_t w = o.patch_value;  memcpy(&frame[0x28], &w, 4);
        w = o.patch_addr ? 1u : 0u;  memcpy(&frame[0x2c], &w, 4);
    }

    // ---- выбор места --------------------------------------------------------------
    const size_t need_frame = kPayloadHdr + o.payload_size;

    auto try_rwx = [&](uint64_t* cave) -> bool {
        if (o.site == kSiteText) return false;
        // anon rwxp, с запасом после выравнивания; именованные не трогаем
        for (const Region& r : regions) {
            if (r.perms[0] != 'r' || r.perms[1] != 'w' || r.perms[2] != 'x') continue;
            if (!r.path.empty()) continue;
            if (r.end - r.start < (uint64_t)need_frame + 0x100) continue;
            uint64_t c = 0;
            if (find_zero_cave(m, r.start, r.end - r.start, need_frame, &c)) { *cave = c; return true; }
        }
        return false;
    };
    auto try_text = [&](uint64_t* cave) -> bool {
        if (o.site == kSiteRwx) return false;
        uint64_t c = 0;
        if (find_zero_cave(m, img.exec_va, img.exec_filesz, kStubBytes, &c)) { *cave = c; return true; }
        return false;
    };

    auto restore_slot = [&]() {
        if (!m.write(slot, &orig, sizeof(orig))) res.status = kErrWrite;
    };
    auto wipe = [&](uint64_t va, size_t len) {
        if (!o.wipe) return;
        static uint8_t zeros[0x800] = {};
        size_t done = 0;
        while (done < len) {
            const size_t chunk = (len - done) < sizeof(zeros) ? (len - done) : sizeof(zeros);
            if (!m.write(va + done, zeros, chunk)) { res.status = kErrWrite; return; }
            done += chunk;
        }
    };

    // ---- путь rwx: payload сразу на место, стаб не нужен --------------------------
    uint64_t rwx_cave = 0;
    if (try_rwx(&rwx_cave)) {
        if (!m.write(rwx_cave, frame.data(), frame.size())) { res.status = kErrWrite; return res; }
        const uint64_t entry = rwx_cave + kPayloadHdr;
        if (!m.write(slot, &entry, sizeof(entry))) { res.status = kErrWrite; return res; }
        restore_slot();   // мгновенный возврат слота — вызов исполняется один раз
        if (res.status == kErrWrite) return res;
        const double deadline = now_s() + (double)o.wait_ms / 1000.0;
        if (!poll_done(m, rwx_cave + 0x10, deadline)) {
            wipe(rwx_cave, frame.size());
            res.status = kErrTimeout;
            return res;
        }
        wipe(rwx_cave, frame.size());
        res.status = kOk; res.cave = rwx_cave; res.payload_va = entry; res.rwx = true;
        return res;
    }

    // ---- текстовый путь: стаб в хвосте R-X сегмента -------------------------------
    uint64_t stub_cave = 0;
    if (!try_text(&stub_cave)) {
        res.status = kErrNoSite;
        return res;
    }

    uint8_t stub[kStubBytes];
    build_stub(stub);
    res.cave = stub_cave;
    if (!m.write(stub_cave, stub, sizeof(stub))) { res.status = kErrWrite; return res; }

    // Разовый увод слота и немедленный возврат: следующий вызов функции в игре
    // исполняет стаб ровно один раз. Если чит умрёт между этими двумя записями —
    // каждый вызов будет упираться в ограниченный спин стаба (~1 с) и уходить в
    // оригинал: игра тормозит, но жива; повторный inject восстанавливает.
    if (!m.write(slot, &stub_cave, sizeof(stub_cave))) { res.status = kErrWrite; return res; }
    restore_slot();
    if (res.status == kErrWrite) return res;

    res.cave = stub_cave;   // дальше любые исходы сообщают место записи
    // Ждём базу от стаба. Стаб сам сдаётся через ~1 с и ставит kFlagMissed.
    const double deadline = now_s() + (double)o.wait_ms / 1000.0;
    uint64_t mmap_base = 0;
    while (now_s() < deadline) {
        uint64_t scratch = 0;
        uint32_t flags = 0;
        if (m.read_quiet(stub_cave + kStubScratch + kScrBase, &scratch, sizeof(scratch)) &&
            scratch != 0) {
            if ((int64_t)scratch < 0) {
                restore_slot();
                wipe(stub_cave, kStubBytes);
                res.status = kErrMmap;
                res.detail_errno = (int)-(int64_t)scratch;
                return res;
            }
            mmap_base = scratch;
            break;
        }
        if (m.read_quiet(stub_cave + kStubScratch + kScrFlags, &flags, sizeof(flags)) &&
            flags == (uint32_t)kFlagMissed) {
            restore_slot();
            wipe(stub_cave, kStubBytes);
            res.status = kErrTimeout;
            return res;
        }
        sleep_ms(2);
    }
    if (!mmap_base) {
        restore_slot();
        wipe(stub_cave, kStubBytes);
        res.status = kErrTimeout;
        return res;
    }

    // Payload в свежий R-X регион (пишем транспортом — защита страниц не мешает).
    if (!m.write(mmap_base, frame.data(), frame.size())) { res.status = kErrWrite; return res; }
    const uint64_t entry = mmap_base + kPayloadHdr;
    const uint32_t go = (uint32_t)kFlagGo;
    m.write(stub_cave + kStubScratch + kScrEntry, &entry, sizeof(entry));
    m.write(stub_cave + kStubScratch + kScrFlags, &go, sizeof(go));
    m.write(stub_cave + kStubScratch + kScrEntry, &entry, sizeof(entry));  // страховка порядка

    const bool done = poll_done(m, mmap_base + 0x10, deadline);
    wipe(stub_cave, kStubBytes);
    if (o.wipe) wipe(mmap_base, frame.size());
    if (!done) {
        res.status = kErrTimeout;
        return res;
    }
    res.status = kOk; res.cave = stub_cave; res.payload_va = entry; res.rwx = false;
    return res;
}

}  // namespace inject
