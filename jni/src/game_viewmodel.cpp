#include "fps_memory.h"
#include "game.h"
#include "weapon_tuning_memory.h"
#include "fast_fire_memory.h"
#include "fast_loot_memory.h"
#include "no_recoil_memory.h"
#include "no_ballistics_memory.h"
#include "object_data_memory.h"
#include "app_state.h"
#include "farm.h"
#include "maps_lookup.h"
#include <chrono>
#include <cerrno>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <limits>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include "game_viewmodel_internal.h"
#include "game_internal.h"

namespace {
struct Memory {
    template<class T> bool read(uint64_t address, T& value) {
        return address >= 0x10000 && g_mem.read_quiet(address, &value, sizeof(value));
    }
    template<class T> bool write(uint64_t address, const T& value) {
        return address >= 0x10000 && g_mem.write(address, &value, sizeof(value));
    }
    uint64_t ptr(uint64_t address) {
        uint64_t p = 0;
        return read(address, p) ? memio::untag(p) : 0;
    }
    bool klass_is(uint64_t klass, const char* name) {
        using namespace go_active;
        if (!klass) return false;
        char n[64]{}, ns[64]{};
        auto np = ptr(klass + IL2CPP_CLASS_NAME), sp = ptr(klass + IL2CPP_CLASS_NAMESPACE);
        // Указатели имён невалидны — класс структурно мёртв, это не «нет данных».
        if (np < 0x10000 || sp < 0x10000) return false;
        // Строки имён лежат в памяти метаданных, которая на части устройств не
        // читается транспортом (EIO; см. class_identity в game.cpp — там же
        // прецедент структурного допуска). Не прочиталось — верим структуре:
        // объект уже пришёл по проверенной цепочке (свой пайплайн игрока или
        // TypeInfo-слот этого релиза). Прочиталось — только точное имя.
        if (!g_mem.read_quiet(np, n, sizeof(n)) || !g_mem.read_quiet(sp, ns, sizeof(ns))) return true;
        n[63] = ns[63] = 0;
        return std::strcmp(n, name) == 0 && std::strcmp(ns, "Oxide") == 0;
    }
    bool is(uint64_t object, const char* name) { return object && klass_is(ptr(object), name); }
};
Memory memory;

// ---- Снимок /proc/<pid>/maps ----------------------------------------------
//
// Карта памяти процесса игры — это тысячи строк (сотни килобайт). Её разбор —
// десятки миллисекунд на слабом SoC, а ворота записи раньше перечитывали и
// разбирали её В ПОТОКЕ ОТРИСОВКИ каждые 2 секунды: раз в пару
// секунд встают гуи и все визуалы на секунду, и так по кругу (жалоба: «визуалы
// зависают через несколько секунд на секунду»). Теперь карту читает и
// разбирает фоновый поток (heap_maps_worker), а поток отрисовки только ищет
// адрес в готовом отсортированном списке подтверждённых rw-анонимных
// отображений — это бинарный поиск под коротким мьютексом, без syscall'ов.
// Правила ворот не ослаблены: список строит тот же набор условий, что и
// object_data::heap_writable_range (см. тест test_object_data_ranges.cpp);
// список старше kMapsStaleSec считается недействительным (воркер завис) — тогда
// запись отклоняется, а не берётся из просроченной карты.
// Адрес, которого нет в списке, ставит запрос на внеочередное обновление: новое
// отображение подхватывается через ~100 мс, а запись в этот кадр отклоняется
// (контроллеры оружия и так повторяют попытку каждый тик).
constexpr double kMapsRefreshSec = 1.0;   // период обновления, пока ворота востребованы
constexpr double kMapsStaleSec   = 6.0;   // старше — список недействителен
constexpr double kMapsIdleSec    = 30.0;  // ворота не спрашивали дольше — воркер спит

bool read_proc_maps(pid_t pid, std::string& out) {
    out.clear();
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/maps", (int)pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buffer[64 * 1024];
    for (;;) {
        const ssize_t got = ::read(fd, buffer, sizeof(buffer));
        if (got < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        if (got == 0) break;
        if (out.size() + (size_t)got > (8u << 20)) { ::close(fd); return false; }
        out.append(buffer, (size_t)got);
    }
    ::close(fd);
    return !out.empty();
}

std::mutex                         heap_ranges_mu;
std::vector<object_data::HeapRange> heap_ranges;
pid_t                              heap_ranges_pid = -1;
double                             heap_ranges_at = -1e9;     // когда список построен
double                             heap_ranges_asked_at = -1e9;
bool                               heap_ranges_want = false;  // внеочередное обновление
uint64_t                           heap_ranges_gen = 1;       // смена сессии отбрасывает опоздавший результат
std::once_flag                     heap_worker_once;

void invalidate_maps_snapshot() {
    std::lock_guard<std::mutex> lock(heap_ranges_mu);
    heap_ranges.clear();
    heap_ranges_pid = -1;
    heap_ranges_at = -1e9;
    heap_ranges_want = false;
    ++heap_ranges_gen;
}

// Читает и разбирает карту ВНЕ мьютекса (это и есть тяжёлая часть), под ним —
// только подмена списка.
void refresh_heap_ranges(pid_t pid, uint64_t gen) {
    static thread_local std::string text;
    static thread_local std::vector<object_data::HeapRange> fresh;
    if (pid <= 0 || !read_proc_maps(pid, text)) return;
    object_data::heap_writable_ranges(text, fresh);
    const double now = memio::now_seconds();
    std::lock_guard<std::mutex> lock(heap_ranges_mu);
    if (gen != heap_ranges_gen || pid != g_pid) return;   // сессия сменилась, пока читали
    heap_ranges.swap(fresh);
    heap_ranges_pid = pid;
    heap_ranges_at = now;
}

void heap_maps_worker() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pid_t pid = -1;
        uint64_t gen = 0;
        bool due = false;
        {
            std::lock_guard<std::mutex> lock(heap_ranges_mu);
            const double now = memio::now_seconds();
            pid = g_pid;
            gen = heap_ranges_gen;
            const bool in_use = now - heap_ranges_asked_at < kMapsIdleSec;
            due = pid > 0 && in_use &&
                  (heap_ranges_want || heap_ranges_pid != pid ||
                   now - heap_ranges_at >= kMapsRefreshSec);
            if (due) heap_ranges_want = false;
        }
        if (due) refresh_heap_ranges(pid, gen);
    }
}

