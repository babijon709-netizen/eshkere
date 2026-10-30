#include "farm.h"
#include "attach_log.h"
#include "feature_log_gate.h"
#include "farm_joy.h"          // память-джойстик: движение без тача
#include "farm_control_core.h"
#include "cfg.h"
#include "main.h"
#include "game.h"
#include "Android_touch/TouchHelperA.h"
#include "app_state.h"
#include "ui_util.h"
#include "aim.h"                 // g_aimActive, s_fingerDown (владелец камеры)
#include "esp_draw.h"            // FrameBoxes
#include "menu.h"                // g_calibMode
#include "process.h"             // g_esp_attached
#include "runtime_guard.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

// Главный принцип: КАМЕРА НАВОДИТСЯ РОВНО В КРЕСТИК, И УДАРЫ ИДУТ ПО НЕМУ.
// Крестик (hit-streak marker) читается напрямую из памяти игры: у руды это
// трансформ живого маркера OreHitstreaks.MoW, у дерева — локальный отрезок
// TreeHitstreaks.MTQ..MTu (серия засчитывается по дистанции от точки удара до
// маркера/отрезка с радиусом 15 см). Попадание в крестик даёт бонусный ресурс,
// поэтому бот ведёт взгляд (накопитель MouseLook) точно в крестик: центр экрана
// и есть точка удара штатного луча игры, так что удар ложится в X сам, а
// расхождение «точка удара -> крестик» остаётся в сантиметрах — заведомо внутри
// этих 15 см. Допуск наведения считаем от дистанции (kSpotAimM), а удары
// открываем только когда камера уже стоит в нём. Подмены удара нет: игра бьёт
// своим лучом по своему узлу.
//
// Прежний механизм (ПИН КРЕСТИКА: фоновый поток телепортировал маркер в точку
// падения удара, а камера держала центр корпуса) больше не используется —
// запись трансформа маркера на живой сборке перестала проходить. Код пина в
// game.cpp цел, но автофарм его не включает; наведение делает ту же работу без
// записи в маркеры. Пока крестика нет, камера ведёт ЦЕНТР КОРПУСА узла: узел
// большой, луч по нему попадает, а первое попадание и создаёт X.
//
// Крестик существует не всегда: игра создаёт его при первом же попадании по
// узлу и гасит через 15 секунд простоя — цикл всегда один и тот же: удар по
// корпусу -> появился X -> дальше камера ведёт X и все удары идут по нему.
//
// Удары отправляются штатным вводом и проходят все игровые проверки
// дистанции/кулдауна по настоящему лучу игры; game_viewmodel скрывает
// локальные позы инструмента на фазе MINE. Сетевую механику попадания
// не подменяем — двигается только маркер бонусной серии.
//
// Фазы кадра:
//   WALK  — идём к точке подхода С ПЕРВОГО ЖЕ кадра: стик давит прямо в
//           сторону цели относительно камеры (полный аналог, 360°), камера
//           доворачивается на узел параллельно, ходьба её не ждёт;
//   MINE  — камера ведёт крестик (до его появления — центр корпуса);
//   узел добыт / потерян / недостижим — берём следующий (чёрный список).
//
// Удар по посторонней геометрии (постройка!) запрещён на нескольких уровнях:
// тап уходит только когда луч игры подтверждённо стоит в САМОМ узле, ритм
// никогда не обгоняет кулдаун орудия (очередь тапов игры выстреливает в
// уведённую камеру), а мёртвый узел снимается с цели до того, как его
// адреса успеет занять чужой объект (см. проверку живости в game_farm.cpp).

bool g_farmActive = false;   // статус для строки в окне автофарма
int  g_farmPhase = 0;        // 0 простой, 2 подход, 3 удар (1 — поворот — больше не используется)
int  g_farmNodes = 0;        // узлов насчитал последний скан реестра
int  g_farmReason = 1;       // причина простоя из esp_farm_debug()
float g_farmTgtDist = 0.f;   // метры до точки прицела
float g_farmReach = 0.f;     // FPMelee.m_MaxReach + hitRadius орудия в руках, м
int  g_farmTgtKind = 0;      // ресурс текущей цели
int  g_farmSpot = 0;         // 0 — крестика нет, иначе его источник (1 руда, 2/3 дерево)
bool g_farmPaused = false;   // Target is visible, but aim or calibration owns input.
int  g_farmStreak = 0;       // hitstreakIndex: сколько подряд попали в крестик
float g_farmHpPct = -1.f;    // здоровье узла в процентах (-1 = не читается)
float g_farmSpotLife = -1.f; // сколько секунд осталось жить крестику (-1 = неизвестно)
float g_farmAttackPeriod = 0.f; // секунд между ударами орудия (0 = неизвестно)
int  g_farmToolHave = 0;     // флаги ToolPurpose орудия в руках (0 = неизвестно)
int  g_farmToolNeed = 0;     // какие умения запросили отброшенные узлы
int  g_farmXp = 0;           // сколько опыта даёт текущий узел (0 = неизвестно)
bool g_farmBlocked = false;  // узел перекрыт: луч игры упёрся ближе точки прицела
bool g_farmWriteBlocked = false; // No complete maps snapshot for the current target PID.



