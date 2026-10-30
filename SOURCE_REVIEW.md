# Разбор исходников eshkere-magic

## Статус

Распакованная папка `jni/` добавлена в репозиторий и запушена в ветку
`arena/18190031-eshkere`.

- Исходный коммит публикации: `58e6f411724b74978afa25a69d96eaa0daa8e7d2`.
- Архив: `eshkere-magic-source-32c5edf.zip`.
- SHA-256 архива: `1ccacd883554d8529b5efce401440c26a3052b2ad0e4b46394cf9576da3be451`.
- Состав: 194 файла, 13 318 532 байта.
- Все 194 файла в `origin/arena/18190031-eshkere:jni/` побайтово совпадают с архивом.
- Логика чита внутри `jni/` не менялась. Исправления сборки и хост-тестов вынесены в `build.sh`, `.gitignore` и `tools/tests/`.

Имя архива содержит `32c5edf`, но этот идентификатор не подтверждён как коммит данного репозитория.
Ближайшая найденная историческая база — `fba9047`: относительно неё в архиве 14 новых файлов,
15 изменённых, 165 идентичных, удалений нет. Основная дельта — Magic Farm, process hardening,
scoped strings и стабилизация `BW_OFFSET`.

## Границы разбора

Инвентаризированы и хешированы все 194 файла. Разобрана архитектура и критические пути
пользовательского кода: запуск, транспорты памяти, привязка к игре, ESP/aim/farm, Magic Farm,
touch/input, захват экрана, конфиги, обфускация и тесты.

Это не построчный security-аудит вендорных библиотек. ImGui, stb_image, TJpgDec, шрифты и
медиа-массивы проверены как зависимости/ресурсы: версии, лицензии, потребители, целостность.
Строки вендора и сгенерированных медиа не выдаются за полностью прочитанный пользовательский код.

Инвентаризация:

| Группа | Файлов | Строк | Назначение |
|---|---:|---:|---|
| Пользовательский код и сборка | 124 | 46 365 | runtime, game logic, UI, input, capture, config, obfuscation |
| Вендор | 21 | 75 131 | ImGui, stb_image, TJpgDec |
| Медиа/шрифты | 6 | 57 033 | PNG/WAV/atlas/video/font arrays |
| Offset-профили | 3 | 3 172 | Release/Beta/active selector |
| JNI-тесты | 40 | 5 123 | host unit tests |

## Цель и версии

Целевой пакет: `com.catsbit.oxidesurvivalisland`.

Основной профиль — Release build `11991`. Beta-профиль в заголовках ориентирован на `11982`.
Magic Farm поддерживает только Release `11991`; для Beta он должен останавливаться с
`WrongBuild`, а не пытаться использовать чужие RVA.

Смещения и RVA привязаны к конкретному дампу. Перед переносом на другой билд нужны
`dump.cs`, `script.json`, `il2cpp.h`, `libil2cpp.so` и проверка через `tools/offsets/`.
В архиве нет самих дампов игры.

## Архитектура запуска

Ключевые файлы:

- `jni/src/main.cpp` — CLI, запуск, основной цикл, shutdown.
- `jni/src/process.cpp` — поиск/привязка к процессу игры.
- `jni/src/mode_select.h` — выбор Release/Beta и транспорта памяти.
- `jni/src/stealth.h` — маскировка процесса, ранний и runtime hardening.
- `jni/include/process_hardening.h` — `RLIMIT_CORE=0`, `PR_SET_DUMPABLE=0`.
- `jni/include/startup_key.h` — стартовый gate.

Порядок:

1. Process hardening.
2. Private CLI-режимы.
3. Стартовый prompt.
4. Демонизация/маскировка.
5. Оверлей и touch.
6. Выбор Release/Beta.
7. Выбор external/DitPro/FT/RT-Hook/RT-Dev.
8. Привязка к игре и загрузка конфига.
9. Кадровый цикл.
10. Остановка фоновых писателей, восстановление состояния, detach, cleanup.

