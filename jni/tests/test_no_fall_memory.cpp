// Хост-тест «без урона от падения» (jni/src/no_fall_memory.h).
//
// Проверяется то, что нельзя проверить на устройстве быстро: форма цепочки
// (обёртка kccReference, бэклинки), идемпотентность, откат оригинала при
// выключении и при подмене объекта витал, ворота по состояниям Move и по
// «мягкому» приземлению, а также профиль 11968 (у беты свой KCC_MOVE).
#include "no_fall_memory.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>

struct FakeIO {
    std::map<uint64_t, uint8_t> mem;
    std::map<uint64_t, std::pair<std::string, std::string>> types;
    uint64_t fail_read = 0, fail_write = 0;
    unsigned writes = 0;

    template<class T> void put(uint64_t a, T v) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(v); ++i) mem[a + i] = bytes[i];
    }
    template<class T> bool read(uint64_t a, T& v) {
        if (a == fail_read) return false;
        auto* bytes = reinterpret_cast<uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(v); ++i) {
            auto it = mem.find(a + i);
            if (it == mem.end()) return false;
            bytes[i] = it->second;
        }
        return true;
    }
    template<class T> bool write_heap(uint64_t a, T v) {
        if (a == fail_write) return false;
        ++writes;
        put(a, v);
        return true;
    }
    bool named(uint64_t a, const char* n, const char* ns) {
        auto it = types.find(a);
        return it != types.end() && it->second.first == n && it->second.second == ns;
    }
    void object(uint64_t a, const char* n, const char* ns) {
        types[a] = {n, ns};
        put<uint64_t>(a, a + 0x800000);                                    // klass
        put<uint64_t>(a + go_active::MANAGED_CACHED_PTR, a + 0x900000);    // m_CachedPtr
    }
    template<class T> T value(uint64_t a) { T v{}; assert(read(a, v)); return v; }
};

namespace {

constexpr uint64_t kPlayer = 0x10000, kNet = 0x20000, kWrapper = 0x30000, kKcc = 0x40000;
constexpr uint64_t kVitals = 0x50000, kVitals2 = 0x58000;

struct Fixture {
    FakeIO io;
    no_fall::Controller<FakeIO> ctl{io};
    uint64_t vitals = kVitals;

