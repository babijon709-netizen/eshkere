# Headless checks

```sh
tools/tests/run_touch_device_filter.sh
tools/tests/run_menu_ui.sh
tools/tests/run_menu_ui.sh --measure
tools/tests/run_menu_ui.sh --bench
tools/tests/run_capture_surface.sh
tools/tests/run_screen_capture.sh
tools/tests/run_startup_cli.sh
tools/tests/run_predict.sh
```

Requires a C++17 host compiler (`g++`, or set `CXX`). The touch-filter runner
exercises recognized Linux/Android evdev capability combinations used by
physical panels and virtual-machine single-touch devices.

The menu runner builds the real `menu.cpp`, `widgets.cpp`, theme, localization,
and bundled ImGui in a temporary
directory. `ui_host/` replaces only Android/GL headers; the test mocks filesystem,
audio, blur and game-memory callbacks. No device or game process is accessed.

Checks cover:
- independent feature switches and expandable settings, stable IDs;
- release taps, modal/scroll/consumed-input blocking and clipped rows;
- all previous controls across all five tabs, profile actions and confirmation;
- capture buttons, overlay toggle, bitrate presets, next-start semantics and translations;
- calibration entry points, farm/aim point reset and immediate weapon/FPS callbacks;
- expanded layouts at narrow/wide content widths, both languages and themes;
- collapsed content height and scroll bounds after content shrinks;
- offscreen culling without changing layout/slider state, and font-size probes
  that do not allocate a baked font for every trial size.

`run_capture_surface.sh` tests the real SurfaceFlinger ABI wrapper against fake
libgui symbols for Android 8–16, including unavailable symbols and failure; it
also exercises the real EGL switching/rollback logic with a mocked platform
and round-trips the actual config trailer, including bitrate, ignored legacy
duration bits and unchanged flag layout. These tests cannot establish what an
OEM compositor or recording app does on a real device.

`run_screen_capture.sh` exercises the asynchronous supervisor with a fake
recorder, ignored SIGCHLD, simultaneous screenshots, normal stop, failure,
parent death and a single uninterrupted output. It also tests the legacy remuxer
with MediaNDK mocks. When PyAV 16.0.1 is installed, it tests the actual MP4 writer
with real H.264 in AVCC and Annex B forms, playback, seek, sudden death, sparse
sample offsets over 4 GiB, 64-bit duration and index compaction. Without PyAV,
that real-video test is explicitly skipped. These host tests do not capture Android
pixels.

`.github/workflows/android-recording-smoke.yml` builds the actual recorder for
x86_64 Android emulators, captures their displays, and checks Android's media
extractor and host H.264 playback. An encrypted test binary exercises the real
supervisor's self-exec and normal/parent-death stops. The Android 8 run records
for over three minutes and checks the recorder PID, playback and seeking. It
does not run the full production arm64 binary, an OEM firmware, the overlay or a game.
`run_startup_cli.sh` tests the real key gate through pipes, a line-buffered
launcher and a PTY, before and after the ELF patcher, including a complete
prompt line before input, CRLF, wrong key and EOF.
These are host tests, not evidence that the arm64 binary starts on a device.

`run_predict.sh` feeds the pose-delay estimator (`aim_predict.h`) a synthetic
strafer with a known delay between the pose we read and the true position, at
60 and 30 Hz. It must report that delay back within one sample period; with the
velocity delayed by the same amount (the case where the delay is not measurable)
it must report ~0 instead of inventing a lead from noise.

`--measure` prints default content heights (650 px content width, 28 px font).
These are layout measurements, not device screenshots or frame-rate benchmarks.
`--bench` measures warmed host CPU time and submitted geometry for expanded misc
settings behind a 450 px viewport; it excludes Android, game I/O and GPU work.
Compare on the same machine/compiler, not with device FPS.

This is **not** an Android build or an on-device integration test. Before shipping,
build with the project's NDK and check on a device: touch scrolling vs sliders,
color popovers, every calibration's return to its owning inline group, both tab
panel placements, profile load/save/delete, and restoring enabled features.