// Единственные ворота записи в кучу игры: адрес обязан лежать в анонимном
// rw-отображении, подтверждённом разбором /proc/<pid>/maps. В установившемся
// режиме (список свежий, его держит воркер) чтений карты здесь нет — только
// поиск в списке. Синхронное чтение остаётся ровно в одном случае: списка нет
// или он протух (первое включение, смена процесса, долгий простой воркера) —
// иначе первая запись после простоя, в том числе ВОССТАНОВЛЕНИЕ оригиналов при
// выключении функции, отклонялась бы и контроллер уходил в Failed. Это разовая
// цена, а не периодическая: пока ворота востребованы, воркер держит список
// свежим сам.
bool heap_gate_allows(uint64_t address, size_t size) {
    if (g_pid <= 0 || address < 0x10000 || !size || size > 64 ||
        address > std::numeric_limits<uint64_t>::max() - size) return false;
    std::call_once(heap_worker_once, [] { std::thread(heap_maps_worker).detach(); });
    const double now = memio::now_seconds();
    uint64_t gen = 0;
    {
        std::lock_guard<std::mutex> lock(heap_ranges_mu);
        heap_ranges_asked_at = now;
        gen = heap_ranges_gen;
        // Список принадлежит конкретному процессу: сменился pid — подтверждения
        // из прошлой сессии недействительны.
        if (heap_ranges_pid != g_pid) {
            heap_ranges.clear();
            heap_ranges_pid = -1;
            heap_ranges_at = -1e9;
        }
        const double age = now - heap_ranges_at;
        if (heap_ranges_pid == g_pid && age >= 0.0 && age < kMapsStaleSec) {
            if (object_data::heap_range_covers(heap_ranges, address, size)) return true;
            heap_ranges_want = true;   // нового отображения нет в списке: обновить вне очереди
            return false;
        }
    }
    // Списка нет или он протух: одно синхронное чтение, не чаще раза в секунду
    // (карта не читается — не долбим её каждый кадр).
    static double last_sync_at = -1e9;
    if (now - last_sync_at < 1.0) return false;
    last_sync_at = now;
    refresh_heap_ranges(g_pid, gen);
    std::lock_guard<std::mutex> lock(heap_ranges_mu);
    return heap_ranges_pid == g_pid && now - heap_ranges_at < kMapsStaleSec + 1.0 &&
           object_data::heap_range_covers(heap_ranges, address, size);
}

