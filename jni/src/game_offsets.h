#pragma once
#include <cstdint>

// Release profile, build 11991 (проверка: tools/offsets/verify_release_11991.py).
// Предыдущие релизы 11989/11985 — ниже в комментариях.
// Source: dump11991.7z (dump.cs + il2cpp.h + libil2cpp.so + script.json),
// 32402 типа, 22755 TypeInfo-слотов.
//
// Сверка 11989 -> 11991 (dump11991): managed-раскладки НЕ изменились —
// таблица verify_release_11991.py (core traversal, TypeInfo, статики) сошлась
// побайтово, сверх неё пройдены именованные поля всех ключевых классов
// (MouseLook/KCC/FPMotion/FPManager/FPWeaponBase/Hitscan-конфиги/вьюхи/
// виталы/маркеры/стик). Съехали только:
//   * TypeInfo-слоты: .data 0xE376A48..0xE9D9CA8 (file_off = RVA − 0xC000);
//   * методные RVA: .text/il2cpp file_off = RVA − 0x4000 (все старые
//     «текущие» RVA в комментариях протухли — исторические не трогаем);
//   * ротация обфусцированных имён 11989 -> 11991:
//     обработчик kAu -> rIf (база rIF; Health-обёртка kAK -> rIu<>,
//     Vsf -> rIa, события rIj/rIU), аим kwY -> rjD (TQq/Pla-раскладка на
//     месте), GameNetwork lhV -> GwS (по-прежнему статик 0x0),
//     TOD gF/Hi -> lI (TDI 706, TypeInfo 0xE611560, Cycle 0x40),
//     LayerMask-25 yCC -> ryl (TypeInfo 0xE612F58), вьюха Yp -> DV /
//     реализация YU -> DO (TDI 4515, та же тройка 0x50/0x58/0x70),
//     декоратор kun -> DW, hit-tracker KSn -> pH (словарь 0x18),
//     displayName KSQ -> GcV, модель Yf -> Du, nicklabel Di -> ol,
//     voicePlayer kIq -> rqb, FPManager KLY/KLd/KLm/KLI -> GsJ/GsA/GsF/GsU,
//     RaycastData kAm -> rIp, PlayerWeapon view Yp -> DV (поле 0x100).
//   Инварианты живы: PLH -> PM слот = 0x38 (0xE60B780/0xE60B7B8), промисы
//   через 8 байт (0xE61AB50/0xE61AB58), TDI 706/1902/4515/21933/21935.
//
// Ключевой RE сборки 11985 (дроу-путь, file_off = RVA − 0x4000 для .text;
// ИСТОРИЯ — в 11991 RVA съехали):
//   FPManager.LTI(WeaponPiece&, int slot) RVA 0x6D59828 — аналог Kxi 11966:
//     пересоздаёт CTS `str x,[x0,#0x88]!` и UTCS `str x,[x0,#0x98]!`, piece
//     передаётся ПО ССЫЛКЕ (ldr q0,[x21]; ldr x8,[x21,#0x10] = 0x18 байт);
//   PlayerWeapon-метод (аналог pFz) RVA 0x7BB70B4 — перед BL на LTI читает
//     FPManager `ldr x0,[x19,#0x110]` и гейт `ldr w2,[x19,#0xF0]` (slot-аргумент);
//     bool @0xF4 выбирает FP-путь (LTI) против старого Action @0xE8;
//   обёртка RVA 0x7BB7088 — копирует piece_local 0x18 байт из +0xD0/+0xE0
//     (аналог pMj/pMZ, читавших QVN +0xC0 в 11966).
//   Прочие RVA в комментариях ниже (Kxi 0x68EE210, pFz 0x68EFBC8, FDN
//   0x68FCD10, ...) — ИСТОРИЧЕСКИЕ, из dump11966; в 11985 они съехали.

