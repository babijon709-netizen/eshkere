// Хост-тест автолога скинченджера (skin_log.h).
//
// Проверяет то, ради чего лог существует: строки появляются сами, по одной на
// изменение состояния, говорят по-русски то же, что меню, и файл не растёт
// вечно. На устройстве это не проверить иначе как глазами, а здесь видны и
// дедупликация, и ротация, и «ничего не пишем, пока тумблер выключен».
#include "skin_log.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// skin_change_stop_reason() живёт в game_viewmodel.cpp (Android-TU). Тесту важен
// сам факт, что расшифровка стадии попала в строку, а не её точный текст.
const char* skin_change_stop_reason(viewmodel::Status status, int stage) {
    (void)status;
    static char buf[32];
    std::snprintf(buf, sizeof(buf), "stage-%d", stage);
    return buf;
}

namespace {

const char* kDir = "/tmp/benzware_skinlog_test";

std::string log_path() { return std::string(kDir) + "/skin.log"; }

bool file_exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

long file_size(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? (long)st.st_size : -1;
}

std::string slurp(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

int count_of(const std::string& hay, const std::string& needle) {
    int hits = 0;
    for (size_t at = hay.find(needle); at != std::string::npos;
         at = hay.find(needle, at + needle.size()))
        ++hits;
    return hits;
}

// Каталог и файл — как на устройстве перед первым включением.
void fresh() {
    ::mkdir(kDir, 0777);
    ::unlink(log_path().c_str());
}

viewmodel::SkinEntryView g_entries[3];

void fill_entries(bool gold_ready) {
    g_entries[0].index = 0;
    g_entries[0].kind = viewmodel::SkinKind::WeaponObject;
    g_entries[0].levels = 1;
    g_entries[0].ready = true;
    std::snprintf(g_entries[0].id, sizeof(g_entries[0].id), "dmr_default");
    g_entries[1].index = 1;
    g_entries[1].kind = viewmodel::SkinKind::WeaponProgression;
    g_entries[1].levels = 3;
    g_entries[1].ready = gold_ready;
    std::snprintf(g_entries[1].id, sizeof(g_entries[1].id), "dmr_gold");
    g_entries[2].index = 2;
    g_entries[2].kind = viewmodel::SkinKind::WeaponRecolor;
    g_entries[2].levels = 1;
    g_entries[2].ready = false;
    std::snprintf(g_entries[2].id, sizeof(g_entries[2].id), "dmr_red");
}

// Типичный снимок «всё получилось»: ствол прочитан, индекс записан, вьюха
// собрана, ассет жив. Отдельные тесты портят ровно одно поле.
skinlog::Snapshot make_snap(double now) {
    fill_entries(true);
    skinlog::Snapshot s;
    s.session = 1;
    s.enabled = true;
    s.bound = true;
    s.entries = g_entries;
    s.count = 3;
    s.want_index = 1;
    s.profile = "release";
    s.dir = kDir;
    s.now = now;
    s.ui.count = 3;
    s.ui.skin = 1;
    s.ui.level = 0;
    s.ui.stage = 0;
    s.ui.status = viewmodel::Status::Active;
    s.ui.piece_read = true;             // Skin/SkinLevel действительно прочитаны
    s.ui.fp_stale = false;              // вьюха собрана из нашего индекса
    s.ui.handle_live = true;            // ассет загружен
    s.ui.local_state = 0;               // рабочая копия piece держит наш индекс
    std::snprintf(s.ui.weapon, sizeof(s.ui.weapon), "dmr");
    return s;
}

void test_disabled_writes_nothing() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot s = make_snap(0.0);
    s.enabled = false;
    for (int i = 0; i < 20; ++i) w.note(s);
    assert(!file_exists(log_path()));   // файла нет вовсе, а не «пустой»
    assert(w.lines_written() == 0);
    std::printf("  disabled: no file, no lines\n");
}

void test_unbound_writes_nothing() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot s = make_snap(0.0);
    s.bound = false;                    // игра не найдена / процесс умер
    for (int i = 0; i < 20; ++i) w.note(s);
    assert(!file_exists(log_path()));
    std::printf("  unbound: no file\n");
}