viewmodel::Controller<Memory> controller(memory);
viewmodel::FpsController<Memory> fps_controller(memory);
// Deliberately do not inherit Memory::write: these controllers can only write
// through the fresh anonymous-heap mapping gate.
struct HeapMemory {
    template<class T> bool read(uint64_t address, T& value) { return memory.read(address,value); }
    // Диапазонное чтение (пакетные обходы плеер-лупа быстрого подбора):
    // один запрос на непрерывный кусок вместо чтения по элементу.
    bool read_range(uint64_t address, void* out, std::size_t size) {
        return address >= 0x10000 && size > 0 && g_mem.read_quiet(address, out, size);
    }
    bool named_klass(uint64_t klass, const char* name, const char* namespaze) {
        if (klass < 0x10000) return false;
        uint64_t np=memory.ptr(klass+go_active::IL2CPP_CLASS_NAME);
        uint64_t sp=memory.ptr(klass+go_active::IL2CPP_CLASS_NAMESPACE);
        if (np < 0x10000 || sp < 0x10000) return false;   // мёртвый класс, не «нет данных»
        char n[96]{}, ns[96]{};
        const size_t nl=std::strlen(name)+1, sl=std::strlen(namespaze)+1;
        if (nl>sizeof(n) || sl>sizeof(ns)) return false;
        // Имена классов лежат в памяти метаданных: на части устройств она не
        // читается транспортом ВООБЩЕ (EIO), а под нагрузкой чтения падают и
        // на «здоровых» (до 4% за кадр, лог 16.09). Раньше любой такой сбой
        // ронял всю цепочку оружия («не применено», «ожидание») — теперь
        // нечитаемое имя допускает объект ПО СТРУКТУРЕ (прецедент —
        // class_identity в game.cpp), а прочитанное сверяется как раньше.
        if (!g_mem.read_quiet(np,n,nl) || !g_mem.read_quiet(sp,ns,sl)) return true;
        if (std::memcmp(ns,namespaze,sl)!=0) return false;
        if (std::memcmp(n,name,nl)==0) return true;
        // Читаемое, но другое имя: принимаем класс-НАСЛЕДНИК (имя содержит
        // базовое) — у части стволов конфиги и компоненты в своих подклассах
        // («…WeaponConfig» и т.п.), и точное сравнение оставляло их без
        // настроенных полей. Остальная цепочка (бэклинки, кросс-ссылки
        // калькулятора, ворота записи) подтверждает объект независимо.
        n[sizeof(n)-1]=0;
        return std::strstr(n,name)!=nullptr;
    }
    bool exact(uint64_t object, const char* name, const char* namespaze) {
        if (object < 0x10000) return false;
        const uint64_t klass=memory.ptr(object);
        if (klass < 0x10000) return false;
        const uint64_t np=memory.ptr(klass+go_active::IL2CPP_CLASS_NAME);
        const uint64_t sp=memory.ptr(klass+go_active::IL2CPP_CLASS_NAMESPACE);
        char n[96]{}, ns[96]{};
        const size_t nl=std::strlen(name)+1, sl=std::strlen(namespaze)+1;
        // Type-specific timing offsets must never use the permissive
        // metadata-read fallback used by the legacy object-data chain.
        return np>=0x10000 && sp>=0x10000 && nl<=sizeof(n) && sl<=sizeof(ns) &&
            g_mem.read_quiet(np,n,nl) && g_mem.read_quiet(sp,ns,sl) &&
            std::memcmp(n,name,nl)==0 && std::memcmp(ns,namespaze,sl)==0;
    }
    bool named(uint64_t object, const char* name, const char* namespaze) {
        return object>=0x10000 && named_klass(memory.ptr(object),name,namespaze);
    }
    bool describe(uint64_t object, char* name, size_t name_size,
                  char* namespaze, size_t namespace_size) {
        if (object < 0x10000 || !name || !namespaze || name_size < 2 || namespace_size < 2)
            return false;
        const uint64_t klass = memory.ptr(object);
        if (klass < 0x10000) return false;
        const uint64_t name_ptr = memory.ptr(klass + go_active::IL2CPP_CLASS_NAME);
        const uint64_t namespace_ptr = memory.ptr(klass + go_active::IL2CPP_CLASS_NAMESPACE);
        auto copy_cstr = [&](uint64_t source, char* out, size_t capacity) {
            if (source < 0x10000) return false;
            for (size_t i = 0; i + 1 < capacity; ++i) {
                if (!memory.read(source + i, out[i])) { out[0] = 0; return false; }
                if (out[i] == 0) return true;
            }
            out[capacity - 1] = 0;
            return true;
        };
        return copy_cstr(name_ptr, name, name_size) &&
               copy_cstr(namespace_ptr, namespaze, namespace_size);
    }
    // Calculator names rotate between builds (Ze/FJ/...). Its empty namespace
    // and both typed backlinks are checked by the spread controller before any write.
    bool calculator_class(uint64_t object) {
        if (object < 0x10000) return false;
        const uint64_t klass=memory.ptr(object);
        if (klass < 0x10000) return false;
        const uint64_t np=memory.ptr(klass+go_active::IL2CPP_CLASS_NAME);
        const uint64_t sp=memory.ptr(klass+go_active::IL2CPP_CLASS_NAMESPACE);
        char ns=1, name[33]{};
        if (!np || !sp || !g_mem.read_quiet(sp,&ns,1) || ns ||
            !g_mem.read_quiet(np,name,sizeof(name))) return false;
        for (unsigned i=0;i<sizeof(name);++i) {
            if (!name[i]) return i>0;
            const unsigned char c=(unsigned char)name[i];
            if (!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
                  (c>='0'&&c<='9') || c=='_')) return false;
        }
        return false;
    }
    template<class T> bool write_heap(uint64_t address, const T& value) {
        if (!g_mem.bound() || g_pid<=0) return false;
        // Чтение и разбор всей /proc/<pid>/maps на КАЖДУЮ запись (а быстрая
        // стрельба и перезарядка пишут до семи полей в кадре) — это сотни
        // килобайт текста и миллисекунды на запись: оверлей вместе с гуи и
        // визуалами вставал именно на этом. Ворота не ослаблены — просто
        // подтверждённые отображения держит в списке фоновый поток (см. выше),
        // а запись по неподтверждённому адресу отклоняется.
        return heap_gate_allows(address,sizeof(T)) && g_mem.write(address,&value,sizeof(T));
    }
};
HeapMemory heap_memory;
weapon_tuning::Controller<HeapMemory> weapons(heap_memory);
fast_fire::Controller<HeapMemory> rapid_fire(heap_memory);
fast_loot::Controller<HeapMemory> loot_speed(heap_memory);
no_recoil::Controller<HeapMemory> no_recoil_ctl(heap_memory);
no_ballistics::Controller<HeapMemory> no_ballistics_ctl(heap_memory);