namespace {

// Дальность удара, если орудие в руках не опознано / значение не прочиталось.
// Горизонтальные метры до ТОЧКИ ПРИЦЕЛА; подбиралось на устройстве: топор
// дотягивается до коры примерно с 0.8 м, а по камню удар проходит и с полутора —
// пивот камня часто зарыт, и крестик стоит на склоне в стороне от него.
// *Hold — гистерезис: подошли один раз, не дёргаемся.
constexpr float kReachTreeSpot = 0.80f;
constexpr float kReachOreSpot  = 1.55f;
constexpr float kReachTreeHold = 1.00f;
constexpr float kReachOreHold  = 1.80f;
// Пока крестика нет, подходим к корпусу узла — он большой, этого достаточно,
// чтобы первый удар создал X.
constexpr float kReachTreeBody = 2.60f;
constexpr float kReachOreBody  = 2.40f;

// Живая дальность орудия (tgt.melee_reach = FPMelee.m_MaxReach + hitRadius,
// читается из памяти). Игра засчитывает удар строго внутри этой дистанции,
// меряя 3D-метры ОТ ГЛАЗА до точки попадания (FPMelee.ZkX:
// `if (RaycastHit.distance < m_MaxReach + hitRadius) On_Hit else On_Woosh`),
// поэтому с ней работаем в aim_3d, а не по горизонтали. Запас kReachLiveTight —
// на подачу вперёд за время тапа; kReachLiveHold держит узел в работе до самого
// порога, но не за ним (иначе удары свистят впустую).
constexpr float kReachLiveTight = 0.90f;
constexpr float kReachLiveHold  = 0.98f;
// По корпусу (крестика ещё нет) точка прицела стоит в глубине узла — пивот
// дерева в центре ствола, у камня бывает зарыт, — поэтому к дальности удара
// добавляем запас на радиус узла: первому удару достаточно создать X.
constexpr float kReachBodyAllowance = 1.20f;
// Меньше этого живая дальность недостоверна (в конструкторе FPMelee заглушки
// 0.5/0.1 — префаб их перезаписывает, но и префаб может не успеть примениться).
constexpr float kReachLiveMin = 0.70f;
// Пока у игры нет подтверждённого луча, замах запрещён: сбой чтения
// не доказывает, что впереди нет чужой постройки. Поджимаемся к узлу и
// слегка двигаем камеру, чтобы игра пересчитала луч; watchdog ограничивает
// время ожидания.
constexpr float kNoRayCloseTo    = 0.50f;
constexpr float kRayMissNudgeAfter = 0.30f;
constexpr float kRayMissNudgeEvery = 0.50f;
constexpr float kRayMissNudgeDeg   = 0.40f;

// Допуск наведения камеры, градусы. Камера ведёт ту же точку, по которой бьёт:
// крестик, когда он есть (tgt.look_spot), иначе центр корпуса узла. Корпусу
// хватает широкого допуска — он большой; крестик это 15 см, которыми игра
// меряет серию, поэтому допуск по нему считаем от дистанции (kSpotAimM ниже).
constexpr float kAimBodyStart = 4.00f;
constexpr float kAimBodyStop  = 1.50f;
constexpr float kAimWalkStart = 6.00f;   // на подходе правим только заметный увод
constexpr float kAimWalkStop  = 2.50f;   // (камеру ведём сразу: стик больше не
                                         //  ждёт доворота, см. фазу WALK)
constexpr float kPitchDeadWalk = 18.0f;  // и только сильно задранную камеру

// Допуски наведения по крестику — в МЕТРАХ промаха на текущей дистанции, а
// контроллер переводит их в градусы (см. offset_deg в кадре). Игра засчитывает
// серию, если её луч попал не дальше 0.15 м от крестика.
//   kSpotAimM  — докудa доводим прицел: 2 см, то есть ЦЕНТР крестика, а не
//                «примерно в него». Прежний допуск на 1.5 м давал до 9 см мимо
//                центра по yaw, а по pitch ошибку до 6° вообще съедала мёртвая
//                зона — крестик стоял выше/ниже центра экрана.
//   kSpotSwingM — при каком промахе уже можно бить: 5 см (втрое внутри зоны
//                серии), чтобы не ждать последних сантиметров доводки.
// Ниже предела разрешения не опускаемся: мельче kSpotAimMin/kSpotSwingMin не
// довести — это шаг записи/чтения накопителя (kMemDeadDeg/kMemAckEps). Выше
// kSpotTolMax — это уже «по площади», а не по крестику.
constexpr float kSpotAimM    = 0.02f;    // м: докудa доводим прицел
constexpr float kSpotAimMin  = 0.20f;    // град: предел разрешения доводки
constexpr float kSpotSwingM  = 0.05f;    // м: допуск удара по X
constexpr float kSpotSwingMin= 0.30f;    // град
constexpr float kSpotTolMax  = 6.00f;    // град: выше — доводка «по площади»
// Сколько ждём доводку в допуск, прежде чем махнуть рукой на крестик: стоим в
// упор, а камера не садится (запись в накопитель не проходит). Тогда бьём по
// корпусу — урон по узлу идёт, серия просто не растёт; иначе watchdog сдал бы
// живой узел в чёрный список.
constexpr float kSpotStuckTime = 1.20f;  // с

// Замах по корпусу (крестика ещё нет) — только когда камера реально смотрит на
// узел: удар уходит по лучу игры, и тот должен попасть в узел. Допуск широкий:
// узел большой, а точку попадания задаёт сам луч.
constexpr float kSwingBodyYaw   = 8.00f;

constexpr float kArriveWalk     = 0.15f; // дошли до точки подхода
constexpr float kWalkOffHold    = 0.25f; // гистерезис отпускания джойстика, с

// Ритм ударов, когда ритм орудия не прочитался из памяти. Безопасный такт
// 600 мс: держим кнопку атаки в памяти ~60 мс и отпускаем. Прежний повод
// держаться медленнее кулдауна — очередь тапов, которую игра разгребала
// позже, уже в уведённую камеру, — с памятью пропал: преждевременный Try()
// игра просто отбрасывает. Такт оставлен как есть, чтобы не долбить ввод
// каждый кадр и не менять привычное поведение бота.
constexpr float kSwingHoldMs = 60.f;     // зажатие атаки (память),
constexpr float kSwingIdleMs = 540.f;    // пауза до следующего (600 мс цикл)

constexpr float kSettleTime    = 0.08f;  // пауза при смене узла (пальцы подняты)
constexpr float kLostHold      = 0.35f;  // цель может мигнуть на рескане реестра
// Смена узла тоже дебаунсится: новый обязан продержаться «лучшим» несколько
// кадров подряд. Лог 14.09.2026 — узел в 20 м перехватывал цель ровно на один
// кадр 9 раз за 150 с, и каждая смена в обе стороны стоила 0.7 с паузы
// (kSettleTime): 854 кадра простоя. Если цель так и прыгает туда-сюда дольше
// kFlickerGiveUp, переключаемся по-настоящему — иначе бот застыл бы навсегда.
constexpr int   kNodeDebounce  = 2;      // кадров подряд на новом узле
constexpr float kFlickerGiveUp = 1.0f;   // с прыжков — больше не держимся
constexpr float kStuckTime     = 2.20f;  // нет продвижения к точке подхода
// После смены узла бот сначала доворачивается на новую цель и только потом
// идёт. Без этого он с первого кадра давил вперёд по прежнему курсу — у
// срубленного дерева это ровно пень, — упирался, и до kStuckTime копился
// «нет продвижения»: со стороны выглядело как бег вокруг добытого места.
constexpr float kFreshTurnTime  = 0.30f;  // с: доворот на новую цель
constexpr float kFreshTurnYaw   = 12.0f;  // град: с этого угла уже идём
constexpr float kEvadeTime     = 1.70f;  // ~0.6 назад + ~1.1 вбок
constexpr int   kEvadeMax      = 4;
constexpr float kBlockedTime   = 0.30f;  // узел перекрыт дольше — пробуем обойти
// Сколько линия удара должна быть чистой подряд, чтобы простить счётчик обходов
// перекрытого узла. Мгновенное прощение (как было) означало бесконечный бюджет:
// «перекрыт» мигает кадр-другой, счётчик обнуляется, обход начинается заново с
// той же стороны — и бот вечно кружит вокруг узла (лог 14.09, камень 86a3c0:
// два «обход #1 (вправо)» подряд и ни одного удара).
constexpr float kBlockedClear  = 1.20f;
constexpr float kGiveUpDrain   = 12.f;   // бьём, а остаток не падает
constexpr float kGiveUpBlind   = 25.f;   // то же, но остаток не читается

// Камера ведётся через накопитель MouseLook (как мемори-аим): пишем абсолютный
// угол, игра разворачивает камеру в этом же кадре. Следующий шаг шлём только
// после того, как накопитель вернул записанное (игра приняла), либо истёк таймаут.
// Так шаги не складываются и камера не перелетает узел.
constexpr float kLookWaitMax   = 0.025f; // с: ждём не дольше одного-двух кадров игры
constexpr float kMemAckEps     = 0.10f;  // град: допуск «накопитель = наша запись»
constexpr float kMemStepWalk   = 42.0f;  // град: предел одного шага на подходе
constexpr float kMemStepBody   = 4.0f;   // град: предел шага, когда узел в прицеле
constexpr float kMemDeadDeg    = 0.05f;  // град: мельче — не шлём
// Шаг доводки. Следующую запись шлём только после того, как поза камеры показала
// предыдущую (см. «отклик позы» в кадре ниже): тогда задержка отклика не
// накапливается и раскачки нет ни при какой доле шага. Крупную ошибку (крестик
// только появился, камера стояла на корпусе) закрываем ОДНИМ движением — мелкие
// шаги подряд как раз и выглядели болтанкой «вверх-вниз, потом встал».
constexpr float kAimStepFine   = 0.60f;  // доля остатка в мелком шаге
constexpr float kAimJumpGate   = 2.00f;  // град: крупнее — закрываем одним движением
constexpr float kAimJumpGain   = 1.00f;  // ...во весь остаток
constexpr float kAimJumpMax    = 50.0f;  // град: предел одного такого движения
                                         // (крестик появляется, когда камера ещё
                                         // стоит на корпусе — закрываем одним
                                         // движением, а не серией шагов; перелёта
                                         // нет: следующий шаг ждёт ответа позы)
constexpr float kLookRespDeg   = 0.06f;  // град: «поза показала нашу запись»
constexpr float kPitchDeadSpot = 0.50f;  // град: субградусный pitch в упор не доводим

} // namespace

