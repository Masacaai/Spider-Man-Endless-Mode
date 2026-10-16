#pragma once

#include <cstdint>

// The player's real suit power and suit mods during a roguelike run (Campaign). At run start the current loadout is
// saved to <scripts>\EndlessMode_loadout.txt, the power and mod slots are emptied and locked (the menu can view but
// not change them; suits stay free), and boons fill them as the run goes. At run end it's restored and the file is
// deleted. If the game crashed mid-run, the file is still there and Recover() restores it on the next launch.
// All functions run game-thread work through gamethread::Run, so call them from worker threads.
namespace runloadout {
    bool Begin();          // snapshot, clear power and mods, lock
    void End();            // unlock, restore the snapshot, delete the file
    void Recover();        // restore a snapshot left by a crash (no run active); safe to call any time
    bool Active();

    // Run-side changes (bypass the lock). slot: 1 power, 2..4 mods.
    bool SetPower(uint64_t id);
    int FreeModSlot();     // first empty mod slot (2..4), or -1
    bool AddMod(uint64_t id);
    bool HasItem(uint64_t id);
}
