#pragma once
#include "game_offsets_active.h"
#include "mem_io.h"
#include "viewmodel.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chams {

struct Params {
    bool enabled = false;
    bool geometry_outline = false;
    float width = 5.0f;
    float intensity = 45.0f;
    float r = 1.0f;
    float g = 0.18f;
    float b = 0.42f;
    float a = 1.0f;
};

struct SettingsSnapshot {
    float width = 4.0f;
    float intensity = 20.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};
static_assert(sizeof(SettingsSnapshot) == 24, "OutlineSettings scalar block");

template <class Memory>
class Controller {
public:
    explicit Controller(Memory& memory) : memory_(memory) {}

    bool has_state() const {
        return active_ || saved_outline_ || saved_hook_ || saved_get_components_method_;
    }
    viewmodel::Status status() const { return status_; }

    bool reset(bool restore, uint64_t il2cpp_base = 0) {
        bool ok = true;
        if (restore) {
            const uint64_t base = il2cpp_base ? il2cpp_base : last_base_;
            if (saved_hook_ && method_info_ >= 0x10000 && orig_fn_ >= 0x10000) {
                ok &= restore_method_info(method_info_, orig_fn_, orig_vfn_, orig_invoker_);
            } else if (base >= 0x10000) {
                uint64_t mi = 0, orig_fn = 0, hook_fn = 0, reg_go_fn = 0;
                if (resolve_ragdoll_late_update(base, mi, orig_fn, hook_fn, reg_go_fn)) {
                    ok &= restore_method_info(mi, orig_fn, orig_fn, orig_invoker_);
                }
            }
            ok &= restore_get_components_method();
            if (saved_outline_) {
                if (outline_sf_ >= 0x10000)
                    ok &= write_rw(outline_sf_ + kOutlineStaticInitOffset, orig_is_initialized_);
                if (settings_ >= 0x10000)
                    ok &= write_rw(settings_ + kSettingsWidthOffset, orig_settings_);
                if (settings_obj_ >= 0x10000)
                    ok &= write_rw(settings_obj_ + kSettingsWidthOffset, orig_settings_obj_);
                if (feature_ >= 0x10000)
                    ok &= write_rw(feature_ + kFeatureUseGeometryOffset, orig_use_geometry_);
                if (pass_ >= 0x10000) {
                    ok &= write_rw(pass_ + kPassUseGeometryOffset, orig_use_geometry_);
                    clear_pass_list(pass_);
                }
            }
        }
        active_ = false;
        saved_hook_ = false;
        saved_outline_ = false;
        saved_get_components_method_ = false;
        session_ = 0;
        last_base_ = 0;
        method_info_ = 0;
        orig_fn_ = 0;
        orig_vfn_ = 0;
        orig_invoker_ = 0;
        hook_fn_ = 0;
        reg_go_fn_ = 0;
        mesh_method_slot_ = 0;
        orig_mesh_method_ = 0;
        orig_mesh_rgctx_ = 0;
        outline_sf_ = 0;
        feature_ = 0;
        pass_ = 0;
        settings_ = 0;
        settings_obj_ = 0;
        cached_count_ = 0;
        status_ = viewmodel::Status::Off;
        return ok;
    }

