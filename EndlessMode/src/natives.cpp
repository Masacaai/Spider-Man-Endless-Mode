#include "natives.h"

#include "log.h"
#include "scan.h"

namespace natives {
    // Patterns: Tkachov's SDK (GetActor) and ScriptHookSMPC 4.0630 community build (decoded, used as a map only).
    Native table[Count] = {
        { GetActor, "GetActor",
          "44 8B 01 41 8B D0 C1 EA 14 81 E2 FF 07 00 00 74 ?? 41 81 E0 FF FF 0F 00 4B 8D 0C ?? 48 C1 E1 06 48 03 0D ?? ?? ?? ?? 44 3B 05 ?? ?? ?? ?? 73 ?? 0F B7 41 ?? 3B D0 74 ?? 33 C9 48 8B C1",
          kDirect, 0x15a0560 },
        { GameThreadTick, "GameThreadTick",
          "48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8D 8B 40 5A 00 00",
          12, 0x1f745f0 },
        { SpawnRaw, "SpawnRaw",
          "0F 29 8C 24 80 00 00 00 E8 ?? ?? ?? ?? 48 8B F8",
          9, 0x1f65170 },
        { LoadAssetByHash, "LoadAssetByHash",
          "E8 ?? ?? ?? ?? 8B 44 24 78 4C 8B CF 48 8B 54 24 58 4C 8B C3",
          43, 0x1626380 },
        { ActorAssetManager, "ActorAssetManager",
          "48 81 EC 60 01 00 00 33 ED 48 8D 0D ?? ?? ?? ??",
          12, 0x60421e0 },
        { CreateSyncObject, "CreateSyncObject",
          "0F 85 BB 03 00 00 49 8B CF E8",
          10, 0x1980a60 },
        { EntityInit, "EntityInit",
          "75 0F 48 8D 15 ?? ?? ?? ?? 48 8B C8",
          21, 0x159db20 },
        { SyncRegisterHandle, "SyncRegisterHandle",
          "75 0F 48 8D 15 ?? ?? ?? ?? 48 8B C8",
          61, 0x1986230 },
        { EntityIsSetUp, "EntityIsSetUp",
          "E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 75 16",
          9, 0x159da80 },
        { SyncLookup, "SyncLookup",
          "E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 75 16",
          25, 0x1f7b710 },
        { EntityAttach, "EntityAttach",
          "E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 85 C0 75 16",
          36, 0x159e590 },
        { HeroHandleData, "HeroHandleData",
          "85 FF 75 75 40 38 7E 34 74",
          24, 0x5d9de68 },
        { LoadAssetByName, "LoadAssetByName",
          "48 89 5C 24 08 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 40 49 8B F1 49 8B D8 48 8B FA 48 8B E9 48 85 D2 74 ?? 80 3A 00 74 ?? 48 8D 4C 24 58 E8",
          kDirect, 0x1626300 },
        { SyncSlotTable, "SyncSlotTable",
          "0F 85 BB 03 00 00 49 8B CF E8",
          -9, 0x7a65f00 },
        // The next four patterns match twice; the first hit (the one Script Hook uses) gives the expected target.
        { SyncActivate, "SyncActivate",
          "B2 01 48 8B CE E8 ?? ?? ?? ?? EB 16",
          6, 0x19877e0 },
        { SyncFlag2, "SyncFlag2",
          "B2 01 48 8B CE E8 ?? ?? ?? ?? EB 16",
          29, 0x1980e90 },
        { SyncFinish1, "SyncFinish1",
          "41 FF 52 28 B2 01 48 8B CB E8",
          10, 0x1987860 },
        { SyncFinish0, "SyncFinish0",
          "41 FF 52 28 B2 01 48 8B CB E8",
          25, 0x1986b90 },
        { DeleteSyncObject, "DeleteSyncObject",
          "66 45 89 88 18 01 00 00 0F B7 91",
          kDirect, 0x1981690, 0, 0, -0x37 },
        { EntityDestroy, "EntityDestroy",
          "48 85 C0 74 09 48 8B 48 08 E8 ?? ?? ?? ?? 48 8D 4B 64",
          10, 0x159d500 },
        { ConvertStringToHash, "ConvertStringToHash",
          "74 35 48 BA 42 0F 87 D7 95 57 6C C9",
          kDirect, 0x1bb25d0, 0, 0, -0x14 },
        // Asset-manager registration (vtable store); matches once per manager, so pick by expected RVA.
        { ZoneAssetManager, "ZoneAssetManager",
          "48 8D 44 24 30 48 89 05 ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ??",
          22, 0x621d900, 0, 0, 0, true },
        { ProxSystem, "ProxSystem", "48 8B 53 60 48 8D 0D ?? ?? ?? ?? 41 B1 02 44 8B 00 E8 ?? ?? ?? ??", 7, 0x722aa80 },
        { ProxEnable, "ProxEnable", "48 8B 53 60 48 8D 0D ?? ?? ?? ?? 41 B1 02 44 8B 00 E8 ?? ?? ?? ??", 18, 0xd3bfd0 },
        { ProxDisable, "ProxDisable", "C6 41 68 00 E8 ?? ?? ?? ?? 45 33 C0 48 8D 0D ?? ?? ?? ?? 8B 10 E8 ?? ?? ?? ??", 22, 0xd3bc10 },
        { ProxStop, "ProxStop", "BA E2 F5 7C B5 EB 11 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??", 15, 0xd3bdf0 },
        { ProxReset, "ProxReset", "81 FA 2C 5E C9 CE 0F 85 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ??", 20, 0xd3b710 },
        { ProxClearTiers, "ProxClearTiers", "E8 ?? ?? ?? ?? 8B 54 24 38 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? BA E2 F5 7C B5", 1, 0xd3bbe0 },
        { ProxPushTier, "ProxPushTier", "E8 ?? ?? ?? ?? 8B 54 24 38 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? BA E2 F5 7C B5", 17, 0xd39ea0 },
        { HealthSet, "HealthSet",
          "40 53 48 83 EC 30 48 8B 01 48 8B D9 0F 29 74 24 20 0F 28 F1 FF 50 30 84 C0 75 ?? 48 8D 4B 18 E8 ?? ?? ?? ?? 48 85 C0 0F 85 ?? ?? ?? ?? 0F 57 C9 F3 0F 5F F1 F3 0F 5D B3 80 00 00 00",
          kDirect, 0x1f16fc0 },
        { DmgSys, "DmgSys", "48 8D 0D ?? ?? ?? ?? C7 44 24 28 FF FF FF FF F3 0F 11 45 ?? F3 0F 11 4D ?? 48 89 44 24 20 E8 ?? ?? ?? ??", 3, 0x62a4ec0 },
        { DmgCreate, "DmgCreate", "48 8D 0D ?? ?? ?? ?? C7 44 24 28 FF FF FF FF F3 0F 11 45 ?? F3 0F 11 4D ?? 48 89 44 24 20 E8 ?? ?? ?? ??", 31, 0x1eb6e40 },
        { DmgCommit, "DmgCommit", "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 F6 0F 84 C0 00 00 00", 8, 0x1eb7c60 },
        { HealthOnDamage, "HealthOnDamage",
          "48 89 74 24 10 57 48 83 EC 20 48 8B F9 48 8B F2 48 8B 49 60 48 85 C9 75 ?? 45 33 C0 48 89 5C 24 30 48 8D 15 ?? ?? ?? ?? B9 C0 04 00 00",
          kDirect, 0x1f15c30 },
        // HeroPositionTriangulatedEvent sender at exe+4871fe.
        { EventSys, "EventSys", "48 8D 0D ?? ?? ?? ?? 48 89 6C 24 60 41 81 E0 FF 07 00 00 48 89 6C 24 58 25 FF FF 0F 00 F3 0F 11 74 24 50 BA CA 8A 43 2C F3 0F 11 74 24 48 C6 44 24 40 01 89 6C 24 38 41 C1 E0 14 48 89 6C 24 30 44 0B C0 40 88 6C 24 28 C7 44 24 20 01 00 00 00 E8 ?? ?? ?? ??", 3, 0x7475690 },
        { SendEvent, "SendEvent", "48 8D 0D ?? ?? ?? ?? 48 89 6C 24 60 41 81 E0 FF 07 00 00 48 89 6C 24 58 25 FF FF 0F 00 F3 0F 11 74 24 50 BA CA 8A 43 2C F3 0F 11 74 24 48 C6 44 24 40 01 89 6C 24 38 41 C1 E0 14 48 89 6C 24 30 44 0B C0 40 88 6C 24 28 C7 44 24 20 01 00 00 00 E8 ?? ?? ?? ??", 81, 0x16d5e80 },
        { GroupAlert, "GroupAlert", "41 B0 01 48 8B CF 8B 10 E8 ?? ?? ?? ??", 9, 0x496b40, 0, 0, 0, true },
        { ResolveRef, "ResolveRef", "8B 11 8B CA C1 E9 14 85 C9 74 ?? 81 E2 FF FF 0F 00 8B C2 48 C1 E0 04 48 03 05 ?? ?? ?? ?? 3B 15 ?? ?? ?? ??", kDirect, 0x16798f0 },
        { RefTable, "RefTable", "8B 11 8B CA C1 E9 14 85 C9 74 ?? 81 E2 FF FF 0F 00 8B C2 48 C1 E0 04 48 03 05 ?? ?? ?? ?? 3B 15 ?? ?? ?? ??", 26, 0x7a44320 },
        // Script Hook's GetPlayerHudMessage pattern (imul by the 0x5F8 element size, then lea of the array).
        { HudMessages, "HudMessages", "48 69 C3 F8 05 00 00 48 8D 0D ?? ?? ?? ?? 48 03 C1", 10, 0x6d28f80 },
        { HudShow, "HudShow",
          "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 20 55 41 54 41 55 41 56 41 57 48 8D A8 A8 FB FF FF 48 81 EC 30 05 00 00 0F 29 70 C8 0F 28 F3 4D 8B F8 48 63 DA 48 8B F9 4D 85 C0 0F 84 ?? ?? ?? ?? 83 FB 15",
          kDirect, 0x7793d0 },
        { HudClear, "HudClear",
          "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 57 48 81 EC 90 00 00 00 41 0F B6 E9 48 63 DA 48 8B F9 83 FB 08 7E 0C 8D 43 F5 83 F8 09",
          kDirect, 0x77a1e0 },
        { HeroOnDamage, "HeroOnDamage",
          "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ?? ?? ?? ?? 0F B6 F0 84 C0 74 ?? 4C 8B 03 48 8B CB 41 FF 50 30 84 C0 74 ?? F3 0F 10 05 ?? ?? ?? ?? 0F 2F 87 44 01 00 00",
          kDirect, 0x82cf70 },
        { ActorTable, "ActorTable", "44 8B 01 41 8B D0 C1 EA 14 81 E2 FF 07 00 00 74 ?? 41 81 E0 FF FF 0F 00 4B 8D 0C 40 48 C1 E1 06 48 03 0D ?? ?? ?? ?? 44 3B 05 ?? ?? ?? ??", 35, 0x7a44380 },
        { ActorCount, "ActorCount", "44 8B 01 41 8B D0 C1 EA 14 81 E2 FF 07 00 00 74 ?? 41 81 E0 FF FF 0F 00 4B 8D 0C 40 48 C1 E1 06 48 03 0D ?? ?? ?? ?? 44 3B 05 ?? ?? ?? ??", 42, 0x7a4439c },
        { GotoStateInit, "GotoStateInit", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 0F 29 74 24 30 48 8B FA 48 8B D9 E8 ?? ?? ?? ?? F2 0F 10 47 30 48 8D 4F 24 F2 0F 11 83 1C 01 00 00", kDirect, 0x1dd6660, 0, 0, 0, true },
        { MsgSend, "MsgSend", "C6 43 65 01 48 8D 44 24 50 48 8B 15 ?? ?? ?? ?? B9 01 00 00 00 C7 44 24 28 08 00 00 00 48 89 44 24 20 E8 ?? ?? ?? ??", 35, 0x2142440 },
        { FadeOutMsg, "FadeOutMsg", "C6 43 65 01 48 8D 44 24 50 48 8B 15 ?? ?? ?? ?? B9 01 00 00 00 C7 44 24 28 08 00 00 00 48 89 44 24 20 E8 ?? ?? ?? ??", 12, 0x7c50c80 },
        { FadeInMsg, "FadeInMsg", "C6 43 65 00 48 8D 44 24 50 48 8B 15 ?? ?? ?? ?? B9 01 00 00 00 C7 44 24 28 08 00 00 00 48 89 44 24 20 E8 ?? ?? ?? ??", 12, 0x7c50c78 },
        { TargetLookup, "TargetLookup", "45 33 C0 48 8D 54 24 30 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B 44 24 30 44 8B 4C 24 38", 16, 0x1910fd0, 0, 0, 0, true },
        { TargetSys, "TargetSys", "45 33 C0 48 8D 54 24 30 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B 44 24 30 44 8B 4C 24 38", 11, 0x7a42f80, 0, 0, 0, true },
        { AssetRelease, "AssetRelease", "48 89 5C 24 08 57 48 83 EC 20 8B 05 ?? ?? ?? ?? 48 8B DA 48 83 79 40 00 48 8B F9 74 ?? 48 85 D2 74 ?? 48 3B 91 88 00 00 00", kDirect, 0x1626940, 0, 0, 0, true },
    };

    int ResolveAll() {
        // The table is indexed by Id; an entry out of place would hand callers the wrong address (it did once:
        // the fade call jumped into data). Refuse to resolve anything if the order is off.
        for (int i = 0; i < Count; i++) {
            if (table[i].id != i) {
                LOG("natives: table entry %d (%s) has id %d; table order is broken, nothing resolved", i, table[i].name, (int)table[i].id);
                for (auto& n : table) n.addr = 0;
                return 0;
            }
        }
        int ok = 0;
        for (auto& n : table) {
            auto hits = scan::FindAll(n.pattern, n.any_hit ? 64 : 4);
            n.hits = (int)hits.size();
            n.addr = 0;
            if (hits.empty()) {
                LOG("  %-20s NOT FOUND", n.name);
                continue;
            }
            auto resolve = [&](uintptr_t hit) {
                return n.rel32_off == kDirect ? hit + n.adjust : scan::Rel32Target(hit, n.rel32_off);
            };
            uintptr_t a = resolve(hits[0]);
            if (n.any_hit) {
                for (uintptr_t hit : hits) {
                    if (scan::Rva(resolve(hit)) == n.expected_rva) { a = resolve(hit); break; }
                }
            }
            uintptr_t rva = scan::Rva(a);
            bool match = rva == n.expected_rva;
            if (match) {
                n.addr = a;
                ok++;
            }
            LOG("  %-20s hits=%d  exe+%llx  %s", n.name, n.hits, (unsigned long long)rva,
                match ? "OK" : "MISMATCH (disabled)");
        }
        return ok;
    }

    uintptr_t Get(Id id) {
        return table[id].addr;
    }
}
