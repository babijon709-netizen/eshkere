#pragma once

#include "viewmodel.h"
#include "game_offsets_active.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

namespace weapon_skin {

// The first-person model reload is asynchronous (PlayerWeapon -> FPManager.LTI).
// A memory-field override alone is insufficient because the loader takes a
// WeaponPiece value copy. After the guarded field writes, IO may invoke the
// build-specific FPManager reload entry on UnityMain; the invoker changes only
// thread registers/stack data and never patches libil2cpp RX pages.
//
// A short skin value is an index into ItemData.skinData. -1 is the game's
// default (confirmed by Item's constructor and setter in the 11978 libil2cpp).
inline constexpr int kMaxArrayEntries = 4096;
inline constexpr auto kScanInterval = std::chrono::milliseconds(250);

inline void append_utf8(char* out, std::size_t cap, std::size_t& used, uint32_t cp) {
    auto byte = [&](uint8_t v) {
        if (used + 1 < cap) out[used++] = static_cast<char>(v);
    };
    if (cp <= 0x7f) byte(static_cast<uint8_t>(cp));
    else if (cp <= 0x7ff) {
        byte(static_cast<uint8_t>(0xc0 | (cp >> 6)));
        byte(static_cast<uint8_t>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        byte(static_cast<uint8_t>(0xe0 | (cp >> 12)));
        byte(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3f)));
        byte(static_cast<uint8_t>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0x10ffff) {
        byte(static_cast<uint8_t>(0xf0 | (cp >> 18)));
        byte(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3f)));
        byte(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3f)));
        byte(static_cast<uint8_t>(0x80 | (cp & 0x3f)));
    }
}

template<class IO>
bool read_managed_string(IO& io, uint64_t string, char* out, std::size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = '\0';
    if (string < 0x10000) return false;
    int32_t length = 0;
    if (!io.read(string + go_active::IL2CPP_STRING_LENGTH, length) || length <= 0 || length > 256) return false;
    std::size_t used = 0;
    for (int32_t i = 0; i < length && used + 4 < cap; ++i) {
        uint16_t first = 0;
        if (!io.read(string + go_active::IL2CPP_STRING_CHARS + static_cast<uint64_t>(i) * 2, first)) return false;
        uint32_t cp = first;
        if (first >= 0xd800 && first <= 0xdbff && i + 1 < length) {
            uint16_t second = 0;
            if (!io.read(string + go_active::IL2CPP_STRING_CHARS + static_cast<uint64_t>(i + 1) * 2, second)) return false;
            if (second >= 0xdc00 && second <= 0xdfff) {
                cp = 0x10000 + ((static_cast<uint32_t>(first) - 0xd800) << 10) +
                     (static_cast<uint32_t>(second) - 0xdc00);
                ++i;
            }
        }
        append_utf8(out, cap, used, cp);
    }
    out[used] = '\0';
    return used != 0;
}

template<class IO>
struct SkinFieldPatch {
    uint64_t address = 0;
    int16_t original = -1;
    int16_t last = -1;
    bool written = false;

    bool restore(IO& io) {
        if (!written) { address = 0; return true; }
        int16_t current = 0;
        if (!io.read(address, current)) return false;
        if (current == last && !io.write_heap(address, original)) return false;
        address = 0; original = last = -1; written = false;
        return true;
    }

    bool apply(IO& io, uint64_t target, int16_t value) {
        if (target < 0x10000) return restore(io);
        if (address != target) {
            if (!restore(io)) return false;
            int16_t current = 0;
            if (!io.read(target, current)) return false;
            address = target; original = current; last = current; written = false;
        }
        int16_t current = 0;
        if (!io.read(address, current)) return false;
        // The game owns these fields too. If it replaced our last value, make
        // its new value the restore target instead of overwriting it later.
        if (!written || current != last) original = current;
        if (current != value && !io.write_heap(address, value)) return false;
        last = value; written = true;
        return true;
    }
};

template<class IO>
class Controller {
    IO& io_;
    uint64_t session_ = 0;
    uint64_t player_ = 0;
    bool enabled_ = false;
    int selection_ = -1;
    std::chrono::steady_clock::time_point last_scan_{};
    std::array<SkinFieldPatch<IO>, 5> patches_{};
    uint64_t last_refresh_weapon_ = 0;
    uint64_t last_refresh_manager_ = 0;
    int last_refresh_selection_ = std::numeric_limits<int>::min();
    bool last_refresh_enabled_ = false;
    bool refresh_invoked_ = false;
    bool refresh_failed_ = false;
    viewmodel::WeaponSkinUiInfo info_{};

