// Production farm scan/liveness against a fake memio transport. This checks
// actual per-component classification and purge code, not a second model.
#include "../../jni/src/game_farm.cpp"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

memio::Reader g_mem;
std::vector<FarmEntity> g_farm_entities;
std::unordered_map<uint64_t, double> g_farm_blacklist;
double g_farm_next_scan = 0;
unsigned g_farm_mask = 15;
bool g_go_name_offset_valid = false;

static constexpr uint64_t kBase = 0x10000, kIdentity = 0x20000, kArray = 0x21000;
static constexpr uint64_t kA = 0x22000, kB = 0x23000, kClass = 0x30000;

bool valid_obj(uint64_t p) { return p >= 0x10000 && p < 0x0001000000000000ULL; }
uint8_t marker_class_of(uint64_t klass) {
    if (klass == kClass) return MARKER_CLASS_MINEABLE;
    if (klass == 0x31000) return MARKER_CLASS_UNKNOWN;
    return MARKER_CLASS_NONE;
}
int read_managed_collection(uint64_t, uint64_t*, int) { return 0; }
bool read_managed_string(uint64_t, char*, size_t) { return false; }
uint64_t managed_object_native(uint64_t managed) {
    return memio::untag(rd<uint64_t>(managed + MANAGED_CACHED_PTR));
}
uint64_t native_component_transform(uint64_t native) { return native ? native + 0x1000 : 0; }
bool marker_world_position(uint64_t transform, Vec3& out) {
    out = {(float)(transform & 0xfff), 1.f, 2.f}; return true;
}
bool read_transform_name(uint64_t, char*, size_t) { return false; }
void collect_transform_subtree(uint64_t root, std::vector<uint64_t>& nodes, size_t) {
    nodes.clear();
    if (root == 0x52000) nodes.push_back(0x51000);
}

class FakeMemory final : public memio::Transport {
public:
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x70000);
    uint64_t fail_start = 0, fail_end = 0;
    bool open(pid_t) override { return true; }
    void close() override {}
    const char* name() const override { return "fake"; }
    ssize_t read_once(uint64_t addr, void* out, size_t len) override {
        if ((fail_start && addr < fail_end && addr + len > fail_start) ||
            addr < kBase || addr + len > kBase + bytes.size()) {
            errno = EIO; return -1;
        }
        std::memcpy(out, bytes.data() + addr - kBase, len);
        return static_cast<ssize_t>(len);
    }
    ssize_t write_once(uint64_t addr, const void* in, size_t len) override {
        if (addr < kBase || addr + len > kBase + bytes.size()) { errno = EIO; return -1; }
        std::memcpy(bytes.data() + addr - kBase, in, len);
        return static_cast<ssize_t>(len);
    }
    template <class T> void put(uint64_t addr, T value) {
        assert(write_once(addr, &value, sizeof(value)) == (ssize_t)sizeof(value));
        g_mem.frame_begin();
    }
    void fail(uint64_t addr, size_t size) { fail_start = addr; fail_end = addr + size; g_mem.frame_begin(); }
    void recover() { fail_start = fail_end = 0; g_mem.frame_begin(); }
};

static std::vector<FarmEntity> scan() {
    std::vector<FarmEntity> stage;
    uint64_t behaviours = 0;
    const int verdict = farm_classify_identity(kIdentity, 0, stage, behaviours);
    assert(verdict == 1 || verdict == 2);
    assert(behaviours == kArray);
    return stage;
}

