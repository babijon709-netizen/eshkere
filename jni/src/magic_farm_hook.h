#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace magicfarm {
// The 11991 prologue contains STR, STP, ADRP, MOV. Accept exactly these
// instructions; never copy an unknown PC-relative prologue into a trampoline.
inline bool trampoline_11991(const std::uint32_t* original, std::uintptr_t pc,
                            std::uint32_t (&out)[12]) {
    if (original[0] != 0xf81e0ffe || original[1] != 0xa9014ff4 ||
        (original[2] & 0x9f00001fu) != 0x90000014u || original[3] != 0xaa0003f3)
        return false;
    std::int64_t imm = ((original[2] >> 29) & 3u) | (((original[2] >> 5) & 0x7ffffu) << 2);
    if (imm & (1 << 20)) imm -= (1 << 21);
    const std::uint64_t page = ((pc + 8) & ~std::uintptr_t(4095)) + imm * 4096;
    out[0] = original[0]; out[1] = original[1];
    // MOVZ/MOVK x20 reconstruct ADRP's absolute page at the new address.
    for (unsigned i = 0; i < 4; ++i)
        out[2+i] = (i ? 0xf2800000u : 0xd2800000u) | (i << 21) |
                   (static_cast<std::uint32_t>((page >> (i*16)) & 0xffff) << 5) | 20;
    out[6] = original[3];
    out[7] = 0xd503201fu;
    out[8] = 0x58000051u; // LDR x17, +8
    out[9] = 0xd61f0220u; // BR x17
    const std::uint64_t resume = pc + 16;
    std::memcpy(out + 10, &resume, sizeof(resume));
    return true;
}
} // namespace magicfarm
