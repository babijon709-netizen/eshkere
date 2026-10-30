#include "magic_farm_hook.h"
#include "magic_farm_profile.h"
#include <cassert>
#include <cstring>

int main() {
    std::uint32_t original[4]{};std::memcpy(original,magicfarm::profile::FixedUpdateBytes,16);
    std::uint32_t out[12]{};
    const std::uintptr_t pc=0x7000000000ULL+magicfarm::profile::FixedUpdate;
    assert(magicfarm::trampoline_11991(original,pc,out));
    assert(out[0]==original[0] && out[1]==original[1] && out[6]==original[3]);
    std::uint64_t page=0;
    for (unsigned i=0;i<4;++i) page|=std::uint64_t((out[2+i]>>5)&0xffff)<<(i*16);
    assert(page==0x7000000000ULL+0xE9E3000);
    std::uint64_t resume=0;std::memcpy(&resume,out+10,8);assert(resume==pc+16);
    assert(out[8]==0x58000051 && out[9]==0xd61f0220);
    original[2]=0x14000000;assert(!magicfarm::trampoline_11991(original,pc,out));
    original[0]=0xd503201f;assert(!magicfarm::trampoline_11991(original,pc,out));
}
