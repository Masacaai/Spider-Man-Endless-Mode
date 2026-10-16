#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Entity layout on 4.0630 (from ScriptHookSMPC's GetComponentByName / GetTransform):
//   [entity+0x00] Transform*      (position at +0x30)
//   [entity+0x68] component array, 16-byte entries { desc*, instance* }; type name = (char*)[desc+0x60]
//   [entity+0x70] int16 component count
namespace entity {
    // Instance pointer of the first component whose type name equals name, or 0. Guarded reads.
    uintptr_t FindComponent(uintptr_t entity, const char* name);

    // Calls fn(type name, instance) for each component. Guarded reads.
    void ForEachComponent(uintptr_t entity, const std::function<void(const std::string&, uintptr_t)>& fn);

    // Enemy health: float at BotHealth+0x88 (as jedijosh920's Mod Menu reads it). false if unavailable.
    bool ReadBotHealth(uintptr_t entity, float& health);

    // Health component of any bot, bosses included: BotHealth if present, else the first component whose type name
    // ends in "Health" (not HeroHealth). Layout of all of them (Health base): +0x80 max, +0x88 current.
    uintptr_t HealthComponent(uintptr_t entity);
    bool ReadHealth(uintptr_t entity, float& health, float& max_health);

    // Multiplies max health and refills to the new max (game thread: uses Health::SetHealth, which clamps to +0x80).
    // Returns the new max, or 0 on failure.
    float ScaleHealth(uintptr_t entity, float mult);

    // Player health: HeroHealth+0x88 current, +0x80 max (from the Mod Menu's debug print). false if unavailable.
    bool ReadHeroHealth(uintptr_t hero, float& health, float& max_health);

    // Actor handle: (u16[+0x10] & 0x7FF) << 20 | (u32[+0xC] & 0xFFFFF). 0 if unreadable.
    uint32_t HandleOf(uintptr_t entity);

    // Live entity for an actor handle (actor table at [exe+7a44380], 0xC0 per entity), or 0. Tries the slot the
    // handle's index names first, then scans the table.
    uintptr_t FromHandle(uint32_t handle);

    // The separate head actor of a bot, or 0: the actor-table slot right after the body (handle + 1, or + 2), with an
    // AnimControllerComponent, no weapon/health and at most 3 components, within `max_dist` of the body (live
    // 2026-10-14). `offset` gets the slot distance (1 or 2).
    uintptr_t FindHead(uintptr_t body, float max_dist = 3.0f, int* offset = nullptr);

    // The gameplay camera: the actor with a FollowCamera component (found 2026-10-13; one per game, near the hero).
    // Its transform is the camera's: row 2 (+0x20) forward, row 3 (+0x30) position. Cached; re-found (at most every
    // 2 s) when the cached actor no longer has the component. 0 if not found.
    uintptr_t Camera();

    // Handles of the area/group actors a bot belongs to (BotAreaActor / BotAreaActorGroup component +0x48).
    // Up to 4 entries; returns how many were found.
    int AreaHandles(uintptr_t entity, uint32_t out[4], std::string names[4]);

    // BotData (reflected config, field table at exe+395e260, offsets at exe+395e440): bool SpawnEngagedWithHero
    // +0x08, Intro flags +0x09..+0x0D, float IntroSpeed +0x10, bool DisableIdleWander +0x14, objects +0x18/+0x38/+0x58,
    // float AggressorDesire +0x78, float DefenderDesire +0x7C, bool ShareAwarenessWithSkirmishGroup +0x80,
    // bool ProjectileThrower +0x81, string DisplayName +0x88, bool CanUseQTEAttacks +0x98, bool SetModelInstAsHero +0x99.
    // Known homes: Bot (runtime base of ThugBot etc.) inline at +0x160 (getter exe+2fff30 reads +0x168; Bot vtable
    // slot 7, exe+2f5aa0, acts on it), and BotPrius (per-type config) at +0x2C0. This scans the components inline
    // and one pointer level deep (pointer target, and target+0x2C0) for that shape.
    struct BotDataHit {
        std::string component;
        uintptr_t addr = 0;     // address of the BotData struct
        int depth = 0;          // 0 = inline in the component, 1 = behind a pointer at component+ptr_off
        uint32_t ptr_off = 0;
        uint32_t inline_off = 0;
        bool exact = false;     // vtable is BotData's (exe+395e988) or GrenadierBotData's / TaskmasterBotData's
    };
    std::vector<BotDataHit> FindBotData(uintptr_t entity);

    // EXPERIMENTAL forced aggro: writes the fields observed to change when a bot becomes aware of the
    // hero (live diff, recon/M1_natives_4.0630.md). Runs on the game thread. Not the game's own API.
    enum AwareParts : int { kAwareTiered = 1, kAwareDB = 2, kAwareSkirmish = 4, kAwareAll = 7 };
    void ForceAwareHack(uintptr_t entity, uint32_t hero_handle, int parts);
}
