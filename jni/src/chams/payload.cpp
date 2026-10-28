// libimghelper.so — ин-процессный пейлоад чамсов: inline-хук в libil2cpp.so
// на методе рендера URP и подмена материалов ТОЛЬКО противников (не союзников
// и не локального игрока) на always-on-top шейдер. Грузится внешним
// инжектором (jni/src/chams_inject.h, CLI `chams-inject`), а сам НИЧЕГО не
// делает, пока файл-контроллера kCtlPath не скажет «1»: тумблер в меню —
// единственный источник включения, на старте чит ничего не трогает.
//
// Дизайн против детектора (libloader.so в поставке игры):
//   * патч — ОДНО 4-байтовое слово `B` на первом инструкции пролога (не
//     классический 16-байтовый LDR/BR-трамплин с его сигнатурами);
//   * стаб и литерал — на анонимной r-x странице в пределах ±128 МБ от цели
//     (mmap с хинтом), никаких правок PLT/GOT/экспортов чужих библиотек;
//   * ptrace — только у внешнего разового CLI-инжектора; в самом пейлоаде нет
//     ptrace, /proc/<pid>/mem и вообще записей куда-либо, кроме этого 4-байтового
//     патча и собственной анонимной страницы;
//   * все чтения in-process идут через карту readable-регионов /proc/self/maps
//     (проверка на каждом разыменовании): кривой RVA или чужая сборка игры =
//     пейлоад молча выключается (гейт по имени класса PlayerManager), а не
//     падает вместе с игрой.
//
// Регенерация после нового дампа (см. также game_offsets.h):
//   dump.cs:  grep -a "UniversalRenderPipeline" | RVA двух Render(...),
//             Shader.Find, Renderer.get_materials, Material.get_shader/
//             set_shader/set_color/SetInt, Application.get_dataPath;
//   типовые смещения: Il2CppClass.static_fields=0xB8, .name=0x10,
//             List.items=0x10/.size=0x18, Array.first_elem=0x20,
//             String.length=0x10/.chars=0x14 (IL2CPP_* в game_offsets.h);
//   PlayerModelInfo: skinnedMeshRenderer=0x50, headRenderer=0x58 из dump.cs,
//             обёртка PlayerManager+PLAYER_MODEL_INFO_REF(+0x10)=PlayerModelInfo;
//   прологи: capstone-дизасс первых 16 байт по файловому офсету (RVA − 0x4000
//             для .text этого билда) — оба пролога без PC-rel, первое слово
//             переносится в стаб как есть;
//   шейдерные имена stringliteral.json не содержит (built-in ищется всегда).
//
// Не проверено на устройстве (NDK в песочнице нет; собрано host-проверками и
// CI): фактический always-on-top шейдера в URP (см. kShaderName), тайминги
// инжекта на живой игре, реакция libloader.so. Статус-байт в kDiagPath:
//   '0' старт, '1' нет libil2cpp, '2' гейт класса не прошёл (чужая сборка),
//   '3' хук стоит/включён, '4' выключено, ждём кадр для восстановления,
//   '5' шейдер не найден, '6' кадр отработал (применено/восстановлено).

#include <atomic>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "game_offsets.h"