namespace {
struct FarmTrace {
    int event=0;
    uint64_t target=0, attacks=0, attack_ok=0;
    bool have=false, ray_self=false, ray_foreign=false;
    bool last_attack_on=false, last_attack_ok=false;
    float distance=0.f, reach=0.f, period=0.f;
} farm_trace;
const char* farm_event_name(int event) {
    switch(event) {
    case 1:return "off/no-resources";case 2:return "maps-not-ready";
    case 3:return "not-attached";case 4:return "no-target";
    default:return "controller";
    }
}
struct FarmLogOnExit {
    ~FarmLogOnExit() {
        static feature_log::Gate gate;
        const uint64_t key=(uint64_t(farm_trace.event) | (uint64_t(g_farmPhase)<<8) |
            (uint64_t(g_farmPaused)<<12) |
            (uint64_t(g_state.farm_on)<<14)) ^ (farm_trace.target<<20);
        if(!gate.accept(key,std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),g_state.farm_on)) return;
        // guard: 0 unknown, 1 clear, 2 protected. bypass — записи, которым
        // снимок карты не нужен: отображение подтвердил сам ядерный транспорт
        // (на таких клиентах /proc/<pid>/maps не читается вовсе). По этим двум
        // числам видно, ворота это или логика фарма.
        attachlog::say("[farm] on=%d event=%s active=%d phase=%d paused=%d nodes=%d idle_reason=%d write_blocked=%d guard=%d bypass=%llu blocked=%llu backend=%d",
            int(g_state.farm_on),farm_event_name(farm_trace.event),
            int(g_farmActive),g_farmPhase,int(g_farmPaused),g_farmNodes,g_farmReason,int(g_farmWriteBlocked),
            esp_write_guard_state(),esp_write_guard_bypasses(),esp_write_guard_blocks(),
            esp_get_mem_backend());
        if(g_state.farm_on)
            attachlog::say("[farm.target] valid=%d id=0x%llx kind=%d distance=%.3f reach=%.3f ray_self=%d ray_foreign=%d period=%.3f hp=%.1f attack_ok=%llu attempts=%llu last_attack_on=%d last_attack_ok=%d aim_owns=%d calibration=%d",
                int(farm_trace.have),(unsigned long long)farm_trace.target,g_farmTgtKind,
                double(farm_trace.distance),double(farm_trace.reach),int(farm_trace.ray_self),int(farm_trace.ray_foreign),
                double(farm_trace.period),double(g_farmHpPct),(unsigned long long)farm_trace.attack_ok,
                (unsigned long long)farm_trace.attacks,int(farm_trace.last_attack_on),int(farm_trace.last_attack_ok),int(g_aimActive),g_calibMode);
    }
};
}

static void UpdateFarmInner(float dt);