    void tick(uint64_t session, uint64_t il2cpp_base, const Params& raw) {
        if (session != session_ || (last_base_ && il2cpp_base != last_base_)) {
            reset(false);
            session_ = session;
        }
        last_base_ = il2cpp_base;

        if (!raw.enabled) {
            if (has_state()) reset(true, il2cpp_base);
            status_ = viewmodel::Status::Off;
            return;
        }
        if (il2cpp_base < 0x10000) {
            status_ = viewmodel::Status::Waiting;
            return;
        }

        uint64_t sf = 0, feature = 0, pass = 0, settings = 0, settings_obj = 0;
        if (!resolve_outline(il2cpp_base, sf, feature, pass, settings, settings_obj)) {
            status_ = viewmodel::Status::Waiting;
            return;
        }

        if (!saved_outline_ || feature != feature_ || settings != settings_) {
            SettingsSnapshot s{};
            uint8_t geom = 0;
            uint8_t is_init = 1;
            if (!memory_.read(settings + kSettingsWidthOffset, s) ||
                !memory_.read(feature + kFeatureUseGeometryOffset, geom)) {
                status_ = viewmodel::Status::Waiting;
                return;
            }
            memory_.read(sf + kOutlineStaticInitOffset, is_init);
            orig_is_initialized_ = is_init ? 1u : 0u;
            orig_settings_ = s;
            orig_use_geometry_ = geom ? 1u : 0u;
            if (settings_obj >= 0x10000) {
                SettingsSnapshot so{};
                if (memory_.read(settings_obj + kSettingsWidthOffset, so))
                    orig_settings_obj_ = so;
                else
                    settings_obj = 0;
            }
            outline_sf_ = sf;
            feature_ = feature;
            pass_ = pass;
            settings_ = settings;
            settings_obj_ = settings_obj;
            saved_outline_ = true;
        }

        SettingsSnapshot desired{};
        desired.width = viewmodel::bounded(raw.width, 1.0f, 32.0f, 5.0f);
        desired.intensity = viewmodel::bounded(raw.intensity, 1.0f, 100.0f, 45.0f);
        desired.r = viewmodel::bounded(raw.r, 0.0f, 1.0f, 1.0f);
        desired.g = viewmodel::bounded(raw.g, 0.0f, 1.0f, 0.18f);
        desired.b = viewmodel::bounded(raw.b, 0.0f, 1.0f, 0.42f);
        desired.a = viewmodel::bounded(raw.a, 0.0f, 1.0f, 1.0f);
        const uint8_t desired_geom = raw.geometry_outline ? 1u : 0u;

        apply_outline_settings(feature, pass, settings, settings_obj, desired, desired_geom);

        // Redirect GameObject.GetComponentsInChildren<MeshRenderer>(bool) -> SkinnedMeshRenderer
        const bool skinned_ready = redirect_get_components_to_skinned(il2cpp_base);

        uint64_t mi = method_info_;
        uint64_t orig_fn = orig_fn_;
        uint64_t hook_fn = hook_fn_;
        uint64_t reg_go_fn = reg_go_fn_;
        if (!validate_method_info(mi, orig_fn, hook_fn, reg_go_fn)) {
            mi = 0;
            if (!resolve_ragdoll_late_update(il2cpp_base, mi, orig_fn, hook_fn, reg_go_fn)) {
                status_ = viewmodel::Status::Waiting;
                return;
            }
            method_info_ = mi;
            orig_fn_ = orig_fn;
            hook_fn_ = hook_fn;
            reg_go_fn_ = reg_go_fn;
            uint64_t vfn = 0, inv = 0;
            memory_.read(mi + kMethodInfoVirtualPtrOffset, vfn);
            memory_.read(mi + kMethodInfoInvokerOffset, inv);
            orig_vfn_ = memio::untag(vfn) ? memio::untag(vfn) : orig_fn;
            orig_invoker_ = memio::untag(inv);
        }

        // Collect active player characterModel GameObjects for multi-player chams
        PlayerTarget targets[kMaxPlayers]{};
        const size_t target_count = collect_player_targets(il2cpp_base, targets);

        if (target_count > 0 && reg_go_fn >= 0x10000) {
            if (!sync_multi_player_chams(
                    sf, pass, settings, mi, reg_go_fn, skinned_ready, targets, target_count)) {
                status_ = viewmodel::Status::Failed;
                return;
            }
        } else {
            // Fallback: hook Ragdoll.LateUpdate -> hWT.KVH / Ymm.YpE (void (Component*))
            const uint8_t init_true = 1;
            write_rw(sf + kOutlineStaticInitOffset, init_true);
            if (orig_invoker_ >= 0x10000) {
                write_rw(mi + kMethodInfoInvokerOffset, orig_invoker_);
            }
            uint64_t cur_mp = 0, cur_vmp = 0;
            if (!memory_.read(mi + kMethodInfoPtrOffset, cur_mp) ||
                !memory_.read(mi + kMethodInfoVirtualPtrOffset, cur_vmp)) {
                status_ = viewmodel::Status::Failed;
                return;
            }
            cur_mp = memio::untag(cur_mp);
            cur_vmp = memio::untag(cur_vmp);

            if (cur_mp != hook_fn || cur_vmp != hook_fn) {
                if (!write_rw(mi + kMethodInfoPtrOffset, hook_fn) ||
                    !write_rw(mi + kMethodInfoVirtualPtrOffset, hook_fn)) {
                    status_ = viewmodel::Status::Failed;
                    return;
                }
                uint64_t check_mp = 0;
                if (!memory_.read(mi + kMethodInfoPtrOffset, check_mp) ||
                    memio::untag(check_mp) != hook_fn) {
                    status_ = viewmodel::Status::Failed;
                    return;
                }
            }
        }

        saved_hook_ = true;
        active_ = true;
        status_ = viewmodel::Status::Active;
    }

private:
    static constexpr size_t kMaxPlayers = 32;

    static constexpr uint64_t kClassElementClassOffset  = 0x40;
    static constexpr uint64_t kClassDeclaringTypeOffset = 0x50;
    static constexpr uint64_t kClassMethodsOffset       = 0x98;
    static constexpr uint64_t kClassStaticFieldsOffset  = 0xB8;
    static constexpr uint64_t kClassMethodCountOffset   = 0x120;

    static constexpr uint64_t kMethodInfoPtrOffset        = 0x00;
    static constexpr uint64_t kMethodInfoVirtualPtrOffset = 0x08;
    static constexpr uint64_t kMethodInfoInvokerOffset    = 0x10;
    static constexpr uint64_t kMethodInfoKlassOffset      = 0x20;
    static constexpr uint64_t kMethodInfoRgctxOffset      = 0x38;
    static constexpr uint64_t kMethodInfoParamCountOffset = 0x52;

    static constexpr uint64_t kOutlineStaticSettingsOffset = 0x00;
    static constexpr uint64_t kOutlineStaticInitOffset     = 0x08;
    static constexpr uint64_t kOutlineStaticPassOffset     = 0x10;