// ============================== конфиг ====================================
namespace chams_cfg {

// Пакет и пути инжектора/пейлоада — ДОЛЖНЫ совпадать с chams_inject.h.
inline constexpr const char* kPkgName  = "com.catsbit.oxidesurvivalisland";
inline constexpr const char* kCtlPath   = "/data/data/com.catsbit.oxidesurvivalisland/files/.imgh_state";
inline constexpr const char* kDiagPath  = "/data/data/com.catsbit.oxidesurvivalisland/files/.imgh_diag";

// Цвет чамсов противника (компоненты _Color, красный).
inline constexpr float kColorR = 0.95F, kColorG = 0.07F, kColorB = 0.07F, kColorA = 1.0F;

// Шейдер: единственный built-in unlit с процедурой ZTest-свойств; в URP его
// pass без LightMode рисуется как SRPDefaultUnlit. FILL: если на устройстве
// модель не вылезает сквозь стены — крутить тут: "_ZTest"=8 (Always) и
// "_ZWrite"=0 ставим при подмене, но если шейдер их не экспонирует, нужен
// другой шейдер (вариант: готовые байты своего — // FILL).
inline constexpr const char* kShaderName = "Hidden/Internal-Colored";

// ---- RVA рантайм-методов, release 11978 (dump11978; libil2cpp.so md5-тот же,
// что в шипнутом APK): Il2CppDumper script.json / dump.cs.
inline constexpr uint64_t kRenderListRva   = 0xD2DE1F0; // UniversalRenderPipeline.Render(ctx, List<Camera>) Slot:7 — живой путь SRP
inline constexpr uint64_t kRenderArrRva    = 0xD2DE158; // UniversalRenderPipeline.Render(ctx, Camera[])   Slot:4
inline constexpr uint64_t kShaderFindRva   = 0xD460384; // static Shader.Find(string)
inline constexpr uint64_t kGetMaterialsRva = 0xD45EC24; // Renderer.get_materials() (thunk -> instantiate)
inline constexpr uint64_t kGetShaderRva    = 0xD467F30; // Material.shader { get; }
inline constexpr uint64_t kSetShaderRva    = 0xD468024; // Material.shader { set; }
inline constexpr uint64_t kSetColorRva     = 0xD468264; // Material.color { set; } (Color = HFA: 4 float)
inline constexpr uint64_t kSetIntRva       = 0xD46F808; // Material.SetInt(string, int)
inline constexpr uint64_t kDataPathRva     = 0xD42F650; // Application.get_dataPath() — шаблон для managed-строк

// TypeInfo-слоты (внутри слота лежит Il2CppClass*).
inline constexpr uint64_t kPmTypeInfoRva   = 0xE66DEE8; // Oxide.PlayerManager       (= PLAYER_MANAGER_TYPEINFO_RVA)
inline constexpr uint64_t kGcTypeInfoRva   = 0xE669190; // Oxide.GameControllerBase (= GAME_CONTROLLER_TYPEINFO_RVA)

// ---- игровые смещения (зеркалят game.cpp; продублированы, чтобы пейлоад
// остался одним TU без тяжёлых include'ов игры).
inline constexpr uint64_t kClassStaticFields = 0xB8;  // Il2CppClass.static_fields (IL2CPP_CLASS_STATIC_FIELDS)
inline constexpr uint64_t kClassName         = 0x10;  // Il2CppClass.name (char*)
inline constexpr uint64_t kListItems         = 0x10;
inline constexpr uint64_t kListSize          = 0x18;
inline constexpr uint64_t kArrFirstElem      = 0x20;  // Array.vector (Array.max_length = +0x18)
inline constexpr uint64_t kStrLength         = 0x10;
inline constexpr uint64_t kStrChars          = 0x14;
inline constexpr uint64_t kPmActiveList      = game_offsets::PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST; // 0x08
inline constexpr uint64_t kPmClientList      = game_offsets::PLAYER_MANAGER_STATIC_FIELDS_LIST;       // 0x10
inline constexpr uint64_t kGcLocalPlayer     = game_offsets::GAME_CONTROLLER_LOCAL_PLAYER_FIELD;      // 0x10
inline constexpr uint64_t kTeamName          = game_offsets::PLAYER_TEAM_NAME;   // 0x2B0
inline constexpr uint64_t kClanId            = game_offsets::PLAYER_CLAN_ID;     // 0x2C0
inline constexpr uint64_t kClanTag           = game_offsets::PLAYER_CLAN_TAG;    // 0x2C8
inline constexpr uint64_t kModelInfoRef      = game_offsets::PLAYER_MODEL_INFO_REF; // 0x260 (PlayerManager -> Yf-обёртка)
inline constexpr uint64_t kWrapModelInfo     = 0x10;   // Yf+0x10 -> PlayerModelInfo
inline constexpr uint64_t kPmiBodyRenderer   = 0x50;   // PlayerModelInfo.skinnedMeshRenderer (dump.cs)
inline constexpr uint64_t kPmiHeadRenderer   = 0x58;   // PlayerModelInfo.headRenderer

inline constexpr int kPollUs  = 200 * 1000;   // опрос контроллера, 200 мс
inline constexpr int kRangeUs = 2500 * 1000;  // обновление карты регионов, 2.5 с

}  // namespace chams_cfg

// ======================= безопасные обращения ==============================
namespace {

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

struct Range { u64 start; u64 end; };

constexpr int kMaxRanges = 1024;
Range g_ranges[kMaxRanges];
int    g_range_count = 0;
u64    g_il2cpp_base = 0;

u64 region_limit(u64 addr) {  // конец региона, содержащего addr; 0 если нет
    int lo = 0, hi = g_range_count - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_ranges[mid].start <= addr) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (best < 0) return 0;
    return (addr < g_ranges[best].end) ? g_ranges[best].end : 0;
}

