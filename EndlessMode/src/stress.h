#pragma once

#include "director.h"

#include <functional>
#include <cstdint>
#include <string>

// How many enemies can the game take? Spawns in steps around the hero, measures the game-thread tick rate
// (frame-rate proxy) and the worst frame after each step, and stops at a floor. Results go to the log, the HUD and
// <scripts>\EndlessMode_runs\stress_<date>_<time>.csv.
namespace stress {
    struct Params {
        int step = 10;          // enemies added per step
        int max = 200;          // stop after this many
        float min_hz = 25.0f;   // stop when the tick rate falls below this
        int settle_s = 6;       // wait after a step before measuring
        int measure_s = 4;
        bool engaged = false;   // engaged: they fight (heavier AI); hero health is kept topped up
        std::string faction = "prisoners";
        uint32_t seed = 1234;   // spawn-point / unit order (the same seed replays the same spawns)
        bool mixed = false;     // every faction's goons, all roles (minus calm-only classes), not just melee/pistol
        int start = 0;          // spawn this many in the first step (skip the early steps)
        int bodies = 0;         // percent of each step killed right after spawning (bodies on the ground): does the
                                // ~160 hang count actors or live AI? (user, 2026-10-14: one pool for both)
    };

    void Start(const Params& p, std::function<director::HeroPos()> hero);
    void Stop();
    bool Running();
}
