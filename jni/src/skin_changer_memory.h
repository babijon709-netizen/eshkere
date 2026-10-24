#pragma once
// Скинченджер: подмена скина ствола в руках записью индекса в Oxide.WeaponPiece.
// Только object-скины (другая модель), перекраска материалом не делается —
// см. viewmodel::SkinKind.
//
// ЧТО именно пишется и почему (проверено по dump11966 и дизассемблеру, все
// RVA — из того дампа):
//
//   Oxide.WeaponPiece (структура 0x10 байт в релизе, 0x18 в бете 11977)
//     bool Enabled 0x0 | short Number 0x2 | short Skin 0x4 |
//     byte SkinLevel 0x6 | bool Loaded 0x7 | WeaponModData[] Mods 0x8
//   лежит в HyperHug...Weapons.PlayerWeapon ДВУМЯ копиями:
//     +0x110 (бета +0x130) — SyncVar `weaponPiece` (поток server->client);
//     +0xC0  (бета +0xD0)  — рабочая копия QVN.
//   Раньше чит писал только SyncVar, а вторую копию запрещал трогать
//   («НЕ SyncVar, must not be used»). Устройство это опровергло: лог показал
//   17 циклов записи в SyncVar без единой пересборки вьюхи. Дизассемблер
//   объяснил почему и назвал настоящую цель:
//
//   * РАБОЧАЯ КОПИЯ (+0xC0; в 11978 +0xD0, поле wDT) — вот что уходит в
//     сборку визуала. Интерфейс экипировки hbr (слоты pMj(int) 18 @0x768B988,
//     pMZ(int,bool) 19 @0x7671340; в 11978 jH/kUA) делает `ldp x1,x8,[x0,#0xC0]`
//     и через pFz(WeaponPiece,bool) @0x7671350 зовёт FPManager.Kxi(piece,slot)
//     @0x68EE210 -> CreateFPWeapon. Аналог в 11978 (свежий RE): обёртка
//     0x7BB7088 копирует 0x18 байт из +0xD0/+0xE0 на стек, метод 0x7BB70B4
//     (гейт `ldr w2,[x19,#0xF0]`, FPManager `ldr x0,[x19,#0x110]`, bool +0xF4)
//     зовёт FPManager.LTI(piece&,slot) @0x6D59828 -> та же сборка
//     (d__68/d__69 @0x68EF274/0x68EF65C) -> PlayerFPWeaponView.XZE/MjH
//     (WeaponPiece,EV,Action). MjH @0x7C8DD7C кладёт ПРОШЛУЮ ПО ЗНАЧЕНИЮ piece
//     в копию вьюхи (+0x28/+0x30) и возвращает ничего не сделав, если bit0
//     (Enabled) переданной piece равен нулю.
//   * SkinVisualHandle (11966: +0xB0, 11978: +0xC0) собирается тоже из копии
//     piece: pMd(WeaponPiece)
//     @0x767CF50 пишет принятую piece в +0xC0/+0xC8 и зовёт Ov.pXc @0x76A3CC4
//     -> EN.XLy -> skinData[Skin] -> hbE.VkR. На спавне pMd зовёт
//     OnStartClient @0x767F35C (piece берётся из +0x110), по серверному RPC с
//     piece — десериализатор ppU -> ppb -> pMd.
//   * SyncVar-копия (+0x110) сама по себе визуал НЕ двигает: её хук-обработчик
//     pFR(piece,piece) @0x7675638 — пустой `ret`, Update PlayerWeapon
//     @0x767A520 не читает ни одну копию, у FPManager Update нет вовсе. Её
//     читают серверные string-RPC (pIy/hdz/FvU/iL/FtG -> FPManager.KNw
//     @0x68EB508), phW(hCY) @0x7671D0C (моды) и свойство get_TjU (pFN
//     @0x7690E28).
//   Вывод: писать надо ОБЕ копии — у каждой свой потребитель.
//
//   WeaponPiece.Skin — ИНДЕКС В СОБСТВЕННОМ массиве скинов предмета, а не
//   глобальный id. Это буквально код игры, EN.XLy(WeaponPiece,
//   List<AssetReference>) RVA 0x7CBC768:
//     tbz w20,#0 / tbnz x20,#0x2f -> Enabled==0 или Skin<0: ссылок нет вовсе
//     asr w1,w20,#0x10        -> Number
//     bl  EG.XHb              -> PlayerWeaponPiece (реестр EG, static)
//     ldr x1,[piece+0x18]     -> PlayerWeaponPiece.id
//     bl  ItemData.GwO        -> ItemData
//     ldr x8,[x0,#0xD0]       -> ItemData.skinData (SkinData[])
//     ldr w10,[x8,#0x18]      -> длина, Skin сравнивается с ней
//     add x8,x8,Skin,lsl #3
//     ldr x20,[x8,#0x20]      -> skinData[Skin]
//     lsr x1,x20,#0x30        -> SkinLevel
//     bl  hbE.VkR             -> AssetReference'ы скина
//   То есть достаточно положить в +0x4 другой индекс — дальше игра сама
//   возьмёт другой SkinData и другой префаб.
//
// ПОЧЕМУ Enabled НЕ ТРОГАЕМ. Прежняя версия «форсировала пересборку»: гасила
// Enabled в SyncVar и возвращала через 3 тика, ожидая холстер/дроу. Устройство
// показало ноль эффекта, а дизассемблер объяснил: сборка читает Enabled из
// piece, ПЕРЕДАННОЙ ПО ЗНАЧЕНИЮ (из +0xC0, а не из +0x110), а CreateFPWeapon
// при Enabled==0 вызывает PlayerFPWeaponView.Release() + FPManager.KxS — то
// есть ноль в этом бите рисковал разобрать вьюху, а не пересобрать. Пишутся
// только Skin/SkinLevel, в обеих копиях. §4.21 уточняет: Enabled=1 в рабочую
// копию возвращается — но только зеркалом ПОГАШЕННОЙ головы из SyncVar; это не
// «приказ пересобраться», а возврат канонического состояния, которое там
// оставил бы pMd.
//
// КТО ПЕРЕСОБИРАЕТ. Чит — внешний процесс (только process_vm_readv/writev,
// ptrace-слот занят собственным потомком, инъекции кода нет — mem_io.h,
// stealth.h), поэтому вызвать функции игры он не может. Пересборку делает игра
// на своих событиях: холстер/дроу (pMZ 0x7671340 читает piece из РАБОЧЕЙ
// копии (+0xC0, 11978: +0xD0) и несёт в pFz -> FPManager.Kxi 0x68EE210 -> Kxj 0x68ED5A8 ->
// CreateFPWeapon d__69), RPC насадок/модов (phW читает SyncVar ЖИВЬЁМ ->
// FPManager.Kxi) и спавн/подбор ствола (OnStartClient -> pMd). Дроп/подбор не
// пересобирает (§4.18). Исход вызова pMZ решают два гейта CreateFPWeapon
// (dump11966, MoveNext 0x68EF65C): первый — FPManager._currentWeapon.uhq
// (+0x50 -> +0x38, «показан»): shown -> отложенный RELEASE (0x68EFA40, это
// холстер); второй (на дроу) — Enabled из ГОЛОВЫ РАБОЧЕЙ КОПИИ (+0xC0),
// которую pMZ передаёт по значению: Enabled==1 -> сборка из skinData[Skin]
// с ассет-запросом (0x68EFBC8 -> хвост 0x68EFD34); Enabled==0 -> тот же
// RELEASE — и не запрашивается ничего. Игра гасит голову
// на холстере (00 00 ff ff ff ff 00 00), и устройства v13/v14 провели с
// погашенной головой ВСЮ сессию: каждый дроу выпускал вьюху, ready=0 и ручка
// мертва у всех скинов, включая оригинальный; строки «вьюха собрана» в тех
// логах — враньё баги fp_stale (см. read_live_state). Откат §4.19 был нужен,
// но недостаточен. Задача чита — держать НАШ индекс в SyncVar И в рабочей
// копии, а погашенную голову достраивать зеркалом из SyncVar (§4.21) — тогда
// дроу снова становится настоящей сборкой. На чужом сервере респаун приносит
// серверную piece и OnStartClient может успеть до нашей записи — это граница
// внешнего скинченджера, а не сбой записи.
//
// ТРЕТЬЯ ТОЧКА ЗАПИСИ — Oxide.Item.skin (+0x30, int) и skinLevel (+0x34,
// byte). Доказано дизассемблером dump11966: при дропе ItemPickup.KZq
// (0x6926BDC) копирует Item.skin в SyncVar дропа (KZv -> pickup.skin +0xE4),
// а Item.skinLevel — в piece дропа; PlayerInventory.Gah/Gaz
// (0x69DFD18/0x69E2578) читают Item.skin в RPC синка инвентаря. Запись сюда
// делает дропнутый ствол и сохранение носителем нашего скина.
// А ВОТ пересборку вьюхи дроп/подбор НЕ запускает — опровергнуто тем же
// дизассемблером и устройством (skin.log v11, сборка e75e4d8: Item записан,
// 97 с — ноль пересборок, ноль игровых переходов piece). Факты: PW piece
// SyncVar пишет только сеттер pFL (0x7671CA0), а его вызывают лишь
// мод-обработчики (семейство phW) и пять строковых Command (pIy/hdz/FvU/
// iL/FtG — скин-UI игры); generic-сеттер (0x76918C4) пропускает запись и
// хук, когда значение равно текущему (0x8645EBC -> 0x86451C4), то есть наша
// собственная запись SyncVar подавляет игровой переход; CreateFPWeapon
// (<KNw>d__69 MoveNext 0x68EF65C) берёт скин из piece (state+0x3A ->
// ItemData.skinData[]), а не из Item.skin; подбор/экипировка PW piece не
// трогают вовсе. Локальные триггеры пересборки без модов — холстер/дроу
// (pMZ читает рабочую копию — см. «КТО ПЕРЕСОБИРАЕТ») и СПАВН игрока:
// PW.OnStartClient (0x767F35C) читает SyncVar +0x110 напрямую (ldp). Рецепт
// поэтому: записать скин -> убрать и достать ствол; ПОЛНЫЙ перезапуск игры —
// запасной вариант (автосейв несёт Item.skin и текущий SyncVar). Повторный
// вход в мир БЕЗ перезапуска процесса сам по себе не помогает: устройство
// (skin.log v12, сборка 47985e4) показало переход мира на той же сессии —
// объект игрока переживает переход, OnStartClient не перезапускается, и ноль
// пересборок.
//
// ЧЕТВЁРТАЯ ТОЧКА ЗАПИСИ — ВТОРОЙ ЭКЗЕМПЛЯР тех же Oxide.Item.skin/skinLevel,
// из кэша дроу PlayerWeapon+0x120 (RE 11978). Экип-дроу via(kJw) (Slot 32,
// 0x7BB7540) сначала кэширует в +0x120 Item записи экипировки ([kJw+0x10],
// 0x7BB77D8; при ротации экземпляра обнуляется int +0x11C), затем строит
// SyncVar из ЭТОГО экземпляра — skin +0x30 (0x7BB7864), skinLevel +0x34,
// mods +0x40 — и подаёт piece в FPManager.LTI. Чит резолвит Item по FP-цепи
// (FPM.cur -> FPObject -> Item) и обычно получает тот же экземпляр, но
// серверное обновление Item (ресинк инвентаря) ротирует экземпляр в kJw БЕЗ
// пересборки FPM.cur: цепь чита устаревает, запись уходит в мёртвый
// экземпляр, а каждый дроу читает новый — ствол навсегда с дефолтным скином.
// Симптом устройства (11978): «на некоторых оружиях скины применяются только
// если скидываю и подбираю оружие» — подбор пересобирает FPM.cur, цепи
// сходятся. Лечение: пока кэш дроу отличается от FP-цепи, пишем в ОБА
// экземпляра (страж — тот же ItemData: в окне перехода кэш хранит Item
// ПРОШЛОГО ствола). Срез §4.26 печатает обе копии (item=/ditem=).
//
// АССЕТЫ КАЧАЮТСЯ САМИ — ручная докачка пользователем НЕ нужна (dump11966,
// дизассемблер). Сборка визуала pXc -> hbB.VsH 0x6CDD3B8 -> VkM 0x6CD9418 ->
// VkF 0x6CDAFA4: на каждую AssetReference зовётся hbG.Vqs(out
// ResourceLazyState); ResourceDownloadLedger.get_IsKnown 0xC2ED624 == false ->
// Vkp 0x6CDB1B0 (одноразовый флаг hbB+0x61) -> VkI =
// <RefreshAfterCatalogAsync>d__39 (MoveNext 0x6CDDAD8): hbO.Vqi() — обновление
// каталога; get_RequiresPlaceholder 0xC2ED66C -> Vky(hbU,true) — состояние
// «плейсхолдер» (hbU+0x58=1, флаг +0x65). Дальше ITickable hbB.Tick 0x6CD9CBC
// доводит закачку сам: каждые 250 мс Vkh 0x6CD91B4 ретраит каждый плейсхолдер
// той же сборкой (VkM); раз в 1 с VkX 0x6CD9ED4 отбирает «ближние»
// плейсхолдеры (радиус 80 м от игрока, top-64 — FP-вьюха ствола висит на
// камере, дистанция всегда 0), Vkl 0x6CDA5B4 дедуплицирует их RuntimeKey в
// список uxa, Vkn 0x6CDA810 при изменении набора отдаёт его hbw (слот 3/4 —
// приоритетная очередь закачки, таймаут 5000 мс). hbG/hbV/hbw/hbO — интерфейсы
// resourceDownloadService (реализация hba), статусы — SkinDownloadState/
// SkinDownloadSummary. Как только ссылка становится известна/скачана, ретрай
// Vkh собирает НАСТОЯЩИЙ визуал. Вывод: достаточно ОДНОЙ пересборки из нашего
// индекса (триггеры — блок «КТО ПЕРЕСОБИРАЕТ» выше); «ассет не запрашивался»
// означает «сборки ещё не было», а не «пользователь должен скачать».
//
// ДИАГНОСТИКА «пересобралось ли»: PlayerWeapon.playerFPWeaponView (релиз
// 11978 и бета +0x108; 11966: +0xF0) хранит piece, ИЗ КОТОРОЙ вьюха построена
// (+0x28). Пока weaponPiece.Skin != fpView.piece.Skin — визуал старый
// (fp_stale в меню). Рядом PlayerWeapon.skinVisualHandle (11966: +0xB0,
// 11978/бета: +0xC0): _model == 0 означает, что
// визуал скина не загружен вовсе.
//
// СБОРКА МОЖЕТ УПАСТЬ (§4.22, «отравление Томпсоном»). CreateFPWeapon не
// обёрнут в try/catch, а бросить внутри есть чему: XZN 0x7C8DFDC проверяет
// Skin по длине ItemData.skinData (IndexOutOfRange 0x7C8E274) и record по
// Number (NRE 0x7C8E270), хвост сборки дважды проверяет Number по
// prefabTable (IndexOR 0x68F0420/0x68F046C), LoadAssetAsync и instantiate
// тоже могут бросить. ASSEMBLY 0x68EFE84 уже сделала _currentWeapon = новый
// FPWeapon (0x68F00B0) и SetActive(false), но On_Draw (uhq=1, Item=аргумент)
// живёт ПОСЛЕ точки броска — исключение оставляет полусобранный панцирь:
// uhq==0, Item==null при живом _currentWeapon (здоровый инвариант — uhq==1
// тогда и только тогда, когда Item!=null). Дальше каждый дроу обречён: блок
// сборки уничтожает и обнуляет _currentWeapon только на здоровом пути
// (0x68EFC9C), а на панцире гейты снова идут в сборку, которая снова падает
// на том же скине. Устройство показало именно это: первый сломанный ствол
// (Томпсон) утянул за собой все — дроу любого оружия гоняет ту же пересборку
// через один FPManager. Резолв нашего контроллера на панцире честно падает на
// стадии 9 (Item==null) — отсюда «нет скинов» в меню после поломки. Выход
// один: обнулить _currentWeapon извне (перезаход на сервер делает то же
// пересозданием FPManager) и не давать упавшему скину немедленно отравить
// сборку снова — отсюда детектор панциря (45 с наблюдения: здоровая ПЕРВАЯ
// закачка ассета выглядит так же, On_Draw приходит только в async-колбэке),
// одноразовый сброс и карантин скина по ItemData.m_Id ствола.
//
// СООТВЕТСТВИЕ ПРАВИЛАМ РЕПО:
//   * память — только через IO (в GUI это HeapMemory: read мимо кэша, запись
//     через ворота heap_gate_allows), своего fd нет — KERNEL.md §4.1/§4.2;
//   * запись только по расхождению со свежим чтением + readback — §4.10;
//   * оригиналы возвращаются на выключении/смене ствола/отвале привязки, а при
//     смене сессии адреса забываются БЕЗ записи — как у no_recoil/weapon_tuning;
//   * DETECTION.md: SyncVar weaponPiece — поток server->client, рабочая копия
//     +0xC0 — не сетевое поле вовсе; в исходящих пакетах скина нет, значит это
//     тир 3 (клиентский визуал), не тир 5.
#include "viewmodel.h"
#include "viewmodel_memory.h"
#include "game_offsets_active.h"
#include "weapon_tuning_memory.h"   // pointer(), Identity, Life, kWeapons
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace skinchanger {
using weapon_tuning::pointer;
using weapon_tuning::Identity;
using weapon_tuning::Life;
using weapon_tuning::kWeapons;

// Пространства имён классов, которые резолвятся по имени (когда TypeInfo-слот
// в профиле отсутствует или не читается).
inline constexpr const char* kPlayerNs = "HyperHug.Games.Oxide.Features.Player";

// Массив skinData у предмета бывает ДЛИННЫМ: устройство (§4.23) показало
// популярные стволы (Томпсон, Дезерт Игл, Самопал), у которых позиций больше
// прежнего предела 24 — и ствол отвергался целиком с враньём «нет скинов».
// Предел списка теперь 64 = SKIN_INDEX_MAX + 1 (sanitize дальше не пустит
// индекс, запись большего числа — мусор в SyncVar). Строка длиннее kIdCap по-
// прежнему означает, что мы смотрим не на тот объект: id скинов — короткие
// ascii-идентификаторы ассетов.
constexpr int kMaxSkins = viewmodel::kSkinEntryMax;
constexpr int kIdCap    = 40;

// Поле, в которое пишем. Повторяет семантику viewmodel::Patch (снимок ->
// запись только по расхождению -> возврат оригинала, если поле всё ещё наше),
// но пишет через io.write_heap: HeapMemory намеренно не даёт незащищённого
// write, все heap-контроллеры ходят только через ворота (game_viewmodel.cpp).
template<class T> struct Slot {
    uint64_t address = 0;
    T original{}, last{}, sampled{};
    bool owned = false;
    bool fresh = false;

    static bool same(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
    template<class IO> bool read(IO& io, T& out) const {
        out = {};
        return address >= 0x10000 && io.read(address, out);
    }
    // Значение уже прочитано (голова WeaponPiece берётся одним запросом, см.
    // read_piece), поэтому ни чтения, ни проверки значения на диапазон здесь
    // нет. Адрес доказывает структура piece, а сам Skin у ствола без скина
    // legitimately отрицательный — range-проверка прочитанного значения и
    // ставила скинченджер на «поле скина не читается» на исправном билде.
    void sample_value(uint64_t addr, T current) {
        // Оригиналом считается то, что лежит в поле, пока это НЕ наша запись.
        if (!owned || address != addr || !same(current, last)) original = current;
        address = addr; sampled = current; fresh = true;
    }
    // Снять «свежесть»: следующий put() запишет значение, даже если оно уже
    // совпадает с прочитанным. Так работает запрос «переписать обе копии».
    void invalidate() { fresh = false; }
    template<class IO> bool put(IO& io, T value) {
        if (!viewmodel::valid(value) || address < 0x10000) return false;
        // Уже совпадает (в том числе потому, что это наша прошлая запись) —
        // ноль обращений к памяти игры сверх одного чтения.
        if (fresh && same(value, sampled)) { fresh = false; return true; }
        if (!io.write_heap(address, value)) return false;
        T back{};
        if (!io.read(address, back) || !same(back, value)) return false;   // readback
        last = value; sampled = value; owned = true; fresh = false;
        return true;
    }
    template<class IO> bool restore(IO& io) {
        if (!owned) { *this = {}; return true; }
        T current{};
        if (!read(io, current)) return false;   // не читается — владение не снимаем
        // Поле принадлежит и игре: возвращаем оригинал, только если там всё
        // ещё наша запись, иначе затрём чужое обновление (пересинк SyncVar).
        if (same(current, last) && !io.write_heap(address, original)) return false;
        *this = {};
        return true;
    }
};

// Oxide.WeaponPiece как он лежит внутри PlayerWeapon. В релизе 0x10 байт, в бете
// 0x18 (добавился EmptyHands 0x10), но интересующая нас голова Enabled..Loaded
// — первые 8 байт в обоих профилях, поэтому размер берётся из профиля, а
// проверяется только голова.
struct Piece {
    uint8_t enabled = 0;   // 0x0 bool
    int16_t number = 0;    // 0x2 номер предмета (item id из EG.Rpc/Rpk)
    int16_t skin = 0;      // 0x4 индекс в ItemData.skinData[] этого предмета
    uint8_t level = 0;     // 0x6 уровень WeaponProgressionSkinData
    uint8_t loaded = 0;    // 0x7 bool, «визуал скина загружен»
};

enum class PieceRead : int {
    Ok = 0,
    Unreadable,   // транспорт не смог прочитать голову: адрес/привязка
    NotAPiece,    // прочиталось, но это не WeaponPiece: оффсет не от этого билда
};

// Номер предмета — короткий id из таблицы EG.Rpc. Верхняя граница взята с
// большим запасом: её задача отсечь мусор по неверному адресу, а не описать
// реальное количество предметов.
inline constexpr int kPieceNumberMax = 4095;
// Голова piece — Enabled 0x0, Number 0x2, Skin 0x4, SkinLevel 0x6, Loaded 0x7:
// 8 байт, одинаковые в релизе (sizeof 0x10) и в бете (0x18, добавился EmptyHands
// 0x10). Читаем ровно её: хвост (Mods/EmptyHands) не нужен, а лишние байты —
// лишний шанс упереться в конец отображения.
inline constexpr size_t kPieceHead = 8;

// Голова piece ОДНИМ чтением. Три отдельных чтения int16/uint8 стоили бы трёх
// обращений к транспорту на тик и могли бы прийти из разных состояний поля
// (игра пересинкивает SyncVar между чтениями).
//
// Адрес доказывается СТРУКТУРОЙ, а не диапазоном skin: Enabled и Loaded — bool,
// обязаны быть 0/1, Number — неотрицательный короткий id. Такое доказательство
// не отвергает легитимные значения Skin (отрицательное «скина нет», индекс
// больше 63 у предмета с большим набором), в отличие от range-проверки самого
// поля, из-за которой исправный билд выглядел как «поле не читается».
template<class IO>
// raw (если задан) получает байты головы как есть — для диагностики в логе.
// relaxed — ослабленная валидация ДЛЯ РАБОЧЕЙ КОПИИ (+0xC0; 11978: +0xD0):
// собственный путь дроу игры (pMZ читает piece из +0xC0 и несёт в pFz -> FPManager.Kxi ->
// CreateFPWeapon) решает исход по Enabled, а Loaded/Number в этой копии бывают
// неканоническими. Ранее здесь утверждалось «CreateFPWeapon смотрит только
// Enabled» — дизассемблер dump11966 это опроверг: Number bounds-check'ится
// дважды (0x68EFD9C -> IndexOR 0x68F0420 и 0x68EFCF4 -> IndexOR 0x68F046C),
// хвост сборки индексирует prefabTable по Number (+0x3a), НЕ по Skin.
// Ослабление всё равно верное: мусорный Number игра обработает сама (броском,
// за которым присматривает детектор панциря §4.22), а строгая валидация
// рабочей копии ставила исправный билд на «стадия 21» навсегда (устройство
// это показало: SyncVar живой, вьюха живая, а +0xC0 «не WeaponPiece»).
// SyncVar-копия остаётся строгой: её пишет игра, и мусор там действительно
// означает чужую раскладку.
PieceRead read_piece(IO& io, uint64_t addr, Piece& out, uint8_t* raw = nullptr,
                     bool relaxed = false) {
    using namespace go_active;
    out = {};
    if (raw) std::memset(raw, 0, kPieceHead);
    if (addr < 0x10000) return PieceRead::Unreadable;
    // Профиль обязан описывать голову внутри этих 8 байт: иначе раскладка чужая,
    // и это NotAPiece, а не выход за границу буфера.
    if (WEAPONPIECE_ENABLED >= kPieceHead || WEAPONPIECE_SKIN_LEVEL >= kPieceHead ||
        WEAPONPIECE_LOADED >= kPieceHead || WEAPONPIECE_NUMBER + 1 >= kPieceHead ||
        WEAPONPIECE_SKIN + 1 >= kPieceHead)
        return PieceRead::NotAPiece;
    uint8_t head[kPieceHead] = {};
    if (!io.read_range(addr, head, kPieceHead)) return PieceRead::Unreadable;
    if (raw) std::memcpy(raw, head, kPieceHead);
    out.enabled = head[WEAPONPIECE_ENABLED];
    out.loaded  = head[WEAPONPIECE_LOADED];
    out.level   = head[WEAPONPIECE_SKIN_LEVEL];
    std::memcpy(&out.number, head + WEAPONPIECE_NUMBER, sizeof(out.number));
    std::memcpy(&out.skin,   head + WEAPONPIECE_SKIN,   sizeof(out.skin));
    // Enabled — bool в любой копии: не-bool значит это не WeaponPiece вовсе
    // (и гейт CreateFPWeapon с таким значением ушёл бы в Release).
    if (out.enabled > 1) return PieceRead::NotAPiece;
    if (relaxed) return PieceRead::Ok;
    if (out.loaded > 1) return PieceRead::NotAPiece;
    if (out.number < 0 || out.number > kPieceNumberMax) return PieceRead::NotAPiece;
    return PieceRead::Ok;
}

// 8 байт головы в "xx xx ..." для лога/CLI: 23 символа + NUL.
inline void format_piece_head(const uint8_t (&head)[kPieceHead], char (&out)[24]) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < kPieceHead; ++i) {
        out[i * 3]     = hex[head[i] >> 4];
        out[i * 3 + 1] = hex[head[i] & 0xF];
        out[i * 3 + 2] = ' ';
    }
    out[kPieceHead * 3 - 1] = '\0';
}