void test_enable_reports_state_once() {
    fresh();
    skinlog::Writer w;
    const skinlog::Snapshot s = make_snap(0.0);
    w.note(s);
    const std::string text = slurp(log_path());
    assert(count_of(text, "== skin.log: профиль release, сессия 1 ==") == 1);
    assert(count_of(text, "ствол dmr: скинов 3") == 1);
    assert(count_of(text, "в памяти игры Skin=1 SkinLevel=0") == 1);
    assert(count_of(text, "применено (обе копии piece)") == 1);
    assert(count_of(text, "вьюха собрана из нашего индекса") == 1);
    assert(count_of(text, "рабочая копия piece: наш индекс") == 1);
    assert(count_of(text, "визуал скина загружен") == 1);
    assert(count_of(text, "выбран #1 dmr_gold: progression, уровней 3, ассет запрашивался") == 1);
    assert(text.compare(0, 6, "+0.0s ") == 0);   // время — от первого открытия
    assert(count_of(text, "!!") == 0);           // порядок — никаких тревог

    // Тот же снимок 100 раз подряд: ни одной новой строки. Это и есть
    // «лог не мешает» — в установившемся режиме он молчит.
    const int before = w.lines_written();
    for (int i = 1; i <= 100; ++i) w.note(make_snap(0.1 * i));
    assert(w.lines_written() == before);
    assert(slurp(log_path()) == text);
    std::printf("  enabled: full state once, then silence (%d lines)\n", before);
}

void test_off_and_lost_bind_are_different() {
    fresh();
    skinlog::Writer w;
    w.note(make_snap(0.0));

    skinlog::Snapshot off = make_snap(1.0);
    off.enabled = false;
    w.note(off);
    for (int i = 0; i < 10; ++i) w.note(make_snap(1.1 + i));  // выключено: молчим
    std::string text = slurp(log_path());
    assert(count_of(text, "скинченджер выключен — оригинал возвращён") == 1);
    assert(count_of(text, "связь с игрой потеряна") == 0);

    // Включаем обратно и рвём связь: формулировка другая, потому что при
    // потере процесса оригиналы намеренно НЕ снимаются (reset(false)).
    w.note(make_snap(2.0));
    skinlog::Snapshot lost = make_snap(3.0);
    lost.bound = false;
    w.note(lost);
    w.note(lost);
    text = slurp(log_path());
    assert(count_of(text, "связь с игрой потеряна — записанное осталось в памяти игры") == 1);
    std::printf("  off vs lost-bind: distinct single lines\n");
}

void test_unreadable_piece_values() {
    fresh();
    skinlog::Writer w;
    // Голова WeaponPiece не прочиталась: значений нет, и лог обязан это сказать
    // словами, а не напечатать Skin=-1 (минус один — легитимное «скина нет»).
    skinlog::Snapshot s = make_snap(0.0);
    s.ui.piece_read = false;
    s.ui.skin = -1;
    s.ui.level = -1;
    s.ui.stage = 14;
    s.ui.status = viewmodel::Status::Failed;
    w.note(s);
    std::string text = slurp(log_path());
    assert(count_of(text, "в памяти игры Skin/SkinLevel не прочитались") == 1);
    assert(count_of(text, "Skin=-1") == 0);
    assert(count_of(text, "!! WeaponPiece не читается (стадия 14: stage-14)") == 1);

    // Прочиталось, но это не WeaponPiece: показываем сырое значение — мусорный
    // Skin видно глазами, и это отличает оффсеты не от билда от мёртвого
    // транспорта.
    skinlog::Snapshot bad = make_snap(1.0);
    bad.ui.piece_read = true;
    bad.ui.skin = 1234;
    bad.ui.level = 90;
    bad.ui.stage = 15;
    w.note(bad);
    text = slurp(log_path());
    assert(count_of(text, "в памяти игры Skin=1234 SkinLevel=90") == 1);
    assert(count_of(text, "!! Оффсеты WeaponPiece не от этого билда (стадия 15: stage-15)") == 1);

    // Отрицательный Skin у ствола без скина — нормальное значение, а не ошибка:
    // строка про него пишется, тревоги нет.
    skinlog::Snapshot none = make_snap(2.0);
    none.ui.piece_read = true;
    none.ui.skin = -1;
    none.ui.level = 0;
    none.ui.stage = 0;
    w.note(none);
    text = slurp(log_path());
    assert(count_of(text, "в памяти игры Skin=-1 SkinLevel=0") == 1);
    std::printf("  piece values: unknown vs garbage vs legitimate -1\n");
}

