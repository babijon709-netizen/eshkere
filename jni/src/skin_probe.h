#pragma once
// skin_probe.h — CLI-команды skins / skin: перечислить скины ствола в руках и
// записать выбранный индекс в SyncVar Oxide.WeaponPiece, напечатав что вышло.
//
//     su -c /data/local/tmp/benzware skins                    // режим из .mode
//     su -c /data/local/tmp/benzware skins external           // режим аргументом
//     su -c /data/local/tmp/benzware skins rt-dev 12345       // режим + pid
//     su -c /data/local/tmp/benzware skin 2                   // индекс 2, уровень 0
//     su -c /data/local/tmp/benzware skin 2 1                 // индекс 2, уровень 1
//     su -c /data/local/tmp/benzware skin 2 0 --rebuild       // + перезапись копий сразу
//
// Зачем отдельная команда. Вопрос «ПЕРЕСОБИРАЕТ ли игра FP-визуал после
// записи индекса» решён дизассемблером dump11966 и устройством (skin.log):
// НЕ пересобирает на месте — у PlayerFPWeaponView нет Update, хук SyncVar
// пустой (pFR @0x7675638 = ret), а сборка вьюхи (FPManager.Kxi/KNw ->
// CreateFPWeapon -> XZE/MjH) запускается только событиями игры. Устройство
// показало, какие из них работают: RPC насадок/модов (phW читает SyncVar
// +0x110 живьём) и спавн/подбор ствола (OnStartClient); простой холстер/дроу
// НЕ пересобирает — рабочая копия (+0xC0) в эти моменты держит «пустую» piece
// {Enabled=0, Number=-1}. Поэтому чит пишет ОБЕ копии, а проверяется так:
//
//   1) benzware skins         — список: индекс, тип, уровни, готов ли ассет;
//   2) benzware skin <N>      — записать индекс N в обе копии;
//   3) в игре сменить насадку/мод или дропнуть и поднять ствол (или респаун);
//   4) benzware skins         — строка «вьюха» скажет, собралась ли FP-вьюха
//                               из нашего индекса (fp_stale), строка «копия» —
//                               наш ли индекс в рабочей копии, строка
//                               «предмет» — наш ли индекс в Oxide.Item
//                               (триггер дроп/подбор), и жива ли ручка
//                               визуала скина.
//
// Картинка сменилась — путь полный. Не сменилась после смены мода — смотрите
// столбец «ассет»: докачка НЕ ручная — первая же сборка вьюхи сама ставит
// недостающий ассет в очередь игры (hbB.VkF -> Vkp; Tick -> VkX -> Vkl/Vkn;
// ретрай плейсхолдера каждые 250 мс), поэтому достаточно подождать и
// перечитать список. --rebuild
// здесь — не «пересобрать вьюху» (внешний процесс не может звать функции игры), а
// «перезаписать обе копии немедленно, даже если значения уже совпадают».
//
// Как и dump/skel, это отдельный короткоживущий процесс: без GUI и маскировки,
// транспорт — аргумент → .mode → трейлер конфига, изучение явным шагом до bind,
// без фоллбэков (KERNEL.md §2, §4.5). Записанное значение НАМЕРЕННО не
// откатывается на выходе: смысл команды в том, чтобы игра подхватила его уже
// после завершения процесса.
//
// Логи этих команд — skins_cli.log и skin_cli.log в каталоге конфигов. Суффикс
// _cli обязателен: рядом живёт skin.log, который ведёт сам чит (skin_log.h),
// а metadump::Log::open() открывает файл на "w" и стёр бы его в первый же вызов
// команды.
//
// Коды выхода: 0 — порядок, 1 — игра не найдена или транспорт не привязался,
// 2 — привязались, но контроллер не дошёл до списка/записи (стадия в выводе),
// 3 — esp_init не подтвердил игру, 4 — у предмета в руках нет скинов,
// 5 — запрошенного индекса нет в списке этого предмета.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "config.h"          // CfgDir()
#include "game.h"            // esp_init, esp_set_mem_backend, esp_mem_last_errno
#include "game_internal.h"   // g_mem
#include "maps_lookup.h"     // maps::lookup_library_bases
#include "metadata_dump.h"   // metadump::Log, read_proc_maps, saved_backend
#include "process.h"         // process_find_game_pid, process_find_unity_candidates
#include "skin_changer_memory.h"  // skinchanger::kMaxSkins
#include "viewmodel.h"       // esp_viewmodel_tick, esp_skin_entries, esp_skin_ui