struct Context {
    Identity player, weapon, fp, fp_object, item, item_data, skins;
    uint64_t fp_view = 0;      // PlayerWeapon.playerFPWeaponView (диагностика)
    uint64_t piece = 0;        // адрес SyncVar-копии Oxide.WeaponPiece (+0x110)
    uint64_t piece_local = 0;  // адрес рабочей копии (+0xC0) — цель записи №2
    // §4.24: FP-объект взят из типизированного кэша uzN (+0x58), потому что
    // _currentWeapon (+0x50) пуст. Диагностика зомби-резолва: игра уронила
    // сборку до хранения FPObject, а цепочка Item/ItemData ещё читается
    // через прежний объект — чит считает «применено», вьюха мертва. В
    // operator== НЕ входит: смена флага — не смена контекста (иначе каждый
    // дроу дёргал бы restore_all/перескан).
    bool via_cache = false;
    bool operator==(const Context& b) const {
        return player == b.player && weapon == b.weapon && fp == b.fp &&
               fp_object == b.fp_object && item == b.item &&
               item_data == b.item_data && skins == b.skins &&
               fp_view == b.fp_view && piece == b.piece &&
               piece_local == b.piece_local;
    }
    // §4.27: тот же ствол с точностью до пересозданных игрой экземпляров.
    // Каждый дроу линкует свежий Item (skin.log: It вращается на каждом цикле
    // uhq 0->1, а PW/ItemData/skinData/обе piece стоят) — это НЕ смена ствола:
    // список скинов тот же (он висит на ItemData, не на Item), оригиналы те же.
    bool same_gun(const Context& b) const {
        return player == b.player && weapon == b.weapon && fp == b.fp &&
               item_data == b.item_data && skins == b.skins &&
               fp_view == b.fp_view && piece == b.piece &&
               piece_local == b.piece_local;
    }
};

// il2cpp System.String: длина (int32) в +0x10, символы UTF-16 в +0x14.
// Читаем по одному uint16: range-чтения нет у хост-теста, а id короткие.
// Суррогаты не разбираем — в идентификаторах ассетов их не бывает, замена на
// '?' сохраняет строку печатаемой.
template<class IO>
bool read_id(IO& io, uint64_t str, char* out, size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = '\0';
    if (str < 0x10000) return false;
    int32_t length = 0;
    if (!io.read(str + go_active::IL2CPP_STRING_LENGTH, length)) return false;
    if (length <= 0 || length > kIdCap) return false;
    size_t pos = 0;
    for (int32_t i = 0; i < length; ++i) {
        uint16_t unit = 0;
        if (!io.read(str + go_active::IL2CPP_STRING_CHARS + (uint64_t)i * 2u, unit)) return false;
        uint32_t cp = unit;
        if (cp >= 0xD800u && cp <= 0xDFFFu) cp = '?';
        char enc[3];
        int n = 0;
        if (cp < 0x80) {
            if (cp < 0x20 || cp == 0x7F) cp = '?';
            enc[0] = (char)cp; n = 1;
        } else if (cp < 0x800) {
            enc[0] = (char)(0xC0 | (cp >> 6));
            enc[1] = (char)(0x80 | (cp & 0x3F));
            n = 2;
        } else {
            enc[0] = (char)(0xE0 | (cp >> 12));
            enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[2] = (char)(0x80 | (cp & 0x3F));
            n = 3;
        }
        if (pos + (size_t)n >= cap) break;
        for (int k = 0; k < n; ++k) out[pos++] = enc[k];
    }
    out[pos] = '\0';
    return pos > 0;
}

// Точное имя класса по Il2CppClass*: klass->name (IL2CPP_CLASS_NAME), прочитан
// побайтово и сравнен ВМЕСТЕ с завершающим нулём — имя, начинающееся с образца
// («WeaponObjectSkinDataV2»), образцом не пройдёт. Намеренно без допуска по
// instance_size из HeapMemory::named_klass: у всех SkinData-наследников размер
// «подходит», и с допуском все шесть типов схлопнулись бы в один — первый
// проверенный.
template<class IO>
bool klass_name_is(IO& io, uint64_t klass, const char* name) {
    uint64_t text = 0;
    if (!pointer(io, klass + go_active::IL2CPP_CLASS_NAME, text) || text < 0x10000)
        return false;
    const size_t length = std::strlen(name);
    if (length == 0 || length >= 48) return false;
    char buf[48] = {};
    for (size_t i = 0; i <= length; ++i) {
        char c = 0;
        if (!io.read(text + i, c)) return false;
        buf[i] = c;
    }
    return std::memcmp(buf, name, length + 1) == 0;
}

// §4.24: имя класса объекта в буфер — для диагностики панциря. Читает тот же
// klass->name (IL2CPP_CLASS_NAME), что klass_name_is, но БЕЗ образца: в логе
// имя само по себе вердикт («FPBow» — у его FDN override NRE на погашенном
// PlayerEventHandler, «FPHitscan» — базовый FDN выходит молча). Нечитаемое —
// пустая строка, не догадка.
template<class IO>
void klass_name_read(IO& io, uint64_t obj, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    uint64_t klass = 0, text = 0;
    if (!pointer(io, obj, klass) ||
        !pointer(io, klass + go_active::IL2CPP_CLASS_NAME, text) || text < 0x10000)
        return;
    size_t pos = 0;
    while (pos + 1 < cap) {
        char c = 0;
        if (!io.read(text + pos, c) || c == '\0') break;
        out[pos++] = c;
    }
    out[pos] = '\0';
}

// Тип скина — сравнением класса с TypeInfo-слотом профиля. Слот быстрее всего
// (одно разыменование qword) и есть в обоих профилях для всех классов скинов
// (в бете нет только EG, он здесь и не нужен). НО слот — абсолютный RVA,
// привязанный и к билду дампа, и к базе libil2cpp, на которую привязался чит.
// Оба якоря плавают: игра перезапускается через exec (pid сохраняется) и в
// карте остаётся старый «(deleted)» образ с мёртвыми метаданными, а во время
// загрузки TypeInfo-статики живого образа ещё NULL — привязка в этот момент
// берёт первый читаемый кандидат (game.cpp, package_verified_release) и слоты
// начинают давать мусор. Проверено устройством (skin.log): в одной сессии все
// типы деградировали в «other» при той же версии игры, а цепочка, запись
// индекса и readback продолжали работать — они позиционно независимы (классы
// сверяются по имени от самого объекта, игрок находится мирочиталкой).
// Поэтому при промахе слотов есть резерв, которому база не нужна вовсе:
// точное имя класса читается через klass самого объекта (klass_name_is).
// Строки читаются только когда слоты уже не совпали, поэтому при здоровой
// привязке резерв не стоит ни одного лишнего чтения.
template<class IO>
viewmodel::SkinKind kind_of(IO& io, uint64_t base, uint64_t skin_klass) {
    using namespace go_active;
    if (!skin_klass || skin_klass < 0x10000) return viewmodel::SkinKind::Unknown;
    const std::uint64_t slots[] = {
        WEAPONPROGRESSIONSKIN_TYPEINFO_RVA,
        WEAPONPREVIEWPREFABSKIN_TYPEINFO_RVA,
        WEAPONOBJECTSKIN_TYPEINFO_RVA,
        WEAPONRECOLORSKIN_TYPEINFO_RVA,
        EQUIPOBJECTSKIN_TYPEINFO_RVA,
        EQUIPRECOLORSKIN_TYPEINFO_RVA,
    };
    const viewmodel::SkinKind kinds[] = {
        viewmodel::SkinKind::WeaponProgression,
        viewmodel::SkinKind::WeaponPreview,
        viewmodel::SkinKind::WeaponObject,
        viewmodel::SkinKind::WeaponRecolor,
        viewmodel::SkinKind::EquipObject,
        viewmodel::SkinKind::EquipRecolor,
    };
    // Точные имена классов в порядке kinds[]. Имена в метаданных не
    // облифусцированы (dump.cs несёт их как есть) и не зависят ни от ASLR,
    // ни от номера сборки.
    static const char* const names[] = {
        "WeaponProgressionSkinData",
        "WeaponPreviewPrefabSkinData",
        "WeaponObjectSkinData",
        "WeaponRecolorSkinData",
        "EquipmentObjectSkinData",
        "EquipmentRecolorSkinData",
    };
    constexpr size_t kKinds = sizeof(kinds) / sizeof(kinds[0]);
    for (size_t i = 0; base && i < kKinds; ++i) {
        if (!slots[i]) continue;
        // Слот — ВНУТРЕННЯЯ .data-ячейка: одно разыменование даёт Il2CppClass*
        // (тот же механизм, что у PLAYER_MANAGER_TYPEINFO_RVA).
        uint64_t klass = 0;
        if (pointer(io, base + slots[i], klass) && klass == skin_klass) return kinds[i];
    }
    for (size_t i = 0; i < kKinds; ++i)
        if (klass_name_is(io, skin_klass, names[i])) return kinds[i];
    return viewmodel::SkinKind::Other;
}

// Число уровней у прогрессии: FPPrefabRefs — AssetReference[], его длина и
// есть число уровней. У остальных классов уровень один.
template<class IO>
int levels_of(IO& io, uint64_t skin, viewmodel::SkinKind kind) {
    if (kind != viewmodel::SkinKind::WeaponProgression) return 1;
    uint64_t refs = 0;
    if (!pointer(io, skin + go_active::WEAPONSKIN_PROG_FP_REFS, refs) || refs < 0x10000)
        return 1;
    int32_t length = 0;
    if (!io.read(refs + go_active::IL2CPP_ARRAY_LENGTH, length)) return 1;
    if (length <= 0 || length > go_active::SKIN_LEVEL_MAX + 1) return 1;
    return length;
}

// Запрашивался ли ассет скина: AssetReference хранит не кэш ассета, а ручку
// операции (m_Operation, первый член — IAsyncOperation*). Ненулевой указатель
// означает, что Addressables эту ссылку уже грузил. Докачка НЕ ручная: сборка
// визуала сама ставит недостающий ассет в очередь (блок «АССЕТЫ КАЧАЮТСЯ САМИ»
// в шапке), поэтому «не ready» — признак того, что сборки из этого индекса ещё
// не было, а не того, что пользователь должен что-то скачать.
template<class IO>
bool asset_ready(IO& io, uint64_t skin, viewmodel::SkinKind kind) {
    using namespace go_active;
    uint64_t field = 0;
    bool is_array = false;
    switch (kind) {
        case viewmodel::SkinKind::WeaponObject:      field = WEAPONSKIN_FP_REF; break;
        case viewmodel::SkinKind::WeaponProgression: field = WEAPONSKIN_PROG_FP_REFS; is_array = true; break;
        case viewmodel::SkinKind::WeaponPreview:     field = WEAPONSKIN_PREVIEW_REF; break;
        case viewmodel::SkinKind::WeaponRecolor:     field = WEAPONSKIN_RECOLOR_MATERIAL_FP; break;
        case viewmodel::SkinKind::EquipObject:       field = EQUIPSKIN_FP_OBJECT_REF; break;
        case viewmodel::SkinKind::EquipRecolor:      field = EQUIPSKIN_OBJECT_REF; break;
        default: return false;
    }
    uint64_t reference = 0;
    if (is_array) {
        uint64_t array = 0;
        if (!pointer(io, skin + field, array) || array < 0x10000) return false;
        int32_t length = 0;
        if (!io.read(array + IL2CPP_ARRAY_LENGTH, length) || length <= 0) return false;
        if (!pointer(io, array + IL2CPP_ARRAY_FIRST_ELEMENT, reference)) return false;
    } else if (!pointer(io, skin + field, reference)) {
        return false;
    }
    if (reference < 0x10000) return false;
    uint64_t operation = 0;
    return io.read(reference + ASSETREF_OPERATION, operation) && operation != 0;
}

