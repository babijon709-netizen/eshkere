#pragma once
// CSV-трейс для переписывания предикта.
//
// Единственный способ поставить упреждение на числа, а не на подбор: в трейсе
// лежит и то, что игра показывает нам (поза удалённого игрока из памяти), и то,
// что она знает про него авторитетно (KCC.Move.RealVelocity), и тайм-минги
// Mirror, и камера. По такой записи задержка отрисовки считается
// кросс-корреляцией: интегрируем авторитетную скорость от метки времени отсчёта
// и ищем сдвиг, при котором наша поза совпадает — это и есть реальное окно
// задержки, которое больше не нужно угадывать.
//
// Файл: <CfgDir()>/aimtrace.csv, пишется по кадру на игрока, пока включён
// тумблер в меню (AIM → Автокалибровка → Запись трейса).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "config.h"     // CfgDir()

namespace aimtrace {

inline bool& enabled() {
    static bool value = false;
    return value;
}

struct Row {
    double  time = 0.0;        // mono seconds
    uint64_t id = 0;           // PlayerManager
    float   feet[3] = {};      // позиция, которую читаем из памяти (после нашей экстраполяции)
    float   raw_feet[3] = {};  // то же без нашей экстраполяции (сырой отсчёт снапшота)
    float   vel[3] = {};       // измеренная нами скорость (PlayerTrack)
    float   head[3] = {};      // точка головы по кости (если есть)
    bool    head_valid = false;
    float   dist = 0.0f;       // дистанция до цели, м
    float   age_ms = 0.0f;     // возраст отсчёта позиции
    float   buffer_ms = 0.0f;  // NetworkClient.bufferTime
    float   rtt_ms = 0.0f;     // NetworkTime.rtt
    int     tick = 0;          // NetworkServer.tickRate
    float   cam[3] = {};       // позиция камеры
    float   fwd[3] = {};       // направление взгляда
    float   local[3] = {};     // локальный игрок
    float   local_vel[3] = {}; // авторитетная скорость локального игрока
};

namespace detail {
inline double& last_shot() { static double v = -1e9; return v; }
inline double& last_hit() { static double v = -1e9; return v; }
inline FILE*& file() { static FILE* f = nullptr; return f; }
inline int& rows_since_flush() { static int v = 0; return v; }

inline void close() {
    if (file()) { fclose(file()); file() = nullptr; }
    rows_since_flush() = 0;
}
} // namespace detail

inline void reset() {
    detail::close();
    detail::last_shot() = detail::last_hit() = -1e9;
}

inline void set_enabled(bool on) {
    // Выключили — файл закрываем сразу: на устройстве трейс забирают копией,
    // и недописанный хвост в буфере выглядит как «трейс пустой».
    if (!on) reset();
    enabled() = on;
}

inline void shot(double now) { detail::last_shot() = now; }
inline void hit(double now) { detail::last_hit() = now; }

inline void row(const Row& r) {
    if (!enabled() || !std::isfinite(r.time)) return;
    if (!detail::file()) {
        const std::string path = std::string(CfgDir()) + "/aimtrace.csv";
        detail::file() = fopen(path.c_str(), "w");
        if (!detail::file()) { enabled() = false; return; }
        fprintf(detail::file(),
                "t,id,feet_x,feet_y,feet_z,raw_x,raw_y,raw_z,vel_x,vel_y,vel_z,"
                "head_x,head_y,head_z,head_ok,dist,age_ms,buffer_ms,rtt_ms,tick,"
                "cam_x,cam_y,cam_z,fwd_x,fwd_y,fwd_z,"
                "local_x,local_y,local_z,lvel_x,lvel_y,lvel_z,shot_age_ms,hit_age_ms\n");
    }
    fprintf(detail::file(),
            "%.4f,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%d,%.2f,%.1f,%.1f,%.1f,%d,"
            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
            "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f\n",
            r.time, (unsigned long long)r.id,
            r.feet[0], r.feet[1], r.feet[2],
            r.raw_feet[0], r.raw_feet[1], r.raw_feet[2],
            r.vel[0], r.vel[1], r.vel[2],
            r.head[0], r.head[1], r.head[2], r.head_valid ? 1 : 0,
            r.dist, r.age_ms, r.buffer_ms, r.rtt_ms, r.tick,
            r.cam[0], r.cam[1], r.cam[2], r.fwd[0], r.fwd[1], r.fwd[2],
            r.local[0], r.local[1], r.local[2],
            r.local_vel[0], r.local_vel[1], r.local_vel[2],
            (float)((r.time - detail::last_shot()) * 1000.0),
            (float)((r.time - detail::last_hit()) * 1000.0));
    if (++detail::rows_since_flush() >= 120) {
        detail::rows_since_flush() = 0;
        fflush(detail::file());
    }
}

} // namespace aimtrace
