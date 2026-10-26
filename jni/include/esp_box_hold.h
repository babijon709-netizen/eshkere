#pragma once
// Короткое окно удержания визуалов между кадрами.
//
// Здоровый кадр авторитетен: холд заменяется целиком. Битый кадр (transient)
// сливает читаемые записи по стабильному id и продлевает недостающие.
//
// Пропуск кадра — не повод мигать. Раньше здоровый кадр заменял список
// целиком, и одна неудавшаяся читка позиции игрока (или один кадр без матрицы
// вида) гасила его бокс ровно на кадр: на экране это «визуалы иногда мерцают на
// миллисекунды». Поэтому короткое окно пропуска действует и на здоровом кадре —
// запись перепроецируется текущей камерой, как и на битом, только окно короче
// (miss_seconds), чтобы труп/вышедший не висел тенью.
//
// Продлевать можно ТОЛЬКО мировой якорь. Экранные координаты, посчитанные
// прошлой матрицей вида, приклеены к экрану: как только камера поехала, бокс
// (и метка) разъезжаются с объектом — это ровно жалоба «визуалы плавают по
// экрану, а не держатся намертво». Поэтому битый кадр зовёт reproject() для
// каждой удержанной записи и пересчитывает экран текущей камерой; кадр, у
// которого матрицы вида нет вовсе, честно дорисовать запись не может — она
// убирается сразу, а не висит до конца окна.
#include <algorithm>
#include <cstddef>
#include <vector>
#include "game.h"

namespace espboxhold {

template <typename Value>
struct Entry {
    Value value{};
    double last_seen = 0.0;
};

template <typename Value, typename Predicate>
inline void erase_if(std::vector<Entry<Value>>& hold, Predicate removed) {
    hold.erase(std::remove_if(hold.begin(), hold.end(),
        [&](const Entry<Value>& entry) { return removed(entry.value); }), hold.end());
}

// reproject(value) -> bool: перепроецировать мировую геометрию записи текущим
// опубликованным кадром. false — кадр без матрицы вида (или запись за кромкой):
// запись не рисуется.
template <typename Value, typename Reproject>
inline void update(std::vector<Entry<Value>>& hold,
                   const std::vector<Value>& fresh,
                   bool transient,
                   double now,
                   double hold_seconds,
                   double miss_seconds,
                   Reproject&& reproject,
                   std::vector<Value>& out) {
    // Окно жизни записи: на битом кадре — полное, на здоровом — короткое
    // (столько, сколько нужно, чтобы пережить один-два пропущенных кадра).
    const double window = transient ? hold_seconds
                                    : (miss_seconds > 0.0 ? miss_seconds : hold_seconds);
    {
        for (const Value& value : fresh) {
            auto found = std::find_if(hold.begin(), hold.end(), [&](const Entry<Value>& entry) {
                return entry.value.id == value.id;
            });
            if (found != hold.end()) {
                found->value = value;
                found->last_seen = now;
            } else {
                Entry<Value> entry;
                entry.value = value;
                entry.last_seen = now;
                hold.push_back(entry);
            }
        }
        // last_seen == now бывает только у обновлённых этим кадром записей:
        // всё, что старше, либо вышло за окно, либо перепроецируется текущей
        // камерой. Кадр без матрицы вида перепроецировать не может — тогда
        // запись уходит сразу, а не висит приклеенной к экрану.
        for (std::size_t index = 0; index < hold.size();) {
            if (!(hold[index].last_seen < now)) { ++index; continue; }
            if (now - hold[index].last_seen > window ||
                !reproject(hold[index].value))
                hold.erase(hold.begin() + (std::ptrdiff_t)index);
            else
                ++index;
        }
    }

    out.clear();
    out.reserve(hold.size());
    for (const Entry<Value>& entry : hold) out.push_back(entry.value);
}

}  // namespace espboxhold
