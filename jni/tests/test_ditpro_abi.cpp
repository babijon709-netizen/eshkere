#include "../src/mem_io.h"
#include "../src/kernel_io.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {

// Simulates a PTE-walk kernel transport (DitPro / RT-Dev) in the main menu
// where the cold ELF header page (base + 0x0) has been evicted by kswapd and
// only PlayerLoopHelper_TypeInfo is initialized.
class EvictedElfHeaderTransport : public memio::Transport {
public:
    const char* name() const override { return "mock_ditpro_pte"; }
    bool open(pid_t pid) override {
        open_ = (pid > 0);
        return open_;
    }
    void close() override { open_ = false; }

    void set_u64(uint64_t addr, uint64_t val) {
        words_[addr & memio::kAddressTagMask] = val;
    }

    ssize_t read_once(uint64_t addr, void* out, size_t len) override {
        if (!open_) { errno = EBADF; return -1; }
        const uint64_t a = addr & memio::kAddressTagMask;
        // Evicted cold ELF header page: fails with EIO.
        if ((a & ~0xFFFULL) == (base_ & ~0xFFFULL)) {
            errno = EIO;
            return -1;
        }
        if (len == sizeof(uint64_t)) {
            auto it = words_.find(a);
            uint64_t v = (it != words_.end()) ? it->second : 0ULL;
            std::memcpy(out, &v, sizeof(v));
            return (ssize_t)sizeof(v);
        }
        errno = EIO;
        return -1;
    }

    ssize_t write_once(uint64_t, const void*, size_t len) override {
        return (ssize_t)len;
    }

    void set_base(uint64_t b) { base_ = b; }

private:
    bool open_ = false;
    uint64_t base_ = 0;
    std::unordered_map<uint64_t, uint64_t> words_;
};

} // namespace

int main() {
    // 1. Verify unified 40-byte DitProRwReq layout across 131 KPM, 138 KPM, 138 KO, and 138 bitflag.
    static_assert(sizeof(memio::DitProRwReq) == 0x28, "DitProRwReq size mismatch");
    static_assert(offsetof(memio::DitProRwReq, pid) == 0x00, "pid offset mismatch");
    static_assert(offsetof(memio::DitProRwReq, addr) == 0x08, "addr offset mismatch");
    static_assert(offsetof(memio::DitProRwReq, user_buf) == 0x10, "user_buf offset mismatch");
    static_assert(offsetof(memio::DitProRwReq, size) == 0x18, "size offset mismatch");
    static_assert(offsetof(memio::DitProRwReq, flags) == 0x20, "flags offset mismatch");

    // 2. Verify 32-byte DitProBaseReq layout for 0x400 / 0x61a18 module base lookup.
    static_assert(sizeof(memio::DitProBaseReq) == 0x20, "DitProBaseReq size mismatch");
    static_assert(offsetof(memio::DitProBaseReq, pid) == 0x00, "base pid offset mismatch");
    static_assert(offsetof(memio::DitProBaseReq, name_ptr) == 0x08, "base name_ptr offset mismatch");
    static_assert(offsetof(memio::DitProBaseReq, start) == 0x10, "base start offset mismatch");
    static_assert(offsetof(memio::DitProBaseReq, end) == 0x18, "base end offset mismatch");

    // 3. Verify command constants for DitPro 131.
    static_assert(memio::kDitPro131CmdVersion == 0x11L);
    static_assert(memio::kDitPro131CmdCheck == 0x332L);
    static_assert(memio::kDitPro131Read == 0x100L);
    static_assert(memio::kDitPro131Write == 0x200L);
    static_assert(memio::kDitPro131CmdPid == 0x300L);
    static_assert(memio::kDitPro131CmdModBase == 0x400L);

    // 4. Verify Reader::bind succeeds in the main menu when page 0 (ELF header)
    //    is evicted and only PlayerLoopHelper_TypeInfo is initialized.
    {
        constexpr uint64_t kFakeBase  = 0x7000000000ULL;
        constexpr uint64_t kFakeKlass = 0x7100123400ULL;
        constexpr uint64_t kFakeImage = 0x7100567800ULL;

        EvictedElfHeaderTransport tp;
        tp.set_base(kFakeBase);

        memio::Reader reader;
        reader.set_transport(&tp);

        // Without any initialized TypeInfo, bind must fail cleanly.
        assert(!reader.bind(4242, kFakeBase));

        // Initialize only the Release-profile PlayerLoopHelper_TypeInfo slot
        // (game_offsets::PLAYER_LOOP_HELPER_TYPEINFO_RVA, 11991 = 0xE60B780)
        // and its Il2CppClass::image (+0x0), while NetworkClient/PlayerManager
        // remain 0 (as in the main menu).
        tp.set_u64(kFakeBase + game_offsets::PLAYER_LOOP_HELPER_TYPEINFO_RVA, kFakeKlass);
        tp.set_u64(kFakeKlass, kFakeImage);

        assert(reader.bind(4242, kFakeBase));
        assert(reader.bound());
    }

    // 5. Each current Beta 11982 hot slot can independently verify a live
    //    Il2CppClass and image while the ELF header page is unavailable.
    const uint64_t beta_singles[] = {
        game_offsets_beta::PLAYER_LOOP_HELPER_TYPEINFO_RVA,
        game_offsets_beta::UNITY_CAMERA_TYPEINFO_RVA,
        game_offsets_beta::NETWORK_CLIENT_TYPEINFO_RVA,
        game_offsets_beta::PLAYER_MANAGER_TYPEINFO_RVA,
        game_offsets_beta::GAME_CONTROLLER_TYPEINFO_RVA,
    };
    for (uint64_t slot : beta_singles) {
        constexpr uint64_t kFakeBase  = 0x7200000000ULL;
        constexpr uint64_t kFakeKlass = 0x7300123400ULL;
        constexpr uint64_t kFakeImage = 0x7300567800ULL;
        EvictedElfHeaderTransport tp;
        tp.set_base(kFakeBase);
        tp.set_u64(kFakeBase + slot, kFakeKlass);
        memio::Reader reader;
        reader.set_transport(&tp);
        // A slot on its own isn't enough; Il2CppClass::image must be valid.
        assert(!reader.bind(4242, kFakeBase));
        tp.set_u64(kFakeKlass, kFakeImage);
        assert(reader.bind(4242, kFakeBase));
        assert(reader.bound());
    }

    // 6. The two-slot fast path also works without readable class objects.
    //    Test both pairs independently; an incomplete pair must be rejected.
    const uint64_t beta_pairs[][2] = {
        {game_offsets_beta::PLAYER_MANAGER_TYPEINFO_RVA, game_offsets_beta::GAME_CONTROLLER_TYPEINFO_RVA},
        {game_offsets_beta::PLAYER_LOOP_HELPER_TYPEINFO_RVA, game_offsets_beta::NETWORK_CLIENT_TYPEINFO_RVA},
    };
    for (const auto& pair : beta_pairs) {
        constexpr uint64_t kFakeBase = 0x7400000000ULL;
        constexpr uint64_t kClassA = 0x7500123400ULL;
        constexpr uint64_t kClassB = 0x7500567800ULL;
        EvictedElfHeaderTransport tp;
        tp.set_base(kFakeBase);
        tp.set_u64(kFakeBase + pair[0], kClassA);
        memio::Reader reader;
        reader.set_transport(&tp);
        assert(!reader.bind(4242, kFakeBase));
        tp.set_u64(kFakeBase + pair[1], kClassB);
        assert(reader.bind(4242, kFakeBase));
        assert(reader.bound());
    }

    return 0;
}