// Карта readable-регионов /proc/self/maps: обновляется воркером, читается со
// всех потоков. Bad RVA или чужая сборка дают отказ вместо SIGSEGV в игре.
void refresh_maps() {
    int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    char buf[8192];
    Range tmp[kMaxRanges];
    int   cnt = 0;
    u64   il2cpp = 0;
    char  line[512];
    int   llen = 0;
    ssize_t got;
    while ((got = ::read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < got; ++i) {
            if (buf[i] != '\n') {
                if (llen < (int)sizeof(line) - 1) line[llen++] = buf[i];
                continue;
            }
            line[llen] = '\0';
            char* dash = strchr(line, '-');
            if (dash && line[0]) {
                char* endp = nullptr;
                u64 s = strtoull(line, nullptr, 16);
                u64 e = strtoull(dash + 1, &endp, 16);
                char* p = line;
                while (*p && *p != ' ') ++p;
                while (*p == ' ') ++p;
                if (p[0] == 'r' && e > s && cnt < kMaxRanges) {
                    tmp[cnt].start = s;
                    tmp[cnt].end = e;
                    ++cnt;
                }
                if (!il2cpp && strstr(line, "/libil2cpp.so") != nullptr) il2cpp = s;
            }
            llen = 0;
        }
    }
    ::close(fd);
    for (int i = 1; i < cnt; ++i) {  // сортировка для бинарного поиска
        Range key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].start > key.start) { tmp[j + 1] = tmp[j]; --j; }
        tmp[j + 1] = key;
    }
    memcpy(g_ranges, tmp, (size_t)cnt * sizeof(Range));
    g_range_count = cnt;
    if (il2cpp) g_il2cpp_base = il2cpp;  // первая (низшая) карта файла = база ELF
}

bool in_range(u64 addr, u64 size) {
    if (addr < 0x10000 || size > 0x100000000ULL) return false;
    u64 limit = region_limit(addr);
    return limit != 0 && addr + size <= limit;
}

template <class T> bool rd(u64 addr, T* out) {
    if (!in_range(addr, sizeof(T))) return false;
    *out = *reinterpret_cast<const T*>(addr);
    return true;
}
u64 rd_ptr(u64 addr) {
    u64 v = 0;
    rd<u64>(addr, &v);
    return v;
}
bool valid_obj(u64 p) {
    return p >= 0x10000 && p < 0x0001000000000000ULL && (p & 0x7) == 0;
}
template <class T> bool wr(u64 addr, const T& v) {
    if (!in_range(addr, sizeof(T))) return false;
    *reinterpret_cast<T*>(addr) = v;
    return true;
}

// C-строка в пределах своего региона: сравнение без риска вылезти за маппинг.
bool cstr_eq(u64 addr, const char* expect) {
    if (!expect) return false;
    u64 limit = region_limit(addr);
    if (!limit) return false;
    const char* p = reinterpret_cast<const char*>(addr);
    u64 maxn = limit - addr;
    if (maxn > 128) maxn = 128;
    for (u64 k = 0; k < maxn; ++k) {
        if (p[k] != expect[k]) return false;
        if (p[k] == '\0') return true;
    }
    return false;
}

// ---- il2cpp-вызовы: адрес = base + RVA, хвостовой const MethodInfo* не
// используется этими методами → nullptr (FILL: при падении перейти на
// *_Injected-варианты из dump.cs — они вообще без method).
struct Color { float r, g, b, a; };  // HFA: и в C#, и в AAPCS64 идёт в v0

using Fn_Find      = void* (*)(void* str, const void* method);
using Fn_GetMats   = void* (*)(void* renderer, const void* method);
using Fn_GetShader = void* (*)(void* mat, const void* method);
using Fn_SetShader = void (*)(void* mat, void* shader, const void* method);
using Fn_SetColor  = void (*)(void* mat, Color c, const void* method);
using Fn_SetInt    = void (*)(void* mat, void* name, int value, const void* method);
using Fn_DataPath  = void* (*)(const void* method);

struct Api {
    Fn_Find      Find      = nullptr;
    Fn_GetMats   GetMats   = nullptr;
    Fn_GetShader GetShader = nullptr;
    Fn_SetShader SetShader = nullptr;
    Fn_SetColor  SetColor  = nullptr;
    Fn_SetInt    SetInt    = nullptr;
    Fn_DataPath  DataPath  = nullptr;
    bool ok() const {
        return Find && GetMats && GetShader && SetShader && SetColor && SetInt && DataPath;
    }
};
Api g_api{};

