#include "chams_memory.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>

struct FakeMem {
    std::map<uint64_t, uint8_t> mem;
    unsigned writes = 0;

    template <class T>
    void put(uint64_t a, const T& v) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(T); ++i) mem[a + i] = bytes[i];
    }

    template <class T>
    bool read(uint64_t a, T& v) {
        auto* bytes = reinterpret_cast<uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(T); ++i) {
            auto it = mem.find(a + i);
            if (it == mem.end()) return false;
            bytes[i] = it->second;
        }
        return true;
    }

    template <class T>
    bool write_heap(uint64_t a, const T& v) {
        ++writes;
        put(a, v);
        return true;
    }

    template <class T>
    T get(uint64_t a) {
        T v{};
        assert(read(a, v));
        return v;
    }
};

static void test_offsets_selector() {
    using go_active::GameVersion;
    go_active::Select(GameVersion::Release);
    assert(go_active::OUTLINE_FEATURE_TYPEINFO_RVA == 0xDEA03C8);
    assert(go_active::RAGDOLL_BODYPART_TYPEINFO_RVA == 0xDEADB00);
    assert(go_active::RAGDOLL_LATE_UPDATE_RVA == 0x764FDF8);
    assert(go_active::OUTLINE_REGISTER_COMPONENT_RVA == 0x67C2D00);

    go_active::Select(GameVersion::Beta);
    assert(go_active::OUTLINE_FEATURE_TYPEINFO_RVA == 0xDE9D280);
    assert(go_active::RAGDOLL_BODYPART_TYPEINFO_RVA == 0xDEAA958);
    assert(go_active::RAGDOLL_LATE_UPDATE_RVA == 0x76558D8);
    assert(go_active::OUTLINE_REGISTER_COMPONENT_RVA == 0x679A384);

    go_active::Select(GameVersion::Release);
}

