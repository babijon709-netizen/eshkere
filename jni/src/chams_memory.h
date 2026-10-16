#pragma once
#include "game_offsets_active.h"
#include "mem_io.h"
#include "viewmodel.h"
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

    bool has_state() const { return active_ || saved_outline_ || saved_hook_; }
    viewmodel::Status status() const { return status_; }

    bool reset(bool restore, uint64_t il2cpp_base = 0) {
        bool ok = true;
        if (restore) {
            const uint64_t base = il2cpp_base ? il2cpp_base : last_base_;
            if (saved_hook_ && method_info_ >= 0x10000 && orig_fn_ >= 0x10000) {
                ok &= restore_method_info(method_info_, orig_fn_, hook_fn_);
            } else if (base >= 0x10000) {
                uint64_t mi = 0, orig_fn = 0, hook_fn = 0;
                if (resolve_ragdoll_late_update(base, mi, orig_fn, hook_fn)) {
                    uint64_t cur = 0;
                    if (memory_.read(mi + kMethodInfoPtrOffset, cur) &&
                        memio::untag(cur) == hook_fn) {
                        ok &= restore_method_info(mi, orig_fn, hook_fn);
                    }
                }
            }
            if (saved_outline_) {
                if (settings_ >= 0x10000)
                    ok &= memory_.write_heap(settings_ + kSettingsWidthOffset, orig_settings_);
                if (settings_obj_ >= 0x10000)
                    ok &= memory_.write_heap(settings_obj_ + kSettingsWidthOffset, orig_settings_obj_);
                if (feature_ >= 0x10000)
                    ok &= memory_.write_heap(feature_ + kFeatureUseGeometryOffset, orig_use_geometry_);
                if (pass_ >= 0x10000) {
                    ok &= memory_.write_heap(pass_ + kPassUseGeometryOffset, orig_use_geometry_);
                    clear_pass_list(pass_);
                }
            }
        }
        active_ = false;
        saved_hook_ = false;
        saved_outline_ = false;
        session_ = 0;
        last_base_ = 0;
        method_info_ = 0;
        orig_fn_ = 0;
        hook_fn_ = 0;
        feature_ = 0;
        pass_ = 0;
        settings_ = 0;
        settings_obj_ = 0;
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

        uint64_t feature = 0, pass = 0, settings = 0, settings_obj = 0;
        if (!resolve_outline(il2cpp_base, feature, pass, settings, settings_obj)) {
            status_ = viewmodel::Status::Waiting;
            return;
        }

        if (!saved_outline_ || feature != feature_ || settings != settings_) {
            SettingsSnapshot s{};
            uint8_t geom = 0;
            if (!memory_.read(settings + kSettingsWidthOffset, s) ||
                !memory_.read(feature + kFeatureUseGeometryOffset, geom)) {
                status_ = viewmodel::Status::Waiting;
                return;
            }
            orig_settings_ = s;
            orig_use_geometry_ = geom ? 1u : 0u;
            if (settings_obj >= 0x10000) {
                SettingsSnapshot so{};
                if (memory_.read(settings_obj + kSettingsWidthOffset, so))
                    orig_settings_obj_ = so;
                else
                    settings_obj = 0;
            }
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

        uint64_t mi = method_info_;
        uint64_t orig_fn = orig_fn_;
        uint64_t hook_fn = hook_fn_;
        if (!validate_method_info(mi, orig_fn, hook_fn)) {
            mi = 0;
            if (!resolve_ragdoll_late_update(il2cpp_base, mi, orig_fn, hook_fn)) {
                status_ = viewmodel::Status::Waiting;
                return;
            }
            method_info_ = mi;
            orig_fn_ = orig_fn;
            hook_fn_ = hook_fn;
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
            if (!memory_.write_heap(mi + kMethodInfoPtrOffset, hook_fn) ||
                !memory_.write_heap(mi + kMethodInfoVirtualPtrOffset, hook_fn)) {
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

        saved_hook_ = true;
        active_ = true;
        status_ = viewmodel::Status::Active;
    }

private:
    static constexpr uint64_t kClassDeclaringTypeOffset = 0x50;
    static constexpr uint64_t kClassMethodsOffset       = 0x98;
    static constexpr uint64_t kClassStaticFieldsOffset  = 0xB8;
    static constexpr uint64_t kClassMethodCountOffset   = 0x120;

    static constexpr uint64_t kMethodInfoPtrOffset        = 0x00;
    static constexpr uint64_t kMethodInfoVirtualPtrOffset = 0x08;
    static constexpr uint64_t kMethodInfoKlassOffset      = 0x20;
    static constexpr uint64_t kMethodInfoParamCountOffset = 0x52;

    static constexpr uint64_t kOutlineStaticSettingsOffset = 0x00;
    static constexpr uint64_t kOutlineStaticInitOffset     = 0x08;
    static constexpr uint64_t kOutlineStaticPassOffset     = 0x10;

    static constexpr uint64_t kFeatureSettingsOffset    = 0x28;
    static constexpr uint64_t kFeatureUseGeometryOffset = 0x30;

    static constexpr uint64_t kPassFeatureOffset     = 0xB8;
    static constexpr uint64_t kPassListOffset        = 0xC8;
    static constexpr uint64_t kPassUseGeometryOffset = 0xD8;

    static constexpr uint64_t kSettingsObjectOffset = 0x10;
    static constexpr uint64_t kSettingsWidthOffset  = 0x18;

    static constexpr uint64_t kPlayerModelHolderOffset   = 0x260;
    static constexpr uint64_t kModelHolderInfoOffset     = 0x10;
    static constexpr uint64_t kModelInfoCharAnimOffset   = 0x60;
    static constexpr uint64_t kCharAnimRagdollOffset     = 0x38;

    static constexpr uint64_t kBufferListValuesOffset    = 0x18;

    Memory& memory_;
    uint64_t session_ = 0;
    uint64_t last_base_ = 0;
    bool active_ = false;
    bool saved_hook_ = false;
    bool saved_outline_ = false;
    viewmodel::Status status_ = viewmodel::Status::Off;

    uint64_t method_info_ = 0;
    uint64_t orig_fn_ = 0;
    uint64_t hook_fn_ = 0;

    uint64_t feature_ = 0;
    uint64_t pass_ = 0;
    uint64_t settings_ = 0;
    uint64_t settings_obj_ = 0;
    uint8_t orig_use_geometry_ = 0;
    SettingsSnapshot orig_settings_{};
    SettingsSnapshot orig_settings_obj_{};

    uint64_t ptr(uint64_t addr) {
        uint64_t v = 0;
        return addr >= 0x10000 && memory_.read(addr, v) ? memio::untag(v) : 0;
    }

    bool resolve_outline(uint64_t il2cpp_base,
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

        uint8_t is_init = 0;
        if (!memory_.read(sf + kOutlineStaticInitOffset, is_init) || !is_init)
            return false;

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
            memory_.write_heap(settings + kSettingsWidthOffset, desired);
        }
        if (settings_obj >= 0x10000 &&
            memory_.read(settings_obj + kSettingsWidthOffset, cur) &&
            std::memcmp(&cur, &desired, sizeof(desired)) != 0) {
            memory_.write_heap(settings_obj + kSettingsWidthOffset, desired);
        }
        uint8_t cur_geom = 0;
        if (memory_.read(feature + kFeatureUseGeometryOffset, cur_geom) &&
            (cur_geom ? 1u : 0u) != desired_geom) {
            memory_.write_heap(feature + kFeatureUseGeometryOffset, desired_geom);
        }
        if (memory_.read(pass + kPassUseGeometryOffset, cur_geom) &&
            (cur_geom ? 1u : 0u) != desired_geom) {
            memory_.write_heap(pass + kPassUseGeometryOffset, desired_geom);
        }
    }

    void clear_pass_list(uint64_t pass) {
        const uint64_t list = ptr(pass + kPassListOffset);
        if (list < 0x10000) return;
        int32_t sz = 0;
        if (memory_.read(list + go_active::IL2CPP_LIST_SIZE, sz) && sz > 0 && sz <= 512) {
            const int32_t zero = 0;
            memory_.write_heap(list + go_active::IL2CPP_LIST_SIZE, zero);
        }
    }

    bool validate_method_info(uint64_t mi, uint64_t orig_fn, uint64_t hook_fn) {
        if (mi < 0x10000 || orig_fn < 0x10000 || hook_fn < 0x10000) return false;
        uint64_t mp = 0;
        uint8_t params = 0xff;
        if (!memory_.read(mi + kMethodInfoPtrOffset, mp) ||
            !memory_.read(mi + kMethodInfoParamCountOffset, params))
            return false;
        mp = memio::untag(mp);
        return params == 0 && (mp == orig_fn || mp == hook_fn);
    }

    bool find_late_update_in_klass(uint64_t klass,
                                   uint64_t orig_fn,
                                   uint64_t hook_fn,
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
            if (mp == orig_fn || mp == hook_fn) {
                out_mi = mi;
                return true;
            }
        }
        return false;
    }

    bool scan_player_list_for_ragdoll_klass(uint64_t list,
                                            uint64_t orig_fn,
                                            uint64_t hook_fn,
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
            const uint64_t holder = ptr(player + kPlayerModelHolderOffset);
            if (holder < 0x10000) continue;
            const uint64_t model_info = ptr(holder + kModelHolderInfoOffset);
            if (model_info < 0x10000) continue;
            const uint64_t char_anim = ptr(model_info + kModelInfoCharAnimOffset);
            if (char_anim < 0x10000) continue;
            const uint64_t ragdoll = ptr(char_anim + kCharAnimRagdollOffset);
            if (ragdoll < 0x10000) continue;
            const uint64_t ragdoll_klass = ptr(ragdoll);
            if (ragdoll_klass < 0x10000 || ragdoll_klass == last_checked_klass) continue;
            last_checked_klass = ragdoll_klass;
            if (find_late_update_in_klass(ragdoll_klass, orig_fn, hook_fn, out_mi))
                return true;
        }
        return false;
    }

    bool resolve_ragdoll_late_update(uint64_t il2cpp_base,
                                     uint64_t& out_mi,
                                     uint64_t& out_orig_fn,
                                     uint64_t& out_hook_fn) {
        const uint64_t orig_rva = go_active::RAGDOLL_LATE_UPDATE_RVA;
        const uint64_t hook_rva = go_active::OUTLINE_REGISTER_COMPONENT_RVA;
        if (!orig_rva || !hook_rva) return false;
        const uint64_t orig_fn = il2cpp_base + orig_rva;
        const uint64_t hook_fn = il2cpp_base + hook_rva;
        out_orig_fn = orig_fn;
        out_hook_fn = hook_fn;

        // 1. Try Ragdoll.BodyPart.declaringType (+0x50 -> Ragdoll Il2CppClass*).
        if (go_active::RAGDOLL_BODYPART_TYPEINFO_RVA != 0) {
            const uint64_t bp_klass = ptr(il2cpp_base + go_active::RAGDOLL_BODYPART_TYPEINFO_RVA);
            const uint64_t ragdoll_klass = ptr(bp_klass + kClassDeclaringTypeOffset);
            if (find_late_update_in_klass(ragdoll_klass, orig_fn, hook_fn, out_mi))
                return true;
        }

        // 2. Fallback: walk remote/sleeping players in PlayerManager lists to
        //    grab a live Ragdoll instance and read its Il2CppClass* header.
        if (go_active::PLAYER_MANAGER_TYPEINFO_RVA != 0) {
            const uint64_t pm_klass = ptr(il2cpp_base + go_active::PLAYER_MANAGER_TYPEINFO_RVA);
            const uint64_t pm_sf = ptr(pm_klass + kClassStaticFieldsOffset);
            if (pm_sf >= 0x10000) {
                const uint64_t client_list = ptr(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_LIST);
                if (scan_player_list_for_ragdoll_klass(client_list, orig_fn, hook_fn, out_mi))
                    return true;

                const uint64_t active_ra = ptr(pm_sf + go_active::PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST);
                const uint64_t active_list = ptr(active_ra + kBufferListValuesOffset);
                if (scan_player_list_for_ragdoll_klass(active_list, orig_fn, hook_fn, out_mi))
                    return true;

                const uint64_t sleeper_ra = ptr(pm_sf + 0x00);
                const uint64_t sleeper_list = ptr(sleeper_ra + kBufferListValuesOffset);
                if (scan_player_list_for_ragdoll_klass(sleeper_list, orig_fn, hook_fn, out_mi))
                    return true;
            }
        }
        return false;
    }

    bool restore_method_info(uint64_t mi, uint64_t orig_fn, uint64_t hook_fn) {
        uint64_t cur_mp = 0;
        if (!memory_.read(mi + kMethodInfoPtrOffset, cur_mp)) return false;
        cur_mp = memio::untag(cur_mp);
        if (cur_mp != orig_fn && cur_mp != hook_fn) return false;
        bool ok = true;
        ok &= memory_.write_heap(mi + kMethodInfoPtrOffset, orig_fn);
        ok &= memory_.write_heap(mi + kMethodInfoVirtualPtrOffset, orig_fn);
        return ok;
    }
};

} // namespace chams
