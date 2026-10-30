#include "magic_farm_protocol.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <set>

using namespace magicfarm;
struct Memory {
    Shared shared;
    unsigned calls{};
    std::set<unsigned> failures;
    bool write(std::uintptr_t address,const void* p,std::size_t size) {
        if (failures.count(++calls)) return false;
        assert(address>=0x10000 && address+size<=0x10000+sizeof(shared));
        std::memcpy(reinterpret_cast<unsigned char*>(&shared)+address-0x10000,p,size);
        return true;
    }
};
int main() {
    constexpr std::uint64_t now=1000000000ULL;
    Command c{};c.enabled=1;c.net_id=42;c.deadline_ns=now+kLeaseNs;c.point={1,2,3};
    assert(valid(c,now));assert(!valid(c,c.deadline_ns));assert(!valid(c,0));
    c.enabled=0;assert(!valid(c,now));c.enabled=1;
    c.point.x=std::numeric_limits<float>::quiet_NaN();assert(!valid(c,now));c.point.x=1;
    Memory m;assert(publish_command(m,0x10000,0,c));
    assert(m.shared.sequence==2 && m.shared.command.net_id==42);
    // Failed payload AND failed clearing must leave a rejected odd sequence.
    m.calls=0;m.failures={2,3};
    assert(!publish_command(m,0x10000,m.shared.sequence,c));assert(m.shared.sequence&1);
    m.calls=0;m.failures.clear();
    assert(publish_command(m,0x10000,m.shared.sequence,c));assert(!(m.shared.sequence&1));
    // A failed final commit is also recoverable; never expose a partial packet.
    m.calls=0;m.failures={3};
    assert(!publish_command(m,0x10000,m.shared.sequence,c));assert(m.shared.sequence&1);
    m.calls=0;m.failures.clear();
    assert(publish_command(m,0x10000,m.shared.sequence,Command{}));
    assert(!valid(m.shared.command,now));assert(!(m.shared.sequence&1));
    float x=0,y=0;movement(90,1,x,y);assert(x>.999f && std::fabs(y)<.001f);
    movement(-90,1,x,y);assert(x<-.999f && std::fabs(y)<.001f);
    movement(180,1,x,y);assert(y<-.999f);
    movement(std::numeric_limits<float>::infinity(),1,x,y);assert(!x && !y);
}
