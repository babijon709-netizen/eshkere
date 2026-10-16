// Хост-тест inject.h: синтетический arm64 ELF в собственной памяти теста.
// Транспорт — самопись: адреса «чужого процесса» = смещения в статическом
// буфере. Логика разбора (ELF/GOT/кэв) и оркестрация проверяются целиком,
// кроме фактического исполнения arm64-кода (на x86 оно невозможно) — вместо
// него проверяется, что по таймауту слот возвращён и всё затёрто.
#include "inject.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

// «Память чужого процесса» — статический буфер этого же процесса.
constexpr uint64_t kBufSize = 0x10000;
static uint8_t g_buf[kBufSize];

struct LocalTransport final : memio::Transport {
    const char* name() const override { return "local (тест)"; }
    bool open(pid_t) override { return true; }
    void close() override {}
    ssize_t read_once(uint64_t addr, void* out, size_t len) override {
        if (addr + len > kBufSize) { errno = EIO; return -1; }
        memcpy(out, g_buf + addr, len);
        return (ssize_t)len;
    }
    ssize_t write_once(uint64_t addr, const void* in, size_t len) override {
        if (addr + len > kBufSize) { errno = EIO; return -1; }
        memcpy(g_buf + addr, in, len);
        return (ssize_t)len;
    }
};

// Синтетический образ: ehdr + 2 phdr (R-X LOAD + PT_DYNAMIC), dynamic,
// dynsym, dynstr, 2 rela (strlen JUMP_SLOT, memcpy GLOB_DAT), значение слота.
// Образ живёт на базе kImageBase (0 в parse_image запрещён намеренно).
constexpr uint64_t kImageBase   = 0x1000;
constexpr uint64_t kPhoff       = kImageBase + 0x40;
constexpr uint64_t kDynamic     = kImageBase + 0x100;
constexpr uint64_t kSymtab      = kImageBase + 0x200;
constexpr uint64_t kStrtab      = kImageBase + 0x300;
constexpr uint64_t kJmprel      = kImageBase + 0x400;  // JUMP_SLOT strlen
constexpr uint64_t kGlobrel     = kImageBase + 0x430;  // GLOB_DAT memcpy
constexpr uint64_t kSlotStrlen  = kImageBase + 0x500;  // GOT-слот strlen
constexpr uint64_t kSlotMemcpy  = kImageBase + 0x508;  // GOT-слот memcpy (GLOB_DAT)
constexpr uint64_t kOrigStrlen  = 0x7777000;
constexpr uint64_t kExecFilesz  = 0x8000;
constexpr uint64_t kExecVa      = kImageBase;           // PT_LOAD R-X, p_vaddr = 0
constexpr uint64_t kTextEnd     = kExecVa + 0x6000;     // тут кончается «код», дальше нули