    static constexpr uint64_t kFeatureSettingsOffset    = 0x28;
    static constexpr uint64_t kFeatureUseGeometryOffset = 0x30;

    static constexpr uint64_t kPassFeatureOffset     = 0xB8;
    static constexpr uint64_t kPassListOffset        = 0xC8;
    static constexpr uint64_t kPassUseGeometryOffset = 0xD8;

    static constexpr uint64_t kOutlineEntryGameObjectOffset    = 0x10;
    static constexpr uint64_t kOutlineEntryRenderersListOffset = 0x18;
    static constexpr uint64_t kOutlineEntrySettingsOffset      = 0x20;

    static constexpr uint64_t kSettingsObjectOffset = 0x10;
    static constexpr uint64_t kSettingsWidthOffset  = 0x18;

    static constexpr uint64_t kPlayerDeathHandlerRefOffset   = 0x80;
    static constexpr uint64_t kInterfaceRefTargetOffset      = 0x18;
    static constexpr uint64_t kDeathHandlerRagdollOffset     = 0xE0;
    static constexpr uint64_t kPlayerInventoryOffset         = 0x98;
    static constexpr uint64_t kInventoryEquipmentOffset      = 0x70;
    static constexpr uint64_t kEquipmentModelInfoOffset      = 0x30;
    static constexpr uint64_t kPlayerCharacterModelOffset    = 0x150;
    static constexpr uint64_t kPlayerModelHolderOffset       = 0x260;
    static constexpr uint64_t kModelHolderInfoOffset         = 0x10;
    static constexpr uint64_t kModelInfoSkinnedRendererOffset = 0x50;
    static constexpr uint64_t kModelInfoHeadRendererOffset   = 0x58;
    static constexpr uint64_t kModelInfoCharAnimOffset       = 0x60;
    static constexpr uint64_t kCharAnimRagdollOffset         = 0x38;

    static constexpr uint64_t kBufferListValuesOffset        = 0x18;
    static constexpr uint64_t kManagedArrayLengthOffset      = 0x18;

    struct PlayerTarget {
        uint64_t player        = 0;
        uint64_t game_object   = 0;
        uint64_t body_renderer = 0;
        uint64_t head_renderer = 0;
    };

    struct CachedEntry {
        uint64_t game_object = 0;
        uint64_t entry       = 0;
    };

    Memory& memory_;
    uint64_t session_ = 0;
    uint64_t last_base_ = 0;
    bool active_ = false;
    bool saved_hook_ = false;
    bool saved_outline_ = false;
    bool saved_get_components_method_ = false;
    viewmodel::Status status_ = viewmodel::Status::Off;

    uint64_t method_info_ = 0;
    uint64_t orig_fn_ = 0;
    uint64_t orig_vfn_ = 0;
    uint64_t orig_invoker_ = 0;
    uint64_t hook_fn_ = 0;
    uint64_t reg_go_fn_ = 0;

    uint64_t mesh_method_slot_ = 0;
    uint64_t orig_mesh_method_ = 0;
    uint64_t orig_mesh_rgctx_ = 0;

    uint64_t outline_sf_ = 0;
    uint64_t feature_ = 0;
    uint64_t pass_ = 0;
    uint64_t settings_ = 0;
    uint64_t settings_obj_ = 0;
    uint8_t orig_is_initialized_ = 1;
    uint8_t orig_use_geometry_ = 0;
    SettingsSnapshot orig_settings_{};
    SettingsSnapshot orig_settings_obj_{};

    CachedEntry cached_entries_[kMaxPlayers]{};
    size_t cached_count_ = 0;

    template <class Mem, class T>
    static auto detail_write_fallback(Mem& m, uint64_t a, const T& v, int)
        -> decltype(m.write(a, v)) {
        return m.write(a, v);
    }
    template <class Mem, class T>
    static bool detail_write_fallback(Mem&, uint64_t, const T&, long) {
        return false;
    }

    template <class T>
    bool write_rw(uint64_t addr, const T& value) {
        if (memory_.write_heap(addr, value)) return true;
        return detail_write_fallback(memory_, addr, value, 0);
    }

    uint64_t ptr(uint64_t addr) {
        uint64_t v = 0;
        return addr >= 0x10000 && memory_.read(addr, v) ? memio::untag(v) : 0;
    }

    bool redirect_get_components_to_skinned(uint64_t il2cpp_base) {
        if (go_active::OUTLINE_GET_COMPONENTS_MESH_METHOD_RVA == 0 ||
            go_active::OUTLINE_GET_COMPONENTS_SKINNED_METHOD_RVA == 0) {
            return true;
        }
        const uint64_t mesh_slot = il2cpp_base + go_active::OUTLINE_GET_COMPONENTS_MESH_METHOD_RVA;
        const uint64_t skinned_slot = il2cpp_base + go_active::OUTLINE_GET_COMPONENTS_SKINNED_METHOD_RVA;

        const uint64_t cur_mesh = ptr(mesh_slot);
        const uint64_t skinned = ptr(skinned_slot);

        if (!saved_get_components_method_ && cur_mesh >= 0x10000 && cur_mesh != skinned) {
            mesh_method_slot_ = mesh_slot;
            orig_mesh_method_ = cur_mesh;
            orig_mesh_rgctx_ = ptr(cur_mesh + kMethodInfoRgctxOffset);
            saved_get_components_method_ = true;
        }

        if (skinned < 0x10000) return false;

        if (cur_mesh != skinned) {
            write_rw(mesh_slot, skinned);
        }
        if (saved_get_components_method_ && orig_mesh_method_ >= 0x10000) {
            const uint64_t skinned_rgctx = ptr(skinned + kMethodInfoRgctxOffset);
            if (skinned_rgctx >= 0x10000) {
                write_rw(orig_mesh_method_ + kMethodInfoRgctxOffset, skinned_rgctx);
            }
        }
        return true;
    }