    void build_vitals(uint64_t v, float min_fall_speed) {
        using namespace go_active;
        io.object(v, "PlayerVitals", "Oxide");
        io.put<float>(v + VITALS_MIN_FALL_SPEED, min_fall_speed);
        io.put<uint64_t>(v + VITALS_PLAYER_BACKREF, kPlayer);
    }
    void build_kcc(uint64_t kcc, int32_t state, float vy) {
        using namespace go_active;
        io.object(kcc, "KCC", "HyperHug.Games.Oxide.Features.Player");
        io.put<uint64_t>(kcc + KCC_PLAYER_BACKREF, kPlayer);
        io.put<int32_t>(kcc + KCC_MOVE + KCC_MOVE_STATE, state);
        io.put<float>(kcc + KCC_MOVE + KCC_MOVE_REAL_VELOCITY_Y, vy);
        io.put<float>(kcc + KCC_MOVE + KCC_MOVE_BASE_VELOCITY_Y, vy);
    }
    void bind_vitals(uint64_t v) {
        using namespace go_active;
        vitals = v;
        io.put<uint64_t>(kPlayer + PLAYER_VITALS, v);
    }
    Fixture(int32_t state = 4, float vy = -25.f, float min_fall = 8.5f) {
        using namespace go_active;
        io.object(kPlayer, "PlayerManager", "Oxide");
        io.object(kNet, "NetworkIdentity", "Mirror");
        io.put<uint64_t>(kPlayer + NETWORK_BEHAVIOUR_IDENTITY, kNet);
        io.put<uint8_t>(kNet + NETWORK_IDENTITY_IS_LOCAL_PLAYER, 1);
        // kccReference — обёртка InterfaceReference<KCC>: сам объект контроллером
        // не является, контроллер лежит в его поле +0x10. Так проверяется
        // «прощупывание» обёртки, а не только прямой указатель.
        io.object(kWrapper, "InterfaceReference_1", "Oxide");
        io.put<uint64_t>(kPlayer + PLAYER_KCC_REFERENCE, kWrapper);
        io.put<uint64_t>(kWrapper + 0x10, kKcc);
        build_kcc(kKcc, state, vy);
        build_vitals(kVitals, min_fall);
        bind_vitals(kVitals);
    }
    uint64_t min_speed_address() const {
        return vitals + go_active::VITALS_MIN_FALL_SPEED;
    }
    uint64_t velocity_y_address() const {
        return kKcc + go_active::KCC_MOVE + go_active::KCC_MOVE_REAL_VELOCITY_Y;
    }
    uint64_t base_y_address() const {
        return kKcc + go_active::KCC_MOVE + go_active::KCC_MOVE_BASE_VELOCITY_Y;
    }
    void tick(bool enabled = true) { ctl.tick(1, 0x1000000, kPlayer, enabled); }
};

void test_patch_and_clamp() {
    using namespace go_active;
    Fixture f;
    f.tick();
    assert(f.ctl.status == viewmodel::Status::Active);
    assert(f.ctl.stage() == 0);
    assert(f.io.value<float>(f.min_speed_address()) == no_fall::kMinFallSpeed);
    assert(f.io.value<float>(f.velocity_y_address()) == 0.f);
    assert(f.ctl.has_state());
    // Идемпотентность: порог уже наш, скорость уже нулевая — записей нет.
    const unsigned writes = f.io.writes;
    f.io.put<float>(f.velocity_y_address(), -25.f);   // игра обновила скорость
    f.io.put<float>(f.base_y_address(), -25.f);
    f.tick();
    assert(f.io.writes == writes + 2);   // обнулены обе скорости Move, виталы не тронуты
    assert(f.io.value<float>(f.base_y_address()) == 0.f);
    assert(f.io.value<float>(f.velocity_y_address()) == 0.f);
    assert(f.io.value<float>(f.min_speed_address()) == no_fall::kMinFallSpeed);
    // Выключение: оригинал порога возвращается, скорость остаётся у игры.
    f.tick(false);
    assert(f.ctl.status == viewmodel::Status::Off);
    assert(!f.ctl.has_state());
    assert(std::fabs(f.io.value<float>(f.min_speed_address()) - 8.5f) < 1e-6f);
    // Включение заново — правка идёт поверх восстановленного значения.
    f.tick(true);
    assert(f.ctl.status == viewmodel::Status::Active);
    assert(f.io.value<float>(f.min_speed_address()) == no_fall::kMinFallSpeed);
}

void test_state_and_soft_landing_gates() {
    {   // плывём (MoveState 6): скорость не трогаем, порог всё равно глушим
        Fixture f(6, -25.f);
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Active);
        assert(f.io.value<float>(f.velocity_y_address()) == -25.f);
        assert(f.io.value<float>(f.base_y_address()) == -25.f);
        assert(f.io.value<float>(f.min_speed_address()) == no_fall::kMinFallSpeed);
    }
    {   // мёртв (7) — тоже не трогаем
        Fixture f(7, -25.f);
        f.tick();
        assert(f.io.value<float>(f.velocity_y_address()) == -25.f);
    }
    {   // мягкое приземление: -0.2 м/с оставляем игре
        Fixture f(4, -0.2f);
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Active);
        assert(f.io.value<float>(f.velocity_y_address()) == -0.2f);
        assert(f.io.value<float>(f.base_y_address()) == -0.2f);
    }
    {   // вверх (прыжок) — не обнуляем
        Fixture f(4, 7.f);
        f.tick();
        assert(f.io.value<float>(f.velocity_y_address()) == 7.f);
    }
    {   // шаг с уступа (WALKING) — правим, иначе урон придёт с земли
        Fixture f(1, -25.f);
        f.tick();
        assert(f.io.value<float>(f.velocity_y_address()) == 0.f);
    }
}

