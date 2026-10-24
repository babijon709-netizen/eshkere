# Third-person chase camera (release 11978)

Misc → «Третье лицо»: enabled toggle, distance 1–6 m (default 2.5 m),
shoulder offset −1.5–1.5 m (default +0.4 m). There is no separate hotkey.
Off at startup even when saved enabled; explicit profile load can restore the
saved toggle. TPP1 is an optional, XOR-encoded 16-byte trailer after SPN1;
existing config structures and old profiles remain readable.

## Implementation

Pure external, one executable, selected memory transport. No injected module,
code patch or remote managed-method call. Reuses the existing camera resolver,
view/projection/world-to-clip writer and generation guards. It does **not** move
the native camera/eye Transform, KCC or character, and does not rotate the body
for a cosmetic anti-aim demonstration. The shared writer skips Transform slots
while owned by third person; ordinary freecam retains its existing behavior.

Eye follows the validated local KCC position plus crouch-aware capsule height and
look-height offset. Orientation follows MouseLook, not spoofed PlayerInput.
Each overlay frame recomputes an orthonormal camera basis and the desired point
behind the eye. Normal touch look remains with the game; no freecam joystick or
look-zone interception in this mode.

Freecam, farm, ADS, attack, unreadable attack/pose data and respawn suspend it.
Attack ownership is checked using fresh GameController/handler/local-owner reads.
These are frame-level gates, **not** synchronization with the game's firing or
render threads. Aim is paused while third person owns the camera. External AA
may continue under its own gates. Camera disable stops the shared writer and
marks view dirty for the game to rebuild. Process/player/native-camera/Transform
changes discard ownership without restoring old Transform data; process reset
cancels the writer generation before unbinding.

## Limits

- This is a render-view displacement. It does **not** force hidden local-body
  Renderers on or change culling/layers. Whether the local body is drawn in this
  build is unverified; a hidden body stays hidden. This is not a verified
  full-body third-person implementation.
- No wall collision raycast: the camera can clip through geometry.
- First-person weapon/hands visibility is unchanged.
- Existing external view writer races with Unity's camera updates; smoothness,
  absence of flicker and runtime compatibility require device testing.
- Seeing a local body spin cannot prove that the server received spoofed yaw.
  Conversely, a stationary local body does not disprove outgoing spoofing. The
  current external AA edits shared PlayerInput, not a verified packet-only path.
  Confirm remote appearance separately with another observer; no sending,
  server-acceptance, firing-isolation or detection guarantee is made.

## Verification and build

```
tools/tests/run_third_person.sh
bash tools/tests/run_menu_ui.sh
tools/tests/run_network_aa.sh
ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh
```

Geometry/basis, follow translation, pitch/yaw/shoulder signs, finite checks under
fast-math, and profile validation tested on host (normal/fast-math/ASan+UBSan).
Headless production-menu test covers off-default state, collapse/expand and
both distance controls. Integration sources passed host syntax checking. These
checks do not execute Unity or validate the Android ABI/build/runtime. No NDK
build or device/server test was performed here. Output of the real build:
`libs/arm64-v8a/benzware.sh`.
