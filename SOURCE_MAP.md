# SOURCE_MAP.md — карта чита по распакованному `jni/`

Ревизия: `magic-purpose-mask-fix-v1` (BUILD_INFO.json), NDK r26d, arm64-v8a, API 24+.
Источник: `eshkere-magic-source.zip`, sha256 `f2e30c354289d0896dc4f101b7bc9da08ff7159f471109d5a178010327e83c9b`.
Распаковано в `jni/` (194 файла, 14 МБ). Копии служебных файлов архива — `docs/magic/`.

Отличие от `SOURCE_REVIEW.md`: тот документ описывает ревизию с **инъекцией**
`magic_farm_*` (ptrace + memfd + dlopen + inline-hook). В этой ревизии инъекции
нет: `magic_farm_loader/module/profile/hook.cpp` отсутствуют, вместо них
`jni/src/magic_tools_memory.h` — правка полей объектов через тот же `g_mem`.
Всё, что в старых документах написано про Appdome fail-closed, тоже устарело
(см. §9).

---

## 1. Состав

| Группа | Файлов | Строк | Где |
|---|---:|---:|---|
| Пользовательский код | 111 `.h` + 78 `.cpp` | 54 477 | `jni/src`, `jni/include` |
| Профили оффсетов | 3 | 3 177 | `game_offsets.h` (release 11991), `game_offsets_beta.h` (beta 11982), `game_offsets_active.h` (селектор) |
| Хост-тесты | 41 | — | `jni/tests/` |
| Вендор | ImGui, stb_image, TJpgDec | — | `jni/src/ImGui`, `jni/include/ImGui`, `third_party/tjpgd` |
| Медиа/шрифты | аватарка (JPEG-кадры), атлас брони, иконки вкладок, шрифт, WAV | — | `jni/src/media`, `jni/include/media`, `include/ImGui/Font` |

Цель: `com.catsbit.oxidesurvivalisland`, Unity **IL2CPP**, релиз **11991**, бета **11982**.
Инъекции, хуков, правок кода игры нет: только чтение памяти и точечные записи в
managed-поля/нативные структуры.

---

## 2. Порядок запуска (`jni/src/main.cpp`, 614 строк)

1. `process_hardening` живёт в `main` ниже по порядку, но первым делом разбираются
   приватные CLI: `--check-driver`, `--scan-driver`, `dump`, `skel`,
   `--capture-record`, `--capture-merge` — все **до** маскировки и GUI.
2. `startup_key::Request()` — `Enter key:` из терминала MT (строка `t.me/benzware`
   в исходнике; это prompt, не защита).
3. `stealth::DaemonizeEarly()` → fork + setsid + уход из cgroup MT + `oom_score_adj=-1000`.
4. `stealth::ApplyTracerGuard()` (SIGCHLD + dumpable=0; отдельного ptrace-потомка нет,
   комментарии в шапке `stealth.h` про него устарели).
5. `stealth::ApplyEarly(argc, argv)` — cmdline «vold …», environ в ноль, `comm=vold`.
6. `process_hardening::Apply()` — `RLIMIT_CORE=0`, `PR_SET_DUMPABLE=0` (проверяет `PR_GET_DUMPABLE`).
7. Игнор SIGHUP/INT/TERM/PIPE/TTIN/TTOU/TSTP + `InstallCrashSignalCleanup()`.
8. Оверлей: `initGUI_draw(surface_side, surface_side, true)` — квадратный Surface
   (`ANativeWindowCreator`, свой EGL), `Blur::Init`, `CfgWatchInit` (inotify),
   `AudioInit`, `Touch_Init` (+ фоновый retry каждые 3 с).
9. `stealth::ApplyRuntime()`.
10. **`modesel::RunVersionSelect()`** (Release/Beta) → **`modesel::RunModeSelect()`**
    (Прямое чтение / DitPro / RT-Dev). Оба до назначения транспорта.
11. `start_attach_thread()` — поиск pid, `esp_init`, `runtime_guard` refresh.
12. `stealth::ProtectProcess(getpid())` + повтор каждые 300 кадров.
13. `ConfigLoadLast()` (`CfgPickNewest` по mtime), `SyncOverlayCaptureMode()`.
14. Кадровый цикл: `esp_mem_frame_begin()` → `esp_viewmodel_tick()` → FakeLag apply →
    `DrawEspOverlay()` → `UpdateAim()` → `UpdateFarm()` → `RenderMenu()` →
    `UpdateFreecam()` → `AimEndFrame()` → `drawEnd()`.