    bool restore_get_components_method() {
        if (!saved_get_components_method_) return true;
        bool ok = true;
        if (orig_mesh_method_ >= 0x10000) {
            if (orig_mesh_rgctx_ >= 0x10000) {
                ok &= write_rw(orig_mesh_method_ + kMethodInfoRgctxOffset, orig_mesh_rgctx_);
            }
            if (mesh_method_slot_ >= 0x10000) {
                ok &= write_rw(mesh_method_slot_, orig_mesh_method_);
            }
        }
        saved_get_components_method_ = false;
        mesh_method_slot_ = 0;
        orig_mesh_method_ = 0;
        orig_mesh_rgctx_ = 0;
        return ok;
    }

    bool resolve_outline(uint64_t il2cpp_base,
                         uint64_t& out_sf,
                         uint64_t& out_feature,
                         uint64_t& out_pass,
                         uint64_t& out_settings,
                         uint64_t& out_settings_obj) {
        const uint64_t rva = go_active::OUTLINE_FEATURE_TYPEINFO_RVA;
        if (!rva) return false;
        const uint64_t klass = ptr(il2cpp_base + rva);
        if (klass < 0x10000) return false;
        const uint64_t sf = ptr(klass + kClassStaticFieldsOffset);
        if (sf < 0x10000) return false;

        if (!saved_outline_) {
            uint8_t is_init = 0;
            if (!memory_.read(sf + kOutlineStaticInitOffset, is_init) || !is_init)
                return false;
        }

        const uint64_t settings = ptr(sf + kOutlineStaticSettingsOffset);
        const uint64_t pass = ptr(sf + kOutlineStaticPassOffset);
        if (settings < 0x10000 || pass < 0x10000) return false;

        const uint64_t feature = ptr(pass + kPassFeatureOffset);
        if (feature < 0x10000) return false;
        if (ptr(feature) != klass || ptr(feature + kFeatureSettingsOffset) != settings)
            return false;

        uint64_t settings_obj = ptr(settings + kSettingsObjectOffset);
        if (settings_obj >= 0x10000 && ptr(settings_obj + go_active::MANAGED_CACHED_PTR) == 0)
            settings_obj = 0;

        out_sf = sf;
        out_feature = feature;
        out_pass = pass;
        out_settings = settings;
        out_settings_obj = settings_obj;
        return true;
    }

    void apply_outline_settings(uint64_t feature,
                                uint64_t pass,
                                uint64_t settings,
                                uint64_t settings_obj,
                                const SettingsSnapshot& desired,
                                uint8_t desired_geom) {
        SettingsSnapshot cur{};
        if (memory_.read(settings + kSettingsWidthOffset, cur) &&
            std::memcmp(&cur, &desired, sizeof(desired)) != 0) {
            write_rw(settings + kSettingsWidthOffset, desired);
        }
        if (settings_obj >= 0x10000 &&
            memory_.read(settings_obj + kSettingsWidthOffset, cur) &&
            std::memcmp(&cur, &desired, sizeof(desired)) != 0) {
            write_rw(settings_obj + kSettingsWidthOffset, desired);
        }
        uint8_t cur_geom = 0;
        if (memory_.read(feature + kFeatureUseGeometryOffset, cur_geom) &&
            (cur_geom ? 1u : 0u) != desired_geom) {
            write_rw(feature + kFeatureUseGeometryOffset, desired_geom);
        }
        if (memory_.read(pass + kPassUseGeometryOffset, cur_geom) &&
            (cur_geom ? 1u : 0u) != desired_geom) {
            write_rw(pass + kPassUseGeometryOffset, desired_geom);
        }
    }

    void clear_pass_list(uint64_t pass) {
        const uint64_t list = ptr(pass + kPassListOffset);
        if (list < 0x10000) return;
        int32_t sz = 0;
        if (memory_.read(list + go_active::IL2CPP_LIST_SIZE, sz) && sz > 0 && sz <= 512) {
            const int32_t zero = 0;
            write_rw(list + go_active::IL2CPP_LIST_SIZE, zero);
        }
    }

