#pragma once

namespace touchfilter {

struct Caps {
    bool mt_x = false;
    bool mt_y = false;
    bool abs_x = false;
    bool abs_y = false;
    bool btn_touch = false;
    bool btn_tool_finger = false;
    bool tool_pen = false;
    bool direct = false;
    bool pointer = false;
};

enum Kind {
    NotTouch = 0,
    MultiTouch = 1,
    SingleTouch = 2
};

inline Kind classify(const Caps& caps) {
    const bool multi_touch = caps.mt_x && caps.mt_y;

    // A stylus can share the same DIRECT multitouch event node as the finger
    // panel. Accept that combined device, but still reject pen-only digitizers.
    if (caps.tool_pen && !(caps.direct && multi_touch)) return NotTouch;
    if (caps.pointer && !caps.direct) return NotTouch;
    if (multi_touch) return MultiTouch;
    if (caps.abs_x && caps.abs_y && (caps.btn_touch || caps.btn_tool_finger))
        return SingleTouch;
    return NotTouch;
}

} // namespace touchfilter
