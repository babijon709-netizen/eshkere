#include "weapon_skin_memory.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>

struct FakeIO {
    std::map<uint64_t, uint8_t> bytes;
    std::map<uint64_t, std::pair<std::string, std::string>> types;
    int writes = 0;
    int refresh_calls = 0;
    uint64_t refreshed_manager = 0;
    uint64_t refreshed_piece = 0;
    int32_t refreshed_slot = -1;
    uint64_t refreshed_rva = 0;
    int16_t refreshed_skin = -1;

    bool reload_weapon_visual(uint64_t manager, uint64_t piece, int32_t slot,
                              uint64_t rva, int16_t skin) {
        ++refresh_calls;
        refreshed_manager = manager;
        refreshed_piece = piece;
        refreshed_slot = slot;
        refreshed_rva = rva;
        refreshed_skin = skin;
        return true;
    }

    template<class T> void put(uint64_t address, const T& value) {
        const auto* src = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) bytes[address + i] = src[i];
    }
    template<class T> bool read(uint64_t address, T& value) {
        auto* dst = reinterpret_cast<uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(T); ++i) {
            auto it = bytes.find(address + i);
            if (it == bytes.end()) return false;
            dst[i] = it->second;
        }
        return true;
    }
    uint64_t ptr(uint64_t address) {
        uint64_t value = 0;
        return read(address, value) ? value : 0;
    }
    bool named(uint64_t object, const char* name, const char* ns) {
        auto it = types.find(object);
        return it != types.end() && it->second.first == name && it->second.second == ns;
    }
    template<class T> bool write_heap(uint64_t address, const T& value) {
        ++writes; put(address, value); return true;
    }
};

int main() {
    using namespace go_active;
    constexpr uint64_t player = 0x100000;
    constexpr uint64_t weapon = 0x200000;
    constexpr uint64_t item = 0x300000;
    constexpr uint64_t data = 0x400000;
    constexpr uint64_t array = 0x500000;
    constexpr uint64_t skin0 = 0x600000;
    constexpr uint64_t skin1 = 0x610000;
    constexpr uint64_t manager = 0x700000;
    constexpr uint64_t fp_view = 0x800000;

    FakeIO io;
    io.types[weapon] = {"PlayerWeapon", "HyperHug.Games.Oxide.Features.Weapons"};
    io.types[item] = {"Item", "Oxide"};
    io.types[data] = {"ItemData", "Oxide"};
    io.types[manager] = {"FPManager", ""};
    io.types[fp_view] = {"PlayerFPWeaponView", ""};
    io.put(player + PLAYER_WEAPON_REFERENCE, weapon);
    io.put(weapon + PLAYERWEAPON_PLAYER_BACKREF, player);
    io.put(weapon + PLAYERWEAPON_ITEM, item);
    io.put(weapon + PLAYERWEAPON_SKIN_REFRESH_SLOT, int32_t(1));
    io.put(weapon + PLAYERWEAPON_FP_MANAGER, manager);
    io.put(weapon + PLAYERWEAPON_FP_VIEW, fp_view);
    io.put(item + ITEM_DATA, data);
    io.put(item + ITEM_SKIN, int16_t(-1));
    io.put(data + ITEMDATA_SKIN_DATA, array);
    io.put(array + IL2CPP_ARRAY_LENGTH, int32_t(2));
    io.put(array + IL2CPP_ARRAY_FIRST_ELEMENT, skin0);
    io.put(array + IL2CPP_ARRAY_FIRST_ELEMENT + 8, skin1);
    io.put(weapon + PLAYERWEAPON_PIECE + WEAPONPIECE_SKIN, int16_t(-1));
    io.put(weapon + PLAYERWEAPON_CACHED_PIECE + WEAPONPIECE_SKIN, int16_t(-1));
    io.put(manager + FPMANAGER_UZL + WEAPONPIECE_SKIN, int16_t(-1));
    io.put(fp_view + PLAYERFPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN, int16_t(-1));

    const uint64_t managed_name = 0x900000;
    io.put(managed_name + IL2CPP_STRING_LENGTH, int32_t(5));
    const char16_t name[] = u"AK-47";
    for (uint64_t i = 0; i < 5; ++i)
        io.put(managed_name + IL2CPP_STRING_CHARS + i * 2, uint16_t(name[i]));
    char decoded[16]{};
    assert(weapon_skin::read_managed_string(io, managed_name, decoded, sizeof(decoded)));
    assert(std::string(decoded) == "AK-47");

    weapon_skin::Controller<FakeIO> ctl(io);
    ctl.tick(1, player, true, 1, true);
    const auto& info = ctl.snapshot();
    assert(info.player_found && info.weapon_found && info.data_found);
    assert(info.skin_count == 2 && info.patch_applied);
    assert(info.visual_refresh_invoked && !info.visual_refresh_failed);
    assert(io.refresh_calls == 1 && io.refreshed_manager == manager);
    assert(io.refreshed_piece == manager + FPMANAGER_UZL && io.refreshed_slot == 1);
    assert(io.refreshed_rva == FP_MANAGER_SKIN_REFRESH_RVA && io.refreshed_skin == 1);
    int16_t value = -1;
    const uint64_t fields[] = {
        item + ITEM_SKIN,
        weapon + PLAYERWEAPON_PIECE + WEAPONPIECE_SKIN,
        weapon + PLAYERWEAPON_CACHED_PIECE + WEAPONPIECE_SKIN,
        manager + FPMANAGER_UZL + WEAPONPIECE_SKIN,
        fp_view + PLAYERFPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN
    };
    for (uint64_t field : fields) {
        assert(io.read(field, value) && value == 1);
    }
    assert(io.writes == 5);

    // Repeated ticks do not spam writes; turning the feature off conditionally
    // restores only fields that still contain our override.
    ctl.tick(1, player, true, 1, false);
    assert(io.writes == 5 && io.refresh_calls == 1);
    ctl.tick(1, player, false, 1, false);
    for (uint64_t field : fields) assert(io.read(field, value) && value == -1);
    assert(io.writes == 10 && io.refresh_calls == 2);
    assert(!ctl.snapshot().patch_applied);
    assert(ctl.snapshot().visual_refresh_invoked && !ctl.snapshot().visual_refresh_failed);
    assert(io.refreshed_skin == -1);

    // If the game replaced a patched field, do not overwrite it during restore.
    weapon_skin::SkinFieldPatch<FakeIO> patch;
    io.put(item + ITEM_SKIN, int16_t(-1));
    assert(patch.apply(io, item + ITEM_SKIN, int16_t(0)));
    io.put(item + ITEM_SKIN, int16_t(2));
    assert(patch.restore(io));
    assert(io.read(item + ITEM_SKIN, value) && value == 2);

    std::puts("weapon skin catalog/patch controller checks passed");
    return 0;
}
