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
//   * РАБОЧАЯ КОПИЯ (+0xC0) — вот что уходит в сборку визуала. Интерфейс
//     экипировки hbr (слоты pMj(int) 18 @0x768B988, pMZ(int,bool) 19
//     @0x7671340) делает `ldp x1,x8,[x0,#0xC0]` и через pFz(WeaponPiece,bool)
//     @0x7671350 зовёт FPManager.Kxi(piece,slot) @0x68EE210 -> CreateFPWeapon
//     (d__68/d__69 @0x68EF274/0x68EF65C) -> PlayerFPWeaponView.XZE/MjH
//     (WeaponPiece,EV,Action). MjH @0x7C8DD7C кладёт ПРОШЛУЮ ПО ЗНАЧЕНИЮ piece
//     в копию вьюхи (+0x28/+0x30) и возвращает ничего не сделав, если bit0
//     (Enabled) переданной piece равен нулю.
//   * SkinVisualHandle (+0xB0) собирается тоже из копии piece: pMd(WeaponPiece)
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
// только Skin/SkinLevel, в обеих копиях.
//
// КТО ПЕРЕСОБИРАЕТ. Чит — внешний процесс (только process_vm_readv/writev,
// ptrace-слот занят собственным потомком, инъекции кода нет — mem_io.h,
// stealth.h), поэтому вызвать функции игры он не может. Пересборку делает игра
// на своих событиях, и устройство (skin.log, сборка 105f082) показало, какие
// из них ДЕЙСТВИТЕЛЬНО работают: RPC насадок/модов (phW читает SyncVar
// ЖИВЬЁМ -> FPManager.Kxi) и спавн/подбор ствола (OnStartClient -> pMd).
// Простой холстер/дроу вьюху НЕ пересобирает: рабочая копия в эти моменты
// держит «пустую» piece {Enabled=0, Number=-1, Skin=-1} — игра гасит её на
// холстере и не восстанавливает на дроу (сырые байты головы видны в skin.log),
// а собранная вьюха переиспользуется. Задача чита — к моменту события держать
// НАШ индекс в SyncVar (и в рабочей копии, пока та живая): запись по
// расхождению + readback каждый тик. На чужом сервере респаун приносит
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
// трогают вовсе. Единственный локальный триггер пересборки без модов —
// СПАВН игрока: PW.OnStartClient (0x767F35C) читает SyncVar +0x110 напрямую
// (ldp) и запускает пересборку. Рецепт поэтому: записать скин -> перезапуск
// игры / повторный вход в мир (автосейв несёт Item.skin и текущий SyncVar).
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
// +0xF0, бета +0x108) хранит piece, ИЗ КОТОРОЙ вьюха построена (+0x28). Пока
// weaponPiece.Skin != fpView.piece.Skin — визуал старый (fp_stale в меню).
// Рядом PlayerWeapon.skinVisualHandle (+0xB0/+0xC0): _model == 0 означает, что
// визуал скина не загружен вовсе.
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

// Массив skinData у предмета короткий (единицы-десятки позиций), а id скинов —
// ascii-идентификаторы ассетов. Оба предела защищают от чтения мусора: длина
// массива больше kMaxSkins или строка длиннее kIdCap означает, что мы смотрим
// не на тот объект, и список остаётся пустым.
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
// relaxed — ослабленная валидация ДЛЯ РАБОЧЕЙ КОПИИ (+0xC0): собственный путь
// дроу игры (pMZ читает piece из +0xC0 и несёт в pFz -> FPManager.Kxi ->
// CreateFPWeapon) смотрит только Enabled, а Loaded/Number в этой копии бывают
// неканоническими. Строгая валидация рабочей копии ставила исправный билд на
// «стадия 21» навсегда (устройство это показало: SyncVar живой, вьюха живая,
// а +0xC0 «не WeaponPiece»). SyncVar-копия остаётся строгой: её пишет игра,
// и мусор там действительно означает чужую раскладку.
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
    bool operator==(const Context& b) const {
        return player == b.player && weapon == b.weapon && fp == b.fp &&
               fp_object == b.fp_object && item == b.item &&
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
    // get_TjU. Рабочая копия (+0x110-0x50): из неё hbr.pMZ/pMj собирают
    // FP-вьюху на дроу, и её же перезаписывает pMd. Обе — цели записи.
    Slot<int16_t> skin_;
    Slot<uint8_t> level_;
    Slot<int16_t> skin_local_;
    Slot<uint8_t> level_local_;
    // Копия 3 — Oxide.Item.skin (+0x30, int32) / skinLevel (+0x34, byte):
    // дроп и сохранение несут наш скин (см. заголовок файла). Семантика та же: снимок ->
    // запись по расхождению -> readback -> возврат оригинала.
    Slot<int32_t> skin_item_;
    Slot<uint8_t> level_item_;

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
        if (!io.read(skins + IL2CPP_ARRAY_LENGTH, length) ||
            length <= 0 || length > kMaxSkins) return fail(12);
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
        // Копия piece, ИЗ КОТОРОЙ FP-вьюха действительно построена: пока её
        // индекс не равен нашему, визуал старый (пересборки ещё не было).
        if (piece && context_.fp_view >= 0x10000) {
            Piece view_piece;
            if (read_piece(io, context_.fp_view + FPWEAPONVIEW_PIECE, view_piece) == PieceRead::Ok)
                fp_stale_ = (view_piece.skin != piece->skin);
        }
    }

    bool restore_all() {
        // Все три точки равноправны: возвращаем оригиналы в каждую.
        bool ok = level_item_.restore(io);
        ok = skin_item_.restore(io) && ok;
        ok = level_local_.restore(io) && ok;
        ok = skin_local_.restore(io) && ok;
        ok = level_.restore(io) && ok;
        ok = skin_.restore(io) && ok;
        return ok;
    }

