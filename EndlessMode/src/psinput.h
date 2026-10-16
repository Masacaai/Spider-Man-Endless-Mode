#pragma once

#include <cstdint>

// PlayStation controllers read directly over HID (shared, read-only), because the game talks to DualSense natively
// and XInput never sees it. Supports DualSense / DualSense Edge / DualShock 4, USB and Bluetooth.
namespace psinput {
    void Start();              // background reader thread (re-scans for controllers every few seconds)
    bool TriangleDown();
    bool Connected();

    // Face buttons and D-pad from the same byte: high nibble square 0x10, cross 0x20, circle 0x40, triangle 0x80;
    // low nibble the D-pad hat (0 up, 1 up-right, 2 right, 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left,
    // 8 = none).
    enum Button { kSquare, kCross, kCircle, kTriangle, kUp, kRight, kDown, kLeft };
    bool Down(Button b);
    unsigned LastActivityTick();   // GetTickCount() of the last button or D-pad change (0 = never)
}