int main() {
    FakeMemory memory;
    memory.put<uint32_t>(kBase, 0x464c457f); // ELF for memio::Reader bind
    g_mem.set_transport(&memory);
    assert(g_mem.bind(123, kBase));
    memory.put<uint64_t>(kIdentity + MANAGED_CACHED_PTR, 0x40000);
    memory.put<uint64_t>(kIdentity + NETID_BEHAVIOURS, kArray);
    memory.put<int32_t>(kArray + IL2CPP_ARRAY_LENGTH, 2);
    memory.put<uint64_t>(kArray + IL2CPP_ARRAY_FIRST_ELEMENT, kA);
    memory.put<uint64_t>(kArray + IL2CPP_ARRAY_FIRST_ELEMENT + 8, kB);
    for (uint64_t component : {kA, kB}) {
        memory.put<uint64_t>(component, kClass);
        memory.put<uint64_t>(component + MANAGED_CACHED_PTR, 0x41000 + component - kA);
        memory.put<uint64_t>(component + kMineableObjectToHide, 0);
        memory.put<int32_t>(component + MINEABLE_ENTITY_TYPE, (int32_t)MineableEntityType::Stone);
        memory.put<float>(component + MINEABLE_CURRENT_HEALTH, component == kA ? 0.1f : 100.f);
        memory.put<float>(component + MINEABLE_MAX_HEALTH, 1000.f);
        memory.put<float>(component + MINEABLE_FRACTION, 0.f); // unreliable on clients
    }

    float hp = -1, max = -1, fraction = -1;
    assert(farm_node_life(kIdentity, kA, &hp, &max, &fraction) == FarmLife::Alive);
    assert(hp > 0.f && fraction < 0.005f);
    auto nodes = scan();
    assert(nodes.size() == 2 && nodes[0].component == kA && nodes[1].component == kB);
    g_farm_entities = nodes;

    // A failed native-pointer read is unknown, not a destroyed node. Keep its
    // cached entry across a scan so the controller can pause and retry it.
    memory.fail(kA + MANAGED_CACHED_PTR, sizeof(uint64_t));
    assert(farm_node_life(kIdentity, kA) == FarmLife::Unknown);
    nodes = scan();
    assert(nodes.size() == 2 && nodes[0].component == kA && nodes[1].component == kB);
    assert(g_farm_blacklist.empty());
    memory.recover();

    // Unknown HP or an unsynchronised zero fraction cannot prove depletion.
    memory.fail(kA + MINEABLE_CURRENT_HEALTH, sizeof(float));
    assert(farm_node_life(kIdentity, kA, &hp, &max, &fraction) == FarmLife::Alive);
    assert(hp < 0.f);
    memory.recover();
    memory.put<float>(kA + MINEABLE_CURRENT_HEALTH, std::numeric_limits<float>::quiet_NaN());
    assert(farm_node_life(kIdentity, kA, &hp, &max, &fraction) == FarmLife::Alive);
    assert(hp < 0.f);

    // One depleted sibling does not hide or purge the other component.
    memory.put<float>(kA + MINEABLE_CURRENT_HEALTH, 0.f);
    assert(farm_node_life(kIdentity, kA) == FarmLife::Dead);
    nodes = scan();
    assert(nodes.size() == 1 && nodes[0].component == kB);
    g_farm_scan_stage = g_farm_entities;
    farm_purge_entity(kA);
    assert(g_farm_entities.size() == 1 && g_farm_entities[0].component == kB);
    assert(g_farm_scan_stage.size() == 1 && g_farm_scan_stage[0].component == kB);

    // Blacklisting one component likewise leaves the sibling available.
    memory.put<float>(kA + MINEABLE_CURRENT_HEALTH, 5.f);
    g_farm_blacklist[kA] = 500.;
    nodes = scan();
    assert(nodes.size() == 1 && nodes[0].component == kB);
    g_farm_blacklist.clear();
    nodes = scan();
    assert(nodes.size() == 2);

    memory.put<uint64_t>(kA + MANAGED_CACHED_PTR, 0);
    assert(farm_node_life(kIdentity, kA) == FarmLife::Dead);
    nodes = scan();
    assert(nodes.size() == 1 && nodes[0].component == kB);

    // A failed native-pointer read must not make two zeroes equal and falsely
    // authorise a ray that could hit a foreign object.
    FarmEntity ray_node;
    ray_node.identity = kIdentity; ray_node.component = kA;
    ray_node.ext = 0x24000; ray_node.ext_kind = FARM_EXT_ORE;
    const uint64_t marker = 0x25000, collider = 0x26000, hit_collider = 0x27000;
    memory.put<uint64_t>(ray_node.ext + OREHS_MARKER, marker);
    memory.put<uint64_t>(ray_node.ext + OREHS_COLLIDER, collider);
    memory.put<uint64_t>(marker + MANAGED_CACHED_PTR, 0x49000);
    memory.put<uint64_t>(collider + MANAGED_CACHED_PTR, 0);
    memory.put<uint64_t>(hit_collider + MANAGED_CACHED_PTR, 0);
    MeleeReach reach{};
    reach.ray_valid = true; reach.ray_hit_go = 0x28000; reach.ray_collider = hit_collider;
    int why = -1;
    assert(!ray_hit_is_self_node(reach, ray_node, why));
    memory.put<uint64_t>(collider + MANAGED_CACHED_PTR, 0x50000);
    memory.put<uint64_t>(hit_collider + MANAGED_CACHED_PTR, 0x50000);
    assert(ray_hit_is_self_node(reach, ray_node, why) && why == 1);
    memory.put<uint64_t>(marker + MANAGED_CACHED_PTR, 0);
    assert(!ray_hit_is_self_node(reach, ray_node, why));

    // The subtree verdict cache must be keyed by component, not by the shared
    // NetworkIdentity, or one sibling's collider looks like the other's.
    FarmEntity first, second;
    first.identity = second.identity = kIdentity;
    first.component = kA; second.component = kB;
    first.transform = 0x52000; second.transform = 0x53000;
    uint64_t hit_transform = 0; int walked = 0;
    assert(ray_hit_in_node_subtree(reach, first, hit_transform, walked));
    assert(!ray_hit_in_node_subtree(reach, second, hit_transform, walked));
    g_mem.unbind();
    std::puts("farm scan/liveness and sibling isolation: PASS");
}