15. Teardown: `ArmShutdownDeadline()` → capture shutdown → `g_fakeLag.destroy()` →
    `Blur::Free` → `shutdown()` → `Touch_Close` → `silent_aim::shutdown` →
    `esp_freecam_set(false)` → `stop_attach_thread` → `esp_viewmodel_reset(true)` →
    `process_detach` → `_exit(0)`.

---

## 3. Доступ к памяти — единственная точка входа

```
фичи → g_mem (memio::Reader, src/mem_io.h) → Transport* (назначается ОДИН раз)
        ├── ProcfsTransport   process_vm_readv/writev сырым syscall, /proc/<pid>/mem не открывается
        ├── KernelTransport   FT: сокет bing/gongchuang, коды 0x300/0x301/0x302, auth 0x300
        ├── RtDevTransport    /dev/[A-GNY]{1,6}, ioctl 0x801/0x802
        ├── RtHookTransport   ioctl(-1, 0x259/0x25a) — хук syscall ioctl в ядре
        └── (DitPro 131)      syscall 18, req 0x28, read 0x100 / write 0x200 / pid 0x300 / base 0x400
```

- `Reader`: кэш 128-байтных блоков, 64 set × 4 way = 256 блоков на поток, TTL 0.05 с,
  сброс на `esp_mem_frame_begin()`. Запись помечает блок «не кэшировать» (`mark_written`).
- `kFailStreak = 8` → проба `probe_readable()` (ELF-заголовок по базе libil2cpp) →
  переоткрытие **тем же** транспортом. Фоллбэка на procfs нет и не должно быть.
- `kAddressTagMask = 0x00FFFFFFFFFFFFFF` — снятие TBI/MTE-тега со всех указателей из игры.
- `Reader::write()` гейтится `runtime_guard::writes_allowed(pid)` (§9) и `valid_user_range`.
- Фоновые потоки (сайлент) ходят только через `esp_silent_read/write` → тот же транспорт.
  Инвариант из `KERNEL.md`: единственные `process_vm_*` в коде — сам `ProcfsTransport`.

---

## 4. Профили оффсетов

- `game_offsets.h` — release **11991** (32402 типа, 22755 TypeInfo-слотов). TypeInfo —
  внутренний `.data`-слот (одно разыменование до `Il2CppClass*`), `.text` file_off = RVA−0x4000,
  `.data` file_off = RVA−0xC000. В шапке — карта ротации обфусцированных имён 11966→11985→11991.
- `game_offsets_beta.h` — beta **11982**; смещения части полей другие
  (`PlayerManager.fpManager` 0x90 release / 0xD0 beta).
- `game_offsets_active.h` — `go_active::` алиасы, каждый через `BW_OFFSET`, выбирается
  `selected_version`. Все фичи читают только `go_active::`.
- Проверки: `tools/offsets/verify_release_11991.py`, `verify_release_11989/11985.py`,
  `verify_beta_11982.py`, `offsets_diff.py`; тесты профиля `test_release_11978_offsets.cpp`,
  `test_beta_11982_offsets.cpp`, `test_world_streaming_offsets.cpp`.
- TypeInfo-слот — одноразовый якорь: обязателен позиционно-независимый резерв по имени
  класса с самого объекта (`class_identity`, побайтовое сравнение с NUL).

---

## 5. Ядро игры — `game.cpp` (12 604 строки), разделы по маркерам

| Строка | Раздел |
|---:|---|
| 57 | доступ к памяти игры (`g_mem`, `esp_silent_*`, backend) |
| 170 | Иксрей — `Camera.nearClip` со снапшотом |
| 225 | «Всегда день» — `TOD_Sky.Cycle.Holder.Hour` |
| 636 | команда/клан |
| 1279 | фрикам (камера отдельно от тела, писатель + snapback) |
| 1809 | что пишет фоновый писатель фрикама |
| 3144 | чувствительность взгляда (`MouseLook.m_Sensitivity`) |
| 3185 | мемори-аим: `MouseLook` +0x4C/+0x50, ось выстрела, гироскоп, инверсия |
| 4694 | Skeleton ESP (22 кости) |
| 5213 | ragdoll-кости без сопоставления имён |
| 6152 | машины и «ghost»-копии |
| 6574 | временной фильтр позиции |
| 6615 | оружие в руках у чужих (третье лицо) |
| 7122 | тайминги Mirror из игры |
| 7165 | `CharacterSpeedConfig` |
| 8292 | ADS-состояние локального игрока |
| 8355 | дальность ближнего боя (`FPMelee.m_MaxReach` + `hitRadius`) |
| 8571 | «пин крестика» — клей hit-streak маркера к точке удара |
| 9329 | пеленги 360° (стрелки/радар) |
| 9782 | **vis_check** — окклюзия через `OcclusionModule` + `PlayerHitTracker` |
| 10618 | наблюдатель мерцаний визуалов (только фиксирует) |
| 10971 | здоровье прямого пути позиции |
| 11112 | кадр ESP: публикация, перепроецирование (`esp_box_reproject`) |
| 11271 | тик кадра игры |
| 12466 | диагностика конвейера костей (`benzware skel`) |

