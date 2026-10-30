// Host tests for maps readiness and diagnostic loader detection.
#include "runtime_guard.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {
const char* kLoader =
    "7000000000-7000001000 r--p 00000000 fd:00 10 /data/app/lib/arm64/libloader.so\n"
    "7000001000-7000002000 r-xp 00001000 fd:00 10 /data/app/lib/arm64/libloader.so\n";
const char* kLoaderDeleted =
    "7000000000-7000001000 r-xp 00000000 fd:00 10 /data/app/lib/arm64/libloader.so (deleted)\n";
const char* kLoaderApk =
    "7000000000-7000001000 r-xp 00000000 fd:00 10 /data/app/split_config.arm64_v8a.apk!/lib/arm64-v8a/libloader.so\n";
const char* kClear =
    "7000000000-7000001000 r-xp 00000000 fd:00 11 /data/app/lib/arm64/libil2cpp.so\n"
    "7000001000-7000002000 r-xp 00000000 fd:00 12 /data/app/lib/arm64/libunity.so\n";
const char* kLookalikes =
    "7000000000-7000001000 r-xp 00000000 fd:00 13 /data/app/lib/arm64/mylibloader.so\n"
    "7000001000-7000002000 r-xp 00000000 fd:00 14 /data/app/lib/arm64/libloader.so.bak\n"
    "7000002000-7000003000 r-xp 00000000 fd:00 15 /data/app/lib/arm64/libloaderXso\n";
} // namespace

int main() {
    assert(!runtime_guard::snapshot_allows_writes(runtime_guard::Policy::Unknown));
    assert(runtime_guard::snapshot_allows_writes(runtime_guard::Policy::Clear));
    assert(runtime_guard::snapshot_allows_writes(runtime_guard::Policy::Protected));
    assert(!runtime_guard::writes_allowed(-1));
    assert(runtime_guard::maps_have_loader(kLoader));
    assert(runtime_guard::maps_have_loader(kLoaderDeleted));
    assert(runtime_guard::maps_have_loader(kLoaderApk));
    assert(!runtime_guard::maps_have_loader(kClear));
    assert(!runtime_guard::maps_have_loader(kLookalikes));
    assert(!runtime_guard::maps_have_loader(""));

    runtime_guard::reset();
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Unknown);
    assert(!runtime_guard::is_protected(100));
#ifdef __ANDROID__
    assert(!runtime_guard::writes_allowed(100));
#endif

    runtime_guard::observe_maps(100, kClear);
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Clear);
    assert(runtime_guard::writes_allowed(100));
    assert(runtime_guard::policy(101) == runtime_guard::Policy::Unknown);
#ifdef __ANDROID__
    assert(!runtime_guard::writes_allowed(101));
#endif

    runtime_guard::observe_maps(100, kLoader);
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Protected);
    assert(runtime_guard::is_protected(100));
    assert(runtime_guard::writes_allowed(100));
    assert(runtime_guard::snapshot_allows_writes(runtime_guard::policy(100)));
    assert(!runtime_guard::snapshot_allows_writes(runtime_guard::policy(101)));
    assert(!runtime_guard::is_protected(101));

    // A failed/truncated snapshot must not downgrade a known protected client
    // to clear, and must not certify an unknown client as clear.
    runtime_guard::observe_maps(100, "", false);
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Unknown);
    assert(!runtime_guard::is_protected(100));
    runtime_guard::observe_maps(100, kLoader);
    runtime_guard::observe_maps(100, std::string(kLoader) + std::string(8u << 20, 'x'), false);
    assert(runtime_guard::policy(100) == runtime_guard::Policy::Unknown);

    runtime_guard::observe_maps(101, kLoaderDeleted);
    assert(runtime_guard::is_protected(101));
    assert(!runtime_guard::is_protected(100));

    assert(!runtime_guard::refresh_proc(-1));
    runtime_guard::reset();
    assert(runtime_guard::policy(101) == runtime_guard::Policy::Unknown);
    std::puts("maps readiness / loader diagnostic policy: PASS");
}
