#pragma once
// Testable memory controller. IO must provide fresh read<T>, write<T>, ptr,
// and is(object, className) (strict Oxide namespace check).
#include "viewmodel.h"
#include "game_offsets_active.h"
#include <array>
#include <cmath>

namespace viewmodel {
struct Position { float x, y, z; };
struct BlendSpeed { float value; };
inline bool valid(BlendSpeed v) { return finite(v.value) && v.value >= 0.f && v.value <= 10000.f; }
inline bool valid(float v) { return finite(v) && v > 1.f && v < 179.f; }
// Целые поля скинченджера (skin_changer_memory.h): Patch<T> резолвит valid()
// в этом пространстве имён, а своих целых перегрузок здесь не было.
//   int16_t — Oxide.WeaponPiece.Skin, индекс в ItemData.skinData[];
//   uint8_t — WeaponPiece.SkinLevel (уровень прогрессии) И WeaponPiece.Enabled
//             (bool 0/1, которым скинченджер заставляет игру пересобрать
//             визуал на холстер/дроу) — оба укладываются в 0..7.
// Пределы совпадают с game_offsets::SKIN_INDEX_MAX / SKIN_LEVEL_MAX.
inline bool valid(std::int16_t v) { return v >= 0 && v <= go_active::SKIN_INDEX_MAX; }
//   int32_t — Oxide.Item.skin: тот же индекс скина, но поле предмета
//             4-байтовое (третья точка записи скинченджера — триггер
//             дроп/подбор, см. skin_changer_memory.h и KERNEL.md §4.17).
inline bool valid(std::int32_t v) { return v >= 0 && v <= go_active::SKIN_INDEX_MAX; }
inline bool valid(std::uint8_t v) { return v <= go_active::SKIN_LEVEL_MAX; }
inline bool valid(Position v) {
    return finite(v.x) && finite(v.y) && finite(v.z) &&
           v.x > -10.f && v.x < 10.f && v.y > -10.f && v.y < 10.f && v.z > -10.f && v.z < 10.f;
}
template<class T> bool same(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
// Поле, которое игра пересчитывает сама (lerp позы руки, нормализованный
// вектор), в биты с нашим желаемым не совпадёт никогда — сравнение по
// точному значению превращает «писать по расхождению» в «писать каждый
// кадр». Для таких полей расхождение считается с допуском, и допуск берётся
// много меньше того, что видно глазом (см. вызовы).
// Значения, у которых «почти равно» не бывает (целые счётчики, перечисления
// кадрового лимитера), сравниваются только точно — для них допуск не определён.
template<class T> inline bool close(const T&, const T&, float) { return false; }
inline bool close(float a, float b, float eps) {
    return finite(a) && finite(b) && std::fabs(a - b) <= eps;
}
inline bool close(const Position& a, const Position& b, float eps) {
    return finite(a.x) && finite(a.y) && finite(a.z) &&
           finite(b.x) && finite(b.y) && finite(b.z) &&
           std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
           std::fabs(a.z - b.z) <= eps;
}

struct Identity {
    uint64_t object = 0, klass = 0, native = 0;
    template<class IO> bool capture(IO& io, uint64_t obj) {
        object = obj; klass = io.ptr(obj);
        native = io.ptr(obj + go_active::MANAGED_CACHED_PTR);
        return object && klass && native;
    }
    template<class IO> bool alive(IO& io) const {
        return object && klass && native && io.ptr(object) == klass &&
            io.ptr(object + go_active::MANAGED_CACHED_PTR) == native;
    }
};

template<class T> struct Patch {
    uint64_t address = 0;
    T original{}, last{};
    T sampled{};                 // что в этом тике читали по address
    bool written = false;
    bool fresh = false;          // sampled снят в этом тике и ещё не израсходован
    template<class IO> bool sample(IO& io, uint64_t addr) {
        T current{};
        if (!io.read(addr, current) || !valid(current)) return false;
        if (!written || address != addr || !same(current, last)) original = current;
        address = addr; sampled = current; fresh = true;
        return true;
    }
    // Пишем только по расхождению. Решает снимок этого же тика, а не last:
    // поле принадлежит и игре, вернёт своё — sample() это увидит и запись
    // случится. Пока значение уже наше, тик стоит одно чтение и ноль записей,
    // иначе каждая включённая функция оставляла бы детектору раунд-трип
    // в память на каждом кадре.
    template<class IO> bool put(IO& io, T value, float eps = 0.0f) {
        if (!valid(value)) return false;
        if (fresh && (same(value, sampled) || close(value, sampled, eps))) { fresh = false; return true; }
        if (!io.write(address, value)) return false;
        last = value; written = true; sampled = value; fresh = false; return true;
    }
    template<class IO> bool restore(IO& io) {
        if (!written) { *this = {}; return true; }
        T current{};
        if (!io.read(address, current)) return false;
        // The game owns this field too. Do not restore an obsolete value over
        // an ADS/weapon-state update which has already replaced our last write.
        if (same(current, last) && !io.write(address, original)) return false;
        *this = {}; return true;
    }
};

template<class IO> class Controller {
    IO& io;
    uint64_t session_ = 0;
    Identity player_, fp_, weapon_, motion_;
    Patch<float> fov_;
    struct HandPatch {
        uint64_t object = 0, klass = 0, slot = 0;
        Patch<Position> target;
        Patch<Position> current;
        Patch<BlendSpeed> speed;
    };
    std::array<HandPatch, 8> hands_{};
    unsigned retry_ = 0;

    bool restore_hands() {
        using namespace go_active;
        if (!player_.alive(io) || !weapon_.alive(io) || !motion_.alive(io) ||
            io.ptr(weapon_.object + FPOBJECT_PLAYER_BACKREF) != player_.object ||
            io.ptr(motion_.object + FPMOTION_OBJECT) != weapon_.object) {
            hands_ = {}; return true;
        }
        bool ok = true;
        for (auto& h : hands_) {
            if (!h.object) continue;
            if (io.ptr(h.slot) != h.object || io.ptr(h.object) != h.klass) { h = {}; continue; }
            bool targetRestored = h.target.restore(io);
            bool currentRestored = h.current.restore(io);
            bool speedRestored = h.speed.restore(io);
            if (targetRestored && currentRestored && speedRestored) h = {};
            else ok = false;
        }
        return ok;
    }
    bool restore_fov() {
        if (!player_.alive(io) || !fp_.alive(io)) { fov_ = {}; return true; }
        return fov_.restore(io);
    }
    uint64_t motion_from_action(uint64_t action, uint64_t weapon, unsigned depth, unsigned& budget) {
        using namespace go_active;
        if (!action || depth > 2 || !budget) return 0;
        --budget;
        uint64_t target = io.ptr(action + DELEGATE_TARGET);
        if (target && io.is(target, "FPMotion") &&
            io.ptr(target + FPMOTION_OBJECT) == weapon &&
            io.ptr(target + FPMOTION_PLAYER) == io.ptr(player_.object + PLAYER_EVENT_HANDLER) &&
            io.ptr(target + MANAGED_CACHED_PTR)) return target;
        uint64_t list = io.ptr(action + MULTICAST_DELEGATES), count = 0;
        if (!list || !io.read(list + IL2CPP_ARRAY_LENGTH, count) || count > 32) return 0;
        for (uint64_t i = 0; i < count && budget; ++i) {
            auto found = motion_from_action(io.ptr(list + IL2CPP_ARRAY_FIRST_ELEMENT + i * 8), weapon, depth + 1, budget);
            if (found) return found;
        }
        return 0;
    }
public:
    Diagnostics status{};
    explicit Controller(IO& memory) : io(memory) {}
    bool has_state() const { return player_.object != 0; }
    void reset(bool restore) {
        if (restore) { restore_fov(); restore_hands(); }
        player_ = {}; fp_ = {}; weapon_ = {}; motion_ = {};
        fov_ = {}; hands_ = {}; retry_ = 0; session_ = 0; status = {};
    }
    void tick(uint64_t session, uint64_t player, ViewmodelSettings s) {
        using namespace go_active;
        s = sanitize(s);
        if (session != session_) { reset(false); session_ = session; }
        status = {s.fov_enabled ? Status::Waiting : Status::Off, s.hands_enabled ? Status::Waiting : Status::Off};
        if (!s.fov_enabled && !s.hands_enabled && !player_.object) return;
        // No writes through a cached local-player address after scene/respawn.
        if (player != player_.object || !player_.alive(io)) {
            reset(false); session_ = session;
            status = {s.fov_enabled ? Status::Waiting : Status::Off, s.hands_enabled ? Status::Waiting : Status::Off};
            if (!player || !io.is(player, "PlayerManager") || !player_.capture(io, player)) return;
        }
        uint64_t fp = io.ptr(player + PLAYER_FP_MANAGER);
        if (!fp || !io.is(fp, "FPManager")) return;
        if (fp != fp_.object || !fp_.alive(io)) {
            if (!restore_fov() || !restore_hands()) { status = {Status::Failed, Status::Failed}; return; }
            weapon_ = {}; motion_ = {}; retry_ = 0;
            if (!fp_.capture(io, fp)) return;
        }
        uint64_t handler = io.ptr(player + PLAYER_EVENT_HANDLER);
        const bool handlerKnown = handler &&
            io.ptr(handler + EVENT_HANDLER_MANAGER_BACKREF) == player;
        // Custom FOV is authoritative in every state, including ADS. The old
        // controller restored the game's ADS value here, which looked like the
        // setting randomly switched off whenever the player aimed.
        if (!s.fov_enabled) {
            const bool ok=restore_fov();
            status.fov=ok?Status::Off:Status::Failed;
        } else {
            const bool ok=fov_.sample(io,fp+FPMANAGER_WORLD_FOV_TARGET) &&
                          fov_.put(io,s.fov);
            status.fov=ok?Status::Active:Status::Failed;
        }
        uint64_t weapon = io.ptr(fp + FPMANAGER_CURRENT_OBJECT);
        if (!weapon) weapon = io.ptr(fp + FPMANAGER_CURRENT_WEAPON);
        if (weapon != weapon_.object || !weapon_.alive(io)) {
            if (!restore_hands()) { status.hands = Status::Failed; return; }
            motion_ = {}; retry_ = 0; weapon_ = {};
            if (weapon && io.ptr(weapon + FPOBJECT_PLAYER_BACKREF) == player) weapon_.capture(io, weapon);
        }
        // Custom hands stay active in normal movement states. Disabled is the
        // only state that restores those owned slots; the native ADS slot is
        // deliberately never captured or written below.
        if (!s.hands_enabled) {
            const bool ok=restore_hands();
            status.hands=ok?Status::Off:Status::Failed;
            if (!s.fov_enabled && status.fov==Status::Off && status.hands==Status::Off) {
                reset(false); session_=session;
            }
            return;
        }
        if (!handlerKnown || !weapon_.alive(io) ||
            io.ptr(weapon+FPOBJECT_PLAYER_BACKREF)!=player) return;
        if (!motion_.alive(io) || io.ptr(motion_.object + FPMOTION_OBJECT) != weapon ||
            io.ptr(motion_.object + FPMOTION_PLAYER) != handler) {
            if (!restore_hands()) { status.hands = Status::Failed; return; }
            motion_ = {};
            if (retry_) { --retry_; return; }
            retry_ = 30; // bounded search; at most once per 30 overlay frames
            unsigned budget = 64;
            auto event = io.ptr(weapon + FPOBJECT_DRAW_EVENT);
            auto found = motion_from_action(event ? io.ptr(event + FPDRAW_ACTION) : 0, weapon, 0, budget);
            if (!found || !motion_.capture(io, found)) return;
            retry_ = 0;
        }
        // m_AimOffset belongs to the game's sight alignment. Replacing either
        // its target or smoothed cache with the static custom-hands value makes
        // the weapon stop lining up with the sight. Keep ADS fully native while
        // applying custom hands to every non-ADS pose.
        const uint64_t offsets[] = {FPMOTION_IDLE_OFFSET, FPMOTION_RUN_OFFSET,
            FPMOTION_RUN_RELOAD_OFFSET, FPMOTION_ATTACK_OFFSET,
            FPMOTION_LADDER_OFFSET, FPMOTION_JUMP_OFFSET, FPMOTION_LOOK_OFFSET,
            FPMOTION_CLOSE_OFFSET};
        const uint64_t native_aim_object = io.ptr(motion_.object + FPMOTION_AIM_OFFSET);
        bool any = false, ok = true;
        for (size_t i = 0; i < hands_.size(); ++i) {
            auto& h = hands_[i];
            uint64_t slot = motion_.object + offsets[i], object = io.ptr(slot);
            // A prefab may alias a normal slot to its ADS TransformOffset.
            // Native sight alignment wins: release any old ownership and skip
            // the shared object regardless of which slot exposed it.
            if (object && object == native_aim_object) {
                const bool targetRestored = h.target.restore(io);
                const bool currentRestored = h.current.restore(io);
                const bool speedRestored = h.speed.restore(io);
                if (targetRestored && currentRestored && speedRestored) h = {};
                else ok = false;
                continue;
            }
            if (h.object != object) {
                // An unlinked offset may have been freed: never restore via it.
                h = {};
                // A prefab may start sharing an offset between poses. Transfer
                // its existing snapshot instead of sampling our own shifted value.
                for (size_t j = i + 1; object && j < hands_.size(); ++j) {
                    if (hands_[j].object == object && io.ptr(hands_[j].slot) == object) {
                        h = hands_[j]; hands_[j] = {}; break;
                    }
                }
            }
            if (!object) continue;
            bool duplicate = false;
            for (size_t j = 0; j < i; ++j) if (hands_[j].object == object) duplicate = true;
            if (duplicate) continue;
            if (!io.is(object, "TransformOffset")) { ok = false; continue; }
            const uint64_t klass = io.ptr(object);
            if (h.klass && h.klass != klass) h = {};
            h.object = object; h.klass = klass; h.slot = slot;
            if (!h.target.sample(io, object + TRANSFORMOFFSET_POSITION) ||
                !h.current.sample(io, object + TRANSFORMOFFSET_CURRENT_POSITION)) {
                ok = false;
                continue;
            }
            // FPMotion selects one of these non-ADS targets, then TransformOffset
            // interpolates m_Position into its separate current-position cache
            // at +0x44.  Some idle poses have a zero/very low lerp speed, which
            // is why changing only m_Position appeared to work only while
            // running.  Hold both the target and current cache at the requested
            // absolute value; state transitions are now static immediately.
            const Position value{s.x*0.01f,s.y*0.01f,s.z*0.01f};
            // Цель позы игра пишет только при смене состояния — там хватит
            // точного сравнения. Кэш m_Position она интерполирует каждый кадр,
            // поэтому для него допуск 0,2 мм: визуально ноль, а запись
            // остаётся только когда рука реально уехала.
            const bool targetApplied = h.target.put(io, value);
            const bool currentApplied = h.current.put(io, value, 0.0002f);
            // Retired ADS snap helper may still own a speed from an older tick;
            // restore it once, then leave animation speed to the game.
            const bool speedApplied = h.speed.restore(io);
            if (!targetApplied || !currentApplied || !speedApplied) ok=false;
            else any=true;
        }
        status.hands = !ok ? Status::Failed : (any ? Status::Active : Status::Waiting);
    }
};
} // namespace viewmodel
