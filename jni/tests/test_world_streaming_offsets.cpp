#include "game_offsets_active.h"

#include <cassert>

int main() {
    using go_active::GameVersion;

    assert(go_active::selected_version == GameVersion::Release);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xDD80038);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);

    go_active::Select(GameVersion::Beta);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xDEA23B0);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);

    go_active::Select(GameVersion::Release);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xDD80038);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    return 0;
}
