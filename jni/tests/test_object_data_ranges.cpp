// Host test: heap_writable_ranges/heap_range_covers must agree with heap_writable_range.
#include "object_data_memory.h"
#include <cstdio>
#include <cstdlib>
#include <random>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static std::string line(unsigned long long lo, unsigned long long hi, const char* perms,
                        unsigned long long off, unsigned maj, unsigned min, unsigned long long ino, const char* name) {
    char b[512];
    std::snprintf(b, sizeof b, "%llx-%llx %s %08llx %02x:%02x %llu %s\n", lo, hi, perms, off, maj, min, ino, name);
    return b;
}
static void agree(const std::string& maps, uint64_t a, size_t sz) {
    uint64_t lo, hi;
    const bool ref = object_data::heap_writable_range(maps, a, sz, lo, hi);
    std::vector<object_data::HeapRange> r;
    object_data::heap_writable_ranges(maps, r);
    const bool got = object_data::heap_range_covers(r, a, sz);
    if (ref != got) { std::printf("MISMATCH addr=%llx size=%zu ref=%d got=%d\n", (unsigned long long)a, sz, ref, got); ++fails; }
}
int main() {
    std::string m;
    m += line(0x1000000, 0x1100000, "r--p", 0, 0xfd, 1, 55, "/system/lib64/libc.so");
    m += line(0x1100000, 0x1200000, "rw-p", 0x100000, 0xfd, 1, 55, "/system/lib64/libc.so");
    m += line(0x1200000, 0x1210000, "rw-p", 0, 0, 0, 0, "");                       // small bss tail -> rejected
    m += line(0x1300000, 0x1400000, "rw-p", 0, 0, 0, 0, "");                       // allowed anon
    m += line(0x1400000, 0x1500000, "rw-p", 0, 0, 0, 0, "[anon:scudo:primary]");   // allowed
    m += line(0x1500000, 0x1600000, "rw-p", 0, 0, 0, 0, "[anon:dalvik-main space]");// named -> rejected
    m += line(0x1600000, 0x1700000, "r--p", 0, 0, 0, 0, "");                       // perms -> rejected
    m += line(0x2000000, 0x2100000, "r-xp", 0, 0xfd, 1, 77, "/data/app/x/lib/arm64/libil2cpp.so");
    m += line(0x2100000, 0x3100000, "rw-p", 0, 0, 0, 0, "");                       // 16MB anon tail of file -> allowed
    m += line(0x3200000, 0x3300000, "rw-p", 0, 0, 0, 0, "[heap]");
    std::mt19937_64 rng(1);
    for (uint64_t a = 0xFF000; a < 0x3400000; a += 0x4F8) { agree(m, a, 4); agree(m, a, 64); }
    for (int i = 0; i < 20000; ++i) agree(m, 0x1000000 + rng() % 0x2500000, 1 + rng() % 64);
    // boundaries
    agree(m, 0x1400000 - 4, 4); agree(m, 0x1400000 - 3, 4); agree(m, 0x1500000 - 4, 4); agree(m, 0x1500000 - 3, 8);
    // garbage line mid-way: both must stop at the same place
    std::string bad = m + "garbage line\n" + line(0x4000000, 0x4100000, "rw-p", 0, 0, 0, 0, "");
    for (uint64_t a = 0x1000000; a < 0x4200000; a += 0x10000) agree(bad, a, 8);
    // unsorted -> abort
    std::string uns = line(0x5000000, 0x5100000, "rw-p", 0, 0, 0, 0, "") + line(0x4000000, 0x4100000, "rw-p", 0, 0, 0, 0, "");
    agree(uns, 0x5000010, 8); agree(uns, 0x4000010, 8);
    // empty / tiny addresses
    std::vector<object_data::HeapRange> e; CHECK(!object_data::heap_range_covers(e, 0x20000, 4));
    CHECK(!object_data::heap_range_covers(e, 0x10, 4));
    std::printf(fails ? "FAILED %d\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
