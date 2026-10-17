#include "game_offsets_active.h"

#include <cassert>

int main() {
    using go_active::GameVersion;

    assert(go_active::selected_version == GameVersion::Release);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xDEA5540);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);

    go_active::Select(GameVersion::Beta);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xE62CB98);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);

    go_active::Select(GameVersion::Release);
    assert(go_active::WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA == 0xDEA5540);
    assert(go_active::WORLD_STREAMING_GRASS_ENABLED_FIELD == 0x18);
    assert(go_active::WORLD_STREAMING_TREES_ENABLED_FIELD == 0x19);
    return 0;
}
