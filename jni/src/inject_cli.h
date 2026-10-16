// inject_cli.h — CLI-обёртка `benzware inject ...` над inject.h.
//
// Запуск — как у dump/skel: в main() до stealth-маскировки (нужен вывод в
// терминал), без GUI и потоков, процесс завершается сам. Транспорт выбирается
// один раз аргументом (или external по умолчанию) до первого bind, без
// фоллбэков (KERNEL.md §2/§4.5).
//
//   su -c /data/local/tmp/benzware inject [external|ft|rt-dev|rt-hook] [pid]
//        [--symbol strlen] [--payload /path/payload.bin]
//        [--patch 0xADDR 0xVALUE] [--site rwx|text|auto]
//        [--no-wipe] [--wait MS]
//
// Выход: 0 — payload исполнился; 1 — игра/транспорт; 2 — кэв/GOT-слот;
// 3 — payload не исполнился за wait_ms; 4 — payload-файл; 5 — mmap стаба;
// 6 — транспорт не смог писать.

#pragma once

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "inject.h"
#include "config.h"          // CfgDir()
#include "game.h"            // esp_set_mem_backend, ESP_MEM_*
#include "game_internal.h"   // g_mem
#include "kernel_io.h"       // memio::*_available — явный pre-bind шаг
#include "process.h"         // process_find_game_pid, process_find_unity_candidates

namespace injectcli {

static int parse_backend(const char* s) {
    if (!s) return -1;
    if (!strcmp(s, "external") || !strcmp(s, "procfs")) return ESP_MEM_EXTERNAL;
    if (!strcmp(s, "ft"))       return ESP_MEM_FT;
    if (!strcmp(s, "rt-dev"))   return ESP_MEM_RT_DEV;
    if (!strcmp(s, "rt-hook"))  return ESP_MEM_RT_HOOK;
    return -1;
}

static bool read_proc_maps_text(pid_t pid, std::string* out) {
    return inject::read_maps(pid, out);
}

static bool load_payload_file(const char* path, std::vector<uint8_t>* out) {
    // fd открывается и сразу закрывается (KERNEL.md §5: долгоживущих fd нет).
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    out->clear();
    uint8_t buf[0x4000];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) out->insert(out->end(), buf, buf + n);
    close(fd);
    return n == 0 && out->size() >= inject::kPayloadHdr + 16;
}

static const char* status_text(inject::Status s) {
    switch (s) {
        case inject::kOk:          return "payload исполнен";
        case inject::kErrPayload:  return "payload-блоб плохой";
        case inject::kErrNoImage:  return "libil2cpp.so не найдена / ELF не читается";
        case inject::kErrNoSite:   return "не найден кэв или GOT-слот символа";
        case inject::kErrTimeout:  return "payload не исполнился за отведённое время";
        case inject::kErrMmap:     return "mmap в игре не удался";
        case inject::kErrWrite:    return "транспорт не смог писать";
    }
    return "?";
}

