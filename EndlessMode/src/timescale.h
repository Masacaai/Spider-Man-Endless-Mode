#pragma once

// World time scale through the game's own TimeScaleSystem (static at exe+621b890; current scale at +0x18), the way
// TimeScaleAction (mission scripts' slow motion) does it: exe+19bbaa0 Start(sys, channel, target, rate, name, mode)
// on channel 5. Used to freeze the fight while the player picks a reward (user: "stop the game when the user is
// picking, like the pause menu"). Game-thread work is posted; safe from any thread.
namespace timescale {
    bool Set(float target, float rate = 50.0f);   // rate: scale units per second (big = instant)
    float Current();
    bool Freeze();     // ~0 (not exactly 0, to stay clear of divisions by zero in the game)
    bool Unfreeze();   // back to 1
}
