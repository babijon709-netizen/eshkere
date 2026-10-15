#pragma once
// wall_check.h — line-of-sight по нативным коллайдерам карты (чистая геометрия, без game.h).
// v1: только BoxCollider. MeshCollider (tag=37) пока не покрыт — см. мешевый зонд в
// world_probe_patch.txt. Раскладка из дампа world v2 (Unity 6000.3, Android arm64);
// перепроверять `xvcen world` после апдейта движка.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace wallchk {

namespace off {                          // нативный Collider
constexpr unsigned kContactOffset = 0x48;   // float == 0.01f у ВСЕХ Collider
constexpr unsigned kIsTrigger     = 0x4C;   // u8 (по статистике дампа: сферы-зоны = 1; не проверено на живом)
constexpr unsigned kEnabled       = 0x4D;   // u8
constexpr unsigned kBoxCenter     = 0x60;   // Vec3 (локальный)
constexpr unsigned kBoxSize       = 0x6C;   // Vec3 (полный размер, не half)
constexpr unsigned kRawBytes      = 0x80;   // сколько читать с начала компонента
}
constexpr uint32_t kMeshTag = 37;           // FILL: MeshCollider (world.log: tag=37, colliders==count) — нужен только мешевому зонду
constexpr uint32_t kBoxTag = 34;            // FILL: индекс типа BoxCollider, стабилен в пределах сборки (world.log: tag=34, colliders==count)

constexpr float kMaxHalfExtent = 60.0F;     // отсеять «пол мира» 10000x150x10000
constexpr float kShrink        = 0.03F;     // грань внутрь: луч вдоль стены/из упора не мигает

struct Box {
    float c[3];       // мировой центр
    float r[3][3];    // r[i][j]: i-я мировая ось, j-я локальная (R); local = R^T * world
    float h[3];       // half extents (с учётом scale, минус kShrink)
    float lo[3], hi[3];   // мировой AABB для грубого отсева
};

// q = (x,y,z,w) как в Unity; R — матрица поворота локальное -> мировое.
inline void quat_to_mat(const float q[4], float r[3][3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    r[0][0] = 1 - 2 * (y * y + z * z); r[0][1] = 2 * (x * y - z * w);     r[0][2] = 2 * (x * z + y * w);
    r[1][0] = 2 * (x * y + z * w);     r[1][1] = 1 - 2 * (x * x + z * z); r[1][2] = 2 * (y * z - x * w);
    r[2][0] = 2 * (x * z - y * w);     r[2][1] = 2 * (y * z + x * w);     r[2][2] = 1 - 2 * (x * x + y * y);
}

enum class Reject { None, Disabled, Trigger, NotFinite, Giant, Degenerate, NotCollider };

// raw — первые kRawBytes байт компонента; pos/q/scale — мировая поза трансформа GameObject.
inline Reject decode_box(const uint8_t* raw, const float pos[3], const float q[4],
                         const float scale[3], Box& out) {
    float contact; std::memcpy(&contact, raw + off::kContactOffset, 4);
    if (!(std::fabs(contact - 0.01F) < 1e-6F)) return Reject::NotCollider;
    if (raw[off::kEnabled] == 0) return Reject::Disabled;
    if (raw[off::kIsTrigger] != 0) return Reject::Trigger;
    float cen[3], size[3];
    std::memcpy(cen, raw + off::kBoxCenter, 12);
    std::memcpy(size, raw + off::kBoxSize, 12);
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(cen[i]) || !std::isfinite(size[i]) || !std::isfinite(pos[i]) ||
            !std::isfinite(scale[i])) return Reject::NotFinite;
    float qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(qn > 0.5F)) return Reject::NotFinite;
    const float qq[4] = {q[0] / qn, q[1] / qn, q[2] / qn, q[3] / qn};
    quat_to_mat(qq, out.r);
    for (int j = 0; j < 3; ++j) {
        out.h[j] = 0.5F * std::fabs(size[j] * scale[j]);
        if (out.h[j] > kMaxHalfExtent) return Reject::Giant;
        out.h[j] -= kShrink;
        if (out.h[j] <= 0.0F) return Reject::Degenerate;
    }
    for (int i = 0; i < 3; ++i) {
        out.c[i] = pos[i] + out.r[i][0] * cen[0] * scale[0] + out.r[i][1] * cen[1] * scale[1] +
                   out.r[i][2] * cen[2] * scale[2];
        const float e = std::fabs(out.r[i][0]) * out.h[0] + std::fabs(out.r[i][1]) * out.h[1] +
                        std::fabs(out.r[i][2]) * out.h[2];
        out.lo[i] = out.c[i] - e;
        out.hi[i] = out.c[i] + e;
    }
    return Reject::None;
}

inline void to_local(const Box& b, const float p[3], float o[3]) {
    const float d[3] = {p[0] - b.c[0], p[1] - b.c[1], p[2] - b.c[2]};
    for (int j = 0; j < 3; ++j) o[j] = b.r[0][j] * d[0] + b.r[1][j] * d[1] + b.r[2][j] * d[2];
}

inline bool contains(const Box& b, const float p[3]) {
    float l[3]; to_local(b, p, l);
    return std::fabs(l[0]) <= b.h[0] && std::fabs(l[1]) <= b.h[1] && std::fabs(l[2]) <= b.h[2];
}

// Отрезок a->b пересекает бокс (slab test в локальных осях).
inline bool segment_hits(const Box& b, const float a[3], const float e[3]) {
    float la[3], le[3]; to_local(b, a, la); to_local(b, e, le);
    float t0 = 0.0F, t1 = 1.0F;
    for (int j = 0; j < 3; ++j) {
        const float d = le[j] - la[j];
        if (std::fabs(d) < 1e-8F) {
            if (std::fabs(la[j]) > b.h[j]) return false;
            continue;
        }
        float ta = (-b.h[j] - la[j]) / d, tb = (b.h[j] - la[j]) / d;
        if (ta > tb) { const float s = ta; ta = tb; tb = s; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    return true;
}

class WallSet {
public:
    void clear() { boxes_.clear(); }
    void swap(std::vector<Box>& other) { boxes_.swap(other); }
    size_t size() const { return boxes_.size(); }

    // true — между a и b стоит блокер. Бокс, внутри которого лежит конец отрезка,
    // не считается (голова цели в косяке, камера в проёме).
    bool blocked(const float a[3], const float b[3]) const {
        float lo[3], hi[3];
        for (int i = 0; i < 3; ++i) { lo[i] = a[i] < b[i] ? a[i] : b[i]; hi[i] = a[i] < b[i] ? b[i] : a[i]; }
        for (const Box& bx : boxes_) {
            if (bx.hi[0] < lo[0] || bx.lo[0] > hi[0] || bx.hi[1] < lo[1] || bx.lo[1] > hi[1] ||
                bx.hi[2] < lo[2] || bx.lo[2] > hi[2]) continue;
            if (contains(bx, a) || contains(bx, b)) continue;
            if (segment_hits(bx, a, b)) return true;
        }
        return false;
    }
private:
    std::vector<Box> boxes_;
};

}  // namespace wallchk
