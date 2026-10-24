#include "network_aa.h"
#include "network_aa_external.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>

static bool near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
static void world_movement(const netaa::PlayerInput& p, float& x, float& z) {
    const float y = p.yaw * 0.017453292519943295f;
    x = p.movement[0]*std::cos(y) + p.movement[1]*std::sin(y);
    z = -p.movement[0]*std::sin(y) + p.movement[1]*std::cos(y);
}
struct FakeInputMemory {
    netaa::PlayerInput packet{};
    int reads=0, writes=0;
    bool readable=true, writable=true, mutate_second=false;
    bool read(uint64_t address, void* out, size_t size) {
        assert(address == 0x1000 || address == 0x2000);
        assert(size == sizeof(packet));
        if (!readable) return false;
        if (++reads == 2 && mutate_second) ++packet.tick;
        std::memcpy(out, &packet, size); return true;
    }
    bool write(uint64_t address, const void* in, size_t size) {
        assert(address == 0x1000 || address == 0x2000);
        assert(size == 24); // no flags, padding, Tick or next object fields
        if (!writable) return false;
        std::memcpy(&packet, in, size); ++writes; return true;
    }
};
static void external_tests() {
    using namespace netaa;
    Command config; config.flags=1; config.yaw_mode=YawMode::Backwards;
    FakeInputMemory mem;
    mem.packet.ready=1; mem.packet.look[2]=1; mem.packet.tick=10;
    mem.packet.movement[1]=1; mem.packet.sprint=1;
    const PlayerInput original=mem.packet;
    External controller;
    controller.update(mem, 0x1000, true, config, 1000);
    assert(mem.writes==1 && controller.pending());
    assert(mem.packet.tick==10 && mem.packet.sprint==1 && mem.packet.ready==1);
    const PlayerInput spoofed=mem.packet;
    controller.update(mem, 0x1000, true, config, 1016);
    assert(mem.writes==1); // never spin the already spoofed yaw a second time
    assert(std::memcmp(&spoofed,&mem.packet,sizeof(spoofed))==0);
    controller.update(mem, 0x1000, false, config, 1020);
    assert(mem.writes==2 && !controller.pending());
    assert(std::memcmp(&original,&mem.packet,sizeof(original))==0);
    assert(controller.restores()==1);

    controller.update(mem, 0x1000, true, config, 1030);
    ++mem.packet.tick; mem.packet.yaw=75.f; // a newer game write wins
    const PlayerInput newer=mem.packet;
    controller.update(mem, 0x1000, false, config, 1040);
    assert(std::memcmp(&newer,&mem.packet,sizeof(newer))==0);

    mem.packet=original; mem.reads=0; mem.mutate_second=true;
    const int before=mem.writes;
    controller.forget(); controller.update(mem,0x1000,true,config,1100);
    assert(mem.writes==before && controller.state==ExternalState::Raced);
    mem.mutate_second=false; mem.packet=original;
    controller.forget(); controller.update(mem,0x1000,true,config,1200);
    controller.forget(); // detach/rebind: no restoration into a recycled object
    const PlayerInput after=mem.packet;
    controller.update(mem,0x2000,false,config,1210);
    assert(std::memcmp(&after,&mem.packet,sizeof(after))==0);

    mem.packet=original; controller.forget();
    controller.update(mem,0x1000,true,config,1300);
    controller.update(mem,0x1000,true,config,1600); // stalled producer: restore
    assert(!controller.pending() && std::memcmp(&original,&mem.packet,sizeof(original))==0);
    mem.readable=false; controller.forget();
    controller.update(mem,0x1000,true,config,1700);
    assert(controller.state==ExternalState::ReadFailed);
    mem.readable=true; mem.writable=false;
    controller.update(mem,0x1000,true,config,1710);
    assert(controller.state==ExternalState::WriteFailed);
}

