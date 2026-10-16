#pragma once

#include <string>

// The game's own HUD text slots (exe+7793d0 / exe+77a1e0, the functions Script Hook's AddMessage wraps).
// Slot 0 is the general notification box. Calls are posted to the game thread; safe from any thread.
namespace hud {
    // Shows text in a slot for `seconds` (0 = until cleared or replaced). Returns false if unavailable.
    // The game's medium message (top of the screen, the style of level-ups; UI hunt 2026-10-14): exe+77db70 type 7.
    // Falls back to Show() if that call fails. For banners: wave titles, wave cleared, run over.
    bool Banner(const std::string& text, float fallback_seconds = 5.0f);

    // The big "Get Ready" countdown (CountDown element, ShowBigCountDown / HideBigCountDown; UI hunt session 2).
    void Countdown(int seconds);
    void HideCountdown();
    // The hideout-style counter (Counter element, ShowCounter(label, current, max, ...) / HideCounter).
    void Counter(const std::string& label, int current, int max);
    void HideCounter();

    bool Show(const std::string& text, float seconds = 4.0f, int slot = 0);
    bool Clear(int slot = 0);
    bool Visible();   // byte [hud+0x48] (Script Hook's IsMessageVisible)

    // Full-screen fade to black and back (the game's ScreenFadeAction messages). Non-blocking.
    bool FadeOut(float seconds);
    bool FadeIn(float seconds);
}