void build_image() {
    memset(g_buf, 0, sizeof(g_buf));

    Elf64_Ehdr eh = {};
    memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS64;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_machine = EM_AARCH64;
    eh.e_phoff = 0x40;
    eh.e_phentsize = sizeof(Elf64_Phdr);
    eh.e_phnum = 2;
    memcpy(g_buf + kImageBase, &eh, sizeof(eh));

    Elf64_Phdr ph[2] = {};
    ph[0].p_type = PT_LOAD;
    ph[0].p_flags = PF_R | PF_X;
    ph[0].p_vaddr = 0;
    ph[0].p_filesz = kExecFilesz;
    ph[0].p_memsz = kExecFilesz;
    ph[1].p_type = PT_DYNAMIC;
    ph[1].p_vaddr = 0x100;
    ph[1].p_filesz = 9 * sizeof(Elf64_Dyn);
    ph[1].p_memsz = ph[1].p_filesz;
    memcpy(g_buf + kPhoff, ph, sizeof(ph));

    const std::pair<uint64_t, uint64_t> dyn[] = {
        {DT_SYMTAB, kSymtab - kImageBase}, {DT_STRTAB, kStrtab - kImageBase},
        {DT_STRSZ, 0x100},
        {DT_SYMENT, sizeof(Elf64_Sym)},
        {DT_JMPREL, kJmprel - kImageBase}, {DT_PLTRELSZ, 1 * sizeof(Elf64_Rela)},
        {DT_RELA, kGlobrel - kImageBase}, {DT_RELASZ, 1 * sizeof(Elf64_Rela)},
        {DT_NULL, 0},
    };
    for (size_t i = 0; i < sizeof(dyn) / sizeof(dyn[0]); ++i) {
        Elf64_Dyn d;
        d.d_tag = dyn[i].first;
        d.d_un.d_val = dyn[i].second;
        memcpy(g_buf + kDynamic + i * sizeof(d), &d, sizeof(d));
    }

    Elf64_Sym syms[3] = {};
    syms[1].st_name = 1;   // "strlen"
    syms[1].st_info = (STB_GLOBAL << 4) | STT_FUNC;
    syms[1].st_shndx = 1;
    syms[1].st_value = kOrigStrlen;
    syms[2].st_name = 8;   // "memcpy"
    syms[2].st_info = (STB_GLOBAL << 4) | STT_FUNC;
    syms[2].st_shndx = 1;
    syms[2].st_value = 0x7778000;
    memcpy(g_buf + kSymtab, syms, sizeof(syms));
    memcpy(g_buf + kStrtab, "\0strlen\0memcpy\0", 15);

    Elf64_Rela js = {};
    js.r_offset = kSlotStrlen - kImageBase;
    js.r_info = ((uint64_t)1 << 32) | R_AARCH64_JUMP_SLOT;
    memcpy(g_buf + kJmprel, &js, sizeof(js));

    Elf64_Rela gd = {};
    gd.r_offset = kSlotMemcpy - kImageBase;
    gd.r_info = ((uint64_t)2 << 32) | R_AARCH64_GLOB_DAT;
    memcpy(g_buf + kGlobrel, &gd, sizeof(gd));

    const uint64_t orig = kOrigStrlen;
    memcpy(g_buf + kSlotStrlen, &orig, 8);

    // «Код» после таблиц, дальше (до конца сегмента) — нули (кэв).
    constexpr uint64_t kCodeStart = kImageBase + 0x700;
    memset(g_buf + kCodeStart, 0xAB, kTextEnd - kCodeStart);
    // Отдельная ELF-магия для пробы bind (адрес 0 в bind запрещён).
    memcpy(g_buf + 0xF000, "\x7f" "ELF", 4);
}

const char* kMapsText =
    "1000-9000 r-xp 00000000 fd:01 1           /data/app/libil2cpp.so\n"
    "7777000-7778000 r-xp 00000000 fd:01 2     /apex/com.android.runtime/lib64/bionic/libc.so\n"
    "9000000-9100000 rwxp 00000000 00:00 0     \n";

struct MemReader {
    memio::Reader reader;
    LocalTransport transport;
    MemReader() {
        reader.set_transport(&transport);
        assert(reader.bind(1, 0xF000));   // проба: ELF-магия на 0xF000
        assert(reader.bound());
    }
};

void test_stub_words() {
    uint8_t stub[inject::kStubBytes];
    inject::build_stub(stub);
    const uint32_t* w = (const uint32_t*)stub;
    // Скелет: сохранение регистров, svc, адреса ветвлений, возвраты.
    assert(w[0] == 0xa9bf7bfd);
    assert(w[0x38 / 4] == 0xd4000001);          // svc #0 (mmap)
    assert(w[0x3c / 4] == 0x10000430);          // adr x16, 0xC0
    assert(w[0x84 / 4] == 0xd61f0220);          // br x17 (payload)
    assert(w[0x88 / 4] == 0x52bffff1);          // movz w17, 0xFFFF<<16
    assert(w[0xb0 / 4] == 0xf9400e11);          // ldr x17, [x16, #24]
    assert(w[0xb4 / 4] == 0xd61f0220);          // br x17 (orig)
    // Scratch пуст.
    for (size_t i = inject::kStubScratch; i < inject::kStubBytes; ++i)
        assert(stub[i] == 0);
}