W2S: Unity **column-major**, камера смотрит в −Z, положительный clip `w`, Y экрана инвертируется.
Матрицу вида собирают из живого `Transform` камеры, а не из `cam+0x70` — те кэши ленивые
и протухают (это и был «ESP плывёт за камерой»). Аим считается от firing reference
(`LookDirection`), а не от визуальной камеры: sway/kick расходятся с осью выстрела.

`game_internal.h` — общий срез: `rd_buf/rd/rd_exact/rd_fresh/rd_ptr/wr_buf`, `g_frame_vp`,
`FarmEntity`, `TransformHierarchyLayout`, `MeleeReach`, резолверы камеры/игрока для
`game_markers.cpp` / `game_farm.cpp`.

---

## 6. Фичи → файлы

| Фича | Где |
|---|---|
| ESP/HUD/стрелки/радар | `esp_draw.cpp` (696), `hud.cpp`, `esp_box_hold.h`, `overlay_geometry.h`, `armor_esp.h` |
| Выбор цели, тач/память/сайлент | `aim.cpp` (863), `aim_control.h`, `aim_learn.h`, `aim_lead_learn.h`, `aim_predict.h`, `aim_ballistics.h`, `aim_trace.h`, `bot_lead.h`, `manes_aim_source.h` |
| Сайлент-писатель (свой поток) | `silent_aim.cpp` + `silent_aim_core.h` |
| Проверка видимости | `vis_check.h` (642) |
| Автофарм (тач/джойстик) | `farm.cpp` (1068), `farm_joy.cpp`, `farm_control_core.h`, `farm_joy_core.h`, `game_farm.cpp` |
| Маркеры мира | `game_markers.cpp` (4 500+) |
| Правки оружия/рук/FPS | `game_viewmodel.cpp` + `weapon_tuning_memory.h`, `viewmodel_memory.h`, `fast_fire_memory.h`, `fast_loot_memory.h`, `no_recoil_memory.h`, `no_ballistics_memory.h`, `fps_memory.h`, `object_data_memory.h` |
| Magic Tools / Magic Farm | `magic_tools_memory.h` (194) + `tool_purpose.h`, интеграция в `game_viewmodel.cpp`, `farm.cpp`, `menu.cpp`, `config.cpp` |
| FakeLag | `FakeLag.cpp` — `iptables -I INPUT … -p udp -m length --length 80:65535 -j DROP`, снятие по сигналам/дедлайну |
| Захват экрана | `screen_capture.cpp`, `record_capture.cpp` (fresh-exec helper), `record_display.cpp` (virtual display + MediaCodec), `record_mp4.cpp` (фрагментный MP4), `capture_merge.cpp` |
| Ввод | `Android_touch/TouchHelperA.cpp` (1058) — uinput-палец, identity клонируется с реального тачскрина, слот 9 у аима, 6–8 у фарма |
| UI | `menu.cpp` (2424, 6 вкладок: Меню/Аим/ESP/Разное/Конфиги/Опции; «Меню» скрыта), `widgets.cpp`, `theme.cpp`, `lang.cpp` + `lang_tables.inc`, `VidAvatar.cpp` (tjpgd-аватарка) |
| Конфиги | `config.cpp` (926): `CfgBlob` ровно 260 байт (`static_assert`), XOR 0xA7, трейлеры `CUI1` (UI/layout/backend) и `CFX2` (Ext2, `farm_magic`/магия — биты сверх 24). Путь `/data/local/benzware/*.cfg`, `.mode` → `/storage/emulated/0/benzware/` |
| Стелс/обфускация | `stealth.h`, `process_hardening.h`, `bw_obf.h/.cpp`, `scoped_text.h`, `str.h` (`XS()`), `attach_log.cpp`, `feature_log_gate.h` |

Magic Tools по факту: подтверждённая цепочка `PlayerManager → FPManager → _currentWeapon(FPTool)
→ RaycastManager` (сверка класса, имени, identity, backref), затем `m_MaxReach` (0x130) и
`m_RayLength/m_AimRayLength` (0x38/0x3C) поднимаются до ≥7.5 м и `alwaysCrit` (0x170) := 1;
восстановление только своего значения (`Saved<T>` + `last`), `valid_purposes()` принимает
только `-1` (Everything) или биты 1|2|4, нормализуя первый в 1|2|4 для матчинга.