namespace skinprobe {

// Сколько тиков сделать и с каким интервалом. Тик оружия в esp_viewmodel_tick
// ограничен 100 мс, а список скинов перечитывается раз в 5 тиков — поэтому
// шаг 150 мс и 8 повторов (~1.2 с) гарантированно накрывают и запись обеих
// копий, и перечитывание списка.
constexpr int kTickSleepMs = 150;
constexpr int kTicks       = 8;

struct Options {
    const char* mode_arg = nullptr;
    pid_t want_pid = 0;
    int index = -1;          // -1 = только список
    int level = 0;
    bool rebuild = false;
    bool have_index = false;
};

// Разбор аргументов: числа — индекс и уровень, --rebuild — флаг немедленной
// перезаписи обеих копий, остальной текст — режим транспорта. Ошибку печатаем сами.
inline bool ParseArgs(int argc, char* argv[], bool want_index, Options& out, const char* usage) {
    int numbers = 0;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!a || !a[0]) continue;
        if (std::strcmp(a, "--rebuild") == 0 || std::strcmp(a, "rebuild") == 0) {
            out.rebuild = true;
            continue;
        }
        if (a[0] >= '0' && a[0] <= '9') {
            const int value = std::atoi(a);
            // Уровень — только ВТОРОЕ число подряд и только пока не встретился
            // режим: в «skin 2 rt-dev 12345» число 12345 это pid, а не уровень.
            if (numbers == 0 && want_index) {
                out.index = value; out.have_index = true; ++numbers; continue;
            }
            if (numbers == 1 && want_index && !out.mode_arg) {
                out.level = value; ++numbers; continue;
            }
            if (out.want_pid == 0) { out.want_pid = (pid_t)value; continue; }
            std::printf("лишний числовой аргумент: %s\n%s\n", a, usage);
            return false;
        }
        if (!out.mode_arg) { out.mode_arg = a; continue; }
        std::printf("не понимаю аргумент: %s\n%s\n", a, usage);
        return false;
    }
    if (want_index && !out.have_index) {
        std::printf("нужен индекс скина (его печатает команда skins)\n%s\n", usage);
        return false;
    }
    return true;
}

// Привязка к игре — дословно тот же порядок, что у skel/dump: один транспорт
// на жизнь процесса, изучение до bind, без фолбэков.
// Возвращает код выхода: 0 = привязались и esp_init подтвердил игру.
inline int Attach(const Options& options, metadump::Log& log, const char* self_name) {
    int backend = -1;
    if (options.mode_arg) {
        backend = metadump::ParseBackendArg(options.mode_arg);
        if (backend < 0) {
            log.say("неизвестный режим: %s (external | ft | rt-dev | rt-hook)", options.mode_arg);
            return 1;
        }
    } else {
        backend = metadump::saved_backend();
    }
    log.say("режим    : %s%s", metadump::backend_name(backend),
            options.mode_arg ? " (аргументом)" : " (сохранённый)");
    esp_set_mem_backend(backend);
    metadump::PrepareOneShotBackend(backend);

    std::vector<pid_t> pids;
    if (options.want_pid > 0) {
        pids.push_back(options.want_pid);
    } else {
        const pid_t official = process_find_game_pid();
        if (official > 0) pids.push_back(official);
        else {
            pid_t cand[16] = {};
            const int n = process_find_unity_candidates(cand, 16);
            for (int i = 0; i < n; ++i) pids.push_back(cand[i]);
        }
    }
    if (pids.empty()) {
        log.say("!! игра не найдена — запусти её и зайди на сервер, "
                "в руках должен быть ствол");
        return 1;
    }

    bool bound = false;
    pid_t used_pid = 0;
    for (const pid_t pid : pids) {
        std::string text;
        if (!metadump::read_proc_maps(pid, &text)) continue;
        log.say("процесс  : pid %d (%s)", (int)pid, metadump::proc_cmdline(pid).c_str());
        std::uint64_t bases[8] = {};
        const int nb = maps::lookup_library_bases(text, "libil2cpp.so", bases, 8);
        for (int i = 0; i < nb; ++i)
            if (g_mem.bind(pid, bases[i])) { bound = true; break; }
        if (bound) { used_pid = pid; break; }
        log.say("   не привязалось (errno %d)", esp_mem_last_errno());
    }
    if (!bound) {
        log.say("!! транспорт не читает память игры: errno %d.", esp_mem_last_errno());
        log.say("   Проверь драйвер:  benzware --check-driver");
        log.say("   Или задай режим явно:  benzware %s external", self_name);
        return 1;
    }
    log.say("привязка : pid %d, %s", (int)used_pid, g_mem.backend_name());
    if (!esp_init(used_pid)) {
        log.say("!! esp_init не подтвердил игру: состояние %d. Профиль (Release/Beta) "
                "не совпадает со сборкой игры, или игра ещё грузится.",
                (int)esp_attach_state());
        return 3;
    }
    log.say("профиль  : %s", go_active::VersionName());
    return 0;
}