// Причина остановки быстрой стрельбы для лога: расшифровка стадии, которую
// выставляет fast_fire::Controller (resolve/build_plan/apply в
// fast_fire_memory.h). Стадия 0 + не-Active — ожидание привязки/игрока.
const char* fast_fire_stop_reason(viewmodel::Status status, int stage) {
    if (status == viewmodel::Status::Off) return "disabled or game unbound";
    switch (stage) {
        case 1:  return "no local player or network identity";
        case 2:  return "FPManager missing or dead";
        case 3:  return "weapon slot in hands is empty";
        case 4:  return "weapon type not supported";
        case 5:  return "weapon backlink to player mismatch";
        case 6:  return "fire mode or burst wait unreadable";
        case 7:  return "weapon timings unreadable or out of range";
        case 8:  return "weapon context changed mid-write";
        case 9:  return "originals could not be restored (restart the game)";
        case 10: return "period write not confirmed by readback";
        case 11: return "burst overlap guard: cache shorter than full burst cycle";
        default: return "waiting for bind or player";
    }
}
// «Без отдачи»: расшифровка стадий из no_recoil_memory.h для лога.
const char* no_recoil_stop_reason(viewmodel::Status status, int stage) {
    if (status == viewmodel::Status::Off) return "disabled or game unbound";
    switch (stage) {
        case 1: case 2:  return "no local player";
        case 3: case 4:  return "network identity missing or not local";
        case 5:          return "FPManager missing or dead";
        case 6:          return "not a hitscan in hands";
        case 7:          return "weapon backlink to player mismatch";
        case 8:          return "HitscanWeaponConfig not found";
        case 9:          return "RecoilConfig asset not found";
        case 10: case 12: return "recoil values unreadable";
        case 11:         return "weapon context changed mid-write";
        case 13: case 14: return "write not confirmed by readback";
        default:         return "waiting for bind or player";
    }
}
const char* no_ballistics_kind_name(no_ballistics::Kind kind) {
    using no_ballistics::Kind;
    switch (kind) {
        case Kind::Hitscan: return "hitscan";
        case Kind::Bow: return "bow";
        case Kind::Crossbow: return "crossbow";
        case Kind::RocketLauncher: return "rocket";
        case Kind::ExplosiveCharge: return "charge";
        case Kind::Snowball: return "snowball";
        case Kind::Spear: return "spear";
        case Kind::Melee: return "melee/tool";
        default: return "unknown";
    }
}
const char* no_ballistics_status_name(viewmodel::Status status) {
    switch (status) {
        case viewmodel::Status::Off: return "off";
        case viewmodel::Status::Waiting: return "waiting";
        case viewmodel::Status::Active: return "active";
        case viewmodel::Status::Failed: return "failed";
        case viewmodel::Status::ADS: return "ads";
        default: return "unknown";
    }
}
const char* no_ballistics_stop_reason(viewmodel::Status status, int stage) {
    if (status == viewmodel::Status::Off) return "disabled or game unbound";
    switch (stage) {
        case 1: case 2: return "no local player";
        case 3: case 4: return "network identity missing or not local";
        case 5: return "FPManager missing or dead";
        case 6: return "unknown weapon type";
        case 7: return "weapon backlink to player mismatch";
        case 8: return "weapon config not found";
        case 9: return "ballistic profile not found";
        case 10: return "projectile prefab field unreadable";
        case 11: return "ShaftedProjectile prefab not found";
        case 12: return "no ballistic targets on active weapon";
        case 13: case 15: return "ballistic values unreadable or out of range";
        case 14: return "weapon context changed mid-write";
        case 16: case 17: return "zero write not confirmed by readback";
        case 18: return "original values could not be restored";
        default: return "waiting for bind or player";
    }
}
uint64_t unity_base = 0;
using Clock = std::chrono::steady_clock;
Clock::time_point fps_last_tick{}, fps_last_resolve{};
// Поиск libunity читает и разбирает всю /proc/<pid>/maps — на слабом SoC это
// десятки миллисекунд. Раньше он шёл прямо в потоке отрисовки раз в секунду,
// пока лимитер ФПС не распознал libunity (неопознанная сборка движка — это
// навсегда): визуалы вставали на секунду каждую секунду. Теперь поиск идёт в
// фоновом потоке, а при неудаче повторяется с нарастающей паузой.
std::atomic<bool>     unity_job_running{false};
std::atomic<bool>     unity_job_ready{false};
std::atomic<uint64_t> unity_job_result{0};
std::atomic<uint64_t> unity_job_session{0};
Clock::time_point     unity_next_resolve{};
int                   unity_fail_streak = 0;
Clock::time_point weapon_last_tick{};
uint64_t weapon_last_player = 0;
unsigned weapon_last_flags = ~0u;
int fps_last_limit = 0;
viewmodel::Status fps_last_log = viewmodel::Status::Off;
// Build ID libunity, который не прошёл таблицу kFpsLayouts (0 — движок
// опознан или данных нет). Показывается в меню: без него вечное «ожидание»
// неотличимо от «игра ещё не на связи», а по самому ID новый лейаут
// добавляется в fps_memory.h без доступа к устройству.
uint64_t fps_unknown_build_id = 0;

