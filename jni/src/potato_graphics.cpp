#include "game.h"
#include "game_internal.h"
#include "process.h"
#include "potato_graphics_prefs.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kPrefsPaths[] = {
    "/data/user/0/com.catsbit.oxidesurvivalisland/shared_prefs/"
    "com.catsbit.oxidesurvivalisland.v2.playerprefs.xml",
    "/data/data/com.catsbit.oxidesurvivalisland/shared_prefs/"
    "com.catsbit.oxidesurvivalisland.v2.playerprefs.xml",
};
constexpr const char* kBackupPath = "/storage/emulated/0/benzware/.potato_graphics_backup";
constexpr size_t kMaxPrefsBytes = 2u * 1024u * 1024u;

bool read_text_file(const char* path, std::string& out) {
    out.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > kMaxPrefsBytes) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    file.read(&out[0], size);
    return file.good() || file.eof();
}

bool read_prefs(std::string& path, std::string& xml) {
    for (const char* candidate : kPrefsPaths) {
        if (read_text_file(candidate, xml)) {
            path = candidate;
            return true;
        }
    }
    return false;
}

bool write_all(int fd, const char* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const ssize_t n = write(fd, data + written, size - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

bool atomic_replace(const char* path, const std::string& content, bool preserve_owner) {
    struct stat original{};
    const bool have_original = stat(path, &original) == 0;
    if (preserve_owner && !have_original) return false;

    char temp[512];
    const int n = std::snprintf(temp, sizeof(temp), "%s.benzware-tmp", path);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(temp)) return false;

    const mode_t mode = have_original ? (original.st_mode & 0777) : 0600;
    const int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;

    bool ok = true;
    if (preserve_owner && fchown(fd, original.st_uid, original.st_gid) != 0) ok = false;
    if (ok && fchmod(fd, mode) != 0) ok = false;
    if (ok) ok = write_all(fd, content.data(), content.size());
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(temp, path) != 0) ok = false;
    if (!ok) unlink(temp);
    return ok;
}

bool backup_exists() {
    return access(kBackupPath, R_OK) == 0;
}

bool read_backup(std::vector<potato_graphics_prefs::SavedValue>& saved) {
    std::string content;
    return read_text_file(kBackupPath, content) &&
           potato_graphics_prefs::parse_backup(content, saved);
}

bool write_backup(const std::vector<potato_graphics_prefs::SavedValue>& saved) {
    const std::string content = potato_graphics_prefs::serialize_backup(saved);
    return atomic_replace(kBackupPath, content, false);
}

bool apply_prefs(bool enabled) {
    std::string path, xml;
    if (!read_prefs(path, xml)) return false;
    if (!enabled) {
        std::vector<potato_graphics_prefs::SavedValue> saved;
        if (!read_backup(saved)) return true; // no saved state to restore
        if (!potato_graphics_prefs::restore(xml, saved) ||
            !atomic_replace(path.c_str(), xml, true)) return false;
        unlink(kBackupPath);
        return true;
    }

    std::vector<potato_graphics_prefs::SavedValue> saved;
    if (backup_exists()) {
        if (!read_backup(saved)) return false;
    } else {
        if (!potato_graphics_prefs::snapshot(xml, saved) || !write_backup(saved)) return false;
    }
    if (!potato_graphics_prefs::apply_low(xml)) return false;
    if (!atomic_replace(path.c_str(), xml, true)) {
        if (!backup_exists()) unlink(kBackupPath);
        return false;
    }
    return true;
}

} // namespace

static bool g_potato_graphics_enabled = false;
static bool g_potato_prefs_ready = false;
static double g_potato_next_check = 0.0;

void esp_set_potato_graphics(bool enabled) {
    if (enabled == g_potato_graphics_enabled) return;
    g_potato_graphics_enabled = enabled;
    g_potato_prefs_ready = false;
    g_potato_next_check = 0.0;
}

void esp_tick_potato_graphics_prefs() {
    if (g_potato_prefs_ready && !g_potato_graphics_enabled && !backup_exists()) return;
    const double now = mono_seconds();
    if (now < g_potato_next_check) return;
    g_potato_next_check = now + 2.0;

    // Never race Unity's PlayerPrefs cache. The preset/restore is written only
    // after all game processes have exited, then Unity reads it on next launch.
    if (process_game_process_running()) {
        g_potato_prefs_ready = false;
        return;
    }
    if (g_potato_prefs_ready) return;
    if (!g_potato_graphics_enabled && !backup_exists()) {
        g_potato_prefs_ready = true;
        return;
    }
    g_potato_prefs_ready = apply_prefs(g_potato_graphics_enabled);
}
