#pragma once
// skin_log.h — автолог скинченджера: <каталог конфигов>/skin.log.
//
// Зачем. Ответ на «применился ли скин» нужен в тот момент, когда игрок просто
// включил тумблер и играет, а не тогда, когда он вспомнил про терминал.
// Команды skins/skin — это отдельный процесс и ручной ввод на телефоне; здесь
// диагноз пишет сам чит, одной строкой на каждое ИЗМЕНЕНИЕ состояния. В
// установившемся режиме (индекс держится, вьюха собрана, ассет загружен) новых
// строк нет вовсе, поэтому файл не растёт и не мешает.
//
// Формат. Время — «+12.3с» от первого открытия файла, а не часы суток: процесс
// стартует с затёртым environ (KERNEL.md §5), локальный час ненадёжен, а вопрос
// один — сколько вьюха оставалась старой. Строки на русском, как весь вывод CLI.
// Те же формулировки показывает меню (SkinBlockUi в menu.cpp) и печатает
// skin_block_text(), поэтому лог и экран не расходятся.
//
// Ротация. Файл дописывается и начинается заново на kMaxBytes; после ротации
// состояние сообщается заново, потому что старый контекст из файла исчез.
//
// Тестируемость. Класс не знает ни CfgDir(), ни часов, ни контроллера — всё
// приходит в Snapshot. Поэтому строки, дедупликацию и ротацию можно проверить
// на хосте (jni/tests/test_skin_log.cpp), а не только «собралось на устройстве».
//
// След. Обычный файл в том же каталоге, что конфиги, .layout и .accent
// (DETECTION.md, тир 0). Появляется только при включённом скинченджере: пока
// тумблер выключен, файл не создаётся и не открывается.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "viewmodel.h"

namespace skinlog {

// Предел файла. 64 КБ — это тысячи строк, то есть десятки часов переключений;
// больше не нужно, а неограниченный рост в /data/local недопустим.
constexpr long kMaxBytes = 64 * 1024;

// Всё, что нужно для одной проверки «изменилось ли что-то». Заполняется в
// game_viewmodel.cpp внутри тика оружия — под тем же мьютексом, что и
// контроллер, и с уже готовым списком скинов.
struct Snapshot {
    uint64_t session = 0;       // эпоха процесса игры
    bool enabled = false;       // тумблер скинченджера
    bool bound = false;         // транспорт привязан к живому процессу
    viewmodel::SkinUiInfo ui;   // снимок контроллера (стадия, вьюха, ассет)
    const viewmodel::SkinEntryView* entries = nullptr;
    int count = 0;              // сколько элементов в entries
    int want_index = 0;         // индекс из настроек (уже sanitize-нутый)
    const char* profile = "?";  // go_active::VersionName()
    const char* dir = "";       // CfgDir()
    double now = 0;             // memio::now_seconds()
};

class Writer {
public:
    // Забыть всё: закрытый файл, сброшенный снимок, новая сессия. Зовётся из
    // esp_viewmodel_reset и при смене epoch.
    void reset() {
        close();
        session_ = 0;
        truncate_ = false;
        clear_snapshot();
    }
    void note(const Snapshot& s);
    ~Writer() { close(); }

    // Для тестов: сколько строк записано за жизнь объекта (не файла).
    int lines_written() const { return lines_; }

private:
    FILE* f_ = nullptr;
    bool  open_tried_ = false;  // каталог может быть недоступен — не долбим каждый тик
    bool  truncate_ = false;    // прошлый файл переполнен — начать с чистого листа
    double t0_ = 0;
    uint64_t session_ = 0;
    int lines_ = 0;
    // Снимок того, о чём уже сообщали: строка пишется только по расхождению.
    // Начальные значения заведомо «невозможные», чтобы первое же сравнение
    // дало строку, а не тишину.
    int  enabled_ = -1, skin_ = -2, level_ = -2, count_ = -2, known_ = -1;
    int  block_ = -1;   // последняя сообщённая ПРИЧИНА (SkinBlock), не стадия
    // stale_/handle_ стартуют с «неизвестно», чтобы первый же тик отчитался о
    // состоянии вьюхи и ассета. rebuild_ стартует с нуля: «ствол возвращён»
    // имеет смысл только как КОНЕЦ цикла пересборки, а не как новость о том,
    // что пересборки не было.
    int  stale_ = -1, handle_ = -1, rebuild_ = 0;
    int  picked_index_ = -2, picked_ready_ = -2;
    char weapon_[48] = {};