// Прогнать тик viewmodel (в нём тикается контроллер скинов) нужное число раз.
inline void Pump(const ViewmodelSettings& settings, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        esp_viewmodel_tick(settings);
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickSleepMs));
    }
}

inline const char* StatusName(viewmodel::Status status) {
    switch (status) {
        case viewmodel::Status::Off:     return "выкл";
        case viewmodel::Status::Waiting: return "ожидание";
        case viewmodel::Status::Active:  return "работает";
        case viewmodel::Status::Failed:  return "ОТКАЗ";
        case viewmodel::Status::ADS:     return "прицел";
        default:                         return "?";
    }
}

inline void PrintUi(metadump::Log& log, const viewmodel::SkinUiInfo& ui) {
    const viewmodel::Status status = ui.status;
    log.say("предмет  : %s", ui.weapon[0] ? ui.weapon : "(не прочитан)");
    log.say("скинов   : %d", ui.count);
    if (ui.piece_read)
        log.say("в памяти : Skin=%d SkinLevel=%d", ui.skin, ui.level);
    else
        log.say("в памяти : Skin/SkinLevel не прочитались (голова WeaponPiece)");
    log.say("вьюха    : %s", ui.fp_stale
            ? "FP-вьюха собрана из ДРУГОГО индекса (пересборки ещё не было)"
            : "FP-вьюха собрана из текущего индекса");
    log.say("копия    : рабочая копия piece %s",
            ui.local_state < 0 ? "не читается (голова не WeaponPiece?)"
            : ui.local_state == 0 ? "держит наш индекс"
                                  : "ОТЛИЧАЕТСЯ (игра перезаписала; тик восстановит)");
    log.say("предмет  : Oxide.Item.skin %s",
            ui.item_state < 0 ? "не читается (третья копия не пишется)"
            : ui.item_state == 0 ? "держит наш индекс (дроп/подбор перенесут его)"
                                 : "ОТЛИЧАЕТСЯ (игра перезаписала; тик восстановит)");
    if (ui.local_head[0])
        log.say("голова   : рабочая копия сырая: %s (неканоническая)", ui.local_head);
    log.say("визуал   : ручка скина %s", ui.handle_live ? "жива (ассет загружен)"
                                                         : "ПУСТА (ассет не загружен)");
    log.say("статус   : %s, стадия %d (%s)", StatusName(status), ui.stage,
            skin_change_stop_reason(status, ui.stage));
}

inline int PrintEntries(metadump::Log& log) {
    viewmodel::SkinEntryView entries[skinchanger::kMaxSkins];
    const int n = esp_skin_entries(entries, skinchanger::kMaxSkins);
    if (n <= 0) {
        log.say("список пуст: у предмета в руках нет массива skinData "
                "(или он не прочитался)");
        return 4;
    }
    log.say("индекс | тип          | ур | ассет  | id");
    for (int i = 0; i < n; ++i) {
        const viewmodel::SkinEntryView& e = entries[i];
        log.say("  %2d   | %-12s | %2d | %-6s | %s%s", e.index,
                viewmodel::skin_kind_name(e.kind), e.levels,
                e.ready ? "есть" : "НЕТ",
                e.id[0] ? e.id : "(id не прочитан)",
                viewmodel::skin_kind_is_object(e.kind) ? "" : "   <- перекраска, не модель");
    }
    return 0;
}

// benzware skins [режим] [pid]
inline int List(int argc, char* argv[]) {
    static const char* usage =
        "использование: benzware skins [external|ft|rt-dev|rt-hook] [pid]";
    Options options;
    if (!ParseArgs(argc, argv, false, options, usage)) return 1;

    mkdir(CfgDir(), 0777);
    metadump::Log log;
    log.open((std::string(CfgDir()) + "/skins_cli.log").c_str());
    log.say("== benzware skins: скины ствола в руках ==");
    log.say("uid/euid : %d / %d%s", (int)getuid(), (int)geteuid(),
            geteuid() == 0 ? " (root)" : "  <-- НЕ ROOT");
    const int attached = Attach(options, log, "skins");
    if (attached != 0) return attached;

    ViewmodelSettings settings;
    settings.skin_enabled = false;      // только чтение: ничего не пишем
    esp_skin_request_scan();            // список перечитывается по запросу
    Pump(settings, 4);

    const viewmodel::SkinUiInfo ui = esp_skin_ui();
    const int rc_entries = PrintEntries(log);
    PrintUi(log, ui);
    log.say("дальше   : benzware skin <индекс> — записать в обе копии, затем "
            "в игре убрать/достать ствол");
    log.say("== конец (код %d) ==, лог %s/skins_cli.log", rc_entries, CfgDir());
    return rc_entries;
}

