#pragma once
#include "weapon_tuning_memory.h"
#include "tool_purpose.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace magic_tools {
// Donor 125x: FPMelee reach 7.5, RaycastManager lengths 7.5.
// Use the selected game profile; no donor process addresses are reused.
inline constexpr float kReach = 7.5f;
inline constexpr float kFarmRadius = 6.f;

struct Diagnostics {
    uint64_t player=0, fp=0, tool=0, ray=0, address=0;
    uint64_t reads=0, read_failures=0, writes=0, write_success=0, restores=0;
    uint32_t actual=0, expected=0;
    unsigned size=0;
    float reach[3]{};
    int critical=-1;
    int purposes=0;
    const char* operation="none";
};
inline const char* stage_name(int n) {
    switch(n) {
    case 0:return "ready/off";case 1:return "player-chain";case 2:return "fp-manager";
    case 3:return "active-tool";case 4:return "tool-purposes";case 5:return "ray-manager";
    case 6:return "revalidate/read";case 7:return "write/readback";case 8:return "restore-pending";
    case 9:return "reach-layout";case 10:return "crit-layout";
    case 11:return "player-class";case 12:return "player-identity";case 13:return "network-pointer";
    case 14:return "network-class";case 15:return "local-player-flag";case 16:return "fp-pointer";
    case 17:return "fp-class";case 18:return "fp-identity";case 19:return "tool-pointer";
    case 20:return "tool-class";case 21:return "tool-identity";case 22:return "tool-backlink";
    case 23:return "ray-pointer";case 24:return "ray-identity";case 25:return "ray-backlink";
    default:return "unknown";
    }
}
template<class T> struct Saved { T original{}, last{}; bool owned=false; };
struct Context {
    weapon_tuning::Identity player, fp, tool, ray;
    bool operator==(const Context& b) const {
        return player==b.player && fp==b.fp && tool==b.tool && ray==b.ray;
    }
};