static void test_hook_and_restore(bool via_player_list) {
    FakeMem io;
    chams::Controller<FakeMem> ctl(io);

    constexpr uint64_t base         = 0x7000000000ULL;
    constexpr uint64_t out_klass    = 0x200000;
    constexpr uint64_t out_sf       = 0x201000;
    constexpr uint64_t out_settings = 0x202000;
    constexpr uint64_t out_sobj     = 0x203000;
    constexpr uint64_t out_pass     = 0x204000;
    constexpr uint64_t out_feature  = 0x205000;
    constexpr uint64_t tcw_list     = 0x206000;

    constexpr uint64_t bp_klass     = 0x300000;
    constexpr uint64_t rag_klass    = 0x301000;
    constexpr uint64_t rag_methods  = 0x302000;
    constexpr uint64_t mi_other     = 0x303000;
    constexpr uint64_t mi_late      = 0x304000;

    const uint64_t orig_fn = base + go_active::RAGDOLL_LATE_UPDATE_RVA;
    const uint64_t hook_fn = base + go_active::OUTLINE_REGISTER_COMPONENT_RVA;

    // OutlineFeature graph
    io.put<uint64_t>(base + go_active::OUTLINE_FEATURE_TYPEINFO_RVA, out_klass);
    io.put<uint64_t>(out_klass + 0xB8, out_sf);
    io.put<uint64_t>(out_sf + 0x00, out_settings);
    io.put<uint8_t>(out_sf + 0x08, 1);
    io.put<uint64_t>(out_sf + 0x10, out_pass);

    io.put<uint64_t>(out_pass + 0xB8, out_feature);
    io.put<uint64_t>(out_pass + 0xC8, tcw_list);
    io.put<uint8_t>(out_pass + 0xD8, 1);
    io.put<int32_t>(tcw_list + go_active::IL2CPP_LIST_SIZE, 3);

    io.put<uint64_t>(out_feature + 0x00, out_klass);
    io.put<uint64_t>(out_feature + 0x28, out_settings);
    io.put<uint8_t>(out_feature + 0x30, 1);

    chams::SettingsSnapshot init_s{3.0f, 15.0f, 0.9f, 0.8f, 0.7f, 1.0f};
    io.put(out_settings + 0x18, init_s);
    io.put<uint64_t>(out_settings + 0x10, out_sobj);
    io.put<uint64_t>(out_sobj + go_active::MANAGED_CACHED_PTR, 0x999000);
    io.put(out_sobj + 0x18, init_s);

    // Ragdoll class + MethodInfo table
    io.put<uint64_t>(rag_klass + 0x98, rag_methods);
    io.put<uint16_t>(rag_klass + 0x120, 20);
    for (uint16_t i = 0; i < 20; ++i) {
        io.put<uint64_t>(rag_methods + i * 8, i == 14 ? mi_late : mi_other);
    }
    io.put<uint64_t>(mi_other + 0x00, base + 0x764F08C);
    io.put<uint64_t>(mi_other + 0x08, base + 0x764F08C);
    io.put<uint64_t>(mi_other + 0x20, rag_klass);
    io.put<uint8_t>(mi_other + 0x52, 0);

    io.put<uint64_t>(mi_late + 0x00, orig_fn);
    io.put<uint64_t>(mi_late + 0x08, orig_fn);
    io.put<uint64_t>(mi_late + 0x20, rag_klass);
    io.put<uint8_t>(mi_late + 0x52, 0);

    if (!via_player_list) {
        io.put<uint64_t>(base + go_active::RAGDOLL_BODYPART_TYPEINFO_RVA, bp_klass);
        io.put<uint64_t>(bp_klass + 0x50, rag_klass);
    } else {
        constexpr uint64_t pm_klass   = 0x400000;
        constexpr uint64_t pm_sf      = 0x401000;
        constexpr uint64_t plist      = 0x402000;
        constexpr uint64_t pitems     = 0x403000;
        constexpr uint64_t player     = 0x404000;
        constexpr uint64_t holder     = 0x405000;
        constexpr uint64_t minfo      = 0x406000;
        constexpr uint64_t canim      = 0x407000;
        constexpr uint64_t ragdoll    = 0x408000;

        io.put<uint64_t>(base + go_active::PLAYER_MANAGER_TYPEINFO_RVA, pm_klass);
        io.put<uint64_t>(pm_klass + 0xB8, pm_sf);
        io.put<uint64_t>(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_LIST, plist);
        io.put<uint64_t>(plist + go_active::IL2CPP_LIST_ITEMS, pitems);
        io.put<int32_t>(plist + go_active::IL2CPP_LIST_SIZE, 1);
        io.put<uint64_t>(pitems + go_active::IL2CPP_ARRAY_FIRST_ELEMENT, player);
        io.put<uint64_t>(player + 0x260, holder);
        io.put<uint64_t>(holder + 0x10, minfo);
        io.put<uint64_t>(minfo + 0x60, canim);
        io.put<uint64_t>(canim + 0x38, ragdoll);
        io.put<uint64_t>(ragdoll + 0x00, rag_klass);
    }

    chams::Params p{};
    p.enabled = true;
    p.geometry_outline = false;
    p.width = 7.0f;
    p.intensity = 60.0f;
    p.r = 0.1f;
    p.g = 0.9f;
    p.b = 0.3f;
    p.a = 1.0f;

    ctl.tick(1, base, p);
    assert(ctl.status() == viewmodel::Status::Active);
    assert(io.get<uint64_t>(mi_late + 0x00) == hook_fn);
    assert(io.get<uint64_t>(mi_late + 0x08) == hook_fn);
    assert(io.get<uint8_t>(out_feature + 0x30) == 0);
    assert(io.get<uint8_t>(out_pass + 0xD8) == 0);
    assert(std::fabs(io.get<float>(out_settings + 0x18) - 7.0f) < 1e-5f);
    assert(std::fabs(io.get<float>(out_sobj + 0x1C) - 60.0f) < 1e-5f);
    assert(std::fabs(io.get<float>(out_sobj + 0x24) - 0.9f) < 1e-5f);

    // Disable -> clean restore
    p.enabled = false;
    ctl.tick(1, base, p);
    assert(ctl.status() == viewmodel::Status::Off);
    assert(io.get<uint64_t>(mi_late + 0x00) == orig_fn);
    assert(io.get<uint64_t>(mi_late + 0x08) == orig_fn);
    assert(io.get<uint8_t>(out_feature + 0x30) == 1);
    assert(io.get<uint8_t>(out_pass + 0xD8) == 1);
    assert(std::fabs(io.get<float>(out_settings + 0x18) - 3.0f) < 1e-5f);
    assert(std::fabs(io.get<float>(out_sobj + 0x1C) - 15.0f) < 1e-5f);
    assert(io.get<int32_t>(tcw_list + go_active::IL2CPP_LIST_SIZE) == 0);
}

int main() {
    test_offsets_selector();
    test_hook_and_restore(false);
    test_hook_and_restore(true);
    return 0;
}
