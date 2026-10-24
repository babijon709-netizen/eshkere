# External input anti-aim — release 11978

One normal `benzware` executable. No Zygisk/Magisk module, injected library,
remote execution, inline hook, network hook, executable-memory write, or library
patch. The abandoned module implementation and its build/dependencies/IPC have
been removed. The feature uses the existing selected `g_mem` transport.

## What this actually does

Best-effort writes to the local `Oxide.PlayerInputHandler`'s **shared data**:
`PlayerInput` at +0x40, first 24 bytes only (MovementInput, LookDirection,
YRotation). It does not intercept the outgoing serialized copy. The game can
consume/overwrite these values before or after a read/write; this may affect
local prediction as well as packets. A successful memory write is not proof
of a changed outgoing packet, changed remote pose, or server acceptance.

The UI deliberately says `External · best effort`, not "hook ready". Options:
spin, jitter, backwards; real/up/down elevation; spin speed 30–1440 deg/s and
reverse. Rotation is based on monotonic time and a bounded tick/time cache.
Movement is compensated in the fake-yaw basis for yaw-relative processing.
Actual movement/pose behavior still requires device verification.

## Layout and guards

Source: `dump11978.7z/dump.cs`, `PlayerInput` TDI 6562 (0x28 bytes),
`Oxide.PlayerInputHandler` TDI 9777. Offsets are reused from the project's
release profile. Before enabling writes, read (never modify) the 32-byte
fingerprint of the release 11978 PlayerInput serializer at RVA 0x7494290.
Beta and fingerprint mismatches are rejected.

Resolve GameControllerBase static fields, current player and input handler;
validate handler class and owner back-reference. Recheck identities before
writing. PID/base/player/handler changes discard all retained ownership rather
than restoring into a new session. Null/failed pointer reads cause no writes.

The controller reads without the frame cache and re-reads the entire 40-byte
input before a single 24-byte write. A changed snapshot aborts the write.
Tick, flags, padding and adjacent fields are never written. This narrows a
TOCTOU race; it does **not** turn remote writes into atomic compare/exchange.
Partial writes and game updates during the final write remain possible.

Repeated calls on our own snapshot use its saved real basis, not the previously
spoofed yaw. New game-produced values win. When disabled/paused, the original
prefix is restored only if Tick and the entire currently observed prefix still
match our last write, with another full-snapshot check before restoration.
Unchanged input Tick for more than 250 ms suspends spoofing and attempts that
conditional restoration. Normal shutdown also attempts restoration; SIGKILL
cannot run cleanup. The next game update normally produces fresh input, but a
server may already have consumed earlier altered values.

Pauses: menu, calibration, freecam, farm, aim ownership, ADS, attack held,
respawning, not-ready or too-close input; unreadable attack state fails closed.
The external reads are not synchronized with the game's fire/update thread,
so this is not a guarantee that every shot bypasses spoofing.

No claim of being undetectable. Avoiding library patches removes that particular
modification, not detection of external data writes or unusual server input.

## UI / config / tests

**Misc → Anti-aim · External**; off by default, and never enabled by automatic
startup profile restoration. Explicit profile load can restore the toggle.
The existing frozen config layouts are unchanged; the appended SPN1 settings
trailer carries the toggle, reverse, speed and yaw/pitch modes.

Counters: `W` = successful writes, `R` = conditional restores, `race` = skipped
writes because the input changed. None is a packet acknowledgement.

```sh
tools/tests/run_network_aa.sh
tools/tests/run_menu_ui.sh
ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh
```

Host tests cover packet shape/normalization, deterministic phase, yaw-relative
movement, flags/padding preservation, invalid inputs, profile compatibility,
repeated same-tick calls, conditional restore, newer game writes, changed Tick,
read/write failures, detach/address reuse, and stalled input. They run with
-O2, -O3 -ffast-math, ASan/UBSan. Android/device/server behavior is not tested
by these host fixtures.

If an older experimental Zygisk module was installed separately, remove/disable
it and restart the game before testing this external-only executable. This
code does not silently uninstall anything from the device.
