#pragma once
#include "Vector.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace thirdperson {
inline bool finite(float value) {
    uint32_t bits; std::memcpy(&bits,&value,4);
    return (bits & 0x7f800000u) != 0x7f800000u;
}
inline float distance(float v) { return finite(v) && v >= 1.f && v <= 6.f ? v : 2.5f; }
inline float shoulder(float v) { return finite(v) && v >= -1.5f && v <= 1.5f ? v : .4f; }
struct Pose { Vec3 position{}, forward{}, right{}, up{}; };
inline bool pose(Vec3 eye, float yaw, float pitch, float back, float side, Pose& out) {
    if (!finite(eye.x) || !finite(eye.y) || !finite(eye.z) ||
        !finite(yaw) || !finite(pitch) || std::fabs(yaw)>3600.f || std::fabs(pitch)>90.f) return false;
    const float y=yaw*.017453292519943295f, p=pitch*.017453292519943295f;
    const float sy=std::sin(y), cy=std::cos(y), sp=std::sin(p), cp=std::cos(p);
    out.forward={sy*cp,sp,cy*cp}; out.right={cy,0.f,-sy}; out.up={-sy*sp,cp,-cy*sp};
    back=distance(back); side=shoulder(side);
    out.position={eye.x-out.forward.x*back+out.right.x*side,
                  eye.y-out.forward.y*back,
                  eye.z-out.forward.z*back+out.right.z*side};
    return finite(out.position.x) && finite(out.position.y) && finite(out.position.z);
}
inline Mat4 view(const Pose& p) {
    const Vec3 r=p.right, u=p.up, f=p.forward, c=p.position;
    Mat4 m{};
    m.m[0]=r.x; m.m[4]=r.y; m.m[8]=r.z; m.m[12]=-(r.x*c.x+r.y*c.y+r.z*c.z);
    m.m[1]=u.x; m.m[5]=u.y; m.m[9]=u.z; m.m[13]=-(u.x*c.x+u.y*c.y+u.z*c.z);
    m.m[2]=-f.x; m.m[6]=-f.y; m.m[10]=-f.z; m.m[14]=f.x*c.x+f.y*c.y+f.z*c.z;
    m.m[15]=1.f;
    return m;
}
struct Profile {
    uint32_t magic=0x31505054u, flags=0; // TPP1; bit0 enabled
    float back=2.5f, side=.4f;
};
static_assert(sizeof(Profile)==16,"stable third-person trailer");
inline bool valid(const Profile& p) {
    return p.magic==0x31505054u && (p.flags & ~1u)==0 && finite(p.back) && finite(p.side) &&
        p.back>=1.f && p.back<=6.f && p.side>=-1.5f && p.side<=1.5f;
}
} // namespace thirdperson