bool resolve_api() {
    if (g_api.ok()) return true;
    if (!g_il2cpp_base) return false;
    g_api.Find      = (Fn_Find)(g_il2cpp_base + chams_cfg::kShaderFindRva);
    g_api.GetMats   = (Fn_GetMats)(g_il2cpp_base + chams_cfg::kGetMaterialsRva);
    g_api.GetShader = (Fn_GetShader)(g_il2cpp_base + chams_cfg::kGetShaderRva);
    g_api.SetShader = (Fn_SetShader)(g_il2cpp_base + chams_cfg::kSetShaderRva);
    g_api.SetColor  = (Fn_SetColor)(g_il2cpp_base + chams_cfg::kSetColorRva);
    g_api.SetInt    = (Fn_SetInt)(g_il2cpp_base + chams_cfg::kSetIntRva);
    g_api.DataPath  = (Fn_DataPath)(g_il2cpp_base + chams_cfg::kDataPathRva);
    return g_api.ok();
}

// ---- managed-строки: единственный безопасный источник — геттер, возвращающий
// СВЕЖУЮ строку (Application.dataPath), в которую пишем своё содержимое. GC
// консервативный и неперемещающий → мутация на месте корректна; длина dataPath
// (десятки байт) перекрывает и kShaderName, и "_ZTest"/"_ZWrite". Строку между
// вызовами не держим (не полагаемся на сканирование GC нашего .bss); ассет
// шейдера корневой у движка — его кэшировать можно.
void* make_managed_str(const char* src, int len) {
    if (!g_api.DataPath || len <= 0 || len > 255) return nullptr;
    void* s = g_api.DataPath(nullptr);
    if (!valid_obj((u64)s)) return nullptr;
    int32_t have = 0;
    if (!rd<int32_t>((u64)s + chams_cfg::kStrLength, &have) || have < len) return nullptr;
    u64 chars_addr = (u64)s + chams_cfg::kStrChars;
    if (!in_range(chars_addr, (u64)len * 2)) return nullptr;
    u16* dst = reinterpret_cast<u16*>(chars_addr);
    for (int i = 0; i < len; ++i) dst[i] = (u16)(unsigned char)src[i];
    return wr<int32_t>((u64)s + chams_cfg::kStrLength, len) ? s : nullptr;
}

// ---- группы игроков: 1-в-1 с game.cpp (groups_are_allied), чтобы чамсы
// совпадали с «Союзниками» оверлея: союзник = непустой общий team ИЛИ clan.
struct PlayerGroup {
    char team[40] = {};
    char clan[48] = {};
    char tag[16]  = {};
    bool any() const { return team[0] || clan[0] || tag[0]; }
};