uint64_t find_unity() {
    std::string text;
    if (!read_proc_maps(g_pid, text)) return 0;
    uint64_t candidates[8]{}, found = 0;
    int count = maps::lookup_library_bases(text, "libunity.so", candidates, 8);
    for (int i = 0; i < count; ++i) {
        if (!viewmodel::fps_layout(memory, candidates[i])) continue;
        if (found) return 0; // ambiguous live/stale mappings: do not guess
        found = candidates[i];
    }
    // Неопознанная версия движка (игра обновилась): запоминаем build ID
    // первого образа для меню и журнала. Не прочитался — 0, тогда в меню
    // останется просто «ожидание».
    fps_unknown_build_id = 0;
    // Повтор той же неопознанной сборки — не новость: в логе 11:52-11:54 эта
    // строка шла 92 раза за две минуты и топила всё остальное. Печатаем смену
    // build id или числа mapped-образов сразу, одинаковое — раз в 30 c.
    // mapped отличает «игра обновилась» от «карты памяти не читаются», а
    // непрочитанный id говорит, что лейаут добавлять ещё рано.
    static uint64_t last_logged_build = 0;
    static int last_logged_maps = -1;
    static Clock::time_point last_logged_at{};
    if (found) {
        last_logged_build = 0;   // следующая неопознанная сборка — сразу в лог
        last_logged_maps = -1;
    }
    if (!found && count > 0) {
        uint64_t id = 0;
        const bool id_read = memory.read(candidates[0] + 0x318, id) && id != 0;
        if (id_read) fps_unknown_build_id = id;
        const auto now_log = Clock::now();
        const bool changed = id != last_logged_build || count != last_logged_maps;
        if (changed || now_log - last_logged_at > std::chrono::seconds(30)) {
            last_logged_at = now_log;
            last_logged_build = id;
            last_logged_maps = count;
            if (id_read)
                ;
            else
                ;
        }
    }
    return found;
}
void start_unity_resolve(uint64_t session) {
    if (unity_job_running.exchange(true)) return;
    try {
        std::thread([session]() {
            const uint64_t base = find_unity();
            unity_job_result.store(base);
            unity_job_session.store(session);
            unity_job_ready.store(true);
            unity_job_running.store(false);
        }).detach();
    } catch (...) {
        unity_job_running.store(false);
    }
}

