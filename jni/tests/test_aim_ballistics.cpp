#include "aim_ballistics.h"

#include <cassert>
#include <cmath>

static bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

int main() {
    using namespace aim_ballistics;
    const Vec3 origin{0.0f, 1.5f, 0.0f};
    Parameters params{100.0f, 0.0f, 0.0f, 0.0f, 0.0f, true};
    Solution solution{};

    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.valid);
    assert(near(solution.flight_time, 1.0f, 0.01f));
    assert(solution.direction.z > 0.999f);
    assert(std::fabs(solution.direction.x) < 0.001f);

    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {5.0f, 0.0f, 0.0f}, {},
                 params, solution));
    assert(solution.direction.x > 0.04f && solution.direction.x < 0.06f);
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {-5.0f, 0.0f, 0.0f}, {},
                 params, solution));
    assert(solution.direction.x < -0.04f && solution.direction.x > -0.06f);

    params.gravity = 1.0f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.direction.y > 0.04f);
    assert(solution.flight_time > 1.0f);

    // Поле gravity читается из двух мест с разной семантикой (снимки дампов
    // в test_no_ballistics_memory.cpp): профиль — множитель (0.65), префаб
    // ShaftedProjectile — абсолютное м/с² (9.81). Интегратор ждёт множитель,
    // поэтому м/с² обязано нормализоваться, иначе дуга ×10 и выстрел
    // перелетает голову. Проверяем значения и тождество решения.
    assert(near(gravity_multiplier(1.0f), 1.0f, 1e-5f));
    assert(near(gravity_multiplier(0.0f), 0.0f, 1e-6f));
    assert(near(gravity_multiplier(0.65f), 0.65f, 1e-5f));
    assert(near(gravity_multiplier(-0.65f), 0.65f, 1e-5f));
    assert(near(gravity_multiplier(9.81f), 1.0f, 1e-3f));
    assert(near(gravity_multiplier(-9.81f), 1.0f, 1e-3f));
    assert(near(gravity_multiplier(19.62f), 2.0f, 1e-3f));

    {
        Parameters raw{100.0f, 1.0f, 0.0f, 0.0f, 0.0f, true};
        Parameters norm = raw;
        norm.gravity = gravity_multiplier(9.81f);
        Solution from_raw{}, from_norm{};
        assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, raw, from_raw));
        assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, norm, from_norm));
        assert(near(from_raw.direction.y, from_norm.direction.y, 1e-3f));
        assert(near(from_raw.flight_time, from_norm.flight_time, 0.001f));
    }

    params.gravity = 0.0f;
    params.air_drag = 0.1f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    assert(solution.flight_time > 1.0f);

    params.air_drag = 0.0f;
    assert(solve(origin, {0.0f, 1.5f, 100.0f}, {}, {3.0f, 0.0f, 0.0f},
                 params, solution));
    assert(solution.direction.x < 0.0f);

    params.gravity = 1.0f;
    params.air_drag = 0.0f;
    params.underwater_drag = 0.0f;
    assert(solve({0.0f, -2.0f, 0.0f}, {0.0f, -2.0f, 100.0f}, {}, {},
                 params, solution));
    assert(std::fabs(solution.direction.y) < 0.001f);

    // A shot that enters water should stop accumulating air gravity after the
    // surface crossing; the recovered launch direction must remain shallow.
    assert(solve({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 100.0f}, {}, {},
                 params, solution));
    assert(solution.direction.y > 0.0f && solution.direction.y < 0.05f);

    params.max_range = 50.0f;
    assert(!solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    params.max_range = 0.0f;
    assert(!solve(origin, {0.0f, 1.5f, 100.0f}, {0.0f, 0.0f, 150.0f}, {},
                  params, solution));

    params.muzzle_speed = 0.0f;
    assert(!solve(origin, {0.0f, 1.5f, 100.0f}, {}, {}, params, solution));
    return 0;
}