bool read_utf16_str(u64 str_obj, char* out, size_t cap, int32_t max_chars) {
    if (!valid_obj(str_obj) || !out || cap < 2) return false;
    int32_t length = 0;
    if (!rd<int32_t>(str_obj + chams_cfg::kStrLength, &length)) return false;
    if (max_chars > 63) max_chars = 63;
    if (length <= 0 || length > max_chars) return false;
    if (!in_range(str_obj + chams_cfg::kStrChars, (u64)length * 2)) return false;
    const u16* chars = reinterpret_cast<const u16*>(str_obj + chams_cfg::kStrChars);
    size_t pos = 0;
    for (int32_t i = 0; i < length && pos + 1 < cap; ++i) {
        u32 cp = chars[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < length) {
            u32 lo = chars[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            } else {
                cp = '?';
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = '?';
        }
        char enc[4];
        int enc_len = 1;
        if (cp < 0x80) {
            if (cp < 0x20 || cp == 0x7F) cp = '?';
            enc[0] = (char)cp; enc_len = 1;
        } else if (cp < 0x800) {
            enc[0] = (char)(0xC0 | (cp >> 6));
            enc[1] = (char)(0x80 | (cp & 0x3F));
            enc_len = 2;
        } else if (cp < 0x10000) {
            enc[0] = (char)(0xE0 | (cp >> 12));
            enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[2] = (char)(0x80 | (cp & 0x3F));
            enc_len = 3;
        } else if (cp <= 0x10FFFF) {
            enc[0] = (char)(0xF0 | (cp >> 18));
            enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[3] = (char)(0x80 | (cp & 0x3F));
            enc_len = 4;
        } else {
            enc[0] = '?'; enc_len = 1;
        }
        if (pos + (size_t)enc_len >= cap) break;
        for (int k = 0; k < enc_len; ++k) out[pos++] = enc[k];
    }
    out[pos] = '\0';
    return pos > 0;
}

void read_player_group(u64 player, PlayerGroup* out) {
    *out = PlayerGroup{};
    if (!valid_obj(player)) return;
    read_utf16_str(rd_ptr(player + chams_cfg::kTeamName), out->team, sizeof(out->team), 39);
    read_utf16_str(rd_ptr(player + chams_cfg::kClanId),  out->clan, sizeof(out->clan), 47);
    read_utf16_str(rd_ptr(player + chams_cfg::kClanTag), out->tag,  sizeof(out->tag), 15);
    (void)out->any();
}

bool groups_allied(const PlayerGroup& local, const PlayerGroup& other) {
    if (local.team[0] && strcmp(local.team, other.team) == 0) return true;
    if (local.clan[0] && strcmp(local.clan, other.clan) == 0) return true;
    return false;
}

// ---- карта «материал -> его родной шейдер» для выключения. Трогаются только
// ТЕКУЩИЕ материалы (свежие из get_materials) и при включении, и при выключении
// — осиротевшие записи никогда не разыменовываются.
struct MatOrig { void* mat; void* orig; };
constexpr int kMatCap = 512;
MatOrig g_mat_orig[kMatCap];
int     g_mat_count = 0;
int     g_mat_head  = 0;

void* orig_of(void* mat) {
    for (int i = 0; i < g_mat_count; ++i)
        if (g_mat_orig[i].mat == mat) return g_mat_orig[i].orig;
    return nullptr;
}
void remember_orig(void* mat, void* orig) {
    if (orig_of(mat)) return;
    if (g_mat_count < kMatCap) {
        g_mat_orig[g_mat_count++] = {mat, orig};
    } else {
        g_mat_orig[g_mat_head] = {mat, orig};
        g_mat_head = (g_mat_head + 1) % kMatCap;
    }
}

// ---- ленивая инициализация шейдера: только на потоке рендера, внутри
// захваченного Render — там il2cpp полностью разбужен. Частота — раз в ~60
// попыток, чтобы неsound Applicаtion.dataPath на каждый кадр при сбое.
void* g_chams_shader = nullptr;
int   g_shader_retry  = 0;
std::atomic<int> g_tick_diag{0};  // 0 пусто, 5 шейдер не найден, 6 кадр отработал

bool ensure_shader() {
    if (g_chams_shader) return true;
    int n = g_shader_retry++;
    if (n != 0 && (n % 60) != 0) return false;
    void* s = make_managed_str(chams_cfg::kShaderName, (int)strlen(chams_cfg::kShaderName));
    if (!s) { g_tick_diag.store(5); return false; }
    void* sh = g_api.Find(s, nullptr);
    if (!valid_obj((u64)sh)) { g_tick_diag.store(5); return false; }
    g_chams_shader = sh;
    return true;
}

// Применить/восстановить материалы одного рендерера.
void apply_one_renderer(u64 rmr, bool restore) {
    if (!valid_obj(rmr) || !g_api.GetMats) return;
    void* arr = g_api.GetMats(reinterpret_cast<void*>(rmr), nullptr);
    if (!valid_obj((u64)arr)) return;
    int32_t n = 0;
    if (!rd<int32_t>((u64)arr + chams_cfg::kArrFirstElem - 8, &n)) return;  // Array.max_length @ +0x18
    if (n <= 0 || n > 16) return;
    for (int32_t i = 0; i < n; ++i) {
        u64 mat = rd_ptr((u64)arr + chams_cfg::kArrFirstElem + (u64)i * 8);
        if (!valid_obj(mat)) continue;
        void* sh = g_api.GetShader(reinterpret_cast<void*>(mat), nullptr);
        if (!valid_obj((u64)sh)) continue;
        if (restore) {
            if (g_chams_shader && sh == g_chams_shader) {
                void* orig = orig_of(reinterpret_cast<void*>(mat));
                if (orig) g_api.SetShader(reinterpret_cast<void*>(mat), orig, nullptr);
            }
            continue;
        }
        if (sh == g_chams_shader) continue;  // уже наш
        void* orig = orig_of(reinterpret_cast<void*>(mat));
        if (!orig) remember_orig(reinterpret_cast<void*>(mat), sh);  // первый встреченный = родной
        g_api.SetShader(reinterpret_cast<void*>(mat), g_chams_shader, nullptr);
        Color c{chams_cfg::kColorR, chams_cfg::kColorG, chams_cfg::kColorB, chams_cfg::kColorA};
        g_api.SetColor(reinterpret_cast<void*>(mat), c, nullptr);
        // ZTest Always / ZWrite off — где шейдер экспонирует свойства; молчаливый
        // no-op, где не экспонирует (см. FILL в шапке).
        void* ztest  = make_managed_str("_ZTest", 6);
        void* zwrite = make_managed_str("_ZWrite", 7);
        if (ztest)  g_api.SetInt(reinterpret_cast<void*>(mat), ztest, 8, nullptr);
        if (zwrite) g_api.SetInt(reinterpret_cast<void*>(mat), zwrite, 0, nullptr);
    }
}

// Полный проход по игрокам: локальный + союзники не трогаются.
void chams_walk(bool restore) {
    u64 base = g_il2cpp_base;
    if (!base || !g_api.ok()) return;
    u64 pm_class = rd_ptr(base + chams_cfg::kPmTypeInfoRva);
    u64 gc_class = rd_ptr(base + chams_cfg::kGcTypeInfoRva);
    if (!valid_obj(pm_class) || !valid_obj(gc_class)) return;
    u64 pm_statics = rd_ptr(pm_class + chams_cfg::kClassStaticFields);
    u64 gc_statics = rd_ptr(gc_class + chams_cfg::kClassStaticFields);
    if (!valid_obj(pm_statics) || !valid_obj(gc_statics)) return;

    u64 local = rd_ptr(gc_statics + chams_cfg::kGcLocalPlayer);
    if (!valid_obj(local) || rd_ptr(local) != pm_class) local = 0;

    PlayerGroup local_group{};
    if (local) read_player_group(local, &local_group);

    // Оба реестровых списка, как в game.cpp (client+active, дубликаты не страшны).
    const u64 list_fields[2] = {chams_cfg::kPmClientList, chams_cfg::kPmActiveList};
    for (u64 lf : list_fields) {
        u64 list = rd_ptr(pm_statics + lf);
        if (!valid_obj(list)) continue;
        u64 items = rd_ptr(list + chams_cfg::kListItems);
        int32_t count = 0;
        if (!rd<int32_t>(list + chams_cfg::kListSize, &count)) continue;
        if (!valid_obj(items) || count < 0 || count > 512) continue;
        for (int32_t i = 0; i < count; ++i) {
            u64 p = rd_ptr(items + chams_cfg::kArrFirstElem + (u64)i * 8);
            if (!valid_obj(p) || p == local) continue;
            if (rd_ptr(p) != pm_class) continue;
            PlayerGroup grp;
            read_player_group(p, &grp);
            if (groups_allied(local_group, grp)) continue;
            u64 wrap = rd_ptr(p + chams_cfg::kModelInfoRef);
            if (!valid_obj(wrap)) continue;
            u64 pmi = rd_ptr(wrap + chams_cfg::kWrapModelInfo);
            if (!valid_obj(pmi)) continue;
            apply_one_renderer(rd_ptr(pmi + chams_cfg::kPmiBodyRenderer), restore);
            apply_one_renderer(rd_ptr(pmi + chams_cfg::kPmiHeadRenderer), restore);
        }
    }
}

// ======================= хук: стаб + патч «B» ==============================
// Стаб (arm64, LE; байты проверены capstone-дизассемблером):
//   00 stp x0,x1,[sp,#-0x50]!
//   .. stp x2..x8,x30
//   14 ldr x16,#0x38           литерал со ссылкой на bw_chams_tick (внутри
//                              стаба — диапазон LDR всегда покрыт)
//   18 blr x16                 вызов C-логики (x0..x8,lr уже сохранены)
//   1C..2C ldp ...             восстановление
//   30 <оригинальное 1-е слово>  патч воркера (вставляется как есть, без PC-rel)
//   34 B target+4                патч воркера (±128 МБ — отсюда хинт-страница)
//   38 .quad &bw_chams_tick      патч воркера
constexpr u32 kStubHead[] = {
    0xA9BB07E0,  // stp x0, x1, [sp, #-0x50]!
    0xA9010FE2,  // stp x2, x3, [sp, #0x10]
    0xA90217E4,  // stp x4, x5, [sp, #0x20]
    0xA9031FE6,  // stp x6, x7, [sp, #0x30]
    0xA9047BE8,  // stp x8, x30, [sp, #0x40]
    0x58000130,  // ldr x16, #0x38
    0xD63F0200,  // blr x16
    0xA9447BE8,  // ldp x8, x30, [sp, #0x40]
    0xA9431FE6,  // ldp x6, x7, [sp, #0x30]
    0xA94217E4,  // ldp x4, x5, [sp, #0x20]
    0xA9410FE2,  // ldp x2, x3, [sp, #0x10]
    0xA8C507E0,  // ldp x0, x1, [sp], #0x50
};
constexpr u64 kStubInsn0Off  = 0x30;
constexpr u64 kStubBranchOff = 0x34;
constexpr u64 kStubTickOff   = 0x38;
constexpr u64 kStubSize      = 0x40;

bool is_pc_relative(u32 op) {
    if ((op & 0x7C000000u) == 0x14000000u) return true;   // B / BL
    if ((op & 0xFF000010u) == 0x54000000u) return true;   // B.cond
    if ((op & 0x7E000000u) == 0x34000000u) return true;   // CBZ / CBNZ
    if ((op & 0x7E000000u) == 0x36000000u) return true;   // TBZ / TBNZ
    if ((op & 0x1F000000u) == 0x10000000u) return true;   // ADR
    if ((op & 0x9F000000u) == 0x90000000u) return true;   // ADRP
    if ((op & 0x3B000000u) == 0x18000000u) return true;   // LDR/PRFM literal (включая FP)
    return false;
}

u32 encode_branch(u64 from, u64 to) {
    int64_t delta = (int64_t)(to - from) / 4;
    return 0x14000000u | ((u32)delta & 0x03FFFFFFu);
}

struct Hook {
    u64  target = 0;
    u64  stub   = 0;
    u32  orig   = 0;
    bool armed  = false;
};
Hook g_hooks[2];
int  g_hook_count = 0;

// Хинт-страница в пределах ±128 МБ от цели: пробуем 4..120 МБ в обе стороны.
u64 hunt_stub_page(u64 target) {
    static const int kDeltasMB[] = {4, 8, 16, 32, 64, 96, 120};
    u64 page_base = target & ~0xFFFULL;
    for (int pass = 0; pass < 2; ++pass) {
        int sign = pass ? -1 : 1;
        for (int mb : kDeltasMB) {
            u64 delta = (u64)mb * 1024ull * 1024ull;
            if (sign < 0 && page_base <= delta + 0x10000) continue;
            u64 hint = page_base + (u64)sign * delta;
            void* p = mmap(reinterpret_cast<void*>(hint), kStubSize,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) continue;
            if ((u64)p != hint) { munmap(p, kStubSize); continue; }
            u64 d = (u64)(hint > target ? hint - target : target - hint);
            if (d >= 0x7000000ull) { munmap(p, kStubSize); continue; }  // запас до предела ±128 МБ
            return hint;
        }
    }
    return 0;
}

extern "C" void bw_chams_tick();  // определение ниже

bool install_one(u64 target) {
    if (!in_range(target, 4)) return false;
    u32 in0 = 0;
    if (!rd<u32>(target, &in0)) return false;
    if (is_pc_relative(in0)) return false;  // FILL: пролог не переносится — хукнуть иной метод

    u64 stub = hunt_stub_page(target);  // PROT_READ|WRITE, ещё не исполняема
    if (!stub) return false;

    u8 code[kStubSize] = {};
    memcpy(code, kStubHead, sizeof(kStubHead));
    memcpy(code + kStubInsn0Off, &in0, 4);
    u32 br = encode_branch(stub + kStubBranchOff, target + 4);
    memcpy(code + kStubBranchOff, &br, 4);
    u64 tick = (u64)(uintptr_t)&bw_chams_tick;
    memcpy(code + kStubTickOff, &tick, 8);

    // W^X для стаба: пишем в RW, затем RX (RWX-состояния не бывает).
    memcpy(reinterpret_cast<void*>(stub), code, kStubSize);
    if (mprotect(reinterpret_cast<void*>(stub), kStubSize, PROT_READ | PROT_EXEC) != 0) {
        munmap(reinterpret_cast<void*>(stub), kStubSize);
        return false;
    }
    __builtin___clear_cache(reinterpret_cast<char*>(stub),
                            reinterpret_cast<char*>(stub) + kStubSize);

    // Патч цели: мгновенный RWX (другие потоки исполняют соседние байты),
    // одно атомарное 32-битное слово (на AArch64 — single-copy atomic),
    // icache-flush, права обратно как были.
    u64 page = target & ~0xFFFULL;
    u64 span = ((target + 4 + 0xFFF) & ~0xFFFULL) - page;  // на случай границы страницы
    if (mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        munmap(reinterpret_cast<void*>(stub), kStubSize);
        return false;
    }
    __atomic_store_n(reinterpret_cast<u32*>(target), encode_branch(target, stub), __ATOMIC_RELEASE);
    __builtin___clear_cache(reinterpret_cast<char*>(target), reinterpret_cast<char*>(target) + 4);
    mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_EXEC);

    g_hooks[g_hook_count++] = Hook{target, stub, in0, true};
    return true;
}

