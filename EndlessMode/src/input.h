#pragma once

// One place for the mod's controls, so prompts can name the button for the device the player is actually using
// (user, 2026-10-13: never hard-code "F" / "Triangle"). Keyboard via GetAsyncKeyState (game window focused only),
// Xbox pads (and PlayStation pads exposed through Steam Input) via XInput, PlayStation pads via psinput (HID).
namespace input {
    enum class Device { Keyboard, Xbox, PlayStation };
    enum class Action {
        Interact,   // F / Y / Triangle: dock prompts, exit circle
        Choice1,    // 1 / Square / X
        Choice2,    // 2 / Triangle / Y
        Choice3,    // 3 / Circle / B
        Choice4,    // 4 / Cross / A (only when a fourth option is on offer)
    };

    // Polls devices and updates the last-used one; call regularly (the portal loop does, every 100 ms).
    void Update();
    Device Last();

    bool Down(Action a);
    // Down this poll but not the previous one (per action; call once per poll).
    bool Pressed(Action a);

    // Button name for the last-used device, e.g. "F", "Y", "Triangle", "1".
    const char* Name(Action a);
    // In-text button icon: the game's [BTN_X/Y/B/A] tokens on controllers, "[1]" etc. on keyboard.
    const char* Glyph(Action a);
}
