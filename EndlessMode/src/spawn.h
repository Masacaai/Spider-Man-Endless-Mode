#pragma once

#include <cstdint>
#include <string>

namespace spawn {
    struct Vec3 { float x, y, z; };

    // Loads the actor asset by path (e.g. "character\\enemy\\enm_meleethug\\enm_prison_meleethug.actor"),
    // waits for it, and spawns it at pos/heading (degrees) on the game thread.
    // Blocks the calling (non-game) thread; returns the entity pointer or 0.
    // engaged: right after SpawnRaw (before activation) sets BotData.SpawnEngagedWithHero (+0x08) on every
    // BotData-shaped struct found in the bot's components. EXPERIMENTAL, see entity::FindBotData.
    // path may also be "#<16 hex digits>", a raw asset id, for actors whose path isn't known.
    uintptr_t SpawnActor(const std::string& path, Vec3 pos, float heading_deg, bool engaged = false);

    // Starts loading an actor asset (and returns immediately): heavy actors (bosses) spawn incomplete - no mover, no
    // head - if spawned before their dependencies have streamed in. Preload them well ahead (verified 2026-10-13).
    // Also preloads the actors it creates at runtime (weapons, jetpacks, heads; table from tools/assets/deps.py).
    void Preload(const std::string& path);
    // True once `path` was preloaded at least `min_ms` ago and it and its dependencies report loaded (or 20 s passed). An actor never preloaded is preloaded now and reported not
    // ready, so every unit type gets streamed in before its first spawn (a SwordsmanBot spawned cold came out without
    // its mover and crashed the game, 2026-10-13).
    bool Ready(const std::string& path, unsigned min_ms = 8000);

    // Lets go of a preloaded actor (and its dependencies no other preloaded actor needs): our reference on each asset
    // is dropped with the game's AssetRelease. Assumed (not verified yet): live actors hold their own references.
    void Release(const std::string& path);
    void ReleaseAll();   // end of a run / stress test
    void LogPreloads();  // counts, to the log

    // Sync object whose handle resolves to entity (0 if none).
    uintptr_t FindSyncFor(uintptr_t entity);

    // Deletes the entity's sync object and destroys the entity on the game thread. Blocks; true on success.
    bool DespawnActor(uintptr_t entity);
}
