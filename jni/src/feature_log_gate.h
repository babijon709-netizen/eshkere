#pragma once
#include <cstdint>
namespace feature_log {
// Changed states: at most twice a second. Active states: heartbeat every 5 s.
struct Gate {
    uint64_t key=0;
    double last=-1.0;
    bool initialized=false;
    bool accept(uint64_t next,double now,bool active) {
        const bool changed=!initialized || next!=key;
        if(initialized && now>=last && now-last<0.5) return false;
        if(!changed && (!active || (now>=last && now-last<5.0))) return false;
        initialized=true;key=next;last=now;return true;
    }
};
}
