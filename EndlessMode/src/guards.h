#pragma once

// Crash guards for game code that assumes mission context. Installed once at startup.
//   BotStateGotoLocal init (exe+1dd6660): caches the bot's MoverStandard / TargetingBase / NavEdgeResolver and then
//   dereferences the MoverStandard without a null check. Bosses spawned outside their mission crashed there
//   (2026-10-13, Tombstone). The guard patches in a null check at the crash site (see guards.cpp).
namespace guards {
    bool Install();
}