void test_block_cause_is_reported() {
    fresh();
    skinlog::Writer w;
    // Стадия 13 = индекса нет в массиве этого предмета.
    skinlog::Snapshot bad = make_snap(0.0);
    bad.ui.stage = 13;
    bad.ui.status = viewmodel::Status::Failed;
    bad.want_index = 7;                 // такого индекса в списке нет
    w.note(bad);
    const std::string text = slurp(log_path());
    // Формулировка — дословно skin_block_text(BadIndex), та же, что в меню.
    assert(count_of(text, "!! Индекс не подходит стволу (стадия 13: stage-13)") == 1);
    assert(count_of(text, "применено") == 0);
    assert(count_of(text, "выбран #7") == 0);   // нет в списке — не выдумываем

    // Стадия 6 = в руках пусто; причина меняется — строка меняется.
    skinlog::Snapshot empty = make_snap(1.0);
    empty.ui.stage = 6;
    empty.ui.count = 0;
    empty.ui.weapon[0] = '\0';
    empty.ui.status = viewmodel::Status::Waiting;
    w.note(empty);
    const std::string text2 = slurp(log_path());
    assert(count_of(text2, "!! В руках пусто (стадия 6: stage-6)") == 1);
    assert(count_of(text2, "Индекс не подходит стволу") == 1);  // старая строка на месте
    std::printf("  stage -> cause: BadIndex, EmptyHands\n");
}

void test_stale_view_and_asset_warnings() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot stale = make_snap(0.0);
    stale.ui.fp_stale = true;
    stale.ui.handle_live = false;
    w.note(stale);
    std::string text = slurp(log_path());
    assert(count_of(text, "вьюха СТАРАЯ: собрана из другого индекса — пересоберётся на событие игры: смена насадки/мода, подбор ствола, респаун") == 1);
    assert(count_of(text, "!! визуал скина НЕ загружен: игра докачает ассет сама при сборке вьюхи") == 1);

    // Игрок убрал и достал ствол: обе строки меняются, и это видно без CLI.
    w.note(make_snap(1.0));
    text = slurp(log_path());
    assert(count_of(text, "вьюха собрана из нашего индекса — картинка должна смениться") == 1);
    assert(count_of(text, "визуал скина загружен") == 1);

    // Ассет выбранного скина не запрашивался — главная причина «не применилось».
    skinlog::Snapshot no_asset = make_snap(2.0);
    fill_entries(false);
    no_asset.entries = g_entries;
    no_asset.ui.handle_live = false;
    w.note(no_asset);
    text = slurp(log_path());
    assert(count_of(text, "выбран #1 dmr_gold: progression, уровней 3, ассет НЕ запрашивался") == 1);
    assert(count_of(text, "!! ассет ещё не запрашивался: нужна одна сборка вьюхи (смена насадки/мода, подбор, респаун) — закачает игра сама") == 1);

    // Перекраска подписана в самой строке выбора: на устройстве раз за разом
    // выбирают recolor и ждут смены модели.
    skinlog::Snapshot rec = make_snap(3.0);
    rec.want_index = 2;                  // dmr_red — WeaponRecolor
    w.note(rec);
    text = slurp(log_path());
    assert(count_of(text, "выбран #2 dmr_red: recolor, уровней 1, ассет НЕ запрашивался — перекраска (материал, не модель)") == 1);
    // Перекраске строка про закачку не пишется: одна осталась от progression.
    assert(count_of(text, "ассет ещё не запрашивался") == 1);
    std::printf("  stale view, handle, missing asset: warned once each; recolor labeled, not nagged\n");
}

