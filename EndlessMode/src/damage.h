#pragma once

#include <cstdint>

// Sends a damage message through the game's own damage pipeline (the path melee uses), so the target's
// awareness reacts the way it does to a real hit (LineOfSightAwareness: DamageEvent -> kConfirming/kConfirmed).
namespace damage {
    // Queues on the game thread: a hit on target_entity attributed to attacker_handle, dealing `amount` damage
    // (msg+0x144, read by Health::OnDamage exe+1f15c30). A small amount makes it a real DamageEvent.
    // mode 3: replay the learned hit wholesale (+0x13C..+0x1A7 and its flag bits).
    // mode 0: header + amount only. mode 1: also copy attack fields (+0x154, +0x168, +0x19C) from the learned
    // hit template (observer::CopyHitTemplate). mode 2: mode 1 + the attack tag list (+0x180..+0x18C).
    bool Nudge(uintptr_t target_entity, uint32_t attacker_handle, const float attacker_pos[3], float amount = 2.0f,
               int mode = 0);
}
