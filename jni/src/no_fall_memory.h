#pragma once
// «Без урона от падения»: только правка кучи, никаких записей в код и хуков.
//
// Что говорит дамп 11966 (dump.cs + libil2cpp.so):
//   Oxide.EntityVitals : GenericVitals (dump.cs:466678)
//     [SerializeField][Range(1, 30)] protected float m_MinFallSpeed;  0xC0
//     [SerializeField][Range(1, 60)] protected float m_MaxFallSpeed;  0xC4
//     public SoundPlayer m_FallDamageAudio;                           0xD8
//   обработчик приземления — виртуальный слот 23, EntityVitals.KrL(float),
//   RVA 0x689a9ac:
//     bl 0x5d44694            // скорость — нормальное число?
//     tbnz w0,#0 → ret        //   нет → выход
//     ldr s0,[x19,#0xc0]      // m_MinFallSpeed
//     fcmp s0,s8 / b.le 0x689a9e0   // дальше идут ТОЛЬКО если m_MinFallSpeed <= fallSpeed
//     ret                     // иначе приземление считается мягким
//   1e9 в 0xC0 делает «мягким» любое падение: ветка, где виталы реагируют на
//   удар (звук падения Oxide.SoundPlayer.Play и всё, что игра кладёт рядом),
//   перестаёт выполняться совсем.
//
// У игрока обработчик переопределён: PlayerVitals.KrL (RVA 0x68af9f4) базу не
// зовёт, а перекладывает полученную скорость в сетевой синк —
// x8=[this+0x108] (PlayerVitals.PlayerEvent), x0=[x8+0xf8] и хвостовой вызов
// 0xb604fdc (generic hkh<float>::set: пишет +0x20 значение, +0x24 прежнее и
// поднимает dirty). То есть у игрока локальный обработчик сам урон не считает —
// о силе удара судит та сторона, которой прилетит этот синк. Поэтому второй
// половиной фичи обнуляется источник числа, а не его приёмник.
//
// Источник — значение Move внутри контроллера:
//   HyperHug.Games.Oxide.Features.Player.Move (11966 struct Move 4309,
//   11968 struct Move 4453): State 0x00, Pose 0x04, Position 0x0C,
//   RealVelocity 0x18 (.y = 0x1C), BaseVelocity 0x38.
//   Кто вызывает слот 23 — только vtable (статических вызовов EntityVitals.KrL
//   в .text нет, проверено сканом), поэтому полагаться на один адрес нельзя:
//   гасим и скорость (RealVelocity.y), и порог (m_MinFallSpeed).
//
// Обнуляются обе вертикальные скорости значения Move — RealVelocity.y (0x1C) и
// BaseVelocity.y (0x3C): имя метода, который готовит аргумент слота 23,
// обфусцировано, поэтому трогается то, что игра и так пересчитывает каждый
// физический тик. Делается это, пока MoveState в диапазоне 0..4 (стоим/идём/
// бежим/пригнувшись/в полёте); лесенка (5), плавание (6), мёртвый (7) и
// транспорт (8) не трогаем.
// Между нашими тиками игра пишет туда правду, но прирост за кадр —
// g*dt ≈ 0.16 м/с, что ниже любого возможного порога (Range(1,30) у
// m_MinFallSpeed), так что в кадр приземления скорость приходит копеечной.
//
// m_MinFallSpeed возвращается игре, когда фича выключается, когда объект
// витал меняется (респаун) и при отвязке процесса. RealVelocity — поле самой
// игры и его возвращать нечего: перестали обнулять — следующий физический тик
// восстановит значение сам.
//
// KCC ищется тем же способом, что и у читателя мира (resolve_player_kcc в
// game.cpp): поле player.kccReference (0xB0) может быть обёрткой
// InterfaceReference<T>, поэтому сначала сам объект, потом его поля, потом
// скан полей игрока; настоящий контроллер опознаётся по бэклинку
// KCC.player (0x78) == player. Имя класса в обеих сборках читается как «KCC»
// (11966: 216635, 11968: 209108), но требовать его мы НЕ станем: имена
// ротируются между билдами, а бэклинк KCC + класс витал с бэклинком
// PlayerVitals.Player (0x100) держат цепочку надёжнее.
#include "viewmodel.h"
#include "game_offsets_active.h"
#include "weapon_tuning_memory.h"   // pointer(), Identity, Life
#include <cstdint>