// false = структурный отказ (пролог/страница): не долбить повторами каждый
// poll; сбрасывается при выключении тумблера.
bool install_hooks() {
    if (g_hook_count > 0) return true;
    if (!resolve_api()) return false;
    bool a = install_one(g_il2cpp_base + chams_cfg::kRenderListRva);
    bool b = install_one(g_il2cpp_base + chams_cfg::kRenderArrRva);
    return a || b;
}

void uninstall_hooks() {
    for (int i = 0; i < g_hook_count; ++i) {
        Hook& h = g_hooks[i];
        if (!h.armed) continue;
        u64 page = h.target & ~0xFFFULL;
        u64 span = ((h.target + 4 + 0xFFF) & ~0xFFFULL) - page;
        if (mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            __atomic_store_n(reinterpret_cast<u32*>(h.target), h.orig, __ATOMIC_RELEASE);
            __builtin___clear_cache(reinterpret_cast<char*>(h.target),
                                    reinterpret_cast<char*>(h.target) + 4);
            mprotect(reinterpret_cast<void*>(page), span, PROT_READ | PROT_EXEC);
        }
        if (h.stub) munmap(reinterpret_cast<void*>(h.stub), kStubSize);
        h.armed = false;
    }
    g_hook_count = 0;
}

// ======================== сам хук (поток рендера) =========================
std::atomic<int> g_active{0};        // включено контроллером (выставляет воркер)
std::atomic<int> g_applied{0};       // материалы сейчас в чамсах
std::atomic<int> g_restore_done{1};  // выключение обработано кадром