int main() {
    external_tests();
    using namespace netaa;
    Command c;
    c.flags = 1;
    assert(valid(c));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    c.speed = nan; assert(!valid(c)); c.speed = 360;
    assert(speed(nan) == kDefaultSpeed);
    c.yaw_mode = static_cast<YawMode>(3); assert(!valid(c)); c.yaw_mode = YawMode::Spin;
    c.pitch_mode = static_cast<PitchMode>(3); assert(!valid(c)); c.pitch_mode = PitchMode::Keep;
    c.flags = 4; assert(!valid(c)); c.flags = 1;
    PlayerInput real{};
    real.movement[0] = .3f; real.movement[1] = .8f;
    real.look[0] = .6f; real.look[1] = 0; real.look[2] = .8f;
    real.yaw = 37; real.tick = 16; real.ready = 1;
    real.sprint = 1; real.padding[1] = 0xac; real.tail[0] = 0x7b;
    const PlayerInput saved = real;
    PlayerInput fake{};
    assert(rewrite(real,c,fake,500));
    assert(std::memcmp(&real,&saved,sizeof(real)) == 0);
    assert(near(fake.yaw, -180.f));
    assert(near(fake.look[0],0) && near(fake.look[2],-1));
    // No changes to tick, flags, or padding bytes; never touch authoritative input.
    PlayerInput unchanged = fake;
    std::memcpy(unchanged.movement, real.movement, sizeof(real.movement));
    std::memcpy(unchanged.look, real.look, sizeof(real.look)); unchanged.yaw = real.yaw;
    assert(std::memcmp(&unchanged,&real,sizeof(real)) == 0);
    PlayerInput duplicate;
    assert(rewrite(real,c,duplicate,500));
    assert(std::memcmp(&fake,&duplicate,sizeof(fake)) == 0);

    for (auto mode : {YawMode::Spin, YawMode::Jitter, YawMode::Backwards}) {
        c.yaw_mode = mode;
        for (float yaw : {-179.f, -90.f, 0.f, 37.f, 179.f}) {
            real.yaw = yaw;
            for (uint32_t tick : {0u, 1u, 16u, 65535u, 0xffffffffu}) {
                real.tick = tick;
                assert(rewrite(real,c,fake,500));
                float rx,rz,fx,fz;
                world_movement(real,rx,rz); world_movement(fake,fx,fz);
                assert(near(rx,fx) && near(rz,fz));
                assert(fake.yaw >= -180 && fake.yaw < 180);
                assert(near(fake.look[0]*fake.look[0]+fake.look[1]*fake.look[1]+fake.look[2]*fake.look[2],1));
            }
        }
    }
    real = saved;
    c.yaw_mode = YawMode::Spin; real.tick = 8;
    c.flags = 1; assert(rewrite(real,c,fake,250)); assert(near(fake.yaw,90));
    c.flags = 3; assert(rewrite(real,c,fake,250)); assert(near(fake.yaw,-90));
    c.pitch_mode = PitchMode::Up; assert(rewrite(real,c,fake,500)); assert(fake.look[1] > .99f);
    c.pitch_mode = PitchMode::Down; assert(rewrite(real,c,fake,500)); assert(fake.look[1] < -.99f);
    real.look[1] = .6f; real.look[0] = 0; real.look[2] = .8f;
    c.pitch_mode = PitchMode::Keep; assert(rewrite(real,c,fake,500)); assert(near(fake.look[1],.6f));
    real = saved;
    real.aim = 1; assert(!rewrite(real,c,fake,500)); real.aim = 0;
    real.ready = 0; assert(!rewrite(real,c,fake,500)); real.ready = 1;
    real.too_close = 1; assert(!rewrite(real,c,fake,500)); real.too_close = 0;
    real.movement[0] = nan; assert(!rewrite(real,c,fake,500)); real = saved;
    real.movement[0] = 1; real.movement[1] = 1; assert(!rewrite(real,c,fake,500)); real = saved;
    real.look[2] = nan; assert(!rewrite(real,c,fake,500)); real = saved;
    real.yaw = nan; assert(!rewrite(real,c,fake,500)); real = saved;
    real.crouch = 2; assert(!rewrite(real,c,fake,500)); real = saved;
    c.flags = 0; assert(!rewrite(real,c,fake,500));
    PhaseCache cache;
    assert(cache.phase(16, 1000) == 1000);
    assert(cache.phase(17, 1020) == 1020);
    assert(cache.phase(16, 1040) == 1000); // same phase for an older resend
    assert(cache.phase(16, 2010) == 2010); // old connection / expiry
    assert(cache.phase(16, 100) == 100);   // clock reset
    assert(cache.phase(144, 200) == 200); // hash collision
    Profile profile;
    assert(valid(profile));
    profile.flags = 3; assert(valid(profile)); // previous SPN1 format
    profile.flags = 1u | (2u<<2) | (2u<<4); assert(valid(profile));
    profile.flags = 3u<<2; assert(!valid(profile));
    profile.flags = 0; profile.degrees_per_second = nan; assert(!valid(profile));
    return 0;
}