// benzware skin <индекс> [уровень] [--rebuild] [режим] [pid]
inline int Set(int argc, char* argv[]) {
    static const char* usage =
        "использование: benzware skin <индекс> [уровень] [--rebuild] "
        "[external|ft|rt-dev|rt-hook] [pid]";
    Options options;
    if (!ParseArgs(argc, argv, true, options, usage)) return 1;

    mkdir(CfgDir(), 0777);
    metadump::Log log;
    log.open((std::string(CfgDir()) + "/skin_cli.log").c_str());
    log.say("== benzware skin: запись индекса %d (уровень %d%s) ==",
            options.index, options.level, options.rebuild ? ", с немедленной перезаписью" : "");
    log.say("uid/euid : %d / %d%s", (int)getuid(), (int)geteuid(),
            geteuid() == 0 ? " (root)" : "  <-- НЕ ROOT");
    if (geteuid() != 0) log.say("!! нужен root: su -c <путь>/benzware skin ...");
    const int attached = Attach(options, log, "skin");
    if (attached != 0) return attached;

    // Сначала список без записи: индекс обязан существовать в массиве ЭТОГО
    // предмета, иначе мы положили бы мусор в копии WeaponPiece.
    ViewmodelSettings probe;
    probe.skin_enabled = false;
    esp_skin_request_scan();
    Pump(probe, 4);
    {
        viewmodel::SkinEntryView entries[skinchanger::kMaxSkins];
        const int n = esp_skin_entries(entries, skinchanger::kMaxSkins);
        if (n <= 0) {
            log.say("!! у предмета в руках нет скинов — писать некуда");
            return 4;
        }
        bool known = false;
        int levels = 1;
        for (int i = 0; i < n; ++i)
            if (entries[i].index == options.index) {
                known = true;
                levels = entries[i].levels;
                log.say("цель     : [%d] %s, тип %s, уровней %d, ассет %s",
                        entries[i].index, entries[i].id[0] ? entries[i].id : "?",
                        viewmodel::skin_kind_name(entries[i].kind), levels,
                        entries[i].ready ? "запрашивался" : "НЕ запрашивался (может не быть на устройстве)");
            }
        if (!known) {
            log.say("!! индекса %d нет в списке этого предмета (всего %d)",
                    options.index, n);
            return 5;
        }
        if (options.level >= levels) {
            log.say("уровень %d вне диапазона этого скина (0..%d) — беру %d",
                    options.level, levels - 1, levels - 1);
            options.level = levels - 1;
        }
    }

    ViewmodelSettings settings;
    settings.skin_enabled = true;
    settings.skin_index = options.index;
    settings.skin_level = options.level;
    if (options.rebuild) esp_skin_request_rebuild();
    Pump(settings, kTicks);

    const viewmodel::SkinUiInfo ui = esp_skin_ui();
    PrintUi(log, ui);
    if (ui.skin != options.index) {
        log.say("!! в памяти лежит Skin=%d, а не %d — запись не прошла",
                ui.skin, options.index);
        return 2;
    }
    log.say("записано : WeaponPiece.Skin=%d SkinLevel=%d (обе копии)", ui.skin, ui.level);
    if (ui.fp_stale) {
        log.say("вьюха НЕ пересобрана: смени насадку/мод в игре или дропни и "
                "подними ствол (или респаун), затем проверь « benzware skins »");
    } else {
        log.say("вьюха пересобрана из нашего индекса — картинка должна была смениться");
    }
    if (!ui.handle_live)
        log.say("!! ручка визуала пуста: ассет ещё не скачан. Докачка автоматическая — "
                "первая сборка вьюхи из нашего индекса сама поставит ассет в очередь "
                "игры; подождите и перечитайте « benzware skins ».");
    // Намеренно БЕЗ esp_viewmodel_reset(true): записанное должно пережить
    // завершение этого процесса, иначе игра ничего не успеет подхватить.
    log.say("== конец (код 0) ==, лог %s/skin_cli.log", CfgDir());
    return 0;
}

}  // namespace skinprobe