void test_respawn_switches_vitals_object() {
    using namespace go_active;
    Fixture f;
    f.tick();
    assert(f.io.value<float>(f.min_speed_address()) == no_fall::kMinFallSpeed);
    // Игрок возродился: PlayerManager.vitals указывает на новый объект.
    f.build_vitals(kVitals2, 12.f);
    f.bind_vitals(kVitals2);
    f.tick();
    assert(f.ctl.status == viewmodel::Status::Active);
    // Старый объект отдаётся игре, новый перехвачен.
    assert(std::fabs(f.io.value<float>(kVitals + VITALS_MIN_FALL_SPEED) - 8.5f) < 1e-6f);
    assert(f.io.value<float>(kVitals2 + VITALS_MIN_FALL_SPEED) == no_fall::kMinFallSpeed);
    const no_fall::Controller<FakeIO>::Debug d = f.ctl.debug();
    assert(d.vitals == kVitals2);
    assert(d.clamps == 2u);   // RealVelocity + BaseVelocity на первом тике
}

void test_guards() {
    using namespace go_active;
    {   // не локальный игрок — ни одной записи
        Fixture f;
        f.io.put<uint8_t>(kNet + NETWORK_IDENTITY_IS_LOCAL_PLAYER, 0);
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Waiting);
        assert(f.ctl.stage() == 4);
        assert(f.io.writes == 0u);
    }
    {   // нет игрока (цепочка ещё не разобралась) — ждём, оригинал не трогаем
        Fixture f;
        f.ctl.tick(1, 0x1000000, 0, true);
        assert(f.ctl.status == viewmodel::Status::Waiting);
        assert(f.ctl.stage() == 1);   // резолв честно говорит «нет игрока»
        assert(std::fabs(f.io.value<float>(f.min_speed_address()) - 8.5f) < 1e-6f);
    }
    {   // бэклинк витал не совпал — чужой объект не правим
        Fixture f;
        f.io.put<uint64_t>(kVitals + go_active::VITALS_PLAYER_BACKREF, 0x990000);
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Waiting);
        assert(f.ctl.stage() == 6);
        assert(f.io.writes == 0u);
    }
    {   // KCC не находится (обёртка врёт) — цепочка рвётся до записи
        Fixture f;
        f.io.put<uint64_t>(kWrapper + 0x10, 0);
        f.io.put<uint64_t>(kPlayer + go_active::PLAYER_KCC_REFERENCE, 0);
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Waiting);
        assert(f.ctl.stage() == 7);
        assert(f.io.value<float>(f.min_speed_address()) == 8.5f);
    }
    {   // запись отклонена воротами кучи → Failed, оригинал не потерян
        Fixture f;
        f.io.fail_write = f.min_speed_address();
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Failed);
        assert(f.ctl.stage() == 11);
    }
    {   // мусорное (нечитаемое) значение порога → стадия 9, записи нет
        Fixture f;
        f.io.put<float>(f.min_speed_address(), 1.0e30f);
        f.tick();
        assert(f.ctl.status == viewmodel::Status::Failed);
        assert(f.ctl.stage() == 9);
        assert(f.io.writes == 0u);
    }
}

void test_lost_chain_restores() {
    using namespace go_active;
    Fixture f;
    f.tick();
    assert(f.ctl.has_state());
    // KCC пропал (объект уничтожен): цепочка не подтверждается, правка
    // возвращается, статус — ожидание, а не «применено».
    f.io.put<uint64_t>(kKcc + KCC_PLAYER_BACKREF, 0);   // объект «утонул» в толпе
    f.tick();
    assert(f.ctl.status == viewmodel::Status::Waiting);
    assert(!f.ctl.has_state());
    assert(std::fabs(f.io.value<float>(f.min_speed_address()) - 8.5f) < 1e-6f);
}

void test_session_change_forgets_without_writes() {
    Fixture f;
    f.ctl.tick(1, 0x1000000, kPlayer, true);
    const unsigned writes = f.io.writes;
    assert(writes > 0u);
    // Смена сессии (переподключение): старые адреса забываются без записей —
    // процесс мог смениться, писать в них уже нельзя.
    f.ctl.tick(2, 0x2000000, kPlayer, true);
    assert(f.ctl.status == viewmodel::Status::Active);
    assert(f.io.writes >= writes);
    // Порог уже наш, поэтому повторной записи поля нет: значение осталось
    // прежним, а оригинал восстановлен не будет — ровно как у «без отдачи».
    assert(f.io.value<float>(f.min_speed_address()) == no_fall::kMinFallSpeed);
}