---

## 7. Инварианты (соблюдать в любом новом коде)

1. Никаких собственных fd/`process_vm_*`/`ptrace`/`/proc/<pid>/mem`: только `g_mem` /
   `esp_silent_*`. `/proc/<pid>/maps` — можно.
2. Транспорт назначается один раз, до первого bind; фоллбэков и ре-детекта протокола нет.
3. Писать — только по расхождению, с чтением свежего значения и допуском там, где игра
   сама пересчитывает поле (0.005° углы, dot ≥ 0.99999988 оси, 0.2 мм кэш позиции руки).
4. Каждый `Saved<T>` помнит `original` и `last`; чужое изменение поля не перезатирать;
   при смене сессии (`epoch`) или базы либы — забыть без записи.
5. Указатели из памяти — только через `memio::untag`/`rd_ptr`.
6. Всё выставленное вне процесса обязано сниматься само (iptables, uinput, снапшоты полей).
7. Новые оффсеты — только через `go_active::` и с проверкой по `tools/offsets/`.
8. Строки UI — через `XS()`/lang-таблицу, не литералами (их шифрует `.rodata`-проход).

---

## 8. Сборка и проверки

```
ANDROID_NDK_HOME=<ndk-r26d> ./build.sh      # ndk-build → libs/arm64-v8a/benzware.sh
```

- `jni/Android.mk`: `-O3 -ffast-math -march=armv8-a+simd`, LTO, `--icf=all`, `--rosegment`,
  `-z max-page-size=16384`, `-fno-jump-tables`, strip-all, `-fno-ident`.
- Ключ сборки генерирует `build.sh` в `jni/include/bw_build_key.h` (в git не попадает).
- `jni/Application.mk`: arm64-v8a, android-24, `c++_static`, `--strip-all`.
- **Не хватает в репозитории:** `tools/obf/elf_crypt.py` — последний шаг корневого
  `build.sh` (шифрование `.rodata` + `--check`). Без него сборка падает на постобработке;
  сам `ndk-build` при этом проходит. Также нет `magic_farm_blob.inc`-пасса — он не нужен,
  инъекции в этой ревизии нет.

Проверено локально (g++ 12, `-std=c++17 -O2 -ffast-math -I src -I include`):
**38 из 41** хост-теста проходят. Падают три — `test_manes_aim_source`,
`test_player_motion_prediction`, `test_target_predictor`: все три ассертят `isfinite`-гварды,
которые `-ffast-math` выбрасывает. Без `-ffast-math` — зелёные. То есть это рассогласование
флагов теста и сборки, а не баг кода; но на устройстве NaN-проверки в aim/фильтрах
**не работают** (сборка идёт с `-ffast-math`).

---

## 9. Что изменилось против старых документов

- **Инъекция Magic Farm удалена.** Нет `magic_farm_*`, нет ptrace/memfd/dlopen, нет
  inline-hook и vtable-CAS. `Magic Tools` = правка полей живого `FPTool`/`RaycastManager`
  через транспорт; `Magic Farm` = тот же контроллер + существующий автофарм.
- **Fail-closed по `libloader.so` снят.** `runtime_guard::writes_allowed()` теперь требует
  только полный снапшот `/proc/<pid>/maps`; наличие Appdome-загрузчика
  (`Policy::Protected`) — диагностика, запись разрешена (`snapshot_allows_writes`).
  `farm.h`: `g_farmProtected` → `g_farmWriteBlocked` (пишется только при Unknown).
- `runtime_guard` переехал в `jni/src/`, `maps_lookup.h` остался.
- Конфиг: биты 24–26 — выбор руды для Magic, 27 — признак наличия (см. `tool_purpose.h`,
  `config.cpp`). Бит 128 (`fast_reload`) — мёртвая функция, исполнителя нет.

---

## 10. Риски, которые видны из кода (без морали)

- Запись в память игры на клиенте с Appdome RASP (**libloader.so**, key-injection и
  click-bot сканер в DEX — см. `LOADER_AUDIT.md`) больше ничем не блокируется, кроме
  полноты maps. Это ровно то, что защита ищет: heap-правки полей живых объектов.
- Бета 11982: Magic Farm/инструменты на неё не рассчитаны (смещения `PlayerManager`
  другие); `PlayerManager.fpManager` 0x90/0xD0.
- FakeLag — глобальное правило на весь входной UDP; `SIGKILL` оставляет DROP до перезапуска.
- Захват экрана — приватные display-API, зависят от прошивки; на устройстве не проверялось
  (`device_tested: false` в `docs/magic/BUILD_INFO.json`).
- `fast_reload` в конфиге сохраняется, но ничего не делает.