// Забрать результат фонового поиска. Результат от прошлой сессии (процесс
// сменился, пока читали карту) выбрасывается.
void collect_unity_resolve(uint64_t session, Clock::time_point now) {
    if (!unity_job_ready.exchange(false)) return;
    if (unity_job_session.load() != session) return;
    const uint64_t base = unity_job_result.load();
    unity_base = base;
    static const int kBackoffSec[] = {1, 2, 5, 10, 30};
    const bool bad = !base || fps_controller.status == viewmodel::Status::Failed;
    unity_fail_streak = bad ? (unity_fail_streak < 4 ? unity_fail_streak + 1 : 4) : 0;
    unity_next_resolve = now + std::chrono::seconds(kBackoffSec[bad ? unity_fail_streak : 0]);
}

void tick_fps(uint64_t session, int requested_limit) {
    const int limit = viewmodel::sanitize_fps_limit(requested_limit);
    const bool enabled = limit != 0;
    if (!enabled && !fps_controller.has_state()) { fps_last_limit = 0; return; }
    const auto now = Clock::now();
    if (limit == fps_last_limit && now - fps_last_tick < std::chrono::milliseconds(100)) return;
    const bool changed = limit != fps_last_limit;
    fps_last_tick = now; fps_last_limit = limit;
    // A libunity mapping can be replaced during a scene reload. Keeping the
    // old base after one failed validation leaves the FPS control permanently
    // stuck in "waiting/failed", because the resolver was only retried while
    // unity_base was zero. Re-resolve both before the first success and after
    // a failed controller tick — но в фоне и с нарастающей паузой (см. выше).
    collect_unity_resolve(session, now);
    const bool need_resolve = !unity_base || fps_controller.status == viewmodel::Status::Failed;
    if (enabled && need_resolve && now >= unity_next_resolve) {
        unity_next_resolve = now + std::chrono::seconds(1);   // пока идёт поиск, не стартуем второй
        start_unity_resolve(session);
    }
    fps_controller.tick(session, unity_base, limit);
    if (changed || fps_last_log != fps_controller.status) {
        fps_last_log = fps_controller.status;
    }
}
uint64_t epoch = 1;
viewmodel::Diagnostics last_log{};
}

std::recursive_mutex& viewmodel_session_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

