#pragma once

#include "plan.h"

#include <cstdint>
#include <string>
#include <vector>

// Roguelike layer for Campaign runs (docs/ROGUELIKE.md, docs/LEDGER.md).
//  - Zero-start: the run begins with no suit power and no suit mods (runloadout), and the player earns them.
//  - After each cleared wave: pick 1 of 3 boons (suit power, suit mod, or a stat boon). After a boss wave: pick 1 of
//    3 suit powers, then an optional bittersweet curse (always an upside and a downside).
//  - Boons never touch Spider-Man's skills; curses never add enemy health (numbers, mix and elites only).
// The director drives it: Begin/End around a run, OnWaveCleared / OnKill, Tick while Offering(), and AdjustWave on
// each new wave plan. Damage scaling is read by the stats damage hooks (OutgoingMult / IncomingMult).
namespace boons {
    void Begin(uint32_t seed);    // run start (Campaign): pools from the game's item categories, loadout cleared
    void End();                   // run end: loadout restored, modifiers reset
    bool Active();

    void OnWaveCleared(int wave, bool boss_wave);   // queues the reward offer(s)
    void OnKill();
    bool Offering();              // an offer is on screen (the director holds the next wave)
    void Tick();                  // input + HUD for the current offer (worker thread, ~every 100 ms)

    void AdjustWave(plan::WavePlan& p, int horde_max);   // curses' enemy-side effects (counts, mix, elites)

    float OutgoingMult();         // damage the hero deals
    float IncomingMult();         // damage the hero takes

    std::vector<std::string> Summary();   // current boons and curses, for a loadout view
}