extern "C" void bw_chams_tick() {
    if (!g_api.ok()) { resolve_api(); if (!g_api.ok()) return; }
    bool want = g_active.load(std::memory_order_acquire) != 0;
    if (want) {
        if (ensure_shader()) {
            chams_walk(false);
            g_applied.store(1, std::memory_order_release);
            g_restore_done.store(0, std::memory_order_release);
            if (g_tick_diag.load() != 5) g_tick_diag.store(6);
        }
    } else if (g_applied.load(std::memory_order_acquire)) {
        chams_walk(true);
        g_applied.store(0, std::memory_order_release);
        g_tick_diag.store(6);
    } else {
        g_restore_done.store(1, std::memory_order_release);
    }
}

// ============================== воркер ====================================
void diag_write(char c) {  // один байт статуса — вся «телеметрия» пейлоада
    static char last = 0;
    if (c == last) return;
    int fd = ::open(chams_cfg::kDiagPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    (void)::write(fd, &c, 1);
    ::close(fd);
    last = c;
}

bool read_ctl() {
    int fd = ::open(chams_cfg::kCtlPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char c = 0;
    ssize_t n = ::read(fd, &c, 1);
    ::close(fd);
    return n == 1 && c == '1';
}

// Гейт сборки: имя класса PlayerManager из TypeInfo-слота. Чужой/обновлённый
// билд игры даёт мусор → отказ, хук не ставится никогда (RVAs-то другие).
bool class_gate(u64 base) {
    u64 pm = rd_ptr(base + chams_cfg::kPmTypeInfoRva);
    if (!valid_obj(pm)) return false;
    u64 name = rd_ptr(pm + chams_cfg::kClassName);
    if (!name) return false;
    return cstr_eq(name, "PlayerManager");
}

void* worker_main(void*) {
    u64  maps_acc  = 0;
    bool gate_ok   = false;
    bool install_failed = false;
    for (;;) {
        maps_acc += (u64)chams_cfg::kPollUs;
        if (maps_acc >= (u64)chams_cfg::kRangeUs || g_range_count == 0) {
            maps_acc = 0;
            refresh_maps();
        }
        if (!g_il2cpp_base) { diag_write('1'); ::usleep(chams_cfg::kPollUs); continue; }
        if (!gate_ok) {
            gate_ok = class_gate(g_il2cpp_base);
            if (!gate_ok) { diag_write('2'); ::usleep(chams_cfg::kPollUs); continue; }
        }
        bool want = read_ctl();
        g_active.store(want ? 1 : 0, std::memory_order_release);
        if (want && g_hook_count == 0 && !install_failed) {
            if (!install_hooks()) install_failed = true;
        }
        if (!want) {
            install_failed = false;  // новый цикл вкл/выкл — даём шанс снова
            if (g_hook_count > 0 && g_restore_done.load(std::memory_order_acquire))
                uninstall_hooks();
        }
        int tick = g_tick_diag.load();
        char d;
        if (g_hook_count > 0) {
            if (tick == 5) d = '5';
            else if (!want) d = '4';
            else d = (tick == 6) ? '6' : '3';
        } else {
            d = '0';  // простой: гейт прошёл, ничего не включено
        }
        diag_write(d);
        ::usleep(chams_cfg::kPollUs);
    }
    return nullptr;
}

__attribute__((constructor)) void chams_payload_init() {
    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&th, &attr, worker_main, nullptr);
    pthread_attr_destroy(&attr);
}

}  // namespace