static int Run(int argc, char* argv[]) {
    // argv[0] — само слово «inject»; дальше режим, pid и опции.
    const char* mode_arg = nullptr;
    const char* payload_arg = nullptr;
    const char* symbol = "strlen";
    const char* site = "auto";
    pid_t want_pid = 0;
    uint64_t patch_addr = 0, patch_value = 0;
    bool have_patch = false;
    int wait_ms = 3000;
    bool wipe = true;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!a || !a[0]) continue;
        if (!strcmp(a, "--symbol"))       symbol = argv[++i];
        else if (!strcmp(a, "--payload")) payload_arg = argv[++i];
        else if (!strcmp(a, "--site"))    site = argv[++i];
        else if (!strcmp(a, "--wait"))    wait_ms = atoi(argv[++i]);
        else if (!strcmp(a, "--no-wipe")) wipe = false;
        else if (!strcmp(a, "--patch")) {
            patch_addr = strtoull(argv[++i], nullptr, 0);
            patch_value = (uint32_t)strtoul(argv[++i], nullptr, 0);
            have_patch = true;
        }
        else if (a[0] == '-') { printf("не понимаю опцию: %s\n", a); return 1; }
        else if (a[0] >= '0' && a[0] <= '9') want_pid = (pid_t)atoi(a);
        else if (!mode_arg) mode_arg = a;
        else { printf("не понимаю аргумент: %s\n", a); return 1; }
    }

    int backend = ESP_MEM_EXTERNAL;
    if (mode_arg) {
        backend = parse_backend(mode_arg);
        if (backend < 0) {
            printf("неизвестный режим: %s (external | ft | rt-dev | rt-hook)\n", mode_arg);
            return 1;
        }
    }
    inject::Site site_sel = inject::kSiteAuto;
    if (!strcmp(site, "rwx"))  site_sel = inject::kSiteRwx;
    if (!strcmp(site, "text")) site_sel = inject::kSiteText;

    printf("== benzware inject: скрытый one-shot инжект ==\n");
    printf("uid/euid : %d / %d%s\n", (int)getuid(), (int)geteuid(),
           geteuid() == 0 ? " (root)" : "  <-- НЕ ROOT");
    if (geteuid() != 0) printf("!! нужен root: su -c <путь>/benzware inject\n");
    printf("режим    : %s\n",
           backend == ESP_MEM_EXTERNAL ? "external" :
           backend == ESP_MEM_FT       ? "ft" :
           backend == ESP_MEM_RT_DEV   ? "rt-dev" : "rt-hook");

    // Единственное назначение транспорта за жизнь процесса (§2), до bind.
    esp_set_mem_backend(backend);
    if (backend == ESP_MEM_FT)           memio::driver_available(nullptr);
    else if (backend == ESP_MEM_RT_DEV)  memio::rt_dev_available();
    else if (backend == ESP_MEM_RT_HOOK) memio::rt_hook_available();

    // ---- payload --------------------------------------------------------------
    std::string payload_path = payload_arg ? std::string(payload_arg)
                                           : std::string(CfgDir()) + "/inject/payload.bin";
    std::vector<uint8_t> blob;
    if (!load_payload_file(payload_path.c_str(), &blob)) {
        printf("!! payload не читается: %s\n", payload_path.c_str());
        printf("   собери демку:  ./payload/build_payload.sh   (нужен NDK)\n");
        printf("   и положи в:    %s\n", payload_path.c_str());
        return 4;
    }
    printf("payload  : %s (%zu байт)\n", payload_path.c_str(), blob.size());

    // ---- процесс игры ----------------------------------------------------------
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
        printf("!! игра не найдена — запусти её и дождись загрузки libil2cpp\n");
        return 1;
    }

    // ---- привязка по libil2cpp (как у dump) ------------------------------------
    std::string maps_text;
    bool bound = false;
    pid_t used_pid = 0;
    uint64_t il2cpp_base = 0;
    for (const pid_t pid : pids) {
        std::string text;
        if (!read_proc_maps_text(pid, &text)) continue;
        uint64_t bases[8] = {};
        const int nb = maps::lookup_library_bases(text, "libil2cpp.so", bases, 8);
        for (int i = 0; i < nb && !bound; ++i) {
            if (g_mem.bind(pid, bases[i])) {
                bound = true;
                used_pid = pid;
                il2cpp_base = bases[i];
                maps_text.swap(text);
            }
        }
        if (bound) break;
    }
    if (!bound) {
        printf("!! транспорт не привязался ни к одному кандидату (errno %d)\n", g_mem.last_error());
        return 1;
    }
    printf("процесс  : pid %d, libil2cpp @ 0x%llx\n", (int)used_pid,
           (unsigned long long)il2cpp_base);

    // ---- инжект ------------------------------------------------------------------
    inject::Options o;
    o.got_symbol = symbol;
    o.payload = blob.data();
    o.payload_size = blob.size();
    o.il2cpp_base = il2cpp_base;
    o.maps_text.swap(maps_text);
    o.wait_ms = wait_ms;
    o.wipe = wipe;
    o.site = site_sel;
    if (have_patch) {
        o.patch_addr = patch_addr;
        o.patch_value = (uint32_t)patch_value;
    }
    printf("символ   : %s (GOT-слот)\n", symbol);
    printf("место    : %s\n", site_sel == inject::kSiteAuto  ? "auto (rwx -> текст)"
                              : site_sel == inject::kSiteRwx ? "rwx"
                                                             : "текст");

    const inject::Result r = inject::run(g_mem, o);
    if (r.status != inject::kOk) {
        printf("!! %s", status_text(r.status));
        if (r.status == inject::kErrMmap) printf(" (errno %d)", r.detail_errno);
        printf("\n");
        if (r.status == inject::kErrTimeout)
            printf("   слот уже возвращён, стаб затёрт — можно повторить (--wait 5000)\n");
        return (int)r.status;
    }
    printf("готово   : %s путь, запись @ 0x%llx, entry @ 0x%llx\n",
           r.rwx ? "rwx" : "текст", (unsigned long long)r.cave,
           (unsigned long long)r.payload_va);
    printf("got-слот : 0x%llx (оригинал 0x%llx, возвращён)\n",
           (unsigned long long)r.slot, (unsigned long long)r.orig_fn);
    if (have_patch && r.status == inject::kOk)
        printf("демо-патч: 0x%llx <- 0x%x выполнен в игре\n",
               (unsigned long long)patch_addr, (unsigned)patch_value);
    return 0;
}

}  // namespace injectcli