// Чередование стадий ВНУТРИ одной причины не должно спамить: на загрузке игры
// «нет локального игрока» прыгает между стадиями 1 и 2 каждый тик, и дедуп по
// стадии превращал лог в простыню из 577 одинаковых строк.
void test_same_cause_is_one_line() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot a = make_snap(0.0);
    a.ui.stage = 1;
    a.ui.status = viewmodel::Status::Waiting;
    w.note(a);
    skinlog::Snapshot b = a; b.now = 0.1; b.ui.stage = 2;
    w.note(b);
    skinlog::Snapshot c = b; c.now = 0.2; c.ui.stage = 1;
    w.note(c);
    std::string text = slurp(log_path());
    assert(count_of(text, "Нет локального игрока") == 1);
    assert(count_of(text, "стадия 1:") == 1 && count_of(text, "стадия 2:") == 0);

    // Успех виден и при статусе Waiting: индекс подтверждён чтением обеих
    // копий, стадия 0 — строка «применено» обязана появиться.
    skinlog::Snapshot ok = make_snap(0.3);
    ok.ui.status = viewmodel::Status::Waiting;
    w.note(ok);
    text = slurp(log_path());
    assert(count_of(text, "применено") == 1);
    std::printf("  same cause: 1 line; success visible while waiting\n");
}

// Рабочая копия piece (+0xC0) — та, из которой игра соберёт вьюху на дроу.
// Её состояние обязано быть в логе: «игра перезаписала» объясняет, почему
// скин откатился, а молчание выглядело бы как «записали и потеряли».
void test_local_copy_line() {
    fresh();
    skinlog::Writer w;
    w.note(make_snap(0.0));
    skinlog::Snapshot overwritten = make_snap(0.1);
    overwritten.ui.local_state = 1;      // игра (RPC/спавн) перезаписала копию
    w.note(overwritten);
    w.note(overwritten);                 // без перемен — одна строка
    w.note(make_snap(0.5));              // тик восстановил наш индекс
    const std::string text = slurp(log_path());
    assert(count_of(text, "рабочая копия piece: наш индекс") == 2);
    assert(count_of(text, "рабочая копия piece: ОТЛИЧАЕТСЯ (игра перезаписала — восстанавливаем)") == 1);
    std::printf("  local copy: ours/overwritten/restored, one line per change\n");
}

void test_weapon_switch_and_session_change() {
    fresh();
    skinlog::Writer w;
    w.note(make_snap(0.0));
    skinlog::Snapshot other = make_snap(1.0);
    std::snprintf(other.ui.weapon, sizeof(other.ui.weapon), "ak47");
    other.ui.count = 2;
    w.note(other);
    std::string text = slurp(log_path());
    assert(count_of(text, "ствол dmr: скинов 3") == 1);
    assert(count_of(text, "ствол ak47: скинов 2") == 1);
    assert(count_of(text, "== skin.log") == 1);

    // Новый процесс игры: прошлые строки описывали другую жизнь, поэтому
    // шапка пишется заново, а состояние сообщается с нуля.
    skinlog::Snapshot next = make_snap(2.0);
    next.session = 2;
    w.note(next);
    text = slurp(log_path());
    assert(count_of(text, "== skin.log: профиль release, сессия 2 ==") == 1);
    assert(count_of(text, "== skin.log") == 2);
    assert(count_of(text, "ствол dmr: скинов 3") == 2);   // сообщили заново
    std::printf("  weapon switch + session change: re-announced\n");
}

