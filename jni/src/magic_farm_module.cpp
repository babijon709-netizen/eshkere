// C++17, in-process Android arm64 companion; release 11991 only.
#include "magic_farm_protocol.h"
#include "magic_farm_profile.h"
#include "magic_farm_hook.h"
#include <cstring>
#include <ctime>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

namespace magicfarm {
namespace {
Shared shared;
std::uintptr_t image{};
using FixedUpdate = void (*)(void*, void*);
FixedUpdate original_update{};
bool installed{};
bool fatal{};
std::uint64_t next_hit{};
using HitAck = void (*)(void*,void*,void*,void*);
HitAck original_ack{};
HitAck original_tree_ack{};
std::uint32_t quiet_node{};
std::uint64_t quiet_until{};
// Named managed layouts verified against dump11991/dump.cs. Native Unity
// structures are validated by the exact method signatures below.
namespace off {
constexpr std::size_t Klass=0, Cached=0x10, ClassName=0x10, Parent=0x58;
constexpr std::size_t ToolPlayer=0xC0, PlayerFp=0x90, CurrentTool=0x50;
constexpr std::size_t PlayerRay=0x88, WorldCamera=0x30, Respawning=0x228;
constexpr std::size_t Reach=0x130, Radius=0x134, Period=0x138, Pause=0x13C;
constexpr std::size_t Purpose=0x168, NodePurpose=0x70, Health=0x78;
constexpr std::size_t Entries=0x18, Count=0x20, ArrayData=0x20, EntrySize=0x18;
constexpr std::size_t EntryKey=8, EntryValue=0x10;
constexpr std::size_t ClassStatics=0xB8, Spawned=0x30, Behaviours=0x80, ArrayLength=0x18;
constexpr std::size_t HitPoint=0x10, HitNormal=0x1C, Attacker=0x28;
constexpr std::size_t Extensions=0xE8, TreeOriginal=0x28, TreeLive=0x50;
constexpr std::size_t MarkerSound=0x50, SoundVolume=0x18, Vtable=0x138;
}
template<class T> T field(const void* object, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(object)+offset, sizeof(value));
    return value;
}
template<class T> void put(void* object, std::size_t offset, const T& value) {
    std::memcpy(static_cast<unsigned char*>(object)+offset, &value, sizeof(value));
}
bool pointer(const void* p) {
    const auto a = reinterpret_cast<std::uintptr_t>(p) & 0x00ffffffffffffffULL;
    return a > 0x10000 && a < 0x800000000000ULL;
}
bool alive(const void* p) { return pointer(p) && pointer(field<void*>(p, off::Cached)); }
bool family(const void* object, const char* expected) {
    if (!pointer(object)) return false;
    void* klass = field<void*>(object, off::Klass);
    for (unsigned i=0; i<32 && pointer(klass); ++i) {
        const char* name=field<const char*>(klass, off::ClassName);
        if (pointer(name) && std::strcmp(name, expected)==0) return true;
        klass=field<void*>(klass, off::Parent);
    }
    return false;
}
std::uint64_t clock_ns() {
    timespec t{}; if (clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec);
}
void state(State s) {
    if (s==State::WrongBuild || s==State::HookFailed || s==State::Exception) fatal=true;
    __atomic_store_n(&shared.state, static_cast<std::uint32_t>(s), __ATOMIC_RELEASE);
}
template<class Fn> Fn method(std::uintptr_t rva) { return reinterpret_cast<Fn>(image+rva); }
bool quiet_hit(void* node) {
    return clock_ns()<quiet_until && alive(node) &&
        method<std::uint32_t (*)(void*,void*)>(profile::NetId)(node,nullptr)==quiet_node;
}
struct Volume {float lo,hi;};
struct MutedSound {
    void* sound{};Volume previous{};bool ok{true};
    explicit MutedSound(void* item) {
        if (!alive(item)) return;
        sound=field<void*>(item,off::MarkerSound);
        if (!sound) return;
        if (!family(sound,"SoundPlayer")) {ok=false;sound=nullptr;return;}
        previous=field<Volume>(sound,off::SoundVolume);
        if (!finite(previous.lo) || !finite(previous.hi)) {ok=false;sound=nullptr;return;}
        put(sound,off::SoundVolume,Volume{});
    }
    ~MutedSound() {if(sound) put(sound,off::SoundVolume,previous);}
};
void tree_ack(void* extension,void* node,void* info,void* metadata) {
    if (!quiet_hit(node)) {original_tree_ack(extension,node,info,metadata);return;}
    MutedSound prefab(field<void*>(extension,off::TreeOriginal));
    MutedSound previous(field<void*>(extension,off::TreeLive));
    if (!prefab.ok || !previous.ok) {state(State::HookFailed);return;}
    original_tree_ack(extension,node,info,metadata);
    // Instantiate can deep-copy the serializable SoundPlayer. Restore that
    // new copy too, after its one-shot sound was created with zero volume.
    auto fresh=field<void*>(extension,off::TreeLive);
    auto sound=alive(fresh)?field<void*>(fresh,off::MarkerSound):nullptr;
    if (prefab.sound && pointer(sound) && sound!=prefab.sound && sound!=previous.sound && family(sound,"SoundPlayer"))
        put(sound,off::SoundVolume,prefab.previous);
}
void hit_ack(void* extension,void* node,void* info,void* metadata) {
    if (quiet_hit(node)) return;
    original_ack(extension,node,info,metadata);
}
bool prepare_tree_ack(void* node) {
    auto collection=field<void*>(node,off::Extensions);
    if (!pointer(collection)) return true;
    auto klass=field<void*>(collection,0);
    const char* name=pointer(klass)?field<const char*>(klass,off::ClassName):nullptr;
    if (!pointer(name)) return false;
    void* array=collection;
    if (std::strcmp(name,"List`1")==0) array=field<void*>(collection,0x10);
    else {
        const auto len=std::strlen(name);
        if (len<2 || name[len-1]!=']') return false;
    }
    if (!pointer(array)) return false;
    const auto count=field<std::uintptr_t>(array,off::ArrayLength);
    if (count>128) return false;
    for (std::size_t i=0;i<count;++i) {
        auto ext=field<void*>(array,off::ArrayData+i*sizeof(void*));
        if (!alive(ext) || !family(ext,"MineableObjectExtension_TreeHitstreaks")) continue;
        auto type=field<unsigned char*>(ext,0);
        auto entry=reinterpret_cast<std::uintptr_t*>(type+off::Vtable+5*16);
        const auto current=__atomic_load_n(entry,__ATOMIC_ACQUIRE);
        if (current==reinterpret_cast<std::uintptr_t>(&tree_ack)) return true;
        const auto expected=image+profile::TreeAck;
        if (current!=expected) return false;
        original_tree_ack=reinterpret_cast<HitAck>(expected);
        auto old=expected;
        return __atomic_compare_exchange_n(entry,&old,reinterpret_cast<std::uintptr_t>(&tree_ack),false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    }
    return true;
}
bool prepare_ack() {
    if (original_ack) return true;
    auto slot=reinterpret_cast<void**>(image+profile::HitMarkersTypeInfo);
    method<void (*)(void*)>(profile::MetadataInit)(slot);
    if (!pointer(*slot)) return false;
    // Class vtable starts at 0x138 in this dump (also independently verified
    // by FPMelee.On_Hit's slot 37 at 0x388); Qo.lOs is virtual slot 5.
    auto entry=reinterpret_cast<std::uintptr_t*>(static_cast<unsigned char*>(*slot)+off::Vtable+5*16);
    const auto expected=image+profile::HitMarkersAck;
    if (__atomic_load_n(entry,__ATOMIC_ACQUIRE)!=expected) return false;
    original_ack=reinterpret_cast<HitAck>(expected);
    auto old=expected;
    if (!__atomic_compare_exchange_n(entry,&old,reinterpret_cast<std::uintptr_t>(&hit_ack),false,__ATOMIC_RELEASE,__ATOMIC_RELAXED)) {
        original_ack=nullptr;return false;
    }
    return true;
}
bool command(Command& out) {
    const auto first=__atomic_load_n(&shared.sequence,__ATOMIC_ACQUIRE);
    if (first&1) return false;
    std::memcpy(&out,&shared.command,sizeof(out));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return first==__atomic_load_n(&shared.sequence,__ATOMIC_ACQUIRE) && valid(out,clock_ns());
}
struct Ray { Vec origin, direction; };
struct Hit { Vec point, normal; std::uint32_t face; float distance; float uv[2]; std::int32_t collider; };
static_assert(sizeof(Ray)==24 && sizeof(Hit)==44);
void* resolve_node(std::uint32_t id) {
    // The client registry is authoritative here. MineableObject's static
    // server registry is not populated reliably on remote clients.
    auto slot=reinterpret_cast<void**>(image+profile::NetworkClientTypeInfo);
    method<void (*)(void*)>(profile::MetadataInit)(slot);
    if (!pointer(*slot)) return nullptr;
    auto statics=field<void*>(*slot,off::ClassStatics);
    auto registry=pointer(statics)?field<void*>(statics,off::Spawned):nullptr;
    if (!pointer(registry)) return nullptr;
    auto entries=field<unsigned char*>(registry,off::Entries);
    const int count=field<int>(registry,off::Count);
    if (!pointer(entries) || count<0 || count>65536) return nullptr;
    const auto length=field<std::uintptr_t>(entries,0x18);
    if (length>65536 || static_cast<unsigned>(count)>length) return nullptr;
    for (int i=0;i<count;++i) {
        auto e=entries+off::ArrayData+std::size_t(i)*off::EntrySize;
        if (field<std::uint32_t>(e,off::EntryKey)!=id) continue;
        auto identity=field<void*>(e,off::EntryValue);
        if (!alive(identity)) continue;
        auto behaviours=field<unsigned char*>(identity,off::Behaviours);
        if (!pointer(behaviours)) continue;
        const auto count=field<std::uintptr_t>(behaviours,off::ArrayLength);
        if (count>64) continue;
        for (std::size_t n=0;n<count;++n) {
            auto node=field<void*>(behaviours,off::ArrayData+n*sizeof(void*));
            if (!alive(node) || !family(node,"MineableObject")) continue;
            if (method<std::uint32_t (*)(void*,void*)>(profile::NetId)(node,nullptr)==id) return node;
        }
    }
    return nullptr;
}
void tick(void* tool) {
    if (fatal) return;
    Command c{};
    if (!command(c)) { state(State::Idle); return; }
    if (!alive(tool)) return;
    void* player=field<void*>(tool,off::ToolPlayer);
    if (!alive(player) || field<bool>(player,off::Respawning) ||
        !method<bool (*)(void*,void*)>(profile::IsLocal)(player,nullptr)) return;
    auto fp=field<void*>(player,off::PlayerFp);
    if (!alive(fp) || field<void*>(fp,off::CurrentTool)!=tool) return;
    if (!family(tool,"FPTool")) { state(State::NoTool); return; }
    shared.last_net_id=c.net_id;
    auto node=resolve_node(c.net_id);
    if (!node) { state(State::NoTarget); return; }
    const float hp=field<float>(node,off::Health);
    if (!finite(hp) || hp<=0) { state(State::NoTarget); return; }
    const auto required=field<std::uint32_t>(node,off::NodePurpose);
    if (required && !(required&field<std::uint32_t>(tool,off::Purpose))) {
        state(State::NoTool); return;
    }
    auto rayman=field<void*>(player,off::PlayerRay);
    auto camera=alive(rayman)?field<void*>(rayman,off::WorldCamera):nullptr;
    if (!alive(camera)) { state(State::NoTool); return; }
    auto transform=method<void* (*)(void*,void*)>(profile::Transform);
    const auto eye=method<Vec (*)(void*,void*)>(profile::Position)(transform(camera,nullptr),nullptr);
    Vec d{c.point.x-eye.x,c.point.y-eye.y,c.point.z-eye.z};
    const float distance=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    const float reach=field<float>(tool,off::Reach)+field<float>(tool,off::Radius);
    const float period=field<float>(tool,off::Period)+field<float>(tool,off::Pause);
    if (!finite(distance) || distance<0.01f || !finite(reach) || reach<0.2f || reach>20 ||
        !finite(period) || period<0.05f || period>10) { state(State::NoTool); return; }
    d.x/=distance; d.y/=distance; d.z/=distance;
    Hit hit{};
    // Cast independently of the camera. Use the same mask as the actual
    // RaycastManager, including intervening structures; never harvest through walls.
    const int mask=field<int>(rayman,0x48);
    const bool hit_any=method<bool (*)(Ray,Hit*,float,int,int,void*)>(profile::Raycast)(
        Ray{eye,d},&hit,reach,mask,1,nullptr);
    if (!hit_any) { state(State::OutOfReach); return; }
    auto collider=method<void* (*)(Hit*,void*)>(profile::Collider)(&hit,nullptr);
    if (!alive(collider)) { state(State::Blocked); return; }
    auto ct=transform(collider,nullptr), nt=transform(node,nullptr);
    if (!alive(ct) || !alive(nt) ||
        !method<bool (*)(void*,void*,void*)>(profile::IsChildOf)(ct,nt,nullptr)) {
        state(State::Blocked); return;
    }
    shared.hit_distance=hit.distance;
    const auto now=clock_ns();
    if (now<next_hit) return;
    if (!prepare_ack() || !prepare_tree_ack(node)) { state(State::HookFailed);return; }
    auto slot=reinterpret_cast<void**>(image+profile::CompactTypeInfo);
    method<void (*)(void*)>(profile::MetadataInit)(slot);
    if (!pointer(*slot)) { state(State::WrongBuild); return; }
    void* info=method<void* (*)(void*)>(profile::ObjectNew)(*slot);
    if (!pointer(info)) { state(State::Exception); return; }
    method<void (*)(void*,void*)>(profile::CompactCtor)(info,nullptr);
    put(info,off::HitPoint,hit.point); put(info,off::HitNormal,hit.normal); put(info,off::Attacker,eye);
    // This is the normal client-side extension processing (including critical
    // hit-streak calculation), followed by the existing server command. Do not
    // call FPMelee.On_Hit, On_Woosh, TryAttack or change camera/input fields.
    method<void (*)(void*,void*,void*)>(profile::NodeHit)(node,info,nullptr);
    auto interaction=method<void* (*)(void*,void*)>(profile::Interaction)(player,nullptr);
    if (!alive(interaction) || !family(interaction,"PlayerInteraction")) {
        state(State::NoTool); return;
    }
    // Recheck the lease immediately before the network call, after allocations.
    Command latest{};
    if (!command(latest) || latest.net_id!=c.net_id) return;
    quiet_node=c.net_id;quiet_until=now+2000000000ULL;
    method<void (*)(void*,std::uint32_t,void*,void*)>(profile::SendHit)(interaction,c.net_id,info,nullptr);
    next_hit=now+static_cast<std::uint64_t>(period*1000000000.0)+20000000ULL;
    ++shared.sent; shared.last_net_id=c.net_id; state(State::Sent);
}
void update(void* self,void* metadata) {
    original_update(self,metadata);
    try { tick(self); }
    catch (...) { next_hit=clock_ns()+1000000000ULL; state(State::Exception); }
}
bool in_image(std::uintptr_t rva,std::size_t size) {
    struct Query { std::uintptr_t base,rva;std::size_t size;bool valid; } q{image,rva,size,false};
    dl_iterate_phdr([](dl_phdr_info* p,std::size_t,void* raw) {
        auto& q=*static_cast<Query*>(raw);
        if (p->dlpi_addr!=q.base) return 0;
        for (unsigned i=0;i<p->dlpi_phnum;++i) {
            const auto& h=p->dlpi_phdr[i];
            if (h.p_type==PT_LOAD && (h.p_flags&PF_X) && q.rva>=h.p_vaddr &&
                q.rva+q.size<=h.p_vaddr+h.p_memsz) q.valid=true;
        }
        return 1;
    },&q);
    return q.valid;
}
bool fingerprint() {
#define CHECK(name) if (!in_image(profile::name,16) || std::memcmp(reinterpret_cast<void*>(image+profile::name),profile::name##Bytes,16)) return false
    CHECK(FixedUpdate); CHECK(MetadataInit); CHECK(ObjectNew); CHECK(CompactCtor);
    CHECK(NodeHit); CHECK(Interaction); CHECK(SendHit); CHECK(IsLocal);
    CHECK(NetId); CHECK(Raycast); CHECK(Collider); CHECK(Transform); CHECK(Position); CHECK(IsChildOf); CHECK(HitMarkersAck); CHECK(TreeAck);
#undef CHECK
    return true;
}
bool install() {
    const long page=sysconf(_SC_PAGESIZE);
    if (page<4096 || (page&(page-1))) return false;
    auto code=reinterpret_cast<std::uint32_t*>(image+profile::FixedUpdate);
    std::uint32_t relocated[12]{};
    if (!trampoline_11991(code,reinterpret_cast<std::uintptr_t>(code),relocated)) return false;
    void* tramp=mmap(nullptr,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (tramp==MAP_FAILED) return false;
    std::memcpy(tramp,relocated,sizeof(relocated));
    __builtin___clear_cache(static_cast<char*>(tramp),static_cast<char*>(tramp)+sizeof(relocated));
    if (mprotect(tramp,page,PROT_READ|PROT_EXEC)) { munmap(tramp,page);return false; }
    auto start=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(code)&~std::uintptr_t(page-1));
    if (mprotect(start,page,PROT_READ|PROT_WRITE|PROT_EXEC)) { munmap(tramp,page);return false; }
    original_update=reinterpret_cast<FixedUpdate>(tramp);
    std::uint32_t patch[4]{0x58000051u,0xd61f0220u,0,0};
    auto target=reinterpret_cast<std::uintptr_t>(&update);
    std::memcpy(patch+2,&target,sizeof(target));
    std::memcpy(code,patch,sizeof(patch));
    __builtin___clear_cache(reinterpret_cast<char*>(code),reinterpret_cast<char*>(code)+sizeof(patch));
    if (mprotect(start,page,PROT_READ|PROT_EXEC)) {
        std::memcpy(code,profile::FixedUpdateBytes,16);
        __builtin___clear_cache(reinterpret_cast<char*>(code),reinterpret_cast<char*>(code)+16);
        (void)mprotect(start,page,PROT_READ|PROT_EXEC);
        original_update=nullptr;munmap(tramp,page);return false;
    }
    installed=true;return true;
}
} // namespace
} // namespace magicfarm

extern "C" __attribute__((visibility("default"))) std::uintptr_t benzware_magic_attach(std::uintptr_t base) {
    using namespace magicfarm;
    if (installed) return base==image?reinterpret_cast<std::uintptr_t>(&shared):0;
    image=base;
    if (!base || !fingerprint()) state(State::WrongBuild);
    else if (!install()) state(State::HookFailed);
    else state(State::Ready);
    return reinterpret_cast<std::uintptr_t>(&shared);
}