namespace no_fall {
using weapon_tuning::pointer;
using weapon_tuning::Identity;
using weapon_tuning::Life;

inline constexpr const char* kOxide = "Oxide";
// Порог «это уже падение» в m_MinFallSpeed: заведомо недостижимая скорость.
inline constexpr float kMinFallSpeed = 1.0e9f;
// Меньше этой скорости вниз не трогаем: шаг с уступа и начало прыжка должны
// оставаться игрой, иначе обнуление залезет в анимацию и в «прилипание» к
// земле. 0.5 м/с — вдвое ниже минимально возможного порога игры (Range(1,30)).
inline constexpr float kSoftLandingSpeed = 0.5f;

struct Context {
    Identity player, vitals, kcc;
    uint64_t move = 0;     // kcc + KCC_MOVE: значение Move лежит внутри KCC
    uint64_t self = 0;     // player без тега — им сверяем бэклинки
    bool live() const { return kcc.object && vitals.object && move; }
    bool operator==(const Context& b) const {
        return player == b.player && vitals == b.vitals && kcc == b.kcc &&
               move == b.move && self == b.self;
    }
};
struct Scalar {
    float original = 0.f;
    bool owned = false;
};
inline bool valid(float f) { return viewmodel::finite(f) && f >= -1.0e9f && f <= 1.0e9f; }

template<class IO> class Controller {
    IO& io;
    uint64_t session_ = 0, base_ = 0;
    Context context_{};
    Scalar saved_{};          // оригинал m_MinFallSpeed
    int stage_ = 0;
    // Для журнала: что контроллер видел на последнем тике.
    float last_min_ = 0.f, last_speed_ = 0.f;
    unsigned clamps_ = 0;

    uint64_t min_speed_address() const {
        return context_.vitals.object + go_active::VITALS_MIN_FALL_SPEED;
    }
    bool fail(int stage) { stage_ = stage; return false; }

    bool looks_like_kcc(uint64_t candidate, uint64_t self) const {
        if (candidate < 0x10000) return false;
        uint64_t back = 0;
        return pointer(io, candidate + go_active::KCC_PLAYER_BACKREF, back) && back == self;
    }
    uint64_t resolve_kcc(uint64_t player, uint64_t self) {
        using namespace go_active;
        uint64_t reference = 0;
        if (pointer(io, player + PLAYER_KCC_REFERENCE, reference) && reference) {
            if (looks_like_kcc(reference, self)) return reference;
            for (uint64_t offset = 0x08; offset <= 0x60; offset += 8) {
                uint64_t candidate = 0;
                if (pointer(io, reference + offset, candidate) && looks_like_kcc(candidate, self))
                    return candidate;
            }
        }
        for (uint64_t offset = 0x68; offset <= 0x2C8; offset += 8) {
            uint64_t candidate = 0;
            if (pointer(io, player + offset, candidate) && looks_like_kcc(candidate, self))
                return candidate;
        }
        return 0;
    }

    bool resolve(uint64_t player, Context& c) {
        using namespace go_active;
        c = {};
        uint64_t net = 0, vitals = 0, back = 0, kcc = 0;
        uint8_t local = 0;
        if (!player || !io.named(player, "PlayerManager", kOxide)) return fail(1);
        const uint64_t self = player & 0x00ffffffffffffffULL;
        if (!c.player.capture(io, player, true)) return fail(2);
        if (!pointer(io, player + NETWORK_BEHAVIOUR_IDENTITY, net) ||
            !io.named(net, "NetworkIdentity", "Mirror")) return fail(3);
        if (!io.read(net + NETWORK_IDENTITY_IS_LOCAL_PLAYER, local) || local != 1) return fail(4);
        // Виталы локального игрока: PlayerManager.vitals (0xC8) → Oxide.PlayerVitals.
        if (!pointer(io, player + PLAYER_VITALS, vitals) || !vitals) return fail(5);
        if (!io.named(vitals, "PlayerVitals", kOxide)) return fail(5);
        if (!c.vitals.capture(io, vitals, true)) return fail(5);
        // 0xC0 само по себе объект не опознаёт (у ECM.BaseCharacterController
        // там похожий float), поэтому требуем бэклинк PlayerVitals.Player.
        if (!pointer(io, vitals + VITALS_PLAYER_BACKREF, back) || back != self) return fail(6);
        kcc = resolve_kcc(player, self);
        if (!kcc || !c.kcc.capture(io, kcc, true)) return fail(7);
        c.move = kcc + KCC_MOVE;   // значение внутри объекта, разыменования нет
        c.self = self;
        stage_ = 0;
        return true;
    }

    // Дешёвая проверка живости уже подтверждённой цепочки: два разыменования.
    // Полный резолв (со сканом полей игрока) — только впервые или при провале.
    bool chain_alive() {
        using namespace go_active;
        if (!context_.live()) return false;
        uint64_t vitals = 0, back = 0;
        if (!pointer(io, context_.player.object + PLAYER_VITALS, vitals)) return false;
        if (vitals != context_.vitals.object) return false;
        if (!pointer(io, context_.kcc.object + KCC_PLAYER_BACKREF, back)) return false;
        return back == context_.self;
    }

    // Половина 1: порог урона в виталах. Одна запись на контекст.
    bool patch_min_speed() {
        float now = 0.f;
        if (!io.read(min_speed_address(), now)) return fail(9);
        if (!valid(now)) return fail(9);
        last_min_ = now;
        if (now == kMinFallSpeed) return true;   // уже наше значение
        Context fresh;
        // Перед записью подтверждаем всю цепочку заново: адрес мог уехать на
        // респауне вместе с объектом витал.
        if (!resolve(context_.player.object, fresh)) return false;
        if (!(fresh == context_)) return fail(10);
        if (!io.read(min_speed_address(), now) || !valid(now)) return fail(10);
        if (now == kMinFallSpeed) return true;
        saved_.original = now;
        saved_.owned = true;
        if (!io.write_heap(min_speed_address(), kMinFallSpeed)) return fail(11);
        if (!io.read(min_speed_address(), now) || now != kMinFallSpeed) return fail(12);
        last_min_ = now;
        return true;
    }

    bool clamp_velocity_field(uint64_t address) {
        float vy = 0.f;
        if (!io.read(address, vy)) return fail(13);
        if (!valid(vy)) return fail(14);
        last_speed_ = vy;
        if (vy >= -kSoftLandingSpeed) return true;
        if (!io.write_heap(address, 0.f)) return fail(15);
        if (!io.read(address, vy)) return fail(16);
        last_speed_ = vy;
        ++clamps_;
        return true;
    }
    // Половина 2: обнуление вертикальной скорости в сетевом значении Move.
    bool clamp_fall_speed() {
        using namespace go_active;
        std::int32_t state = -1;
        if (!io.read(context_.move + KCC_MOVE_STATE, state)) return fail(13);
        if (state < 0 || state > KCC_MOVE_STATE_CLAMP_MAX) return true;
        if (!clamp_velocity_field(context_.move + KCC_MOVE_REAL_VELOCITY_Y)) return false;
        return clamp_velocity_field(context_.move + KCC_MOVE_BASE_VELOCITY_Y);
    }

    bool restore() {
        if (!saved_.owned) return true;
        const Life life = context_.vitals.object ? context_.vitals.life(io) : Life::Gone;
        if (life == Life::Unreadable) return fail(17);
        if (life != Life::Gone) {
            float now = 0.f;
            if (!io.read(min_speed_address(), now)) return fail(17);
            // Чужое обновление не затираем: возвращаем, только если там всё
            // ещё наше 1e9.
            if (now == kMinFallSpeed) {
                if (!io.write_heap(min_speed_address(), saved_.original)) return fail(17);
                float after = 0.f;
                if (!io.read(min_speed_address(), after) || after != saved_.original)
                    return fail(17);
            }
        }
        saved_ = {};
        return true;
    }

public:
    viewmodel::Status status = viewmodel::Status::Off;
    // Снимок для журнала: что видно по последнему успешному тику.
    struct Debug {
        uint64_t vitals = 0, move = 0;
        float min_fall_speed = 0.f, fall_speed = 0.f;
        unsigned clamps = 0;
        bool patched = false;
    };
    explicit Controller(IO& memory) : io(memory) {}
    int stage() const { return stage_; }
    bool has_state() const { return saved_.owned; }
    Debug debug() const {
        Debug d;
        d.vitals = context_.vitals.object;
        d.move = context_.move;
        d.min_fall_speed = last_min_;
        d.fall_speed = last_speed_;
        d.clamps = clamps_;
        d.patched = saved_.owned;
        return d;
    }
    bool reset(bool restore_originals) {
        const bool ok = !restore_originals || restore();
        saved_ = {};
        context_ = {};
        session_ = base_ = 0;
        stage_ = 0;
        last_min_ = last_speed_ = 0.f;
        clamps_ = 0;
        status = viewmodel::Status::Off;
        return ok;
    }
    void tick(uint64_t session, uint64_t base, uint64_t player, bool enabled) {
        using viewmodel::Status;
        if (session != session_ || base != base_) {
            reset(false);
            session_ = session;
            base_ = base;
        }
        status = enabled ? Status::Waiting : Status::Off;
        if (!enabled || !base) {
            stage_ = 0;
            if (!restore()) status = Status::Failed;
            return;
        }
        if (!context_.live() || !chain_alive()) {
            // Цепочка потерялась (респаун, смена объекта витал): старую правку
            // отдаём игре, новую ищем с нуля.
            const bool had_context = context_.live();
            if (!restore()) status = Status::Failed;
            if (had_context) context_ = {};
            Context next;
            if (!resolve(player, next)) return;
            context_ = next;
        }
        status = (patch_min_speed() && clamp_fall_speed()) ? Status::Active : Status::Failed;
    }
};
}
