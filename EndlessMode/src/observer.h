#pragma once

#include <cstdint>

// Passive research hooks: log what the game itself spawns and loads (hideouts, crimes, missions),
// with the calling code's RVA. They forward every argument unchanged. Off until Enable(true).
namespace observer {
    bool Enable(bool on);

    // Pass-through hook on Health::SetHealth: when health drops, logs the entity, old/new value and a
    // backtrace (exe RVAs) so the game's damage path can be identified.
    bool WatchDamage(bool on);

    // Hit template: the damage message of the latest real hero -> bot hit (learned while dmgwatch hooks are
    // installed). damage::Nudge copies its attack fields so our hit looks like a real one.
    void SetHeroHandle(uint32_t handle);
    bool CopyHitTemplate(uint8_t out[0x1C0]);
}