void esp_viewmodel_reset(bool restore) {
    std::lock_guard<std::recursive_mutex> guard(viewmodel_session_mutex());
    controller.reset(restore && g_mem.bound());
    fps_controller.reset(restore && g_mem.bound());
    if (!weapons.reset(restore && g_mem.bound()))
        ;
    if (!rapid_fire.reset(restore && g_mem.bound()))
        ;
    if (!no_recoil_ctl.reset(restore && g_mem.bound()))
        ;
    if (!no_ballistics_ctl.reset(restore && g_mem.bound()))
        ;
    loot_speed.reset();
    unity_base = 0; fps_last_tick = {}; fps_last_resolve = {}; fps_last_limit = 0;
    unity_next_resolve = {}; unity_fail_streak = 0; unity_job_ready.store(false);
    weapon_last_tick = {}; weapon_last_player = 0; weapon_last_flags = ~0u;
    invalidate_maps_snapshot();
    ++epoch;
}

void esp_viewmodel_tick(const ViewmodelSettings& raw_settings) {
    ViewmodelSettings settings = raw_settings;
    settings.no_ballistics = false;
    using namespace go_active;
    std::lock_guard<std::recursive_mutex> guard(viewmodel_session_mutex());
    const bool want_fast_fire = g_state.fast_fire;
    if (!g_mem.bound() || !g_il2cpp_base || g_pid <= 0) {
        controller.reset(false); fps_controller.reset(false); weapons.reset(false);
        rapid_fire.reset(false);
        rapid_fire.status = want_fast_fire ? viewmodel::Status::Waiting : viewmodel::Status::Off;
        no_recoil_ctl.reset(false);
        no_recoil_ctl.status = settings.no_recoil_button ? viewmodel::Status::Waiting : viewmodel::Status::Off;
        no_ballistics_ctl.reset(false);
        no_ballistics_ctl.status = settings.no_ballistics ? viewmodel::Status::Waiting : viewmodel::Status::Off;
        const bool want_spread = settings.no_spread || esp_silent_wants_no_spread();
        weapons.spread = want_spread ? viewmodel::Status::Waiting : viewmodel::Status::Off;
        unity_base = 0; fps_last_resolve = {};
        unity_next_resolve = {}; unity_fail_streak = 0; unity_job_ready.store(false);
        controller.status = {settings.fov_enabled ? viewmodel::Status::Waiting : viewmodel::Status::Off,
                             settings.hands_enabled ? viewmodel::Status::Waiting : viewmodel::Status::Off};
        return;
    }
    tick_fps(epoch, settings.fps_limit);
    {
        // Быстрый подбор («Забрать все»): живёт в плеер-лупе UniTask и не
        // зависит от цепочки игрока, поэтому тикается до её резолва.
        static fast_loot::State loot_last_state = fast_loot::State::Off;
        static bool loot_announced = false;
        const int loot_zeroed = loot_speed.tick(g_il2cpp_base, settings.fast_loot);
        if (loot_speed.state() != loot_last_state) {
            loot_last_state = loot_speed.state();
        }
        if (loot_zeroed > 0 && !loot_announced) {
            loot_announced = true;
        }
        if (!settings.fast_loot) loot_announced = false;
    }
    const bool want_no_spread = settings.no_spread || esp_silent_wants_no_spread();
    // Use the local identity already validated by the world reader first. The
    // fixed TypeInfo RVA can lag behind a game update even when every managed
    // field offset is unchanged; that used to disable all four weapon toggles.
    uint64_t player = 0;
    static bool local_source_failed = false;
    if (settings.fov_enabled || settings.hands_enabled || controller.has_state() ||
        want_no_spread || weapons.has_state() ||
        want_fast_fire || rapid_fire.has_state() ||
        settings.no_recoil_button || no_recoil_ctl.has_state() ||
        settings.no_ballistics || no_ballistics_ctl.has_state() ||
        g_farmActive) {
        auto local_player_ok = [&](uint64_t candidate) {
            uint64_t net=0; uint8_t local=0;
            return candidate>=0x10000 && memory.is(candidate,"PlayerManager") &&
                memory.read(candidate+go_active::NETWORK_BEHAVIOUR_IDENTITY,net) &&
                heap_memory.named(memio::untag(net),"NetworkIdentity","Mirror") &&
                memory.read(memio::untag(net)+go_active::NETWORK_IDENTITY_IS_LOCAL_PLAYER,local) && local==1;
        };
        if (local_player_ok(g_local_player)) player=g_local_player;
        auto try_class = [&](uint64_t klass) {
            if (player || !klass || !memory.klass_is(klass,"GameControllerBase")) return;
            const uint64_t sf=get_class_static_fields(klass);
            uint64_t raw=0;
            if (!sf || !memory.read(sf+GAME_CONTROLLER_LOCAL_PLAYER_FIELD,raw)) return;
            raw=memio::untag(raw);
            if (local_player_ok(raw)) player=raw;
        };
        try_class(g_game_controller_class);
        try_class(memory.ptr(g_il2cpp_base+GAME_CONTROLLER_TYPEINFO_RVA));
        if (!player) {
            if (!local_source_failed) {
                local_source_failed=true;
            }
            // A failed read is NOT a confirmed scene change. Keep pending
            // originals so a later successful frame can still restore them.
            auto wait=controller.has_state()?viewmodel::Status::Failed:viewmodel::Status::Waiting;
            controller.status={wait,wait};
            weapons.spread=want_no_spread||weapons.has_state()?viewmodel::Status::Failed:viewmodel::Status::Off;
            rapid_fire.tick(epoch, g_il2cpp_base, 0, want_fast_fire);
            no_recoil_ctl.tick(epoch, g_il2cpp_base, 0, settings.no_recoil_button);
            no_ballistics_ctl.tick(epoch, g_il2cpp_base, 0, settings.no_ballistics);
            return;
        }
        local_source_failed=false;
    }
    // Weapon chains are expensive (class names, backlinks and readbacks for
    // every scalar). They only need a watchdog cadence, not the uncapped
    // overlay rate. Setting/player changes bypass the limiter immediately.
    const unsigned weapon_flags = (want_no_spread ? 1u : 0u) |
                                  (want_fast_fire ? 2u : 0u) |
                                  (settings.no_recoil_button ? 4u : 0u) |
                                  (settings.no_ballistics ? 8u : 0u);
    const auto weapon_now = Clock::now();
    const bool weapon_due = weapon_flags != weapon_last_flags ||
                            player != weapon_last_player ||
                            weapon_now - weapon_last_tick >= std::chrono::milliseconds(100);
    if (weapon_due) {
        weapon_last_tick = weapon_now;
        weapon_last_player = player;
        weapon_last_flags = weapon_flags;
        weapons.tick(epoch, g_il2cpp_base, player, want_no_spread);
        rapid_fire.tick(epoch, g_il2cpp_base, player, want_fast_fire);
        no_recoil_ctl.tick(epoch, g_il2cpp_base, player, settings.no_recoil_button);
        no_ballistics_ctl.tick(epoch, g_il2cpp_base, player, settings.no_ballistics);
    }
    {
        // Log no-spread resolution changes only.
        static int last_stage = -1;
        const int stage = want_no_spread ? weapons.stage() : 0;
        if (stage != last_stage) {
            last_stage = stage;
            if (stage)
                ;
            else
                ;
        }
    }
    // Magic-Farm keeps the verified normal hit path (and therefore server-side
    // harvesting/cooldowns), but hides every non-ADS first-person tool pose
    // while mining. To the player only the crosshair and hit-streak marker are
    // visible; there is no local swing animation. Walking and aiming remain the
    // existing touch controller, and the user's own hands setting is restored
    // immediately outside the mining phase.
    ViewmodelSettings visible_settings = settings;
    if (g_farmActive && !g_farmPaused && g_farmPhase == 3) {
        visible_settings.hands_enabled = true;
        visible_settings.x = 0.f;
        visible_settings.y = -50.f;
        visible_settings.z = 0.f;
    }
    controller.tick(epoch, player, visible_settings);
    auto st = esp_viewmodel_diagnostics();
    if (st.fov != last_log.fov || st.hands != last_log.hands ||
        st.no_spread != last_log.no_spread) {
        last_log = st;
    }
}

viewmodel::Diagnostics esp_viewmodel_diagnostics() {
    std::lock_guard<std::recursive_mutex> guard(viewmodel_session_mutex());
    auto status = controller.status;
    status.no_spread = weapons.spread;
    status.fast_fire = rapid_fire.status;
    return status;
}

viewmodel::FpsUiInfo esp_viewmodel_fps_ui() {
    std::lock_guard<std::recursive_mutex> guard(viewmodel_session_mutex());
    viewmodel::FpsUiInfo info;
    info.requested = fps_last_limit;
    info.status = fps_controller.status;
    info.field_value = fps_controller.field_seen();
    info.pace_kind = fps_controller.pace_seen();
    info.build_id = fps_unknown_build_id;
    return info;
}