void test_rotation_is_bounded() {
    fresh();
    skinlog::Writer w;
    // Гоним разные имена ствола, пока файл не упрётся в предел: каждая смена —
    // одна строка, то есть управляемый способ вырастить лог.
    for (int i = 0; i < 6000; ++i) {
        skinlog::Snapshot s = make_snap(0.1 * i);
        std::snprintf(s.ui.weapon, sizeof(s.ui.weapon), "w%05d", i);
        w.note(s);
    }
    const long size = file_size(log_path());
    assert(size > 0);
    assert(size < skinlog::kMaxBytes + 256);   // предел соблюдён с запасом на строку
    const std::string text = slurp(log_path());
    // Ротация — это «начать файл заново», поэтому довоенная история исчезает,
    // а шапка остаётся ровно одна: та, что написана после обрезки.
    assert(count_of(text, "== skin.log") == 1);
    assert(count_of(text, "ствол w00000") == 0);
    assert(count_of(text, "ствол w05999") == 1);
    // После ротации состояние сообщается заново: старый контекст из файла
    // исчез, и читать его продолжение без шапки было бы бессмысленно.
    assert(count_of(text, "в памяти игры Skin=1 SkinLevel=0") == 1);
    assert(count_of(text, "применено") == 1);
    assert(count_of(text, "выбран #1 dmr_gold") == 1);
    std::printf("  rotation: %ld bytes (< %ld), re-announced and continues\n",
                size, (long)skinlog::kMaxBytes);
}

void test_reset_forgets() {
    fresh();
    skinlog::Writer w;
    w.note(make_snap(0.0));
    w.reset();
    w.note(make_snap(1.0));                // та же сессия, но снимок забыт
    const std::string text = slurp(log_path());
    assert(count_of(text, "== skin.log") == 2);
    assert(count_of(text, "ствол dmr: скинов 3") == 2);
    std::printf("  reset: state re-announced\n");
}

void test_unidentified_kind_no_asset_claim() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot s = make_snap(0.0);
    // Игра новее дампа: ни слот TypeInfo, ни имя класса тип не дали. ready
    // при нераспознанном типе не измеряется вовсе (asset_ready нечего
    // читать), и лог обязан сказать «не проверить», а не обвинить устройство.
    g_entries[1].kind = viewmodel::SkinKind::Other;
    g_entries[1].ready = false;
    w.note(s);
    std::string text = slurp(log_path());
    assert(count_of(text, "выбран #1 dmr_gold: other, уровней 3, ассет не проверить (тип не распознан)") == 1);
    assert(count_of(text, "!! тип скина не распознан") == 1);
    assert(count_of(text, "ассет ещё не запрашивался") == 0);   // отсутствие не выдумано

    // Перечитывание списка тип распознало (резерв по имени сработал), и ассет
    // измерен: состояние выбора изменилось — строка пишется заново.
    g_entries[1].kind = viewmodel::SkinKind::WeaponProgression;
    g_entries[1].ready = true;
    s.now = 0.5;
    w.note(s);
    text = slurp(log_path());
    assert(count_of(text, "выбран #1 dmr_gold: progression, уровней 3, ассет запрашивался") == 1);
    assert(count_of(text, "!! тип скина не распознан") == 1);   // без повтора тревоги
    std::printf("  unidentified kind: honest 'cannot measure', re-report when identified\n");
}

// Сырая голова рабочей копии: строка на смену содержимого, только когда копия
// неканоническая. Без неё «стадия 21» в логе — обвинение без доказательства.
void test_local_head_line() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot s = make_snap(0.0);
    std::snprintf(s.ui.local_head, sizeof(s.ui.local_head), "01 7a 52 00 ff ff 7f 00");
    w.note(s);
    std::string text = slurp(log_path());
    assert(count_of(text, "голова рабочей копии piece (сырая): 01 7a 52 00 ff ff 7f 00") == 1);
    // То же содержимое 50 тиков — одна строка.
    for (int i = 0; i < 50; ++i) { s.now = 0.1 + 0.1 * i; w.note(s); }
    text = slurp(log_path());
    assert(count_of(text, "голова рабочей копии") == 1);
    // Содержимое сменилось (другой ствол) — строка снова одна, новая.
    std::snprintf(s.ui.local_head, sizeof(s.ui.local_head), "01 22 33 44 55 66 77 88");
    s.now = 6.0;
    w.note(s);
    text = slurp(log_path());
    assert(count_of(text, "голова рабочей копии") == 2);
    assert(count_of(text, "01 22 33 44 55 66 77 88") == 1);
    // Каноническая голова (поле пусто) — строки нет вовсе.
    w.note(make_snap(7.0));
    text = slurp(log_path());
    assert(count_of(text, "голова рабочей копии") == 2);
    std::printf("  local head hex: one line per content change, silent when canonical\n");
}

