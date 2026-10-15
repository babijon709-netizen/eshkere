#pragma once
// world_dump.h — CLI-команда world: дамп сетевых объектов мира и их компонентов
// с живой игры, чтобы по нему снять раскладку нативных коллайдеров.
//
//     su -c /data/local/tmp/xvcen world                  // режим из .mode
//     su -c /data/local/tmp/xvcen dump world             // то же самое
//     su -c /data/local/tmp/xvcen world ft               // режим аргументом
//     su -c /data/local/tmp/xvcen world rt-dev 12345     // режим + pid
//
// Писать в: <CfgDir>/world.log (отчёт: итоги, типы коллайдеров, hex-срезы ссылок),
// <CfgDir>/world.jsonl (все GameObject-ы: имя, поза, компоненты) и
// <CfgDir>/world.colliders.jsonl (коллайдеры: тип, поза с масштабом, сырые байты).
//
// Как skel и dump — отдельный короткоживущий процесс без GUI и маскировки;
// транспорт назначается один раз до bind (KERNEL.md §2), /proc/<pid>/mem в
// кернел-режимах не открывается. Зайди на сервер и дождись прогрузки карты
// до запуска: дамп видит только то, что уже заспавнилось у клиента.
//
// Коды выхода: 0 — снято, 1 — игра не найдена / транспорт не привязался /
// плохие аргументы, 3 — esp_init отказал (офсеты не под эту сборку),
// 4 — NetworkClient.spawned недоступен.

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"          // CfgDir()
#include "game.h"            // esp_init, esp_world_dump, ESP_MEM_*
#include "game_internal.h"   // g_mem
#include "maps_lookup.h"     // maps::lookup_library_bases
#include "metadata_dump.h"   // metadump::Log, read_proc_maps, saved_backend
#include "process.h"         // process_find_game_pid, process_find_unity_candidates

namespace worlddump {

static int Run(int argc, char* argv[]) {
    // argv[0] — само слово «world»; дальше опционально режим и/или pid.
    const char* mode_arg = nullptr;
    pid_t want_pid = 0;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!a || !a[0]) continue;
        if (a[0] >= '0' && a[0] <= '9') {
            want_pid = (pid_t)atoi(a);
        } else if (!mode_arg) {
            mode_arg = a;
        } else {
            printf("не понимаю аргумент: %s\n", a);
            printf("использование: xvcen world [external|ft|rt-dev|rt-hook] [pid]\n");
            return 1;
        }
    }
    int backend = -1;
    if (mode_arg) {
        backend = metadump::ParseBackendArg(mode_arg);
        if (backend < 0) {
            printf("неизвестный режим: %s (external | ft | rt-dev | rt-hook)\n", mode_arg);
            return 1;
        }
    } else {
        backend = metadump::saved_backend();
    }

    const std::string dir = std::string(CfgDir());
    mkdir(CfgDir(), 0777);
    const std::string report_path = dir + "/world.log";
    const std::string jsonl_path = dir + "/world.jsonl";
    const std::string colliders_path = dir + "/world.colliders.jsonl";

    metadump::Log log;
    log.open(report_path.c_str());
    log.say("== xvcen world: дамп мира ==");
    log.say("uid/euid : %d / %d%s", (int)getuid(), (int)geteuid(),
            geteuid() == 0 ? " (root)" : "  <-- НЕ ROOT");
    if (geteuid() != 0) log.say("!! нужен root: su -c <путь>/xvcen world");
    log.say("режим    : %s%s", metadump::backend_name(backend),
            mode_arg ? " (аргументом)" : " (сохранённый)");

    // Транспорт — один раз, до bind, без фоллбэков (KERNEL.md §2, как dump/skel).
    esp_set_mem_backend(backend);
    metadump::PrepareOneShotBackend(backend);

    std::vector<pid_t> pids;
    if (want_pid > 0) {
        pids.push_back(want_pid);
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
        log.say("!! игра не найдена — запусти её и зайди на сервер");
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
        log.say("   Проверь драйвер:  xvcen --check-driver");
        log.say("   Или задай режим явно:  xvcen world external");
        return 1;
    }
    log.say("привязка : pid %d, %s", (int)used_pid, g_mem.backend_name());

    if (!esp_init(used_pid)) {
        log.say("!! esp_init не подтвердил игру: состояние %d. "
                "Сборка не совпадает с офсетами, или игра ещё грузится.",
                (int)esp_attach_state());
        return 3;
    }

    FILE* jsonl = fopen(jsonl_path.c_str(), "w");
    FILE* colliders = fopen(colliders_path.c_str(), "w");
    if (!jsonl || !colliders)
        log.say("!! не открылись файлы дампа в %s (пишется только то, что открылось)", dir.c_str());
    // Отчёт пишется в тот же файл, что и лог (Log держит его открытым): hex-срезы
    // в терминал не льём, в stdout идут только итоговые строки.
    const int rc = esp_world_dump(log.f, jsonl, colliders, /*max_identities*/ 20000, /*max_nodes*/ 24);
    if (jsonl) fclose(jsonl);
    if (colliders) fclose(colliders);
    if (rc != 0) {
        log.say("!! дамп не снят: NetworkClient.spawned недоступен. Ты на сервере и карта прогружена?");
        return 4;
    }
    log.say("== готово ==");
    log.say("отчёт     : %s  (сверху итоги: раскладка/имена/число коллайдеров)", report_path.c_str());
    log.say("объекты   : %s", jsonl_path.c_str());
    log.say("коллайдеры: %s", colliders_path.c_str());
    log.say("отправь все три файла разработчику");
    return 0;
}

}  // namespace worlddump
