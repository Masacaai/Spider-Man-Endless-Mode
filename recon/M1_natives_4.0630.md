# M1 recon: game natives on Spider-Man.exe 4.0630.0.0

Date: 2026-10-12. All addresses are `Spider-Man.exe`-relative (ImageBase 0x140000000) and valid for **4.0630.0.0 only**.

## Method

1. `dumpbin /disasm` on the 4.0630 community build of `ScriptHookSMPC.dll` (SHA256 `31CE8901…663D`, Nexus 6167).
2. Script Hook stores each signature as an `int32[]` in `.rdata` (byte value, or `-1` = wildcard), copies it into a vector, then either
   - `resolve(pattern, off)` @ `0x180039780`: returns `match + off + 4 + rel32(match + off)` (rip-relative call/lea), or
   - raw scan @ `0x180039600`, then decodes several rel32s inline from the one match.
3. `tools/sigtools/decode_sh.ps1` replays that stack-building code to recover the patterns; `tools/sigtools/sigscan.cpp` checks each pattern against the exe on disk and computes the target.
4. Cross-check: Script Hook's `g5A1B0` target equals the match of Tkachov's SDK `GetActor` signature (`exe+15a0560`), and `g5A1E8` is `HeroSystem + 0x168` versus the SDK's hardcoded `0x5D9DD00`. So the decoding is correct.

Script Hook is used **only as a map**. We never load or call it.

## Resolved natives

`hits` = number of matches in `.text`; 2 means Script Hook silently takes the first, so our own signatures should be longer.

| SH global | Used by export(s) | Target | hits | Notes |
|---|---|---|---|---|
| 5A1F0 | SpawnActor worker | **exe+1f65170** | 1 | raw spawn: `(rcx=ctx from SpawnActor arg1, edx=0, r8=Transform* 0x40 bytes)` → actor/entity* |
| 5A1A8 | SpawnActor worker | exe+159db20 | 1 | called on new entity if `!(byte[ent+8] & 4)` (init?) |
| 5A190 | SpawnActor worker | exe+1986230 | 1 | `(syncObj, &handle)`: registers handle |
| 5A230 | SpawnActor worker | exe+159da80 | 1 | `(ent)` → nonzero = already set up |
| 5A200 | SpawnActor worker | exe+1f7b710 | 1 | `(short [sync+0x114], byte [sync+0x11D])` |
| 5A228 | SpawnActor worker | exe+159e590 | 1 | `(ent, result of 5A200)` |
| 5A1F8 | SpawnActor worker | exe+19877e0 | 2 | `(syncObj, 1)` if flags&8: activate? |
| 5A208 | ? | exe+1980e90 | 2 | |
| 5A220 / 5A218 | ? | exe+1987860 / exe+1986b90 | 2 | |
| 5A178 | CreateSyncObject | exe+1980a60 | 1 | |
| 5A160 | CreateSyncObject, GetAvailableSyncManagerIndex, GetEntitySyncObject | match exe+19825fb | 1 | direct (no rel32) |
| 5A170 | DeleteActor, DeleteSyncObject | match exe+19816c7 | 1 | direct |
| 5A150 | LoadAssetByHash, LoadAssetByName | exe+1626380 | 1 | called `(rcx, rdx, 0, 0, 0,0,0)` |
| 5A158 | GetActorAssetManager | exe+60421e0 (.data) | 1 | the actor asset manager object |
| 5A130 | ConvertStringToHash | match exe+1bb25e4 | 1 | direct |
| 5A1B0 | GetEntityFromHandlePointer, GetPlayerHero | exe+15a0560 | 1 | == SDK GetActor |
| 5A1E8 | GetPlayerHero(HandlePointer) | exe+5d9de68 (.data) | 1 | HeroSystem + 0x168 |
| 5A140 | GetLoadoutRewardConfig | exe+cc1610 | 1 | |
| 5A148 | GetProgressionSystem | exe+70957c0 (.data) | 1 | |
| 5A138 | GetGameTimer | exe+5d6e7ec (.data) | 1 | Script Hook subtracts 8 |
| 5A1C0 / 5A1C8 | (HUD?) | exe+7793d0 / exe+77a1e0 | 1 | |
| 5A198 / 5A1A0 | ? | exe+159c9c0 / exe+159d3a0 | 1 | |
| 5A1B8 | ? | exe+159d500 | 1 | |
| 5A180 | ? | exe+1749970 | 1 | `jmp` tail call |
| 5A2C0 | ? | n/a | **0** | did not match on 4.0630; decoder may be off for this one |

## SpawnActor flow (from Script Hook's disassembly)

Export `SpawnActor(rcx=ctx, rdx=Vector3* pos, xmm2=heading)` stores its args, queues a lambda onto Script Hook's **game-thread** queue, and polls with `Sleep(10)` until the result is set. On the game thread:

1. Build a 0x40-byte Transform: rotation from heading (sin/cos), scale 1.0 at +0x14/+0x3C, position at +0x30.
2. `ent = spawn(ctx, 0, &transform)` (exe+1f65170).
3. `sync = CreateSyncObject()`; build handle = `(word[ent+0x10] & 0x7FF) << 20 | (dword[ent+0xC] & 0xFFFFF)`. This is the same handle layout the SDK's `GetActor` decodes.
4. If `!(byte[ent+8] & 4)`: call 5A1A8(ent). Then 5A190(sync, &handle).
5. If 5A230(ent) == 0: 5A228(ent, 5A200(word[sync+0x114], byte[sync+0x11D])).
6. If flags&8 and !(flags&4): 5A1F8(sync, 1).

## Resolved after the first pass

