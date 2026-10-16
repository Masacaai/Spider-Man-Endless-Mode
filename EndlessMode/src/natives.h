#pragma once

#include <climits>
#include <cstdint>

// Game functions/data on Spider-Man.exe 4.0630.0.0, found by signature at runtime.
// Expected RVAs come from static analysis (recon/M1_natives_4.0630.md); a mismatch means
// the signature landed somewhere new and the native must not be called.
namespace natives {
    enum Id {
        GetActor,           // Actor* (uint32_t* handle)
        GameThreadTick,     // hooked by Script Hook to run work on the game thread
        SpawnRaw,           // entity* (u64 asset hash, 0, Transform* 0x40); asset must be loaded first
        LoadAssetByHash,    // asset* (manager*, u64 hash, 0, 0, 0, 0, 0)
        ActorAssetManager,  // .data object
        CreateSyncObject,
        EntityInit,         // SH 5A1A8
        SyncRegisterHandle, // SH 5A190
        EntityIsSetUp,      // SH 5A230
        SyncLookup,         // SH 5A200
        EntityAttach,       // SH 5A228
        HeroHandleData,     // SH 5A1E8, .data
        LoadAssetByName,    // asset* (manager*, const char* path, 0, 0, 0, 0)
        SyncSlotTable,      // .data: void** -> table of 0x2EE0 sync slots (SH 5A160)
        SyncActivate,       // (sync, 1) when entity flags&8 (SH 5A1F8)
        SyncFlag2,          // (short sync[0x114]) when sync[0x11F]&2 (SH 5A208)
        SyncFinish1,        // (sync, 1) (SH 5A220)
        SyncFinish0,        // (sync, 0) (SH 5A218)
        DeleteSyncObject,   // (sync) (SH 5A170)
        EntityDestroy,      // (entity), game thread (SH 5A1B8)
        ConvertStringToHash,// u64* (u64* out, const char* path) (SH 5A130)
        ZoneAssetManager,   // .data object; LoadAssetByHash(&it, zone hash) loads a .zone (as the Mod Menu does)
        // ProximitySpawnSystem (the engine's built-in enemy spawner; what Chaos City 2 drives in MSM2).
        // Call sites are in ProximitySpawnScriptAction's input handler (exe+d357d0).
        ProxSystem,         // .data singleton
        ProxEnable,         // (sys, u64 config asset id or 0 = default config, u32 activator handle, u8 2)
        ProxDisable,        // (sys, u32 activator handle, 0)
        ProxStop,           // (sys): state 7 -> 6, stop spawning
        ProxReset,          // (sys): hard reset
        ProxClearTiers,     // (sys)
        ProxPushTier,       // (sys, u32 tier): up to 2 tier overrides
        HealthSet,          // Health::SetHealth(health component*, float value): clamps to [0, max], stores at +0x88
        // Damage pipeline (as used by melee at exe+69be20): msg = DmgCreate(&DmgSys, &target_handle, &pos, &vec,
        // &dir, -1) -> fill msg fields (+0x138 attacker handle, ...) -> DmgCommit(&DmgSys) dispatches the newest record.
        DmgSys,             // .data damage system
        DmgCreate,          // returns msg (record+0x60), or &sys+0x123868 on pool exhaustion
        DmgCommit,          // (sys)
        HealthOnDamage,     // Health vtable+0x58 (health*, msg*): copies msg, applies it, accumulates msg+0x144
        // Event system (generic): SendEvent(&EventSys, type_hash, target_handle, &source_handle, int 1, byte 0,
        // qword 0, dword 0, byte 1, float, float, payload*, payload_size). Type hash = Insomniac CRC32 of the class name.
        EventSys,           // .data
        SendEvent,          // exe+16d5e80
        GroupAlert,         // exe+496b40 (group*, u32 alerter handle, u8 flag): sends GroupAlertedEvent to members
        ResolveRef,         // exe+16798f0 (u32* ref) -> object; weak-ref table of 16-byte {object*, generation}
        RefTable,           // .data: [RefTable] = entries, [RefTable+0x20] = count
        // HUD text slots (what Script Hook's AddMessage/ClearMessage wrap): 21 slots per player HUD-message object,
        // slot 0 = the general notification box (game uses it with duration 5.0).
        HudMessages,        // .data array of 0x5F8-byte objects, one per player; element 0 = player 0
        HudShow,            // (hud, int slot <0x15, const char* text, float seconds, int 0, int 0, u8 0, u8 1)
        HudClear,           // (hud, int slot, 0, u8 0)
        HeroOnDamage,       // HeroHealth vtable+0x58 (exe+82cf70) (health*, msg*): same msg layout as HealthOnDamage
        ActorTable,         // .data: [ActorTable] = entities inline, 0xC0 bytes each (GetActor: base + index*0xC0)
        ActorCount,         // .data: u32 number of slots
        GotoStateInit,      // BotStateGotoLocal vtable slot 2 (exe+1dd6660, this, arg): crashes if the bot has no MoverStandard
        // Screen fade (what ScreenFadeAction does, exe+205bb20): MsgSend(1, [FadeOutMsg or FadeInMsg], &{float seconds, u8 0}, 8)
        MsgSend,            // exe+2142440
        FadeOutMsg,         // .data qword: message type for fade-out
        FadeInMsg,          // .data qword: message type for fade-in
        TargetLookup,       // exe+1910fd0 (&TargetSys, out {u64, u32}, 0): the default message target (player) ScreenFadeAction falls back to
        TargetSys,          // .data exe+7a42f80
        AssetRelease,       // exe+1626940 bool (manager*, asset*): drops one reference ([asset+4], u16) that
                            // LoadAssetByHash added; frees the asset at 0 (2026-10-14)
        Count
    };

    constexpr int kDirect = INT_MIN;

    struct Native {
        Id id;                  // must equal the entry's index (checked at startup; the table is indexed by Id)
        const char* name;
        const char* pattern;
        int rel32_off;          // kDirect: the match itself is the address
        uintptr_t expected_rva;
        uintptr_t addr;         // resolved at runtime, 0 if not found
        int hits;
        int adjust = 0;         // kDirect only: address = match + adjust
        bool any_hit = false;   // generic pattern: accept whichever of the hits resolves to expected_rva
    };

    extern Native table[Count];

    // Scans all natives; returns number that resolved to their expected RVA.
    int ResolveAll();

    // Address if it resolved exactly where expected, otherwise 0.
    uintptr_t Get(Id id);

    // SDK's hardcoded HeroSystem for 4.630.0.0 (Tkachov/MSMR-Scripting).
    constexpr uintptr_t kHeroSystemRva = 0x5D9DD00;
}
