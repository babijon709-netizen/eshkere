#include "third_person.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
using namespace thirdperson;
static bool near(float a,float b) { return std::fabs(a-b)<.0001f; }
static float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
int main() {
    Pose p;
    assert(pose({10,2,20},0,0,3,0,p));
    assert(near(p.position.x,10) && near(p.position.y,2) && near(p.position.z,17));
    assert(pose({10,2,20},90,0,3,.5f,p));
    assert(near(p.position.x,7) && near(p.position.z,19.5f));
    assert(pose({10,2,20},0,30,2,-1,p));
    assert(near(p.position.x,9) && near(p.position.y,1));
    for (float yaw : {-180.f,-90.f,0.f,45.f,180.f,720.f})
        for (float pitch : {-90.f,-30.f,0.f,89.f,90.f}) {
            assert(pose({2,3,4},yaw,pitch,3,.7f,p));
            assert(near(dot(p.forward,p.forward),1) && near(dot(p.right,p.right),1) && near(dot(p.up,p.up),1));
            assert(near(dot(p.forward,p.up),0) && near(dot(p.right,p.up),0) && near(dot(p.right,p.forward),0));
            Mat4 m=view(p);
            for (int row=0;row<3;++row) {
                float origin=m.m[row]*p.position.x+m.m[4+row]*p.position.y+m.m[8+row]*p.position.z+m.m[12+row];
                assert(near(origin,0));
                float depth=m.m[row]*p.forward.x+m.m[4+row]*p.forward.y+m.m[8+row]*p.forward.z;
                assert(near(depth,row==2 ? -1.f : 0.f));
            }
            Vec3 delta{p.position.x-2,p.position.y-3,p.position.z-4};
            assert(near(dot(delta,delta),9+.49f));
        }
    Pose base,moved;
    assert(pose({1,2,3},45,20,2,.5f,base));
    assert(pose({11,22,33},45,20,2,.5f,moved));
    assert(near(moved.position.x-base.position.x,10) && near(moved.position.y-base.position.y,20) && near(moved.position.z-base.position.z,30));
    float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
    assert(!pose({nan,0,0},0,0,3,0,p) && !pose({0,0,0},inf,0,3,0,p));
    assert(!pose({0,0,0},0,nan,3,0,p) && !pose({0,0,0},0,91,3,0,p));
    assert(near(distance(nan),2.5f) && near(shoulder(inf),.4f));
    assert(pose({0,0,0},0,0,nan,inf,p));
    assert(near(p.position.z,-2.5f) && near(p.position.x,.4f));
    Profile cfg; assert(valid(cfg)); cfg.flags=1;
    unsigned char raw[16]; std::memcpy(raw,&cfg,16); Profile copy; std::memcpy(&copy,raw,16); assert(valid(copy) && copy.flags==1);
    cfg.flags=2; assert(!valid(cfg)); cfg.flags=0; cfg.back=nan; assert(!valid(cfg));
    cfg=Profile{}; cfg.side=2; assert(!valid(cfg)); cfg=Profile{}; cfg.magic=0; assert(!valid(cfg));
    puts("third-person geometry/profile PASS");
}