- **Spawn ctx** = the asset returned by `LoadAssetByHash(&ActorAssetManager, hash64, 0, 0, 0, 0, 0)`. The asset is loaded when `byte [asset+1] == 1` (Script Hook's `WaitForAssetToLoad` polls exactly this).
- **LoadAssetByName** = `exe+1626300`: `(manager, const char* path, r8, r9, a5, a6)`. It hashes the path with `exe+1bb25d0` (ConvertStringToHash, `(u64* out, const char*)`), then calls ByHash with r9 = path. We call ByName with zeros.
- **Transform** (0x40, 4 rows × 16 B): `[0]=sin θ, [8]=-cos θ, [0x14]=1, [0x20]=cos θ, [0x28]=sin θ, [0x30..0x38]=pos, [0x3C]=1`, with θ = heading/180·π. Everything else is 0.
- **CreateSyncObject** (`exe+1980a60`) takes a 16-byte descriptor: `dword 0, word slot, 0…`. `slot` is the first null entry of the 0x2EE0-entry table at `[exe+7a65f00]`.
- **Sync object starts with the actor handle**, so `GetActor(sync)` returns its entity. DeleteActor finds the sync this way.
- **Despawn** (Script Hook DeleteActor): find sync → `DeleteSyncObject(sync)` (`exe+1981690`) → on the game thread `EntityDestroy(entity)` (`exe+159d500`).
- After `SyncActivate`, Script Hook also calls: if `byte[sync+0x11F] & 2`, then `SyncFlag2(short[sync+0x114])` (`exe+1980e90`); then `SyncFinish1(sync, 1)` (`exe+1987860`) and `SyncFinish0(sync, 0)` (`exe+1986b90`).
- Script Hook's direct-match resolver (`0x1800396D0`) returns `match + off`, not a rel32. Its offsets are negative (`-0x37`, `-0x14`).

## Game thread

Script Hook hooks **`exe+1f745f0`** (its global `5A2C0`; it is called from `exe+1f731a1`). The detour pops one queued job per call, then jumps to the original, preserving only rcx. On 4.0630 the signature `48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8D 8B 40 5A 00 00`, rel32 at +12, matches once. ArkWeb's `exe+16244c0` is an alternative, not yet checked.

Script Hook also hooks LoadAssetByHash, CreateSyncObject and `exe+1749970` (its global `5A180`). Its 4.0630 crash fix was in the LoadAssetByHash detour (the truncated name pointer).

## Correct spawn sequence (from jedijosh920's Mod Menu, `scripts\SMPCModMenu.dll`)

The Mod Menu resolves Script Hook exports with `GetProcAddress`. Its menu spawn path is:
`hash = ConvertStringToHash(path)` → `asset = LoadAssetByHash(GetActorAssetManager(), hash)` → `WaitForAssetToLoad(asset)` → `SpawnActor(hash, &pos, heading)`.
So **SpawnRaw's first argument is the u64 asset hash**, not the asset pointer. The asset only has to be loaded first.

## Enemy Generator (Mod Menu minigame) = prior art for our director

Every tick: if fewer than 10 thugs are tracked, spawn `character\enemy\enm_meleethug\enm_meleethug.actor` at a random angle and radius around the hero. For each tracked thug: `GetComponentByName(entity, "BotHealth")`, and **if `float [BotHealth+0x88] <= 0`, the thug is dead → `DeleteActor`**. Thugs that wander too far are also deleted. Spawned thugs evidently fight (that is the minigame's point), so no faction/aggro call is needed.

## Entity layout (from Script Hook GetComponentByName / GetTransform / GetPosition)

- `[entity+0x00]` Transform*. Position is at `+0x30` (`+0x70` is another vec3, maybe the previous position).
- `[entity+0x68]` component array of 16-byte entries `{ desc*, instance* }`; `[entity+0x70]` int16 count.
- Component type name = `(char*)[desc+0x60]`. Known names: `BotHealth`, `HeroHealth`, `TargetData`, `HeroStateRunLocal`, `HeroSpiderAssaultManager`, `HeroCharacterManager`, `CombatCamera`…

## Arena leads (M7)

- Mod Menu "Enter Peter's Apartment (Load Zone Interior)" loads `levels\i20_city\instance\peter_apartment\...\*.zone` by path. If the prison is an instance, the same mechanism could load it. Its zone paths are unknown; they aren't in Models.txt/Actors.txt.
- Mod Menu also has "Load Mission/Objective" (Previous/Next Mission). Its implementation hasn't been read yet.
- Prison assets exist: `environment\building_kits\prison\*`, `actors\design_wall_actor\gp_a2_prison_cellblock_door.actor`, `actors\temp_sail_boat_hull_only\gp_a2_prison_boat.actor`, `characters\enemy\enm_thug_raft_*`.

## The Raft (prison-break arena), found offline

Tool: `tools/toctool` parses `asset_archive\toc` (zlib → DAT1) and extracts assets from DSAR archives (LZ4 blocks). Hash = ALERT's CRC64 on a lowercase, forward-slash path, verified against the wiki vector and real zones.

- `levels/i20_city/i20_city.level` (17.9 MB) lists every zone/region path (12338 zone strings).
- The prison is instance **`levels/i20_city/instance/Prison_Interior/`**. Its regions are `Prison_Interior`, `Prison_Raft_Exterior` and `Prison_Raft_HeliCrash` (region #791 of 1843 in the level's region table). It has 21 zones, all present in the toc's zone-key section (`6D921D7B`). The list is in `EndlessMode/src/arena.cpp`, plus the gameplay zones `Raft_Int_Gameplay`, `Raft_Ext_Gameplay` and `GP_A2_PRISON_HeliCrash_Gameplay`.
- **Location:** actor records in `Raft_Int_Gameplay.zone` (0x140 stride) sit at x 1350–1395, z −1175…−1243, y = 39 and 45 (two cell-block tiers). Instances sit in far map corners: the Mod Menu's Peter's-apartment teleport is (1311.1, 44.6, 1220.8).
- **Zone manager** = `exe+621D900` (static object; vtable written at `exe+1c21a3`; 117 code xrefs). It's the same registration pattern as `ActorAssetManager` (`exe+60421e0`). The Mod Menu loads interiors with `LoadAssetByHash(&ZoneManager, hash)`, sleeps 3 s, then calls SetPosition.
- Mission overlays: `levels/i20_city/overlay/StoryMission/GP_A2_PRISON/GP_A2_PRISON.region`, `GP_A2_PRISON_PRE`, `GP_A2_PRISON_INTRO_TRANSITION`, `GP_A2_PRISON_RYK_EXIT_CIN`.
- **Fallback arenas:** open-world prisoner faction bases `OM_BASE_PRISONER_{FINANCIAL_DIST,CHINATOWN,CLINTON,HARLEM}` (5-wave hideouts with prison enemies; the exe has `OnFactionBasePrisonerWave1..5Start`).

## In-game results (2026-10-12, first session)

- All 22 natives resolved in-process. The hook on `exe+1f745f0` runs about 92 times/s (per frame), and game-thread jobs work.
- Hero: HeroHealth 208/208 read correctly. The hero has 215 components.
- Spawn works: `enm_prison_meleethug` reads BotHealth 170, and the thug fights. **Heading convention confirmed**: the transform's row 2 is forward, so heading = atan2(dz, dx) in degrees faces the target.
- Defeated (webbed) enemy reads BotHealth 0.0. Despawn works.
- Wave director ran waves 1–6 in Times Square, 32 kills, no crash.
- Issues: (1) spawned bots don't aggro until they see the hero; (2) some enemies go down in one hit (suspect `enm_melee_fighting_inmate`; per-type start health is now logged).

## Aggro leads (not done)

- A conduit/script node with inputs `ForceAggro` / `ForceRelaxed` / `UseDefault` (outputs `OnForceAggro`…), next to `LockHeroInput` nodes. The strings are only referenced from a .data table at exe+6008490..558. It may be the hero's combat stance, not bot alerting.
- Events: `ScriptedGroupAlertEvent` (built at exe+1317330 and exe+1318800), `BotAlertGroupEvent`, `GadgetAlertEnemiesEvent`, `BotAlertedEvent`. Config text: "Amount of aggro to force on the engagement", `ForceAggroState`.
- Bot components: `SkirmishAwareness` (`HandleAwarenessLevelChangedEvent`), `TieredAggroTargetingGame`, `GameAwarenessDB`, `TargetingStimulusListenerGame`.

## Ryker's Island (the arena) — 2026-10-12

- The Raft interior loads (8+6 zones) with working collision and nav, and a thug fights there. But it's unlit (region activation?), has an invisible blocker wall at about x 1358–1360, and the user doesn't want an indoor arena.
- **Ryker's yard is part of the open world** (sound zone `Tile_U19/AUD_Exterior_Prison_SFX.zone`). The user teleported there at (1255, 4, -815) and it's lit and complete. No zone loading is needed.
- Mission checkpoints: `CHK_PRISON_RYK_{ARRIVAL,YARD_FIGHT,ROOFTOP,ARMORY,DOCKS_CIN,INTERSECTION,HELI_TAKEDOWN_CIN,WARDEN_CIN,SAVEGUARD,EXIT_CIN}`. Mission overlay zones: `overlay/StoryMission/GP_A2_PRISON/GP_A2_PRISON_{Main,EXT_GAMEPLAY,atmosphere,Power,...}.zone`.
- Yard bounds from ground-level placements in `GP_A2_PRISON_EXT_GAMEPLAY.zone` (middle 90%): x 1202–1304, z −859…−745, ground y ≈ 1. There's a raised east section at y ≈ 10 (x ≈ 1333). Walk logs: `recon/rykers_walk_1.csv`, `recon/raft_walk_1.csv`.
- Yard roster in that zone adds `enm_riflethug\enm_thug_prison_rifle`, `enm_sniper_prison` and `enm_prison_pistolthug`.

## Bosses / factions (from boss-fight gameplay zones)

Spawnable boss actors (standalone fightability untested): `character\enemy\enm_meleethug\{rhino,scorpion,docock,enm_tombstone,enm_tombstone_chain,enm_kingpin_boss,enm_mechahammerhead}.actor`, `character\enemy\villains\shocker.actor`, `characters\enemy\enm_Silver_Sable_Craft\enm_Silver_Sable_Craft.actor`. Vulture and Electro appear only as npc/chase variants (scripted aerial fights).
Faction counts in Mod Menu Actors.txt: street ~7, Fisk ~20, demons 16, prisoners 20, Sable 18, Hammerhead/Maggia ~30, Olympus (hammerhead02/03, jetpack, mecha) ~23.

## Aggro: best lead

The spawner has `SunsetSpawnerFactoryInitialTargets` ("Initial Targets"), so hideout reinforcements are likely spawned with a target already set. Plan: `observe on` during a hideout replay, take the `caller=exe+…` of those spawns, and disassemble what the spawner calls after SpawnRaw. `ScriptedGroupAlertEvent` refs at exe+1317330 are only GetTypeName stubs.

## Aggro state: live memory diff (2026-10-12)

Test: a prison thug spawned facing away; two snapshots while unaware (A1, A2); the user walks into view, the bot engages; two snapshots while aware (B1, B2). Hero handle was `00108667`. Fields that went 0 → hero handle when aware:

| Component | Offset | Other changes |
|---|---|---|
| TieredAggroTargetingGame | +0x78 | +0x60: 0→0x00010000, +0x64: 0→45.0f (aggro amount?), +0x6C: 0→0x2038, +0x70: 0→0x00010000 |
| GameAwarenessDB | +0x8C | (none else in the first 512 B) |
| SkirmishAwareness | +0x180 | +0x178 float changes constantly (timer) |
| BotLosChecker | +0x4C | +0x48: 2→0; +0x50/+0x54 become counters |
| CombatJobHost | +0xB4 | +0xB0: 2→0, +0xB8: 0→0x0020CD80 |
| SkirmishJobHost | — | +0x48: 0xE→0x6, +0xB0: 2→0 |

RTTI (tools/sigtools/rtti.cpp): `TieredAggroTargetingGame` TypeDescriptor exe+6bb8160, COL exe+558c808, **vtable exe+386e9c0** (28 methods, in exe+1df7xxx�1df9xxx (base) and exe+5a2xxx�5a3xxx). Constructors at exe+1df6550 and exe+1df7930 zero +0x78. The init at exe+1df7a30 sets defaults (+0x74 = 100.0, +0x80 = 50.0, +0x84 = 40.0, +0x88 = -1.0 double). The subrecords at +0x5C and +0x6C are built by exe+1f7b570, and +0x78 is the last dword of the +0x6C record, so the target is probably set by copying a whole record (no direct setter found).

Next: (1) quick hack — write TieredAggro/AwarenessDB fields on a fresh bot via a `poke` command and see what drives behavior; (2) proper fix — find the TieredAggroTargetingGame method that writes +0x78 (RTTI `.?AVTieredAggroTargetingGame@@` → vtable → methods) and call it.

## Aggro: what the data says (2026-10-12, later)

- Writing the "aware" fields directly fails: TieredAggroTargeting resets them every update. The GameAwarenessDB handle alone (it persists) does nothing.
- `empty_distraction.actor`, `combat_area_volume.actor` and `combatvolume.actor` spawned at the hero: no effect.
- **Hideout and crime enemies are not spawned via SpawnRaw.** They come from zone data (streamed by exe+159f394) and their encounters. Hideout waves spawn pairs of `actors\design_sphere\design_sphere_nocoll.actor` markers via the helper exe+638a10 (caller exe+638aba). Next step: backtraces on sync creation to find the encounter spawner.
- Enemy actors reference `configs\LineOfSightAwareness\LineOfSightAwareness.config` (decoded with ALERT: `recon/LineOfSightAwareness.config.json`), type BotConfigAwareness:
  - **IdleVision**: CylinderRadius 6 (360°), WedgeRadius 40 at half-angle 10°, SmallWedge 30 m at 60°. **EngagedVision**: Cylinder 80. This is why idle bots only notice you right in front.
  - Stimulus handlers: `HeroPositionTriangulatedEvent` → kConfirming (no radius); `DeathSeenEvent` → kConfirmed +25 aggro; Damage/Death within 10 m; `InvestigateStimulusEvent` investigate radius 200; `UserAlertEnemiesToEnviroInteractEvent` 50.
- Plan A: find the decoded IdleVision struct in memory (`vsearch 64 40 10 30 60 6`) and widen it while the mode runs. Plan B: fire `HeroPositionTriangulatedEvent`.
- Corrupted civilians (Mod Menu "zombies", "Hostile Pedestrians" mod) use `corruptedbotmelee.config` and the conduit `npc_Civ_Male_Corrupted` (VFX only).

## ProximitySpawnSystem: the engine's own enemy spawner (the aggro and pop-in fix)

Lead from MSM2's Chaos City 2 (VitorLFR): it re-enables the system used for the symbiote takeover and swaps its `ProximitySpawnerConfig`. MSMR has the same system, with only an empty default config (`configs/ProxSpawnConfig_Default`: MaxAlive 40, MaxSpawnRadius 50, no SpawnInfos), and nothing in the 397 overlay zones references a proximity config.

ProximitySpawnerConfig fields (exe reflection strings): `SpawnInfos[] { ActorTypeAsset, MaxAlive }`, `MaxAlive`, `MinSpawnRadius`, `MaxSpawnRadius`, `MinDeleteRadius`, `MinDelayBetweenSpawns`, `MaxDelayBetweenSpawns`, **`StartAggressive`**, `EncourageIdleWandering`, **`MinPercentOutOfView` / `MaxPercentOutOfView`**, `DefaultCleanUpType` (kConfigDefault, kDeleteInstant, kDeleteOffCamera, kDeleteOffCameraOutOfRange, kDeleteBehindCamOutOfRange, kDeleteNotRenderedOutOfRange, kReZoneToActivator), `ProximityTieredOverrides` (OverrideMinDelay/OverrideMaxDelay…). Spawn points come from nav/anim clues. System config: `DefaultSpawnConfig`, `AnimClueGatherRadius`, `NavClueGatherRadius`.

Runtime (`ProximitySpawnScriptAction` input handler exe+d357d0; vtable exe+3908990):
- System singleton **exe+722AA80** (state at +0x18, activator handle at +0x2C, tier list at +0x30, count at +0x44, flag at +0x1D).
- `Enable` exe+d3bfd0(sys, u64 config asset id — if the top bit is clear it uses the default config at [sys+0x1F20]+0x34, u32 activator handle, u8 2).
- `Disable` exe+d3bc10(sys, activator, 0); `Stop` exe+d3bdf0 (state 7→6); `Reset` exe+d3b710; `ClearTiers` exe+d3bbe0; `PushTier` exe+d39ea0(sys, tier).
- Input hashes: 862A994E enable, 89C6D606 disable, CEC95E2C reset, 5EB99EC7 stop.

**Crash 1 (2026-10-12 18:53):** `prox on` with config 0 caused an AV at exe+d3c030 in Enable: `rbx = [sys+0x1F20]` (system config data pointer) was null. The handle at +0x1F20 is {data*, asset* (+0x1F28), ids (+0x1F30), loaded (+0x1F38)}, released via config manager exe+6050D30 (unload exe+1626940). `ProximitySpawnSystem::Init` = vtable[1] exe+d3aaa0: hashes the path at exe+3909510, loads it via exe+1694460(&cfgMgr, hash, 0, exe+39094f8, 0), and registers the on-loaded callback exe+d3a850. It never ran in MSMR (or never completed). Fix: `prox peek` → `prox init` → `prox on` (now always passes an explicit config id and refuses to run while the data pointer is null). Other readers of +0x1F20: exe+d3a5e6, d3a686, d3bfb0, d3c49c, d3c7d7.

**Crash 2 (18:57):** calling Init (vtable[1]) by hand → AV writing to 0 inside memset (exe+363e6e9).
**Full archive scan (tools/toctool/grepall.cpp, all 214 archives):** only 3 assets mention the system: the system config (B6335BE782F4E591), the default spawner config (A990C09632715DF6), and the **BootConfig** (87F8A35C50556B8B, which preloads the system config). **No mission, zone or conduit in MSMR uses it.** The startup function that registers game systems (around exe+2bc3xx–2bc7ee; 118 entries, e.g. ItemSpawnerSystem object exe+5da0d60, HeroSystem 5d9dd00) does not include ProximitySpawnSystem (exe+722AA80, built by the dynamic initializer exe+128b80 → ctor exe+d399e0). The system is dormant engine code.
**PINNED 2026-10-12 (user decision):** probably not release-ready in 2018. To revisit: register it with the scheduler the way the list consumer does, or ask MSM2 modders (Chaos City 2 author) how it is enabled there.

Our config: `EndlessMode/data/prox_prison.json` → ALERT json2config → `build/EndlessMode_Spawner.stage` (replaces ProxSpawnConfig_Default). Script command: `prox on|off|stop|reset|tier n|state`.

## Aggro attempt 3: idle vision widened (failed) → damage-based aggro (in progress)

- `vsearch 128 40 10 30 60 6` (now guard-page safe; the first version crashed on a guard page, code 0x80000001) found 3 in-memory copies of IdleVision: window+0x30 = 0.75, **+0x34 CylinderRadius 6**, +0x38 / +0x3C height ±2, then wedge 40/10…, then EngagedVision 0.75 80 80 -80. Poked 6→50 and ±2→±20 in all copies: neither old nor freshly spawned bots noticed the hero. So the bots don't read these copies, or Acquire/Confirm timing gates detection.
- **Next (user chose option 2):** make bots aware via the game's damage path (LineOfSightAwareness: DamageEvent → kConfirming/kConfirmed, aggro +15). BotHealth RTTI vtable exe+3839e98 (base Health methods in exe+1f15xxx). Writes to health +0x88 happen in: exe+1f15740 (init: sets +0x88/+0x8C from config), **exe+1f16fc0** (modifies health; calls vtable+0x30, exe+1984370, exe+1676b90, exe+1f16f60), and exe+1f17220 (sets max/current). Plan: hook these in game, have the user punch a bot, log the backtrace to find the upstream ApplyDamage(target, DamageInfo{attacker...}), then call it with 1 damage from the hero at spawn. Fallback if unreliable: LOS-checked spawn points (user dislikes it).

## Aggro attempt 4: damage pipeline (2026-10-12 evening) — inconclusive, parked

- Damage system singleton exe+62A4EC0. `DmgCreate` exe+1eb6e40(sys, &target_handle, &pos, &vec, &dir, -1) → msg (record+0x60; failure returns sys+0x123868). `DmgCommit` exe+1eb7c60(sys) dispatches the newest record. Melee caller exe+69be20 (create at 69c107, commit at 69c283). Attack-data fill exe+8cdf60: msg+0x138 attacker handle, +0x13C/+0x140 category, +0x144 damage, +0x150, +0x154, +0x168 attack id, +0x170/+0x180 lists, +0x19C hash, flag bits at +8/+0x18 (0x800 = +0x144, 0x8000 = +0x154, 0x100000 = +0x168, 0x400000 = +0x180, 0x4000000 = +0x19C).
- `Health::SetHealth` exe+1f16fc0 (clamp + store +0x88). `Health::OnDamage` exe+1f15c30 (vtable+0x58; copies msg, applies it, accumulates +0x144 into +0x98). Damage apply caller exe+1ed1540 (calls OnDamage at 1ed171e).
- Results: our hit (0 dmg) pushes the bot; real damage 2–4 HP applies; neither causes aggro, even at 5 m; copying attack fields from a real punch doesn't either. A real web from behind did aggro, but web mechanics or the user's faster-web mod could explain it. Wholesale replay of a web message crashed (jump to 0x3, via stale list pointers).
- Conclusion: hostility comes from sight; damage only makes an idle bot suspicious (config: kConfirming).

## Hideout wave spawning (backtrace observer, 2026-10-12)

All hideout sync creations (1575) come from one chain: CreateSyncObject ← exe+1f02284 ← 1f01c3a ← 1f027c2 ← 1f009b3 ← 72a7c9 ← 1fb7722 ← main loop. The wave marker spheres come via exe+638a10 ← 1f7984a ← 1982634 ← 1f02284 (same chain). So enemies are instantiated by a generic **actor-activation system** (exe+1f00xxx–1f02xxx) in its own update; the wave logic only queues requests. Next: `bots` + `comps 0x<entity>` on a live hideout enemy vs one of our spawns, to diff components; then find the request/queue side.

## Hideout enemies vs ours: component diff (live, 2026-10-12 21:55)

`bots` found 17 hideout bots. Comparing a live, engaged hideout enemy (136 components) with our spawned prison thug (130):
- **Only on the hideout enemy:** `BotAreaActor`, `BotAreaActorGroup` ×2 (structural), plus combat-state behaviours (`BehaviorMeleeGame`, `BehaviorGotoGame`, `FlankingDestinationModifierGame`, `AimAtPointsTarget`, `CombatCameraFramingTarget`) and `BotLootDrop`.
- **Only on ours:** `BehaviorSingleState`, a couple of `BotComboCondition*` (enemy type / idle state).
- Component layout (0x60 stride): +0x00 vtable, +0x08 owner entity, +0x10 1.0f, … **+0x48 actor handle**: BotAreaActor → 0x206360, BotAreaActorGroup → 0x20636B (both groups). So hideout enemies are bound to **area/group actors placed in the hideout zone**. Hypothesis: the group gets alerted (BotAlertGroupEvent / GroupAlertedEvent / "Alert Group"), so all members (and later waves) know the hero.
- Next: `actor h20636b` → `comps 0x<entity>` on the group actor; find its alert state and member list; then make our spawns members of an alerted group (or spawn our own group actor).

## Event system and bot groups (offline RE, 2026-10-12 night) — untested in game

- **Event type hash** = ALERT `dat1lib.crc32.hash(class name, False)`. Verified: ScriptedGroupAlertEvent 3A57F5AD, BotAlertGroupEvent F9C1D45E, BotAlertedEvent D8ABB208, GroupAlertedEvent 0FD8F64B, HeroPositionTriangulatedEvent 2C438ACA, BotHeardLoudSoundEvent 3D05C385, DeathSeenEvent 3E9BA834, WeaponFiredEvent F6607648, HeroSetAlertedEvent 80B26DFF, BunkerBustedEvent 3CFD90F1, DamageEvent A37B9229.
- **SendEvent** exe+16d5e80 (from the HeroPositionTriangulatedEvent sender at exe+4871fe): `(&EventSys exe+7475690, type, target_handle, u32* source_handle|null, int 1, u8 0, u64 0, u32 0, u8 1, float, float, payload*, size)` → event* or 0. Payload fields live from event+0x130. The BotAlertGroupEvent sender (exe+345e09) passes source = null, a5 = 0, then calls Group::Alert(group, own handle, 1).
- **Group::Alert** exe+496b40 `(group*, u32 alerter, u8 flag)`: global gate byte exe+70a405d (nonzero → no-op); resolves the alerter actor; resolves the ref at group+0x4AC and calls exe+48dba0 on it; loops members `[group+0x50+i*8]`, count u32 `group+0x450` (max 128 = fixed array); record u32 +0x10 == 2 → bot, handle at +0x70; sends GroupAlertedEvent to each.
- **Weak-ref resolver** exe+16798f0 `(u32* ref)`: index = low 20 bits, generation = high 12; entries 16 bytes `{object*, u32 gen}` at `[exe+7a44320]`, count int32 `[exe+7a44340]`. (Separate from the actor table GetActor uses.)
- **Group lookup** exe+487c20(obj): ref obj+0xB4 → X; exe+642420(X): ref X+0x188 → Y; `[Y+0x58]` → Z; ref Z+0x1C → group. Callers of 487c20 not yet identified, so the `groups` command instead scans the whole weak-ref table for objects that have the member-array shape and lists the ones containing a given bot.
- No "add member" function found yet: the only `inc [reg+0x450]` is exe+1d6bda2 (unrelated region). Membership probably comes from BotAreaActorGroup registration at spawn. **Open.**
- Tooling in EndlessMode (commands): `sendevent <target> <Event|hex> [source]`, `groups [bot]`, `group <0xG>`, `grpalert <0xG|bot> [alerter]`; `bots` now prints `h<handle>` plus `area=`/`grp=` handles.

## Event/group tests in game (2026-10-12, 22:24–22:37)

- `sendevent` to our spawned idle thug (actor handle, source hero): GroupAlertedEvent, BotAlertedEvent, HeroPositionTriangulatedEvent, BotHeardLoudSoundEvent, HeroSetAlertedEvent, ScriptedGroupAlertEvent → events created (non-null), **no reaction**. No native bots near Ryker's to compare.
- Hideout (stealth, wave 1, 12 bots, all `grp=0040d3c4`, `area=0040d3b6`): the scan finds group object `0x1bea03d1140`, count 12. Its "member" ids (e.g. 00405f1c) are **not actor handles**: they are weak refs to a component on each bot (component: +0 vtable, +8 owner entity, +0x14 own ref).
- **`grpalert` (Group::Alert, alerter = hero) → all 12 went into SEARCH** ("looking around", don't know where the hero is). No crash. First working aggro lever.
- HeroPositionTriangulatedEvent then sent to all 12 actor handles, and separately to all 12 component refs → still searching.
- After the hero was really spotted: count +0x450 went 12 → 20 (the same object, list rewritten). Every bot's BotAreaActor handle changed 0040d3b6 → 0040d3b7. New small groups (9/3/1 members) appeared, probably combat squads.
- **The +0x50 array is not a roster:** each entry is a record { vtable, vtable2, u32 type @+0x10 (0 or 2), float time @+0x14, ref @+0x1C (00818BD2, same in all), u32 count, member refs from +0x28, ref @+0x70 }. Group::Alert adds type-2 records. Real detection added type-0 records. **Hypothesis:** type 0 = "target confirmed / hero position known", type 2 = "alerted / search". Next: RE what writes type-0 records (start from the GroupAlertedEvent and BotAlertGroupEvent handlers).
- After a death and checkpoint reload at wave 2: all 19 bots (dead wave 1 and live wave 2) share **one group actor** 004134fb; wave 1 area = 004134ed, wave 2 area = 004134ee. → **One group per hideout, one area per wave.** Wave 2 spawned already engaged (user report).

## Reframe: it's the Skirmish system, and the aggro switch is `BotData.SpawnEngagedWithHero` (offline, 2026-10-12 late)

- RTTI: the "group" object = **JobManagerSkirmish** (vtable exe+385a1b0; it is a component of the group actor). Its +0x50 records = **JobEntrySkirmish** (vtable exe+385a150, 0x98 bytes, ctor exe+494da0). The per-bot member = **SkirmishJobHost** component (exe+385a310). So "Group::Alert" (exe+496b40) is a JobManagerSkirmish method that sends GroupAlertedEvent to type-2 job hosts. Related classes: SkirmishAwareness(+Action), SkirmishScriptAction, SkirmishGroupConfig, HeroLostBySkirmishGroupEvent.
- **`SpawnEngagedWithHero`**: a BotData field. Its tooltip (exe+395e0d0) reads "Spawn in knowing the hero's location, and get mad at him".
  - BotData field table: names exe+395e260, types exe+395e3a8, offsets exe+395e440. Layout: vtable +0 (exe+395e988); bool SpawnEngagedWithHero +0x08; ShootDuringIntro +0x09; IntroIsInterruptibleByDamage/Awareness/ModeControl +0x0A..0x0C; IntroAfterFinishingInvestigation +0x0D; IntroSpeed +0x10; DisableIdleWander +0x14; IdleHomeArea/HomeArea/InnerAwareness objects +0x18/+0x38/+0x58; float AggressorDesire +0x78; DefenderDesire +0x7C; ShareAwarenessWithSkirmishGroup +0x80; ProjectileThrower +0x81; DisplayName +0x88; CanUseQTEAttacks +0x98; SetModelInstAsHero +0x99.
  - Embedded in **Bot** (runtime base of ThugBot, vtable exe+3832218) at **+0x160**. The getter exe+2fff30 returns byte [this+0x168]. Bot vtable slot 7 (exe+2f5aa0) checks it and calls vfunc+0x330 (exe+2f8b70, acts on a target handle at +0x20C/+0x214).
  - Also embedded in **BotPrius** (per-type config, ctor exe+ee6330) at **+0x2C0**. Copy function exe+ee6130.
- Data: it's set in the `.actor` main bot component (e.g. `SlayerRangedBot.BotData.SpawnEngagedWithHero = true`) for docock, scorpion, taskmaster, mr negative variants, mechahammerhead, and the hammerhead minigun heavies, and per instance in **170 zone files** (hideout/encounter waves). The prison thug's `ThugBot.BotData` only sets `ProjectileThrower: true`.
- Tools: `tools/sigtools/actor2json.py` (decodes .actor components with ALERT), `vt2name.py` (vtable → RTTI name), `dumpq.py` (qword dump with string/pointer annotation).
- EndlessMode: `botdata <target>` lists BotData structs (EXACT = real vtable); `spawn <x> <dist> <heading> engaged` sets +0x08 = 1 on every EXACT BotData (live Bot copy and Prius) right after SpawnRaw, before activation. `wave aggro on` makes the director spawn engaged. **Untested.** If slot 7 runs inside SpawnRaw, only the Prius write helps, and only from the 2nd spawn of that type.

## Actor paths

`<game>\scripts\SMPCModMenu\Actors.txt` (from jedijosh920's Mod Menu) lists 5167 asset paths. The prison-break enemies are under `character\enemy\enm_meleethug\enm_prison_*`, `enm_thug_prison_*` and `enm_grenadier\enm_thug_*prison_rpg`. Bosses include `enm_tombstone`, `enm_kingpin_boss` and `docock`.

## CONFIRMED in game (2026-10-12 22:54): engaged spawns work

`spawn 0 -15 180 engaged` ? write ThugBot+0x168 (BotData.SpawnEngagedWithHero) 0?1 right after SpawnRaw, before activation ? the prison thug, spawned 15 m behind and facing away, **immediately came at the hero**. The Prius copy wasn't found (no pointer in the component's first 0x400) and isn't needed. **Aggro problem solved.**

- Director (2026-10-12 23:10): engaged spawns are now the default (`wave aggro off` turns them off). The write targets only Bot+0x160 (the scanner had also overrun a small CooldownComponent into a neighbour), and it logs a WARNING if an enemy class has no Bot+0x160 BotData. Wave 1 (melee, baton, pistol) all came in engaged. Grenadier/rifle/sniper/heavy are untested. Stuck-spawn detection: a melee enemy that moves < 2 m within 12 s (hero not within 6 m) gets its point blacklisted in `scripts\EndlessMode_badpoints_<arena>.txt`, is despawned, and is refunded to the wave.

## Offline build 2026-10-13 (untested in game)

- **HUD:** Script Hook's AddMessage = `exe+7793d0(hud, slot<0x15, text, float seconds, 0, 0, u8 0, u8 1)`; Clear = `exe+77a1e0(hud, slot, 0, u8 0)`; IsVisible = `byte[hud+0x48]`. The hud object array is at exe+6d28f80 (pattern `48 69 C3 F8 05 00 00 48 8D 0D rel32`, one 0x5F8 element per player). Game callers use slots 0 (notifications, 5 s), 0x0B, 0x0E and 0x14.
- **HeroHealth** vtable exe+38a6df0: slot 11 OnDamage = exe+82cf70 (BotHealth/Health use exe+1f15c30). It uses the same damage message (+0x138 attacker, +0x144 amount).
- **Health:** max at +0x80 (SetHealth clamps to it), current at +0x88. ScaleHealth writes max, then calls SetHealth.
- **Roster mining:** 287 enemy actors, 221 with a bot component. Boss classes: DocOck, ScorpionBot, TaskmasterBot, RhinoBot, Shocker, HammerheadBot, MechaHammerhead, SilverSable, MrNegativeSwordsman, FiskBot; Tombstone is a SlayerBot with 1000 HP. Mr. Negative also sets a `StayEngaged` BotData flag. See tools/roster/enemies.txt.
- See docs/ENDLESS_MODE.md for the systems built on top.

## Gameplay camera (verified in game 2026-10-13)
- The camera is an actor in the actor table with 11 components: FollowCamera, CameraPenetrationTest, CameraTargetBlender, FollowCameraMover, CameraCollisionLine, CameraRollEffect, CameraAnimEffect, ...
- Find it by scanning the actor table for the `FollowCamera` component (`entity::Camera()`, cached).
- Its transform at [actor+0] is the camera's: row 2 (+0x20) is forward, row 3 (+0x30) is position (same layout as the hero's).
- Verified by rotating the camera with the hero standing still: the position moved to the hero's other side (1136.1 → 1140.5) and forward flipped (+0.97 → -0.97 in x).

## Perks and inventory (static RE, 2026-10-13; not yet called live)
- Suit mods, suit powers and skills are perks (`PerkItemConfig` assets, 119 extracted to tools/assets/perks/ with JSON). An asset id is the CRC64 path hash (ALERT `crc64.hash`).
- PerkSystem: static instance at exe+5da5340 (vtable exe+38f7078). [+0x18] is a container with count at +0x30 (exe+c89710 returns it); exe+c89630(sys, idx) returns the perk config at an index. This looks like the catalog of all perks, not what the player owns.
- **AddToInventoryAction** (mission-graph action; vtable exe+38e46c8, execute exe+bc86f0) shows how the game grants items:
  - target: input pin hash 0x7817619B, else the hero via TargetLookup exe+1910fd0(&TargetSys exe+7a42f80, ...);
  - count: pin 0x53F0C08A → [action+0x68] int; item: [action+0x60] = u64 asset id (asset table entry type 4 = config);
  - per target entity: inventory component = resolve(entity ref, type exe+6e096f0) via exe+16799a0 / exe+167ab40;
  - `inv->vfunc[0x168/8=45](inv, asset_id, count)` → bool can-add (GameInventoryManager slot 45 = exe+bbf790);
  - `inv->vfunc[0x80/8=16](inv, &result, asset_id, count)` → add (slot 16 = exe+1f5b720); the result is checked by exe+1f508f0 / exe+1f507d0 (3 = failure).
  - The inventory component is GameInventoryManager (vtable exe+38e3148) [I: slot 45 lands in its own code range].
- **RemoveFromInventoryAction** (vtable exe+38e4758, execute exe+bc8bc0) uses a MultiInventoryRemove builder (0x110 bytes): init exe+1f60500; add an inventory ([inv+0x14] u32) via exe+1f60530 (≤16); add (asset id, count) via exe+1f60550 (≤16); execute exe+1f60aa0(builder, &result, owner, true).
- Debug-menu action names "SuperiorSpiderManActionHandle*" (e.g. AddPerkLevel, handler exe+c89180) are dev-menu handlers, not the gameplay path.
- Open questions: owning vs **equipping** (suit mods use slots, the suit power is one equip; see EquipManager / SpiderSkillEquipItem / PerkEquipItem); a per-item count query for the snapshot; whether a granted perk applies at once.
- Saves backed up before any live inventory test: backups/saves_20261013_pre_perks (23 files, md5-checked).

## Equipped loadout (verified live 2026-10-13)
- **Owning vs equipping:** adding or removing a suit power's PerkItemConfig through the inventory (`inv add/remove`, transaction status 1 = done, status table exe+63cc918[u16 & 0x3FF]) does **not** change the suit power menu. Unlocks come from elsewhere (likely suit ownership). Equipping from the menu doesn't go through manager Add/Remove/slots 17-18/MultiRemove either (watch hooks saw nothing).
- **The equipped loadout is two 5 x u64 blocks of asset ids in the exe's .data:** exe+70a2828 (A) and exe+70a2868 (B). Layout: [0] suit (950257660a67973d), [1] suit power, [2..4] suit mods. Found with `vscan`: Rock Out id → switch to Web Blossom → keep.
- Block A is read by the menu's "is equipped?" check (exe+cc5200: category [elem+0x1D0] 0 = suit → A[0], 1 = power → A[1], 2 = mod → A[2..4]).
- **Writing A[1] changes the live suit power:** poking Rock Out's id over Web Blossom made the power fire as Rock Out and the menu show Rock Out, with no menu visit. Block B still held Web Blossom (role unknown: saved copy or previous?).
- Still to test: do suit mods (passive perks) apply when written into A[2..4], or do they need an apply step (ProgUpgradeEquipEvent)? Does the save pick up A at autosave?
- **Mods need the apply step (verified 2026-10-13):** pistol bullets (enm_thug_kingpin_pistol) did 40 per hit with Ballistic Inserts only poked into A[4] (exe+70a2848), still 40 after a menu visit with the original mod back in, and **26 (= 40 x 0.65, DEFENSE_BALLISTIC)** once Ballistic Inserts was equipped through the menu. So writing a mod slot only changes what the menu shows; the menu's equip path applies the perk. Powers work by poke because they're read when triggered.
- Ballistic Inserts = AEB9462304EDF8F4 (DEFENSE_BALLISTIC). After that menu equip, A[3] also changed to A2E7AA41C7E72D42 (UI shuffle?).
- **Retest (user asked, 2026-10-13 19:27-19:29, god mode, hits logged):** M1: Ballistic Inserts equipped via menu, pistol 26, sniper 68.25 (105 x 0.65). M2: old mod poked over it at t=57 s with no menu, and every later pistol hit was still 26. **Poking a mod slot changes neither the applied effect nor (it seems) anything but the menu record.** P1: Web Blossom poked into A[1] with no menu, and the power fired as Web Blossom. **Powers: poke works; mods: need the apply step.**

## Equip path (watchpoint, 2026-10-13)
- A hardware write watchpoint on A[4] (exe+70a2848) while equipping Ballistic Inserts from the menu caught two writes on one thread: exe+cc67d3 (inside the old item's unequip, via exe+cca680) and exe+cc60bc (the equip routine).
- **Equip manager = exe+70957c0** (490 code refs; a major global). The loadout block A is at mgr+0xD068 (8 slots, indices 0..7), copy B at mgr+0xD0A8, owned-items table at mgr+0x8808 (lookup exe+1bcfaf0(table, u64 id) → ptr to record ptr).
- **exe+cc5fc0 Equip(mgr, record, slot, also_B):** if the slot holds a different id, look up the old record, call exe+cca680(old, 0, 0, 0) (unequip) and dispatch an event with [ev+0x138] = slot; write the new id ([*record + 0x64] u64) to A[slot] (and B if also_B); send event hash 0xE3FB5FB4 via SendEvent exe+16d5e80 with [ev+0x138] = slot, delivered by exe+14cd960 to [*record + 8]. Slot 0 (suit) has extra handling ([record+0x34], exe+cc1610).
- **exe+cc6200 EquipById(mgr, u64 id):** no-op if already equipped; record = lookup; requires [record+0x34] (owned) and ![*record+0x44] (not locked); category [*record+0x1D0]: 0 suit, 1 power, 2 mod (slot table exe+5db1088), mods go to the first free of 2..4, else 4; then tail-calls Equip(..., also_B = true). Called from exe+bc0575 (inventory code: auto-equip on acquire?).
- `equip <id> [slot]` dev command wraps both (SEH-guarded) and logs which slots changed.
- **Activation (live test 2026-10-13):** Equip exe+cc5fc0 from code swapped Ballistic Inserts out (bullets 26 → 40) but equipping it back the same way left bullets at 40: Equip only *removes* the old perk. The menu's wrapper **exe+cbf380 MenuEquip(record, slot, flag)** calls Equip, then for slot != 1 finds the hero (player slots exe+5d9dd1c, 0x14 each, GetActor), gets the inventory (type exe+6e096f0, exe+bbfbf0), item = inv->vfunc[0x198/8](asset id), and **exe+bc03e0(inv, item, 1) activates it**. `equip <id> <slot>` now goes through MenuEquip.
- **Loadout lock (live test):** hooking Equip and refusing calls that weren't ours stopped all menu changes (refusals logged for slots 0, 1, 2 and 4; the block was unchanged; the HUD message showed on return to the game). Hole: MenuEquip still ran activation after a refused Equip, so the lock now also hooks MenuEquip (exe+cbf380) and refuses the whole call.
- **Equip from code verified (2026-10-13 20:00):** `equip AEB9462304EDF8F4 4` through MenuEquip took bullets from 40 to 26 with no menu visit.
- **Unequip path:** with the lock on, the menu's swap was refused (slot 4, twice), but **unequip** emptied the slot (A[4] = 0, bullets back to 40). Unequip = **exe+cca680 MenuUnequip(record, flag, force, ?)**: refuses locked items unless force; calls the core exe+cc6700(mgr, record, flag) (writes A[slot] = 0 at exe+cc67c7); then deactivates on the hero inventory. Callers: exe+7b738f (menu), exe+cc6054 (Equip removing the old item), exe+cca445. The lock now hooks it too; `unequip <id>` calls it.