template<class IO> class Controller {
    IO& io;
    Context ctx{};
    Diagnostics diag{};
    template<class T> bool read(uint64_t a,T& v) {
        ++diag.reads;
        if(io.read(a,v)) return true;
        ++diag.read_failures;diag.operation="read-failed";diag.address=a;diag.size=sizeof(T);
        return false;
    }
    template<class T> bool write(uint64_t a,const T& value,bool restoring=false) {
        ++diag.writes;if(restoring) ++diag.restores;
        diag.address=a;diag.size=sizeof(T);diag.expected=0;
        std::memcpy(&diag.expected,&value,std::min(sizeof(T),sizeof(diag.expected)));
        diag.operation=restoring?"restore-failed":"write-failed";
        if(!io.write_heap(a,value)) return false;
        ++diag.write_success;diag.operation=restoring?"restore-ok":"write-ok";return true;
    }
    template<class T> bool readback(uint64_t a,T& now,const T& wanted) {
        if(!read(a,now)) return false;
        diag.actual=0;std::memcpy(&diag.actual,&now,std::min(sizeof(T),sizeof(diag.actual)));
        if(now!=wanted) { diag.operation="readback-mismatch";diag.address=a;diag.size=sizeof(T);return false; }
        return true;
    }
    std::array<Saved<float>,3> fields{};
    Saved<uint8_t> crit{};
    uint64_t session=0, base=0;
    int stage_=0;
    bool ready_=false;
    bool fail(int n) { stage_=n; ready_=false; return false; }
    bool ptr(uint64_t a,uint64_t& p) { if(!read(a,p)) return false;p&=0x00ffffffffffffffULL;return p>=0x10000; }
    bool resolve(uint64_t player, Context& c) {
        using namespace go_active;
        uint64_t net=0, fp=0, tool=0, ray=0, back=0;
        uint8_t local=0;
        diag.player=player;diag.fp=diag.tool=diag.ray=0;diag.purposes=0;
        if(!io.exact(player,"PlayerManager","Oxide")) return fail(11);
        if(!c.player.capture(io,player,true)) return fail(12);
        if(!ptr(player+NETWORK_BEHAVIOUR_IDENTITY,net)) return fail(13);
        if(!io.named(net,"NetworkIdentity","Mirror")) return fail(14);
        if(!read(net+NETWORK_IDENTITY_IS_LOCAL_PLAYER,local) || local!=1) return fail(15);
        if(!ptr(player+PLAYER_FP_MANAGER,fp)) return fail(16);
        diag.fp=fp;
        if(!io.exact(fp,"FPManager","Oxide")) return fail(17);
        if(!c.fp.capture(io,fp,true)) return fail(18);
        if(!ptr(fp+FPMANAGER_CURRENT_OBJECT,tool)) return fail(19);
        diag.tool=tool;
        if(!io.exact(tool,"FPTool","Oxide")) return fail(20);
        if(!c.tool.capture(io,tool,true)) return fail(21);
        if(!ptr(tool+FPOBJECT_PLAYER_BACKREF,back) || back!=player) return fail(22);
        int purposes=0;
        if(!read(tool+FPTOOL_TOOL_PURPOSES,purposes)) return fail(4);
        diag.purposes=purposes;
        if(!farm_tool::valid_purposes(purposes)) return fail(4);
        if(!ptr(tool+FPOBJECT_RAYCAST_MANAGER,ray)) return fail(23);
        diag.ray=ray;
        if(!c.ray.capture(io,ray,true)) return fail(24);
        if(!ptr(ray+RAYCASTMAN_PLAYER,back) || back!=player) return fail(25);
        return true;
    }
    uint64_t addr(size_t i) const {
        if(i==0) return ctx.tool.object+go_active::FPMELEE_MAX_REACH;
        return ctx.ray.object+(i==1?go_active::RAYCASTMAN_RAY_LENGTH:go_active::RAYCASTMAN_AIM_RAY_LENGTH);
    }
    bool fresh(uint64_t player) {
        Context c;
        return resolve(player,c) && c==ctx;
    }
    template<class T> bool restore_one(const weapon_tuning::Identity& owner,uint64_t a,Saved<T>& s) {
        if(!s.owned) return true;
        const auto life=owner.life(io);
        if(life==weapon_tuning::Life::Unreadable) return false;
        if(life==weapon_tuning::Life::Gone) { s={}; return true; }
        T value{};
        if(!read(a,value)) return false;
        // Another writer owns a different value. Never restore over it.
        if(value==s.last && value!=s.original) {
            if(owner.life(io)!=weapon_tuning::Life::Alive || !write(a,s.original,true) ||
                !readback(a,value,s.original)) return false;
        }
        s={}; return true;
    }
    bool restore() {
        bool ok=true;
        for(size_t i=0;i<fields.size();++i)
            if(!restore_one(i==0?ctx.tool:ctx.ray,addr(i),fields[i])) ok=false;
        if(!restore_one(ctx.tool,ctx.tool.object+go_active::FPTOOL_ALWAYS_CRIT,crit)) ok=false;
        return ok;
    }
    template<class T> bool apply(uint64_t player,uint64_t a,T target,Saved<T>& s) {
        T now{};
        if(!fresh(player) || !read(a,now)) return fail(6);
        if(now==target) return true;
        if(!s.owned || now!=s.last) s.original=now;
        s.last=target; s.owned=true;
        if(!write(a,target) || !readback(a,now,target)) return fail(7);
        return true;
    }
public:
    explicit Controller(IO& memory):io(memory){}
    const Diagnostics& diagnostics() const { return diag; }
    bool ready() const { return ready_; }
    int stage() const { return stage_; }
    bool has_state() const {
        if(crit.owned) return true;
        for(const auto& s:fields) if(s.owned) return true;
        return false;
    }
    bool reset(bool restore_originals) {
        bool ok=!restore_originals || restore();
        fields={};crit={};ctx={};session=base=0;ready_=false;stage_=0;return ok;
    }
    void tick(uint64_t epoch,uint64_t library,uint64_t player,bool enabled) {
        ready_=false;
        if(epoch!=session || library!=base) { reset(false);session=epoch;base=library; }
        if(!enabled) { if(!restore()) fail(8); return; }
        if(!player || !library) { fail(1);return; }
        Context c;
        if(!resolve(player,c)) {
            // Unreadable active objects are not proof of destruction.
            restore();return;
        }
        if(!(c==ctx)) {
            if(!restore()) { fail(8);return; }
            ctx=c;
        }
        // Validate the whole layout before the first write.
        float now[3]{};uint8_t critical=0;
        for(size_t i=0;i<3;++i) {
            if(!read(addr(i),now[i])) { fail(9);return; }
            diag.reach[i]=now[i];
            if(!viewmodel::finite(now[i]) || now[i]<=0.f || now[i]>100.f) {
                diag.operation="layout-invalid";diag.address=addr(i);diag.size=4;
                std::memcpy(&diag.actual,&now[i],4);fail(9);return;
            }
        }
        if(!read(ctx.tool.object+go_active::FPTOOL_ALWAYS_CRIT,critical)) { fail(10);return; }
        diag.critical=critical;
        if(critical>1) { fail(10);return; }
        for(size_t i=0;i<3;++i) {
            const float target=std::max(now[i],kReach);
            if(!apply(player,addr(i),target,fields[i])) return;
            diag.reach[i]=target;
        }
        if(!apply(player,ctx.tool.object+go_active::FPTOOL_ALWAYS_CRIT,uint8_t{1},crit)) return;
        diag.critical=1;ready_=true;stage_=0;
    }
};
}