public:
    viewmodel::Status status = viewmodel::Status::Off;
    explicit Controller(IO& memory) : io(memory) {}
    int stage() const { return stage_; }
    bool has_state() const {
        return skin_.owned || level_.owned || skin_local_.owned || level_local_.owned ||
               skin_item_.owned || level_item_.owned;
    }
    // Одноразовый запрос перезаписать обе копии piece, даже если значения уже
    // совпадают (меню «Переписать обе копии» / one-shot CLI `--rebuild`). Сама
    // пересборка вьюхи — событие игры (RPC насадок/модов, спавн/подбор
    // ствола), внешний процесс её запустить не может; запрос лишь гарантирует,
    // что к событию в обеих копиях лежит наш индекс.
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
        info.item_state = item_state_;
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

        Context next;
        if (!resolve(player, next)) {
            if (!restore_all()) status = Status::Failed;
            return;
        }
        if (!(next == context_)) {
            // Сменился ствол/предмет: старый возвращаем, новый перехватываем.
            // Оригинал индекса принадлежит ПРОШЛОМУ стволу — возвращать его в
            // новый бессмысленно, поэтому при смене контекста владение
            // снимается без записи только если возврат не прошёл.
            if (!restore_all()) {
                skin_ = {}; level_ = {}; skin_local_ = {}; level_local_ = {};
                skin_item_ = {}; level_item_ = {};
            }
            context_ = next;
            scan_age_ = kScanEvery;   // перечитать список сразу
            entry_count_ = 0;
        }

        if (scan_request_ || scan_age_ >= kScanEvery || entry_count_ == 0) rescan();
        else ++scan_age_;
        scan_request_ = false;

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
        // вьюха пересобирается на спавне (OnStartClient читает SyncVar).
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

        // Копия 2 — рабочая (+0xC0): по дампу из неё hbr.pMZ/pMj подают piece
        // в FPManager.Kxi. Устройство уточнило картину: между дроу копия
        // держит «пустую» piece {Enabled=0, Number=-1} (игра гасит её на
        // холстере), поэтому пишем мы её только когда голова проходит хотя бы
        // щадящую проверку, и основную ставку делаем на SyncVar (её читает
        // RPC-путь модов phW и OnStartClient). Голова читается отдельно:
        // копия живёт своей жизнью — pMd перезаписывает её целиком.
        Piece local;
        uint8_t local_head[kPieceHead] = {};
        PieceRead local_read = read_piece(io, context_.piece_local, local, local_head);
        const bool local_strict_ok = (local_read == PieceRead::Ok);
        if (local_read == PieceRead::NotAPiece)
            local_read = read_piece(io, context_.piece_local, local, local_head, true);
        if (local_read != PieceRead::Ok) {
            status = Status::Failed;
            stage_ = (local_read == PieceRead::Unreadable) ? 20 : 21;
            // Отклонённую голову — в лог сырыми байтами: «стадия 21» без них
            // не отличает мусор от осмысленных, но неканонических значений.
            if (local_read == PieceRead::NotAPiece)
                format_piece_head(local_head, local_head_raw_);
            return;
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

        // Пересборки здесь НЕТ и быть не может: вьюху собирает игра на своих
        // событиях (RPC насадок/модов, спавн/подбор ствола — см. заголовок
        // файла). Наша работа закончена, когда обе копии держат нужный индекс.
        stage_ = 0;
        status = Status::Active;
    }

private:
    // Список перечитывается не каждый тик: строки читаются посимвольно, а тик
    // оружия идёт раз в 100 мс. 5 тиков — это ~0.5 с, достаточно для меню и
    // незаметно по числу обращений к памяти игры.
    static constexpr int kScanEvery = 5;
};
}  // namespace skinchanger