void test_parse_regions() {
    std::vector<inject::Region> regions;
    assert(inject::parse_regions(kMapsText, &regions));
    assert(regions.size() == 3);
    assert(strncmp(regions[0].perms, "r-x", 3) == 0);
    assert(regions[2].perms[0] == 'r' && regions[2].perms[1] == 'w' && regions[2].perms[2] == 'x');
    assert(regions[2].path.empty());
    assert(regions[1].path.find("libc.so") != std::string::npos);
}

void test_parse_image_and_got() {
    MemReader mr;
    inject::Image img;
    assert(inject::parse_image(mr.reader, kImageBase, &img));
    assert(img.exec_va == kExecVa && img.exec_filesz == kExecFilesz);
    assert(img.dyn_va == kDynamic);

    uint64_t slot = 0, orig = 0;
    assert(inject::find_got_slot(mr.reader, kImageBase, "strlen", &slot, &orig));
    assert(slot == kSlotStrlen);
    assert(orig == kOrigStrlen);

    // memcpy — только GLOB_DAT, вторым проходом.
    assert(inject::find_got_slot(mr.reader, kImageBase, "memcpy", &slot, &orig));
    assert(slot == kSlotMemcpy);

    // Чужое имя — отказ.
    assert(!inject::find_got_slot(mr.reader, kImageBase, "nope", &slot, &orig));
}

void test_zero_cave() {
    MemReader mr;
    uint64_t cave = 0;
    assert(inject::find_zero_cave(mr.reader, kExecVa, kExecFilesz, inject::kStubBytes, &cave));
    assert(cave >= kTextEnd && cave + inject::kStubBytes <= kExecVa + kExecFilesz);
    assert(cave % 16 == 0);
    // Диапазон короче нужного нулевого рана — кэва нет.
    assert(!inject::find_zero_cave(mr.reader, kExecVa, 0xF0, inject::kStubBytes, &cave));
}

void test_run_timeout_restores_slot() {
    MemReader mr;
    static uint8_t blob[inject::kPayloadHdr + 64];
    memcpy(blob, "\x55\x50\x4a\x49", 4);   // тело не исполняется, это не важно

    inject::Options o;
    o.payload = blob;
    o.payload_size = sizeof(blob);
    o.il2cpp_base = kImageBase;
    o.maps_text = kMapsText;
    o.wait_ms = 80;      // ни один CPU не исполнит arm64 — ждём честный таймаут
    o.wipe = true;

    const inject::Result r = inject::run(mr.reader, o);
    // RWX-регион 0x9000000 за пределами буфера — записи туда дадут EIO и путь
    // rwx отвалится... нет: cave-сканер читает через read_quiet и на EIO
    // молча пропустит регион, оркестрация выберет текстовый кэв.
    assert(r.status == inject::kErrTimeout);
    assert(!r.rwx);
    assert(r.slot == kSlotStrlen && r.orig_fn == kOrigStrlen);
    // Слот возвращён.
    uint64_t slot_value = 0;
    assert(mr.reader.read_quiet(kSlotStrlen, &slot_value, sizeof(slot_value)));
    assert(slot_value == kOrigStrlen);
    // Стаб затёрт (текстовый путь, wipe = true).
    uint8_t back[inject::kStubBytes] = {};
    assert(mr.reader.read_quiet(r.cave, back, sizeof(back)));
    for (size_t i = 0; i < sizeof(back); ++i) assert(back[i] == 0);
}

void test_run_payload_checks() {
    MemReader mr;
    inject::Options o;
    uint8_t tiny[8] = {};
    o.payload = tiny;
    o.payload_size = sizeof(tiny);
    o.maps_text = kMapsText;
    assert(inject::run(mr.reader, o).status == inject::kErrPayload);
    o.payload = nullptr;
    assert(inject::run(mr.reader, o).status == inject::kErrPayload);
}

}  // namespace

int main() {
    build_image();
    test_stub_words();
    test_parse_regions();
    test_parse_image_and_got();
    test_zero_cave();
    test_run_payload_checks();
    test_run_timeout_restores_slot();
    printf("test_inject: все проверки прошли\n");
    return 0;
}
