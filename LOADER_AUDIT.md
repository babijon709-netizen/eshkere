# Oxide 1.16.11991 — `libloader.so` audit and fail-closed guard

Date: 2026-10-30. Target: `com.catsbit.oxidesurvivalisland`, release 11991,
arm64-v8a. This is a static audit of the supplied APKs plus a defensive change
to benzware. No game process was executed, no Appdome check was bypassed, and
no device-level undetectability claim is made.

## Inputs and identity

| Artifact | Size | SHA-256 |
|---|---:|---|
| `split_config.arm64_v8a.apk` | 85,899,899 | `51335e42b9aa9e2d93b030fd37d7510a5a6c0e9c7b394490f3b40d40244d7796` |
| `base.apk` | 85,996,969 | `161ad1b8eb0d8cfe22000f0000bb72cb36003d51e4ed0a0884b2923adf08dd9f` |
| `lib/arm64-v8a/libloader.so` | 15,396,432 | `1df708b06a2a17f8a9a7e367270bd5d5582fe48ae2eff87acff916b7d0f5d99b` |
| `lib/arm64-v8a/libil2cpp.so` | 245,163,512 | `0083464d5663cf2a90c1808b4ed999b335abdefd5ec4140f4f28255f1f6723c3` |

`libil2cpp.so` is byte-identical to the already verified release-11991 dump.
`base.apk` manifest: versionName `1.16.11991`, versionCode `11991`.

`libloader.so` is an AArch64 ELF shared object, SONAME `libloader.so`, sole
dynamic export `JNI_OnLoad`, `BIND_NOW`, GNU_STACK RW. Clear marker:
`LIBLOADER_VERSION=6.17.12156.79926`. Sections include `.adi`, `.frame`,
`.stack`, `.elf`, `.dyn_hash`, `.gnu.plt`, `.gnu.got`, `.gcc_abi`.
APKiD's ELF protector rules identify the combination
`libloader.so` / `LIBLOADER_VERSION=` plus sections such as `.adi` as Appdome
(`appdome_elf_b` in `rednaga/APKiD`, `apkid/rules/elf/protectors.yara`).
The binary is heavily obfuscated: encrypted strings, flattened dispatcher in
`JNI_OnLoad`, AES-like table rounds. Exact native policy flags were **not**
fully decoded.

Do not conflate this with sibling `libpglarmor.so`, `libnms.so`,
`libbuffer_pgl.so`, `libfile_lock_pgl.so`: those carry ByteDance/Pangle/GeckoX
markers and are separate libraries. Managed ACTk detectors and
`PlayIntegrityAttestationProvider` also exist independently in the dump.

## What the supplied DEX proves

`base.apk!classes7.dex` (163,148 bytes) contains the runtime bootstrap that
loads this native library:

- `oRLDI0.r3nXO8.wuMBq1.m7VnM0.rNoYP1()` runs once on the main thread,
  calls `n1P7S0()`, which calls `System.loadLibrary("loader")`.
  Load exceptions call `Process.killProcess(Process.myPid())`.
- `oRLDI0.r3nXO8.wuMBq1.n0tf61` is an `android.app.Application` subclass
  whose `<clinit>` calls that bootstrap.
- `lUP4x6.oUObc4()` registers natives through
  `qwerty.asdfgh.zxcvbn.lkjhgf()`.
- `awIdC3.zU1kn8(Context)` calls `lUP4x6.setApplication(context)` and
  `lUP4x6.dGA592()` during dynamic init.

Concrete Java-side defense code in the same DEX:

- Key injection: `oRLDI0.a7gBM4.hkVWX4` keeps
  `shouldBlockKeyInjection`, `shouldBlockKeyInjectionInit`, and
  `whitelisted_key_injection_packages`. `pr4Fp1()` asks native
  `lUP4x6.xNLE59(String)` for the policy. A registered key-event listener
  inspects `KeyEvent.getFlags()` / action and reports through native
  `lUP4x6.fNyRE4(String, String[])`.
- Installed-package / click-bot scan: `oRLDI0.iX2zo5.mQ7mg9.l45yM5()`
  enumerates installed applications and requested permissions, looking for
  `android.permission.ACCESS_SUPERUSER` combined with
  `SYSTEM_ALERT_WINDOW`, `ACCESS_SUPERUSER_SERVICE`, or `SUPERUSER_SHELL`.
  It builds the message `Detected malicious click bot package: ... with
  permissions: ...` and sends one of the native event IDs
  `3fceb4349c991181_72e2b273fbfab56`,
  `3fceb4349c991181_571b521ac836bae`,
  `3fceb4349c991181_d951a6e756f4f72`.
- Enforcement: `oRLDI0.r3nXO8.wuMBq1.xl0Xj2` tracks
  `shouldFinishActivities`, finishes/removes activities, and `cp63w1()`
  relaunches the package then calls `Process.killProcess(myPid())`.

This proves client-side reporting/enforcement machinery is present. It does
not prove which remote endpoint receives events, whether a particular solo
server ignores them, or that every Appdome defense is enabled. A solo/private
game server does not by itself disable client RASP.

## Native capability surface (imports, not a policy verdict)

`libloader.so` imports, among 222 undefined symbols:

- Loader / module inspection: `android_dlopen_ext`, `dlopen`, `dlsym`,
  `dladdr`, `dl_iterate_phdr`.
- Memory / permissions: `mmap`, `mprotect`, `mremap`, `madvise`, `munmap`.
- Process / debug / signals: `fork`, `pthread_create`, `pthread_atfork`,
  `prctl`, `kill`, `getppid`, `syscall`, `sigaction`, `signal`.
- File / directory / proc-style walking: `open`, `read`, `fopen`, `fgets`,
  `stat`, `lstat`, `opendir`, `readdir`, `nftw`, `access`, `[f/l]xattr`.
- Network: `socket`, `connect`, `bind`, `listen`, `accept`, `sendto`,
  `recvfrom`, `getaddrinfo`, `gethostbyname`.
- Properties: `__system_property_get`, `__system_property_find`.

No direct `ptrace`, `process_vm_*`, or `inotify` imports were found, but
`syscall` is imported, so absence of a named import is not absence of the
capability. Strings are encrypted; cleartext grep is not a behavioral audit.

Appdome's public documentation describes anti-hooking, code-injection,
anti-debugging, and runtime integrity defenses, with Threat-Events for app
handling. That matches the observed Java event bridge, but the exact native
checks in this sample remain only partially reversed.

## Why the old Magic Farm path was incompatible

Before this change, Magic Farm did exactly the kinds of runtime modification
a client RASP is built to notice:

1. root `ptrace(SEIZE/INTERRUPT)` of `UnityMain`;
2. remote `mmap` scratch, `memfd_create("benzware_magic")`, write through
   `/proc/<pid>/fd/N`, `android_dlopen_ext("/proc/self/fd/N", USE_LIBRARY_FD)`;
3. `dlsym("benzware_magic_attach")` and an in-process companion thread;
4. `mprotect` RW→RWX→RX and a 16-byte inline hook at `FixedUpdate`;
5. two vtable CAS swaps (`HitMarkersAck`, `TreeAck`);
6. shared-memory command writes.

The normal farm is not a safe fallback on this client either: it writes
joystick vectors, attack-held byte, sprint action state, and camera angles
into game memory (`farm_joy.cpp`, `farm.cpp`, `esp_mem_aim_write_angles`).
Other features also write through the same `memio::Reader`.

## Change shipped: fail closed, do not bypass

New `jni/src/runtime_guard.h` publishes a per-pid policy from a complete
`/proc/<pid>/maps` snapshot:

- `libloader.so` mapped (including `(deleted)` and APK-embedded `!/lib/...`
  paths) → `Protected`;
- complete snapshot without it → `Clear`;
- empty/truncated/unreadable → `Unknown`.

On Android, game-memory writes require `Clear`. Host rigs stay usable unless
a test explicitly observes a protected snapshot.

Enforcement points:

- `memio::Reader::write()` refuses writes unless
  `runtime_guard::writes_allowed(pid)`. This covers `wr_buf`,
  `esp_silent_write`, farm joystick/attack/sprint, camera angles, viewmodel,
  xray, freecam, silent aim, and marker writes that use the Reader.
- `magic_farm_loader.cpp::load_module()` refreshes maps **before** ptrace.
  Protected → log `protected client ... injection refused` and return new
  `State::ProtectedClient`; no retry loop for that state.
- `magicfarm::Update()` also stops before `client().ensure()` when protected.
- `UpdateFarm()` refreshes the guard at most once per second on Android,
  releases cached normal-controller input once, stops Magic, pauses both
  paths, and sets `g_farmProtected`.
- Menu shows `Клиент под защитой: автофарм и запись в память отключены`;
  Magic status shows the protected-client reason.
- `esp_reset()` clears the guard; maps readers in `game.cpp`,
  `game_viewmodel.cpp`, and `process.cpp` republish it.

This removes benzware's own injection and game-memory write surface on the
protected client. It does **not** hide root, the overlay, external
`process_vm_readv`, `/proc` access, or the tool process from Appdome. Those
can still be detected. No ban-free guarantee is made.

## Verification

Local:

- `tools/tests/run_all_host_tests.sh`: 41/41 PASS (includes new
  `jni/tests/test_runtime_guard.cpp`).
- `tools/tests/run_farm_tests.sh`: PASS, including protected-client pause,
  no Magic load call, release, and clear-snapshot resume.
- `tools/tests/run_menu_ui.sh`: PASS.
- `g++ -fsyntax-only` for `magic_farm_loader.cpp` with host dlext/blob stubs:
  PASS (expected glibc ptrace enum warning under `-fpermissive`).
- `git diff --check`: clean.

Android NDK build / CI: run `41033601187` SUCCESS in 2m35s for commit
`a8fe28b`; artifact `benzware.sh` size 3,012,920, sha256
`6c3cde7bd92b29973b30a9c1f4d919b6a6bcb45922b09dd86c0146620635f8d3`.
Device runtime verification was not performed here. On-device acceptance for this guard:
with the supplied 11991 client, `magic.log` should contain
`protected client ... injection refused`, the menu should show the protected
line, `TracerPid` should stay 0, and `libbenzware_magic.so` should not appear
in the game's maps. Beta-11982 remains unsupported.
