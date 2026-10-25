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

    // 3. Verify command constants for 131 and 138.
    static_assert(memio::kDitPro131CmdVersion == 0x11L);
    static_assert(memio::kDitPro131CmdCheck == 0x332L);
    static_assert(memio::kDitPro131Read == 0x100L);
    static_assert(memio::kDitPro131Write == 0x200L);
    static_assert(memio::kDitPro131CmdPid == 0x300L);
    static_assert(memio::kDitPro131CmdModBase == 0x400L);

    static_assert(memio::kDitPro138CmdOpenFd == 0x61628L);
    static_assert(memio::kDitPro138CmdVersion == 0x61629L);
    static_assert(memio::kDitPro138CmdRead == 0x61718L);
    static_assert(memio::kDitPro138CmdWrite == 0x61818L);
    static_assert(memio::kDitPro138CmdPid == 0x61918L);
    static_assert(memio::kDitPro138CmdModBase == 0x61a18L);

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

        // Initialize only Release 11978 PlayerLoopHelper_TypeInfo (0xE66DEB0)
        // and its Il2CppClass::image (+0x0), while NetworkClient/PlayerManager
        // remain 0 (as in the main menu).
        tp.set_u64(kFakeBase + 0xE66DEB0ULL, kFakeKlass);
        tp.set_u64(kFakeKlass, kFakeImage);

        assert(reader.bind(4242, kFakeBase));
        assert(reader.bound());
    }

    // 5. Same check for Beta 11977 PlayerLoopHelper_TypeInfo (0xE647100).
    {
        constexpr uint64_t kFakeBase  = 0x7200000000ULL;
        constexpr uint64_t kFakeKlass = 0x7300123400ULL;
        constexpr uint64_t kFakeImage = 0x7300567800ULL;

        EvictedElfHeaderTransport tp;
        tp.set_base(kFakeBase);
        tp.set_u64(kFakeBase + 0xE647100ULL, kFakeKlass);
        tp.set_u64(kFakeKlass, kFakeImage);

        memio::Reader reader;
        reader.set_transport(&tp);
        assert(reader.bind(4242, kFakeBase));
        assert(reader.bound());
    }

    return 0;
}
