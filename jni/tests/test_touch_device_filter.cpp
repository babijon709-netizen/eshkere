#include "touch_device_filter.h"
#include <cassert>

int main() {
    using namespace touchfilter;

    Caps panel{};
    panel.mt_x = panel.mt_y = panel.direct = true;
    assert(classify(panel) == MultiTouch);

    Caps xiaomi_touch{};
    xiaomi_touch.mt_x = xiaomi_touch.mt_y = true;
    xiaomi_touch.pointer = true;
    assert(classify(xiaomi_touch) == NotTouch);

    Caps pen{};
    pen.abs_x = pen.abs_y = pen.btn_touch = pen.direct = pen.tool_pen = true;
    assert(classify(pen) == NotTouch);

    // Some panels expose finger multitouch and stylus capabilities on the same
    // DIRECT event node. Do not reject the whole touchscreen because it can
    // also report a pen.
    Caps hybrid_panel{};
    hybrid_panel.mt_x = hybrid_panel.mt_y = true;
    hybrid_panel.direct = hybrid_panel.tool_pen = true;
    assert(classify(hybrid_panel) == MultiTouch);

    // A mouse-like VM integration may mark the device as a pointer; DIRECT
    // still identifies it as a touchscreen rather than an external mouse.
    Caps direct_pointer_panel{};
    direct_pointer_panel.mt_x = direct_pointer_panel.mt_y = true;
    direct_pointer_panel.direct = direct_pointer_panel.pointer = true;
    assert(classify(direct_pointer_panel) == MultiTouch);

    Caps vm{};
    vm.abs_x = vm.abs_y = vm.btn_touch = true;
    assert(classify(vm) == SingleTouch);

    Caps vm_tool_button{};
    vm_tool_button.abs_x = vm_tool_button.abs_y = true;
    vm_tool_button.btn_tool_finger = true;
    assert(classify(vm_tool_button) == SingleTouch);

    Caps joystick{};
    joystick.abs_x = joystick.abs_y = true;
    assert(classify(joystick) == NotTouch);
}