    void close() {
        if (f_) { std::fclose(f_); f_ = nullptr; }
        open_tried_ = false;
    }
    void clear_snapshot() {
        enabled_ = -1; skin_ = level_ = count_ = -2; known_ = -1; block_ = -1;
        stale_ = handle_ = -1; rebuild_ = 0;
        picked_index_ = picked_ready_ = -2;
        weapon_[0] = '\0';
    }
    bool ensure_open(const Snapshot& s) {
        if (f_) {
            const long size = std::ftell(f_);
            if (size >= 0 && size < kMaxBytes) return true;
            std::fclose(f_); f_ = nullptr;
            truncate_ = true;
            open_tried_ = false;  // переоткрываем СРАЗУ: иначе open_tried_
                                  // остался бы true и лог закрылся навсегда
            clear_snapshot();     // файл новый — сообщаем всё заново
        }
        if (open_tried_) return false;
        open_tried_ = true;
        if (!s.dir || !s.dir[0]) return false;
        char path[256];
        std::snprintf(path, sizeof(path), "%s/skin.log", s.dir);
        f_ = std::fopen(path, truncate_ ? "w" : "a");
        if (!f_) return false;
        t0_ = s.now;
        return true;
    }
    void say(const Snapshot& s, const char* fmt, ...) {
        if (!ensure_open(s)) return;
        ++lines_;
        std::fprintf(f_, "+%.1fs ", s.now - t0_);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(f_, fmt, ap);
        va_end(ap);
        std::fputc('\n', f_);
        std::fflush(f_);   // строка должна пережить kill процесса: читают её
                           // обычно сразу после того, как картинка не сменилась
    }
};

inline void Writer::note(const Snapshot& s) {
    if (s.session != session_) {
        // Новый процесс игры: прошлые строки описывали другую жизнь.
        reset();
        session_ = s.session;
    }
    if (!s.bound || !s.enabled) {
        if (enabled_ == 1)
            say(s, s.bound ? "скинченджер выключен — оригинал возвращён"
                           : "связь с игрой потеряна — записанное осталось в памяти игры");
        enabled_ = 0;
        return;
    }
    if (enabled_ != 1) {
        enabled_ = 1;
        if (!ensure_open(s)) return;
        say(s, "== skin.log: профиль %s, сессия %llu ==", s.profile,
            (unsigned long long)s.session);
    }
    if (!ensure_open(s)) return;

    // Предмет в руках: одна строка на смену ствола (или на первое чтение).
    if (s.ui.weapon[0] && std::strcmp(s.ui.weapon, weapon_) != 0) {
        std::snprintf(weapon_, sizeof(weapon_), "%s", s.ui.weapon);
        say(s, "ствол %s: скинов %d", weapon_, s.ui.count);
    } else if (s.ui.count != count_) {
        say(s, "скинов у предмета: %d", s.ui.count);
    }
    count_ = s.ui.count;

    // Что реально лежит в SyncVar — в том числе когда запись не удалась: это
    // главная строка диагноза. Отрицательный Skin у ствола без скина показывается
    // как есть, а «не прочиталось» отличается от значения отдельной строкой.
    const int known = s.ui.piece_read ? 1 : 0;
    if (known != known_ || (known && (s.ui.skin != skin_ || s.ui.level != level_))) {
        known_ = known; skin_ = s.ui.skin; level_ = s.ui.level;
        if (known) say(s, "в памяти игры Skin=%d SkinLevel=%d", s.ui.skin, s.ui.level);
        else say(s, "в памяти игры Skin/SkinLevel не прочитались");
    }

    // Причина остановки — главное, ради чего лог существует. Дедупликация по
    // ПРИЧИНЕ, а не по стадии: стадии внутри одной причины («нет игрока» 1 и 2,
    // «пусто в руках» 7 и 9) на загрузке/смене ствола чередуются каждый тик, и
    // дедуп по стадии давал сотни одинаковых строк подряд. Стадию пишем внутрь
    // строки: она нужна разработчику, а не игроку.
    const int block = (int)viewmodel::skin_block(s.ui.stage);
    if (s.ui.stage == 0) {
        // «Применено» пишем и когда статус Waiting: при включённой
        // автопересборке индекс уже подтверждён чтением, а статус станет
        // Active только после холстер/дроу — и перехода стадии при этом нет,
        // то есть со старым условием строку успеха не было видно НИКОГДА.
        if (block_ != 0) { block_ = 0; say(s, "применено"); }
    } else if (block != block_) {
        block_ = block;
        say(s, "!! %s (стадия %d: %s)",
            viewmodel::skin_block_text(viewmodel::skin_block(s.ui.stage)), s.ui.stage,
            skin_change_stop_reason(s.ui.status, s.ui.stage));
    }

    const int stale = s.ui.fp_stale ? 1 : 0;
    if (stale != stale_) {
        stale_ = stale;
        if (stale) say(s, "вьюха СТАРАЯ: собрана из другого индекса — нужен холстер/дроу");
        else if (s.ui.count > 0) say(s, "вьюха собрана из нашего индекса — картинка должна смениться");
    }

    const int handle = s.ui.handle_live ? 1 : 0;
    if (handle != handle_) {
        handle_ = handle;
        if (handle) say(s, "визуал скина загружен");
        else if (s.ui.count > 0)
            say(s, "!! визуал скина НЕ загружен: ассета может не быть на устройстве");
    }

    const int rebuild = s.ui.rebuild_busy ? 1 : 0;
    if (rebuild != rebuild_) {
        rebuild_ = rebuild;
        say(s, rebuild ? "пересборка: ствол убран" : "пересборка: ствол возвращён");
    }

    // Строка про выбранный скин: одна на смену выбора или готовности ассета.
    // «Ассет не запрашивался» — главный признак того, что скина на устройстве
    // просто нет: скины докачиваются по требованию (hbG/SkinDownloadSummary),
    // и внешний процесс Addressables поднять не может.
    const viewmodel::SkinEntryView* picked = nullptr;
    for (int i = 0; i < s.count; ++i)
        if (s.entries[i].index == s.want_index) { picked = &s.entries[i]; break; }
    const int ready = picked ? (picked->ready ? 1 : 0) : -1;
    if (s.want_index != picked_index_ || ready != picked_ready_) {
        picked_index_ = s.want_index; picked_ready_ = ready;
        if (picked) {
            say(s, "выбран #%d %s: %s, уровней %d, ассет %s", s.want_index,
                picked->id[0] ? picked->id : "(id не прочитан)",
                viewmodel::skin_kind_name(picked->kind), picked->levels,
                ready == 1 ? "запрашивался" : "НЕ запрашивался");
            if (ready == 0)
                say(s, "!! чего нет на устройстве, внешним процессом не загрузить");
        }
    }
}

}  // namespace skinlog
