#include "magic_tools_memory.h"
#include <cassert>
#include <map>
#include <string>
#include <limits>
#include <cstdio>

struct FakeIO {
    std::map<uint64_t,uint8_t> mem;
    std::map<uint64_t,std::string> types;
    uint64_t fail_read=0, fail_write=0;
    unsigned writes=0;
    template<class T> void put(uint64_t a,T v) {
        auto* b=reinterpret_cast<const uint8_t*>(&v);
        for(size_t i=0;i<sizeof(T);++i) mem[a+i]=b[i];
    }
    template<class T> bool read(uint64_t a,T& v) {
        if(a==fail_read) return false;
        auto* b=reinterpret_cast<uint8_t*>(&v);
        for(size_t i=0;i<sizeof(T);++i) { auto it=mem.find(a+i);if(it==mem.end())return false;b[i]=it->second; }
        return true;
    }
    template<class T> bool write_heap(uint64_t a,T v) {
        if(a==fail_write)return false;
        ++writes;put(a,v);return true;
    }
    bool exact(uint64_t a,const char* n,const char*) { return types[a]==n; }
    bool named(uint64_t a,const char* n,const char* ns) { return exact(a,n,ns); }
    void obj(uint64_t a,const char* n) {
        types[a]=n;put<uint64_t>(a,a+0x800000);put<uint64_t>(a+go_active::MANAGED_CACHED_PTR,a+0x900000);
    }
    template<class T> T get(uint64_t a) { T v{};assert(read(a,v));return v; }
};
constexpr uint64_t p=0x10000,n=0x20000,fp=0x30000,t=0x40000,r=0x50000;
struct Rig {
    FakeIO io;magic_tools::Controller<FakeIO> c{io};
    Rig() {
        using namespace go_active;
        io.obj(p,"PlayerManager");io.obj(n,"NetworkIdentity");io.obj(fp,"FPManager");io.obj(t,"FPTool");io.obj(r,"RaycastManager");
        io.put(p+NETWORK_BEHAVIOUR_IDENTITY,n);io.put<uint8_t>(n+NETWORK_IDENTITY_IS_LOCAL_PLAYER,1);
        io.put(p+PLAYER_FP_MANAGER,fp);io.put(fp+FPMANAGER_CURRENT_OBJECT,t);
        io.put(t+FPOBJECT_PLAYER_BACKREF,p);io.put(t+FPOBJECT_RAYCAST_MANAGER,r);
        io.put<int>(t+FPTOOL_TOOL_PURPOSES,2);io.put(r+RAYCASTMAN_PLAYER,p);
        io.put<float>(t+FPMELEE_MAX_REACH,1.5f);io.put<float>(r+RAYCASTMAN_RAY_LENGTH,1.5f);
        io.put<float>(r+RAYCASTMAN_AIM_RAY_LENGTH,1.5f);io.put<uint8_t>(t+FPTOOL_ALWAYS_CRIT,0);
    }
    void tick(bool on=true) { c.tick(1,0x100000,p,on); }
};
int main() {
    using namespace go_active;
    { Rig a;a.tick(false);assert(a.io.writes==0);a.tick();assert(a.c.ready());assert(a.io.writes==4);assert(a.c.diagnostics().write_success==4);
      assert(a.io.get<float>(t+FPMELEE_MAX_REACH)==7.5f);
      assert(a.io.get<uint8_t>(t+FPTOOL_ALWAYS_CRIT)==1);
      a.tick();assert(a.io.writes==4);a.tick(false);assert(!a.c.has_state());
      assert(a.io.get<float>(r+RAYCASTMAN_RAY_LENGTH)==1.5f);assert(a.io.get<uint8_t>(t+FPTOOL_ALWAYS_CRIT)==0); }
    { Rig a;a.io.types[t]="FPHitscan";a.tick();assert(!a.c.ready() && a.io.writes==0);assert(a.c.stage()==20);assert(a.c.diagnostics().tool==t); }
    { Rig a;a.io.fail_read=fp+FPMANAGER_CURRENT_OBJECT;a.tick();assert(a.io.writes==0);assert(a.c.stage()==19);assert(a.c.diagnostics().address==a.io.fail_read);assert(a.c.diagnostics().read_failures==1); }
    { Rig a;a.io.put<float>(r+RAYCASTMAN_AIM_RAY_LENGTH,std::numeric_limits<float>::quiet_NaN());a.tick();assert(a.io.writes==0); }
    { Rig a;a.tick();a.io.put<float>(t+FPMELEE_MAX_REACH,4.f);a.tick(false);
      assert(a.io.get<float>(t+FPMELEE_MAX_REACH)==4.f);assert(!a.c.has_state()); }
    { Rig a;a.tick();a.io.fail_read=t+FPMELEE_MAX_REACH;a.tick(false);assert(a.c.has_state());
      a.io.fail_read=0;a.tick(false);assert(!a.c.has_state());assert(a.io.get<float>(t+FPMELEE_MAX_REACH)==1.5f); }
    { Rig a;a.tick();a.io.put<uint64_t>(t+MANAGED_CACHED_PTR,0);unsigned old=a.io.writes;a.tick(false);
      assert(a.io.get<float>(t+FPMELEE_MAX_REACH)==7.5f);assert(a.io.writes==old+2);assert(!a.c.has_state()); }
    { Rig a;a.io.fail_write=r+RAYCASTMAN_RAY_LENGTH;a.tick();assert(!a.c.ready());
      a.io.fail_write=0;a.tick(false);assert(!a.c.has_state());assert(a.io.get<float>(t+FPMELEE_MAX_REACH)==1.5f); }
    { Rig a;a.tick();unsigned old=a.io.writes;a.c.tick(2,0x100000,0,false);assert(a.io.writes==old);assert(!a.c.has_state()); }
    // Regression: the live FPTool from attach.log has Everything = 0xffffffff.
    { Rig a;a.io.put<int32_t>(t+FPTOOL_TOOL_PURPOSES,-1);a.tick();
      assert(a.c.ready() && a.io.writes==4);assert(a.c.diagnostics().purposes==-1);
      a.tick(false);assert(!a.c.has_state());
      assert(a.io.get<float>(t+FPMELEE_MAX_REACH)==1.5f); }
    for(int32_t invalid : {0,-2,8,9,static_cast<int32_t>(0x80000000u)}) {
      Rig a;a.io.put<int32_t>(t+FPTOOL_TOOL_PURPOSES,invalid);a.tick();
      assert(!a.c.ready() && a.c.stage()==4 && a.io.writes==0);
    }
    assert(farm_tool::effective_purposes(-1)==7);
    assert((farm_tool::effective_purposes(-1)&2)!=0);
    assert(farm_tool::effective_purposes(1)==1);
    assert(farm_tool::effective_purposes(2)==2);
    assert(farm_tool::effective_purposes(3)==3);
    assert(farm_tool::effective_purposes(4)==4);
    std::puts("magic tools lifecycle / Everything-mask regression: ok");
}
