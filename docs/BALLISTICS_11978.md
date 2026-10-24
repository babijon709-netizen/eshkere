# Release 11978 firearm prediction

Sources: `dump11978.7z` (`dump.cs`, `libil2cpp.so`) and `weaponassets.7z`
(`data-weapons-configs_assets_all_9cc8cc5a32f278cd381af64ba6c210ee.bundle`).
These inputs live under ignored `dumps/`, not in Git.

## Verified native recurrence

- `BallisticProfile.cQi`, RVA `0x7BF07D8`: copies signed gravity, air drag,
  underwater drag unchanged to `BallisticParams`.
- `BallisticIntegrator.Step`, RVA `0xCA6AF9C`: default timestep 1/32 second.
- Explicit-dt overload, RVA `0xCA6AFA4`:
  1. Determine underwater state through the supplied callback; without it,
     use `Position.y < 0`.
  2. Above water only, add `gravityVector * Gravity * dt` to old velocity.
  3. Multiply by `clamp(1 - drag * dt, 0, 1)`.
  4. Average old velocity with that result.
  5. Store the **average** as the next velocity and advance position by it.

The solver's scalar launch response and gravity response follow the same
recurrence, including fractional interpolation within a simulation segment.
`Parameters::averaged_velocity` enables this path for firearm profiles and
verified firearm fallback rows. Legacy projectile normalization/integration is
not changed by this patch.

Firearm assets store gravity 5 and zero air/underwater drag; signal flare config
45 stores gravity 2.5. Speed and range differ by weapon. Runtime config remains
preferred over the object-ID fallback. Unknown weapons no longer get a fabricated
500 m/s profile. Close-range firearm target velocity is no longer suppressed.

## Checks and limits

`test_aim_ballistics.cpp` includes a separate double-precision scalar forward
simulation, golden two-step gravity/drag values, and 144 interception cases over
six firearm speeds, signed gravity, drag, moving targets, inherited velocity,
and integer/fractional ticks. Original legacy tests remain in place.

This is not proof of on-device/server parity: the external solver still assumes
a gravity vector of (0, -9.81, 0) and, without a game water callback, sea level 0.
Camera-origin selection, control lead, motion estimation, spread, server timing,
and actual inheritance of shooter velocity need device validation. No new ping
compensation or claims of guaranteed hits are introduced.

Host regression command (no NDK required):

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Ijni/include -Ijni/src \
  jni/tests/test_aim_ballistics.cpp -o /tmp/benzware-ballistics && \
  /tmp/benzware-ballistics
```

Android build: `ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh`.