template<class IO> class Controller {
    IO& io;
    uint64_t session_ = 0, base_ = 0;
    Context context_{};
    uint64_t weapon_cache_ = 0;

    // SyncVar-копия (+0x110): её читают серверные string-RPC, phW и свойство
    // get_TjU. Рабочая копия (+0x110-0x50): из неё hbr.pMZ подаёт piece в
    // CreateFPWeapon на дроу, и её же перезаписывает pMd. Обе — цели записи.
    // Голову рабочей копии игра гасит на холстере (Enabled=0), а погашенная
    // голова — вердикт Release для сборки (KERNEL.md §4.21), поэтому у головы
    // свои зеркальные слоты из SyncVar.
    Slot<int16_t> skin_;
    Slot<uint8_t> level_;
    Slot<int16_t> skin_local_;
    Slot<uint8_t> level_local_;
    Slot<uint64_t> head_lo_local_;   // +0xC0: Enabled/Number/Skin/Level/Loaded
    Slot<uint64_t> head_hi_local_;   // +0xC8: Mods — pMd несёт оба qword
    // Копия 3 — Oxide.Item.skin (+0x30, int32) / skinLevel (+0x34, byte):
    // дроп и сохранение несут наш скин (см. заголовок файла). Семантика та же: снимок ->
    // запись по расхождению -> readback -> возврат оригинала.
    Slot<int32_t> skin_item_;
    Slot<uint8_t> level_item_;
    // Копия 4 — ВТОРОЙ экземпляр Oxide.Item из кэша дроу (pw+0x120):
    // via(kJw) (Slot 32, 0x7BB7540) на каждом экип-дроу кэширует туда Item
    // записи экипировки ([kJw+0x10]) и строит SyncVar из ЕГО skin(+0x30)/
    // skinLevel(+0x34)/mods(+0x40). Обычно это тот же экземпляр, что в
    // FP-цепи (копия 3), но серверное обновление Item ротирует экземпляр в
    // kJw без пересборки FPM.cur: FP-цепь устаревает, запись уходит в мёртвый
    // экземпляр, а дроу вечно читает новый — ствол навсегда с дефолтным
    // скином, лечит только дроп+подбор (устройство 11978: «на некоторых
    // оружиях скины применяются только если скидываю и подбираю»). Пишем в
    // оба, пока они расходятся; страж по тому же ItemData не даёт писать в
    // чужой ствол в окне перехода (кэш обновляется самим дроу ДО чтения
    // skin). В operator== контекста НЕ входит по той же причине, что
    // via_cache: ротация экземпляра — не смена ствола.
    Identity draw_item_{};
    Slot<int32_t> skin_item2_;
    Slot<uint8_t> level_item2_;
    // §4.29: ШЕСТАЯ точка — piece ВНУТРИ FPWeaponView (view+0x28). Сессия 41:
    // SV/wk/Item держат наш индекс, иконка (живой читатель Item) показывает
    // скин, а руки — нет; vw piece — ЕДИНСТВЕННАЯ видимая копия, мёрзлая на
    // -1. Значит, сборка печёт -1 во все нисходящие копии, а рендер читает
    // либо piece вьюхи, либо что-то невидимое (хэндл/модель). Если рендер
    // читает piece вживую (recolor — материал, не модель), пост-сборочная
    // запись чинит руки без гонки: вьюха стабильна (vw= const всю сессию),
    // переписанное игрой на дроу Slot-семантика перезахватывает сама.
    // Страж — Number вьюхи равен Number SyncVar и >= 0: красим ровно тот
    // ствол, что резолвнули (руки с Number -1 при погашенной SyncVar под
    // страж не проходят). Пишем ТОЛЬКО Skin/Level — Number/Enabled чужие.
    Slot<int16_t> skin_view_;
    Slot<uint8_t> level_view_;
    int view_state_ = -1;    // piece вьюхи: -1 не ведём, 0 наш, 1 чужой
    // §4.30: ЭКСПЕРИМЕНТ — сброс флага Loaded (+0x7) в SV/wk/vw. Сессия 36:
    // все шесть точек держат наш индекс, руки не смотрят, хэндл модели нулём
    // (hnd=0), ассет ни разу не запрашивался. Сборка печёт -1 и не ставит
    // ассет в очередь; пост-сборочная краска очередь не трогает. Если гейт
    // запроса — Loaded («не загружен — переоцени piece и запроси»), сброс
    // при нашем индексе в поле запустит запрос и резолв хэндла. Безопасное
    // направление ТОЛЬКО 1->0 и ТОЛЬКО пока хэндл мёртв: установка 1 при
    // нулевом хэндле могла бы уронить рендер, живой хэндл — руки прочь.
    Slot<uint8_t> loaded_sv_, loaded_wk_, loaded_view_;
    // §4.28: ПЯТЫЙ источник — стабильный инвентарный Oxide.Item. Каждый дроу
    // на сервере строит вьюху из -1, хотя все четыре точки держат наш индекс:
    // via(kJw)->drawSV пишет серверную piece в SyncVar и тут же кормит её в
    // LTI — окно нулевое, снаружи не вклиниться (§4.27 это доказал нулем
    // побед). А дроп+подбор лечит — подбор кладёт наш индекс в инвентарный
    // экземпляр, и дроу копируют его. Значит, свежий Item каждого дроу
    // создаётся КОПИЕЙ инвентарного (skin -1 сервера), и писать надо его:
    // одна запись — все будущие дроу копируют наше, без гонки. Раскладка
    // контейнеров PlayerInventoryData/Client неизвестна (дампа здесь нет),
    // поэтому адрес ищется зондированием: массив/поле, чей элемент — живой
    // Oxide.Item ровно нашего ItemData. Валидация — ТОЧНОЕ совпадение
    // указателя ItemData (64 бита мимо не проходят) + стабильность klass:
    // имя класса не сверяется (в бинаре оно может быть обфусцировано, а
    // ItemData уже доказывает, что это наш предмет). Пишем только в
    // подтверждённый два тика подряд адрес; транзиенты FP-цепи и кэша дроу
    // исключены (их ведут копии 3–4, а ротация каждый дроу давала бы churn).
    struct InvTarget {
        uint64_t ref = 0;    // адрес, ПО КОТОРОМУ лежит указатель на Item
                             // (слот массива либо поле структуры — ресинк
                             // меняет экземпляр, ссылка перечитывается)
        Identity id;         // последний подтверждённый экземпляр
        Slot<int32_t> skin;
        Slot<uint8_t> level;
    };
    static constexpr int kMaxInv = 4;
    InvTarget inv_[kMaxInv]{};
    int inv_count_ = 0;
    // Кандидаты зонда с прошлого тика: усыновление — только подтверждённым
    // дважды (транзиент, мелькнувший один тик, не пишется никогда).
    uint64_t inv_probe_cands_[kMaxInv * 2]{};
    int inv_probe_n_ = 0;
    int inv_probe_tick_ = 0; // каденс зонда: проход каждый 5-й тик, пока пусто
    // §4.28: якоря последнего прохода зонда для среза (1 — жив, 0 — нет).
    bool inv_dbg_data_ = false, inv_dbg_client_ = false, inv_dbg_warr_ = false;
    // §4.31: форма последнего прохода: длина warr-массива (-1 — не массив),
    // L1-полей, опознанных массивами, попыток push в L2 (o>кап — переполнение
    // бюджета) и «почти-попаданий» (читаемый +0x20, но чужой ItemData: пояс
    // держит Item'ы, но не нашего ствола). Ноль новых чтений — те же проходы.
    int inv_dbg_wlen_ = -1, inv_dbg_arrays_ = 0, inv_dbg_l2push_ = 0;
    int inv_dbg_miss_ = 0;
    // §4.32: kJw-форма записи экипировки (P+0x10 -> Item ровно нашего
    // ItemData): k — живых, z — с нулевым Item и слотом u32 <= 64 (пустая
    // ветвь via голодает), ks — skin первого живого (ok — есть хоть один).
    // pw — 4-й флаг inv=: PlayerWeapon отсканирован своим L1.
    bool inv_dbg_pw_ = false;
    int inv_dbg_kjw_ = 0, inv_dbg_kjw_null_ = 0;
    int32_t inv_dbg_kjw_skin_ = 0;
    bool inv_dbg_kjw_skin_ok_ = false;
    // §4.33: zh — ПУСТОЙ kJw-слот, совпавший с живым слотом дроу (pw+0x150):
    // держит ли обнулённая запись именно наш слот. zhm — сколько таких
    // совпадений (слоты уникальны — >1 подозрительно), za — адрес первого
    // (стабильность адреса между проходами = запись инъектируема).
    int64_t inv_live_slot_ = -1;
    bool inv_dbg_zh_ok_ = false;
    uint32_t inv_dbg_zh_ = 0;
    int inv_dbg_zhm_ = 0;
    uint64_t inv_dbg_za_ = 0;
    // §4.37: rej — кандидаты zh, отклонённые как массивы (bounds=null +
    // length=N совпадают с kJw-формой null+слот; класс кончается на "]").
    int inv_dbg_rej_ = 0;
    // §4.34: yh — запись СВОЕГО живого слота с НЕ-нулевым Item (stale/чужой:
    // гейт LzY может ронять и такой — валидация глубже null-чека), yhm —
    // сколько таких. zh (null-fill) предпочтительнее yh (overwrite).
    bool inv_dbg_yh_ok_ = false;
    uint64_t inv_dbg_yh_ = 0;
    int inv_dbg_yhm_ = 0;
    // §4.40: yh-any — первая ВАЛИДНАЯ (Item нашего дефа, слот<=64) запись
    // БЕЗ привязки к живому слоту (адрес/hex/x), её слот и счётчик. kJw
    // может жить в другом слотовом пространстве — yh молчит не потому,
    // что записей нет, а потому, что слот чужой. Только чтение.
    bool inv_dbg_yha_ok_ = false;
    uint64_t inv_dbg_yha_ = 0;
    uint32_t inv_dbg_yhas_ = 0;
    int inv_dbg_yhan_ = 0;
    // §4.41: yhn — ЧУЖИЕ (X, слот<=64): X не валидируется нашим дефом и не
    // наш, но klass читается (настоящий объект, не мусор). kJw мог протухнуть
    // в Item прошлого ствола (чужой деф, скин -1 — validate слеп, yh-any
    // молчит) — via печёт его -1 каждый дроу. Имя класса первой (Item =
    // протухший kJw, остальное = шум). Только чтение.
    bool inv_dbg_yhx_ok_ = false;
    uint64_t inv_dbg_yhx_ = 0;
    uint32_t inv_dbg_yhxs_ = 0;
    int inv_dbg_yhn_ = 0;
    char inv_dbg_yhxc_[12] = {};
    // §4.42: chain-walk гейтов G3 (vVT/HcG) и G4 (LzY). Дизассемблер
    // dump11978: kJw = HZu()[slot] (HZu = inv+0x30→+0xD8, массив в H+0x10),
    // HcG требует flagobj/байт/H/слот-в-диапазоне/kJw/Item/Krf==0, vVT =
    // HcG && !LWM (durMax>0 && !unbrk && krm<=0). kh = первый упавший хоп
    // (0-6 цепочка, 7 Item-null = G4, 8 Krf, 9 LWM, a LWM-цепь нечитаема,
    // f PASS), kv/kl/kf/kb — вердикты/биты, ks — слот HcG (server+0xE8),
    // kj — kJw живого слота (элемент дроу). Только чтение.
    int inv_dbg_kh_ = 0;
    int inv_dbg_kv_ = 0;
    int inv_dbg_kl_ = 0;
    int inv_dbg_kf_ = 0;
    int inv_dbg_kb_ = 0;
    int32_t inv_dbg_ks_ = 0;
    bool inv_dbg_kj_ok_ = false;
    uint64_t inv_dbg_kj_ = 0;
    // §4.34: инъекция FP-Item в запись экипировки. Slot несёт адрес
    // (cand+0x10), оригинал (null/stale — возврат) и last (бой с затиранием).
    // prev — подтверждение адреса двумя проходами (как кандидаты зонда);
    // adopted slot — смена слота (другой ствол) снимает инъекцию; live_item —
    // живой FP-Item с этого тика (0 в блэкауте — вливать нечего).
    Slot<uint64_t> kjw_item_;
    uint64_t kjw_prev_ = 0;
    int64_t kjw_slot_ = -1;
    uint64_t kjw_live_item_ = 0;
    // §4.38: заливка ditem (pw+0x120) нашим FP-Item + dbg-состояние
    // (0 пассивно/очищено, 1 наш (залит/уже), 2 чужой оставлен копии 4,
    // 3 запись не прошла).
    Slot<uint64_t> ditem_;
    int ditem_dbg_ = 0;
    // §4.39: kill-switches. kjw-adoption (§4.34) роняет игру: qj=2 overwrite
    // портит ItemData belt-Item при совпадении ammo==slot (сессия 32:
    // yh=6e5030c8d0 стабилен, краш на #10 при активном втором клоббере).
    // ditem-fill (§4.38) бесполезен: uzL=-1 при залитом ditem ×4 (#3/#6/#8/#9
    // сессии 32 — via читает НЕ ditem). Оба OFF; тесты включают.
    bool kjw_adopt_enabled_ = false;
    bool ditem_fill_enabled_ = false;
    bool inv_dbg_active_ = false;   // зонд идёт: validate считает промахи
    int inv_state_ = -1;     // инвентарный Item: -1 нет/не найден, 0 наш, 1 чужой

    bool rewrite_request_ = false; // одноразовый запрос «переписать обе копии»
    bool scan_request_ = false;    // одноразовый запрос перечитать список

    int  scan_age_ = 0;            // тиков до перечитывания списка
    viewmodel::SkinEntryView entries_[kMaxSkins]{};
    int  entry_count_ = 0;
    char weapon_name_[48] = {};
    int  live_skin_ = -1, live_level_ = -1;
    bool piece_read_ = false;   // live_skin_/live_level_ действительно прочитаны
    bool fp_stale_ = false, handle_live_ = false;
    int  local_state_ = -1;     // рабочая копия: -1 не читается, 0 наша, 1 чужая
    int  item_state_ = -1;      // Oxide.Item.skin: -1 не читается, 0 наш, 1 чужой
    char local_head_raw_[24] = {};  // hex головы рабочей копии, если она неканоническая

    // §4.22: наблюдение за полусобранным панцирем FP-вьюхи и его последствия.
    // Детектор живёт в нерезолв-ветке tick(): на панцире Item==null и наш
    // собственный резолв честно падает на стадии 9, поэтому смотреть надо
    // именно туда. shell_ticks_ считает ПОДРЯД идущие тики с сигнатурой
    // панциря (сбрасывается, как только сигнатура пропала); порог отделяет
    // отравление от здоровой первой закачки ассета, у которой сигнатура та же.
    int  shell_ticks_ = 0;
    // §4.24: детали текущего панциря (порог и лог) и счётчик беспанцирного
    // застревания: piece в руках, а FP-цепочка не резолвится (стадии 7–9).
    bool shell_peh_null_ = false;
    int  shell_back_ = -1;
    char shell_class_[24] = {};
    int  stuck_ticks_ = 0;
    // §4.24: зомби-резолв — _currentWeapon пуст, а резолв проходит через кэш
    // uzN (+0x58) живым объектом прежней сборки: чит видит «применено», вьюха
    // мертва, детектор панциря и дедуп блока молчат. Считаем только когда
    // piece «держится в руках» (Enabled=1): пустой _currentWeapon при пустых
    // руках — нормальный холстер.
    int  cache_ticks_ = 0;
    // §4.25: застревание дроу — Number в руках (SyncVar piece) расходится с
    // Number последней сборки, дошедшей до TAIL (FPManager.uzL +0x68, Number
    // +0x2; копия кладётся в 0x68EFD40 в начале каждой сборки). Расхождение
    // десятками секунд при Enabled=1 означает, что дроу не доходит до
    // CreateFPWeapon: резолв при этом ПРОХОДИТ (живой FPObject прежней
    // сборки, стадия 0, «применено») — форма, невидимая ни детектору панциря,
    // ни счётчику зомби, ни дедупу блока. Устройство v17: «в рандом момент
    // перестают применяться скины, хотя пишет применено; лечит перезайти».
    int  stall_ticks_ = 0;
    int  stall_hand_ = -1;       // SyncVar piece.Number (что в руках)
    int  stall_view_ = -1;       // uzL.Number (из чего собиралась вьюха)
    // §4.26: сырой срез FP-состояния — максимально подробное логирование
    // против молчаливых форм отравления (устройство v18: 25 с здоровой игры,
    // затем ~15 минут ПОЛНОЙ тишины при «применено» — ни один счётчик не
    // сработал). Сердцебиение раз в kDumpHeartbeatTicks плюс срез на каждое
    // изменение триггерных полей (указатель cur, uzL.Number, голова SyncVar)
    // с минимальным интервалом kDumpCooldownTicks. Сердцебиение заодно —
    // доказательство живости тика: нет строк среза в окне отравления = умер
    // чит/транспорт, а не игра.
    int  dump_seq_ = 0;          // монотонный номер среза (не сбрасывается)
    char dump_text_[640] = {};   // §4.42: 512->640 (kh/kv/kl/kf/kb/ks/kj chain-walk)
    int  dump_ticks_ = 0;        // тики с последнего среза (сердцебиение)
    int  dump_cooldown_ = 0;     // минимальный интервал между срезами
    uint32_t last_sv_head_ = 0xFFFFFFFFu;  // Enabled|Number SyncVar последнего среза
    int  last_uzl_number_ = 0x7FFFFFFF;
    uint64_t last_cur_ = ~0ull;
    int  last_inv_count_ = -1;   // §4.28: срез и на усыновление/сброс пояса
    bool reset_armed_ = true;    // сброс _currentWeapon — один на эпизод
    int  reset_count_ = 0;       // сколько раз чит сбрасывал панцирь (лог/меню)
    int32_t quarantine_id_ = -1;   // ItemData.m_Id ствола, на котором упала сборка
    int  quarantine_skin_ = -1;    // индекс скина, который её уронил

    int stage_ = 0;
    bool fail(int stage) { stage_ = stage; return false; }

    // PlayerWeapon висит на PlayerManager.weaponReference — ленивой обёртке,
    // как kccReference. Порядок тот же, что у resolve_player_weapon_component
    // в game.cpp: сама ссылка, её поля, затем широкий перебор по игроку.
    // Признак — обратная ссылка на игрока + имя класса.
    bool looks_like_weapon(uint64_t candidate, uint64_t player) {
        uint64_t back = 0;
        return candidate >= 0x10000 &&
               pointer(io, candidate + go_active::PLAYERWEAPON_PLAYER_BACKREF, back) &&
               back == player && io.named(candidate, "PlayerWeapon", kWeapons);
    }
    bool find_weapon(uint64_t player, uint64_t& out) {
        if (weapon_cache_ && looks_like_weapon(weapon_cache_, player)) {
            out = weapon_cache_;
            return true;
        }
        weapon_cache_ = 0;
        uint64_t reference = 0;
        if (!pointer(io, player + go_active::PLAYER_WEAPON_REFERENCE, reference)) return false;
        if (looks_like_weapon(reference, player)) { weapon_cache_ = out = reference; return true; }
        if (reference >= 0x10000) {
            for (uint64_t offset = 0x08; offset <= 0x60; offset += 8) {
                uint64_t candidate = 0;
                if (pointer(io, reference + offset, candidate) &&
                    looks_like_weapon(candidate, player)) {
                    weapon_cache_ = out = candidate;
                    return true;
                }
            }
        }
        for (uint64_t offset = 0x68; offset <= 0x350; offset += 8) {
            uint64_t candidate = 0;
            if (pointer(io, player + offset, candidate) &&
                looks_like_weapon(candidate, player)) {
                weapon_cache_ = out = candidate;
                return true;
            }
        }
        return false;
    }

    bool resolve(uint64_t player, Context& c) {
        using namespace go_active;
        uint64_t net = 0, weapon = 0, fp = 0, active = 0, object = 0, back = 0;
        uint64_t item = 0, data = 0, skins = 0;
        uint8_t local = 0;
        if (!player || !io.named(player, "PlayerManager", "Oxide")) return fail(1);
        if (!c.player.capture(io, player, true)) return fail(2);
        if (!pointer(io, player + NETWORK_BEHAVIOUR_IDENTITY, net) ||
            !io.named(net, "NetworkIdentity", "Mirror")) return fail(3);
        if (!io.read(net + NETWORK_IDENTITY_IS_LOCAL_PLAYER, local) || local != 1) return fail(4);
        if (!find_weapon(player, weapon) || !c.weapon.capture(io, weapon, true)) return fail(5);
        c.piece = weapon + PLAYERWEAPON_PIECE;
        c.piece_local = weapon + PLAYERWEAPON_PIECE_LOCAL;
        // FP-вьюха и ручка визуала — только диагностика, их отсутствие не
        // останавливает запись индекса.
        pointer(io, weapon + PLAYERWEAPON_FP_VIEW, c.fp_view);
        // Цепочка до ItemData: авторитетный слот +0x50, типизированный кэш
        // +0x58 — запасной (тот же выбор, что у контроллеров оружия).
        if (!pointer(io, player + PLAYER_FP_MANAGER, fp) ||
            !io.named(fp, "FPManager", "Oxide") || !c.fp.capture(io, fp, true)) return fail(6);
        const bool have_active = pointer(io, fp + FPMANAGER_CURRENT_OBJECT, active);
        object = (have_active && active) ? active : 0;
        if (!object && !pointer(io, fp + FPMANAGER_CURRENT_WEAPON, object)) return fail(7);
        if (!object) return fail(7);
        // §4.24: объект взят из кэша uzN (+0x58) при пустом _currentWeapon —
        // признак зомби-резолва (см. Context.via_cache).
        c.via_cache = !(have_active && active);
        if (!pointer(io, object + FPOBJECT_PLAYER_BACKREF, back) || back != player) return fail(8);
        if (!c.fp_object.capture(io, object, true)) return fail(8);
        if (!pointer(io, object + FPOBJECT_ITEM, item) || !item ||
            !c.item.capture(io, item, false)) return fail(9);
        if (!pointer(io, item + ITEM_DATA, data) ||
            !io.named(data, "ItemData", "Oxide") ||
            !c.item_data.capture(io, data, true)) return fail(10);
        if (!pointer(io, data + ITEMDATA_SKIN_DATA, skins) || !skins ||
            !c.skins.capture(io, skins, false)) return fail(11);
        int32_t length = 0;
        if (!io.read(skins + IL2CPP_ARRAY_LENGTH, length) || length <= 0)
            return fail(12);
        // §4.23: длинный массив — НЕ «нет скинов». У популярных стволов
        // skinData длиннее прежнего предела 24, и общая стадия 12 врала
        // меню и логу «у предмета нет скинов» на стволах, чьи скины видны
        // в самой игре. Предел поднят до kMaxSkins=64 (sanitize дальше и
        // не пустит индекс), а остаток получает честную стадию 27.
        if (length > kMaxSkins) return fail(27);
        stage_ = 0;
        return true;
    }

    // Перечитать список скинов предмета в руках. Дорого (строки по символу),
    // поэтому вызывается не каждый тик, а по возрасту и при смене контекста.
    void rescan() {
        using namespace go_active;
        entry_count_ = 0;
        for (auto& e : entries_) e = {};
        weapon_name_[0] = '\0';
        int32_t length = 0;
        if (!io.read(context_.skins.object + IL2CPP_ARRAY_LENGTH, length) ||
            length <= 0 || length > kMaxSkins) return;
        uint64_t name = 0;
        if (pointer(io, context_.item_data.object + ITEMDATA_SHORTNAME, name) && name)
            read_id(io, name, weapon_name_, sizeof(weapon_name_));
        if (!weapon_name_[0] &&
            pointer(io, context_.item_data.object + ITEMDATA_NAME, name) && name)
            read_id(io, name, weapon_name_, sizeof(weapon_name_));
        for (int32_t i = 0; i < length && entry_count_ < kMaxSkins; ++i) {
            uint64_t skin = 0;
            if (!pointer(io, context_.skins.object + IL2CPP_ARRAY_FIRST_ELEMENT +
                             (uint64_t)i * 8u, skin) || skin < 0x10000) continue;
            viewmodel::SkinEntryView& e = entries_[entry_count_];
            e.index = i;
            uint64_t skin_klass = 0;
            pointer(io, skin, skin_klass);
            e.kind = kind_of(io, base_, skin_klass);
            e.levels = levels_of(io, skin, e.kind);
            e.ready = asset_ready(io, skin, e.kind);
            uint64_t text = 0;
            if (pointer(io, skin + SKINDATA_ID, text) && text)
                read_id(io, text, e.id, sizeof(e.id));
            if (pointer(io, skin + SKINDATA_BASE_ID, text) && text)
                read_id(io, text, e.base_id, sizeof(e.base_id));
            // Пустой id — не повод выбрасывать строку: индекс и тип остаются
            // рабочими, а меню подпишет позицию номером.
            ++entry_count_;
        }
        scan_age_ = 0;
    }

    // Что сейчас в памяти игры: индекс, уровень, свежесть FP-вьюхи и ручки.
    // piece передаётся, когда голова уже прочитана (путь записи), и читается
    // здесь, когда нет (перечитывание списка при выключенном тумблере). Skin
    // показывается как есть, в том числе отрицательный «скина нет»: раньше
    // значение фильтровалось по знаку, и -1 в памяти было не отличить от
    // «не прочиталось».
    void read_live_state(const Piece* piece = nullptr) {
        using namespace go_active;
        Piece local;
        if (!piece)
            piece = (read_piece(io, context_.piece, local) == PieceRead::Ok) ? &local : nullptr;
        piece_read_ = piece != nullptr;
        live_skin_ = piece ? piece->skin : -1;
        live_level_ = piece ? piece->level : -1;
        fp_stale_ = handle_live_ = false;
        // Рабочая копия: «наша» — значит игра её ещё не перезаписала;
        // «чужая» — копия уже перезаписана игрой (RPC/спавн/холстер),
        // следующий тик восстановит наш индекс.
        local_state_ = -1;
        // Копия 3 (Oxide.Item.skin): «наш» — значит дропнутый ствол и сейв
        // несут индекс; «чужой» — игра перезаписала Item (крафт, пересинк),
        // следующий тик восстановит.
        item_state_ = -1;
        int32_t item_skin = 0;
        if (context_.item.object >= 0x10000 &&
            io.read(context_.item.object + go_active::ITEM_SKIN, item_skin))
            item_state_ = (piece && item_skin == (int32_t)piece->skin) ? 0 : 1;
        // Копия 4: кэш дроу разошёлся и держит чужое значение — Item.skin
        // ЦЕЛИКОМ «чужой»: экип-дроу строит SyncVar именно из этого
        // экземпляра, и вердикт меню не должен врать «наш», пока дроу читает
        // дефолт.
        if (item_state_ == 0 && draw_item_.object) {
            int32_t draw_skin = 0;
            if (!io.read(draw_item_.object + go_active::ITEM_SKIN, draw_skin) ||
                !piece || draw_skin != (int32_t)piece->skin)
                item_state_ = 1;
        }
        Piece work;
        // Проверка рабочей копии — щадящая, ровно как в tick(): иначе копия,
        // которую тик только что записал, показывала бы «не читается».
        if (context_.piece_local &&
            read_piece(io, context_.piece_local, work, nullptr, true) == PieceRead::Ok)
            local_state_ = (piece && work.skin == piece->skin &&
                            work.level == piece->level) ? 0 : 1;
        uint64_t model = 0;
        if (pointer(io, context_.weapon.object + PLAYERWEAPON_SKIN_HANDLE + SKINHANDLE_MODEL, model))
            handle_live_ = (model != 0);
        // Копия piece, ИЗ КОТОРОЙ FP-вьюха действительно построена. Вьюха
        // «не наша» и когда собрана из другого индекса, и когда её больше нет
        // (холстер: CreateFPWeapon выпустил вьюху, PW.playerFPWeaponView не
        // читается). Прежняя версия оставляла fp_stale=false на нечитаемой
        // вьюхе — и лог врал «вьюха собрана из нашего индекса» (устройство
        // v13/v14: 12/8 таких строк с мёртвой ручкой и ready=0).
        if (piece) {
            Piece view_piece;
            fp_stale_ = !(context_.fp_view >= 0x10000 &&
                          read_piece(io, context_.fp_view + FPWEAPONVIEW_PIECE,
                                     view_piece) == PieceRead::Ok &&
                          view_piece.skin == piece->skin);
        }
    }

    // §4.21: зеркало погашенной головы рабочей копии из SyncVar. Игра гасит
    // голову на холстере (Enabled=0, Number=-1), и hbr.pMZ подаёт её в
    // CreateFPWeapon, где Enabled==0 — ветка «UniTask.Delay -> Release()»
    // (dump11966, 0x68EFA04): вьюха выпускается, ассет не запрашивается.
    // pMd копирует piece целиком (оба qword) — делаем то же из SyncVar, в
    // котором уже лежит наш индекс. Звать только когда PW жив и голова
    // действительно погашена: живую голову зеркало не трогает. Сбой
    // writeback'а виден стадиями 24 (низ) / 25 (хвост).
    bool refill_local_head(int* stage) {
        uint64_t sync_lo = 0, cur_lo = 0;
        if (!io.read(context_.piece, sync_lo) || !io.read(context_.piece_local, cur_lo))
            return true;   // не читается — пропускаем, не повод ронять тик
        head_lo_local_.sample_value(context_.piece_local, cur_lo);
        if (!head_lo_local_.put(io, sync_lo)) { if (stage) *stage = 24; return false; }
        uint64_t sync_hi = 0, cur_hi = 0;
        if (io.read(context_.piece + 8, sync_hi) &&
            io.read(context_.piece_local + 8, cur_hi)) {
            head_hi_local_.sample_value(context_.piece_local + 8, cur_hi);
            if (!head_hi_local_.put(io, sync_hi)) { if (stage) *stage = 25; return false; }
        }
        return true;
    }

    // §4.28: кандидат — живой экземпляр ровно нашего ItemData, не транзиент.
    // Валидация — ТОЧНОЕ совпадение указателя ItemData: 64 бита мимо не
    // проходят, имя класса не сверяется (в бинаре оно может быть
    // обфусцировано, а ItemData уже доказывает наш предмет).
    bool validate_inv_item(uint64_t obj) {
        using namespace go_active;
        if (obj < 0x10000 || !context_.item_data.object) return false;
        if (obj == context_.item.object || obj == draw_item_.object) return false;
        uint64_t data = 0;
        if (!pointer(io, obj + ITEM_DATA, data)) return false;
        // §4.31: читаемый +0x20 с чужим ItemData — «почти-попадание» (пояс
        // держит Item, но другого ствола). Считаем только внутри прохода:
        // ведение и restore зовут validate вне зонда.
        if (data != context_.item_data.object) {
            if (inv_dbg_active_ && inv_dbg_miss_ < 999) ++inv_dbg_miss_;
            return false;
        }
        uint64_t klass = 0;
        if (!pointer(io, obj, klass) || klass < 0x10000) return false;
        int32_t skin = 0;
        uint8_t level = 0;
        return io.read(obj + ITEM_SKIN, skin) && io.read(obj + ITEM_SKIN_LEVEL, level);
    }

    // §4.32: тихая проверка «P+0x10 — наш Item» для kJw-формы: БЕЗ подсчёта
    // промахов (m — только про Item-форму, иначе PW-мусор из piece/shorts
    // утопит счётчик) и БЕЗ исключения FP-Item/кэша дроу: вопрос-то как раз
    // в том, совпадает ли kJw-Item с FP-цепью («обычно один объект»).
    bool kjw_item_is_ours(uint64_t it) {
        using namespace go_active;
        if (it < 0x10000 || !context_.item_data.object) return false;
        uint64_t data = 0;
        if (!pointer(io, it + ITEM_DATA, data)) return false;
        return data == context_.item_data.object;
    }

    // §4.32: kJw-форма в точке P (KERNEL.md §29a: via читает [kJw+0x10]).
    // Живая: P+0x10 -> наш Item (skin первого — в ks=). Пустая: P+0x10 null
    // и слот u32 <= 64 — кандидат в голодающую запись (гейт g4 LzY роняет
    // каждый дроу в пустую ветвь skin=-1). Только считает, не пишет.
    void check_kjw_shape(uint64_t p, bool l2 = false) {
        using namespace go_active;
        if (p < 0x10000) return;
        uint64_t it = 0;
        if (!pointer(io, p + KJW_ITEM, it)) return;
        if (it >= 0x10000) {
            // §4.40: yh-any ПЕРВЫМ — все валидные (Item, слот<=64), любой
            // слот, включая наши (k — подмножество: kjw_item_is_ours тоже
            // дефовый, иначе yh-any недостижим). X обязан валидироваться
            // Item нашего дефа (урок сессии 32) — массивы/Item'ы/мусор про
            // +0x10=ItemData отсекаются сами. Только чтение.
            uint32_t aslot = 0;
            const bool sok = io.read(p + KJW_SLOT, aslot);
            if (sok && aslot <= 64 && validate_inv_item(it)) {
                if (inv_dbg_yhan_ < 999) ++inv_dbg_yhan_;
                if (!inv_dbg_yha_ok_) {
                    inv_dbg_yha_ok_ = true;
                    inv_dbg_yha_ = p;
                    inv_dbg_yhas_ = aslot;
                }
            } else if (sok && aslot <= 64 && !kjw_item_is_ours(it)) {
                // §4.41: чужой X (не наш деф, не наш Item — validate и k
                // молчат), но klass ровно "Item": настоящий Item прошлого
                // ствола (класс один на все дефы), не net/fp/моторный шум
                // со случайным мелким +0x18. FP-Item сюда не падает
                // (kjw_item_is_ours — дефовый). Имя только для первой
                // (дорого — строка), счёт — всем.
                // §4.41: только L1 (прямые поля баз). L2 пересекает L1 (fp в
                // контейнерах — та же запись считалась бы дважды), а вложен
                // ный чужак — не kJw (экип-запись прямая по построению).
                // k-ветка ниже — общая (ранний return убил бы L2-k).
                if (!l2) {
                    char fcls[12] = {};
                    klass_name_read(io, it, fcls, sizeof(fcls));
                    if (std::strcmp(fcls, "Item") == 0) {
                        if (inv_dbg_yhn_ < 999) ++inv_dbg_yhn_;
                        if (!inv_dbg_yhx_ok_) {
                            inv_dbg_yhx_ok_ = true;
                            inv_dbg_yhx_ = p;
                            inv_dbg_yhxs_ = aslot;
                            std::snprintf(inv_dbg_yhxc_, sizeof(inv_dbg_yhxc_),
                                          "%s", fcls);
                        }
                    }
                }
            }
            if (inv_dbg_kjw_ < 999 && kjw_item_is_ours(it)) {
                int32_t skin = 0;
                if (!io.read(it + ITEM_SKIN, skin)) return;
                ++inv_dbg_kjw_;
                if (!inv_dbg_kjw_skin_ok_) {
                    inv_dbg_kjw_skin_ = skin;
                    inv_dbg_kjw_skin_ok_ = true;
                }
                return;
            }
            // §4.34: не-наш Item на СВОЁМ живом слоте — yh (stale-запись).
            uint32_t slot = 0;
            if (inv_live_slot_ >= 0 && io.read(p + KJW_SLOT, slot) &&
                slot == (uint32_t)inv_live_slot_) {
                if (inv_dbg_yhm_ < 999) ++inv_dbg_yhm_;
                if (!inv_dbg_yh_ok_) {
                    inv_dbg_yh_ok_ = true;
                    inv_dbg_yh_ = p;
                }
            }
            return;
        }
        if (it != 0) return;
        uint32_t slot = 0;
        if (!io.read(p + KJW_SLOT, slot) || slot > 64) return;
        if (inv_dbg_kjw_null_ < 999) ++inv_dbg_kjw_null_;
        // §4.33: пустой слот живого слота дроу — кандидат на инъекцию Item.
        // §4.37: массивы (bounds=null, length=N) совпадают с kJw-формой —
        // настоящий kJw прячется среди z. Кандидата проверяем по классу
        // (только совпавшие со слотом — единицы за проход, дёшево): имя на
        // "]" = массив, отклонить в rej= (z-ряд не трогаем — непрерывность).
        if (inv_live_slot_ >= 0 && slot == (uint32_t)inv_live_slot_) {
            char cls[24] = {};
            klass_name_read(io, p, cls, sizeof(cls));
            const size_t nl = std::strlen(cls);
            if (nl > 0 && cls[nl - 1] == ']') {
                if (inv_dbg_rej_ < 999) ++inv_dbg_rej_;
            } else {
                if (inv_dbg_zhm_ < 999) ++inv_dbg_zhm_;
                if (!inv_dbg_zh_ok_) {
                    inv_dbg_zh_ok_ = true;
                    inv_dbg_zh_ = slot;
                    inv_dbg_za_ = p;
                }
            }
        }
    }

    // §4.28: один проход зонда. Три якоря: прямой массив пояса
    // player+0x1B8 («inventory/belt weapon objects», game.cpp route 4 имён
    // оружия — элементы читаются как Item) и PlayerInventoryData/Client
    // (оффсеты те же, что у ESP model info). Data/Client сканируются на ДВА
    // уровня: поля 0x10..0xF8 (прямое Item-значение либо массив 1..32), затем
    // поля подобъектов 0x10..0x80 (контейнеры пояса/рюкзака — массивы слотов
    // живут в них, одноуровневый зонд v1 за 77 с ничего не нашёл).
    // Запоминаются АДРЕСА ССЫЛОК (поле/слот), а не экземпляры: ресинк меняет
    // экземпляр, ссылка перечитывается каждый тик. Найденное — в
    // inv_probe_cands_; усыновление (дважды подтверждённым) — в maintain().
    // §4.42: chain-walk G3/G4 (HcG-точно + LWM + пик живого слота). Хопы
    // повторяют cbz/tbz via/HcG/LzY/LWM один в один (включая OOB-семантику
    // HcG: throw трактуем как падение хопа — дроу всё равно не печёт).
    // kj-пик независим от валидности HcG-слота (дивергенция ks/slot видна).
    void walk_kjw_chain(uint64_t inv) {
        using namespace go_active;
        uint64_t server = 0;
        if (!pointer(io, inv + INV_PLAYER_INVENTORY_SERVER, server) ||
            server < 0x10000) { inv_dbg_kh_ = 0; return; }
        uint64_t flagobj = 0;
        if (!pointer(io, server + INVSERVER_FLAGOBJ, flagobj) ||
            flagobj < 0x10000) { inv_dbg_kh_ = 1; return; }
        uint8_t b49 = 0;
        if (!io.read(flagobj + INVSERVER_FLAGBIT, b49)) { inv_dbg_kh_ = 1; return; }
        inv_dbg_kb_ = b49 ? 1 : 0;
        if (!b49) { inv_dbg_kh_ = 2; return; }
        uint64_t hinv = 0;
        if (!pointer(io, server + INVSERVER_INV, hinv) ||
            hinv < 0x10000) { inv_dbg_kh_ = 3; return; }
        uint64_t arr = 0;
        if (!pointer(io, hinv + KJW_ITEM, arr) || arr < 0x10000) {
            inv_dbg_kh_ = 4;
            return;
        }
        int32_t hslot = 0;
        uint32_t alen = 0;
        if (!io.read(server + INVSERVER_SLOT, hslot) ||
            !io.read(arr + IL2CPP_ARRAY_LENGTH, alen)) { inv_dbg_kh_ = 4; return; }
        inv_dbg_ks_ = hslot;
        if (inv_live_slot_ >= 0 && (uint32_t)inv_live_slot_ < alen) {
            uint64_t lkj = 0;
            if (pointer(io, arr + IL2CPP_ARRAY_FIRST_ELEMENT +
                            (uint64_t)inv_live_slot_ * 8, lkj) &&
                lkj >= 0x10000) {
                inv_dbg_kj_ok_ = true;
                inv_dbg_kj_ = lkj;
            }
        }
        if (hslot < 0 || (uint32_t)hslot >= alen) { inv_dbg_kh_ = 5; return; }
        uint64_t kjw = 0;
        if (!pointer(io, arr + IL2CPP_ARRAY_FIRST_ELEMENT +
                         (uint64_t)hslot * 8, kjw) ||
            kjw < 0x10000) { inv_dbg_kh_ = 6; return; }
        uint64_t kitem = 0;
        if (!pointer(io, kjw + KJW_ITEM, kitem) || kitem < 0x10000) {
            inv_dbg_kh_ = 7;
            return;
        }
        uint8_t krf = 0;
        if (!io.read(kitem + ITEM_KRF, krf)) { inv_dbg_kh_ = 7; return; }
        inv_dbg_kf_ = krf ? 1 : 0;
        if (krf) { inv_dbg_kh_ = 8; return; }
        uint64_t kdata = 0, kcfg = 0, kjx = 0;
        float dur = 0, krm = 0;
        uint8_t unb = 0;
        if (!pointer(io, kitem + ITEM_DATA, kdata) || kdata < 0x10000 ||
            !pointer(io, kdata + ITEMDATA_CFG, kcfg) || kcfg < 0x10000 ||
            !io.read(kcfg + CFG_DURMAX, dur) ||
            !io.read(kcfg + CFG_UNBRK, unb) ||
            !pointer(io, kitem + ITEM_KJX, kjx) || kjx < 0x10000 ||
            !io.read(kjx + KJX_KRM, krm)) { inv_dbg_kh_ = 0xA; return; }
        const int lwm = (dur > 0 && !unb && krm <= 0) ? 1 : 0;
        inv_dbg_kl_ = lwm;
        if (lwm) { inv_dbg_kh_ = 9; return; }
        inv_dbg_kh_ = 0xF;
        inv_dbg_kv_ = 1;
    }

    void probe_inventory() {
        using namespace go_active;
        inv_probe_n_ = 0;
        inv_dbg_data_ = inv_dbg_client_ = inv_dbg_warr_ = false;
        inv_dbg_wlen_ = -1;
        inv_dbg_arrays_ = inv_dbg_l2push_ = inv_dbg_miss_ = 0;
        inv_dbg_pw_ = false;
        inv_dbg_kjw_ = inv_dbg_kjw_null_ = 0;
        inv_dbg_kjw_skin_ok_ = false;
        inv_live_slot_ = -1;
        inv_dbg_zh_ok_ = false;
        inv_dbg_zhm_ = 0;
        inv_dbg_za_ = 0;
        inv_dbg_rej_ = 0;
        inv_dbg_yh_ok_ = false;
        inv_dbg_yh_ = 0;
        inv_dbg_yhm_ = 0;
        inv_dbg_yha_ok_ = false;
        inv_dbg_yha_ = 0;
        inv_dbg_yhas_ = 0;
        inv_dbg_yhan_ = 0;
        inv_dbg_yhx_ok_ = false;
        inv_dbg_yhx_ = 0;
        inv_dbg_yhxs_ = 0;
        inv_dbg_yhn_ = 0;
        inv_dbg_yhxc_[0] = '\0';
        inv_dbg_kh_ = 0;
        inv_dbg_kv_ = 0;
        inv_dbg_kl_ = 0;
        inv_dbg_kf_ = 0;
        inv_dbg_kb_ = 0;
        inv_dbg_ks_ = 0;
        inv_dbg_kj_ok_ = false;
        inv_dbg_kj_ = 0;
        const int cap = (int)(sizeof(inv_probe_cands_) / sizeof(inv_probe_cands_[0]));
        if (!context_.player.object) return;
        inv_dbg_active_ = true;
        uint64_t warr = 0;
        inv_dbg_warr_ = pointer(io, context_.player.object + PLAYER_WEAPONS_ARRAY, warr) &&
                        warr >= 0x10000;
        if (inv_dbg_warr_) {
            int32_t wl = -1;
            if (io.read(warr + IL2CPP_ARRAY_LENGTH, wl)) inv_dbg_wlen_ = wl;
            scan_array_for_inv(warr);
        }
        // §4.32: PlayerWeapon — свой L1, только kJw-форма и только тихо:
        // validate здесь утопил бы m мусором из piece/shorts/хэндлов.
        const uint64_t pw = context_.weapon.object;
        inv_dbg_pw_ = pw >= 0x10000;
        if (inv_dbg_pw_) {
            uint32_t ls = 0;
            if (io.read(pw + PLAYERWEAPON_DRAW_SLOT, ls))  // §4.33: живой слот
                inv_live_slot_ = (int64_t)ls;              // для zh-матчинга
            for (uint64_t off = 0x10; off <= 0x1F8; off += 8) {
                uint64_t p = 0;
                if (!pointer(io, pw + off, p) || p < 0x10000) continue;
                if (p == context_.item.object || p == context_.item_data.object ||
                    p == draw_item_.object)
                    continue;
                check_kjw_shape(p);
            }
        }
        uint64_t inv = 0;
        if (!pointer(io, context_.player.object + PLAYER_INVENTORY, inv) || inv < 0x10000) {
            inv_dbg_active_ = false;
            return;
        }
        uint64_t anchors[2] = {};
        pointer(io, inv + INV_PLAYER_INVENTORY_DATA, anchors[0]);
        pointer(io, inv + INV_PLAYER_INVENTORY_CLIENT, anchors[1]);
        inv_dbg_data_ = anchors[0] >= 0x10000;
        inv_dbg_client_ = anchors[1] >= 0x10000;
        walk_kjw_chain(inv);   // §4.42: G3/G4 chain-walk (живой слот уже снят)
        // §4.40: kJw нет в PW/Data/Client-L1 (yh молчит везде) — сканируем
        // поля FPManager (+0x20..; +0x10/+0x18 известны — cachedPtr/0),
        // PlayerInventory (свой L1 — раньше читали только якоря!), игрока
        // и дальние поля PW. Только check_kjw_shape (без validate — как
        // PW-L1); validate решает сам (X обязан быть Item нашего дефа).
        {
            const uint64_t bases[4] = {context_.fp.object, inv,
                                       context_.player.object, pw};
            const uint64_t from[4] = {0x20, 0x10, 0x10, 0x200};
            const uint64_t till[4] = {0xF8, 0xF8, 0x1F8, 0x2F8};
            for (int b = 0; b < 4; ++b) {
                const uint64_t base = bases[b];
                if (base < 0x10000) continue;
                for (uint64_t off = from[b]; off <= till[b]; off += 8) {
                    uint64_t p = 0;
                    if (!pointer(io, base + off, p) || p < 0x10000) continue;
                    if (p == context_.item.object ||
                        p == context_.item_data.object ||
                        p == draw_item_.object)
                        continue;
                    check_kjw_shape(p);
                }
            }
        }
        // §4.31: бюджет L2 16->32 (худший проход ~1.6k чтений — в пределах
        // тика), дедуп повторов и пропуск уже известных адресов (якоря,
        // FP-Item/ItemData/кэш дроу — их сканировать нечего).
        // §4.33: 32->48 — сессия 47 давит o33 (переполнение на единицу,
        // последний контейнер не сканируется — k=0 может врать).
        // §4.40: 48->64 — Data/Client-L1 расширен до 0x1F8 (рюкзак глубже),
        // запас под новые контейнеры.
        uint64_t l2[64];
        int l2_n = 0;
        const int l2_cap = (int)(sizeof(l2) / sizeof(l2[0]));
        for (int a = 0; a < 2; ++a) {
            const uint64_t base = anchors[a];
            if (base < 0x10000) continue;
            for (uint64_t off = 0x10; off <= 0x1F8; off += 8) {
                uint64_t p = 0;
                if (!pointer(io, base + off, p) || p < 0x10000) continue;
                check_kjw_shape(p);   // §4.32: kJw-форма раньше Item-формы
                if (validate_inv_item(p)) {
                    // Прямое Item — уже классификация: массив и контейнер
                    // не ищем (указатель на массив Item'ом не валидируется).
                    if (inv_probe_n_ < cap) inv_probe_cands_[inv_probe_n_++] = base + off;
                    continue;
                }
                if (scan_array_for_inv(p)) continue;
                // Не Item и не массив — возможно контейнер: второй уровень.
                if (p == anchors[0] || p == anchors[1] || p == warr ||
                    p == context_.item.object || p == context_.item_data.object ||
                    p == draw_item_.object)
                    continue;
                bool dupe = false;
                for (int i = 0; i < l2_n; ++i)
                    if (l2[i] == p) { dupe = true; break; }
                if (dupe) continue;
                ++inv_dbg_l2push_;
                if (l2_n < l2_cap) l2[l2_n++] = p;
            }
        }
        for (int i = 0; i < l2_n; ++i) {
            for (uint64_t off = 0x10; off <= 0x80; off += 8) {
                uint64_t p = 0;
                if (!pointer(io, l2[i] + off, p) || p < 0x10000) continue;
                check_kjw_shape(p, true);   // §4.32 (§4.41: L2 — без yhn)
                if (validate_inv_item(p)) {
                    if (inv_probe_n_ < cap) inv_probe_cands_[inv_probe_n_++] = l2[i] + off;
                    continue;
                }
                if (inv_probe_n_ >= cap) { inv_dbg_active_ = false; return; }
                scan_array_for_inv(p);
            }
        }
        // §4.34: усыновление записи своего слота — дважды подтверждённой
        // (транзиент не пишется никогда). zh (null-fill) предпочтительнее yh
        // (stale-overwrite). Только если живой kJw нет (k==0 — чинить нечего)
        // и FP-Item жив (вливать нечего — нечего). Известные объекты
        // исключены: FP-Item с совпадением +0x18 — не запись экипировки.
        // §4.36: za/yh доказано декоем (сессия 39 #6->#7: дроу под пином qj=1
        // построил -1 — настоящий kJw (x20) elsewhere). Инжект оставлен как
        // живой зонд (стабильность zh + прогрев пути записи для настоящего).
        {
            uint64_t cand = 0;
            if (inv_dbg_zh_ok_)
                cand = inv_dbg_za_;
            else if (inv_dbg_yh_ok_)
                cand = inv_dbg_yh_;
            if (kjw_adopt_enabled_ && cand >= 0x10000 && inv_dbg_kjw_ == 0 &&
                kjw_live_item_ >= 0x10000 && cand != kjw_live_item_ &&
                cand != context_.item_data.object &&
                cand != draw_item_.object && cand != context_.weapon.object &&
                cand != context_.player.object && cand != warr && cand != inv &&
                cand != anchors[0] && cand != anchors[1]) {
                if (cand == kjw_prev_ && !kjw_item_.owned) {
                    uint64_t orig = 0;
                    if (pointer(io, cand + KJW_ITEM, orig)) {
                        kjw_item_.sample_value(cand + KJW_ITEM, orig);
                        if (kjw_item_.put(io, kjw_live_item_))
                            kjw_slot_ = inv_live_slot_;
                        else
                            kjw_item_ = {};
                    }
                }
                kjw_prev_ = cand;
            } else if (!kjw_item_.owned) {
                kjw_prev_ = 0;
            }
        }
        inv_dbg_active_ = false;
    }

    // §4.28: элементы массива — кандидаты, если это наш Item. Возвращает true,
    // когда по адресу похож на массив (длина 1..32) — поле уже классифицировано.
    bool scan_array_for_inv(uint64_t arr) {
        using namespace go_active;
        const int cap = (int)(sizeof(inv_probe_cands_) / sizeof(inv_probe_cands_[0]));
        int32_t len = 0;
        if (!io.read(arr + IL2CPP_ARRAY_LENGTH, len) || len <= 0 || len > 32)
            return false;
        ++inv_dbg_arrays_;
        for (int32_t i = 0; i < len; ++i) {
            const uint64_t slot =
                arr + IL2CPP_ARRAY_FIRST_ELEMENT + (uint64_t)i * 8u;
            uint64_t e = 0;
            if (!pointer(io, slot, e)) continue;
            if (inv_probe_n_ >= cap) return true;
            if (validate_inv_item(e)) inv_probe_cands_[inv_probe_n_++] = slot;
        }
        return true;
    }

    // §4.34: ведение kJw-инъекции. FP мёртв (live_item == 0, блэкаут/холстер) —
    // инъекция указывает в освобождённое: снять НЕМЕДЛЕННО (висячий kJw.item
    // уронил бы ctor сборки на ближайшем дроу). Слот сменился — запись чужая:
    // снять, прозондировать заново. Иначе бой: держать указатель на ЖИВОЙ
    // FP-Item (ротация per-draw и затирание игрой лечатся одной записью;
    // original при этом НЕ трогаем — restore вернёт честный null/stale).
    // §4.35: ведение kJw-инъекции — PINNING. Ошибка §4.34: снятие на холстере
    // оставляло каждый дроу без покрытия (инжект жил только в hold-окне между
    // усыновлением и холстером — сессия 34: qj=1 в hold, дроу строил из null).
    // Теперь вживлённый Item ДЕРЖИМ через холстер: указатель из живой
    // структуры игры для консервативного GC — корень, Item не освобождается
    // (а освобождённый-но-нетронутый читается безопасно и несёт наш скин).
    // Свежий FP-Item НЕ преследуем: в нём серверный -1, а в пине — наш индекс,
    // запечённый копией 3 при hold. Снимаем только инвалид (ItemData чужой /
    // переиспользование кучи / смена ствола-слота / нечитаемость), а ресинк,
    // ротировавший сюда свежий -1, перекрашиваем прямо в пине (сирота:
    // restore бессмысленен, Item мёртв для игры).
    void maintain_kjw(int16_t want_skin, uint8_t want_level) {
        using namespace go_active;
        if (!kjw_item_.owned) return;   // усыновление — в зонде (§4.34)
        if (inv_live_slot_ >= 0 && inv_live_slot_ != kjw_slot_) {
            if (!kjw_item_.restore(io)) kjw_item_ = {};
            kjw_prev_ = 0;
            kjw_slot_ = -1;
            return;
        }
        uint64_t cur = 0, data = 0;
        const bool ok = kjw_item_.read(io, cur) && cur >= 0x10000 &&
                        context_.item_data.object &&
                        pointer(io, cur + ITEM_DATA, data) &&
                        data == context_.item_data.object;
        if (!ok) {
            // Пин умер (фрибут/переиспользование/ресинк чужим) или мир
            // умирает: restore пишет оригинал, только если там наш last, —
            // мусор не трогаем; усыновление начнётся заново.
            if (!kjw_item_.restore(io)) kjw_item_ = {};
            if (!kjw_item_.owned) {
                kjw_item_ = {};
                kjw_prev_ = 0;
                kjw_slot_ = -1;
            }
            return;
        }
        // Пин жив — перекрасить при расхождении (ресинк-ротация свежим -1).
        int32_t skin = 0;
        if (io.read(cur + ITEM_SKIN, skin) && skin != (int32_t)want_skin &&
            io.write_heap(cur + ITEM_SKIN, (int32_t)want_skin)) {
            int32_t back = 0;
            io.read(cur + ITEM_SKIN, back);
        }
        uint8_t level = 0;
        if (io.read(cur + ITEM_SKIN_LEVEL, level) && level != want_level &&
            io.write_heap(cur + ITEM_SKIN_LEVEL, want_level)) {
            uint8_t back = 0;
            io.read(cur + ITEM_SKIN_LEVEL, back);
        }
    }

    // §4.38: заливка кэша дроу pw+0x120 (ditem, Item-кэш via(kJw)).
    // Экип-дроу via(kJw) строит SyncVar ИЗ DITEM: на населённом ditem пуст
    // (null — серверный fill-source голодает), дроу печёт -1, а наши
    // тиковые перекраски (SV/вьюха/FP-Item) ложатся ПОСЛЕ запекания.
    // Пустой/битый кэш заливаем нашим FP-Item (перекрашен копией 3);
    // валидный чужой (ресинк-ротация серверным) НЕ трогаем — его красит
    // копия 4, а состояние сервера целее нашего (аттачменты/прочее из дроу
    // берутся оттуда же). FP мёртв — возврат null (висячий ditem = краш).
    // Оригинал — as-found, возврат на disable/смену контекста.
    void maintain_ditem(uint64_t live_item) {
        using namespace go_active;
        const uint64_t pw = context_.weapon.object;
        if (!pw) { ditem_dbg_ = 0; return; }
        const uint64_t addr = pw + PLAYERWEAPON_DRAW_ITEM;
        uint64_t cur = 0;
        if (!pointer(io, addr, cur)) { ditem_dbg_ = 0; return; }
        if (!ditem_fill_enabled_) {   // §4.39: теория мертва (сессия 32) — молчим
            if (ditem_.owned) {
                if (!ditem_.restore(io)) ditem_ = {};
                if (!ditem_.owned) ditem_ = {};
            }
            ditem_dbg_ = 0;
            return;
        }
        if (live_item < 0x10000) {
            if (ditem_.owned) {
                if (!ditem_.restore(io)) ditem_ = {};
                if (!ditem_.owned) ditem_ = {};
            }
            ditem_dbg_ = 0;
            return;
        }
        if (cur == 0 || cur == live_item) {
            ditem_.sample_value(addr, cur);
            ditem_dbg_ = ditem_.put(io, live_item) ? 1 : 3;
            return;
        }
        uint64_t ddata = 0;
        Identity probe{};
        if (pointer(io, cur + ITEM_DATA, ddata) &&
            ddata == context_.item_data.object && probe.capture(io, cur, false) &&
            probe.life(io) == Life::Alive) {
            // Валидный чужой — владение снимаем БЕЗ записи (адрес не наш).
            ditem_ = {};
            ditem_dbg_ = 2;
            return;
        }
        ditem_.sample_value(addr, cur);
        ditem_dbg_ = ditem_.put(io, live_item) ? 1 : 3;
    }

    // §4.28: ведение инвентарных целей + зонд, когда вести нечего. Зовётся и
    // из основной ветки, и из блэкаута (§4.27): пояс стабилен и от FP-цепи не
    // зависит. Best-effort и молча: неуспех виден строкой лога по inv_state_,
    // а не стадией. §4.34: live_item — живой FP-Item этого тика (0 в
    // блэкауте): источник указателя для kJw-инъекции.
    void maintain_inventory(int16_t want_skin, uint8_t want_level,
                            uint64_t live_item) {
        using namespace go_active;
        kjw_live_item_ = live_item;
        bool any_foreign = false;
        for (int i = 0; i < inv_count_;) {
            InvTarget& t = inv_[i];
            uint64_t obj = 0;
            Identity id;
            if (!pointer(io, t.ref, obj) || !validate_inv_item(obj) ||
                !id.capture(io, obj, false)) {
                // Ссылка умерла (фрибут/ресинк унёс контейнер) — снять молча:
                // слоты чистятся без записи, чужой адрес не трогаем.
                for (int j = i; j + 1 < inv_count_; ++j) inv_[j] = inv_[j + 1];
                inv_[--inv_count_] = {};
                continue;
            }
            t.id = id;
            int32_t cur_skin = 0;
            uint8_t cur_level = 0;
            if (io.read(obj + ITEM_SKIN, cur_skin) &&
                io.read(obj + ITEM_SKIN_LEVEL, cur_level)) {
                t.skin.sample_value(obj + ITEM_SKIN, cur_skin);
                t.level.sample_value(obj + ITEM_SKIN_LEVEL, cur_level);
                t.skin.put(io, (int32_t)want_skin);
                t.level.put(io, want_level);
                if (cur_skin != (int32_t)want_skin) any_foreign = true;
            }
            ++i;
        }
        // Зонд — каждый 5-й тик, пока вести нечего (в ритме рескана): проход
        // тяжёлый (~тысяча чтений на два уровня), а пояс никуда не убегает —
        // усыновление за ~1 с. Подтверждение — два прохода подряд.
        ++inv_probe_tick_;
        if (inv_count_ == 0 && inv_probe_tick_ % 5 == 0) {
            uint64_t prev[sizeof(inv_probe_cands_) / sizeof(inv_probe_cands_[0])];
            const int prev_n = inv_probe_n_;
            for (int i = 0; i < prev_n; ++i) prev[i] = inv_probe_cands_[i];
            probe_inventory();
            for (int i = 0; i < inv_probe_n_ && inv_count_ < kMaxInv; ++i) {
                bool confirmed = false;
                for (int j = 0; j < prev_n; ++j)
                    if (prev[j] == inv_probe_cands_[i]) { confirmed = true; break; }
                if (!confirmed) continue;
                uint64_t obj = 0;
                Identity id;
                if (!pointer(io, inv_probe_cands_[i], obj) ||
                    !validate_inv_item(obj) || !id.capture(io, obj, false))
                    continue;
                InvTarget& t = inv_[inv_count_++];
                t.ref = inv_probe_cands_[i];
                t.id = id;
                int32_t cur_skin = 0;
                uint8_t cur_level = 0;
                if (io.read(obj + ITEM_SKIN, cur_skin) &&
                    io.read(obj + ITEM_SKIN_LEVEL, cur_level)) {
                    t.skin.sample_value(obj + ITEM_SKIN, cur_skin);
                    t.level.sample_value(obj + ITEM_SKIN_LEVEL, cur_level);
                    t.skin.put(io, (int32_t)want_skin);
                    t.level.put(io, want_level);
                    if (cur_skin != (int32_t)want_skin) any_foreign = true;
                }
            }
        }
        inv_state_ = inv_count_ == 0 ? -1 : (any_foreign ? 1 : 0);
        maintain_kjw(want_skin, want_level);   // §4.35: пиннинг kJw — каждый тик
        maintain_ditem(live_item);            // §4.38: заливка ditem — каждый тик
    }

    // §4.29: ведение piece вьюхи. Зовётся ПОСЛЕ read_live_state: детектор
    // «вьюха СТАРАЯ» обязан видеть собранное игрой, а не нашу краску, —
    // иначе он никогда не сработает и тест 13 врёт. Best-effort и молча:
    // неуспех виден строкой лога по view_state_, а не стадией.
    void maintain_view(int16_t want_skin, uint8_t want_level, int16_t sv_number,
                       bool force) {
        using namespace go_active;
        view_state_ = -1;
        if (sv_number < 0) return;
        const uint64_t pw = context_.weapon.object;
        if (!pw) return;
        uint64_t view = 0;
        if (!pointer(io, pw + PLAYERWEAPON_FP_VIEW, view) || view < 0x10000) return;
        int16_t vnum = 0;
        if (!io.read(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_NUMBER, vnum)) return;
        if (vnum != sv_number) return;   // вьюха показывает не наш ствол
        int16_t vskin = 0;
        uint8_t vlevel = 0;
        if (!io.read(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN, vskin)) return;
        io.read(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN_LEVEL, vlevel);
        skin_view_.sample_value(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN, vskin);
        level_view_.sample_value(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN_LEVEL, vlevel);
        if (force) { skin_view_.invalidate(); level_view_.invalidate(); }
        skin_view_.put(io, want_skin);
        level_view_.put(io, want_level);
        view_state_ = (vskin != want_skin) ? 1 : 0;
    }

    // §4.30: сброс Loaded — строго ПОСЛЕ краски (в поле уже наш индекс).
    // Видимость — срез L= (значения) + существующая строка «визуал скина
    // загружен» (флип хэндла). Отдельной строки нет: эксперимент.
    void maintain_loaded(int16_t sv_number) {
        using namespace go_active;
        const uint64_t pw = context_.weapon.object;
        if (!pw) return;
        uint64_t model = 0;
        if (pointer(io, pw + PLAYERWEAPON_SKIN_HANDLE + SKINHANDLE_MODEL, model) &&
            model != 0)
            return;   // хэндл живой — визуал резолвлен, флаги не трогаем
        uint8_t l = 0;
        if (context_.piece &&
            io.read(context_.piece + WEAPONPIECE_LOADED, l) && l == 1) {
            loaded_sv_.sample_value(context_.piece + WEAPONPIECE_LOADED, l);
            loaded_sv_.put(io, (uint8_t)0);
        }
        if (context_.piece_local &&
            io.read(context_.piece_local + WEAPONPIECE_LOADED, l) && l == 1) {
            loaded_wk_.sample_value(context_.piece_local + WEAPONPIECE_LOADED, l);
            loaded_wk_.put(io, (uint8_t)0);
        }
        if (sv_number < 0) return;
        uint64_t view = 0;
        int16_t vnum = 0;
        if (!pointer(io, pw + PLAYERWEAPON_FP_VIEW, view) || view < 0x10000) return;
        if (!io.read(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_NUMBER, vnum)) return;
        if (vnum != sv_number) return;
        if (io.read(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_LOADED, l) && l == 1) {
            loaded_view_.sample_value(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_LOADED, l);
            loaded_view_.put(io, (uint8_t)0);
        }
    }

    bool restore_all() {
        // Все три точки равноправны: возвращаем оригиналы в каждую — но ТОЛЬКО
        // пока контекст жив. После смерти игрока/мира адреса слотов указывают
        // на освобождённую кучу: запись туда — порча памяти (та же причина, по
        // которой смена сессии забывает адреса без записи). Мёртвый контекст
        // лишь снимает владение. Устройство (skin.log v12): мир перезаходил
        // без смены процесса, и прежний безусловный возврат стирал индекс
        // ровно перед спавном, которому SyncVar нужен для пересборки вьюхи.
        bool ok = true;
        if (context_.weapon.object && context_.weapon.life(io) == Life::Alive) {
            // Зеркало головы возвращается ПЕРЕД байтовыми слотами: пока голова
            // цела (current == last), restore кладёт обратно погашенные байты
            // игры; байтовые слоты затем видят «чужое» значение и не пишут.
            // Обратный порядок оставил бы Enabled=1 в «восстановленной» копии.
            ok = head_hi_local_.restore(io) && ok;
            ok = head_lo_local_.restore(io) && ok;
            ok = level_local_.restore(io) && ok;
            ok = skin_local_.restore(io) && ok;
            ok = level_.restore(io) && ok;
            ok = skin_.restore(io) && ok;
            // §4.29: вьюха умирает вместе с оружием — тот же класс владения,
            // что piece-слоты. Смена ствола без пересборки вьюхи: current —
            // наша краска прошлого ствола, last совпал — вернём оригинал.
            ok = level_view_.restore(io) && ok;
            ok = skin_view_.restore(io) && ok;
            // §4.30: флаги Loaded — туда же: оригинал (запечённая игрой 1)
            // возвращается, только если там всё ещё наш сброс.
            ok = loaded_view_.restore(io) && ok;
            ok = loaded_wk_.restore(io) && ok;
            ok = loaded_sv_.restore(io) && ok;
        } else {
            skin_ = {}; level_ = {}; skin_local_ = {}; level_local_ = {};
            head_lo_local_ = {}; head_hi_local_ = {};
            skin_view_ = {}; level_view_ = {};
            loaded_sv_ = {}; loaded_wk_ = {}; loaded_view_ = {};
        }
        view_state_ = -1;
        if (context_.item.object && context_.item.life(io) == Life::Alive) {
            ok = level_item_.restore(io) && ok;
            ok = skin_item_.restore(io) && ok;
        } else {
            skin_item_ = {}; level_item_ = {};
        }
        if (draw_item_.object && draw_item_.life(io) == Life::Alive) {
            ok = level_item2_.restore(io) && ok;
            ok = skin_item2_.restore(io) && ok;
        } else {
            skin_item2_ = {}; level_item2_ = {};
        }
        draw_item_ = {};
        // §4.28: инвентарные цели — живым возвращается оригинал, мёртвые
        // ссылки чистятся без записи; в любом случае цели забываются (новый
        // контекст прозондируется заново).
        for (int i = 0; i < inv_count_; ++i) {
            InvTarget& t = inv_[i];
            uint64_t obj = 0;
            if (pointer(io, t.ref, obj) && validate_inv_item(obj)) {
                ok = t.level.restore(io) && ok;
                ok = t.skin.restore(io) && ok;
            } else {
                t.skin = {}; t.level = {};
            }
        }
        for (int i = 0; i < kMaxInv; ++i) inv_[i] = {};
        inv_count_ = 0;
        inv_probe_n_ = 0;
        inv_probe_tick_ = 0;
        inv_state_ = -1;
        // §4.34: kJw-инъекция — оригинал (null/stale) возвращается слотом
        // (только если там всё ещё наша запись), остатки владения чистятся.
        ok = kjw_item_.restore(io) && ok;
        kjw_item_ = {};
        kjw_prev_ = 0;
        kjw_slot_ = -1;
        // §4.38: ditem — оригинал (null/foreign-as-found) слотом.
        ok = ditem_.restore(io) && ok;
        ditem_ = {};
        ditem_dbg_ = 0;
        return ok;
    }

    // §4.24: чем оказался панцирь. Детали читаются только когда сигнатура
    // совпала и нужны порогу и логу; «не прочиталось» остаётся «не знаем».
    struct ShellLook {
        bool peh_null = false;   // PlayerEventHandler (+0xC8) прочитался нулём
        int  back = -1;          // ссылка на игрока: 1 наша, 0 null, 2 чужая, -1 не читается
        char klass[24] = {};     // имя класса полусобранного объекта
    };

    // §4.22/§4.24: сигнатура полусобранного панциря — _currentWeapon жив,
    // лежит в именном FPManager, Item==null и uhq==0. Здоровый инвариант
    // (dump11966): uhq==1 <=> Item!=null; FDN (слот 10) гасит оба вместе,
    // On_Draw (слот 17) — ставит оба вместе. Нечитаемое поле — не панцирь:
    // не угадываем. Обратная ссылка на игрока в §4.24 перестала быть частью
    // сигнатуры и стала деталью (ShellLook.back): ASSEMBLY хранит
    // _currentWeapon (dump11966 0x68F00B0) РАНЬШЕ, чем Player+0xC0
    // (0x68F015C) и PlayerEventHandler+0xC8 (0x68F01E4), и исключение в этом
    // окне (NRE-трамплины 0x68F0480–0x68F0490 на геттерах weaponManager)
    // оставляет панцирь с нулевой ссылкой — прежний детектор его не видел.
    bool fp_build_shell(uint64_t player, uint64_t& fp, ShellLook& look) {
        using namespace go_active;
        look = ShellLook{};
        if (!player || !pointer(io, player + PLAYER_FP_MANAGER, fp) || fp < 0x10000 ||
            !io.named(fp, "FPManager", "Oxide")) return false;
        uint64_t cur = 0;
        if (!pointer(io, fp + FPMANAGER_CURRENT_OBJECT, cur) || cur < 0x10000) return false;
        uint64_t item = 0;
        if (!io.read(cur + FPOBJECT_ITEM, item) || item != 0) return false;
        uint8_t shown = 0xFF;
        if (!io.read(cur + FPOBJECT_SHOWN, shown) || shown != 0) return false;
        // Детали. PEH: здоровая сборка ставит его синхронно, до первого await,
        // поэтому погашенный PEH, продержавшийся секунды, — исключение в окне
        // геттеров. Для классов с override FDN это ещё и блокада каждого дроу:
        // FPBow.FDN (0x68C4970) делает NRE 0x594D46C на [this+0xC8]==0, а гейт
        // CreateFPWeapon зовёт FDN на _currentWeapon (виртуальный слот,
        // 0x68EFA0C) ДО очистки и пересборки — базовый FPObject.FDN (0x68FCD10)
        // на нуле выходит молча, поэтому Томпсон в §4.22 висел 45 с, а лук
        // отравляет мгновенно и навсегда.
        uint64_t peh = 0;
        if (io.read(cur + FPOBJECT_EVENT_HANDLER, peh)) look.peh_null = (peh == 0);
        uint64_t back = 0;
        if (!pointer(io, cur + FPOBJECT_PLAYER_BACKREF, back)) look.back = -1;
        else if (back == player) look.back = 1;
        else look.back = back ? 2 : 0;
        klass_name_read(io, cur, look.klass, sizeof(look.klass));
        return true;
    }

    // §4.22: обеззараживание — обнулить FPManager._currentWeapon (+0x50),
    // чтобы следующий дроу не нашёл панцирь и собрал вьюху заново (гейт
    // 0x68EF940 проверяет _currentWeapon на смерть первым). Это то же, что
    // делает перезайти на сервер, только без перезайти. НЕ через Slot<T>:
    // слот при следующем restore вернул бы освобождённый указатель панциря
    // обратно в поле. Запись сырая, одноразовая, подтверждённая readback.
    bool reset_fp_build(uint64_t fp) {
        using namespace go_active;
        if (!io.write_heap(fp + FPMANAGER_CURRENT_OBJECT, (uint64_t)0)) return false;
        uint64_t cur = 0xdeadbeef;
        if (!io.read(fp + FPMANAGER_CURRENT_OBJECT, cur) || cur != 0) return false;
        ++reset_count_;
        reset_armed_ = false;
        return true;
    }

    // §4.22: карантин — ствол возвращается к родному скину (restore_all), а
    // связка «ItemData.m_Id + индекс» запоминается: тот же скин на том же
    // стволе больше не применяется (стадия 26), пока игрок не выберет другой
    // индекс или другой ствол. Id читается с проверкой живости: мёртвый
    // контекст — не повод читать освобождённую кучу. Если id не прочитался,
    // quarantine_id_ остаётся -1, а настоящие m_Id положительны — такой
    // карантин снимется при первом же резолве: честная деградация до
    // «панцирь сбросили, пробуем снова».
    void quarantine_current(int selected) {
        quarantine_skin_ = selected;
        quarantine_id_ = -1;
        int32_t id = -1;
        if (context_.item_data.object &&
            context_.item_data.life(io) == Life::Alive &&
            io.read(context_.item_data.object + go_active::ITEMDATA_ID, id))
            quarantine_id_ = id;
        restore_all();
    }

    // §4.26: читатели полей среза. Нечитаемое уходит в строку как «?», а не
    // выдуманный ноль: срез существует, чтобы показывать правду, в том числе
    // «транспорт это не читает».
    void dump_hex(uint64_t addr, char* out, size_t cap) {
        uint64_t v = 0;
        if (addr >= 0x10000 && io.read(addr, v))
            std::snprintf(out, cap, "%llx", (unsigned long long)v);
        else
            std::snprintf(out, cap, "?");
    }
    void dump_u32(uint64_t addr, char* out, size_t cap) {
        uint32_t v = 0;
        if (addr >= 0x10000 && io.read(addr, v))
            std::snprintf(out, cap, "%x", v);
        else
            std::snprintf(out, cap, "?");
    }
    void dump_byte(uint64_t addr, char* out, size_t cap) {
        uint8_t v = 0;
        if (addr >= 0x10000 && io.read(addr, v))
            std::snprintf(out, cap, "%u", v);
        else
            std::snprintf(out, cap, "?");
    }
    void dump_i16(uint64_t addr, char* out, size_t cap) {
        int16_t v = 0;
        if (addr >= 0x10000 && io.read(addr, v))
            std::snprintf(out, cap, "%d", (int)v);
        else
            std::snprintf(out, cap, "?");
    }
    void dump_i32(uint64_t addr, char* out, size_t cap) {
        int32_t v = 0;
        if (addr >= 0x10000 && io.read(addr, v))
            std::snprintf(out, cap, "%d", v);
        else
            std::snprintf(out, cap, "?");
    }

    // §4.26: одна строка сырого FP-состояния — всё, из чего состоит конвейер
    // сборки вьюхи. cur с классом и инвариантом (Item/uhq/обратная ссылка/
    // обработчик); CTS/CTS2/UTCS — Kxi/LTI пересоздаёт их на КАЖДЫЙ вызов
    // (dump11966 0x68EE290/0x68EE2B4; 11978 LTI 0x6D598A8/0x6D598CC), поэтому
    // смена их указателей — доказательство, что дроу дошёл до Kxi; флаги
    // uzj/uhF; uzL — piece последней сборки, дошедшей до TAIL; все три копии
    // piece (SyncVar/рабочая/вьюха); гейт дроу PW+0xF0 и ЖИВОЙ слот PW+0x150
    // (11978: живой путь drawSV носит SyncVar и читает слот 0x150; гейт 0xF0
    // вооружает только спящий ceC — по паре gate/slot видно, какой путь жив;
    // 11966: 0xDC; цепочка pMZ->pFz / jH->vVb) и FPManager от PW +0x110;
    // ручка визуала. Срез не зависит от исхода резолва и полезнее
    // всего именно в молчаливых окнах.
    void build_fp_dump(uint64_t player) {
        using namespace go_active;
        char* p = dump_text_;
        size_t left = sizeof(dump_text_);
        *p = '\0';
        auto ap = [&](const char* s) {
            if (left <= 1 || !s || !s[0]) return;
            size_t n = std::strlen(s);
            if (n >= left) n = left - 1;
            std::memcpy(p, s, n);
            p += n; left -= n; *p = '\0';
        };
        char v[24];
        uint64_t fp = 0;
        pointer(io, player + PLAYER_FP_MANAGER, fp);
        const uint64_t fpa = fp >= 0x10000 ? fp : 0;
        uint64_t cur = 0;
        ap("cur=");
        if (fpa && pointer(io, fpa + FPMANAGER_CURRENT_OBJECT, cur) && cur >= 0x10000) {
            dump_hex(cur, v, sizeof(v)); ap(v);
            char klass[20] = {};
            klass_name_read(io, cur, klass, sizeof(klass));
            ap("("); ap(klass[0] ? klass : "?"); ap(")");
            ap(" It=");   dump_hex(cur + FPOBJECT_ITEM, v, sizeof(v)); ap(v);
            ap(" uhq=");  dump_byte(cur + FPOBJECT_SHOWN, v, sizeof(v)); ap(v);
            ap(" back="); dump_hex(cur + FPOBJECT_PLAYER_BACKREF, v, sizeof(v)); ap(v);
            ap(" peh=");  dump_hex(cur + FPOBJECT_EVENT_HANDLER, v, sizeof(v)); ap(v);
        } else {
            ap("0");
        }
        ap(" CTS=");  dump_hex(fpa ? fpa + FPMANAGER_CTS : 0, v, sizeof(v)); ap(v);
        ap("/");      dump_hex(fpa ? fpa + FPMANAGER_CTS2 : 0, v, sizeof(v)); ap(v);
        ap(" UTCS="); dump_hex(fpa ? fpa + FPMANAGER_UTCS : 0, v, sizeof(v)); ap(v);
        ap(" uzj=");  dump_byte(fpa ? fpa + FPMANAGER_UZJ : 0, v, sizeof(v)); ap(v);
        ap(" uhF=");  dump_byte(fpa ? fpa + FPMANAGER_UHF : 0, v, sizeof(v)); ap(v);
        ap(" uzL=");
        dump_byte(fpa ? fpa + FPMANAGER_UZL + WEAPONPIECE_ENABLED : 0, v, sizeof(v)); ap(v); ap("/");
        dump_i16(fpa ? fpa + FPMANAGER_UZL + WEAPONPIECE_NUMBER : 0, v, sizeof(v)); ap(v); ap("/");
        dump_i16(fpa ? fpa + FPMANAGER_UZL + WEAPONPIECE_SKIN : 0, v, sizeof(v)); ap(v);
        ap(" SV=");
        dump_byte(context_.piece ? context_.piece + WEAPONPIECE_ENABLED : 0, v, sizeof(v)); ap(v); ap("/");
        dump_i16(context_.piece ? context_.piece + WEAPONPIECE_NUMBER : 0, v, sizeof(v)); ap(v); ap("/");
        dump_i16(context_.piece ? context_.piece + WEAPONPIECE_SKIN : 0, v, sizeof(v)); ap(v);
        ap(" wk=");
        dump_byte(context_.piece_local ? context_.piece_local + WEAPONPIECE_ENABLED : 0, v, sizeof(v)); ap(v); ap("/");
        dump_i16(context_.piece_local ? context_.piece_local + WEAPONPIECE_NUMBER : 0, v, sizeof(v)); ap(v); ap("/");
        dump_i16(context_.piece_local ? context_.piece_local + WEAPONPIECE_SKIN : 0, v, sizeof(v)); ap(v);
        const uint64_t pw = context_.weapon.object;
        uint64_t view = 0;
        ap(" vw=");
        if (pw && pointer(io, pw + PLAYERWEAPON_FP_VIEW, view) && view >= 0x10000) {
            dump_hex(view, v, sizeof(v)); ap(v); ap(" ");
            dump_byte(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_ENABLED, v, sizeof(v)); ap(v); ap("/");
            dump_i16(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_NUMBER, v, sizeof(v)); ap(v); ap("/");
            dump_i16(view + FPWEAPONVIEW_PIECE + WEAPONPIECE_SKIN, v, sizeof(v)); ap(v);
        } else {
            ap("0");
        }
        ap(" gate="); dump_u32(pw ? pw + PLAYERWEAPON_DRAW_GATE : 0, v, sizeof(v)); ap(v);
        ap(" slot="); dump_u32(pw ? pw + PLAYERWEAPON_DRAW_SLOT : 0, v, sizeof(v)); ap(v);
        // Копии Item.skin: FP-цепь (item=) и кэш дроу pw+0x120 (ditem=
        // указатель:skin). Расхождение item=/ditem= — картина «скин только
        // после дропа+подбора»: экип-дроу via(kJw) строит SyncVar из ditem.
        ap(" item=");
        dump_i32(context_.item.object ? context_.item.object + ITEM_SKIN : 0, v, sizeof(v)); ap(v);
        uint64_t ditem = 0;
        ap(" ditem=");
        if (pw && pointer(io, pw + PLAYERWEAPON_DRAW_ITEM, ditem) && ditem >= 0x10000) {
            dump_hex(pw + PLAYERWEAPON_DRAW_ITEM, v, sizeof(v)); ap(v); ap(":");
            dump_i32(ditem + ITEM_SKIN, v, sizeof(v)); ap(v);
        } else {
            ap("0");
        }
        // §4.38: kJw=PW+0x110? (ditem — это (PW+0x110)+0x10). +0x18 записи
        // (pw+0x128) == gate подтверждает kJw-форму на поле PW.
        uint32_t pw128v = 0;
        if (pw && io.read(pw + PLAYERWEAPON_DRAW_ITEM - KJW_ITEM + KJW_SLOT, pw128v))
            std::snprintf(v, sizeof(v), " pw128=%x", pw128v);
        else
            std::snprintf(v, sizeof(v), " pw128=x");
        ap(v);
        ap(" fpw=");  dump_hex(pw ? pw + PLAYERWEAPON_FP_MANAGER : 0, v, sizeof(v)); ap(v);
        ap(" hnd=");  dump_hex(pw ? pw + PLAYERWEAPON_SKIN_HANDLE + SKINHANDLE_MODEL : 0, v, sizeof(v)); ap(v);
        // §4.28: зонд пояса — живость якорей data:client:warr:pw (§4.32: 4-й —
        // PlayerWeapon), c — кандидатов в последнем проходе, t — ведётся.
        // c=0 при живых якорях = пояс глубже двух уровней; t>0 без
        // «инвентарного» срабатывания видно по item=.
        std::snprintf(v, sizeof(v), " inv=%d:%d:%d:%d:c%dt%d",
                      inv_dbg_data_ ? 1 : 0, inv_dbg_client_ ? 1 : 0,
                      inv_dbg_warr_ ? 1 : 0, inv_dbg_pw_ ? 1 : 0,
                      inv_probe_n_, inv_count_);
        ap(v);
        // §4.31: форма прохода — длина warr, опознанных массивов, попыток L2
        // (o>32 — бюджет переполнен) и почти-попаданий (чужой ItemData).
        // §4.32: +k (живых kJw) и +z (пустых kJw: Item null, слот <= 64).
        char pv[40];
        std::snprintf(pv, sizeof(pv), "/w%da%do%dm%dk%dz%d", inv_dbg_wlen_,
                      inv_dbg_arrays_, inv_dbg_l2push_, inv_dbg_miss_,
                      inv_dbg_kjw_, inv_dbg_kjw_null_);
        ap(pv);
        // §4.30: флаги Loaded SV/wk/vw одним триплетом (x — не читается).
        // Диагностика эксперимента со сбросом: запекает ли сборка 1.
        char lb[3] = {'x', 'x', 'x'};
        uint8_t lv = 0;
        if (context_.piece &&
            io.read(context_.piece + WEAPONPIECE_LOADED, lv) && lv <= 1)
            lb[0] = (char)('0' + lv);
        if (context_.piece_local &&
            io.read(context_.piece_local + WEAPONPIECE_LOADED, lv) && lv <= 1)
            lb[1] = (char)('0' + lv);
        uint64_t lvw = 0;
        if (pw && pointer(io, pw + PLAYERWEAPON_FP_VIEW, lvw) && lvw >= 0x10000 &&
            io.read(lvw + FPWEAPONVIEW_PIECE + WEAPONPIECE_LOADED, lv) && lv <= 1)
            lb[2] = (char)('0' + lv);
        std::snprintf(v, sizeof(v), " L=%c%c%c", lb[0], lb[1], lb[2]);
        ap(v);
        // §4.32: гейты via(kJw) (KERNEL.md §29a) — почему поле берёт пустую
        // ветвь (ditem=0, сборка -1), а здоровая s44 живую. ks — skin живого
        // kJw-Item (x — живых нет); u93 — ItemData+0x93 (гейт g5: проверять
        // анлок Dg.ZpX); flg — флаговый байт гейта g2; nc — кэш number
        // pw+0x128 (-1 всегда = живой путь не срабатывал ни разу).
        if (inv_dbg_kjw_skin_ok_)
            std::snprintf(v, sizeof(v), " ks=%d", (int)inv_dbg_kjw_skin_);
        else
            std::snprintf(v, sizeof(v), " ks=x");
        ap(v);
        uint8_t u93 = 0;
        if (context_.item_data.object &&
            io.read(context_.item_data.object + ITEMDATA_UNLOCK_GATE, u93))
            std::snprintf(v, sizeof(v), " u93=%u", (unsigned)u93);
        else
            std::snprintf(v, sizeof(v), " u93=x");
        ap(v);
        uint8_t flg = 0;
        uint64_t fga = 0, fgb = 0;
        if (context_.player.object &&
            pointer(io, context_.player.object + PLAYER_GATE_A, fga) &&
            fga >= 0x10000 &&
            pointer(io, fga + PLAYER_GATE_B, fgb) && fgb >= 0x10000 &&
            io.read(fgb + PLAYER_GATE_C, flg))
            std::snprintf(v, sizeof(v), " flg=%u", (unsigned)flg);
        else
            std::snprintf(v, sizeof(v), " flg=x");
        ap(v);
        // §4.40: гейт 2 via (KERNEL §29a): флаговый байт player+0x78->+0x1A0
        // ->+0x20. ditem пуст => живая ветвь via не бежит => дроу душит гейт,
        // а не шторм; значение при отказе vs песочнице решает, этот ли.
        // x1/x2/x3 — на каком хопе цепочка оборвалась.
        {
            uint64_t f1 = 0, f2 = 0;
            uint8_t fb = 0;
            const uint64_t pl = context_.player.object;
            if (!pl || !pointer(io, pl + 0x78, f1) || f1 < 0x10000)
                std::snprintf(v, sizeof(v), " fch=x1");
            else if (!pointer(io, f1 + 0x1A0, f2) || f2 < 0x10000)
                std::snprintf(v, sizeof(v), " fch=x2");
            else if (!io.read(f2 + 0x20, fb))
                std::snprintf(v, sizeof(v), " fch=x3");
            else
                std::snprintf(v, sizeof(v), " fch=%x", fb);
            ap(v);
        }
        int16_t nc = 0;
        if (pw && io.read(pw + PLAYERWEAPON_NUMBER_CACHE, nc))
            std::snprintf(v, sizeof(v), " nc=%d", (int)nc);
        else
            std::snprintf(v, sizeof(v), " nc=x");
        ap(v);
        // §4.33: zh — пустой kJw-слот живого слота (hex как gate=, x — нет),
        // zhm — сколько таких, za — адрес первого. zh трекает gate= 19->1a
        // между срезами = слоты настоящие, следующий шаг — инъекция Item.
        if (inv_dbg_zh_ok_)
            std::snprintf(v, sizeof(v), " zh=%x", inv_dbg_zh_);
        else
            std::snprintf(v, sizeof(v), " zh=x");
        ap(v);
        std::snprintf(v, sizeof(v), " zhm=%d", inv_dbg_zhm_);
        ap(v);
        std::snprintf(v, sizeof(v), " rej=%d", inv_dbg_rej_);   // §4.37
        ap(v);
        if (inv_dbg_zh_ok_)
            std::snprintf(v, sizeof(v), " za=%llx",
                          (unsigned long long)inv_dbg_za_);
        else
            std::snprintf(v, sizeof(v), " za=x");
        ap(v);
        // §4.34: yh — запись своего слота с не-нулевым Item (адрес/x), yhm —
        // сколько таких; qj — инъекция (0 нет, 1 null-fill, 2 overwrite).
        if (inv_dbg_yh_ok_)
            std::snprintf(v, sizeof(v), " yh=%llx",
                          (unsigned long long)inv_dbg_yh_);
        else
            std::snprintf(v, sizeof(v), " yh=x");
        ap(v);
        std::snprintf(v, sizeof(v), " yhm=%d", inv_dbg_yhm_);
        ap(v);
        // §4.40: yh-any — первая валидная (Item, слот)-запись без привязки
        // к живому слоту (адрес/x), её слот (hex/x) и счётчик.
        if (inv_dbg_yha_ok_)
            std::snprintf(v, sizeof(v), " yha=%llx",
                          (unsigned long long)inv_dbg_yha_);
        else
            std::snprintf(v, sizeof(v), " yha=x");
        ap(v);
        if (inv_dbg_yha_ok_)
            std::snprintf(v, sizeof(v), " yhas=%x", inv_dbg_yhas_);
        else
            std::snprintf(v, sizeof(v), " yhas=x");
        ap(v);
        std::snprintf(v, sizeof(v), " yhan=%d", inv_dbg_yhan_);
        ap(v);
        // §4.41: чужой X — счёт, первая (адрес/x), её слот (hex/x), класс.
        std::snprintf(v, sizeof(v), " yhn=%d", inv_dbg_yhn_);
        ap(v);
        if (inv_dbg_yhx_ok_)
            std::snprintf(v, sizeof(v), " yhx=%llx",
                          (unsigned long long)inv_dbg_yhx_);
        else
            std::snprintf(v, sizeof(v), " yhx=x");
        ap(v);
        if (inv_dbg_yhx_ok_)
            std::snprintf(v, sizeof(v), " yhxs=%x", inv_dbg_yhxs_);
        else
            std::snprintf(v, sizeof(v), " yhxs=x");
        ap(v);
        if (inv_dbg_yhx_ok_)
            std::snprintf(v, sizeof(v), " yhxc=%s", inv_dbg_yhxc_);
        else
            std::snprintf(v, sizeof(v), " yhxc=-");
        ap(v);
        // §4.42: G3/G4 chain-walk — хоп, вердикты, биты, слот HcG, kJw дроу.
        std::snprintf(v, sizeof(v), " kh=%x", inv_dbg_kh_);
        ap(v);
        std::snprintf(v, sizeof(v), " kv=%d", inv_dbg_kv_);
        ap(v);
        std::snprintf(v, sizeof(v), " kl=%d", inv_dbg_kl_);
        ap(v);
        std::snprintf(v, sizeof(v), " kf=%d", inv_dbg_kf_);
        ap(v);
        std::snprintf(v, sizeof(v), " kb=%d", inv_dbg_kb_);
        ap(v);
        std::snprintf(v, sizeof(v), " ks=%x", (unsigned)inv_dbg_ks_);
        ap(v);
        if (inv_dbg_kj_ok_)
            std::snprintf(v, sizeof(v), " kj=%llx",
                          (unsigned long long)inv_dbg_kj_);
        else
            std::snprintf(v, sizeof(v), " kj=x");
        ap(v);
        const int qj = !kjw_item_.owned ? 0 : (kjw_item_.original == 0 ? 1 : 2);
        std::snprintf(v, sizeof(v), " qj=%d", qj);
        ap(v);
        std::snprintf(v, sizeof(v), " df=%d", ditem_dbg_);   // §4.38
        ap(v);
        // §4.36: настоящий kJw (аргумент via, x20) — НЕ za-декой: сессия 39
        // #6->#7 — дроу под пином qj=1 построил -1, запись игрой не читается.
        // x19 (PW) и x20 (kJw) — разные регистры кэша дроу: kJw не PW.
        // Кандидаты: сам FPManager (+0x10 Item?, +0x18 слот?) и FPWeaponBase
        // (FPM+0x58). null + слот==gate = кандидат в настоящий kJw.
        uint64_t fpm = 0;
        if (pw) pointer(io, pw + PLAYERWEAPON_FP_MANAGER, fpm);
        uint64_t fm10v = 0;
        const bool fm10_ok =
            fpm >= 0x10000 && pointer(io, fpm + KJW_ITEM, fm10v);
        if (!fm10_ok)
            std::snprintf(v, sizeof(v), " fm10=x");
        else if (fm10v == 0)
            std::snprintf(v, sizeof(v), " fm10=null");
        else
            std::snprintf(v, sizeof(v), " fm10=%llx",
                          (unsigned long long)fm10v);
        ap(v);
        uint32_t fm18v = 0;
        if (fpm >= 0x10000 && io.read(fpm + KJW_SLOT, fm18v))
            std::snprintf(v, sizeof(v), " fm18=%x", fm18v);
        else
            std::snprintf(v, sizeof(v), " fm18=x");
        ap(v);
        uint64_t cw = 0;
        if (fpm >= 0x10000) pointer(io, fpm + FPMANAGER_CURRENT_WEAPON, cw);
        uint64_t cw10v = 0;
        const bool cw10_ok =
            cw >= 0x10000 && pointer(io, cw + KJW_ITEM, cw10v);
        if (!cw10_ok)
            std::snprintf(v, sizeof(v), " cw10=x");
        else if (cw10v == 0)
            std::snprintf(v, sizeof(v), " cw10=null");
        else
            std::snprintf(v, sizeof(v), " cw10=%llx",
                          (unsigned long long)cw10v);
        ap(v);
        uint32_t cw18v = 0;
        if (cw >= 0x10000 && io.read(cw + KJW_SLOT, cw18v))
            std::snprintf(v, sizeof(v), " cw18=%x", cw18v);
        else
            std::snprintf(v, sizeof(v), " cw18=x");
        ap(v);
    }

    // §4.26: ритм срезов — на изменение триггерных полей (указатель cur,
    // uzL.Number, голова SyncVar) и по сердцебиению, с минимальным интервалом,
    // чтобы быстрая смена стволов не топила лог. Зовётся каждый тик из
    // включённой ветки ДО резолва: срез обязан строиться и в нерезолве.
    // Триггер, пришедшийся на интервал ожидания, не теряется (last_* не
    // обновляются) — срез будет собран сразу по истечении интервала с
    // новейшим состоянием.
    void dump_watch(uint64_t player) {
        using namespace go_active;
        if (!has_state()) return;
        ++dump_ticks_;
        if (dump_cooldown_ > 0) --dump_cooldown_;
        uint64_t fp = 0;
        pointer(io, player + PLAYER_FP_MANAGER, fp);
        uint64_t cur = 0;
        if (fp >= 0x10000) pointer(io, fp + FPMANAGER_CURRENT_OBJECT, cur);
        int16_t uzl_n = 0;
        const bool uzl_ok = fp >= 0x10000 &&
            io.read(fp + FPMANAGER_UZL + WEAPONPIECE_NUMBER, uzl_n);
        uint32_t sv_head = 0xFFFFFFFFu;
        uint8_t en = 0;
        int16_t num = 0;
        if (context_.piece && io.read(context_.piece + WEAPONPIECE_ENABLED, en) &&
            io.read(context_.piece + WEAPONPIECE_NUMBER, num))
            sv_head = ((uint32_t)en << 16) | (uint32_t)(uint16_t)num;
        const bool changed = cur != last_cur_ ||
                             (uzl_ok && (int)uzl_n != last_uzl_number_) ||
                             sv_head != last_sv_head_ ||
                             inv_count_ != last_inv_count_;
        if (!changed && dump_ticks_ < kDumpHeartbeatTicks) return;
        if (dump_cooldown_ > 0) return;
        build_fp_dump(player);
        ++dump_seq_;
        last_cur_ = cur;
        last_uzl_number_ = uzl_ok ? (int)uzl_n : 0x7FFFFFFF;
        last_sv_head_ = sv_head;
        last_inv_count_ = inv_count_;
        dump_ticks_ = 0;
        dump_cooldown_ = kDumpCooldownTicks;
    }

public:
    viewmodel::Status status = viewmodel::Status::Off;
    explicit Controller(IO& memory) : io(memory) {}
    // §4.39: kill-switches — только для тестов (production OFF навсегда,
    // пока не найден настоящий kJw с валидацией; не выносить в меню).
    void set_kjw_adopt(bool on) { kjw_adopt_enabled_ = on; }
    void set_ditem_fill(bool on) { ditem_fill_enabled_ = on; }
    int stage() const { return stage_; }
    bool has_state() const {
        return skin_.owned || level_.owned || skin_local_.owned || level_local_.owned ||
               skin_item_.owned || level_item_.owned ||
               skin_item2_.owned || level_item2_.owned ||
               skin_view_.owned || level_view_.owned ||
               loaded_sv_.owned || loaded_wk_.owned || loaded_view_.owned ||
               head_lo_local_.owned || head_hi_local_.owned ||
               inv_count_ > 0;
    }
    // Одноразовый запрос перезаписать обе копии piece, даже если значения уже
    // совпадают (меню «Переписать обе копии» / one-shot CLI `--rebuild`). Сама
    // пересборка вьюхи — событие игры (холстер/дроу, RPC насадок/модов,
    // спавн/подбор ствола), внешний процесс её запустить не может; запрос лишь
    // гарантирует, что к событию в обеих копиях лежит наш индекс.
    void request_rebuild() { rewrite_request_ = true; }
    // Одноразовый запрос перечитать список. Нужен и когда функция ВЫКЛЮЧЕНА:
    // меню показывает список до включения, а CLI `skins` только читает. Запрос
    // стоит одно перечитывание (строки читаются посимвольно), поэтому его
    // ставит вызывающий, а не каждый тик.
    void request_scan() { scan_request_ = true; }
    int entries(viewmodel::SkinEntryView* out, int max) const {
        if (!out || max <= 0) return 0;
        const int n = entry_count_ < max ? entry_count_ : max;
        for (int i = 0; i < n; ++i) out[i] = entries_[i];
        return n;
    }
    viewmodel::SkinUiInfo ui() const {
        viewmodel::SkinUiInfo info;
        info.count = entry_count_;
        info.skin = live_skin_;
        info.level = live_level_;
        info.piece_read = piece_read_;
        info.fp_stale = fp_stale_;
        info.handle_live = handle_live_;
        info.local_state = local_state_;
        info.local_refill = head_lo_local_.owned;
        info.item_state = item_state_;
        info.inv_state = inv_state_;
        info.inv_count = inv_count_;
        info.view_state = view_state_;
        info.fp_build_resets = reset_count_;
        info.quarantine_skin = quarantine_skin_;
        // §4.24: наблюдение за панцирём и беспанцирным застреванием — лог
        // пишет периодические строки, меню показывает вердикт.
        info.shell_watch = shell_ticks_ / 10;
        info.shell_peh_null = shell_peh_null_;
        info.shell_back = shell_ticks_ > 0 ? shell_back_ : -1;
        info.stuck_seconds = stuck_ticks_ / 10;
        info.cache_seconds = cache_ticks_ / 10;
        info.stall_seconds = stall_ticks_ / 10;
        info.stall_hand_number = stall_hand_;
        info.stall_view_number = stall_view_;
        info.dump_seq = dump_seq_;
        std::snprintf(info.dump_text, sizeof(info.dump_text), "%s", dump_text_);
        std::snprintf(info.shell_class, sizeof(info.shell_class), "%s",
                      shell_ticks_ > 0 ? shell_class_ : "");
        std::snprintf(info.local_head, sizeof(info.local_head), "%s", local_head_raw_);
        info.stage = stage_;
        info.status = status;
        std::snprintf(info.weapon, sizeof(info.weapon), "%s", weapon_name_);
        return info;
    }
    bool reset(bool restore_originals) {
        const bool ok = !restore_originals || restore_all();
        skin_ = {}; level_ = {}; skin_local_ = {}; level_local_ = {};
        skin_item_ = {}; level_item_ = {};
        skin_item2_ = {}; level_item2_ = {}; draw_item_ = {};
        skin_view_ = {}; level_view_ = {};
        view_state_ = -1;
        loaded_sv_ = {}; loaded_wk_ = {}; loaded_view_ = {};
        head_lo_local_ = {}; head_hi_local_ = {};
        for (int i = 0; i < kMaxInv; ++i) inv_[i] = {};
        inv_count_ = 0;
        inv_probe_n_ = 0;
        inv_probe_tick_ = 0;
        inv_state_ = -1;
        // §4.34: kJw-инъекция забывается БЕЗ записи (адрес из прошлой жизни).
        kjw_item_ = {};
        kjw_prev_ = 0;
        kjw_slot_ = -1;
        // §4.38: ditem забывается БЕЗ записи (адрес из прошлой жизни).
        ditem_ = {};
        ditem_dbg_ = 0;
        context_ = {}; weapon_cache_ = 0;
        rewrite_request_ = false;
        scan_request_ = false;
        entry_count_ = 0; scan_age_ = 0;
        for (auto& e : entries_) e = {};
        weapon_name_[0] = '\0';
        live_skin_ = live_level_ = -1;
        piece_read_ = false;
        fp_stale_ = handle_live_ = false;
        local_state_ = -1;
        item_state_ = -1;
        local_head_raw_[0] = '\0';
        shell_ticks_ = 0;
        stuck_ticks_ = 0;
        cache_ticks_ = 0;
        stall_ticks_ = 0;
        stall_hand_ = stall_view_ = -1;
        // §4.26: состояние срезов сбрасывается, кроме монотонного dump_seq_ —
        // нумерация живёт всю жизнь процесса, дедуп лога по ней не сбивается.
        dump_ticks_ = 0;
        dump_cooldown_ = 0;
        dump_text_[0] = '\0';
        last_sv_head_ = 0xFFFFFFFFu;
        last_uzl_number_ = 0x7FFFFFFF;
        last_cur_ = ~0ull;
        shell_peh_null_ = false;
        shell_back_ = -1;
        shell_class_[0] = '\0';
        reset_armed_ = true;
        reset_count_ = 0;
        quarantine_id_ = -1;
        quarantine_skin_ = -1;
        session_ = base_ = 0;
        status = viewmodel::Status::Off;
        return ok;
    }

    void tick(uint64_t session, uint64_t base, uint64_t player, const ViewmodelSettings& raw) {
        using viewmodel::Status;
        const ViewmodelSettings s = viewmodel::sanitize(raw);
        if (session != session_ || base != base_) {
            // Сменилась сессия/база — адреса из прошлой жизни процесса
            // забываются БЕЗ записи (письмо в мёртвый процесс опаснее
            // незакрытой правки; см. DETECTION.md).
            reset(false);
            session_ = session; base_ = base;
        }
        status = s.skin_enabled ? Status::Waiting : Status::Off;
        // Запрос «переписать обе копии» живёт один тик и имеет смысл только
        // при включённой функции: в выключенном состоянии писать нечего.
        const bool force_rewrite = rewrite_request_;
        rewrite_request_ = false;
        if (!s.skin_enabled) {
            if (!restore_all()) status = Status::Failed;
            if (!has_state()) {
                live_skin_ = live_level_ = -1;
                piece_read_ = false;
                fp_stale_ = handle_live_ = false;
                local_state_ = -1;
                item_state_ = -1;
                local_head_raw_[0] = '\0';
            }
            // Выключенная функция не пишет НИЧЕГО, но список по явному запросу
            // перечитывает: меню рисует его до включения тумблера, а CLI
            // `skins` только читает.
            if (scan_request_ && base) {
                scan_request_ = false;
                Context probe;
                if (resolve(player, probe)) {
                    context_ = probe;
                    rescan();
                    read_live_state();
                } else {
                    entry_count_ = 0;
                }
            } else if (!has_state()) {
                entry_count_ = 0;
            }
            return;
        }
        if (!base) return;

        // §4.26: сырой срез FP-состояния — ДО резолва, чтобы строился и при
        // успехе, и при провале: самые ценные срезы приходятся на молчаливые
        // окна «применено» и нерезолва.
        dump_watch(player);

        Context next;
        if (!resolve(player, next)) {
            // §4.24: зомби-резолв — форма УСПЕШНОГО резолва через кэш uzN;
            // в нерезолв-ветке ей места нет (там считают панцирь и застревание).
            cache_ticks_ = 0;
            stall_ticks_ = 0;
            stall_hand_ = stall_view_ = -1;
            // Нерезолв (пустые руки, игрок временно вышел из мира) — не повод
            // возвращать оригиналы: слоты держат живые адреса, и следующий
            // успешный резолв либо подтвердит их (записи не будет вовсе), либо
            // смену/смерть контекста обработает ветка ниже и restore_all().
            // Прежняя версия возвращала оригиналы здесь и на переходе мира
            // стирала наш индекс из SyncVar и Item — ровно перед спавном,
            // которому они нужны (устройство: skin.log v12, +60 с).
            // §4.22: детектор панциря. Смотрим только когда что-то уже
            // применено (has_state): в свежей сессии _currentWeapon тоже
            // бывает пуст, но это нормальный холстер, а не отравление.
            // Порог kShellWatchTicks отделяет отравление от здоровой первой
            // закачки ассета — у неё сигнатура панциря та же (On_Draw зовётся
            // только в async-колбэке загрузки). Сброс одноразовый на эпизод:
            // взводит его обратно только успешный резолв (вьюха собралась).
            if (has_state()) {
                uint64_t fp = 0;
                ShellLook look;
                if (fp_build_shell(player, fp, look)) {
                    ++shell_ticks_;
                    shell_peh_null_ = look.peh_null;
                    shell_back_ = look.back;
                    std::snprintf(shell_class_, sizeof(shell_class_), "%s", look.klass);
                    // §4.24: два порога. Погашенный PlayerEventHandler —
                    // отравление, доказанное структурой дампа (FPBow.FDN на нём
                    // NRE; гейт зовёт FDN до очистки), а здоровая сборка держит
                    // его погашенным лишь субмиллисекундное синхронное окно
                    // между хранениями — 20 тиков застрахованы от случайного
                    // попадания тика в середину сборки. Ссылка на игрока при
                    // живом PEH — прежний медленный путь §4.22: здоровая ПЕРВАЯ
                    // закачка ассета держит ту же сигнатуру до async-колбэка
                    // On_Draw, поэтому вердикт только после 45 с наблюдения.
                    // Чужая/нулевая ссылка при живом PEH — форма неизвестна:
                    // наблюдаем и пишем в лог, но не пишем в память игры.
                    const bool poison_now =
                        look.peh_null && shell_ticks_ >= kShellPoisonTicks;
                    const bool hung_long =
                        !look.peh_null && look.back == 1 && shell_ticks_ >= kShellWatchTicks;
                    if (reset_armed_ && (poison_now || hung_long) && reset_fp_build(fp))
                        quarantine_current(s.skin_index);
                } else {
                    shell_ticks_ = 0;
                    shell_peh_null_ = false;
                    shell_back_ = -1;
                    shell_class_[0] = '\0';
                }
                // §4.24: панциря нет, но piece «держится в руках» (Enabled=1),
                // а FP-цепочка не резолвится на стадиях 7–9 — игра уронила
                // сборку раньше хранения FPObject (инстанцирование/хвост):
                // сбрасывать нечего, но лог обязан видеть форму, а не молчать.
                if (shell_ticks_ > 0) {
                    stuck_ticks_ = 0;   // панцирь учитывает shell_watch
                } else if ((stage_ == 7 || stage_ == 8 || stage_ == 9) &&
                           context_.weapon.object &&
                           context_.weapon.life(io) == Life::Alive) {
                    Piece held;
                    if (read_piece(io, context_.piece, held) == PieceRead::Ok &&
                        held.enabled == 1)
                        ++stuck_ticks_;
                    else
                        stuck_ticks_ = 0;
                } else {
                    stuck_ticks_ = 0;
                }
            } else {
                shell_ticks_ = 0;
                stuck_ticks_ = 0;
                shell_peh_null_ = false;
                shell_back_ = -1;
                shell_class_[0] = '\0';
            }
            // §4.21: погашенную голову рабочей копии достраиваем и без рук —
            // на дроу hbr.pMZ читает её напрямую из PW +0xC0 (11978: +0xD0), минуя цепочку
            // FPObject, и погашенная голова превращает дроу в Release.
            if (has_state() && context_.weapon.object &&
                context_.weapon.life(io) == Life::Alive) {
                Piece syncvar, work;
                if (read_piece(io, context_.piece, syncvar) == PieceRead::Ok &&
                    syncvar.enabled == 1 &&
                    read_piece(io, context_.piece_local, work, nullptr, true) == PieceRead::Ok &&
                    work.enabled == 0)
                    refill_local_head(nullptr);   // фон: статус Waiting, стадии не трогаем
            }
            // §4.27: записи сквозь блэкаут дроу. На сервере дроу бьёт дважды:
            // холстер роняет резолв (стадия 9, It=0 — слоты молчат весь тик),
            // а подъём переписывает SyncVar и рабочую копию серверной правдой
            // (-1) ДО чтения головой сборки — вьюха строится из -1, и тик
            // после подъёма переписывает уже построенное (skin.log: SV/wk -1
            // пойманы на uhq=1, вьюха мёрзлая; в песочнице переписи нет,
            // поэтому там работает). Пока кэшированный ствол жив, держим наш
            // индекс во всех четырёх точках каждый тик блэкаута: переписка
            // одноразовая в начале окна, TAIL — в конце (видны uhq=0 с живой
            // головой), и на чтении стоит последняя запись. Стадии 7–9 — цепь
            // ПОСЛЕ ствола (FPObject/Item): ствол подтверждён свежим резолвом,
            // адреса кэша — его же; piece лежат внутри PW инлайном, поэтому
            // life(weapon)==Alive доказывает и их. Оригинал не портится:
            // переписка несёт серверную правду, она же была оригиналом.
            // Best-effort и молча: статус Waiting и стадию резолва не трогаем.
            if (has_state() && context_.weapon.object &&
                context_.weapon.life(io) == Life::Alive &&
                (stage_ == 7 || stage_ == 8 || stage_ == 9)) {
                int levels = 1;
                bool index_known = false;
                for (int i = 0; i < entry_count_; ++i) {
                    if (entries_[i].index == s.skin_index) {
                        index_known = true;
                        levels = entries_[i].levels > 0 ? entries_[i].levels : 1;
                        break;
                    }
                }
                if (index_known) {
                    const int16_t want_skin = (int16_t)s.skin_index;
                    const uint8_t want_level =
                        (uint8_t)(s.skin_level < levels ? s.skin_level : levels - 1);
                    Piece sv;
                    if (read_piece(io, context_.piece, sv, nullptr, true) == PieceRead::Ok) {
                        skin_.sample_value(context_.piece + go_active::WEAPONPIECE_SKIN, sv.skin);
                        level_.sample_value(context_.piece + go_active::WEAPONPIECE_SKIN_LEVEL, sv.level);
                        skin_.put(io, want_skin);
                        level_.put(io, want_level);
                        // §4.29: вьюха красится и в блэкауте — страж по
                        // Number внутри (погашенная SyncVar не проходит).
                        maintain_view(want_skin, want_level, sv.number, false);
                        maintain_loaded(sv.number);
                    }
                    Piece wk;
                    if (read_piece(io, context_.piece_local, wk, nullptr, true) == PieceRead::Ok) {
                        skin_local_.sample_value(
                            context_.piece_local + go_active::WEAPONPIECE_SKIN, wk.skin);
                        level_local_.sample_value(
                            context_.piece_local + go_active::WEAPONPIECE_SKIN_LEVEL, wk.level);
                        skin_local_.put(io, want_skin);
                        level_local_.put(io, want_level);
                    }
                    if (context_.item.object &&
                        context_.item.life(io) == Life::Alive) {
                        int32_t item_skin = 0;
                        uint8_t item_level = 0;
                        if (io.read(context_.item.object + go_active::ITEM_SKIN, item_skin) &&
                            io.read(context_.item.object + go_active::ITEM_SKIN_LEVEL, item_level)) {
                            skin_item_.sample_value(
                                context_.item.object + go_active::ITEM_SKIN, item_skin);
                            level_item_.sample_value(
                                context_.item.object + go_active::ITEM_SKIN_LEVEL, item_level);
                            skin_item_.put(io, (int32_t)want_skin);
                            level_item_.put(io, want_level);
                        }
                    }
                    // Копия 4 — тот же резолв кэша дроу, что в основной ветке:
                    // ресинк инвентаря ротирует экземпляр без пересборки.
                    uint64_t ditem = 0, ddata = 0;
                    if (pointer(io, context_.weapon.object + go_active::PLAYERWEAPON_DRAW_ITEM,
                                ditem) &&
                        ditem >= 0x10000 && ditem != context_.item.object &&
                        pointer(io, ditem + go_active::ITEM_DATA, ddata) &&
                        ddata == context_.item_data.object) {
                        if (draw_item_.object != ditem) {
                            Identity probe{};
                            draw_item_ = probe.capture(io, ditem, false) ? probe : Identity{};
                        }
                    } else {
                        draw_item_ = {};
                    }
                    if (draw_item_.object && draw_item_.life(io) == Life::Alive) {
                        int32_t draw_skin = 0;
                        uint8_t draw_level = 0;
                        if (io.read(draw_item_.object + go_active::ITEM_SKIN, draw_skin) &&
                            io.read(draw_item_.object + go_active::ITEM_SKIN_LEVEL, draw_level)) {
                            skin_item2_.sample_value(
                                draw_item_.object + go_active::ITEM_SKIN, draw_skin);
                            level_item2_.sample_value(
                                draw_item_.object + go_active::ITEM_SKIN_LEVEL, draw_level);
                            skin_item2_.put(io, (int32_t)want_skin);
                            level_item2_.put(io, want_level);
                        }
                    }
                    // §4.28: пояс стабилен и от FP-цепи не зависит — ведём и в блэкауте.
                    // §4.34: FP-Item мёртв — вливать нечего (0 снимает инъекцию).
                    maintain_inventory(want_skin, want_level, 0);
                }
            }
            return;
        }
        if (!(next == context_)) {
            // §4.27: чистая ротация экземпляров — дроу пересоздал FPObject/Item
            // того же ствола. Без restore_all (он моргнул бы оригиналом в
            // SyncVar на каждом дроу) и без рескана (строки по символу на
            // каждом дроу): слот Item пересэмплится на новый адрес ниже сам —
            // смена адреса обновляет оригинал значением свежего экземпляра.
            if (has_state() && next.same_gun(context_)) {
                context_ = next;
            } else {
                // Сменился ствол/предмет: старый возвращаем, новый перехватываем.
                // Оригинал индекса принадлежит ПРОШЛОМУ стволу — возвращать его в
                // новый бессмысленно, поэтому при смене контекста владение
                // снимается без записи только если возврат не прошёл.
                if (!restore_all()) {
                    skin_ = {}; level_ = {}; skin_local_ = {}; level_local_ = {};
                    skin_item_ = {}; level_item_ = {};
                    skin_item2_ = {}; level_item2_ = {}; draw_item_ = {};
                    skin_view_ = {}; level_view_ = {};
                    loaded_sv_ = {}; loaded_wk_ = {}; loaded_view_ = {};
                    head_lo_local_ = {}; head_hi_local_ = {};
                }
                context_ = next;
                scan_age_ = kScanEvery;   // перечитать список сразу
                entry_count_ = 0;
            }
        }

        if (scan_request_ || scan_age_ >= kScanEvery || entry_count_ == 0) rescan();
        else ++scan_age_;
        scan_request_ = false;

        // §4.22: карантин проверяется ПОСЛЕ перечитывания списка — меню обязано
        // показывать скины и у ствола на карантине, иначе непонятно, почему
        // выбор не действует. Ствол опознаётся по ItemData.m_Id, а не по
        // адресу или классу: класс общий у нескольких предметов, а «сломал
        // сборку» — свойство конкретного ствола (другой предмет того же класса
        // обязан пробовать свой скин заново). Тот же ствол с тем же индексом —
        // стадия 26, ничего не пишем; другой индекс или другой ствол —
        // карантин снят и сброс снова взведён (игрок сам выбрал повтор).
        if (quarantine_skin_ >= 0) {
            int32_t qid = -1;
            if (context_.item_data.object &&
                context_.item_data.life(io) == Life::Alive &&
                io.read(context_.item_data.object + go_active::ITEMDATA_ID, qid) &&
                qid == quarantine_id_ && s.skin_index == quarantine_skin_) {
                status = Status::Failed;
                stage_ = 26;
                return;
            }
            quarantine_skin_ = -1;
            quarantine_id_ = -1;
            reset_armed_ = true;
        }

        // Индекс обязан существовать в массиве предмета: запись числа, которого
        // у этого ствола нет, — это мусор в SyncVar, а не скин.
        bool index_known = false;
        int levels = 1;
        for (int i = 0; i < entry_count_; ++i) {
            if (entries_[i].index == s.skin_index) {
                index_known = true;
                levels = entries_[i].levels > 0 ? entries_[i].levels : 1;
                break;
            }
        }
        if (!index_known) { status = Status::Failed; stage_ = 13; return; }

        Piece piece;
        const PieceRead piece_read = read_piece(io, context_.piece, piece);
        if (piece_read != PieceRead::Ok) {
            // При NotAPiece сырые значения всё равно показываются: мусорный
            // Skin в логе и в меню видно глазами, и это отличает «оффсет не от
            // этого билда» от «транспорт не читает».
            piece_read_ = (piece_read == PieceRead::NotAPiece);
            live_skin_ = piece_read_ ? piece.skin : -1;
            live_level_ = piece_read_ ? piece.level : -1;
            status = Status::Failed;
            stage_ = (piece_read == PieceRead::Unreadable) ? 14 : 15;
            return;
        }
        const int16_t want_skin = (int16_t)s.skin_index;
        const uint8_t want_level = (uint8_t)(s.skin_level < levels ? s.skin_level : levels - 1);
        // Копия 1 — SyncVar (+0x110): её читают серверные string-RPC, phW и
        // get_TjU; OnStartClient берёт из неё piece для pMd.
        skin_.sample_value(context_.piece + go_active::WEAPONPIECE_SKIN, piece.skin);
        level_.sample_value(context_.piece + go_active::WEAPONPIECE_SKIN_LEVEL, piece.level);
        // «Переписать обе копии» снимает свежесть ПОСЛЕ выборки: put() обязан
        // записать даже совпадающее значение и подтвердить readback'ом, а
        // sample_value иначе вернул бы fresh и сила запроса пропала бы.
        if (force_rewrite) { skin_.invalidate(); level_.invalidate(); }
        if (!skin_.put(io, want_skin)) { status = Status::Failed; stage_ = 16; return; }
        if (!level_.put(io, want_level)) { status = Status::Failed; stage_ = 17; return; }

        // Копия 3 — Oxide.Item.skin (+0x30, int) / skinLevel (+0x34, byte):
        // из неё ItemPickup.KZq (0x6926BDC) берёт скин при дропе, а Gah/Gaz
        // несут его в синк инвентаря и сейв. Триггером пересборки она НЕ
        // является (опровергнуто — см. «ТРЕТЬЯ ТОЧКА ЗАПИСИ» в заголовке):
        // вьюха пересобирается на дроу/спавне (см. «КТО ПЕРЕСОБИРАЕТ»).
        // Нечитаемый Item не роняет тик: SyncVar-копия уже записана, а
        // item_state_ = -1 честно показывает «третья точка не работает».
        int32_t item_skin = 0;
        uint8_t item_level = 0;
        if (io.read(context_.item.object + go_active::ITEM_SKIN, item_skin) &&
            io.read(context_.item.object + go_active::ITEM_SKIN_LEVEL, item_level)) {
            skin_item_.sample_value(context_.item.object + go_active::ITEM_SKIN, item_skin);
            level_item_.sample_value(context_.item.object + go_active::ITEM_SKIN_LEVEL, item_level);
            if (force_rewrite) { skin_item_.invalidate(); level_item_.invalidate(); }
            if (!skin_item_.put(io, (int32_t)want_skin)) { status = Status::Failed; stage_ = 22; return; }
            if (!level_item_.put(io, want_level)) { status = Status::Failed; stage_ = 23; return; }
        }

        // Копия 4 — экземпляр Item из кэша дроу pw+0x120 (см. объявление
        // слотов). Резолв каждый тик: ротация экземпляра — норма (drawSV сам
        // обновляет кэш перед чтением skin), цена — два чтения указателей.
        // Страж — тот же ItemData, что у FP-цепи: в окне перехода кэш хранит
        // Item ПРОШЛОГО ствола, и запись текущего выбора туда испортила бы
        // чужой предмет. Стадии делят с копией 3 — то же поле, другой
        // экземпляр.
        uint64_t ditem = 0, ddata = 0;
        if (pointer(io, context_.weapon.object + go_active::PLAYERWEAPON_DRAW_ITEM, ditem) &&
            ditem >= 0x10000 && ditem != context_.item.object &&
            pointer(io, ditem + go_active::ITEM_DATA, ddata) &&
            ddata == context_.item_data.object) {
            if (draw_item_.object != ditem) {
                Identity probe{};
                draw_item_ = probe.capture(io, ditem, false) ? probe : Identity{};
            }
        } else {
            draw_item_ = {};
        }
        if (draw_item_.object) {
            int32_t draw_skin = 0;
            uint8_t draw_level = 0;
            if (io.read(draw_item_.object + go_active::ITEM_SKIN, draw_skin) &&
                io.read(draw_item_.object + go_active::ITEM_SKIN_LEVEL, draw_level)) {
                skin_item2_.sample_value(draw_item_.object + go_active::ITEM_SKIN, draw_skin);
                level_item2_.sample_value(draw_item_.object + go_active::ITEM_SKIN_LEVEL, draw_level);
                if (force_rewrite) { skin_item2_.invalidate(); level_item2_.invalidate(); }
                if (!skin_item2_.put(io, (int32_t)want_skin)) { status = Status::Failed; stage_ = 22; return; }
                if (!level_item2_.put(io, want_level)) { status = Status::Failed; stage_ = 23; return; }
            }
        }

        // §4.28: пятый источник — инвентарный Item (зонд + ведение). Дроу
        // копирует скин с него в свежий экземпляр, SyncVar и вьюху — одна
        // запись здесь выигрывает все будущие дроу без гонки.
        // §4.34: свежий FP-Item (только что покрашен копией 3) — источник
        // указателя для kJw-инъекции.
        maintain_inventory(want_skin, want_level, context_.item.object);

        // Копия 2 — рабочая (+0xC0): из неё hbr.pMZ подаёт piece в
        // FPManager.Kxi -> CreateFPWeapon на дроу. Пишем только когда голова
        // проходит хотя бы щадящую проверку; SyncVar остаётся опорой для
        // RPC-пути модов phW и OnStartClient. Голова читается отдельно:
        // копия живёт своей жизнью — pMd перезаписывает её целиком, а холстер
        // гасит (Enabled=0, Number=-1). §4.21: погашенная голова — вердикт
        // «отложенный Release» для CreateFPWeapon (Enabled==0 -> UniTask.Delay
        // -> Release(), dump11966 0x68EFA04; устройства v13/v14 провели с ней
        // всю сессию: ручка мертва, ready=0 у всех скинов). Достраиваем её
        // зеркалом из SyncVar (pMd-семантика) и перечитываем голову один раз —
        // байтовые слоты ниже увидят уже дополненную копию.
        Piece local;
        uint8_t local_head[kPieceHead] = {};
        bool local_strict_ok = false;
        bool refill_done = false;
        for (;;) {
            PieceRead local_read = read_piece(io, context_.piece_local, local, local_head);
            local_strict_ok = (local_read == PieceRead::Ok);
            if (local_read == PieceRead::NotAPiece)
                local_read = read_piece(io, context_.piece_local, local, local_head, true);
            if (local_read != PieceRead::Ok) {
                status = Status::Failed;
                stage_ = (local_read == PieceRead::Unreadable) ? 20 : 21;
                // Отклонённую голову — в лог сырыми байтами: «стадия 21» без
                // них не отличает мусор от осмысленных, но неканонических.
                if (local_read == PieceRead::NotAPiece)
                    format_piece_head(local_head, local_head_raw_);
                return;
            }
            if (!refill_done && local.enabled == 0 && piece.enabled == 1) {
                refill_done = true;
                if (!refill_local_head(&stage_)) { status = Status::Failed; return; }
                continue;
            }
            break;
        }
        // Щадящий вариант сработал (Loaded/Number неканонические, Enabled цел) —
        // показать, что именно там лежит; строгая голова диагностикой не шумит.
        if (local_strict_ok) local_head_raw_[0] = '\0';
        else format_piece_head(local_head, local_head_raw_);
        skin_local_.sample_value(context_.piece_local + go_active::WEAPONPIECE_SKIN, local.skin);
        level_local_.sample_value(context_.piece_local + go_active::WEAPONPIECE_SKIN_LEVEL, local.level);
        if (force_rewrite) { skin_local_.invalidate(); level_local_.invalidate(); }
        if (!skin_local_.put(io, want_skin)) { status = Status::Failed; stage_ = 18; return; }
        if (!level_local_.put(io, want_level)) { status = Status::Failed; stage_ = 19; return; }

        // Снимок ДЛЯ МЕНЮ снимается после записи: иначе первый же тик показывал
        // бы значение до применения, и «индекс записался» было не отличить от
        // «индекс не записался» (обе строки выглядели как старый скин).
        // put() подтвердил каждую запись чтением обратно, поэтому локальная
        // копия головы отражает память точно: перечитывать её не нужно. А
        // сравнивать вьюху со значением ДО записи — значит всегда видеть
        // «не устарела» (fp_stale_ навсегда остался бы false).
        piece.skin = want_skin;
        piece.level = want_level;
        read_live_state(&piece);
        // §4.29: краска piece вьюхи — СТРОГО после read_live_state, чтобы
        // детектор «вьюха СТАРАЯ» видел собранное игрой. piece.number здесь —
        // живой Number SyncVar (голова прочитана выше), страж внутри.
        maintain_view(want_skin, want_level, piece.number, force_rewrite);
        // §4.30: сброс Loaded — после краски (в поле уже наш индекс).
        maintain_loaded(piece.number);

        // Пересборки здесь НЕТ и быть не может: вьюху собирает игра на своих
        // событиях (холстер/дроу, RPC насадок/модов, спавн/подбор ствола —
        // см. заголовок файла). Наша работа закончена, когда обе копии держат
        // нужный индекс: ближайший дроу пересоберёт вьюху из рабочей копии.
        // §4.24: зомби-резолв — _currentWeapon пуст, цепочка прошла через кэш
        // uzN (+0x58). Считаем только с piece «в руках» (Enabled=1): при
        // пустых руках узаконенный холстером ноль — не зомби. Счётчик кормит
        // периодические строки skin.log: форма невидима ни детектору панциря
        // (тот живёт в нерезолв-ветке), ни дедупу блока (стадия 0, «применено»).
        if (next.via_cache && piece.enabled == 1) ++cache_ticks_;
        else cache_ticks_ = 0;
        // §4.25: застревание дроу. uzL (FPM+0x68) — копия piece, которую TAIL
        // кладёт в начале каждой дошедшей до него сборки (dump11966
        // 0x68EFD40-44). Number в руках (SyncVar) и Number последней сборки
        // обязаны совпадать уже через цикл холстер/дроу; расхождение на
        // десятки секунд при поднятой голове — дроу не доходит до сборки
        // вовсе. Только наблюдение: сбрасывать здесь нечего (резолв healthy,
        // стадия 0), а безопасная запись в uzL не доказана реверсом.
        int16_t uzl_number = 0;
        if (piece.enabled == 1 && context_.fp.object &&
            io.read(context_.fp.object + go_active::FPMANAGER_UZL +
                        go_active::WEAPONPIECE_NUMBER, uzl_number)) {
            stall_hand_ = piece.number;
            stall_view_ = (int)uzl_number;
            if ((int)uzl_number != (int)piece.number) ++stall_ticks_;
            else stall_ticks_ = 0;
        } else {
            stall_ticks_ = 0;
            stall_hand_ = stall_view_ = -1;
        }
        // §4.22: резолв прошёл всю цепочку (Item и uhq на месте) — вьюха
        // здорова, счётчик наблюдения обнуляется, а сброс снова взводится
        // для следующего эпизода отравления.
        shell_ticks_ = 0;
        stuck_ticks_ = 0;
        shell_peh_null_ = false;
        shell_back_ = -1;
        shell_class_[0] = '\0';
        reset_armed_ = true;
        stage_ = 0;
        status = Status::Active;
    }

private:
    // Список перечитывается не каждый тик: строки читаются посимвольно, а тик
    // оружия идёт раз в 100 мс. 5 тиков — это ~0.5 с, достаточно для меню и
    // незаметно по числу обращений к памяти игры.
    static constexpr int kScanEvery = 5;
    // §4.22: порог наблюдения за панцирем. Здоровая ПЕРВАЯ закачка ассета
    // скина держит ту же сигнатуру (uhq==0, Item==null), пока async-колбэк
    // не позовёт On_Draw, поэтому решение принимается только после долгого
    // наблюдения: 450 тиков × 100 мс = 45 с. Ложное срабатывание на очень
    // медленной закачке не опасно: лишний сброс — просто ещё одна пересборка,
    // а карантин снимается сменой индекса.
    static constexpr int kShellWatchTicks = 450;
    // §4.24: порог быстрого пути — панцирь с погашенным PlayerEventHandler.
    // Отравление доказано структурой (см. fp_build_shell): у FPBow.FDN на нуле
    // NRE, гейт зовёт FDN до очистки _currentWeapon, каждый дроу любого ствола
    // падает. Здоровая сборка держит PEH погашенным только синхронное окно
    // между хранениями (0x68F00B0 -> 0x68F01E4, субмиллисекунды), поэтому
    // 20 тиков × 100 мс = 2 с — с запасом, но в 22 раза быстрее медленного.
    static constexpr int kShellPoisonTicks = 20;
    // §4.26: ритм срезов FP-состояния. Сердцебиение 600 тиков × 100 мс = 60 с;
    // минимальный интервал событийных срезов 20 тиков = 2 с — при быстрой
    // смене стволов (устройство v18: переключения каждые 0.5 с) срезы не
    // топят лог, а при отравлении застывшее состояние видно максимум через
    // минуту. Объём среза ~250 Б: час активной игры ≈ 25–50 КБ, ротация
    // 256 КБ оставляет окну отравления место даже в длинной сессии.
    static constexpr int kDumpHeartbeatTicks = 600;
    static constexpr int kDumpCooldownTicks = 20;
};
}  // namespace skinchanger