    bool is_weapon(uint64_t candidate, uint64_t player) {
        using namespace go_active;
        return candidate >= 0x10000 &&
               io_.ptr(candidate + PLAYERWEAPON_PLAYER_BACKREF) == player &&
               io_.named(candidate, "PlayerWeapon", "HyperHug.Games.Oxide.Features.Weapons");
    }

    uint64_t resolve_weapon(uint64_t player) {
        using namespace go_active;
        if (!player) return 0;
        uint64_t ref = io_.ptr(player + PLAYER_WEAPON_REFERENCE);
        if (is_weapon(ref, player)) return ref;
        if (ref >= 0x10000) {
            for (uint64_t off = 0x08; off <= 0x60; off += 8) {
                const uint64_t candidate = io_.ptr(ref + off);
                if (is_weapon(candidate, player)) return candidate;
            }
        }
        // weaponReference is a lazy interface wrapper. Its layout has rotated,
        // so retain the existing back-reference + class-shape probe as fallback.
        for (uint64_t off = 0x68; off <= 0x350; off += 8) {
            const uint64_t candidate = io_.ptr(player + off);
            if (is_weapon(candidate, player)) return candidate;
        }
        return 0;
    }

    bool restore_all() {
        bool ok = true;
        for (auto& patch : patches_) if (!patch.restore(io_)) ok = false;
        if (ok) info_.patch_applied = false;
        return ok;
    }

    bool read_name(uint64_t owner, uint64_t field, char* out, std::size_t cap) {
        return read_managed_string(io_, io_.ptr(owner + field), out, cap);
    }

