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
    const char* build = "";     // штамп сборки (commit + дата), пустой = не знать
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
    // stale_/handle_/local_/item_ стартуют с «неизвестно», чтобы первый же тик
    // отчитался о состоянии вьюхи, ассета, рабочей копии piece и Oxide.Item.
    int  stale_ = -1, handle_ = -1, local_ = -2, item_ = -2, refill_ = -1;
    // §4.22: resets_ стартует с -1 — первое наблюдение числа сбросов
    // запоминается молча (лог открылся после события — не повод врать, что
    // «только что сбросили»). quarantine_ стартует с -2, чтобы первое же
    // «карантина нет» (-1) не напечатало «карантин снят».
    int  resets_ = -1, quarantine_ = -2;
    // §4.24: наблюдение за панцирём и беспанцирным застреванием — строка на
    // появление/смену деталей и каждые 10 с ожидания. Отравленный хвост
    // больше не молчит: даже если игрок закроет игру до сброса, в логе
    // останется форма отравления (класс панциря, ссылка, обработчик).
    int  shell_bucket_ = -1, stuck_bucket_ = -1, cache_bucket_ = -1;
    char shell_state_[64] = {};
    int  picked_index_ = -2, picked_ready_ = -2;
    char weapon_[48] = {};
    char head_[24] = {};   // последняя сообщённая голова рабочей копии (hex)

    void close() {
        if (f_) { std::fclose(f_); f_ = nullptr; }
        open_tried_ = false;
    }
    void clear_snapshot() {
        enabled_ = -1; skin_ = level_ = count_ = -2; known_ = -1; block_ = -1;
        stale_ = handle_ = -1; local_ = -2; item_ = -2; refill_ = -1;
        resets_ = -1; quarantine_ = -2;
        shell_bucket_ = -1; stuck_bucket_ = -1; cache_bucket_ = -1;
        shell_state_[0] = '\0';
        picked_index_ = picked_ready_ = -2;
        weapon_[0] = '\0';
        head_[0] = '\0';
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
        // Штамп сборки в шапке: по нему лог устройства сверяется с коммитом,
        // без гадания по формулировкам строк.
        if (s.build && s.build[0])
            say(s, "== skin.log: профиль %s, сессия %llu, сборка %s ==", s.profile,
                (unsigned long long)s.session, s.build);
        else
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
        // «Применено» = обе копии piece держат наш индекс (запись подтверждена
        // readback). Картинка при этом может оставаться старой до следующего
        // события игры (моды/насадки, спавн) — об этом строки про вьюху ниже.
        if (block_ != 0) { block_ = 0; say(s, "применено (обе копии piece)"); }
    } else if (block != block_) {
        block_ = block;
        say(s, "!! %s (стадия %d: %s)",
            viewmodel::skin_block_text(viewmodel::skin_block(s.ui.stage)), s.ui.stage,
            skin_change_stop_reason(s.ui.status, s.ui.stage));
    }

    // Голова рабочей копии piece сырыми байтами: строка на смену содержимого.
    // Заполняется только когда копия НЕКаноническая (щадящая проверка приняла
    // её по одному Enabled или отклонила на стадии 21) — это диагностика
    // «что реально лежит в копии», в нормальном состоянии строки нет.
    if (s.ui.local_head[0] && std::strcmp(s.ui.local_head, head_) != 0) {
        std::snprintf(head_, sizeof(head_), "%s", s.ui.local_head);
        say(s, "голова рабочей копии piece (сырая): %s", head_);
    } else if (!s.ui.local_head[0]) {
        head_[0] = '\0';
    }

    const int stale = s.ui.fp_stale ? 1 : 0;
    if (stale != stale_) {
        if (stale) {
            stale_ = stale;
            say(s, "вьюха СТАРАЯ: из другого индекса либо выпущена на холстере — убери и достань ствол (вьюха пересобирается на дроу)");
        } else if (s.ui.stage == 0) {
            stale_ = stale;
            // «Собрана» без живого визуала — не успех и не провал: сборка
            // состоялась (дроу отработал), ассет докачивается сам (ретрай игры
            // каждые 250 мс). Строки v13/v14 (12/8 штук с мёртвой ручкой) были
            // враньём баги fp_stale + погашенной головы; с §4.21 «собрана»
            // печатается только когда вьюха действительно держит наш индекс.
            if (s.ui.count > 0)
                say(s, s.ui.handle_live
                    ? "вьюха собрана из нашего индекса — картинка должна смениться"
                    : "вьюха собрана из нашего индекса, но визуал НЕ загружен — ассет докачивается сам (нужен интернет; ретрай каждые 250 мс — жди)");
        }
        // stage != 0 — записи не было: «собрана из нашего индекса» было бы
        // враньём (устройство это показало: строка красовалась рядом с вечной
        // стадией 21). Сентинель не запоминаем — строка допишется, когда
        // применение действительно состоится.
    }

    // Рабочая копия piece (+0xC0) — та, из которой игра подаёт piece в сборку
    // FP-вьюхи на дроу. «Отличается» означает, что игра перезаписала копию
    // (RPC/спавн): следующий тик вернёт наш индекс, строка нужна, чтобы в логе
    // было видно борьбу с сервером, а не молчаливое откатывание скина.
    if (s.ui.local_state >= 0 && s.ui.local_state != local_) {
        local_ = s.ui.local_state;
        say(s, local_ == 0 ? "рабочая копия piece: наш индекс"
                           : "рабочая копия piece: ОТЛИЧАЕТСЯ (игра перезаписала — восстанавливаем)");
    }

    // §4.21: голова рабочей копии была погашена игрой (Enabled=0) — зеркало
    // из SyncVar в действии. Строка на включение: с этого момента дроу
    // собирает вьюху вместо Release. Обратный переход молчаливый — это
    // штатный возврат оригинала при выключении.
    const int refill = s.ui.local_refill ? 1 : 0;
    if (refill != refill_) {
        refill_ = refill;
        if (refill)
            say(s, "голова рабочей копии была погашена игрой (Enabled=0) — достройка из SyncVar в действии: дроу соберёт вьюху, а не выпустит её");
    }

    // §4.22: игра уронила сборку вьюхи (исключение оставило полусобранный
    // FPObject), чит сбросил _currentWeapon. Строка на каждый сброс — счётчик
    // только растёт, и именно эти моменты устройство должно прислать в логе:
    // по ним видно, на каком скине/стволе ломается сборка. Сам сброс вьюху не
    // пересобирает — пересборка событие игры (холстер/дроу), о чём и говорим.
    // §4.24: наблюдение за панцирём — строка на появление (с деталями: класс,
    // обратная ссылка, PlayerEventHandler) и каждые 10 с ожидания. Детали
    // решают всё: погашенный обработчик у FPBow — блокада каждого дроу
    // (FPBow.FDN делает на нём NRE ещё до очистки _currentWeapon), поэтому
    // сброс прилетает через 2 с; живая ссылка на игрока при живом обработчике
    // — здоровая первая докачка ассета либо отравление §4.22, порог 45 с.
    if (s.ui.shell_watch > 0) {
        char state[64];
        std::snprintf(state, sizeof(state), "%s|%d|%d",
                      s.ui.shell_class[0] ? s.ui.shell_class : "?",
                      s.ui.shell_back, s.ui.shell_peh_null ? 1 : 0);
        const int bucket = s.ui.shell_watch / 10;
        if (std::strcmp(state, shell_state_) != 0) {
            std::snprintf(shell_state_, sizeof(shell_state_), "%s", state);
            shell_bucket_ = bucket;
            say(s, "!! сборка вьюхи встала: полусобранный %s висит в _currentWeapon "
                   "(ссылка игрока: %s, обработчик событий: %s, %d-я секунда) — "
                   "здоровая докачка ассета дойдёт до On_Draw сама, отравление чит сбросит",
                s.ui.shell_class[0] ? s.ui.shell_class : "FPObject",
                s.ui.shell_back == 1  ? "наша"
                : s.ui.shell_back == 0 ? "нулевая"
                : s.ui.shell_back == 2 ? "чужая" : "не прочиталась",
                s.ui.shell_peh_null ? "ПОГАШЕН" : "на месте",
                s.ui.shell_watch);
        } else if (bucket != shell_bucket_) {
            shell_bucket_ = bucket;
            say(s, "вьюха полусобрана уже ~%d с — игра не достраивает, чит наблюдает",
                s.ui.shell_watch);
        }
    } else if (shell_state_[0]) {
        shell_state_[0] = '\0';
        shell_bucket_ = -1;
    }

    // §4.24: беспанцирное застревание — piece «держится в руках» (Enabled=1),
    // FP-цепочка не резолвится (стадии 7–9), а панциря в _currentWeapon НЕТ:
    // игра уронила сборку раньше создания FPObject. Сбрасывать нечего —
    // строка каждые 10 с, чтобы форма попала в лог, а не в молчание дедупа.
    if (s.ui.stuck_seconds >= 10) {
        const int bucket = s.ui.stuck_seconds / 10;
        if (bucket != stuck_bucket_) {
            stuck_bucket_ = bucket;
            say(s, "!! предмет в руках, а вьюха не собирается уже ~%d с (стадия %d): "
                   "панциря в _currentWeapon нет — игра уронила сборку раньше создания "
                   "FPObject, сбрасывать нечего (лечит перезайти на сервер)",
                s.ui.stuck_seconds, s.ui.stage);
        }
    } else {
        stuck_bucket_ = -1;
    }

    // §4.24: зомби-резолв — _currentWeapon пуст, однако резолв ПРОХОДИТ через
    // типизированный кэш uzN (+0x58) живым объектом прежней сборки: стадия 0
    // («применено»), дедуп блока молчит, детектор панциря живёт в нерезолв-
    // ветке — без этой строки форма невидима в логе вовсе (устройство v16:
    // 1–2 минуты отравления — и НИ одной строки после «применено wooden.bow»).
    // Строка на 10-й секунде и дальше каждые 10 с. Записей в память игры здесь
    // нет: сбрасывать нечего (_currentWeapon и так пуст), а на устройстве
    // форму лечило только перезайти на сервер.
    if (s.ui.cache_seconds >= 10) {
        const int bucket = s.ui.cache_seconds / 10;
        if (bucket != cache_bucket_) {
            cache_bucket_ = bucket;
            say(s, "!! _currentWeapon пуст уже ~%d с, а резолв проходит через кэш игры (+0x58): "
                   "чит пишет «применено», но вьюха не пересобрана — эту форму лечит только перезайти на сервер",
                s.ui.cache_seconds);
        }
    } else {
        cache_bucket_ = -1;
    }

    if (resets_ < 0) {
        resets_ = s.ui.fp_build_resets;   // первое наблюдение — запоминаем молча
    } else if (s.ui.fp_build_resets > resets_) {
        resets_ = s.ui.fp_build_resets;
        say(s, "!! игра уронила сборку FP-вьюхи: чит сбросил _currentWeapon (сброс №%d) — убери и достань ствол, вьюха соберётся заново", resets_);
    }

    // §4.22: карантин скина, уронившего сборку. Строка и на установку, и на
    // снятие: снятие означает, что игрок выбрал другой индекс или другой
    // ствол, и сборка получит второй шанс.
    if (s.ui.quarantine_skin != quarantine_) {
        const int was = quarantine_;
        quarantine_ = s.ui.quarantine_skin;
        if (quarantine_ >= 0)
            say(s, "!! ствол %s: скин #%d уронил сборку вьюхи — индекс возвращён к родному, ствол на карантине: выбери другой индекс (тот же снова не применится)",
                s.ui.weapon[0] ? s.ui.weapon : "?", quarantine_);
        else if (was >= 0)
            say(s, "карантин скина #%d снят — сборка пробует снова", was);
    }

    // Oxide.Item.skin (+0x30) — третья точка записи: из неё ItemPickup.KZq
    // берёт скин при дропе, а Gah/Gaz несут его в синк инвентаря и сейв
    // (KERNEL.md §4.17–4.18). Пересборку вьюхи запускает НЕ она, а спавн.
    if (s.ui.item_state >= 0 && s.ui.item_state != item_) {
        item_ = s.ui.item_state;
        say(s, item_ == 0 ? "скин предмета (Oxide.Item): наш индекс — дроп и сохранение несут его"
                          : "скин предмета (Oxide.Item): ОТЛИЧАЕТСЯ (игра перезаписала — восстанавливаем)");
    }

    const int handle = s.ui.handle_live ? 1 : 0;
    if (handle != handle_) {
        handle_ = handle;
        if (handle) say(s, "визуал скина загружен");
        else if (s.ui.count > 0)
            say(s, "!! визуал скина НЕ загружен: игра докачает ассет сама при сборке вьюхи");
    }

    // Строка про выбранный скин: одна на смену выбора или готовности ассета.
    // «Ассет не запрашивался» — признак того, что сборки вьюхи из этого
    // индекса ещё не было: докачка НЕ ручная — первая же сборка сама ставит
    // недостающий ассет в очередь игры (hbB.VkF -> Vkp: обновление каталога;
    // Tick -> VkX -> Vkl/Vkn: приоритетная очередь hbw; ретрай плейсхолдера
    // каждые 250 мс — Vkh -> VkM). Измерить готовность можно только для
    // РАСПОЗНАННОГО типа (asset_ready читает поле конкретного класса): при
    // Unknown/Other готовности нет вовсе, и «НЕ запрашивался» был бы
    // обвинением без доказательства.
    const viewmodel::SkinEntryView* picked = nullptr;
    for (int i = 0; i < s.count; ++i)
        if (s.entries[i].index == s.want_index) { picked = &s.entries[i]; break; }
    const int ready = !picked ? -1
                    : !viewmodel::skin_kind_identified(picked->kind) ? 2
                    : picked->ready ? 1 : 0;
    if (s.want_index != picked_index_ || ready != picked_ready_) {
        picked_index_ = s.want_index; picked_ready_ = ready;
        if (picked) {
            // Перекраску подписываем прямо в строке выбора: устройство раз за
            // разом выбирает recolor и ждёт смены МОДЕЛИ.
            const bool recolor = picked->kind == viewmodel::SkinKind::WeaponRecolor ||
                                 picked->kind == viewmodel::SkinKind::EquipRecolor;
            say(s, "выбран #%d %s: %s, уровней %d, ассет %s%s", s.want_index,
                picked->id[0] ? picked->id : "(id не прочитан)",
                viewmodel::skin_kind_name(picked->kind), picked->levels,
                ready == 1 ? "запрашивался"
                : ready == 0 ? "НЕ запрашивался"
                             : "не проверить (тип не распознан)",
                recolor ? " — перекраска (материал, не модель)" : "");
            // Перекраске строка про закачку не пишется вовсе: у неё нет
            // модели, и «ассета нет» для материала — двойная ошибка. Объекту
            // строка нужна, но уже не приговором: докачка автоматическая
            // (блок «АССЕТЫ КАЧАЮТСЯ САМИ» в skin_changer_memory.h), не хватает
            // лишь одного события пересборки вьюхи.
            if (ready == 0 && !recolor)
                say(s, "!! ассет ещё не запрашивался: нужна одна сборка вьюхи — убери и достань ствол (сборка на дроу), закачает игра сама");
            else if (ready == 2)
                say(s, "!! тип скина не распознан: слоты TypeInfo не совпали (база libil2cpp не от живого образа?) и имя класса не прочлось — перезапустите игру и чит; запись индекса продолжает работать");
        }
    }
}

}  // namespace skinlog
