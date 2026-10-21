# Headless menu checks

```sh
tools/tests/run_menu_ui.sh
tools/tests/run_menu_ui.sh --measure
```

Requires a C++17 host compiler (`g++`, or set `CXX`). The runner builds the real
`menu.cpp`, `widgets.cpp`, theme, localization and bundled ImGui in a temporary
directory. `ui_host/` replaces only Android/GL headers; the test mocks filesystem,
audio, blur and game-memory callbacks. No device or game process is accessed.

Checks cover:
- independent feature switches and expandable settings, stable IDs;
- release taps, modal/scroll/consumed-input blocking and clipped rows;
- all previous controls across all five tabs, profile actions and confirmation;
- calibration entry points, farm/aim point reset and immediate weapon/FPS callbacks;
- expanded layouts at narrow/wide content widths, both languages and themes;
- collapsed content height and scroll bounds after content shrinks.

`--measure` prints default content heights (650 px content width, 28 px font).
These are layout measurements, not device screenshots or frame-rate benchmarks.

This is **not** an Android build or an on-device integration test. Before shipping,
build with the project's NDK and check on a device: touch scrolling vs sliders,
color popovers, every calibration's return to its owning inline group, both tab
panel placements, profile load/save/delete, and restoring enabled features.