    void scan(uint64_t player, bool enabled, int selection) {
        using namespace go_active;
        viewmodel::WeaponSkinUiInfo next{};
        next.player_found = player >= 0x10000;
        next.current_skin = -1;
        const uint64_t weapon = resolve_weapon(player);
        next.weapon_found = weapon != 0;
        const uint64_t item = weapon ? io_.ptr(weapon + PLAYERWEAPON_ITEM) : 0;
        const bool item_ok = item >= 0x10000 && io_.named(item, "Item", "Oxide");
        const uint64_t data = item_ok ? io_.ptr(item + ITEM_DATA) : 0;
        const bool data_ok = data >= 0x10000 && io_.named(data, "ItemData", "Oxide");
        uint64_t skin_array = data_ok ? io_.ptr(data + ITEMDATA_SKIN_DATA) : 0;
        int32_t skin_count = 0;
        const bool array_ok = skin_array >= 0x10000 &&
            io_.read(skin_array + IL2CPP_ARRAY_LENGTH, skin_count) &&
            skin_count >= 0 && skin_count <= kMaxArrayEntries;
        next.data_found = data_ok && array_ok;
        next.skin_count = next.data_found
            ? (skin_count < viewmodel::kMaxWeaponSkinEntries ? skin_count : viewmodel::kMaxWeaponSkinEntries)
            : 0;
        if (item_ok) io_.read(item + ITEM_SKIN, next.current_skin);
        if (data_ok) read_name(data, ITEMDATA_NAME, next.weapon_name, sizeof(next.weapon_name));
        if (array_ok) {
            for (int i = 0; i < next.skin_count; ++i) {
                const uint64_t skin = io_.ptr(skin_array + IL2CPP_ARRAY_FIRST_ELEMENT +
                                              static_cast<uint64_t>(i) * sizeof(uint64_t));
                bool got = skin >= 0x10000 && read_name(skin, SKINDATA_ID,
                                                       next.skin_ids[i], sizeof(next.skin_ids[i]));
                if (!got) std::snprintf(next.skin_ids[i], sizeof(next.skin_ids[i]), "Skin %d", i + 1);
            }
        }

        const bool selected_is_valid = selection == -1 ||
            (next.data_found && selection >= 0 && selection < skin_count &&
             selection < std::numeric_limits<int16_t>::max());
        const uint64_t manager_candidate = weapon ? io_.ptr(weapon + PLAYERWEAPON_FP_MANAGER) : 0;
        const uint64_t view_candidate = weapon ? io_.ptr(weapon + PLAYERWEAPON_FP_VIEW) : 0;
        const uint64_t manager = manager_candidate >= 0x10000 &&
            io_.named(manager_candidate, "FPManager", "") ? manager_candidate : 0;
        const uint64_t fp_view = view_candidate >= 0x10000 &&
            io_.named(view_candidate, "PlayerFPWeaponView", "") ? view_candidate : 0;
        int16_t manager_skin_before = -1;
        const bool manager_skin_was_read = manager &&
            io_.read(manager + FPMANAGER_UZL + WEAPONPIECE_SKIN, manager_skin_before);
        const bool override_active = enabled && weapon && item_ok && data_ok && selected_is_valid;
        if (!override_active) {
            restore_all();
        } else {
            const int16_t value = static_cast<int16_t>(selection);
            const uint64_t addresses[5] = {
                item + ITEM_SKIN,
                weapon + PLAYERWEAPON_PIECE + WEAPONPIECE_SKIN,
                weapon + PLAYERWEAPON_CACHED_PIECE + WEAPONPIECE_SKIN,
                manager >= 0x10000 ? manager + FPMANAGER_UZL + WEAPONPIECE_SKIN : 0,
                fp_view >= 0x10000 ? fp_view + PLAYERFPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN : 0
            };
            bool applied = false;
            for (std::size_t i = 0; i < patches_.size(); ++i)
                applied = patches_[i].apply(io_, addresses[i], value) || applied;
            next.patch_applied = applied;
        }

        int16_t manager_skin_after = -1;
        const bool manager_skin_is_read = manager &&
            io_.read(manager + FPMANAGER_UZL + WEAPONPIECE_SKIN, manager_skin_after);
        const int effective_selection = override_active ? selection : manager_skin_after;
        const bool refresh_key_changed =
            weapon != last_refresh_weapon_ || manager != last_refresh_manager_ ||
            override_active != last_refresh_enabled_ ||
            effective_selection != last_refresh_selection_;
        const bool manager_piece_changed = manager_skin_was_read && manager_skin_is_read &&
            manager_skin_before != manager_skin_after;
        if (manager && manager_skin_is_read && (refresh_key_changed || manager_piece_changed)) {
            int32_t slot = -1;
            if (weapon && io_.read(weapon + PLAYERWEAPON_SKIN_REFRESH_SLOT, slot) &&
                slot >= -1 && slot <= 255 &&
                io_.reload_weapon_visual(manager, manager + FPMANAGER_UZL, slot,
                                         FP_MANAGER_SKIN_REFRESH_RVA, manager_skin_after)) {
                last_refresh_weapon_ = weapon;
                last_refresh_manager_ = manager;
                last_refresh_selection_ = effective_selection;
                last_refresh_enabled_ = override_active;
                refresh_invoked_ = true;
                refresh_failed_ = false;
            } else {
                refresh_invoked_ = false;
                refresh_failed_ = true;
            }
        }
        next.visual_refresh_invoked = refresh_invoked_;
        next.visual_refresh_failed = refresh_failed_;
        info_ = next;
        player_ = player;
        enabled_ = enabled;
        selection_ = selection;
        last_scan_ = std::chrono::steady_clock::now();
    }
public:
    explicit Controller(IO& io) : io_(io) {}
    const viewmodel::WeaponSkinUiInfo& snapshot() const { return info_; }
    bool has_state() const {
        if (enabled_) return true;
        for (const auto& patch : patches_) if (patch.written) return true;
        return false;
    }
    void reset(bool restore) {
        if (restore) restore_all();
        for (auto& patch : patches_) patch = {};
        session_ = player_ = 0;
        enabled_ = false; selection_ = -1; last_scan_ = {};
        last_refresh_weapon_ = last_refresh_manager_ = 0;
        last_refresh_selection_ = std::numeric_limits<int>::min();
        last_refresh_enabled_ = false;
        refresh_invoked_ = refresh_failed_ = false;
        info_ = {};
    }
    void tick(uint64_t session, uint64_t player, bool enabled, int selection, bool force_scan = false) {
        if (session != session_) {
            reset(false);
            session_ = session;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool changed = player != player_ || enabled != enabled_ || selection != selection_;
        if (!enabled && has_state()) {
            // Restore promptly when switched off, but only if a previous write
            // is still present (SkinFieldPatch verifies ownership first).
            scan(player, false, selection);
            return;
        }
        if (!enabled && !force_scan) return;
        if (changed || last_scan_ == std::chrono::steady_clock::time_point{} ||
            now - last_scan_ >= kScanInterval) scan(player, enabled, selection);
    }
};

} // namespace weapon_skin
