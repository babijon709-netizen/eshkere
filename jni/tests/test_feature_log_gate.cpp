#include "feature_log_gate.h"
#include <cassert>
#include <cstdio>
int main() {
    feature_log::Gate g;
    assert(g.accept(0,0,false));
    assert(!g.accept(0,100,false));
    assert(g.accept(1,101,true));
    unsigned accepted=0;
    for(unsigned i=1;i<=4000;++i)
        if(g.accept(1+(i%2),101+i*0.001,true)) ++accepted;
    assert(accepted<=8); // Flapping every millisecond must not spam the file.
    assert(g.accept(3,110,true));
    assert(!g.accept(3,114.999,true));
    assert(g.accept(3,115,true));
    assert(g.accept(0,116,false));
    assert(!g.accept(0,200,false));
    assert(g.accept(1,1,true)); // Clock rollback cannot silence a new state.
    puts("feature log throttling: ok");
}