// Снимок /proc/<pid>/maps (нужен runtime_guard для права записи там, где
// транспорт не проверяет отображение сам) читает карту
// игры ЦЕЛИКОМ: у Unity это тысячи отображений и сотни килобайт. Раньше это
// делалось прямо здесь, раз в секунду, на потоке кадра оверлея — то есть ровно
// в тот момент, когда ESP/фрикам/иксрей должны были отдать следующий кадр.
// Работа фоновая и не срочная: держим её в своём потоке, а кадр только читает
// уже опубликованную политику (attach-путь публикует её и сам, см.
// get_base_candidates/observe_maps).
static void EnsureRuntimeGuardWorker() {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    std::thread([] {
        for (;;) {
            const pid_t pid = g_target_pid;
            if (pid > 0) runtime_guard::refresh_proc(pid);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();
}

// Ворота записи: то же правило, что у g_mem.write (esp_writes_permitted), но с
// починкой на месте. Снимок /proc/<pid>/maps публикуют ещё attach-путь и
// воркер кучи, и между публикацией и проверкой мог пройти до секунды; один
// отказ чтения карты успевал поставить фарм на паузу, сбросить фазу и отпустить
// стик — снаружи это «автофарм не добывает». Поэтому при отказе снимок
// перечитывается прямо в этом кадре (не чаще четырёх раз в секунду) и решение
// принимается по свежим данным.
static bool FarmWritesAllowed() {
    if (esp_writes_permitted()) return true;
    if (g_target_pid <= 0 || !g_esp_attached) return false;
    static pid_t  s_pid  = -1;
    static double s_next = 0.0;
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (g_target_pid == s_pid && now < s_next) return false;
    s_pid  = g_target_pid;
    s_next = now + 0.25;
    runtime_guard::refresh_proc(g_target_pid);
    return esp_writes_permitted();
}

void UpdateFarm(float dt) {
    FarmLogOnExit farm_log_on_exit;
    farm_trace.event=0;farm_trace.target=0;farm_trace.have=false;
    farm_trace.distance=farm_trace.reach=farm_trace.period=0.f;
    farm_trace.ray_self=farm_trace.ray_foreign=false;
#ifdef __ANDROID__
    EnsureRuntimeGuardWorker();
#endif
    if (!FarmWritesAllowed()) {
        if (!g_farmWriteBlocked) {
            // Release the normal controller's cached press/movement state once.
            const bool enabled = g_state.farm_on;
            g_state.farm_on = false;
            UpdateFarmInner(dt);
            g_state.farm_on = enabled;
        }
        farmjoy::release();
        g_farmActive = false; g_farmPaused = true; g_farmPhase = 0;
        g_farmWriteBlocked = true;farm_trace.event=2;
        return;
    }
    g_farmWriteBlocked = false;
    UpdateFarmInner(dt);
}

static void UpdateFarmInner(float dt) {
    static bool  s_moveDown = false;  // стик движения ведём (память, не тач)
    static bool  s_camTurn = false;   // камера ведётся памятью: идёт доводка
    static farm_control::Swing s_swing; // безопасный импульс и повтор записи
    static float s_memWX = 0.f, s_memWY = 0.f; // последняя запись в накопитель
    static bool  s_memPend = false;   // запись ждёт подтверждения накопителем
    static float s_memWait = 0.f;     // сколько ждём подтверждения записи, с
    static unsigned long long s_nodeId = 0;
    static float s_settle = 0.f;      // пауза между целями (пальцы подняты)
    static float s_lostTime = 0.f;    // цель пропала: ждём, вдруг вернётся
    static float s_mineTime = 0.f;    // сколько бьём этот узел без прогресса
    static float s_fracStart = -1.f;  // остаток на входе (для контроля добычи)
    static float s_healthStart = -1.f;// здоровье узла на входе (m_CurrentHealth)
    static float s_blockedTime = 0.f; // сколько подряд узел перекрыт
    static float s_clearTime = 0.f;   // сколько линия удара чистая (дебаунс прощения обходов)
    static int   s_bestStreak = 0;    // максимум серии по крестику на этом узле
    static float s_lastWalk = 1e9f;   // лучшая дистанция до точки подхода
    static float s_stuckTime = 0.f;
    static float s_freshTurn = 0.f;   // остаток доворота на новую цель
    static float s_evadeTime = 0.f;   // > 0: идёт манёвр обхода препятствия
    static float s_evadeDir = 1.f;    // +1 вправо, -1 влево
    static int   s_evadeCount = 0;
    static int   s_evadeWhy = 0;      // 0 нет, 1 застрял на подходе, 2 узел перекрыт
    static float s_walkOffTime = 0.f; // гистерезис отпускания джойстика
    static float s_joyX = 0.f, s_joyY = 0.f;         // сглаженный вектор стика
    static float s_rayMissTime = 0.f; // луч валиден, но никуда не попал, с
    static float s_nudgeTimer = 0.f;  // до следующего качка камеры, с
    static float s_nudgeSign = 1.f;   // направление качка (чередуется)
    static unsigned long long s_candId = 0; // узел-кандидат на смену цели
    static int   s_candFrames = 0;    // сколько кадров подряд он лучший
    static float s_flickerTime = 0.f; // сколько держимся прежнего, с
    static float s_spotAimStuck = 0.f;// сколько не можем довестись в допуск по X
    static float s_lookErrY = 0.f, s_lookErrP = 0.f;
    static bool  s_lookWait = false;
    static float s_lookWaitT = 0.f;
    static bool  s_coarseArmed = true;

    const auto writeAttack = [](bool on) {
        ++farm_trace.attacks;
        const bool ok=farmjoy::attack(on);
        farm_trace.last_attack_on=on;farm_trace.last_attack_ok=ok;
        if(ok) ++farm_trace.attack_ok;
        return ok;
    };
    auto releaseAll = [&]() {
        farmjoy::release();
        s_moveDown = false;
        s_joyX = 0.f; s_joyY = 0.f;
        s_camTurn = false; s_memPend = false;
        s_swing.stop(writeAttack);
        s_memWait = 0.f;
        s_lookWait = false; s_lookWaitT = 0.f;
        s_coarseArmed = true;
    };
    auto resetNode = [&]() {
        s_nodeId = 0;
        s_joyX = 0.f; s_joyY = 0.f;
        s_mineTime = 0.f; s_fracStart = -1.f; s_bestStreak = 0;
        s_healthStart = -1.f; s_blockedTime = 0.f; s_clearTime = 0.f;
        s_lastWalk = 1e9f; s_stuckTime = 0.f; s_freshTurn = 0.f;
        s_evadeTime = 0.f; s_evadeCount = 0; s_evadeWhy = 0;
        s_walkOffTime = 0.f;
        s_rayMissTime = 0.f; s_nudgeTimer = 0.f;
        s_candId = 0; s_candFrames = 0; s_flickerTime = 0.f;
        s_spotAimStuck = 0.f;
        s_lookWait = false; s_lookWaitT = 0.f;
        s_coarseArmed = true;
    };

    if (dt <= 0.f || !std::isfinite(dt)) dt = 1.f / 60.f;
    if (dt > 0.1f) dt = 0.1f;
    // Память-джойстик: кэш цели движения и его периодическая перепроверка.
    farmjoy::tick(dt);

    unsigned mask = 0;
    if (g_state.farm_on) {
        if (g_state.farm_wood)   mask |= 1u;
        if (g_state.farm_stone)  mask |= 2u;
        if (g_state.farm_metal)  mask |= 4u;
        if (g_state.farm_sulfur) mask |= 8u;
    }
    esp_farm_set_resources(mask);
    esp_farm_set_range(g_state.farm_range);

    if (mask == 0 || !g_esp_attached) {
        farm_trace.event=mask==0?1:3;
        releaseAll();
        g_farmActive = false; g_farmPhase = 0; g_farmPaused = false;
        g_farmHpPct = -1.f; g_farmSpotLife = -1.f; g_farmBlocked = false;
        g_farmSpot = 0; g_farmStreak = 0;
        resetNode(); s_settle = 0.f; s_lostTime = 0.f;
        return;
    }
    // Aim owns the camera while tracking a player, and calibration owns the
    // physical touch used to select a control zone. Menu and popover windows
    // do not pause farming, so opening its status/settings cannot stop it.
    const bool driving = !s_fingerDown && !g_aimActive && g_calibMode == 0;

    float sw = (float)native_window_screen_x;
    float sh = (float)native_window_screen_y;
    if (displayInfo.width > displayInfo.height && displayInfo.width >= 100 && displayInfo.height >= 100) {
        sw = (float)displayInfo.width;  sh = (float)displayInfo.height;
    } else if (displayInfo.height > displayInfo.width && displayInfo.height >= 100 && displayInfo.width >= 100) {
        sw = (float)displayInfo.height; sh = (float)displayInfo.width;
    }
    if (sw < 100.f || sh < 100.f) {
        g_farmActive = false;
        releaseAll(); return;
    }

    // Цель меряется относительно состояния камеры, которое публикует
    // esp_get_boxes(); убедимся, что снимок этого кадра есть, даже если ни
    // ESP-оверлей, ни аимбот его в этом кадре не запрашивали.
    FrameBoxes(sw, sh);

    FarmTarget tgt;
    // Пока реально добываем живой узел, удерживаем его даже если другой стал
    // чуть ближе. При смерти/blacklist подборщик возьмёт следующего.
    const unsigned long long preferred = (g_farmPhase == 3 || s_mineTime > 0.f) ? s_nodeId : 0;
    const bool haveTgt = esp_farm_get_target(tgt, preferred) && tgt.valid;
    esp_farm_debug(g_farmNodes, g_farmReason);
    farm_trace.have=haveTgt;farm_trace.target=tgt.id;
    farm_trace.distance=tgt.aim_3d;farm_trace.reach=tgt.melee_reach;
    farm_trace.period=tgt.attack_period;farm_trace.ray_self=tgt.ray_self;farm_trace.ray_foreign=tgt.ray_foreign;
    if (!haveTgt) {
        farm_trace.event=4;
        // Рабочая цель может исчезнуть на пару кадров (рескан реестра, сбой
        // чтения посреди апдейта) и тут же вернуться. Мгновенный сброс всего
        // давал топтание «стоп-шаг-стоп»: на короткое время просто замораживаем
        // ввод, а по-настоящему сбрасываемся, только если цель не вернулась.
        if (s_nodeId != 0 && s_lostTime < kLostHold) {
            s_lostTime += dt;
            s_swing.stop(writeAttack); // вслепую не машем
            if (s_moveDown) { farmjoy::release(); s_joyX = 0.f; s_joyY = 0.f; s_moveDown = false; }
            s_evadeTime = 0.f;
            s_camTurn = false; s_memPend = false; s_memWait = 0.f;
            return;
        }
        g_farmActive = false;
        releaseAll();
        g_farmPhase = 0; g_farmPaused = false;
        g_farmHpPct = -1.f; g_farmSpotLife = -1.f; g_farmBlocked = false;
        g_farmSpot = 0; g_farmStreak = 0;
        resetNode(); s_settle = 0.f; s_lostTime = 0.f;
        return;
    }
    s_lostTime = 0.f;
    g_farmActive = true;
    g_farmPaused = !driving;
    g_farmTgtDist = tgt.aim_dist;
    g_farmReach = tgt.melee_reach;
    g_farmTgtKind = tgt.kind;
    g_farmSpot    = tgt.has_spot ? tgt.spot_source : (tgt.ext_found ? 0 : -1);
    g_farmStreak  = tgt.streak;
    g_farmHpPct   = (tgt.node_health >= 0.f && tgt.node_health_max > 0.f)
                  ? 100.f * tgt.node_health / tgt.node_health_max : -1.f;
    g_farmSpotLife = tgt.has_spot ? tgt.spot_life : -1.f;
    g_farmAttackPeriod = tgt.attack_period;
    g_farmXp = tgt.node_experience;
    g_farmBlocked = tgt.ray_blocked;
    {   // умения орудия: своё поле у FPTool, у узлов — своё требование
        int have = 0, need = 0;
        esp_farm_tool_info(have, need);
        g_farmToolHave = have ? have : tgt.tool_purposes;
        g_farmToolNeed = need;
    }

    // Есть ли у игры данные рейкаста прицела. FPMelee.ZkX берёт distance из
    // GKo (RaycastData/AimRaycast); пока обёртка пуста, удар не засчитывается
    // вовсе — играет один On_Woosh. Лог 14.09.2026: 43 из 48 замахов без урона
    // шли вообще без луча, а из 39 результативных луч был в 37. Отсутствие
    // луча не привязано к крестику: по корпусу узла луч нужен ровно так же.
    const bool noRay = !tgt.ray_valid;
    const bool rayMiss = tgt.ray_valid && !tgt.ray_self && !tgt.ray_foreign && !tgt.ray_blocked;
    s_rayMissTime = rayMiss ? (s_rayMissTime + dt) : 0.f;

    if (!driving) {
        // Меню открыто / камеру ведёт аимбот / идёт калибровка зон: статус
        // живой, ввод стоит. Таймеры подхода и добычи не крутятся, иначе бот
        // «застревал» и сдавался на узле за то время, пока пользователь
        // смотрел в настройки.
        releaseAll();
        g_farmPhase = 0;
        s_walkOffTime = 0.f;
        s_camTurn = false; s_memPend = false; s_memWait = 0.f;
        return;
    }

    if (tgt.id == s_nodeId) {
        // Цель на месте — кандидат сбрасывается: считать надо кадры ПОДРЯД,
        // иначе редкие однокадровые мигания на один и тот же чужой узел
        // накопились бы и переключение всё равно случилось (проверено стендом:
        // на 3-м мигании s_candFrames дорастал до kNodeDebounce).
        s_candId = 0; s_candFrames = 0; s_flickerTime = 0.f;
    } else {
        // Дебаунс: один кадр на новом узле — это ещё не смена цели (рескан
        // реестра легко роняет рабочую цель на кадр). Держим прежний узел тем
        // же способом, что и при пропаже цели: ввод заморожен, ничего не
        // сбрасывается, иначе каждое мигание стоило бы 0.7 с паузы.
        if (tgt.id == s_candId) ++s_candFrames;
        else { s_candId = tgt.id; s_candFrames = 1; s_flickerTime = 0.f; }
        if (s_nodeId != 0 &&
            s_candFrames < kNodeDebounce && s_flickerTime < kFlickerGiveUp) {
            s_flickerTime += dt;
            s_swing.stop(writeAttack);   // вслепую не машем
            if (s_evadeTime > 0.f && s_moveDown) {
                farmjoy::release(); s_joyX = 0.f; s_joyY = 0.f; s_moveDown = false;
                s_evadeTime = 0.f;
            }
            s_camTurn = false; s_memPend = false; s_memWait = 0.f;
            return;
        }
        s_candId = 0; s_candFrames = 0; s_flickerTime = 0.f;
        // Смена узла: поднять все пальцы и постоять. Иначе старые вводные
        // движения/камеры ещё несколько кадров проигрываются против новой цели
        // — та самая бешеная тряска сразу после того, как узел добыт.
        const bool hadNode = s_nodeId != 0;
        resetNode();
        s_nodeId = tgt.id;
        s_lastWalk = tgt.walk_dist;
        s_fracStart = tgt.fraction;
        s_healthStart = tgt.node_health;
        s_blockedTime = 0.f;
        if (hadNode) { releaseAll(); s_settle = kSettleTime; }
        s_freshTurn = kFreshTurnTime;
    }

    // Только подтверждённый нулевой HP является признаком добычи. Маленькая
    // доля и пропажа луча могут встречаться у ещё живого узла.
    if (farm_control::health_depleted(tgt.node_health >= 0.f, tgt.node_health)) {
        esp_farm_blacklist(tgt.id, 6.f);
        releaseAll();
        resetNode();
        s_settle = 0.f;
        s_lostTime = kLostHold;
        g_farmPhase = 0;
        return;
    }

    // Пауза между целями: пальцы подняты, камера стоит, следующая цель
    // начинается с чистого листа.
    if (s_settle > 0.f) {
        s_settle -= dt;
        releaseAll();
        return;
    }

    // Экранная метка ровно на той точке, по которой бот бьёт: зелёная — по
    // крестику, оранжевая — по корпусу (крестика ещё нет или он за стволом).
    // Заодно и диагностика: если метка не на том объекте, чинить надо подбор
    // цели, а не контроллер.
    if (tgt.on_screen) {
        auto* fg = ImGui::GetForegroundDrawList();
        const ImVec4& fc = tgt.at_spot ? cfg::esp::farm_spot_col : cfg::esp::farm_body_col;
        const ImU32 mc = IM_COL32((int)(fc.x*255),(int)(fc.y*255),(int)(fc.z*255), tgt.at_spot ? 235 : 230);
        const float r = tgt.at_spot ? 13.f : 17.f;
        fg->AddCircle({tgt.sx, tgt.sy}, r, mc, 24, 3.f);
        fg->AddLine({tgt.sx - r * 1.6f, tgt.sy}, {tgt.sx - r * 0.5f, tgt.sy}, mc, 3.f);
        fg->AddLine({tgt.sx + r * 0.5f, tgt.sy}, {tgt.sx + r * 1.6f, tgt.sy}, mc, 3.f);
        fg->AddLine({tgt.sx, tgt.sy - r * 1.6f}, {tgt.sx, tgt.sy - r * 0.5f}, mc, 3.f);
        fg->AddLine({tgt.sx, tgt.sy + r * 0.5f}, {tgt.sx, tgt.sy + r * 1.6f}, mc, 3.f);
        if (tgt.at_spot)
            fg->AddCircleFilled({tgt.sx, tgt.sy}, 2.5f, mc, 12);
    }

    // ---- камера: накопитель MouseLook, без тача -------------------------------
    // Знаки те же, что у мемори-аима: yaw +вправо -> накопитель Y, pitch +вверх ->
    // накопитель X с минусом (вверх — меньше). Накопитель уже в градусах, коэффициента
    // чувствительности не нужно: абсолютная запись сама задаёт поворот.
    float lookSens = 0.f; bool lookInv = false, lookGyro = false;
    esp_mem_aim_look_params(lookSens, lookInv, lookGyro);
    float accX = 0.f, accY = 0.f;
    const bool haveAcc = esp_mem_aim_read_angles(accX, accY);
    // Запись подтверждена, если накопитель вернул ровно то, что мы писали.
    const bool acked = s_memPend && haveAcc &&
                       fabsf(WrapDeg180(accY - s_memWY)) < kMemAckEps &&
                       fabsf(WrapDeg180(accX - s_memWX)) < kMemAckEps;

    // ---- фаза ---------------------------------------------------------------
    // Дотянулись ли до точки прицела. По крестику порог заметно ближе, чем по
    // корпусу: удар по X засчитывается только в упор.
    const bool isTree = (tgt.kind == 0);
    const bool atSpot = tgt.at_spot;
    const bool wasMining = (g_farmPhase == 3);
    // meleeTight — дистанция, с которой удар реально достаёт точку прицела.
    // По крестику она же решает «достали ли» (с гистерезисом meleeHold, чтобы
    // не выпускать узел из-за полуметрового дрожания дистанции). По корпусу
    // фаза удара начинается раньше — узел большой, — НО стик продолжает
    // поджимать вперёд вплоть до meleeTight: иначе топор до коры не дотянется,
    // первый удар не пройдёт и крестик так и не появится.
    // Живая дальность орудия (из памяти игры) важнее эмпирических порогов:
    // это ровно то число, с которым игра сравнивает дистанцию удара.
    const bool liveReach = (tgt.melee_reach >= kReachLiveMin);
    // Меряем в той же метрике, в которой задан порог: с живой дальностью —
    // 3D-метры от глаза (как считает игра), без неё — прежние горизонтальные.
    // Исключение — tgt.ray_self: луч игры упёрся в сам узел раньше нашей точки
    // прицела (у камня прицел стоит на пивоте ВНУТРИ породы, log 14.09: aim_3d
    // 1.97..2.33 м при луче 0.51..0.77 м). Игра сравнивает с дальностью удара
    // именно distance своего луча (FPMelee.ZkX), поэтому и мы берём её: на
    // крупном камне aim_3d не влезал ни в один порог, и бот не доходил до фазы
    // удара даже с живым попаданием луча в породу. Для дерева ветка не
    // срабатывает: там прицел стоит на коре (ray_distance ≈ aim_3d), и
    // ray_self ставится только если старая формула вообще заподозрила стену.
    const bool raySelfCloser = tgt.ray_self && tgt.ray_valid &&
                               tgt.ray_distance > 0.f && tgt.ray_distance < tgt.aim_3d;
    const float distNow = liveReach ? (raySelfCloser ? tgt.ray_distance : tgt.aim_3d)
                                    : tgt.aim_dist;
    float meleeTight, meleeHold, meleeBody;
    if (liveReach) {
        meleeTight = tgt.melee_reach * kReachLiveTight;
        meleeHold  = tgt.melee_reach * kReachLiveHold;
        meleeBody  = meleeTight + kReachBodyAllowance;
    } else {
        meleeTight = isTree ? kReachTreeSpot : kReachOreSpot;
        meleeHold  = isTree ? kReachTreeHold : kReachOreHold;
        meleeBody  = isTree ? kReachTreeBody : kReachOreBody;
    }
    const float reachNow = atSpot ? (wasMining ? meleeHold : meleeTight) : meleeBody;
    const bool inReach = distNow <= reachNow;

    // Допуски наведения по крестику: сколько градусов промаха даёт N метров на
    // текущем удалении. Меряем от глаза (aim_3d) — угловая ошибка превращается в
    // промах именно через эту дистанцию.
    const bool lookSpot = tgt.look_spot;
    const float eyeDist = (std::isfinite(tgt.aim_3d) && tgt.aim_3d > 0.05f)
                        ? tgt.aim_3d : tgt.aim_dist;
    auto offset_deg = [&](float meters, float min_deg) {
        float deg = min_deg;
        if (std::isfinite(eyeDist) && eyeDist > 0.05f)
            deg = atan2f(meters, eyeDist) * 57.29577951f;
        return fminf(fmaxf(deg, min_deg), kSpotTolMax);
    };
    const float aimTol   = offset_deg(kSpotAimM,   kSpotAimMin);    // докудa доводим
    const float swingTol = offset_deg(kSpotSwingM, kSpotSwingMin);  // порог удара
    // Ошибка наведения. Её целиком считает game_farm: «камера смотрит мимо точки
    // удара» плюс ограниченная поправка по факту попадания живого луча (см.
    // «Поправка по факту попадания» там же) — контроллеру это одна величина.
    const float steerYaw   = tgt.yaw;
    const float steerPitch = tgt.pitch;
    // Мёртвая зона по pitch: по крестику она РАВНА доводке (внутри допуска pitch
    // всё равно не двигаем, но и ошибка там уже в сантиметрах), по корпусу в
    // упор половина градуса, а на подходе правим только сильно задранную камеру.
    const float pitchDead = lookSpot ? aimTol
                                     : (inReach ? kPitchDeadSpot : kPitchDeadWalk);
    // По крестику доводим и по pitch до того же допуска (по корпусу мёртвая зона
    // по pitch съедает ошибку до полуградуса — там это не важно).
    // Доводка камеры с гистерезисом: доводим, пока ошибка снаружи допуска, и
    // прекращаем внутри него — так камера встаёт в центр X и не дёргается.
    float startDeg, stopDeg;
    if (lookSpot) {
        stopDeg  = aimTol;
        startDeg = aimTol * 1.6f;
    } else if (inReach) {
        startDeg = kAimBodyStart;
        stopDeg  = kAimBodyStop;
    } else {
        startDeg = kAimWalkStart;
        stopDeg  = kAimWalkStop;
    }
    // По крестику pitch сравнивается с допуском напрямую: вычитать мёртвую зону
    // нельзя, иначе errDeg «съедал» ошибку pitch и доводка прекращалась, не
    // поставив крестик в центр по вертикали.
    const float pitchErr = lookSpot ? fabsf(steerPitch)
                                    : fmaxf(fabsf(steerPitch) - pitchDead, 0.f);
    const float errDeg = fmaxf(fabsf(steerYaw), pitchErr);

    // В пределах удара — всегда MINE: камера доводится записью в накопитель
    // прямо в крестик, и удар уходит по нему штатным лучом игры.
    // Вне пределов — идём. Отдельной
    // фазы поворота больше нет: стик давит прямо в сторону цели относительно
    // камеры (полный аналог, 360°), так что шаг к узлу начинается с первого же
    // кадра, а камера доворачивается параллельно — ходьба её не ждёт.
    const int phase = inReach ? 3 : 2;
    g_farmPhase = phase;

    // Потеря ray_self не означает, что узел добыт: камера могла перейти
    // на новый крестик или чтение луча могло пропасть на медленном кадре.
    // В этом случае Swing ниже отпустит атаку и дождётся подтверждения луча.

    // Сел ли прицел на ТОЧКУ УДАРА. По крестику ждём и yaw, и pitch внутри
    // допуска: иначе удар ляжет мимо X и серия не вырастет (ради неё всё и
    // делается). По корпусу допуск широкий — узел большой, а первое попадание
    // лишь создаёт крестик.
    const bool aimCentered = fabsf(steerYaw) <= aimTol && fabsf(steerPitch) <= aimTol;
    if (phase == 3 && lookSpot && !aimCentered) s_spotAimStuck += dt;
    else s_spotAimStuck = 0.f;
    // Удар открываем, когда промах в допуске удара (5 см): серия меряется 15 см,
    // так что этого хватает с запасом. Страховка: довестись не удаётся долго
    // (запись в накопитель не проходит) — бьём, лишь бы камера смотрела в узел:
    // урон по узлу пойдёт, серия просто не вырастет, зато watchdog не сдаст
    // живой узел в чёрный список.
    const bool aimOnSpot = fabsf(steerYaw) <= swingTol && fabsf(steerPitch) <= swingTol;
    const bool continuousTool = tgt.continuous_tool ||
                                (tgt.attack_period > 0.f && tgt.attack_period <= 0.25f);
    const bool aimSettled = lookSpot
        ? (aimOnSpot ||
           (continuousTool && (tgt.ray_self || fabsf(tgt.yaw) <= kSwingBodyYaw)) ||
           (s_spotAimStuck > kSpotStuckTime && fabsf(tgt.yaw) <= kSwingBodyYaw))
        : (fabsf(tgt.yaw) <= kSwingBodyYaw);

    // ---- камера: пишем накопитель MouseLook ----------------------------------
    // Две вещи, без которых камера «моталась» перед тем как встать в крестик:
    //  1) шаги не накапливаются. После каждой записи ждём, пока ПОЗА камеры её
    //     покажет (измеренная ошибка изменилась) или истёк таймаут. Раньше
    //     следующая запись уходила через кадр, пока предыдущая была ещё «в
    //     полёте»: доля остатка 0.45 при задержке отклика в кадр-два даёт
    //     затухающую раскачку — 3-4 проскока мимо цели, и только потом центр.
    //  2) крупная ошибка закрывается одним движением (kAimJump*), мелкая —
    //     мягко (kAimStepFine), чтобы шум точки попадания не дёргал камеру.
    {
        const float wantYaw   = steerYaw;
        const float wantPitch = (fabsf(steerPitch) > pitchDead) ? steerPitch : 0.f;
        const float wantMag   = fmaxf(fabsf(wantYaw), fabsf(wantPitch));

        if (s_lookWait) {
            s_lookWaitT += dt;
            const bool responded = fabsf(steerYaw - s_lookErrY) > kLookRespDeg ||
                                   fabsf(steerPitch - s_lookErrP) > kLookRespDeg;
            if (responded || s_lookWaitT >= kLookWaitMax) s_lookWait = false;
        }

        const bool wantTurn = haveAcc &&
                              (s_camTurn ? (errDeg > stopDeg) : (errDeg > startDeg));
        if (!wantTurn) {
            s_camTurn = false; s_memPend = false; s_memWait = 0.f;
        } else if (!s_lookWait) {
            s_camTurn = true;
            if (s_memPend && !acked && s_memWait < kLookWaitMax) {
                s_memWait += dt;                  // ответа ждём, второй шаг не шлём
            } else {
                if (wantMag < kAimJumpGate * 0.5f) s_coarseArmed = true;
                const bool coarse = s_coarseArmed && wantMag > kAimJumpGate;
                if (coarse) s_coarseArmed = false;
                const float cap = coarse ? kAimJumpMax
                                         : (inReach ? kMemStepBody : kMemStepWalk);
                float offYaw   = wantYaw   * (coarse ? kAimJumpGain : kAimStepFine);
                float offPitch = wantPitch * (coarse ? kAimJumpGain : kAimStepFine);
                offYaw   = fminf(fmaxf(offYaw,   -cap), cap);
                offPitch = fminf(fmaxf(offPitch, -cap), cap);
                if (!std::isfinite(offYaw))   offYaw = 0.f;
                if (!std::isfinite(offPitch)) offPitch = 0.f;
                if (fabsf(offYaw) < kMemDeadDeg && fabsf(offPitch) < kMemDeadDeg) {
                    s_memPend = false; s_memWait = 0.f;
                } else {
                    const float writeY = WrapDeg180(accY + offYaw);
                    const float writeX = WrapDeg180(accX - offPitch);
                    if (esp_mem_aim_write_angles(writeX, writeY)) {
                        s_memWX = writeX; s_memWY = writeY;
                        s_memPend = true; s_memWait = 0.f;
                        s_lookErrY = steerYaw; s_lookErrP = steerPitch;
                        s_lookWait = true; s_lookWaitT = 0.f;
                    } else {
                        s_memPend = false; s_memWait = 0.f;
                    }
                }
            }
        }
    }

    // Качок камеры при промахе луча: игра пересчитывает луч только когда камера
    // двигается, а прицел уже в допуске и сервой не трогается. Амплитуда меньше
    // допуска удара, так что удар по X не страдает; серва при необходимости
    // вернёт камеру сама.
    if (phase == 3 && aimSettled && rayMiss && haveAcc && !s_camTurn && !s_memPend &&
        s_rayMissTime > kRayMissNudgeAfter) {
        s_nudgeTimer -= dt;
        if (s_nudgeTimer <= 0.f) {
            const float writeY = WrapDeg180(accY + s_nudgeSign * kRayMissNudgeDeg);
            if (esp_mem_aim_write_angles(accX, writeY)) {
                s_memWX = accX; s_memWY = writeY;
                s_memPend = true; s_memWait = 0.f;
            }
            s_nudgeSign = -s_nudgeSign;
            s_nudgeTimer = kRayMissNudgeEvery;
        }
    } else {
        s_nudgeTimer = 0.f;
    }

    // Обратная связь памяти-джойстика: пока кандидат ведёт стик, дистанция до
    // точки подхода обязана сокращаться — по этому признаку модуль и выбирает
    // рабочий слой ввода (см. farm_joy.h).
    {
        const bool walkClean = (phase == 2) && (s_evadeTime <= 0.f) &&
                               (tgt.walk_dist > 0.f);
        farmjoy::observe(tgt.walk_dist, walkClean);
    }

    // ---- стик: движение через ПАМЯТЬ игры, без тача ----------------------------
    // Вектор стика (x — вправо, y — вперёд, магнитуда 0..1) уходит в память
    // игры (farm_joy.cpp: UI-стик / ввод хендлера / канал MovementInput),
    // синтетический тач по зоне джойстика больше не используется вовсе.
    {
        // В фазе удара всё ещё поджимаем вперёд, пока до точки прицела дальше
        // meleeTight: у тонких деревьев пивот в центре ствола, и остановка в
        // паре метров оставляла удар коротким. Гистерезис — жмём, пока дальше
        // +0.1..0.25 м, отпускаем только внутри (без хлопанья на границе,
        // которое выглядело как топтание на месте).
        const float pressAt = s_moveDown
            ? meleeTight
            : meleeTight + (liveReach ? 0.12f : (isTree ? 0.08f : 0.25f));
        bool wantWalk =
            (phase == 2 && tgt.walk_dist > kArriveWalk) ||
            (phase == 3 && distNow > pressAt) ||
            (phase == 3 && !aimSettled && !tgt.ray_self && tgt.walk_dist > kArriveWalk) ||
            // Нет луча игры — прицел мимо меша: поджимаем вплотную, там ствол
            // перекрывает уход декали вбок и луч появляется (97% на 0.5 м).
            // Пока ждём подтверждённого луча; watchdog ограничивает попытку.
            (phase == 3 && noRay &&
             distNow > kNoRayCloseTo);
        if (s_evadeTime > 0.f) wantWalk = true;  // манёвр ведёт стик сам

        // Гистерезис отпускания: фаза мигает на кадр-другой вокруг порогов
        // (шум дистанции/угла), и каждое мигание раньше поднимало и снова
        // ставило палец движения — видимое «дёрганье джойстика» на подходе.
        // Теперь палец поднимается, только если ходьба не нужна четверть
        // секунды подряд; на удары это не влияет (палец 2 независим).
        if (wantWalk) s_walkOffTime = 0.f;
        else if (s_moveDown) {
            s_walkOffTime += dt;
            if (s_walkOffTime < kWalkOffHold) wantWalk = true; // держим, гасим дёрганье
        }

        // Доворот на новую цель: пока камера смотрит в сторону прежнего узла,
        // стик не жмём вовсе — иначе бот идёт по старому курсу и упирается в
        // пень. Таймер «нет продвижения» в это время не тикает.
        if (s_freshTurn > 0.f) {
            s_freshTurn -= dt;
            if (fabsf(tgt.walk_yaw) <= kFreshTurnYaw || aimSettled) {
                s_freshTurn = 0.f;
            } else {
                wantWalk = false;
                s_stuckTime = 0.f;
            }
        }

        if (wantWalk) {
            // Направление на цель задаёт сам вектор стика, камера лишь доводит
            // прицел до точки удара.
            float wantX = 0.f, wantY = 0.f;
            if (s_evadeTime > 0.f) {
                // Обход препятствия: коротко назад, затем жёстко вбок и чуть
                // вперёд — скользим вокруг камня/стены, в которые упирается
                // прямолинейная ходьба.
                s_evadeTime -= dt;
                if (s_evadeTime > kEvadeTime - 0.6f) { wantX = 0.f;                  wantY = -0.90f; }
                else                                 { wantX = 0.95f * s_evadeDir;   wantY =  0.35f; }
                if (s_evadeTime <= 0.f) { s_evadeTime = 0.f; s_stuckTime = 0.f; s_lastWalk = 1e9f; }
            } else {
                // Мёртвая зона: пара градусов дрожания yaw не должна рулить
                // вовсе — знак у почти нулевой ошибки меняется каждый кадр, и
                // стик от этого хлопал влево-вправо.
                float steerYaw = tgt.walk_yaw;
                const float yawDead = (phase == 3) ? 3.f : ((tgt.walk_dist > 3.f) ? 8.f : 3.f);
                if (fabsf(steerYaw) < yawDead) steerYaw = 0.f;
                if (phase == 3) {
                    float moveYaw = atSpot ? steerYaw : tgt.yaw;
                    if (fabsf(moveYaw) < 3.f) moveYaw = 0.f;
                    const float a = moveYaw * 0.017453293f;
                    wantX = sinf(a);
                    wantY = cosf(a);
                } else {
                    const float a = steerYaw * 0.017453293f;
                    wantX = sinf(a);
                    wantY = cosf(a);
                }
                if (s_walkOffTime > 0.f) { wantX = 0.f; wantY = 0.f; }  // доезжаем к центру
            }
            // Гладим ТОЛЬКО направление, а к краю стик встаёт с первого кадра.
            // Раньше сглаживался весь вектор, и при смене курса (камера доводит
            // прицел, вектор стика едет за ней) его магнитуда висела на 0.9
            // вместо 1.0 — а магнитуда стика это доля скорости хода: на 0.9
            // персонаж идёт медленнее полного шага. Бег включается не
            // отклонением, а кнопкой бега (её ведёт отдельно farmjoy::sprint),
            // поэтому отклонение держим полным. Угол по-прежнему ведём 26/с
            // (90% пути за ~90 мс), так что «дёрганья» руля нет.
            const float wantLen = sqrtf(wantX * wantX + wantY * wantY);
            if (wantLen > 1e-4f) {
                float ax = wantX / wantLen, ay = wantY / wantLen;
                const float curLen = sqrtf(s_joyX * s_joyX + s_joyY * s_joyY);
                if (curLen > 1e-3f) {
                    const float k = 1.f - expf(-26.f * dt);
                    const float cx = s_joyX / curLen + (ax - s_joyX / curLen) * k;
                    const float cy = s_joyY / curLen + (ay - s_joyY / curLen) * k;
                    const float cl = sqrtf(cx * cx + cy * cy);
                    if (cl > 1e-4f) { ax = cx / cl; ay = cy / cl; }
                }
                const float mag = (wantLen > 1.f) ? 1.f : wantLen;
                s_joyX = ax * mag;
                s_joyY = ay * mag;
            } else {
                s_joyX = 0.f;
                s_joyY = 0.f;
            }
            farmjoy::command(s_joyX, s_joyY);
            // Бег. Игра включает его только по кнопке бега (InputAction
            // LD+0x48), а не по величине стика: без взведённой кнопки бот
            // идёт пешком при любой оттяжке (см. farm_joy.h). Модуль сам
            // снимет кнопку в release().
            farmjoy::sprint(true);
            s_moveDown = true;
        } else if (s_moveDown) {
            farmjoy::release();
            s_joyX = 0.f; s_joyY = 0.f;
            s_moveDown = false;
        }
    }

    // ---- перекрыт ли узел ------------------------------------------------------
    // Луч, которым игра сама проверяет удар (RaycastManager -> активности
    // PlayerEventHandler -> GKo.RaycastHit), упёрся в ЧУЖУЮ геометрию:
    // заметно ближе нашей точки прицела (ray_blocked — камень, забор, склон)
    // или в посторонний объект на линии удара вовсе без учёта дистанции
    // (ray_foreign — то самое «упёрся в постройку носом»). Удар в такую линию
    // ломает чужое, так что не машем — отходим манёвром и, если не помогает,
    // сдаём узел. Учитывается только при севшем прицеле: пока камера едет к
    // узлу, луч цепляет случайные объекты и «перекрыто» выходит ложным.
    // Наведение тут не помогает: луч игры всё равно упирается в чужую
    // геометрию, так что ждём манёвра.
    if (phase == 3 && aimSettled &&
        (tgt.ray_blocked || tgt.ray_foreign)) {
        s_blockedTime += dt;
        if (s_blockedTime > kBlockedTime && s_evadeTime <= 0.f) {
            s_blockedTime = 0.f;
            if (s_evadeCount < kEvadeMax) {
                s_evadeWhy = 2;
                s_evadeTime = kEvadeTime;
                s_evadeDir = (s_evadeCount % 2 == 0) ? 1.f : -1.f;
                ++s_evadeCount;
            } else {
                esp_farm_blacklist(tgt.id, 60.f);
                releaseAll();
                resetNode();
                s_settle = kSettleTime;
            }
        }
    } else {
        // Дебаунс в обе стороны. Таймер перекрытия не обнуляется мгновенно, а
        // тает вдвое медленнее, чем копится: мигание луча (прицел ещё едет, X
        // вот-вот появится) больше не сбрасывает подготовку обхода. Счётчик
        // обходов прощается только после kBlockedClear чистой линии удара —
        // иначе следующая перекрытая точка начнётся сразу с отказа от узла, но
        // и бесконечного круга, как раньше, тоже нет: бюджет kEvadeMax теперь
        // действительно заканчивается и узел уходит в чёрный список.
        s_blockedTime = fmaxf(0.f, s_blockedTime - dt * 2.f);
        if (s_evadeTime <= 0.f && s_evadeWhy == 2) {
            s_clearTime += dt;
            if (s_clearTime > kBlockedClear) { s_evadeWhy = 0; s_evadeCount = 0; s_clearTime = 0.f; }
        } else {
            s_clearTime = 0.f;
        }
    }

    // ---- удары (память, без тача) ----------------------------------------------
    if (phase == 3) {
        s_mineTime += dt;
        // Удар — память, а не палец: держим байт «атака зажата» у хендлера
        // ввода (farmjoy::attack(true)), и игра в каждом кадре сама зовёт
        // AttackContinuously.Try() -> FPManager.OnTry_Attack(true) ->
        // оружие.TryAttackContinuously(). Бьёт оно так часто, как позволяет
        // его же кулдаун (FPMelee.m_NextUseTime), поэтому ритм ниже — только
        // скважность удержания: короткое зажатие и пауза. Такт взят из
        // орудия (m_TimeBetweenAttacks + pauseAfterAttack), живого значения
        // нет — безопасные 600 мс.
        float holdMs = kSwingHoldMs, idleMs = kSwingIdleMs;
        if (tgt.attack_period > 0.05f && tgt.attack_period < 5.f) {
            const float periodMs = fminf(fmaxf(tgt.attack_period * 1020.f, 250.f), 2000.f);
            holdMs = fminf(fmaxf(periodMs * 0.10f, 40.f), 120.f);
            idleMs = fmaxf(periodMs - holdMs, 60.f);
        }
        // Атака возможна только по подтверждённому своему коллайдеру. На
        // пропавшем луче или чужой геометрии зажатие снимается в том же кадре.
        const bool nodeDying = farm_control::health_depleted(
            tgt.node_health >= 0.f, tgt.node_health);
        const bool swingAllowed = farm_control::safe_to_swing(
            aimSettled, tgt.ray_valid, tgt.ray_self, tgt.ray_foreign,
            tgt.ray_blocked, nodeDying);
        s_swing.tick((int)roundf(dt * 1000.f), swingAllowed, continuousTool,
                     (int)holdMs, (int)idleMs, writeAttack);
    } else {
        s_swing.stop(writeAttack);
    }

    // ---- watchdog'и ------------------------------------------------------------
    const bool walkClosing = (phase == 2) ||
                             (phase == 3 && s_moveDown && distNow > meleeTight + 0.10f && !tgt.ray_self);
    if (walkClosing) {
        if (phase == 2) s_mineTime = 0.f;
        const float watchDist = (phase == 2) ? tgt.walk_dist : distNow;
        if (watchDist < s_lastWalk - 0.25f) {
            s_lastWalk = watchDist;
            s_stuckTime = 0.f;
        } else if (s_evadeTime <= 0.f) {
            s_stuckTime += dt;
            if (s_stuckTime > kStuckTime) {
                if (s_evadeCount < kEvadeMax) {
                    s_evadeWhy = 1;
                    s_evadeTime = kEvadeTime;
                    s_evadeDir = (s_evadeCount % 2 == 0) ? 1.f : -1.f;
                    ++s_evadeCount;
                    s_stuckTime = 0.f;
                } else {
                    esp_farm_blacklist(tgt.id, 30.f);
                    releaseAll();
                    resetNode();
                    s_settle = kSettleTime;
                }
            }
        }
    }
    if (phase == 3) {
        if (!walkClosing && s_evadeWhy != 2) { s_evadeCount = 0; s_evadeTime = 0.f; s_evadeWhy = 0; }
        // Машем, а узел не убывает: стоим на волосок дальше нужного (финальный
        // подвод это лечит) или в руке не инструмент. Прогресс теперь видим по
        // двум независимым признакам — падение остатка И рост серии попаданий по
        // крестику (streak растёт только когда игра засчитала удар в X, так что
        // это самый честный сигнал, что фарм действительно работает).
        bool progress = false;
        // Здоровье узла — самый тонкий признак: m_CurrentHealth падает уже от
        // первого дошедшего удара, тогда как fractionRemaining сдвигается на
        // проценты лишь спустя десятки попаданий (а у некоторых узлов не
        // сдвигается вовсе, и watchdog сдавал живой узел).
        if (tgt.node_health >= 0.f) {
            if (s_healthStart < 0.f) s_healthStart = tgt.node_health;
            else if (tgt.node_health < s_healthStart - 0.01f) { s_healthStart = tgt.node_health; progress = true; }
        }
        if (tgt.fraction >= 0.f) {
            if (s_fracStart < 0.f) s_fracStart = tgt.fraction;
            else if (tgt.fraction < s_fracStart - 0.01f) { s_fracStart = tgt.fraction; progress = true; }
        }
        if (tgt.streak > s_bestStreak) { s_bestStreak = tgt.streak; progress = true; }
        if (progress) {
            s_mineTime = 0.f;
        } else {
            // Сдаёмся, только когда прогресс есть чем мерить (здоровье или
            // остаток ЧИТАЮТСЯ) и он явно не двигается долго. Когда не читается
            // ничего, прежний 14-секундный таймер бросал вполне живые узлы на
            // середине — «перестаёт добивать узел».
            const float giveUp = (tgt.node_health >= 0.f || tgt.fraction >= 0.f)
                               ? kGiveUpDrain : kGiveUpBlind;
            if (s_mineTime > giveUp) {
                esp_farm_blacklist(tgt.id, 60.f);
                releaseAll();
                resetNode();
                s_settle = kSettleTime;
            }
        }
    }
}