namespace game_offsets {

// NOTE: every obfuscated class/field name quoted in the comments below is
// valid only for the dump it was read from - the obfuscator re-rolls them on
// each game build (fvp->pmi, wK->ij, Mo->sR, ...; в билде 2026-10-03:
// ij->gt, sR->ON, sY->OB, pGW->WRU, GKo->Wxi, pmp->WxL, pmK->WxY, JE->vV,
// UV->ak, eDt->Wxz, GuI`1 -> не разрешён в дампе, читается как Il2CppObject).
// Names are kept purely as breadcrumbs; when re-checking after an update,
// match structs and fields POSITIONALLY (offset + type).
//
// Карта ротации 11966 -> 11985 (позиции сверены по обоим дампам; комментарии
// ниже цитируют имена обоих билдов, где поле сместилось):
//   хендлер событий hsL -> kAu (базы hsN -> kAH -> kAg); sync-обёртки
//   hkz/hkh/GuI`1 -> kAK<T,K>/kAr<T> (форма та же: Action 0x10, Filter 0x18,
//   значение 0x20, последнее 0x28); GKo -> kAm; Gub-активности -> kAj/kAx;
//   PlayerManager: zNy -> KSQ (displayName), hwR -> kwY (aim assist),
//   Eo -> Yf (model wrapper), zsd -> KSm (VoicePlayerState), Nt/uGa ->
//   lE/KSn (hit tracker; НЕ путать с lE 11966 = TOD_Sky);
//   FPManager: uzN/uzL -> KLY/KLd, uzf/uzi/uzD -> KLm/KLl/KLI, uzj -> KLo,
//   <uhF> -> <KHk>, uzH/BYu -> KLM (FOV), <uhh> -> <KLR> (aim blend),
//   вставлен AssetReference KLE @0x80 (хвост +0x10);
//   FPObject: <uhq> -> <KHI>, Ltl -> KHo (Item), Gum -> kAu;
//   PlayerWeapon: QVW -> wDN (SyncVar piece), sR-ref -> Yp
//   playerWeaponViewReference;
//   вьюха третьего лица sR/ON -> YU : Yp (TDI 4515, найдена по форме полей
//   WeaponBase/WeaponPiece/Transform), декоратор pGW/WRU -> kun (sealed : Yp,
//   единственное поле Yp @0x10);
//   nicklabel gt -> Di; voicePlayer WDB -> kIq; интерфейсы дроу hbr -> jH/kUA;
//   Draw-событие eDt/Wxz -> kAP (Action @0x10); EG (реестр PlayerWeaponPiece)
//   -> YB; hMj/IWt (25 LayerMask-статик) -> kiQ; TOD_Sky lE/Gf -> QA (TDI 706).
//   Переехали в неймспейсы: FPCrossbow/FPSnowball -> HyperHug.Games.Oxide.
//   By_Namespace.Oxide.Player.First_Person.*; конфиги оружия и AimAssist ->
//   HyperHug.Games.Oxide.Features.Weapons.* / .Features.AimAssist.*.

// Native Unity Camera (via Camera::m_CachedPtr) — from libunity.so (dump2)
// get_worldToCameraMatrix_Injected @ 0x5ac514 → helper 0xe1fe08 returns cam+0x70
// get_projectionMatrix_Injected    @ 0x5ac53c → helper 0xe1fe6c returns cam+0xB0
// IMPORTANT: +0x70 / +0xB0 are LAZY caches (dirty flags @ +0x502 / +0x500).
// External process_vm_readv does NOT run the getters, so +0x70 goes stale when the
// camera moves — that is the ESP "drifts with camera" bug. Prefer rebuilding view
// from the live Transform at cam+0x20 (same pointer the dirty path uses).
inline constexpr std::uint64_t CAMERA_PROJECTION_MATRIX = 0xB0;  // Matrix4x4 projection (cached)
inline constexpr std::uint64_t CAMERA_VIEW_MATRIX       = 0x70;  // Matrix4x4 worldToCamera (cached, stale!)
inline constexpr std::uint64_t CAMERA_WORLD_TO_CLIP     = 0xF0;  // projection * worldToCamera product
inline constexpr std::uint64_t CAMERA_PREV_VIEW_PROJ    = 0x5C8; // previousViewProjection (frame-written)
inline constexpr std::uint64_t CAMERA_NATIVE_TRANSFORM  = 0x20;  // Camera (наследник Component) + 0x20 —
                                                                 // GameObject камеры, НЕ Transform. Проверено
                                                                 // по libunity.so релиза: в коде пересборки
                                                                 // матрицы вида (strb wzr,[x19,#0x502] /
                                                                 // stp q0,q1,[x19,#0x70]) значение camera+0x20
                                                                 // подаётся в GetComponentFastPath — то есть
                                                                 // это GameObject. Transform камеры достаём из
                                                                 // его массива компонентов (game.cpp).
inline constexpr std::uint64_t CAMERA_FOV_DEGREES       = 0x170; // get_fieldOfView storage
inline constexpr std::uint64_t CAMERA_ASPECT            = 0x4E0;
inline constexpr std::uint64_t CAMERA_NEAR_CLIP         = 0x454;
inline constexpr std::uint64_t CAMERA_FAR_CLIP          = 0x458;
inline constexpr std::uint64_t CAMERA_VIEW_DIRTY        = 0x502; // byte, set when w2c cache invalid
inline constexpr std::uint64_t CAMERA_PROJ_DIRTY        = 0x500; // byte, set when proj cache invalid

inline constexpr std::uint64_t MANAGED_CACHED_PTR = 0x10;
inline constexpr std::uint64_t NETWORK_BEHAVIOUR_IDENTITY = 0x40;
inline constexpr std::uint64_t NETWORK_IDENTITY_IS_LOCAL_PLAYER = 0x4A;

inline constexpr float PLAYER_HEIGHT          = 1.8F;
inline constexpr float PLAYER_BOX_WIDTH_RATIO = 0.40F;
inline constexpr float MIN_PLAYER_DISTANCE    = 0.0F;
inline constexpr float MAX_PLAYER_DISTANCE    = 300.0F;

// Il2CppClass* TypeInfo globals in libil2cpp.so (this build's script.json).
// TypeInfoPointers[].Address is the inner .data slot that the reader dereferences
// once to obtain Il2CppClass*; these addresses move on every game update.
// GOT/RELATIVE references can identify this slot, but the outer address is not
// the RVA consumed by this reader.
//
// МЕХАНИЗМ (ошибка 9e26509 была здесь): код игры читает класс ДВУМЯ шагами —
// adrp/ldr берёт из .data ВНЕШНЮЮ GOT-запись (outer), чьё содержимое лоадер
// восстанавливает релокацией R_AARCH64_RELATIVE: значение outer = base +
// addend = адрес ВНУТРЕННЕГО слота; klass = [внутренний слот]. Наш читер
// делает ОДНО разыменование: rd_ptr(base + RVA) -> klass, значит RVA обязан
// быть ВНУТРЕННИМ слотом (addend).
//
// Историческая диагностика сборки 337e891 (не build 11966): эти outer-адреса
// не использовать в текущем профиле.
// УРОК 337e891 (слоты были неверны — опечатка в одной hex-цифре outer):
// правильные outer в том билде: PM 0xd9acc68 (не 0xd9a8c68!), GC 0xd9ab2f8
// (не 0xd9a72f8), NC 0xd9ab1a0 (не 0xd9a71a0) — «опечатанные» адреса тоже
// существуют в .rela.dyn, но указывают на чужие слоты, поэтому ручная цепочка
// «подтверждалась» против чужих ячеек. Надёжная методика, применённая тогда:
// 1) dump.cs даёт RVA методов класса ПОИМЁННО; 2) дизассемблируем их и
// голосуем: слот = addend того outer, на который ссылается большинство
// собственных методов класса; 3) РАНТАЙМ-ПОДТВЕРЖДЕНИЕ по дампам benzhack
// (сессия pid 7619, base=0x6e1002b000, rw-сегмент=imagedata): в ячейке слота
// лежит указатель на Il2CppClass в ptr1-регионе, из структуры читаются
// имя/неймспейс (строки в блобе 0x6dfaec8000) — все пять классов совпали
// ПО ИМЕНИ. Старые ячейки 0xDDB1AD8/0xDDB1AA8 опровергнуты рантаймом: там
// мусор 0xa000b409/0xa000b3fd (это «таблица типов v2» протектора S16 из
// малых целых, НЕ слоты классов). Перекрёстное подтверждение: PM-PLH = 0x30,
// как и в прошлом рабочем билде (0xD9C5310/0xD9C52E0).
inline constexpr std::uint64_t PLAYER_MANAGER_TYPEINFO_RVA = 0xE60B7B8; // release 11991: Oxide.PlayerManager TypeInfo slot
// Cysharp.Threading.Tasks.PlayerLoopHelper.
inline constexpr std::uint64_t PLAYER_LOOP_HELPER_TYPEINFO_RVA = 0xE60B780; // release 11991 TypeInfo slot; слот найден напрямую в script.json
inline constexpr std::uint64_t PLAYER_MANAGER_STATIC_FIELDS_ACTIVE_LIST = 0x08; // activePlayerList
inline constexpr std::uint64_t PLAYER_MANAGER_STATIC_FIELDS_LIST = 0x10; // clientPlayerList


// Managed-object layouts verified against release 11985.
inline constexpr std::uint64_t IL2CPP_CLASS_STATIC_FIELDS_OFFSET = 0xB8;
inline constexpr std::uint64_t PLAYERLOOPHELPER_RUNNERS = 0x30;
inline constexpr std::uint64_t LOOPRUNNER_ITEMS = 0x38;
inline constexpr std::uint64_t LOOPRUNNER_TAIL = 0x30;
// Две раскладки промисов UniTask.Delay, сверены по dump11949/11966/11985
// (поля в объекте: 0x10 nextNode, дальше тройка). Именование историческое:
// «голый» набор полей — у DelayIgnoreTimeScalePromise (unscaled-таймер),
// «SCALED» набор — у обычного DelayPromise (ждёт Time.deltaTime).
inline constexpr std::uint64_t DELAYPROMISE_TIMESPAN = 0x18;          // DelayIgnoreTimeScalePromise.delayFrameTimeSpan
inline constexpr std::uint64_t DELAYPROMISE_ELAPSED = 0x1C;           // DelayIgnoreTimeScalePromise.elapsed
inline constexpr std::uint64_t DELAYPROMISE_INITIAL_FRAME = 0x20;     // DelayIgnoreTimeScalePromise.initialFrame
inline constexpr std::uint64_t DELAYPROMISE_SCALED_TIMESPAN = 0x1C;   // DelayPromise.delayTimeSpan
inline constexpr std::uint64_t DELAYPROMISE_SCALED_ELAPSED = 0x20;    // DelayPromise.elapsed
inline constexpr std::uint64_t DELAYPROMISE_SCALED_INITIAL_FRAME = 0x18; // DelayPromise.initialFrame
// TypeInfo-слоты классов промисов для быстрого подбора (см.
// fast_loot_memory.h): release 11991, взяты из script.json TypeInfoPointers
// (Δ8 между двумя классами промисов сохранён: 0xE61AB58/0xE61AB50).
// _ALT — запасной слот: в бете пара классов опознаётся по имени в рантайме
// из двух кандидатов (см. game_offsets_beta.h), в релизе запасной не нужен.
inline constexpr std::uint64_t FASTLOOT_DELAYPROMISE_TYPEINFO_RVA = 0xE61AB58;     // Cysharp...UniTask.DelayPromise (TDI 21933)
inline constexpr std::uint64_t FASTLOOT_DELAYPROMISE_TYPEINFO_RVA_ALT = 0;
inline constexpr std::uint64_t FASTLOOT_IGNOREDELAY_TYPEINFO_RVA = 0xE61AB50;      // Cysharp...UniTask.DelayIgnoreTimeScalePromise (TDI 21935)
inline constexpr std::uint64_t FASTLOOT_IGNOREDELAY_TYPEINFO_RVA_ALT = 0;
inline constexpr std::uint64_t FPHITSCAN_CONFIG = 0x130;
// Projectile/config offsets rechecked against release 11985. The beta profile
// pins the pre-11966 values for the weapon fields that diverged.
inline constexpr std::uint64_t FPBOW_PROJECTILE_PREFAB = 0x150;
inline constexpr std::uint64_t FPCROSSBOW_CONFIG = 0x138;
inline constexpr std::uint64_t FPCROSSBOW_PROJECTILE_PREFAB = 0x148;
inline constexpr std::uint64_t FPROCKETLAUNCHER_CONFIG = 0x128;
inline constexpr std::uint64_t FPROCKETLAUNCHER_PROJECTILE_PREFAB = 0x148;
inline constexpr std::uint64_t FPEXPLOSIVECHARGE_PROJECTILE_PREFAB = 0x130;
inline constexpr std::uint64_t FPSNOWBALL_PROJECTILE_PREFAB = 0x130;
inline constexpr std::uint64_t FPSPEAR_PROJECTILE_PREFAB = 0x178;
// FPHitscan copies config values into these live fields during lMf/sPo. The
// config alone is therefore not enough for a weapon which is already drawn.
inline constexpr std::uint64_t FPWEAPON_TYPE = 0xDC; // FPWeaponBase.weaponType, release 11985
inline constexpr std::uint64_t FPWEAPON_EFFECTIVE_RELOAD_PERIOD = 0xE8;
inline constexpr std::uint64_t FPWEAPON_SHOT_PERIOD = 0x124;
// Per-instance source fields copied into the attack cache; checked in both supplied dumps.
inline constexpr std::uint64_t FPHITSCAN_FIRE_MODE = 0x138;
inline constexpr std::uint64_t FPHITSCAN_FIRE_DURATION = 0x140;
inline constexpr std::uint64_t FPHITSCAN_BURST_LENGTH = 0x144;
inline constexpr std::uint64_t FPHITSCAN_BURST_DURATION = 0x148;
inline constexpr std::uint64_t FPHITSCAN_BURST_PAUSE = 0x14C;
inline constexpr std::uint64_t FPHITSCAN_BURST_WAIT = 0x1F0;
inline constexpr std::uint64_t WAITFORSECONDS_SECONDS = 0x10;
inline constexpr std::uint64_t FPBOW_MIN_SHOT_PERIOD = 0x138;
inline constexpr std::uint64_t FPCROSSBOW_MIN_SHOT_PERIOD = 0x140;
inline constexpr std::uint64_t FPROCKETLAUNCHER_MIN_SHOT_PERIOD = 0x140;
inline constexpr std::uint64_t FPSNOWBALL_MIN_THROW_PERIOD = 0x12C;
inline constexpr std::uint64_t FPEXPLOSIVECHARGE_MIN_THROW_PERIOD = 0x12C;
inline constexpr std::uint64_t FPSPEAR_MIN_THROW_PERIOD = 0x170;
inline constexpr std::uint64_t FPSPEAR_CACHED_THROW_PERIOD = 0x1C0;
inline constexpr std::uint64_t FPHITSCAN_EFFECTIVE_LOOP_RELOAD_PERIOD = 0x2A0;  // было 0x2E0; zrb: сеттер-инициализатор читает config[0x4C] (LoopReloadPeriod) и пишет в +0x2A0 — сверено дизассемблером
inline constexpr std::uint64_t FPHITSCAN_CALCULATOR = 0x1F8;
inline constexpr std::uint64_t CALCULATOR_DISPERSION_CONFIG = 0x10;
inline constexpr std::uint64_t CALCULATOR_HITSCAN_CONFIG = 0x18;
inline constexpr std::uint64_t CALCULATOR_CACHED_BASE_DISPERSION = 0x20;
inline constexpr std::uint64_t HITSCANCONFIG_MAX_DISTANCE = 0x2C;
inline constexpr std::uint64_t HITSCANCONFIG_BULLET_SPEED = 0x30;
inline constexpr std::uint64_t HITSCANCONFIG_FIRE_RATE = 0x34;
inline constexpr std::uint64_t HITSCANCONFIG_PROJECTILES_PER_SHOT = 0x38;
inline constexpr std::uint64_t HITSCANCONFIG_BASE_DISPERSION = 0x3C;
inline constexpr std::uint64_t HITSCANCONFIG_FULL_RELOAD = 0x48;
inline constexpr std::uint64_t HITSCANCONFIG_LOOP_RELOAD = 0x4C;
inline constexpr std::uint64_t HITSCANCONFIG_BALLISTIC_PROFILE = 0x60;
inline constexpr std::uint64_t HITSCANCONFIG_DISPERSION_CONFIG = 0x68;  // было 0x60
inline constexpr std::uint64_t BALLISTICPROFILE_GRAVITY = 0x10;
inline constexpr std::uint64_t BALLISTICPROFILE_AIR_DRAG = 0x14;
inline constexpr std::uint64_t BALLISTICPROFILE_UNDERWATER_DRAG = 0x18;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_MAX_DISTANCE = 0x88;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_LAUNCH_SPEED = 0xA8;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_GRAVITY = 0xAC;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_AIR_DRAG = 0xB0;
inline constexpr std::uint64_t SHAFTEDPROJECTILE_UNDERWATER_DRAG = 0xB4;
inline constexpr std::uint64_t DISPERSIONCONFIG_MIN_DISPERSION = 0x20;
inline constexpr std::uint64_t DISPERSIONCONFIG_MAX_DISPERSION = 0x24;
// Отдача («без отдачи»): конфиг-ассет хитскана (поле 0x70 в
// HitscanWeaponConfig, рядом с dispersionConfig 0x68). Гасятся в ноль
// вертикальные Мин/Макс (Vector2) и боковые дельты; оригиналы «нет отдачи»
// хранит и возвращает при выключении/смене ствола (проверено по дампам
// 11949 и 11966 — раскладка одна).
inline constexpr std::uint64_t HITSCANCONFIG_RECOIL_CONFIG = 0x70;
inline constexpr std::uint64_t RECOILCONFIG_MIN_RECOIL = 0x18;   // Vector2
inline constexpr std::uint64_t RECOILCONFIG_MAX_RECOIL = 0x20;   // Vector2
inline constexpr std::uint64_t RECOILCONFIG_MIN_SIDE = 0x28;
inline constexpr std::uint64_t RECOILCONFIG_MAX_SIDE = 0x2C;

inline constexpr std::uint64_t GAME_CONTROLLER_TYPEINFO_RVA = 0xE606888; // release 11991: Oxide.GameControllerBase TypeInfo slot
// Build 11966 TypeInfoPointers gives the inner slot directly.
inline constexpr std::uint64_t WORLD_STREAMING_CONTROLLER_TYPEINFO_RVA = 0xE610328; // release 11991
inline constexpr std::uint64_t WORLD_STREAMING_GRASS_ENABLED_FIELD = 0x18; // static bool GrassEnabled
inline constexpr std::uint64_t WORLD_STREAMING_TREES_ENABLED_FIELD = 0x19; // static bool TreesEnabled
// CreateVegetationRenderer checks +0x18; StartStreaming checks +0x19 before
// constructing ChunkTreeRenderer (the terrain-tree renderer for bushes).
inline constexpr std::uint64_t GAME_CONTROLLER_LOCAL_PLAYER_FIELD   = 0x10; // <KOG>k__BackingField (PlayerManager; 11966: <ukT>)
inline constexpr std::uint64_t GAME_CONTROLLER_CAMERA_MANAGER_FIELD = 0x38; // <KOx>k__BackingField (CameraManager; 11966: <ukA>)
inline constexpr std::uint64_t CAMERA_MANAGER_CAMERA_FIELD          = 0x20; // m_Camera
// Статические поля GameControllerBase (dump11985, Oxide.GameControllerBase;
// в dump11966 позиции те же, имена <qSp>/<qSt>/<qmo>/<qme>):
// вход в UI-ветку ввода и в хендлер ввода — оба нужны «память-джойстику»
// автофарма (стик вместо синтетического тача).
//   +0x18 <KOL> = kAu (playerEventHandler локального игрока)
//   +0x20 <KOH> = Oxide.PlayerInputHandler     (экземпляр в сцене)
//   +0x50 <KOP> = ScreenControlsManager        (его +0x28 — ScreenControlsReferenceHolder)
//   +0x60 <KOO> = GameControllerBase (сам контроллер; запасной путь к UI)
// Смещения статиков считаются от klass->static_fields (см. get_class_static_fields).
inline constexpr std::uint64_t GAME_CONTROLLER_INPUT_HANDLER_FIELD      = 0x20; // <KOH> (PlayerInputHandler)
inline constexpr std::uint64_t GAME_CONTROLLER_EVENT_HANDLER_FIELD      = 0x18; // <KOL> (kAu)
inline constexpr std::uint64_t GAME_CONTROLLER_STATIC_SELF_FIELD        = 0x60; // <KOO> (GameControllerBase)
inline constexpr std::uint64_t GAME_CONTROLLER_SCREEN_CONTROLS_FIELD    = 0x50; // <KOP> (ScreenControlsManager)

// Слой ввода (dump 11949, Oxide.PlayerInputHandler / ScreenControls*).
// Джойстик движения читается игрой отсюда, а не из тача: у UI-стика
// (класс Joystick / FixedJoystick / FloatingJoystick — ассет Joystick Pack,
// неймспейс пустой) есть собственное поле-значение input, которое живёт между
// кадрами (OnDrag его пишет, OnPointerUp обнуляет) — запись в него и есть
// «двинуть стик», после чего игра сама разложит вектор по своему вводу.
//   Joystick Pack: handleRange 0x54, deadZone 0x58, axisOptions 0x5C,
//                  snapX/Y 0x60/0x61, background 0x68, handle 0x70,
//                  baseJoystick 0x78, baseRect 0x80, canvas 0x88, cam 0x90,
//                  input(Vector2) 0x98, Vector3 0xA0, m_ControlPath 0xB0.
// Осторожно: у игры есть СВОЙ класс Oxide.InputSystem.Joystick (TDI 9815) с
// полями m_MovementRange 0x20 .. float 0x48 — там 0x98 не поле, и писать в
// него нельзя. Поэтому компонент опознаём по имени класса, а неймспейс
// требуем пустой (class_identity), плюс структурная проверка полей стика.
inline constexpr std::uint64_t JOYSTICK_INPUT_VALUE           = 0x98; // Vector2 input (то, что отдаёт Direction)
inline constexpr std::uint64_t JOYSTICK_HANDLE_RANGE          = 0x54;
inline constexpr std::uint64_t JOYSTICK_DEAD_ZONE             = 0x58;
inline constexpr std::uint64_t JOYSTICK_BACKGROUND            = 0x68; // RectTransform
inline constexpr std::uint64_t JOYSTICK_HANDLE                = 0x70; // RectTransform
inline constexpr std::uint64_t JOYSTICK_BASE_JOYSTICK         = 0x78; // GameObject
// ScreenControlsReferenceHolder: GameObject стиков (+ кнопки 0x20..0x70).
inline constexpr std::uint64_t SCREEN_CONTROLS_HOLDER_FIXED    = 0x78; // GameObject FixedJoystick
inline constexpr std::uint64_t SCREEN_CONTROLS_HOLDER_FLOATING = 0x90; // GameObject FloatingJoystick
// ScreenControlsManager.screenControlsReferenceHolder (0x28) — тот же слот,
// что и у PlayerInputHandler.screenControlsReferenceHolder.
inline constexpr std::uint64_t SCREEN_CONTROLS_HOLDER_FIELD    = 0x28;
// PlayerInputHandler (класс с читаемым именем, Oxide.PlayerInputHandler):
//   controller 0x20, screenControlsReferenceHolder 0x28,
//   <qNU> PlayerManager 0x30 (обратная проверка владельца),
//   <qNF> Vsh 0x38, PlayerInput qNa 0x40 (struct 0x28 байт: MovementInput
//   Vector2 0x40, LookDirection Vector3 0x48, YRotation 0x54, Jump/Sprint/
//   Crouch/Aim/Ready 0x58..0x5C, Tick 0x60, TooClose 0x64),
//   bool 0x68 — «кнопка атаки зажата» (защёлка: ставится по нажатию кнопки
//   атаки и снимается по отпусканию, см. PLAYER_INPUT_HANDLER_ATTACK_HELD),
//   bool 0x69 — однокадровый импульс в PlayerInput.Jump (0x58),
//   Nullable<Vector2> qNf 0x6C и qNP 0x78 (сырой ввод стика: hasValue по +0,
//   значение по +4), Nullable<bool> 0x84, floats 0x88/0x8C/0x90,
//   SprintJoystickHandler 0xB0.
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_OWNER_BACKREF = 0x30; // <qNU> PlayerManager
inline constexpr std::uint64_t PLAYER_INPUT_MOVEMENT_INPUT        = 0x40; // PlayerInput.MovementInput (Vector2)
inline constexpr std::uint64_t PLAYER_INPUT_YROTATION             = 0x54; // PlayerInput.YRotation (float)
inline constexpr std::uint64_t PLAYER_INPUT_TICK                  = 0x60; // PlayerInput.Tick (uint)
// Кнопка атаки зажата (bool у хендлера; защёлка, а не импульс: держится,
// пока кнопку не отпустят). Пока там 1, хендлер ввода в КАЖДОМ кадре зовёт
// PlayerEventHandler.AttackContinuously (kAu+0x1F0).Try() -> FPManager
// .OnTry_Attack(true) -> оружие.TryAttackContinuously() — ровно то, что даёт
// зажатая кнопка атаки в UI; частоту режет кулдаун самого оружия.
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_ATTACK_HELD   = 0x68; // bool: атака зажата
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_OVERRIDE_A    = 0x6C; // Nullable<Vector2> qNf
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_OVERRIDE_B    = 0x78; // Nullable<Vector2> qNP
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_SPRINT_OVERRIDE = 0x84; // Nullable<bool> Jjb/Oua
// Layout Nullable<T> в il2cpp: bool hasValue по +0 (в метадате поле идёт
// первым), значение — по +4 (Vector2 выравнивается по 4).
inline constexpr std::uint64_t NULLABLE_HAS_VALUE = 0x0;
inline constexpr std::uint64_t NULLABLE_VALUE     = 0x4;
inline constexpr std::uint64_t NULLABLE_STRIDE    = 0x0C; // размер Nullable<Vector2>

// БЕГ. Персонаж бежит только тогда, когда игра считает кнопку бега нажатой.
// Оценщик спринта PlayerInputHandler.glv (RVA 0x67E3574 — сборка 11989,
// в 11991 съехал; зовётся из glI на
// 0x67E47D4) в КАЖДОМ кадре переписывает PlayerInput.Sprint (хендлер+0x59)
// как «стойка стика && кнопка_бега»:
//   w22 = !(|MovementInput.x| >= 0.9 || MovementInput.y <= -0.1)  (порог из glv)
//   Sprint = w22 && InputAction(LD+0x48).inProgress   — во ВСЕХ трёх SprintMode
// (0 Button / 1 Hybrid / 2 Swipe), то есть одной оттянутой ручки стика для бега
// мало: нужен inProgress у акции. Кнопку на экране жмёт SprintJoystickHandler
// (: OnScreenControl) — бот тачем не играет, поэтому ведём фазу акции в памяти:
//   PlayerInputHandler+0x98  <qNJ> LD (обёртка нового Input System)
//   LD+0x48                  InputAction кнопки бега (то, что читает glv)
//   InputAction+0xC0         int m_ActionIndexInState
//   InputAction+0xC8         InputActionMap -> +0x60 InputActionState
//   InputActionState+0x60    TriggerState* m_ActionStates (шаг 0x44)
//   TriggerState+0x00        InputActionPhase phase (byte: 2 = Started, 1 = Waiting)
// Читает её InputAction.IsInProgress (0xC74CF84) / get_inProgress (0xC74BF3C)
// как (phase & 0xFE) == 2, а TriggerState берётся по
// &actionStates[action->0xC0] (InputActionState.FetchActionState 0xC767658).
// Цепочку и раскладку Input System (пакет Unity, ассембли не обфусцирован)
// сверяем на месте: имена классов LD/InputAction, диапазон индекса, ненулевые
// указатели и фаза в пределах enum — не сошлось, не пишем.
inline constexpr std::uint64_t PLAYER_INPUT_HANDLER_INPUT_ACTIONS = 0x98; // <KVg> Zp (11966: <qNJ> LD)
inline constexpr std::uint64_t PLAYER_INPUT_SPRINT                 = 0x59; // PlayerInput.Sprint (bool)
inline constexpr std::uint64_t LD_SPRINT_ACTION                    = 0x48; // InputAction кнопки бега
inline constexpr std::uint64_t INPUT_ACTION_INDEX                  = 0xC0; // int, индекс в InputActionState
inline constexpr std::uint64_t INPUT_ACTION_STATE_HOLDER           = 0xC8; // +0x60 -> InputActionState
inline constexpr std::uint64_t INPUT_ACTION_STATE_HOLDER_STATE     = 0x60;
inline constexpr std::uint64_t INPUT_ACTION_STATE_STATES           = 0x60; // TriggerState* (шаг ниже)
inline constexpr std::uint64_t INPUT_ACTION_STATE_STRIDE           = 0x44; // sizeof(TriggerState)
inline constexpr std::uint64_t INPUT_ACTION_PHASE_OFFSET           = 0x00;
inline constexpr std::uint8_t  INPUT_ACTION_PHASE_WAITING          = 1;    // InputActionPhase.Waiting
inline constexpr std::uint8_t  INPUT_ACTION_PHASE_STARTED          = 2;    // InputActionPhase.Started

// Oxide.PlayerManager instance fields (dump.cs)
inline constexpr std::uint64_t PLAYER_TRANSFORM = 0x68; // worldCameraRoot
// PlayerManager.lastSavedPosition. Совпадает в release 11949 и beta 11943;
// 0x260 в этих билдах — ссылка fz, поэтому чтение Vector3 оттуда всегда
// сваливалось на более тяжёлый KCC/model path.
inline constexpr std::uint64_t PLAYER_POSITION = 0x1F4;
inline constexpr std::uint64_t PLAYER_CHARACTER_MODEL = 0x150; // characterModel (UnityEngine.GameObject)
inline constexpr std::uint64_t PLAYER_NICKLABEL = 0x130; // nicklabel (class Di в 11985; был Il, ранее ij/wK)
// PlayerManager "uWc" (0x220, was LLI): the real human-readable display name (confirmed
// on-device — equals the nicklabel widget's private name string, e.g.
// "пахановский" / "#Фришка"), whereas userID/voice names carry the machine id.
inline constexpr std::uint64_t PLAYER_DISPLAY_NAME = 0x240;  // 11985: string KSQ (11966: zNy); ВНИМАНИЕ: 0x220 — enum HackType lastViolationType, вставка +0x20 до animator

// nicklabel (Di в 11985; gt/ij/wK в прежних): player back-ref + nickname UI.Text.
inline constexpr std::uint64_t NICKLABEL_PLAYER_BACKREF = 0x20;
inline constexpr std::uint64_t NICKLABEL_NICKNAME_TEXT  = 0x38;
// UnityEngine.UI.Text: managed string with the visible nickname.
inline constexpr std::uint64_t UI_TEXT_MTEXT = 0xE0;
// FPObject (base of FPWeaponBase): display name of the held weapon.
inline constexpr std::uint64_t FPOBJECT_OBJECT_NAME = 0x78;
// Dissonance voice identity: reliable synced display-name source.
// PlayerManager.uWr (0x2E8, was LLT) : VoicePlayerState -> <Name>k__BackingField.
inline constexpr std::uint64_t PLAYER_VOICE_STATE  = 0x338;  // 11985: VoicePlayerState KSm (хвост PM сдвинут +0x10; 11966: zsd @0x328) — единственное поле типа Dissonance.VoicePlayerState в классе
inline constexpr std::uint64_t VOICE_STATE_NAME     = 0x38;
// PlayerManager.voicePlayer (kIq в 11985; WDB/pJk/fuI в прежних) -> string at 0x78 (rEV).
inline constexpr std::uint64_t PLAYER_VOICE_PLAYER = 0x140;
inline constexpr std::uint64_t VOICE_PLAYER_TAG     = 0x78;
// FPObject -> Oxide.Item -> Oxide.ItemData display strings.
inline constexpr std::uint64_t FPOBJECT_ITEM        = 0x40; // <KHo>k__BackingField (11966: <Ltl>)
// FPObject.uhq (+0x38): «вюха показана». Ставится в 1 On_Draw (слот vtable 17,
// 0x68FD250), сбрасывается в 0 Holster-цепочкой FDN (слот 10, 0x68FCD10).
// Здоровый инвариант, доказанный дизассемблером dump11966: uhq==1 тогда и
// только тогда, когда Item(+0x40) != null. Нарушение (uhq==0 при Item==null
// десятки секунд) — signature полусобранной вьюхи после исключения в сборке
// (KERNEL.md §4.22): воротa CreateFPWeapon (0x68EF940) видят uhq==0 и
// каждый дроу гонят пересборку, которая снова падает на том же скине.
inline constexpr std::uint64_t FPOBJECT_SHOWN       = 0x38; // <KHI>k__BackingField (11966: <uhq>)
inline constexpr std::uint64_t ITEM_DATA            = 0x20; // <KrT>k__BackingField (11966: <LIN>)
inline constexpr std::uint64_t ITEMDATA_NAME        = 0x18; // m_Name
inline constexpr std::uint64_t ITEMDATA_SHORTNAME   = 0x20; // m_ShortName
// ItemData.m_Id (+0x58): уникальный id предмета (у нескольких пушек одного
// family класс общий, id — нет).
inline constexpr std::uint64_t ITEMDATA_ID          = 0x58; // m_Id
// Il2Cpp System.String layout: int32 length @0x10, UTF-16 chars @0x14.
inline constexpr std::uint64_t IL2CPP_STRING_LENGTH = 0x10;
inline constexpr std::uint64_t IL2CPP_STRING_CHARS  = 0x14;

// Local player "is aiming" (ADS) state (dump.cs + libil2cpp disasm):
//   PlayerManager.playerEventHandler (0x78) -> class WxG (was Gum/DqO)
//   handler.manager (0xD0) back-ref == PlayerManager (validation)
//   handler.Aim (0x270, was 0x268) -> toggle activity (class Gub, was Dqg), its
//   <...>k__BackingField (0x10) == Active
//   This exact byte is what FPManager.LateUpdate reads to blend the camera to aimFOV.
//   The handler class is a live target: builds add/remove activities in the middle
//   of it. A KnockDoor field inserted at 0x188 shifted every later field by +8, so
//   Aim moved 0x268 -> 0x270 and 0x268 became Jump ("only in scope" silently read
//   the jump flag). offsets_map.json reaches these through `via`
//   (playerEventHandler -> Aim), so update_offsets.py verifies them by field name.
// Fallback: PlayerManager.fpManager (0x90) -> FPManager.KLY (0x58, current FPWeaponBase)
//   FPObject.Player (0xC0) back-ref == PlayerManager (validation)
//   FPWeaponBase.<Kua>k__BackingField (0x120) == isAiming (set in weapon StartAim)
//   FPManager.<KLR>k__BackingField (0xB8) == aim blend 0..1 (secondary hint)
inline constexpr std::uint64_t PLAYER_EVENT_HANDLER          = 0x78;  // тип rIf (11989: kAu; 11966: hsL; 11991: база rIF)
inline constexpr std::uint64_t EVENT_HANDLER_MANAGER_BACKREF = 0xD0;
inline constexpr std::uint64_t EVENT_HANDLER_AIM_ACTIVITY    = 0x2B0;  // 11985: Aim kAj @0x2B0 — вставки Footstep/StepLength/SetVehicleLock сдвинули его с 0x298 (как в бете 11977); AttackContinuously @0x1F0 не сдвинут
// eventHandler.MovementInput (0x128): Vsf<Vector2> — «канал движения» игрока
// (вход KCC), ровно как LookDirection (0x140) для взгляда. Значение внутри
// обёртки — по SYNC_VALUE_OFFSET (+0x20). Отдаём его памяти-джойстику
// автофарма (farm_joy.cpp) как один из кандидатов записи.
inline constexpr std::uint64_t EVENT_HANDLER_MOVEMENT_INPUT  = 0x128;
// Remote-player held-weapon candidates (dump.cs Oxide.PlayerManager). The FP
// manager (0x90) is a local MonoBehaviour and is often empty for other players,
// so these are the synced/inventory-backed fallbacks to probe:
//   0x98  inventory            (Oxide.PlayerInventory) — все три подтверждены именами в новом дампе
//   0xF0  weaponReference      (private; likely the current remote weapon)
//   0x198 weapons              (private array of weapon objects)
// Признак смерти игрока: Oxide.PlayerManager.respawning (bool).
// Источник — РЕЛИЗНЫЙ дамп (dump.cs, struct Oxide_PlayerManager_Fields):
//     bool respawning;   // 0x208
// Стоит единицей, пока игрок мёртв и ждёт возрождения; живой игрок держит
// ноль. Поле на самом PlayerManager, который уже есть в списке кадров, —
// поэтому читается одним чтением, без цепочек. Рядом lastDeathPosition
// (Vector3, 0x1E0) и playerDeathHandlerReference (0x80), но для «не целиться
// в труп» довольно этого флага.
inline constexpr std::uint64_t PLAYER_MANAGER_RESPAWNING = 0x228;  // было 0x208; bool после nextTerrainPosition(0x224)
// Собственный признак смерти игры, а не догадка по позе. Релизный дамп:
//     Oxide.PlayerManager.playerDeathHandlerReference   0x80
//     Oxide.PlayerDeathHandler.death  (bool, synced)    0xF0
// Цепочка через обёртку Reference подтверждена дизассемблером:
//   PlayerManager.get (RVA 0x66bd14c): ldr x8, [x19, #0x80]; cbz; ldr x0, [x8, #0x18]; ret
//   PlayerDeathHandler.get_death (RVA 0x6613d98): ldrb w0, [x0, #0xf0]; ret
//   PlayerDeathHandler.set_death (RVA 0x6612a28): and w0, w19, #1 / add x1, x20, #0xf0
// Нужен потому, что поза ловит не всех: лог 20:45 — из 149 смен цели 14
// пришлись на игроков с здоровьем 0.0, но СТОЯЩИХ (высота 0.86-1.51 м).
inline constexpr std::uint64_t PLAYER_DEATH_HANDLER         = 0x80;
inline constexpr std::uint64_t DEATH_HANDLER_REF_TARGET     = 0x18;
inline constexpr std::uint64_t DEATH_HANDLER_DEATH          = 0xF0;
// Цепочка здоровья (релизный дамп):
//   Oxide.PlayerManager.vitals            (Oxide.PlayerVitals*) 0xC8
//   Oxide.GenericVitals.KQN               (float, protected)    0xB8
// m_MaxHealth лежит там же на 0x88, поэтому 0xB8 — текущее здоровье; имя
// обфусцировано, сверялись позиционно по dump.cs (class Oxide.GenericVitals).
inline constexpr std::uint64_t PLAYER_VITALS       = 0xC8;
inline constexpr std::uint64_t VITALS_HEALTH       = 0xB8;
inline constexpr std::uint64_t EVENT_HANDLER_HEALTH = 0x98; // rIF.Health (11991; было kAH/kAK<float, ...>; 11966: hsN/hkz), Value at +0x20 (HEALTH_VALUE)
inline constexpr std::uint64_t PLAYER_UKX          = 0x260; // Yf KSL (model wrapper; 11966: Eo uKx), modelInfo at +0x10
inline constexpr std::uint64_t UKX_MODEL_INFO      = 0x10;  // PlayerModelInfo* inside the wrapper (<Pqp>k__BackingField)
// Урон от падения (dump.cs): Oxide.EntityVitals : GenericVitals
//   [SerializeField][Range(1, 30)] protected float m_MinFallSpeed;  0xC0
//   [SerializeField][Range(1, 60)] protected float m_MaxFallSpeed;  0xC4
//   public SoundPlayer m_FallDamageAudio;                           0xD8
// Имена полей в дампе НЕ обфусцированы (SerializeField), поэтому значения
// снимались напрямую: релиз 11966 — class Oxide.EntityVitals строка 466678,
// бета 11968 — строка 454330, и там, и там 0xC0/0xC4. Наследник
// Oxide.PlayerVitals : EntityVitals (11966: 467618, 11968: 455117) своих
// float-полей перед 0xC0 не добавляет, так что слот общий для обеих сборок.
// ОСТОРОЖНО: 0xC0 сам по себе объект не опознаёт — у ECM.BaseCharacterController
// там своё поле (gravityMagnitude-подобный float), идентифицируем vitals только
// по классу и бэклинку PlayerVitals.Player (0x100) == player.
inline constexpr std::uint64_t VITALS_MIN_FALL_SPEED  = 0xC0;
inline constexpr std::uint64_t VITALS_MAX_FALL_SPEED  = 0xC4;
inline constexpr std::uint64_t VITALS_PLAYER_BACKREF  = 0x100; // PlayerVitals.Player
inline constexpr std::uint64_t PLAYER_INVENTORY         = 0x98;
inline constexpr std::uint64_t PLAYER_WEAPON_REFERENCE  = 0xF0;  // без изменений
inline constexpr std::uint64_t PLAYER_WEAPONS_ARRAY     = 0x1B8;
// Oxide.PlayerInventory instance fields (dump.cs).
inline constexpr std::uint64_t INV_PLAYER_INVENTORY_DATA = 0x20; // _playerInventoryData
inline constexpr std::uint64_t INV_PLAYER_INVENTORY_CLIENT = 0x28; // _playerInventoryClient

// ---- Сверка с dump.cs (11949 релиз и 11957 бета) ----------------------------
// Профиль ниже проверен против Il2CppDumper-дампов обоих билдов — имена классов
// там разные (обфускация ротируется), поля и адреса совпадают, и искать класс в
// новом билде надо по форме, а не по имени:
//   WeaponPiece (9708 / 9787): Enabled 0x0, Number 0x2, Mods 0x8 — layout
//     совпадает. Но Number — короткий индекс
//     в перечислении предметов (Oxide.ItemDatabase 9659: m_Categories 0x18 ->
//     ItemCategory 9647: m_Items 0x18 -> ItemData), а не постоянный id: у самого
//     предмета id лежит в m_Id 0x58 и он int. Поэтому подписи по номеру —
//     последняя подстраховка (weapon_label_for_item_id в game.cpp).
//   ItemData (9653 / 9731): m_Name 0x18, m_ShortName 0x20, m_Id 0x58 — те же
//     поля, что читают ITEM_DATA/ITEMDATA_NAME/ITEMDATA_SHORTNAME ниже.
//   ItemDatabase (9659 / 9737): m_Categories 0x18, m_ById 0x28, m_ByShortName
//     0x30 — то есть поиск предмета по индексу в перечислении неустойчив, а по
//     m_Id/shortname — нет.
//   Oxide.PlayerEventHandler переименован (11949 — Vsh/9369, 11957 — AZn/9447),
//     имена полей не обфусцированы: LookDirection 0x140, LastLocalHitPoint
//     0x170, LastLocalHitTime 0x178, все трое Vsf<T> (значение в +0x20) — по
//     этой тройке класс и находится в новом билде. В обоих дампах на него
//     ссылается PlayerInputHandler (поле 0x38).
//   PlayerInputHandler (9310 / 9388): private bool на 0x68 — адрес
//     PLAYER_INPUT_HANDLER_ATTACK_HELD подтверждён в обоих билдах (имя поля
//     ротируется: qNz -> PmQ).
//   PlayerManager (9722 / 9801): bool respawning 0x228 сразу за
//     nextTerrainCheck 0x224 — подтверждён в обоих билдах.
//   FPWeaponBase: bool 0x120 и float 0x124 — FPWEAPON_IS_AIMING /
//     FPWEAPON_SHOT_PERIOD подтверждены в обоих билдах (0x118 там синхронизиро-
//     ванный wrapper, 0x121 — второй bool того же блока).
// Имён предметов в дампе нет: ItemData — ScriptableObject, строки живут в
// global-metadata.dat и бандлах, поэтому items.txt за таблицей номеров всё
// равно снимается с устройства.
// ---- Remote (third-person) held weapon, from dump.cs ------------------------
// The FP objects above are local-only MonoBehaviours, so enemies never resolve
// through them. What every client *does* get is the networked weapon component:
//   HyperHug.Games.Oxide.Features.Weapons.PlayerWeapon : Mirror.NetworkBehaviour
//     playerWeaponViewReference  0xD0  (-> class sR, third-person weapon view)
//     Oxide.WeaponPiece          0x100 (SyncVar "weaponPiece", 0x10 bytes)
//     WeaponState                0x110
//     <player>k__BackingField    0x128 (back-ref to PlayerManager, validation)
// It hangs off PlayerManager.weaponReference (0xF0), which is an obfuscated
// lazy-reference wrapper just like kccReference, so it needs the same probing.
// This build inserted 0x40 bytes of new fields before `animator`, so every
// field from there on moved (old 0x90/0xD8/0xE8/0x100). The SyncVar trio was
// re-identified positionally against the _Mirror_SyncVarHookDelegate__loaded /
// _weaponPiece / _weaponState tail, not by name (names are re-obfuscated).
// В PlayerWeapon лежат ДВЕ копии WeaponPiece: SyncVar (wDN @0x130) и «рабочая»
// (wDT @0xD0). В 11985 живой путь дроу читает SyncVar (drawSV: `ldr q0,
// [x19,#0x130]` и `ldr w2,[x19,#0x150]`, затем FPManager.LTI), рабочую копию
// пишет только серверный RPC ceC (на устройстве не наблюдался). Для подписей
// оружия читается SyncVar.
inline constexpr std::uint64_t PLAYERWEAPON_VIEW           = 0x100; // 11985: Yp playerWeaponViewReference (тип-интерфейс; реализация YU, декоратор kun); 11966: 0xE8
inline constexpr std::uint64_t PLAYERWEAPON_PIECE          = 0x130; // SyncVar wDN; 11966: 0x110 (QVW)
inline constexpr std::uint64_t PLAYERWEAPON_PLAYER_BACKREF = 0x160; // 11966: 0x138
// Oxide.WeaponPiece (value type): Enabled 0x0, Number 0x2. Размер 0x18 — piece ВСЕГДА передаётся по ссылке
// (q0 = [0x00..0x10), x8 = [0x10..0x18)), а не одним регистром.
inline constexpr std::uint64_t WEAPONPIECE_ENABLED = 0x00;
inline constexpr std::uint64_t WEAPONPIECE_NUMBER  = 0x02;
// The third-person weapon view (11985: class YU : Yp, TDI 4515; 11966: ON,
// ранее sR/Mo. Интерфейс Yp, был OB/sY/Ms; декоратор kun (sealed : Yp,
// единственное поле Yp @0x10), был WRU/pGW/fSN).
// После каждой ротации имён класс ищется ПО ФОРМЕ ПОЛЕЙ, не по имени:
// единственная реализация интерфейса вьюхи с тройкой
//   WeaponBase  -> MonoBehaviour на спавненном weapon GameObject
//   WeaponPiece -> копия piece вьюхи
//   Transform   -> root transform спавна
// В 11985 тройка уехала на +8 (как в бете 11977): 0x48/0x50/0x60 -> 0x50/0x58/0x70.
// Декоратор по-прежнему заворачивает настоящую вьюху в своё первое поле
// (+0x10) — при чтении разворачиваем один уровень (weapon_name_from_view).
inline constexpr std::uint64_t WEAPONVIEW_WEAPON_BASE    = 0x50;  // YU.PkQ WeaponBase
inline constexpr std::uint64_t WEAPONVIEW_ROOT_TRANSFORM = 0x70;  // YU.<Pks>k__BackingField Transform
inline constexpr std::uint64_t WEAPONVIEW_INNER          = 0x10;  // kun.OmF (Yp) — завёрнутая вьюха
// HyperHug...Player.PlayerModelInfo — the character model rig. The equipment
// view parents the weapon prefab under these holders, so their first child is
// the weapon GameObject (name = prefab name).
//   head 0x20, rightWeaponHolder 0x28, leftWeaponHolder 0x30,
//   equipmentHolder 0x38, characterAnimation 0x60
inline constexpr std::uint64_t MODELINFO_RIGHT_WEAPON_HOLDER = 0x28;
inline constexpr std::uint64_t MODELINFO_LEFT_WEAPON_HOLDER  = 0x30;
inline constexpr std::uint64_t MODELINFO_HEAD                = 0x20;
inline constexpr std::uint64_t MODELINFO_EQUIPMENT_HOLDER    = 0x38;
inline constexpr std::uint64_t MODELINFO_HEAD_MODEL          = 0x48;  // headModel (Transform под головой)
// HyperHug...NPC.NPCBrainHumanoid.modelInfo (0x148) -> NPCHumanoidModelInfo,
// the bot rig (head 0x20, body 0x40, headModel 0x48). Same layout in release
// 11946 and beta 11943 dumps; the class name at runtime is unqualified.
inline constexpr std::uint64_t NPC_BRAIN_MODEL_INFO          = 0x148;
// CharacterAnimation.playerModelInfo (0x30) and PlayerInventoryData
// .playerModelInfo (0x18) are the two ways to reach PlayerModelInfo.
inline constexpr std::uint64_t CHARANIM_PLAYER_MODEL_INFO = 0x30;
inline constexpr std::uint64_t INVDATA_PLAYER_MODEL_INFO  = 0x18; // release 11949: PlayerInventoryData.playerModelInfo
// Il2CppClass: name at 0x10, namespace at 0x18 (used to identify PlayerWeapon).
inline constexpr std::uint64_t IL2CPP_CLASS_NAME      = 0x10;
inline constexpr std::uint64_t IL2CPP_CLASS_NAMESPACE = 0x18;
// eventHandler.LookDirection (0x140): synced Vector3 wrapper, value at +0x20.
// MouseLook.Update writes m_LookRoot.forward into it every frame and
// FPHitscan casts its hit ray along it (not along the camera transform).
inline constexpr std::uint64_t EVENT_HANDLER_LOOK_DIRECTION  = 0x140;
// Отметка выстрела: FPHitscan.ZDW(Ray) кладёт сюда точку попадания (0x170) и
// время (0x178) — по ним в логе видно, когда именно стреляли и что лежало в оси.
// В дампе 11949 это Vsh (typedef 9369): LastLocalHitPoint / LastLocalHitTime,
// оба Vsf<float>/Vsf<Vector3> ⇒ SYNC_VALUE_OFFSET обязателен.
inline constexpr std::uint64_t EVENT_HANDLER_LAST_HIT_POINT  = 0x170;
inline constexpr std::uint64_t EVENT_HANDLER_LAST_HIT_TIME   = 0x178;
inline constexpr std::uint64_t SYNC_VALUE_OFFSET             = 0x20;
inline constexpr std::uint64_t ACTIVITY_ACTIVE_FLAG          = 0x10;
inline constexpr std::uint64_t PLAYER_FP_MANAGER             = 0x90;
// 11985: в FPManager вставлен AssetReference KLE @0x80 (загрузка ассета
// вьюхи, как в бете 11977) — всё, что лежало с 0x78, уехало на +0x10.
inline constexpr std::uint64_t FPMANAGER_CURRENT_WEAPON      = 0x58; // GsJ FPWeaponBase (11989: KLY; 11966: uzN) — типизированный кэш, через него проходит зомби-резолв §4.24
inline constexpr std::uint64_t FPMANAGER_CURRENT_OBJECT      = 0x50; // _currentWeapon (authoritative active FPObject)
inline constexpr std::uint64_t FPMANAGER_UZL                 = 0x68; // KLd WeaponPiece (11966: uzL) последней сборки, дошедшей до TAIL; поля — WEAPONPIECE_* (Number = +0x6A)
inline constexpr std::uint64_t FPMANAGER_CTS                 = 0x88; // KLm CancellationTokenSource (11966: uzf @0x78): LTI/Kxi создаёт новый на каждый вызов (`str x,[x0,#0x88]!` @0x6D598CC) — смена указателя = дроу дошёл до Kxi
inline constexpr std::uint64_t FPMANAGER_CTS2                = 0x90; // KLl второй CTS (тот же жизненный цикл; 11966: uzi @0x80)
inline constexpr std::uint64_t FPMANAGER_UTCS                = 0x98; // KLI UniTaskCompletionSource (11966: uzD @0x88): пересоздаётся в LTI/Kxi (`str x,[x0,#0x98]!` @0x6D598A8)
inline constexpr std::uint64_t FPMANAGER_UZJ                 = 0xA0; // bool KLo (11966: uzj @0x90)
inline constexpr std::uint64_t FPMANAGER_UHF                 = 0xC0; // <KHk>k__BackingField bool (11966: <uhF> @0xB0)
inline constexpr std::uint64_t FPMANAGER_AIM_BLEND           = 0xB8; // <KLR>k__BackingField float (11966: <uhh>/LtX @0xA8)
inline constexpr std::uint64_t FPOBJECT_PLAYER_BACKREF       = 0xC0;
inline constexpr std::uint64_t FPWEAPON_IS_AIMING            = 0x120;

// Максимальная дальность удара ближним орудием (топор/кирка/пила/копьё).
// В dump11966 у Oxide.FPMelee добавлены два int-поля по 0x128/0x12C,
// поэтому m_MaxReach и hitRadius сдвинулись на +8: 0x130 и 0x134.
// Игра засчитывает удар в FPMelee.ZkX() ровно так:
//   GKo data = handler.RaycastData (0x160), если невалиден — handler.AimRaycast (0x168)
//   if (data.RaycastHit.distance < m_MaxReach + hitRadius) On_Hit(data); else On_Woosh();
// distance — это UnityEngine.RaycastHit.get_distance(), то есть 3D-метры ОТ
// КАМЕРЫ (луч строит GuL.ZJP из позы камеры/головы), а не по горизонтали.
// Значения сериализованы в префабе каждого инструмента; в конструкторе FPMelee
// стоят заглушки m_MaxReach=0.5, hitRadius=0.1 (m_TimeBetweenAttacks=0.85,
// m_DamagePerHit=15, m_ImpactForce=15), поэтому читать их надо из живого объекта.
// Лучи кастует Oxide.RaycastManager (FPObject.RaycastManager, 0x90):
//   m_RayLength (0x38, default 1.5) — длина обычного луча -> RaycastData;
//   m_AimRayLength (0x3C) и радиус сферы (0x40) — при доставании орудия
//   FPMelee.On_Draw кладёт туда свои m_MaxReach и hitRadius (RaycastManager.ZIj),
//   сфера -> AimRaycast; m_TooCloseThreeshold (0x44, default 1.0).
inline constexpr std::uint64_t FPOBJECT_RAYCAST_MANAGER      = 0x90;
inline constexpr std::uint64_t FPOBJECT_EVENT_HANDLER        = 0xC8; // Gum (PlayerEventHandler)
inline constexpr std::uint64_t FPMELEE_MAX_REACH             = 0x130;
inline constexpr std::uint64_t FPMELEE_HIT_RADIUS            = 0x134;
// Ритм ударов самого орудия (секунды, сериализованы в префабе; в конструкторе
// FPMelee стоят 0.85 и 0.15). Тап быстрее m_TimeBetweenAttacks игра ставит в
// очередь и часть ударов съедает, медленнее — простой; поэтому такт бота берём
// отсюда, а не из подобранных на глаз миллисекунд.
inline constexpr std::uint64_t FPMELEE_TIME_BETWEEN_ATTACKS  = 0x138;
inline constexpr std::uint64_t FPMELEE_PAUSE_AFTER_ATTACK    = 0x13C;
// Oxide.FPTool : Oxide.FPMelee. The expanded release 11966 base shifts these
// FPTool-specific fields by +8 (база 11985 идентична 11966 — сверено);
// beta keeps its previous profile values.
inline constexpr std::uint64_t FPTOOL_ALWAYS_CRIT            = 0x170; // bool, FPTool.alwaysCrit
inline constexpr std::uint64_t FPTOOL_TOOL_PURPOSES          = 0x168; // enum ToolPurpose, флаги
// Рядом (не читаем): 0x16C m_Efficiency, 0x170 alwaysCrit, 0x171 useAmmo,
// 0x174 attacksPerAmmo.
inline constexpr std::uint64_t RAYCASTMAN_PLAYER             = 0x20;
inline constexpr std::uint64_t RAYCASTMAN_RAY_LENGTH         = 0x38;
inline constexpr std::uint64_t RAYCASTMAN_AIM_RAY_LENGTH     = 0x3C;
inline constexpr std::uint64_t RAYCASTMAN_SPHERE_RADIUS      = 0x40;
inline constexpr std::uint64_t RAYCASTMAN_TOO_CLOSE          = 0x44;
inline constexpr std::uint64_t RAYCASTMAN_LAYER_MASK         = 0x48;  // RaycastManager.m_LayerMask (LayerMask, 0x6916204)
inline constexpr std::uint64_t RAYCASTMAN_AIM_LAYER_MASK     = 0x4C;  // RaycastManager.m_AimLayerMask (LayerMask, 0x69165BC)
// Что прямо сейчас видит прицел — результат лучей RaycastManager, разложенный
// в активности PlayerEventHandler (класс Gum). FPMelee.ZkX берёт их ровно так
// (дизассемблер 0x6533a20):
//   handler = weapon[0xC8]; data = handler[0x160][0x20]  (RaycastData)
//   if (data == null)       data = handler[0x168][0x20]  (AimRaycast)
//   if (data != null && data.RaycastHit.distance < m_MaxReach + hitRadius) Hit
// «Валидность» — это просто data != null (геттер 0x654fc58: cmp x0,#0; cset).
// Отсюда же берём «не перекрыт ли узел»: если луч игры упёрся ближе, чем наша
// точка прицела, удар уйдёт в перекрытие, а не в ресурс.
inline constexpr std::uint64_t GUM_RAYCAST_DATA              = 0x160; // kAr<kAm> (11966: GuI`1<GKo>)
inline constexpr std::uint64_t GUM_AIM_RAYCAST               = 0x168; // kAr<kAm>
inline constexpr std::uint64_t GUI_VALUE                     = 0x20;  // значение внутри kAr<T> (форма обёртки та же: Action 0x10, Filter 0x18, value 0x20)
inline constexpr std::uint64_t GKO_HIT_OBJECT                = 0x18;  // GameObject попадания (kAm.<KjU>; 11966: uMU/xFg)
inline constexpr std::uint64_t GKO_HIT_ENTITY                = 0x20;  // kAl BaseEntity попадания (kAm.<KjY> kAl; 11966: hkO uMB)
inline constexpr std::uint64_t GKO_HIT_PLAYER                = 0x40;  // PlayerManager попадания (kAm.<Kjl>)
inline constexpr std::uint64_t GKO_RAYCAST_HIT               = 0x48;  // UnityEngine.RaycastHit (0x2C; kAm.<KjI>)
inline constexpr std::uint64_t GKO_RAY_ORIGIN                = 0x74;  // Vector3 origin луча (kAm.<Kjo>)
inline constexpr std::uint64_t RAYCASTHIT_DISTANCE           = 0x1C;  // m_Distance внутри RaycastHit

// ---- Проверка видимости (vis_check): RaycastManager + AimAssist (kwY; 11966: hwR/YuB) --
// Сверка по дампам 11985 Release (kwY, AimTarget 8273, AimAssistConfig 8270,
// RaycastManager 9930), 11966 Release (TypeDefIndex 9814/9543/9541/7910/7912)
// и 11959 Beta (9811/9540/9538/7907/7909): все смещения совпадают.
inline constexpr std::uint64_t PLAYER_MOUSE_LOOK             = 0x70;  // PlayerManager.mouseLook (Oxide.MouseLook)
inline constexpr std::uint64_t MOUSELOOK_PITCH_YAW           = 0x4C;  // MouseLook.Ksc (Vector2 pitch/yaw; 11966: JZC)
inline constexpr std::uint64_t PLAYER_RAYCAST_MANAGER        = 0x88;  // PlayerManager.raycastManager
inline constexpr std::uint64_t PLAYER_VOICE_COLLIDER         = 0x168; // PlayerManager.voiceCollider
inline constexpr std::uint64_t PLAYER_TRIGGER_PROBE          = 0x170; // PlayerManager.triggerProbe (CapsuleCollider)
inline constexpr std::uint64_t PLAYER_AIM_ASSIST_CONFIG      = 0x180; // PlayerManager.aimAssistConfig
inline constexpr std::uint64_t PLAYER_AIM_ASSIST             = 0x258; // PlayerManager.<GcM>k__BackingField (11991: rjD; 11989: kwY; 11966: <uKm>/hwR)
inline constexpr std::uint64_t PLAYER_MODEL_INFO_REF         = 0x260; // PlayerManager.KSL (Yf wrapper, +0x10 = PlayerModelInfo; 11966: uKx/Eo)
inline constexpr std::uint64_t FPOBJECT_ENABLE_AIM_ASSIST    = 0xF8;  // FPWeaponBase.useAimAssist (bool, проверяется в FPManager::Kxq)
inline constexpr std::uint64_t KCC_MOTOR_OFFSET              = 0x20;  // BasePlayerKCC.<Motor>k__BackingField
inline constexpr std::uint64_t KCC_MOTOR_CAPSULE             = 0x28;  // KinematicCharacterMotor.Capsule (CapsuleCollider)
inline constexpr std::uint64_t NPC_BRAIN_ENTITY              = 0x68;  // kAg.Entity (kAH) на NPCBrainHumanoid (11966: hsY/hsN)
inline constexpr std::uint64_t NPC_DEATH_COLLIDERS           = 0xC8;  // NPCHumanoidDeathHandler.m_CollidersToDisable (Collider[])
inline constexpr std::uint64_t AIMTARGET_ENTITY_HANDLER      = 0x20;  // AimTarget.entityEventHandler (kAH; 11966: hsN)
inline constexpr std::uint64_t AIMCONFIG_DISTANCE            = 0x18;  // AimAssistConfig.distance (float)
inline constexpr std::uint64_t AIMCONFIG_OUTER_SPEED         = 0x38;  // AimAssistConfig.outerMaxSpeed (float)
inline constexpr std::uint64_t AIMCONFIG_INNER_SPEED         = 0x3C;  // AimAssistConfig.innerMaxSpeed (float)
inline constexpr std::uint64_t AIMCONFIG_OUTER_ABS_SPEED     = 0x40;  // AimAssistConfig.outerMaxAbsoluteSpeed (float)
inline constexpr std::uint64_t AIMCONFIG_INNER_ABS_SPEED     = 0x44;  // AimAssistConfig.innerMaxAbsoluteSpeed (float)
inline constexpr std::uint64_t AIMCONFIG_SWITCH_DELAY        = 0x48;  // AimAssistConfig.targetSwitchDelay (float)
inline constexpr std::uint64_t AIMASSIST_PLAYER              = 0x10;  // kwY.TQq (PlayerManager; 11966: hwR.tjs)
inline constexpr std::uint64_t AIMASSIST_CONFIG              = 0x18;  // kwY.TQk (AimAssistConfig)
inline constexpr std::uint64_t AIMASSIST_INIT_FLAG           = 0x20;  // kwY.TQZ (bool)
inline constexpr std::uint64_t AIMASSIST_ENABLED             = 0x21;  // kwY.TQc (bool)
inline constexpr std::uint64_t AIMASSIST_CAMERA              = 0x28;  // kwY.TQv (Camera)
inline constexpr std::uint64_t AIMASSIST_RAYCAST_HIT         = 0x30;  // kwY.TQD (RaycastHit, 0x2C)
inline constexpr std::uint64_t AIMASSIST_CUR_TARGET          = 0x60;  // kwY.TQi (AimTarget)
inline constexpr std::uint64_t AIMASSIST_CUR_SENSOR          = 0x68;  // kwY.TQQ (AimSensorData, 0x30)
inline constexpr std::uint64_t AIMASSIST_PREV_TARGET         = 0x98;  // kwY.TQV (AimTarget)
inline constexpr std::uint64_t AIMASSIST_PREV_SENSOR         = 0xA0;  // kwY.TQs (AimSensorData, 0x30)
inline constexpr std::uint64_t AIMASSIST_HIT_FLAG            = 0xDC;  // kwY.TQH (bool)
inline constexpr std::uint64_t AIMASSIST_DELTA_ROT           = 0x108; // kwY.<TQw>k__BackingField (Vector2, добавляется к lookDelta в MouseLook)
inline constexpr std::uint64_t AIMSENSOR_COLLIDER            = 0x00;  // AimSensorData.Collider
inline constexpr std::uint64_t AIMSENSOR_START_LOOK_TIME     = 0x08;  // AimSensorData.StartLookTime
inline constexpr std::uint64_t AIMSENSOR_LAST_SEEN_TIME      = 0x0C;  // AimSensorData.LastSeenTime
inline constexpr std::uint64_t AIMSENSOR_LAST_RECT_TIME      = 0x10;  // AimSensorData.LastRectTime
inline constexpr std::uint64_t AIMSENSOR_DISTANCE            = 0x14;  // AimSensorData.DistanceFromCamera
inline constexpr std::uint64_t AIMSENSOR_POSITION            = 0x18;  // AimSensorData.Position (Vector3)
inline constexpr std::uint64_t AIMSENSOR_DIRECTION           = 0x24;  // AimSensorData.Direction (Vector3)

// ---- Проверка видимости всех игроков в 200м: NearbyPlayerScreenMarkerView + OcclusionModule ----
// Na::Tick (0x5CFD528) создаёт Marker (MarkerType.NearbyPlayer = 10, данные NV/Iu)
// для каждого не-союзника/не-соклановца в радиусе 200м. ScreenMarkerPresenter::LateTick
// (0x5D14C30) каждый кадр вызывает NearbyPlayerScreenMarkerView::Tick -> OcclusionModule::Tick
// (0x5D097F4), который стреляет Physics.Raycast(camPos, headPos - camPos, dist, obstacleMask)
// и пишет результат в OcclusionModule + 0x3C (Nullable<bool>: +0x3C hasValue, +0x3D isOccluded).
inline constexpr std::uint64_t GAME_CONTROLLER_MAP_MANAGER   = 0x170; // GameControllerBase.mapManager (MapManager)
inline constexpr std::uint64_t MAP_MANAGER_MAP_VIEW          = 0x28;  // MapManager.mapView (MapView)
inline constexpr std::uint64_t MAP_MANAGER_STATIC_INTEREST   = 0x40;  // MapManager.staticInterestMarkers (StaticInterestMarkers)
inline constexpr std::uint64_t MAP_MANAGER_MARKER_SERVICE    = 0x60;  // MapManager.KzG (интерфейс lO, реализация lN; 11966: uke/NQ)
inline constexpr std::uint64_t MAP_MANAGER_MAP_PRESENTER     = 0x68;  // MapManager.KzL (lw MapMarkerPresenter; 11966: ukE/xd)
inline constexpr std::uint64_t MAP_MANAGER_IS_OPEN           = 0x94;  // MapManager.<Kzb>k__BackingField (bool: открыта ли карта)
inline constexpr std::uint64_t MARKER_INSTALLER_MAP_VIEW     = 0x30;  // MarkerSystemInstaller.mapView
inline constexpr std::uint64_t MAP_VIEW_MAP_RT               = 0x38;  // MapView.map (RectTransform)
inline constexpr std::uint64_t MAP_VIEW_SCROLL_RECT          = 0x48;  // MapView.scrollRect (ScrollRectEx)
inline constexpr std::uint64_t MAP_VIEW_ON_ENABLE_ACTION     = 0x60;  // MapView.xDL (Action, m_target = презентер; 11966: OwV/JuA)
inline constexpr std::uint64_t STATIC_INTEREST_RECT          = 0x30;  // StaticInterestMarkers.rect (RectTransform)
inline constexpr std::uint64_t STATIC_INTEREST_INIT          = 0x4C;  // StaticInterestMarkers.Kze (bool)
inline constexpr std::uint64_t STATIC_INTEREST_RECTS_LIST    = 0x58;  // StaticInterestMarkers.KzT (List<RectTransform>)
inline constexpr std::uint64_t STATIC_INTEREST_POS_LIST      = 0x60;  // StaticInterestMarkers.KzK (List<Vector3>)
inline constexpr std::uint64_t MAP_PRESENTER_MARKER_SERVICE  = 0x10;  // lw.Pex (lO MarkerService; 11966: xd.RTj)
inline constexpr std::uint64_t MAP_PRESENTER_VIEWS_DICT      = 0x28;  // lw.PeP (Dictionary<Marker, MapMarkerView>)
inline constexpr std::uint64_t MAP_PRESENTER_IS_OPEN         = 0x48;  // lw.PeK (bool)
inline constexpr std::uint64_t MAP_MARKER_VIEW_RT            = 0x20;  // MapMarkerView.rectTransform (RectTransform)
inline constexpr std::uint64_t SCROLL_RECT_VIEWPORT          = 0x50;  // ScrollRectEx.m_Viewport (RectTransform)
inline constexpr std::uint64_t SCROLL_RECT_VIEW_RECT         = 0x90;  // ScrollRectEx.m_ViewRect (RectTransform)
inline constexpr std::uint64_t SCROLL_RECT_VIEW_BOUNDS_CENTER  = 0xB0; // ScrollRectEx.m_ViewBounds.m_Center (Vector3)
inline constexpr std::uint64_t SCROLL_RECT_VIEW_BOUNDS_EXTENTS = 0xBC; // ScrollRectEx.m_ViewBounds.m_Extents (Vector3)
inline constexpr std::uint64_t MARKER_SERVICE_ADDED_ACTION   = 0x20;  // lN Action<Marker> (11966: NQ.RWt)
inline constexpr std::uint64_t SCREEN_WINDOW_RECT_TRANSFORM  = 0x30;  // ScreenMarkerWindowView.Prn (RectTransform родитель всех ScreenMarkerView)
inline constexpr std::uint64_t SCREEN_PRESENTER_VIEW_DICT    = 0x48;  // ScreenMarkerPresenter.PrE (Dictionary<Marker, ScreenMarkerView>)
inline constexpr std::uint64_t SCREEN_PRESENTER_MARKER_LIST  = 0x50;  // ScreenMarkerPresenter.Prm (List<Marker>)
inline constexpr std::uint64_t SCREEN_PRESENTER_INST_DICT    = 0x60;  // ScreenMarkerPresenter.PrI (Dictionary<ScreenMarkerView, ScreenMarkerView>)
inline constexpr std::uint64_t MARKER_DATA                   = 0x10;  // Marker.<Peo>k__BackingField (lr; 11966: NX/Ig)
inline constexpr std::uint64_t MARKER_ACTIVE                 = 0x18;  // Marker.<Pen>k__BackingField (bool)
inline constexpr std::uint64_t MARKER_DATA_TYPE              = 0x18;  // NX.RqW (int32 MarkerType, 10 = NearbyPlayer)
inline constexpr std::uint64_t MARKER_DATA_WORLD_POS         = 0x1C;  // lr.PeW (Vector3; 11966: NX.Rqc)
inline constexpr std::uint64_t MARKER_DATA_HEAD_POS          = 0x28;  // lr.Peh (Vector3, headTransform.pos + (0,0.25,0); 11966: NX.Rqs)
inline constexpr std::uint64_t MARKER_DATA_TRANSFORM         = 0x40;  // lr.POZ (Transform PlayerModelInfo.head; 11966: NX.Rqv)
inline constexpr std::uint64_t MARKER_DATA_USER_ID           = 0x58;  // lp.POe (string userId; 11966: NV.Rqd)
inline constexpr std::uint64_t MARKER_DATA_IS_HIT            = 0x73;  // lp.POX (bool, копируется в ctx+0x50 в NearbyPlayerScreenMarkerView::Tick; 11966: NV.Rqb)
inline constexpr std::uint64_t PLAYER_HIT_TRACKER            = 0x358; // 11985: PlayerManager.KSn (lE hit tracker; хвост PM +0x10, 11966: uGa/Nt @0x348)
inline constexpr std::uint64_t HIT_TRACKER_HIT_DICT          = 0x18;  // lE.PTe (Dictionary<uint, float> hit expire times; 11966: Nt.RWf)
inline constexpr std::uint64_t SCREEN_MARKER_VIEW_CONTEXT    = 0x28;  // ScreenMarkerView.context (ScreenMarkerModuleContext)
inline constexpr std::uint64_t SCREEN_MARKER_VIEW_MODULES    = 0x30;  // ScreenMarkerView.modules (List<lW>; 11966: List<Nk/IH>)
inline constexpr std::uint64_t SCREEN_MARKER_VIEW_MARKER     = 0x40;  // ScreenMarkerView.<Prg>k__BackingField (Marker)
inline constexpr std::uint64_t NEARBY_VIEW_MODULE_CTX        = 0x80;  // NearbyPlayerScreenMarkerView.screenMarkerModuleContext (ScreenMarkerModuleContext)
inline constexpr std::uint64_t NEARBY_VIEW_DATA              = 0xA8;  // NearbyPlayerScreenMarkerView.PKR (lp; 11966: Rsq NV/Iu)
inline constexpr std::uint64_t NEARBY_VIEW_CACHED_HIT        = 0xB1;  // NearbyPlayerScreenMarkerView.Prk (bool, кэш data.isHit; 11966: Rsc)
inline constexpr std::uint64_t MARKER_CTX_RAY_TARGET         = 0x44;  // ScreenMarkerModuleContext.<PKS>k__BackingField (Vector3)
inline constexpr std::uint64_t MARKER_CTX_SKIP_OCCLUSION     = 0x50;  // ScreenMarkerModuleContext.<PKB>k__BackingField (bool)
inline constexpr std::uint64_t MARKER_CTX_HIDDEN             = 0x90;  // ScreenMarkerModuleContext.PKd (bool)
inline constexpr std::uint64_t OCCLUSION_MOD_CTX             = 0x20;  // lW.PKX (ScreenMarkerModuleContext; база OcclusionModule)
inline constexpr std::uint64_t OCCLUSION_MOD_MASK            = 0x28;  // OcclusionModule.obstacleMask (LayerMask)
inline constexpr std::uint64_t OCCLUSION_MOD_INTERVAL        = 0x2C;  // OcclusionModule.checkInterval (float)
inline constexpr std::uint64_t OCCLUSION_MOD_NEXT_TIME       = 0x38;  // OcclusionModule.PKO (float)
inline constexpr std::uint64_t OCCLUSION_MOD_HAS_VAL         = 0x3C;  // OcclusionModule.PKT.hasValue (uint8)
inline constexpr std::uint64_t OCCLUSION_MOD_IS_OCCLUDED     = 0x3D;  // OcclusionModule.PKT.value (uint8, 0=виден, 1=за стеной)
inline constexpr std::uint64_t HMJ_TYPEINFO_RVA              = 0xE612F58; // release 11991: ryl (бывш. yCC в 11989 / Pee в 11985) TypeInfo slot (25 LayerMask-статик 0x00..0x60, как hMj в 11966 / IWt в бете)
inline constexpr std::int32_t  MARKER_TYPE_NEARBY_PLAYER     = 10;    // MarkerType.NearbyPlayer
// Тот же struct UnityEngine.RaycastHit (dump.cs 62a8534, строка 642061):
//   m_Point 0x0 (Vector3), m_Normal 0xC (Vector3), m_FaceID 0x18,
//   m_Distance 0x1C, m_UV 0x20, m_Collider 0x28  — размер 0x2C.
inline constexpr std::uint64_t RAYCASTHIT_POINT              = 0x00;  // куда луч упёрся (мир)
inline constexpr std::uint64_t RAYCASTHIT_NORMAL             = 0x0C;  // нормаль поверхности там
// m_Collider — В КАКОМ коллайдере луч остановился. Нужен, чтобы отличить «луч
// упёрся в сам узел добычи» от «узел перекрыт чужой геометрией»: у камня точка
// прицела лежит внутри породы, поэтому луч всегда доходит до неё раньше и без
// этого поля собственный камень выглядел стеной (см. ray_hit_is_self_node).
inline constexpr std::uint64_t RAYCASTHIT_COLLIDER           = 0x28;  // UnityEngine.Collider

// Ragdoll bone list route (dump.cs) — game-maintained list of rig bone
// transforms, no name matching needed:
//   PlayerManager.kccReference (0xB0, possibly a wrapper) -> KCC
//   KCC.ZMl = CharacterAnimation (0x108); KCC.player back-ref (0x78) validates
//   CharacterAnimation.ragdoll (0x38); CharacterAnimation.ZCu back-ref (0x78)
//   Ragdoll.m_Pelvis Rigidbody (0x20), Ragdoll.m_Bones BodyPart[] (0x88)
//   Ragdoll.BodyPart.transform (0x10)
inline constexpr std::uint64_t PLAYER_KCC_REFERENCE        = 0xB0;
inline constexpr std::uint64_t KCC_PLAYER_BACKREF          = 0x78;
inline constexpr std::uint64_t KCC_HEAD_TRANSFORM          = 0x88; // KCC.head (managed UnityEngine.Transform)
inline constexpr std::uint64_t KCC_NORMAL_HEIGHT          = 0xA0; // float, capsule height standing
inline constexpr std::uint64_t KCC_CROUCH_HEIGHT          = 0xA4; // float, capsule height crouched
// Server-side hit volumes: KCC.hitBoxRecorderRoot (0x70) -> HitBoxRecorderRoot
//   .hitBoxes (0x68, Oxide.HitBox[]) ; HitBox.size 0x24 / center 0x30 (local
//   Vector3), m_HitArea 0x68 (0 Head, 1 Chest, 2 Leg, 3 Foot, 4 Hand).
//   World centre == HitBox.transform.TransformPoint(center) (HitBox.Kbr).
inline constexpr std::uint64_t KCC_HITBOX_ROOT            = 0x70;
inline constexpr std::uint64_t HITBOX_ROOT_ARRAY          = 0x68;
inline constexpr std::uint64_t HITBOX_SIZE                = 0x24;
inline constexpr std::uint64_t HITBOX_CENTER              = 0x30;
inline constexpr std::uint64_t HITBOX_AREA                = 0x68;
inline constexpr std::uint64_t KCC_LOOK_HEIGHT_OFFSET     = 0x90; // float, eye = pos + (capsule height + this) * up
// KCC.bWa : HyperHug.Games.Oxide.Features.Player.Move (value struct; 11966: RhW)
//   +0x00 MoveState State (0 idle,1 walk,2 run,3 crouching,4 air,5 climb,6 swim,7 dead)
//   +0x04 Pose (0 Stand, 1 Crouch)   +0x08 bool Aim  +0x0C Vector3 Position
//   +0x18 Vector3 RealVelocity       +0x38 Vector3 BaseVelocity
// 11985: у KCC ДВА поля Move — bWC @0xF0 (новый, рядом с CharacterAnimation)
// и bWa @0x1AC (преемник RhW @0x13C: стоит сразу за PlayerInput bWJ @0x184,
// как в бете 11977 — «Move после PlayerInput, 0x6C байт»). Нужен второй.
inline constexpr std::uint64_t KCC_MOVE                   = 0x1AC;  // 11966: 0x13C; State +0x0 (uint8), Pose +0x4, Aim +0x8, Position +0xC, RealVelocity +0x18
inline constexpr std::uint64_t KCC_VISIBILITY             = 0x308;  // [SyncVar(hook = "OnVisibilityChanged")] public Visibility visibility (0 = Visible, 1 = Hidden); 11966: 0x298
inline constexpr std::uint64_t KCC_MOTOR_ENABLED          = 0x30D;  // [SyncVar] private bool motorEnabled (bhP); 11966: 0x29D
inline constexpr std::uint64_t KCC_HAS_OBSERVER_MOVE      = 0x3C0;  // private bool bhA (11966: RMu @0x350) — сразу после полного ObserverMove bhF @0x380; 1 after first full ObserverMove on remote client
// MoveState значения (enum MoveState 11985 TypeDefIndex 4450, сверен с 11949):
// 0 IDLE, 1 WALKING, 2 RUNNING, 3 CROUCHING, 4 AIRBORNE, 5 CLIMBING,
// 6 SWIMMING, 7 DEAD, 8 SEATED. Поза (Move.Pose, TypeDefIndex 4451) знает
// только Stand 0 и Crouch 1 — лежачей позы в этой сборке НЕТ.
inline constexpr std::int32_t KCC_MOVE_STATE_DEAD          = 7;
// Внутренние смещения самого значения Move (value struct внутри KCC). Имя
// поля ротируется (11966: Move RhW 0x13C, 11968: Move ydh 0x1AC), а раскладка
// значения — нет: сверено по dump.cs обеих сборок (struct Move, TypeDefIndex
// 4309 / 4453): State 0x00, Pose 0x04, Aim 0x08, Position 0x0C,
// RealVelocity 0x18, MoveDirection 0x24, BaseVelocity 0x38.
inline constexpr std::uint64_t KCC_MOVE_STATE              = 0x00; // int32 MoveState
inline constexpr std::uint64_t KCC_MOVE_REAL_VELOCITY      = 0x18; // Vector3
inline constexpr std::uint64_t KCC_MOVE_REAL_VELOCITY_Y    = 0x1C; // float .y
inline constexpr std::uint64_t KCC_MOVE_BASE_VELOCITY      = 0x38; // Vector3
inline constexpr std::uint64_t KCC_MOVE_BASE_VELOCITY_Y    = 0x3C; // float .y
inline constexpr std::int32_t KCC_MOVE_STATE_AIRBORNE      = 4;
// Верхняя граница (включительно) состояний, в которых «нет падения» вообще
// что-то правит: 0 IDLE, 1 WALKING, 2 RUNNING, 3 CROUCHING, 4 AIRBORNE.
// Выше (5 CLIMBING, 6 SWIMMING, 7 DEAD, 8 SEATED) вертикальная скорость —
// часть другой логики (лезу, плыву, сижу), и трогать её нельзя.
inline constexpr std::int32_t KCC_MOVE_STATE_CLAMP_MAX     = 4;
inline constexpr std::uint64_t KCC_CHARACTER_ANIMATION     = 0xE0;  // bWK CharacterAnimation (11966: lua)
inline constexpr std::uint64_t CHAR_ANIM_PLAYER_BACKREF    = 0x78;  // bnI PlayerManager
inline constexpr std::uint64_t CHAR_ANIM_RAGDOLL           = 0x38;
inline constexpr std::uint64_t RAGDOLL_PELVIS_RIGIDBODY    = 0x20;
// 11985: в Ragdoll вставлен bool m_Interpolate (0x60) перед m_SurfaceTexture —
// m_Bones (Rigidbody[]) уехал с 0x88 на 0x90, как в бете 11977.
inline constexpr std::uint64_t RAGDOLL_BONES_ARRAY         = 0x90;
inline constexpr std::uint64_t RAGDOLL_BODYPART_TRANSFORM  = 0x10;
inline constexpr std::uint64_t IL2CPP_ARRAY_LENGTH         = 0x18;

// Native Unity object layout — reversed from libunity.so in this repo:
//   Transform::get_childCount_Injected -> ldr w0, [x0, #0x58]
//   Transform::GetChild helper         -> ldr x8, [x0, #0x48]; ldr x0, [x8, w1, uxtw #3]
//   Component::get_gameObject_Injected -> ldr x0, [x0, #0x20]
//   GameObject::get_transform_Injected -> ldr x8, [x0, #0x20]; ldr x19, [x8, #8]
inline constexpr std::uint64_t TRANSFORM_CHILDREN_ARRAY   = 0x48; // Transform** (direct pointers)
inline constexpr std::uint64_t TRANSFORM_CHILD_COUNT      = 0x58; // int32
// Native Transform hierarchy storage for the release 11945 libunity.
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_DATA    = 0x38;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_INDEX   = 0x40;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_MATRICES = 0x18;
inline constexpr std::uint64_t TRANSFORM_HIERARCHY_INDICES  = 0x20;
inline constexpr std::uint64_t COMPONENT_GAMEOBJECT       = 0x20; // native Component -> native GameObject*
inline constexpr std::uint64_t GAMEOBJECT_COMPONENT_ARRAY = 0x20; // native GameObject -> ComponentPair*
inline constexpr std::uint64_t COMPONENT_PAIR_PTR         = 0x08; // pair[0] + 8 == Transform* (first component)
// GameObject name is a 32-byte core::string (SSO): flags byte at +0x1F,
// heap pointer at +0x0 when (flags >= 0x40), inline chars otherwise.
inline constexpr std::uint64_t GAMEOBJECT_NAME             = 0x48;
// Точное смещение имени внутри GameObject подтверждается в рантайме (сверкой
// с известными именами костей); 0x48 — ожидаемое значение и первая догадка.
inline constexpr std::uint64_t GAMEOBJECT_NAME_GUESS      = GAMEOBJECT_NAME;

inline constexpr std::uint64_t IL2CPP_LIST_ITEMS          = 0x10;
inline constexpr std::uint64_t IL2CPP_LIST_SIZE           = 0x18;
inline constexpr std::uint64_t IL2CPP_ARRAY_FIRST_ELEMENT = 0x20;

// ---- World markers: ore nodes and animals (dump.cs + libil2cpp) -------------
// Both are Oxide.MineableObject subclasses (MineableStone / MineableAnimal /
// MineableTree / MineableObjectWithRandomSpawn), each a Mirror.NetworkBehaviour,
// so the client-side Mirror registry lists all of them:
//   Mirror.NetworkClient.spawned : Dictionary<uint, NetworkIdentity>
// The NetworkClient TypeInfo slot is from release 11985 script.json's
// TypeInfoPointers; its static-field access pattern is also checked against the
// owning class's methods:
//   ldr x0,[x19] ; ldr x8,[x0,#0xb8] (static_fields) ; ldr x0,[x8,#0x28] (spawned)
inline constexpr std::uint64_t NETWORK_CLIENT_TYPEINFO_RVA = 0xE60AA70; // release 11991: Mirror.NetworkClient TypeInfo slot
inline constexpr std::uint64_t NETWORK_CLIENT_SPAWNED      = 0x30;  // Dictionary<uint, NetworkIdentity>; 0x28 handlers

// ---- Mirror: сетевые таймнги (release 11985) --------------------------------
// Поза удалённого игрока, которую читает чит, — это снапшот: он отстаёт от
// удалённого времени на NetworkClient.bufferTime, и сам снапшот приехал к нам
// через rtt/2. Попадание сервер считает в своём «сейчас», то есть через rtt/2
// после нашей записи. Итого цель надо вести на flight + bufferTime + rtt.
//   bufferTime = sendInterval * bufferTimeMultiplier
//     NetworkClient.get_bufferTime, RVA 0xC8883B4 (11991): ldr d1,[x8,#0x108];
//     fmul на sendInterval (внутри зовёт NetworkServer.get_sendInterval,
//     RVA 0xC898468; сам NetworkClient.get_sendInterval 0xC87B8B0 = 1/sendRate).
//   sendRate = NetworkServer.tickRate
//     NetworkClient.get_sendRate, RVA 0xC87B828 (11991): ldr w0,[x8,#8].
//   rtt = NetworkTime._rtt.Value
//     NetworkTime.get_rtt, RVA 0xC8A10C8 (11991): ldr d0,[x8,#0x20].
//     RVA сверены по script.json DotNetSignature; смещения [x8,#...] и fmul
//     перепроверены дизассемблером libil2cpp.so 11991 (file_off = RVA − 0x4000).
// Слоты ниже — metadata usage, а не TypeInfo-кэш: [base+rva] даёт адрес слота,
// [слот] — Il2CppClass*. Резолвер в game.cpp берёт класс только по точному
// имени и namespace, поэтому сдвиг слота в чужом билде даёт откат к
// константам, а не чтение мусора.
inline constexpr std::uint64_t NETWORK_SERVER_TYPEINFO_RVA = 0xE60AB38;
inline constexpr std::uint64_t NETWORK_CLIENT_STATICS_RVA  = 0;
inline constexpr std::uint64_t NETWORK_TIME_TYPEINFO_RVA   = 0xE60AB50;
inline constexpr std::uint64_t NETWORK_CLIENT_BUFFER_MULTIPLIER = 0x108; // double
inline constexpr std::uint64_t NETWORK_SERVER_TICK_RATE         = 0x8;   // int
inline constexpr std::uint64_t NETWORK_TIME_RTT                 = 0x20;  // double, _rtt.Value

// ---- CharacterSpeedConfig: скорости и прыжок персонажа ----------------------
// Ассет висит на KCC локального игрока. Классов два (KCC и SingleKcc, оба
// реализуют Yc), поле лежит на 0x80 у KCC и на 0x88 у SingleKcc — поэтому
// читатель пробует оба и принимает только правдоподобные значения, а не
// угадывает класс.
inline constexpr std::uint64_t CHAR_SPEED_CONFIG_KCC        = 0x80;
inline constexpr std::uint64_t CHAR_SPEED_CONFIG_SINGLE_KCC = 0x88;
inline constexpr std::uint64_t CHAR_SPEED_SPRINT        = 0x18;  // StandSprintSpeed
inline constexpr std::uint64_t CHAR_SPEED_JOG           = 0x1C;  // StandJogSpeed
inline constexpr std::uint64_t CHAR_SPEED_WALK          = 0x20;  // StandWalkSpeed
inline constexpr std::uint64_t CHAR_SPEED_BACK_FACTOR   = 0x2C;  // BackSpeedFactor
inline constexpr std::uint64_t CHAR_SPEED_JUMP_HEIGHT   = 0x30;  // JumpHeight
inline constexpr std::uint64_t CHAR_SPEED_GRAVITY       = 0x34;  // Gravity

// System.Collections.Generic.Dictionary<uint, NetworkIdentity> (this BCL has no
// _fastModMultiplier): _buckets 0x10, _entries 0x18, _count 0x20, _freeList 0x24.
// Entry { int hashCode; int next; uint key; object value; } -> 24 bytes.
inline constexpr std::uint64_t DICT_ENTRIES      = 0x18;
inline constexpr std::uint64_t DICT_COUNT        = 0x20;
inline constexpr std::uint64_t DICT_ENTRY_STRIDE = 0x18;
inline constexpr std::uint64_t DICT_ENTRY_VALUE  = 0x10;

// Mirror.NetworkIdentity
inline constexpr std::uint64_t NETID_NET_ID     = 0x58; // uint netId
inline constexpr std::uint64_t NETID_BEHAVIOURS = 0x80; // NetworkBehaviour[]

// ---- Шкафы (Oxide.Cupboard) ------------------------------------------------
// Класс НЕ обфусцирован ни в релизе 11946, ни в бете 11943: имя "Cupboard"
// (неймспейс Oxide; неймс обфускатор в имени Il2CppClass не входит). Он же
// базовый для TutorialCupboard (шкаф учебного острова — отдельным классом,
// помечается в подписи). Base: Ryf/mao : Mirror.NetworkBehaviour, значит шкаф
// заспавнен у клиента как все маркерные сущности и берётся тем же реестром.
// Раскладка полей идентична в обоих дампах (посмотрено позиционно):
//   inventory 0xA8, isLootable 0xB0, m_Capacity 0xB4, panelName 0xB8,
//   m_Piece(BuildingPiece) 0xC8, m_LootBag 0xD0, authorizedPlayers 0xD8,
//   upkeep-per-player dict 0xE0, protectedTime 0xE8, costItem* 0xEC..0xF8,
//   isPlusSubscribed 0xFC, clanPerkMultiplier 0x100, luxuryTaxEnabled 0x104,
//   luxuryBlocks 0x108, luxuryTax 0x10C..0x114.
inline constexpr std::uint64_t CUPBOARD_IS_LOOTABLE        = 0xB0;  // bool
inline constexpr std::uint64_t CUPBOARD_AUTHORIZED_PLAYERS = 0xD8;  // List<string>
inline constexpr std::uint64_t CUPBOARD_PROTECTED_TIME     = 0xE8;  // float (SyncVar), сек
inline constexpr std::uint64_t CUPBOARD_LUXURY_BLOCKS      = 0x108; // int (SyncVar)
// Реестр ВСЕХ шкафов мира (не только сетевой дальности) существует в клиенте:
// статик-геттер возвращает синглтон env-менеджера, Mf<Cupboard> лежит в нём
// по +0x10; Mf<T>: dict 0x10 / dict 0x18 / Nq<T> 0x20; Nq<T>: count 0x10 (int),
// items(массив) 0x18. Читалкой ESP этот путь НЕ используется (достаточно
// NetworkClient.spawned) — адреса оставлены как breadcrumbs для будущей
// статической перечислительной схемы:
//   релиз 11946: Cupboard.sWB() (= get_JGX)   RVA 0x67D7634 -> ldr x0,[x0,#0x10]
//   бета  11943: Cupboard.Xbp() (= get_SeP)   RVA 0x6860EB0 -> ldr x0,[x0,#0x10]
// Обе функции структурно идентичны: service-locator по metadata handle,
// epilogue `ldr x0, [x0, #0x10]; ret`.

// ---- Постройки: ХП дверей/стен (Oxide.PieceVitals : GenericVitals : kAg) ----
// kAg.Entity (0x68) -> kAH; kAH.Health (0x98) -> обёртка kAK<float,
// ReasonedValueReasonHealthEvent> (11966: pmp/pmK, тип тогда не разрешался).
// Форма обёртки та же, что у kAr (SYNC_VALUE_OFFSET): Action 0x10, Filter
// 0x18, значение 0x20, последнее 0x28 — подтверждено и сеттером 11966
// (GenericVitals.COL() RVA 0xb0f6238: новое значение [obj+0x20], старое
// [obj+0x24]). Лог печатает оба кандидата, слот выбирается по правдоподобию.
inline constexpr std::uint64_t PMP_ENTITY            = 0x68; // kAg.Entity -> kAH (11966: pmp/pmK)
inline constexpr std::uint64_t PMK_HEALTH            = 0x98; // kAK<float, ...> (11966: AsyncReactiveProperty<float>?)
inline constexpr std::uint64_t ARP_LATEST_VALUE      = 0x18; // первый кандидат (в kAK там Filter — значение скорее на 0x20)
inline constexpr std::uint64_t HEALTH_VALUE          = 0x20; // второй кандидат: float значение (сеттер 11966)
inline constexpr std::uint64_t VITALS_MAX_HEALTH     = 0x88; // GenericVitals.m_MaxHealth

// ---- Время суток: TOD_Sky (ассет Time Of Day) ------------------------------
// TOD_Sky — обфусцированный класс с TypeDefIndex 706: его статик-список
// List<Self> лежит по 0x0, Cycle — по 0x40 (TOD_CycleParameters: Hour 0x10,
// Day 0x14, Month 0x18, Year 0x1C — подтверждено dump.cs, TDI не ротируется).
// Ротации имени: 11985 Hi -> 11989 gF -> 11991 lI (в дампе 11991 класс в
// дампе найден по TDI 706, TypeInfo lI_TypeInfo = 0xE611560; точное «Hi» в
// script.json 11991 — ДРУГОЙ класс, слот 0xE606E60, НЕ использовать).
// GameControllerBase.TODSky остаётся на 0x178.
// LayerMask-статики: Pee (11985) -> yCC (11989) -> ryl (11991, TypeInfo
// 0xE612F58), 25 статиков 0x00..0x60 шаг 4 — для resolve_solid_wall_masks.
// Оба слота внутри окна сканирования ниже.
// day_tod_scan() checks 1 KiB per frame and validates candidates against the
// list/Cycle/date signature; re-check the range against the next dump's ELF
// before changing the profile.
inline constexpr std::uint64_t TOD_SCAN_RVA_BEGIN = 0xE600000;
inline constexpr std::uint64_t TOD_SCAN_RVA_END   = 0xE630000; // 11991 release metadata window (.data 0xE376A48..0xE9D9CA8)
inline constexpr std::uint64_t TOD_SKY_CYCLE      = 0x40; // TOD_Sky.Cycle
inline constexpr std::uint64_t TOD_CYCLE_HOUR     = 0x10; // TOD_CycleParameters.Hour
inline constexpr std::uint64_t TOD_CYCLE_DAY      = 0x14;
inline constexpr std::uint64_t TOD_CYCLE_MONTH    = 0x18;
inline constexpr std::uint64_t TOD_CYCLE_YEAR     = 0x1C;

// Oxide.MineableObject — the shared base of ore nodes, trees and animals.
// Всё сверено с дампом 62a8534 (il2cpp.h: Oxide_MineableObject_Fields).
inline constexpr std::uint64_t MINEABLE_LOOT           = 0xA0; // List<Oxide.LootItem>
inline constexpr std::uint64_t MINEABLE_FINISH_BONUS   = 0xA8;
inline constexpr std::uint64_t MINEABLE_CURRENT_HEALTH = 0x78; // float, убывает от каждого удара
inline constexpr std::uint64_t MINEABLE_HIT_ANCHOR     = 0xC8; // Kwq, Transform точки удара (11966: LXX; см. ниже)
// Oxide.LootItem: the item short name the node drops ("stone", "metal.ore", ...)
inline constexpr std::uint64_t LOOTITEM_ITEM_NAME      = 0x10;
inline constexpr std::uint64_t MINEABLE_MAX_HEALTH     = 0xC0; // float
// LXX (0xC8) — Transform-якорь точки удара: ZgL() отдаёт его мировую позицию, а
// hitInfo (MineableObjectHitInfoCompact) несёт её дальше в экстеншены. У руды
// именно к этой точке игра привязывает Collider.ClosestPoint при первом ударе
// (OreHitstreaks.giu), у дерева — рейкаст по стволу. Нужен как запасная точка
// прицела на руде: пивот камня стоит ВНУТРИ породы, и прицел по нему оставляет
// луч игры на поверхности раньше точки прицела. Читается только в диагностике
// и только когда цель — руда.
inline constexpr std::uint64_t MINEABLE_FRACTION       = 0xD0; // fractionRemaining
inline constexpr std::uint64_t MINEABLE_ENTITY_TYPE    = 0xD8; // ServerPlayersAnalytics.EntityType
// Каким орудием узел вообще добывается. Тип — тот же enum Oxide.FPTool.
// ToolPurpose, что и у орудия в руке (FPTool.m_ToolPurposes), то есть сравнение
// побитовое: (purposes & required) != 0. Кирка не рубит дерево и наоборот —
// без этой проверки бот вечно кружил вокруг узла, который нечем взять.
inline constexpr std::uint64_t MINEABLE_REQUIRED_TOOL_PURPOSE = 0x70; // int32, флаги
// Сколько опыта даёт узел (int32). Только для строки статуса.
inline constexpr std::uint64_t MINEABLE_EXPERIENCE     = 0xD4;

enum class ToolPurpose : std::int32_t {
    None       = 0,
    CutWood    = 1, // топор/пила
    BreakRocks = 2, // кирка
    CutAnimals = 4, // нож
};
// Кеш JE-экстеншенов узла (MineableObjectExtension_*). Заполняется в
// MineableObject.cik() через GetComponents<JE> на GameObject узла; cik
// вызывается из OnStartClient -> ciQ() (то есть кеш готов сразу при спавне
// узла на клиенте) и при каждом попадании (SRl -> cik). Именно здесь живёт
// «крестик» ресурса — см. блок ниже.
inline constexpr std::uint64_t MINEABLE_EXTENSIONS     = 0xE8; // IEnumerable<mQ> KwZ (11966: JE[])

// ---- Крестик (hit-streak marker) --------------------------------------------
// «Крестик» — не часть меша дерева/камня, а отдельный объект, который игра
// создаёт при первом же попадании по узлу:
//
//   MineableObject.SRl(локальный удар) -> cik() -> JE.gir(hitInfo)
//     руда:   OreHitstreaks.gir -> giu() (Collider.ClosestPoint — точка на
//             поверхности камня) -> gil() (Instantiate + SetParent +
//             set_position) -> MoW = живой клон; его transform и есть X.
//     дерево: TreeHitstreaks.gir -> raycast по стволу -> Instantiate ->
//             MTn = живой клон, а мировая точка попадания пишется в MTQ/MTu.
//
// Проверка «попал ли удар в крестик» (OreHitstreaks.os для руды и
// TreeHitstreaks.giL + вся его обвязка rRS/DMU/Dne/DfW для деревьев; имена
// и RVA методов 11966 — исторические) меряет дистанцию от точки удара
// ИМЕННО до этих полей: для руды — до позиции transform'а маркера, для
// дерева — до отрезка SPOT_A..SPOT_B (радиус 0.15 м).
// Поэтому они и есть единственно верная цель автофарма: удар по ним игра
// засчитывает как попадание в серию, а всё остальное (корпус, LOD-меш,
// спящий шаблон на пивоте) — нет.

// MineableObjectExtension_OreHitstreaks : JE — руда/камень/сера.
inline constexpr std::uint64_t OREHS_STREAK_INDEX      = 0x20; // int hitstreakIndex
inline constexpr std::uint64_t OREHS_MARKER_TEMPLATE   = 0x28; // спящий шаблон на пивоте узла
inline constexpr std::uint64_t OREHS_MARKER            = 0x30; // Pja — живой клон (X), null до первого удара (11966: MoW)
inline constexpr std::uint64_t OREHS_COLLIDER          = 0x38; // PjN (Collider узла)
inline constexpr std::uint64_t OREHS_MINEABLE          = 0x40; // Pjt — обратная ссылка на узел

// MineableObjectExtension_OreHitstreaksMarker : MonoBehaviour — сам X.
// Мировая позиция его transform'а = точка крестика. Pjh (0x58) — таймер
// жизни: Update() (RVA 0x786eb40 в 11966 — исторический) делает
// `age += Time.deltaTime` и сравнивает с 15.0 (`fmov s1,#15.0; fcmp s0,s1;
// b.le`); при превышении — SetActive(false) на своём GameObject и хвостовой
// вызов владельца (Pjo, 0x38). Так что «поле OREHS_MARKER заполнено» !=
// «крестик жив»: проверять надо возраст. Имена 11985: Pjo/Pjn/PjM/Pjh
// (11966: lzF/lzN/lzA/lHG, из il2cpp.h дампа 62a8534).
// Полный расклад класса (сверен с dump.cs 11985): 0x20 meshRenderer,
// 0x28 ignoringLayerMask, 0x30 sizeByDistance (AnimationCurve), 0x38 Pjo
// (владелец), 0x40 Pjn (MaterialPropertyBlock), 0x48 PjM (smooth),
// 0x50 PjW (HashSet<Collider>), 0x58 Pjh (возраст).
inline constexpr std::uint64_t OREMARK_RENDERER        = 0x20; // MeshRenderer
inline constexpr std::uint64_t OREMARK_OWNER           = 0x38; // Pjo — обратная ссылка на OreHitstreaks
inline constexpr std::uint64_t OREMARK_SMOOTH          = 0x48; // PjM, float — плавное значение размера
inline constexpr std::uint64_t OREMARK_AGE             = 0x58; // Pjh, float — секунды с появления
inline constexpr float         OREMARK_LIFETIME        = 15.0F;

// MineableObjectExtension_TreeHitstreaks : mQ — деревья (11966: JE).
// В 11966 поле MovingMethod исчезало (0x20 занимал шаблон); в 11985 оно
// вернулось на 0x20, шаблон снова на 0x28 — константы не менялись.
inline constexpr std::uint64_t TREEHS_MARKER_TEMPLATE  = 0x28; // hitStreakMarkerOriginal (HitMarkerItem) — шаблон
inline constexpr std::uint64_t TREEHS_STREAK           = 0x48; // Pxi int (11966: MTz; сбрасывается в gie)
inline constexpr std::uint64_t TREEHS_MARKER           = 0x50; // PxQ — живой клон (X), null до первого удара (11966: MTn)
inline constexpr std::uint64_t TREEHS_SPOT_A           = 0x88; // PxG, Vector3 — точка X в мировых (11966: MTQ)
inline constexpr std::uint64_t TREEHS_SPOT_B           = 0xA4; // PxH, Vector3 — второй конец отрезка X (11966: MTu)

// MineableObjectExtension_HitMarkerItem : MonoBehaviour — визуальный X на
// дереве. mark (0x38) — managed Transform декаля: giI ставит его в точку
// попадания + normal*0.25 (чтобы не z-файтил с корой), поэтому для
// прицеливания первична TREEHS_SPOT_A (точка на коре), а декаль — запасной.
// lifetime (0x20) — ПОЛНЫЙ срок жизни, а возраст маркер набирает сам в PjC
// (0xC8; 11966: lzr): Update() (RVA 0x786485c в 11966 — исторический) делает
// `age += Time.deltaTime` и при `age > lifetime` отдаёт объект владельцу
// (Pjr, 0xC0) — то есть в пул, где его переиспользуют для другого дерева.
// Живой X — только пока возраст <= lifetime.
inline constexpr std::uint64_t HITMARK_LIFETIME        = 0x20; // float, полный срок
inline constexpr std::uint64_t HITMARK_FILTER          = 0x28; // MeshFilter
inline constexpr std::uint64_t HITMARK_RENDERER        = 0x30; // Renderer
inline constexpr std::uint64_t HITMARK_MARK            = 0x38; // Transform самого крестика
inline constexpr std::uint64_t HITMARK_AGE             = 0xC8; // PjC, float — сколько уже прожито (11966: lzr)

// ServerPlayersAnalytics.EntityType values used for the labels (enum 11985
// TypeDefIndex 4016; значения 0..25 сверены с 11966, в 11985 добавлен Wolf).
enum class MineableEntityType : std::int32_t {
    None = 0, Bear = 1, Boar = 2, Deer = 3, Rabbit = 4, Chicken = 5, Fish = 6,
    Cannibal = 7, Tree = 8, Stone = 9, Iron = 10, Sulfur = 11, Ice = 12,
    Barrel = 13, Lootbox = 14, RoadSign = 15, StackOfWood = 16, Construction = 17,
    Deployable = 18, Human = 19, Player = 20, Vehicle = 21, Hare = 22,
    LootboxBaloon = 23, LootboxBaloonBig = 24, // added by this game update
    Rat = 25,                                  // added by this game update
    Wolf = 26,                                 // added in 11985
};

// ---- Team / clan membership (Oxide.PlayerManager, dump.cs) ------------------
// All three are Mirror SyncVars (they have _Mirror_SyncVarHookDelegate_* twins
// at 0x388/0x3A8 в 11985 — сдвинулись вместе с хвостом; 11966: 0x378/0x390/
// 0x398), so every client sees them for every player.
// 11985: хвост PlayerManager сдвинут ровно +0x10 относительно 11966 —
// вставлены GameObject KSu (0x270) и Ye KSj (0x278), как в бете 11977 (cnN/cFH).
// Всё до вставки (displayName 0x240, aim assist 0x258, wrapper 0x260) на месте.
inline constexpr std::uint64_t PLAYER_USER_ID   = 0x2A8; // string userID; 11966: 0x298
inline constexpr std::uint64_t PLAYER_TEAM_NAME = 0x2B0; // string teamName; 11966: 0x2A0
inline constexpr std::uint64_t PLAYER_CLAN_ID   = 0x2C0; // string clanId; 11966: 0x2B0
inline constexpr std::uint64_t PLAYER_CLAN_TAG  = 0x2C8; // string clanTag; 11966: 0x2B8

// Vehicles: both are SyncVars (uint netId of the vehicle / index of the seat),
// zero while the player is on foot. A mounted player stops updating
// lastSavedPosition, so his box has to come from the rendered transform.
inline constexpr std::uint64_t PLAYER_VEHICLE_ID = 0x2B8; // uint vehicleID; 11966: 0x2A8
inline constexpr std::uint64_t PLAYER_SEAT_ID    = 0x2BC; // uint seatID; 11966: 0x2AC
inline constexpr std::uint64_t PLAYER_ZIPLINE_ID = 0x2DC; // uint ziplineID; 11966: 0x2CC

// ---- World loot containers (Oxide.LootObject : fNZ : Mirror.NetworkBehaviour)
// Everything openable in the world is a LootObject: road crates, barrels,
// airdrops — and the storage boxes players deploy. The deployed ones are
// building pieces, so m_Piece is the discriminator that keeps them off screen.
inline constexpr std::uint64_t LOOTOBJECT_INVENTORY      = 0xA0; // Oxide.Inventory
inline constexpr std::uint64_t LOOTOBJECT_IS_LOOTABLE    = 0xA8; // bool
inline constexpr std::uint64_t LOOTOBJECT_PANEL_NAME     = 0xE0; // string panelName
// 0xF0 in the previous build: a new  System.String m_ContainerSoundKey  field
// was inserted at 0xE8, pushing m_Piece and everything after it up by 8.
// (panelName @0xE0 sits before the insert and is unchanged.)
inline constexpr std::uint64_t LOOTOBJECT_BUILDING_PIECE = 0xF8; // Building.BuildingPiece m_Piece

// ---- Ground pickups (Oxide.ItemPickup : fNZ : Mirror.NetworkBehaviour) ------
// Everything lying on the ground that can be picked up: mushrooms, berries,
// dropped items, harvested resources. It carries the item short name and the
// stack size directly, so no inventory walk is needed.
inline constexpr std::uint64_t ITEMPICKUP_ITEM_OBJECT = 0xA8; // Oxide.Item
inline constexpr std::uint64_t ITEMPICKUP_SHORTNAME   = 0xD8; // string item
inline constexpr std::uint64_t ITEMPICKUP_AMOUNT      = 0xE0; // int amount

// Oxide.GameControllerBase static fields: a known-good NetworkIdentity used to
// learn the NetworkIdentity class pointer (validates dictionary entries).
inline constexpr std::uint64_t GAME_CONTROLLER_NET_IDENTITY_FIELD = 0x8;

// Custom world FOV and first-person model position (VIEWMODEL.md).
inline constexpr std::uint64_t FPMANAGER_WORLD_FOV_TARGET       = 0xAC; // float KLM (11966: BYu/uzH @0x9C; сдвиг от вставки AssetReference @0x80)
inline constexpr std::uint64_t FPOBJECT_DRAW_EVENT              = 0x20; // Oxide_FPObject_Fields.Draw (kAP)
inline constexpr std::uint64_t FPDRAW_ACTION                    = 0x10; // kAP_Fields.Kgi (11966: eDt.BXi)
inline constexpr std::uint64_t DELEGATE_TARGET                  = 0x20; // System_Delegate_Fields.m_target
inline constexpr std::uint64_t MULTICAST_DELEGATES              = 0x78; // System_MulticastDelegate_Fields.delegates
// 11985: FPMotion перестроен ТОЧНО как в бете 11977 — вставлен
// WeaponAimMotionProfile aimMotion (0x38), позы переехали в начало класса
// (Idle 0x40, Run 0x48, RunReload 0x50, Attack 0x58, Aim 0x60, Look 0x68,
// TooClose 0x70), m_OnLadderOffset и m_JumpOffset ИСЧЕЗЛИ (их роль уехала в
// weaponBody 0x78), ссылка на оружие BYc 0xC8 -> KHA 0xD0, Player 0xE8 -> 0x120.
inline constexpr std::uint64_t FPMOTION_OBJECT                  = 0xD0; // FPMotion.KHA (FPObject; 11966: BYc @0xC8)
inline constexpr std::uint64_t FPMOTION_PLAYER                  = 0x120; // FPMotion.Player (kAu; 11966: 0xE8)
inline constexpr std::uint64_t TRANSFORMOFFSET_POSITION         = 0x24; // Oxide_TransformOffset_Fields.m_Position
inline constexpr std::uint64_t TRANSFORMOFFSET_CURRENT_POSITION = 0x44; // Vector3 uhw (11966: BYE)
inline constexpr std::uint64_t FPMOTION_IDLE_OFFSET             = 0x40; // m_IdleOffset (11966: 0x78)
inline constexpr std::uint64_t FPMOTION_RUN_OFFSET              = 0x48; // m_RunOffset (IKTransformOffset; 11966: 0x80)
inline constexpr std::uint64_t FPMOTION_RUN_RELOAD_OFFSET       = 0x50; // runReloadOffset (11966: 0x88)
inline constexpr std::uint64_t FPMOTION_ATTACK_OFFSET           = 0x58; // m_AttackOffset (11966: 0x90)
inline constexpr std::uint64_t FPMOTION_AIM_OFFSET              = 0x60; // m_AimOffset (11966: 0x98)
// m_OnLadderOffset и m_JumpOffset в 11985 не существуют (как и в бете 11977).
// Слоты намеренно указывают на профиль прицеливания: контроллер рук умеет
// отличать такую ситуацию («префаб может делить TransformOffset между позами»)
// и просто пропускает слот, сняв с него владение, вместо чтения мусора.
inline constexpr std::uint64_t FPMOTION_LADDER_OFFSET           = FPMOTION_AIM_OFFSET;
inline constexpr std::uint64_t FPMOTION_JUMP_OFFSET             = FPMOTION_AIM_OFFSET;
inline constexpr std::uint64_t FPMOTION_LOOK_OFFSET             = 0x68; // m_LookOffset (11966: 0xB0)
inline constexpr std::uint64_t FPMOTION_CLOSE_OFFSET            = 0x70; // m_TooCloseOffset (11966: 0xB8)
// «Активный» TransformOffset в перестроенном классе не опознан; слот не читает
// ни один контроллер, значение — позиционно первый TransformOffset после
// ссылок на оружие (KHa; 11966: BYH @0xD8).
inline constexpr std::uint64_t FPMOTION_ACTIVE_OFFSET           = 0xE8;

// Legacy snapshot field; custom hands no longer modify animation speed.
inline constexpr std::uint64_t TRANSFORMOFFSET_LERP_SPEED = 0x20; // Oxide.TransformOffset.m_LerpSpeed

// Номер билда релизного профиля. Единственное место, где он задан: диагностика
// конвейера печатает его в лог (старые APK печатают просто "Release"), а пин-тест
// следит, чтобы при следующем обновлении профиля он менялся вместе со слотами.
inline constexpr int RELEASE_BUILD = 11991;

}