void test_profiles() {
    using go_active::GameVersion;
    go_active::Select(GameVersion::Release);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);
    assert(go_active::VITALS_PLAYER_BACKREF == 0x100);
    assert(go_active::KCC_MOVE == 0x13C);
    assert(go_active::KCC_MOVE_REAL_VELOCITY_Y == 0x1C);
    assert(go_active::KCC_MOVE_STATE_AIRBORNE == 4);
    assert(go_active::KCC_MOVE_STATE_CLAMP_MAX == 4);
    go_active::Select(GameVersion::Beta);
    assert(go_active::VITALS_MIN_FALL_SPEED == 0xC0);   // dump.cs 11968:454330
    assert(go_active::VITALS_PLAYER_BACKREF == 0x100);  // dump.cs 11968:455117
    assert(go_active::KCC_MOVE == 0x1AC);
    assert(go_active::KCC_MOVE_REAL_VELOCITY_Y == 0x1C);
    assert(go_active::KCC_MOVE_REAL_VELOCITY == 0x18);
    go_active::Select(GameVersion::Release);
    assert(go_active::KCC_MOVE == 0x13C);
}

// На бетовом профиле адрес Move другой — проверка, что контроллер берёт
// смещение из go_active, а не хардкодом.
void test_beta_move_offset() {
    go_active::Select(go_active::GameVersion::Beta);
    Fixture f(4, -30.f);
    f.tick();
    assert(f.ctl.status == viewmodel::Status::Active);
    assert(f.io.value<float>(f.velocity_y_address()) == 0.f);
    assert(f.ctl.debug().move == kKcc + 0x1AC);
    assert(f.io.value<float>(kKcc + 0x1AC + 0x1C) == 0.f);
    go_active::Select(go_active::GameVersion::Release);
    Fixture r(4, -30.f);
    r.tick();
    assert(r.ctl.debug().move == kKcc + 0x13C);
}

// Прицеп конфига: v7, бит 12, старые профили (v3..v6) обязаны остаться
// читаемыми и не получить «без падения» из мусора.
void test_config_extension() {
    ViewmodelSettings on;
    on.no_fall = true;
    on.no_spread = true;
    const auto encoded = viewmodel::encode(on);
    assert(encoded.version == 7 && encoded.size == sizeof(encoded));
    assert((encoded.flags & 4096u) != 0);
    assert((encoded.flags & 64u) != 0);
    auto decoded = viewmodel::decode(&encoded, sizeof(encoded));
    assert(decoded.no_fall && decoded.no_spread);

    // Профиль, сохранённый до появления фичи: бит 12 снят, остальное читается.
    auto v6 = encoded;
    v6.version = 6;
    v6.flags &= ~4096u;
    decoded = viewmodel::decode(&v6, sizeof(v6));
    assert(!decoded.no_fall && decoded.no_spread);

    // А вот если в v6-файле бит 12 всё-таки стоит (ручная правка, битая
    // выгрузка) — decode отбраковывает весь хвост целиком, а не выключает
    // одну фичу: так старый профиль не может «просочиться» наполовину.
    auto v6_dirty = encoded;
    v6_dirty.version = 6;
    decoded = viewmodel::decode(&v6_dirty, sizeof(v6_dirty));
    assert(!decoded.no_fall && !decoded.no_spread && !decoded.fov_enabled);

    // 32-байтовый хвост v1–v3 по-прежнему грузится.
    auto v3 = encoded;
    v3.version = 3;
    v3.size = 32;
    v3.flags = 16u;   // только быстрый подбор
    decoded = viewmodel::decode(&v3, 32);
    assert(decoded.fast_loot && !decoded.no_fall);
}

} // namespace

int main() {
    test_profiles();
    test_config_extension();
    test_patch_and_clamp();
    test_state_and_soft_landing_gates();
    test_respawn_switches_vitals_object();
    test_guards();
    test_lost_chain_restores();
    test_session_change_forgets_without_writes();
    test_beta_move_offset();
    puts("ALL NO-FALL TESTS PASS");
    return 0;
}
