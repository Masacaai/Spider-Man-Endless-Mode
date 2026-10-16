#pragma once

#include "director.h"

#include <functional>
#include <string>

// Getting in and out of Endless Mode without commands:
//   - Dock pad (Manhattan): stand still on it for hold_seconds -> teleport to Ryker's and start a run.
//   - Exit pad (Ryker's): stand still on it -> back to the dock (no run active).
//   - Run over: once the hero has respawned, results are shown and they're returned to the dock.
//   - Keyboard (game focused): F8 = enter + start a run, F9 = end the run / leave Ryker's.
// Pads, keys and run options live in <scripts>\EndlessMode.ini ([portal], [run]).
namespace portal {
    void Start(std::function<director::HeroPos()> hero);   // background thread, ~10 Hz
    void Stop();

    // Saves the hero's current position as the dock pad / Ryker's exit pad.
    bool SetDockHere();
    bool SetExitHere();
    void LogConfig();

    // Same as the triggers: teleport to Ryker's and start a run / end the run and return to the dock.
    void Enter();
    void Leave();

    void Enable(bool on);   // pads + hotkeys on/off (persisted)
}