Стартовый prompt не является границей безопасности: ключ лежит в исходниках/бинаре.
`PR_SET_DUMPABLE=0` ограничивает обычный доступ, но не защищает от root, `CAP_SYS_PTRACE`
или(kernel-драйвера.

В `stealth.h` есть устаревшие комментарии про дочерний ptrace-guard. Текущая реализация
`ApplyTracerGuard()` только игнорирует `SIGCHLD` и ставит `dumpable=0`; отдельного
tracer-потомка нет.

## Память и транспорты

Ключевые файлы:

- `jni/src/mem_io.h` — `memio::Reader`, кэш, external transport.
- `jni/src/kernel_io.h` — FT/DitPro/RT-Dev/RT-Hook ABI.
- `jni/src/maps_lookup.h` — базы библиотек по `/proc/<pid>/maps`.
- `jni/src/game.cpp` — bind/reset, чтение игровых объектов, W2S.

External-путь использует прямые `process_vm_readv`/`process_vm_writev` через syscall.
Это не `/proc/<pid>/mem` pread/pwrite. Кэш — thread-local блоки, а не один большой
batched iovec на весь кадр.

Kernel-пути завязаны на конкретные драйверы и их ABI. Код содержитпробинг/классификацию
команд, self-test и переключение протоколов. Без соответствующего драйвера на устройстве
эти режимы не проверить.

W2S рассчитан на Unity column-major matrices, камера смотрит в `-Z`, используется
положительный clip `w`, экранная ось Y инвертируется. Aim считается от firing reference,
а не только от визуальной камеры, потому что sway/kick могут отличаться от оси выстрела.

## ESP, aim, farm

Ключевые файлы:

- `jni/src/game.cpp`, `game_markers.cpp`, `game_farm.cpp`, `game_viewmodel.cpp`.
- `jni/src/esp_draw.cpp`, `hud.cpp`, `vis_check.h`.
- `jni/src/aim.cpp`, `silent_aim.cpp`, `aim_*.h`, `target_predictor.h`.
- `jni/src/farm.cpp`, `farm_joy.cpp`, `farm_control_core.h`, `farm_joy_core.h`.
- `jni/src/*_memory.h` — точечные патчи оружия/рук/FPS/лута.

ESP строит боксы/скелеты/маркеры по игровым объектам и кэшированным трансформам.
Есть фильтры видимости, удержание боксов, предсказание движения, watchdog на потерю
локального игрока и сброс кэшей при смене мира/процесса.

Aim имеет три режима: touch, memory, silent. Touch использует синтетический uinput-палец
в слоте 9. Silent пишет ось выстрела через выбранный транспорт из фонового writer.
Обычный farm двигается и атакует через игровую память/джойстик, отдельно от Magic Farm.

Деревья в legacy farm целятся не просто в центр объекта: используется игровая ray-проверка,
проекция маркера на кору, геометрия расширения и fallback. Постороннее попадание луча
подавляет удар.

## Magic Farm

Ключевые файлы:

- `jni/include/magic_farm_protocol.h`
- `jni/src/magic_farm.cpp`
- `jni/src/magic_farm_loader.cpp`
- `jni/src/magic_farm_module.cpp`
- `jni/src/magic_farm_profile.h`
- `jni/src/magic_farm_hook.h`

Это не чисто external-фича. Внешний процесс загружает companion-модуль внутрь игры:

1. Находит `UnityMain`.
2. Делает ptrace seize/interrupt.
3. Сохраняет регистры.
4. Выделяет remote scratch.
5. Создаёт `memfd` и пишет туда ELF companion.
6. Через remote `android_dlopen_ext` загружает модуль.
7. Вызывает `benzware_magic_attach`.
8. Модуль проверяет fingerprint Release 11991.
9. Ставит trampoline на `FPMelee.FixedUpdate` и vtable-hook acknowledgements.
10. Внешний клиент публикует команды и читает состояние через shared-протокол.

Протокол:

- `Command` — 32 байта.
- Lease команды — 200 мс.
- Shared-состояние защищено sequence/seqlock-подобной публикацией.
- Проверяются magic, ABI, size, deadline, net id.

Внутриигровой tick проверяет инструмент, purpose, reach, period, raycast, collider и
принадлежность попадания целевому узлу. После этого вызывает normal client-side hit
processing и существующую сетевую команду `SendHit`. Камера/углы не пишутся ради удара.

### Риски Magic Farm

1. `Stop()` очищает команду, но не выгружает модуль и не снимает hooks. Resident-модуль
   остаётся в процессе игры до её перезапуска.
2. Ошибки remote-call/cleanup могут оставить scratch, fd или загруженную библиотеку.
3. Неверный билд, stale pointer или сбой hook может уронить игру.
4. Beta не поддерживается.
5. Сохранённый конфиг может восстановить `farm_magic` и `farm_on`; при следующем запуске
   фича способна продолжить работу без нового ручного включения.
6. Fingerprint проверяет 16 байт на метод, но это не полная гарантия совместимости всего
   игрового объекта/графа.

Практически: перед выключением Magic Farm сначала снимать галочки в меню, а для полного
удаления hooks перезапускать игру.

## Touch и input

Ключевые файлы:

- `jni/src/Android_touch/TouchHelperA.cpp`
- `jni/include/Android_touch/TouchHelperA.h`
- `jni/src/touch_device_filter.h`

Код читает физические input-устройства, фильтрует не-тачскрины, поддерживает type-A и
multitouch, создаёт uinput-устройство и синтезирует пальцы. Aim использует слот 9,
дополнительные farm-пальцы — слоты 6–8. Есть emergency release и учёт статистики отправки.

Без root/uinput инъекция не работает; read-only путь не является полноценным no-root
fallback для аима/фарма.

## Захват экрана

Ключевые файлы:

- `jni/src/screen_capture.cpp`
- `jni/src/record_capture.cpp`
- `jni/src/record_display.cpp`
- `jni/src/record_mp4.cpp`
- `jni/src/capture_merge.cpp`

Запись вынесена в изолированные fresh-exec helper-процессы под supervisor. Используются
приватные Android display API, MediaNDK, безопасное резервирование output inode,
проверка фрагментов и recoverable merge. Это хрупкая ABI-зона: разные Android-версии
требуют разных layout/symbol путей.

## Конфиги

Ключевые файлы:

- `jni/src/config.cpp`
- `jni/include/config.h`
- `jni/include/app_state.h`

Конфиги бинарные, с XOR-маскировкой и фиксированными layout/trailer. Это не шифрование
и не защита от изменения. Базовый blob — 260 байт, дальше compatibility trailers.
`farm_magic` хранится в Ext2 bit 22.

Фактические пути:

- конфиги: `/data/local/benzware/`;
- выбранный backend: `/storage/emulated/0/benzware/.mode`.

В комментариях местами остались старые пути и упоминания перечитывания по `SIGHUP`.
Текущий `main.cpp` игнорирует `SIGHUP`; полноценного reload по сигналу нет.

## FakeLag

`jni/src/FakeLag.cpp` управляет глобальными iptables-правилами для входящего UDP.
Это влияет не только на игру, но и на сетевой стек устройства. Есть cleanup по
deadline/сигналам, но `SIGKILL`/`SIGSTOP` может оставить правило до следующего запуска
или ручного удаления.

Практически: не держать FakeLag включённым на устройстве, где важен остальной UDP-трафик.

## Обфускация

Ключевые файлы:

- `jni/include/bw_obf.h`
- `jni/src/bw_obf.cpp`
- `jni/include/scoped_text.h`
- `tools/obf/elf_crypt.py`

Есть per-build key, шифрование `.rodata`, runtime decrypt, scoped strings, `BW_OFFSET`,
strip и проверка контрольных строк. Это усложняет статический анализ, но не является
криптографической защитой: ключ и алгоритм находятся в бинаре.

## Сборка

Исходный архив не содержал сгенерированный `jni/src/magic_farm_blob.inc`, хотя
`magic_farm_loader.cpp` включает его безусловно. Прямая сборка главного модуля поэтому
была неполной.

Исправление в `build.sh`:

1. Собрать `benzware_magic`.
2. Проверить, что результат — AArch64 ELF64 little-endian.
3. Сгенерировать `jni/src/magic_farm_blob.inc` из точных байтов `.so`.
4. Атомарно заменить include.
5. Собрать `benzware`.
6. Выполнить прежние strip/obfuscation/package стадии.

Сгенерированный blob и build key игнорируются Git. Два явных прохода `ndk-build`
убирают гонку, когда loader компилируется раньше companion при `-j`.

Хост-тесты меню и farm-контроллера теперь линкуют настоящий `jni/src/magic_farm.cpp`,
а платформенные зависимости подменяются только тестовыми doubles. Production-заглушки
не добавлялись.

## Проверка

Локально:

- `tools/tests/run_all_host_tests.sh` — 40/40 PASS.
- `tools/tests/run_menu_ui.sh` — PASS.
- `tools/tests/run_farm_tests.sh` — PASS.
- `tools/tests/run_predict.sh` — PASS.
- `tools/tests/run_screen_capture.sh` — PASS; реальный H.264 локально пропущен из-за отсутствия PyAV 16.0.1.
- `tools/tests/run_capture_surface.sh` — PASS.
- `tools/tests/run_startup_cli.sh` — PASS plain/encrypted.
- `tools/tests/run_obf_selftest.sh` — PASS default/16K.
- `tools/tests/run_touch_device_filter.sh` — PASS.
- `bash -n build.sh tools/tests/*.sh` — PASS.
- `git diff --check` — PASS.

GitHub Actions на коммите `58e6f41`:

- `Build benzware.sh` — SUCCESS.
- NDK: `26.3.11579264`.
- Host checks, Android build, obfuscation и upload — SUCCESS.
- Артефакт `benzware.sh`: SHA-256 `8dbbc13bd1f5f2450652f37316360f111965e4d05ed74076433fa5eded0bec23`, размер 3 012 712 байт.
- `Android capture merge smoke` — SUCCESS.
- `Android MT Manager startup` — SUCCESS.
- `Android continuous recording` — SUCCESS по всем девяти API-версиям, включая Android 26.

Не проверено:

- запуск на реальном rooted-устройстве;
- игра Release 11991/Beta 11982 в рантайме;
- kernel-драйверы FT/DitPro/RT;
- фактическая инъекция Magic Farm в живой процесс;
- полная совместимость приватных display ABI на всех OEM-прошивках.

## Безопасность и зависимости

Скан пользовательского кода не нашёл внешних HTTP(S)-эндпоинтов для exfiltration.
Сетевые сокеты в `kernel_io.h` относятся к ABI локальных kernel-драйверов, а не к
интернет-сервисам. Regex-скан приватных ключей/токенов/userinfo дал ноль совпадений,
но это не исчерпывающий секрет-аудит.

В архиве нет отдельных LICENSE/COPYING/NOTICE файлов. TJpgDec содержит copyright/license
notice в исходнике, Phosphor Icons помечены как MIT в комментарии. Для ImGui и stb_image
лицензионные тексты в составе архива не обнаружены; при распространении их стоит
восстановить из upstream-релизов.

Один незаполненный калибровочный маркер в пользовательском коде:
`jni/src/aim_ballistics.h:44` — `FILL: measure from jump logs (apex time)`.
Это параметр модели предсказания, а не отсутствующий кусок сборки.

## Итог

Папка `jni/` опубликована в исходном виде, сборка companion-модуля восстановлена,
хост-тесты и все четыре Android CI-пайплайна на коммите публикации зелёные. Главные технические риски
исходника — resident Magic Farm hooks без unload, зависимость от точных offset-профилей,
хрупкие kernel/display ABI и глобальное влияние FakeLag на UDP.