    void read_player_model_renderers(uint64_t player,
                                     uint64_t& body_renderer,
                                     uint64_t& head_renderer) {
        body_renderer = 0;
        head_renderer = 0;
        if (player < 0x10000) return;

        uint64_t model_info = 0;
        const uint64_t inv = ptr(player + kPlayerInventoryOffset);
        if (inv >= 0x10000) {
            const uint64_t eq = ptr(inv + kInventoryEquipmentOffset);
            if (eq >= 0x10000) model_info = ptr(eq + kEquipmentModelInfoOffset);
        }
        if (model_info < 0x10000) {
            const uint64_t holder = ptr(player + kPlayerModelHolderOffset);
            if (holder >= 0x10000) model_info = ptr(holder + kModelHolderInfoOffset);
        }
        if (model_info < 0x10000) return;

        body_renderer = ptr(model_info + kModelInfoSkinnedRendererOffset);
        head_renderer = ptr(model_info + kModelInfoHeadRendererOffset);
    }

    void collect_from_player_list(uint64_t list,
                                  PlayerTarget (&out)[kMaxPlayers],
                                  size_t& count) {
        if (list < 0x10000 || count >= kMaxPlayers) return;
        const uint64_t items = ptr(list + go_active::IL2CPP_LIST_ITEMS);
        int32_t sz = 0;
        if (items < 0x10000 || !memory_.read(list + go_active::IL2CPP_LIST_SIZE, sz))
            return;
        if (sz <= 0 || sz > 512) return;
        const int32_t limit = sz < 64 ? sz : 64;
        for (int32_t i = 0; i < limit && count < kMaxPlayers; ++i) {
            const uint64_t player = ptr(items + go_active::IL2CPP_ARRAY_FIRST_ELEMENT +
                                        (uint64_t)i * sizeof(uint64_t));
            if (player < 0x10000) continue;
            const uint64_t go = ptr(player + kPlayerCharacterModelOffset);
            if (go < 0x10000) continue;
            bool dup = false;
            for (size_t j = 0; j < count; ++j) {
                if (out[j].game_object == go) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            PlayerTarget& t = out[count++];
            t.player = player;
            t.game_object = go;
            read_player_model_renderers(player, t.body_renderer, t.head_renderer);
        }
    }

    size_t collect_player_targets(uint64_t il2cpp_base,
                                  PlayerTarget (&out)[kMaxPlayers]) {
        size_t count = 0;
        if (go_active::PLAYER_MANAGER_TYPEINFO_RVA == 0) return 0;
        const uint64_t pm_klass = ptr(il2cpp_base + go_active::PLAYER_MANAGER_TYPEINFO_RVA);
        if (pm_klass < 0x10000) return 0;
        const uint64_t pm_sf = ptr(pm_klass + kClassStaticFieldsOffset);
        if (pm_sf < 0x10000) return 0;

        const uint64_t client_list = ptr(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_LIST);
        collect_from_player_list(client_list, out, count);

        const uint64_t active_ra = ptr(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST);
        collect_from_player_list(active_ra, out, count);
        if (active_ra >= 0x10000) {
            collect_from_player_list(ptr(active_ra + kBufferListValuesOffset), out, count);
        }

        const uint64_t sleeper_ra = ptr(pm_sf + 0x00);
        if (sleeper_ra >= 0x10000) {
            collect_from_player_list(ptr(sleeper_ra + kBufferListValuesOffset), out, count);
        }
        return count;
    }

    void augment_entry_renderers(uint64_t entry, const PlayerTarget& target) {
        const uint64_t rlist = ptr(entry + kOutlineEntryRenderersListOffset);
        if (rlist < 0x10000) return;
        const uint64_t ritems = ptr(rlist + go_active::IL2CPP_LIST_ITEMS);
        int32_t rsize = 0;
        if (ritems < 0x10000 || !memory_.read(rlist + go_active::IL2CPP_LIST_SIZE, rsize) || rsize < 0)
            return;
        int32_t rcap = 0;
        if (!memory_.read(ritems + kManagedArrayLengthOffset, rcap) || rcap <= 0 || rcap > 256)
            return;
        if (rsize > rcap) rsize = rcap;

        const uint64_t extras[2] = {target.body_renderer, target.head_renderer};
        bool modified = false;
        for (uint64_t extra : extras) {
            if (extra < 0x10000 || rsize >= rcap) continue;
            bool present = false;
            for (int32_t i = 0; i < rsize; ++i) {
                if (ptr(ritems + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + (uint64_t)i * 8u) == extra) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                if (write_rw(ritems + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + (uint64_t)rsize * 8u, extra)) {
                    ++rsize;
                    modified = true;
                }
            }
        }
        if (modified) {
            write_rw(rlist + go_active::IL2CPP_LIST_SIZE, rsize);
        }
    }

    uint64_t find_cached_entry(uint64_t go) const {
        for (size_t i = 0; i < cached_count_; ++i) {
            if (cached_entries_[i].game_object == go) return cached_entries_[i].entry;
        }
        return 0;
    }

    void put_cached_entry(uint64_t go, uint64_t entry) {
        for (size_t i = 0; i < cached_count_; ++i) {
            if (cached_entries_[i].game_object == go) {
                cached_entries_[i].entry = entry;
                return;
            }
        }
        if (cached_count_ < kMaxPlayers) {
            cached_entries_[cached_count_++] = {go, entry};
        } else {
            for (size_t i = 1; i < kMaxPlayers; ++i) cached_entries_[i - 1] = cached_entries_[i];
            cached_entries_[kMaxPlayers - 1] = {go, entry};
        }
    }

    bool is_valid_entry(uint64_t entry, uint64_t expected_go) {
        if (entry < 0x10000) return false;
        if (ptr(entry + kOutlineEntryGameObjectOffset) != expected_go) return false;
        const uint64_t rlist = ptr(entry + kOutlineEntryRenderersListOffset);
        if (rlist < 0x10000) return false;
        int32_t rsize = 0;
        return memory_.read(rlist + go_active::IL2CPP_LIST_SIZE, rsize) && rsize > 0;
    }

    bool sync_multi_player_chams(uint64_t sf,
                                 uint64_t pass,
                                 uint64_t settings,
                                 uint64_t mi,
                                 uint64_t reg_go_fn,
                                 bool skinned_ready,
                                 const PlayerTarget (&targets)[kMaxPlayers],
                                 size_t target_count) {
        const uint64_t tcw = ptr(pass + kPassListOffset);
        if (tcw < 0x10000) return false;

        const uint64_t tcw_items = ptr(tcw + go_active::IL2CPP_LIST_ITEMS);
        int32_t tcw_size = 0;
        int32_t tcw_cap = 0;
        memory_.read(tcw + go_active::IL2CPP_LIST_SIZE, tcw_size);
        if (tcw_items >= 0x10000) {
            memory_.read(tcw_items + kManagedArrayLengthOffset, tcw_cap);
            if (tcw_cap < 0 || tcw_cap > 256) tcw_cap = 0;
        }
        if (tcw_size < 0) tcw_size = 0;
        if (tcw_size > tcw_cap) tcw_size = tcw_cap;

        for (int32_t i = 0; i < tcw_size; ++i) {
            const uint64_t entry = ptr(tcw_items + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + (uint64_t)i * 8u);
            if (entry < 0x10000) continue;
            const uint64_t entry_go = ptr(entry + kOutlineEntryGameObjectOffset);
            if (entry_go < 0x10000) continue;
            for (size_t t = 0; t < target_count; ++t) {
                if (targets[t].game_object == entry_go) {
                    augment_entry_renderers(entry, targets[t]);
                    break;
                }
            }
            if (is_valid_entry(entry, entry_go)) {
                if (settings >= 0x10000) write_rw(entry + kOutlineEntrySettingsOffset, settings);
                put_cached_entry(entry_go, entry);
            }
        }

        uint64_t ready_entries[kMaxPlayers]{};
        size_t ready_count = 0;
        uint64_t missing_go = 0;

        for (size_t i = 0; i < target_count; ++i) {
            const uint64_t go = targets[i].game_object;
            const uint64_t cached = find_cached_entry(go);
            if (is_valid_entry(cached, go)) {
                augment_entry_renderers(cached, targets[i]);
                if (settings >= 0x10000) write_rw(cached + kOutlineEntrySettingsOffset, settings);
                ready_entries[ready_count++] = cached;
            } else if (missing_go == 0) {
                missing_go = go;
            }
        }

        if (tcw_items >= 0x10000 && tcw_cap > 0) {
            const int32_t max_write = static_cast<int32_t>(ready_count) < tcw_cap
                ? static_cast<int32_t>(ready_count)
                : tcw_cap;
            for (int32_t i = 0; i < max_write; ++i) {
                write_rw(tcw_items + go_active::IL2CPP_ARRAY_FIRST_ELEMENT + (uint64_t)i * 8u,
                         ready_entries[i]);
            }
            write_rw(tcw + go_active::IL2CPP_LIST_SIZE, max_write);
        }

        const bool need_more_capacity =
            static_cast<int32_t>(ready_count) > tcw_cap && ready_count > 0;

        if (!skinned_ready) {
            const uint8_t init_true = 1;
            write_rw(sf + kOutlineStaticInitOffset, init_true);
            const uint64_t null_go = 0;
            return write_rw(mi + kMethodInfoInvokerOffset, reg_go_fn) &&
                   write_rw(mi + kMethodInfoPtrOffset, null_go);
        }

        if (missing_go != 0 || need_more_capacity) {
            const uint64_t go_to_register = missing_go != 0
                ? missing_go
                : targets[ready_count - 1].game_object;
            const uint8_t init_true = 1;
            write_rw(sf + kOutlineStaticInitOffset, init_true);
            return write_rw(mi + kMethodInfoInvokerOffset, reg_go_fn) &&
                   write_rw(mi + kMethodInfoPtrOffset, go_to_register);
        }

        // All active players are registered in TCW: lock <IsInitialized> = 0 so
        // RaycastManager.Update() (OutlineFeature.wmG) returns early without clearing TCW,
        // and pass nullptr to OutlineFeature.wml so LateUpdate is a 3-instruction no-op.
        const uint8_t init_false = 0;
        write_rw(sf + kOutlineStaticInitOffset, init_false);
        const uint64_t null_go = 0;
        return write_rw(mi + kMethodInfoInvokerOffset, reg_go_fn) &&
               write_rw(mi + kMethodInfoPtrOffset, null_go);
    }

    bool validate_method_info(uint64_t mi,
                              uint64_t orig_fn,
                              uint64_t hook_fn,
                              uint64_t reg_go_fn) {
        if (mi < 0x10000 || orig_fn < 0x10000 || hook_fn < 0x10000) return false;
        uint64_t mp = 0, inv = 0;
        uint8_t params = 0xff;
        if (!memory_.read(mi + kMethodInfoPtrOffset, mp) ||
            !memory_.read(mi + kMethodInfoParamCountOffset, params))
            return false;
        memory_.read(mi + kMethodInfoInvokerOffset, inv);
        mp = memio::untag(mp);
        inv = memio::untag(inv);
        return params == 0 &&
               (mp == orig_fn || mp == hook_fn || (reg_go_fn != 0 && inv == reg_go_fn));
    }

    bool find_late_update_in_klass(uint64_t klass,
                                   uint64_t orig_fn,
                                   uint64_t hook_fn,
                                   uint64_t reg_go_fn,
                                   uint64_t& out_mi) {
        if (klass < 0x10000) return false;
        const uint64_t methods = ptr(klass + kClassMethodsOffset);
        uint16_t count = 0;
        if (methods < 0x10000 || !memory_.read(klass + kClassMethodCountOffset, count))
            return false;
        if (count < 16 || count > 64) return false;

        for (uint16_t i = 0; i < count; ++i) {
            const uint64_t mi = ptr(methods + (uint64_t)i * sizeof(uint64_t));
            if (mi < 0x10000) continue;
            if (ptr(mi + kMethodInfoKlassOffset) != klass) continue;
            uint8_t params = 0xff;
            if (!memory_.read(mi + kMethodInfoParamCountOffset, params) || params != 0)
                continue;
            const uint64_t mp = ptr(mi + kMethodInfoPtrOffset);
            const uint64_t inv = ptr(mi + kMethodInfoInvokerOffset);
            if (mp == orig_fn || mp == hook_fn || (reg_go_fn != 0 && inv == reg_go_fn)) {
                out_mi = mi;
                return true;
            }
        }
        return false;
    }

    uint64_t resolve_ragdoll_from_player(uint64_t player) {
        if (player < 0x10000) return 0;

        // 1) player + 0x80 (playerDeathHandlerReference) -> +0x18 (PlayerDeathHandler) -> +0xE0 (m_Ragdoll)
        const uint64_t pdh_ref = ptr(player + kPlayerDeathHandlerRefOffset);
        if (pdh_ref >= 0x10000) {
            const uint64_t pdh = ptr(pdh_ref + kInterfaceRefTargetOffset);
            if (pdh >= 0x10000) {
                const uint64_t ragdoll = ptr(pdh + kDeathHandlerRagdollOffset);
                if (ragdoll >= 0x10000) {
                    const uint64_t klass = ptr(ragdoll);
                    if (klass >= 0x10000) return klass;
                }
            }
        }

        // 2) player + 0x98 (inventory) -> +0x70 -> +0x30 (PlayerModelInfo) -> +0x60 (characterAnimation) -> +0x38 (ragdoll)
        const uint64_t inv = ptr(player + kPlayerInventoryOffset);
        if (inv >= 0x10000) {
            const uint64_t eq = ptr(inv + kInventoryEquipmentOffset);
            if (eq >= 0x10000) {
                const uint64_t model_info = ptr(eq + kEquipmentModelInfoOffset);
                if (model_info >= 0x10000) {
                    const uint64_t char_anim = ptr(model_info + kModelInfoCharAnimOffset);
                    if (char_anim >= 0x10000) {
                        const uint64_t ragdoll = ptr(char_anim + kCharAnimRagdollOffset);
                        if (ragdoll >= 0x10000) {
                            const uint64_t klass = ptr(ragdoll);
                            if (klass >= 0x10000) return klass;
                        }
                    }
                }
            }
        }

        // 3) player + 0x260 (uKx) -> +0x10 (PlayerModelInfo) -> +0x60 (characterAnimation) -> +0x38 (ragdoll)
        const uint64_t holder = ptr(player + kPlayerModelHolderOffset);
        if (holder >= 0x10000) {
            const uint64_t model_info = ptr(holder + kModelHolderInfoOffset);
            if (model_info >= 0x10000) {
                const uint64_t char_anim = ptr(model_info + kModelInfoCharAnimOffset);
                if (char_anim >= 0x10000) {
                    const uint64_t ragdoll = ptr(char_anim + kCharAnimRagdollOffset);
                    if (ragdoll >= 0x10000) {
                        const uint64_t klass = ptr(ragdoll);
                        if (klass >= 0x10000) return klass;
                    }
                }
            }
        }
        return 0;
    }

    bool scan_player_list_for_ragdoll_klass(uint64_t list,
                                            uint64_t orig_fn,
                                            uint64_t hook_fn,
                                            uint64_t reg_go_fn,
                                            uint64_t& out_mi) {
        if (list < 0x10000) return false;
        const uint64_t items = ptr(list + go_active::IL2CPP_LIST_ITEMS);
        int32_t count = 0;
        if (items < 0x10000 || !memory_.read(list + go_active::IL2CPP_LIST_SIZE, count))
            return false;
        if (count <= 0 || count > 512) return false;
        const int32_t limit = count < 64 ? count : 64;
        uint64_t last_checked_klass = 0;
        for (int32_t i = 0; i < limit; ++i) {
            const uint64_t player = ptr(items + go_active::IL2CPP_ARRAY_FIRST_ELEMENT +
                                        (uint64_t)i * sizeof(uint64_t));
            if (player < 0x10000) continue;
            const uint64_t ragdoll_klass = resolve_ragdoll_from_player(player);
            if (ragdoll_klass < 0x10000 || ragdoll_klass == last_checked_klass) continue;
            last_checked_klass = ragdoll_klass;
            if (find_late_update_in_klass(ragdoll_klass, orig_fn, hook_fn, reg_go_fn, out_mi))
                return true;
        }
        return false;
    }

    bool resolve_ragdoll_late_update(uint64_t il2cpp_base,
                                     uint64_t& out_mi,
                                     uint64_t& out_orig_fn,
                                     uint64_t& out_hook_fn,
                                     uint64_t& out_reg_go_fn) {
        const uint64_t orig_rva = go_active::RAGDOLL_LATE_UPDATE_RVA;
        const uint64_t hook_rva = go_active::OUTLINE_REGISTER_COMPONENT_RVA;
        const uint64_t reg_go_rva = go_active::OUTLINE_REGISTER_GAMEOBJECT_RVA;
        if (!orig_rva || !hook_rva) return false;
        const uint64_t orig_fn = il2cpp_base + orig_rva;
        const uint64_t hook_fn = il2cpp_base + hook_rva;
        const uint64_t reg_go_fn = reg_go_rva ? (il2cpp_base + reg_go_rva) : 0;
        out_orig_fn = orig_fn;
        out_hook_fn = hook_fn;
        out_reg_go_fn = reg_go_fn;

        // 1. Try Ragdoll.BodyPart[]_TypeInfo (element_class +0x40 -> Ragdoll.BodyPart -> declaringType +0x50 -> Ragdoll)
        //    or direct Ragdoll.BodyPart.declaringType (+0x50 -> Ragdoll).
        if (go_active::RAGDOLL_BODYPART_TYPEINFO_RVA != 0) {
            const uint64_t bp_klass = ptr(il2cpp_base + go_active::RAGDOLL_BODYPART_TYPEINFO_RVA);
            const uint64_t ragdoll_klass = ptr(bp_klass + kClassDeclaringTypeOffset);
            if (find_late_update_in_klass(ragdoll_klass, orig_fn, hook_fn, reg_go_fn, out_mi))
                return true;
            const uint64_t elem_klass = ptr(bp_klass + kClassElementClassOffset);
            if (elem_klass >= 0x10000) {
                const uint64_t elem_declaring = ptr(elem_klass + kClassDeclaringTypeOffset);
                if (find_late_update_in_klass(elem_declaring, orig_fn, hook_fn, reg_go_fn, out_mi))
                    return true;
            }
        }

        // 2. Fallback: walk players in PlayerManager lists to grab a live Ragdoll instance.
        if (go_active::PLAYER_MANAGER_TYPEINFO_RVA != 0) {
            const uint64_t pm_klass = ptr(il2cpp_base + go_active::PLAYER_MANAGER_TYPEINFO_RVA);
            const uint64_t pm_sf = ptr(pm_klass + kClassStaticFieldsOffset);
            if (pm_sf >= 0x10000) {
                const uint64_t client_list = ptr(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_LIST);
                if (scan_player_list_for_ragdoll_klass(client_list, orig_fn, hook_fn, reg_go_fn, out_mi))
                    return true;

                const uint64_t active_ra = ptr(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST);
                if (scan_player_list_for_ragdoll_klass(active_ra, orig_fn, hook_fn, reg_go_fn, out_mi))
                    return true;
                const uint64_t active_list = ptr(active_ra + kBufferListValuesOffset);
                if (scan_player_list_for_ragdoll_klass(active_list, orig_fn, hook_fn, reg_go_fn, out_mi))
                    return true;

                const uint64_t sleeper_ra = ptr(pm_sf + 0x00);
                const uint64_t sleeper_list = ptr(sleeper_ra + kBufferListValuesOffset);
                if (scan_player_list_for_ragdoll_klass(sleeper_list, orig_fn, hook_fn, reg_go_fn, out_mi))
                    return true;
            }
        }
        return false;
    }

    bool restore_method_info(uint64_t mi,
                             uint64_t orig_fn,
                             uint64_t orig_vfn,
                             uint64_t orig_invoker) {
        if (mi < 0x10000 || orig_fn < 0x10000) return false;
        bool ok = true;
        ok &= write_rw(mi + kMethodInfoPtrOffset, orig_fn);
        ok &= write_rw(mi + kMethodInfoVirtualPtrOffset, orig_vfn >= 0x10000 ? orig_vfn : orig_fn);
        if (orig_invoker >= 0x10000) {
            ok &= write_rw(mi + kMethodInfoInvokerOffset, orig_invoker);
        }
        return ok;
    }
};

} // namespace chams