// «вьюха собрана из нашего индекса» при НЕнулевой стадии — враньё: записи не
// было, совпал старый индекс. Строка обязана дожидаться применения.
void test_view_line_gated_by_stage() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot bad = make_snap(0.0);
    bad.ui.stage = 21;
    bad.ui.status = viewmodel::Status::Failed;
    bad.ui.fp_stale = false;             // вьюха «совпадает» — но писать-то нечего
    w.note(bad);
    std::string text = slurp(log_path());
    assert(count_of(text, "вьюха собрана из нашего индекса") == 0);
    // Применение состоялось — строка дописана, и ровно одна.
    w.note(make_snap(1.0));
    text = slurp(log_path());
    assert(count_of(text, "вьюха собрана из нашего индекса") == 1);
    // Старая вьюха сообщается на любой стадии: это факт о вьюхе, а не об успехе.
    skinlog::Snapshot stale = make_snap(2.0);
    stale.ui.stage = 13;
    stale.ui.status = viewmodel::Status::Failed;
    stale.ui.fp_stale = true;
    w.note(stale);
    text = slurp(log_path());
    assert(count_of(text, "вьюха СТАРАЯ") == 1);
    std::printf("  'view built' line waits for stage 0; 'stale' line reports anyway\n");
}

// Штамп сборки в шапке: лог с устройства должен однозначно говорить, какая
// сборка его писала (устройство дважды прошивалось устаревшим артефактом).
void test_build_stamp_in_header() {
    fresh();
    skinlog::Writer w;
    skinlog::Snapshot s = make_snap(0.0);
    s.build = "c75ec59 2026-10-22T08:52:02Z";
    w.note(s);
    const std::string text = slurp(log_path());
    assert(count_of(text, "== skin.log: профиль release, сессия 1, сборка c75ec59 2026-10-22T08:52:02Z ==") == 1);
    // Без штампа (хост/старые снимки) шапка остаётся прежней.
    fresh();
    skinlog::Writer w2;
    w2.note(make_snap(0.0));
    assert(count_of(slurp(log_path()), "== skin.log: профиль release, сессия 1 ==") == 1);
    std::printf("  build stamp: in header when known, absent when not\n");
}

void test_unwritable_dir_is_survivable() {
    skinlog::Writer w;
    skinlog::Snapshot s = make_snap(0.0);
    s.dir = "/proc/definitely-not-writable";   // каталог недоступен
    for (int i = 0; i < 50; ++i) w.note(s);
    assert(w.lines_written() == 0);            // не долбим fopen каждый тик
    assert(!file_exists("/proc/definitely-not-writable/skin.log"));
    std::printf("  unwritable dir: no lines, no retry storm\n");
}

}  // namespace

int main() {
    test_disabled_writes_nothing();
    test_unbound_writes_nothing();
    test_enable_reports_state_once();
    test_off_and_lost_bind_are_different();
    test_unreadable_piece_values();
    test_block_cause_is_reported();
    test_stale_view_and_asset_warnings();
    test_same_cause_is_one_line();
    test_local_copy_line();
    test_weapon_switch_and_session_change();
    test_rotation_is_bounded();
    test_reset_forgets();
    test_unidentified_kind_no_asset_claim();
    test_local_head_line();
    test_view_line_gated_by_stage();
    test_build_stamp_in_header();
    test_unwritable_dir_is_survivable();
    ::unlink(log_path().c_str());
    std::printf("OK\n");
    return 0;
}
