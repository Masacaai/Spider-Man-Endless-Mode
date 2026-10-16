# Game internals: what we've learned about Spider-Man Remastered 4.0630.0.0

Everything here comes from reading the game's executable, its asset archives and memory, plus testing in game. All
addresses are `Spider-Man.exe`-relative RVAs (`exe+RVA`; ImageBase `0x140000000`) and **valid for build 4.0630.0.0 only**.

Confidence tags: **[verified]** tested in game; **[RE]** read from disassembly, not exercised, or only partly; **[?]** unknown
or a guess. When you extend this file, keep the tags honest. How each thing was found: [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md).
Earlier, more chronological notes: [`recon/M1_natives_4.0630.md`](../recon/M1_natives_4.0630.md), [UI_HUNT.md](UI_HUNT.md), [BUGS.md](BUGS.md).

Contents: [Loading code into the game](#loading-code-into-the-game) | [The game thread](#the-game-thread) | [Entities, components, handles](#entities-components-and-handles) | [Spawning and despawning](#spawning-and-despawning-actors) | [Assets and models](#assets-and-models) | [Enemy AI and aggro](#enemy-ai-and-aggro) | [Health and damage](#health-and-damage) | [Bosses](#bosses) | [UI (Scaleform)](#ui-scaleform) | [Time, camera, input](#time-camera-and-input) | [Inventory, suit powers, mods](#inventory-suit-powers-and-mods) | [World and arena](#world-and-arena) | [Crash and hang catalogue](#crash-and-hang-catalogue)

## Loading code into the game

- **Overstrike** (Tkachov, GPLv3) installs `.script` mods. With "Enable .script support" and the `-scripts` command-line option,
  its proxy (`winmm.dll` in the game folder) hooks the game's command-line parsing and `LoadLibrary`s each script DLL from
  `<game>\scripts\`, then calls its exported `script_enable()`. **[verified]**
- `script_enable` runs very early, before the world exists. Start a thread and wait. **[verified]**
- Script Hook (the older mod loader) and its natives list were used **only as a map** of which game functions matter. Its
  `SpawnActor`, `LoadAssetByHash`, `CreateSyncObject`, `GetActor`... correspond to the natives in `natives.cpp`.

## The game thread

- **Hook point `exe+1f745f0`**, called once per frame from `exe+1f731a1`. A detour pops a queued job (`gamethread.cpp`), then
  calls the original. Signature in `natives.cpp` (`GameThreadTick`). **[verified]** An alternative per-frame hook, `exe+16244c0`, is used by other projects. **[RE]**
- The tick only runs while the game window is focused. A background game doesn't process mod jobs. **[verified]**
- Game state must be changed from this thread. Our `gamethread::Run` blocks the caller until the job ran (timeout 3 s by default).
- Frame-rate proxy: ticks per second and longest gap between ticks (`ticks`, `fps` dev commands).

## Entities, components and handles

- **Entity** (from Script Hook's `GetComponentByName`/`GetTransform`): `[ent+0x00]` Transform pointer (position `+0x30`),
  `[ent+0x68]` component array of 16-byte entries `{desc*, instance*}`, component type name `(char*)[desc+0x60]`, `int16 [ent+0x70]`
  component count. **[verified]**
- **Actor table**: `[exe+7a44380]` points to entities stored inline, `0xC0` bytes each; count `u32 [exe+7a4439c]`.
  `GetActor(handle)` (exe+15a0560, same function as the SDK's `GetActor`) = base + index*0xC0. **[verified]**
- **Handle layout**: `(word[ent+0x10] & 0x7FF) << 20 | (dword[ent+0xC] & 0xFFFFF)`. **[verified]**
- Weak references: `ResolveRef` exe+16798f0 over a table of 16-byte `{object*, generation}` entries (`[exe+7a44320]`). **[RE]**
- Hero: `HeroSystem` at `exe+5d9dd00`; hero handle data `exe+5d9de68` (= HeroSystem+0x168). **[verified]**
- Finding a component by name and dumping all of an entity's components are dev commands (`comps`, `near`). A bot has
  ~130 components (`ThugBot`, `BotCombatAlternatives`, `AnimControllerComponent`, `TargetData`, `BotStatusEffectTracker*`, ...).

## Spawning and despawning actors

Spawning is a recipe the mod replays from Script Hook's disassembly (details in `recon/M1_natives_4.0630.md`). **[verified]**

```
hash  = ConvertStringToHash(path)                       exe+1bb25d0 (u64* out, const char*)
asset = LoadAssetByHash(&ActorAssetManager, hash, 0,0,0,0,0)    exe+1626380   (ByName: exe+1626300)
wait until byte [asset+1] == 1                           "loaded"
ent   = SpawnRaw(hash, 0, &Transform)                    exe+1f65170   (first arg is the u64 HASH, not the asset pointer)
sync  = CreateSyncObject(descriptor)                     exe+1980a60  (descriptor: dword 0, word slot, ...; slot = first null of the 0x2EE0-entry table at [exe+7a65f00])
handle = register: SyncRegisterHandle(sync, &handle)     exe+1986230
attach / activate: EntityInit exe+159db20, EntityAttach exe+159e590, SyncActivate exe+19877e0 (if flags&8), SyncFlag2, SyncFinish1/0
```

- Transform is 0x40 bytes: `[0]=sin t, [8]=-cos t, [0x14]=1, [0x20]=cos t, [0x28]=sin t, [0x30..0x38]=pos, [0x3C]=1`, t = heading in radians.
- A sync object begins with the actor handle. `GetActor(sync)` returns the entity.
- **Despawn**: find the sync object from the entity, `DeleteSyncObject(sync)` (exe+1981690), then on the game thread
  `EntityDestroy(entity)` (exe+159d500). **[verified]**
- All of it runs on the game thread via `gamethread::Run`; our worker blocks while it does.
- Spawn cost is 20-38 ms per enemy; each spawn is logged as `spawn: [1] ... [8] done` (the numbered steps are the stages above).

## Assets and models

### Loading and reference counts

- `LoadAssetByHash` returns an asset whose `byte [asset+1] == 1` once loaded and which holds a `u16` reference count at `[asset+4]`.
- `AssetRelease(manager, asset)` = exe+1626940 drops one reference. **We never release during a run** (see below).
- An enemy actor is not just its `.actor`: it depends on other actors (weapons, jetpacks, heads, ammo items, projectiles).
  These must be loaded too or the enemy spawns with **white cubes** where the missing models belong. The chain is
  `.actor -> rifle_firing config -> WeaponItemConfig -> ItemActor` for weapons and jetpacks. `tools/assets/deps.py` reads
  the archives and generates `src/preload_deps.inc`; `spawn::Preload` loads those extras and `spawn::Ready` waits for them
  (20 s cap, then logs "not fully loaded"). **[verified]**

### Model capacity and asset lifetime

- **Releasing assets mid-run is fatal**: surviving enemies lose their bodies (head and weapon only) and the game
  crashes at `exe+15f9bc9` (a null mesh pointer). So the mod releases only 30 s after a run ends. **[verified]**
- **The game runs out of room for enemy models** and breaks "whatever loads last": white cubes, floating heads, invisible enemies.
  Observations: 33 unit types fine; 39 fine; 50 broke Olympus (Horde); a 57-type "parade" at Ryker's was fine; 86 broke. The real limit
  and cause are **[?]** (candidates: live bodies and enemies, mid-run loading, or something specific to a faction).
- Therefore pools are chosen by a **unit-type budget** (`plan::kMaxTypes`, default 44; `[run] max_types` in the ini):
  `plan::BudgetedPool` adds factions greedily until the budget is spent. Faction sizes: street 7, fisk 12, demons 9, prisoners
  15, sable 9, hammerhead 10, olympus 11, tombstone 8, taskmaster 8 (89 types in all).
- A history of the "respawn load hang after a long Horde run" (all nine factions, 1000+ assets) fixed by the budget and the
  post-run release is in [BUGS.md](BUGS.md).

### Archive format notes (from the ALERT wiki)

Assets live in `asset_archive`, found via the `toc`; asset ids are CRC64 of the file name; containers are DAT1; `.config` files
are binary JSON-like with a `Type`. `tools/ALERT` (a clone of Tkachov's ALERT, not in this repo) converts configs to JSON and back.
`tools/toctool` searches the table of contents, and `tools/assets/extract.py` pulls assets out.
Don't commit extracted bytes.

## Enemy AI and aggro

- **Why spawned enemies fight at all**: right after `SpawnRaw` and **before activation**, the mod sets
  `BotData.SpawnEngagedWithHero` (the Bot component, `+0x168` of the Bot component per the first recon; `spawn.cpp` finds the
  `BotData`-shaped struct via `entity::FindBotData`). The game sets the same flag for hideout waves. **[verified 2026-10-12]**
- **Heavies are spawned calm on purpose.** Units whose bot class is `SlayerBot` or `SlayerRangedBot` (heavies, miniguns, Tombstone)
  get `calm = true` in the roster (`tools/roster/gen_roster.py`, `CALM_CLASSES`), so they skip the engaged flag and only join the fight when they notice
  the hero. That was a workaround for crashes that, we found later, were really caused by missing dependency assets (see Bosses).
  Whether heavies can now safely spawn engaged is untested; it likely explains why heavies often stand around.
- Awareness: `LineOfSightAwareness` config (damage events move a target from `kConfirming` to `kConfirmed`). `damage.cpp`
  sends a real damage message through the game's pipeline so an enemy reacts like to a hit. **[verified]**
- Bot groups: `Group::Alert` exe+496b40 `(group, alerter handle, flag)` sends `GroupAlertedEvent` to members. The
  generic event system is `SendEvent` exe+16d5e80 `(sys, type_hash, target, &source, 1, 0, 0, 0, 1, f, f, payload, size)` where the
  type hash is Insomniac's CRC32 of the class name. **[RE]**
- Insomniac's AI uses attack tokens and positioning, so more bodies mostly means more *ranged* pressure, not more melee.
- Enemies fight each other when hit by each other's attacks ("infighting"). We let it happen, don't count those kills.
- Known annoyances: enemies spawned onto cage/wall edges get stuck (cages, endless falling animation); `director.cpp` has
  the stationary and never-left-spawn rules to relocate them.
- Time/difficulty facts from stress tests: update rate 115/s baseline, 80/s at 80 enemies, ~60/s at 130-160; the game hung between
  160 and 170 enemies once (a per-component update loop at `exe+1d6cc90`), hence `horde_max = 80`.

## Health and damage

- `Health::SetHealth(health component*, float)` exe+1f16fc0 clamps to `[0, max]` and stores at `+0x88`. The mod raises max
  first and then sets health (used for Hammerhead's 1.8x and for god mode). **[verified]**
- Damage pipeline (as melee uses it, exe+69be20): `msg = DmgCreate(&DmgSys, &target_handle, &pos, &vec, &dir, -1)` (exe+1eb6e40),
  fill fields (`+0x138` attacker handle, `+0x144` amount), `DmgCommit(&DmgSys)` (exe+1eb7c60) dispatches. `Health::OnDamage` is
  exe+1f15c30 (vtable +0x58). **[verified]**
- Hero: `HeroHealth::OnDamage` exe+82cf70. The mod hooks it for telemetry: attacker is read from `msg+0x138`, damage from `+0x144`.
- A bot with health 0 is dead; its entity stays (a corpse) until despawned. Spawns with 0 health (some bosses) act dead, so the
  director tops them up first. **[verified]**
- The pre-death slow motion in the main game and the OVERWHELMED banner: the mod slows time on death
  (see [Time](#time-camera-and-input)).

## Bosses

Details and per-boss results: [BUGS.md](BUGS.md) and the roster table in [ENDLESS_MODE.md](ENDLESS_MODE.md).

- **Root cause of the early boss crashes, missing heads and missing movers (2026-10-13):** the spawner waited only for the body asset.
  Heavy actors depend on more assets, notably a separate **head actor** (`HeadActor`, for example `characters/npc/npc_hammerhead/heads/...head.actor`),
  and the movement components only come together once everything is streamed in. Fix: `spawn::Preload` loads each boss and its head
  at run start and the first spawn waits for it. **[verified]**
- Before that was understood, `BotStateGotoLocal` init (exe+1dd6660) was seen dereferencing a null `MoverStandard`. `guards.cpp`
  still patches in a null check as a backstop. **[verified]**
- Kingpin's `BossHealth` layout differs (`+0x90` max / `+0x98` current versus `+0x80/+0x88`), and he takes no damage outside his story phases: benched.
- Story gates: Hammerhead cuts off at 55% of his health in the story, so the mod gives him x1.8 health to restore the fight length. Some
  fights rely on scripted setpieces and can't be fought standalone (Kingpin: benched).
- The director has a **boss watchdog**: a boss that never loses health and never moves for 40 s is removed and skipped.
- Verified standalone bosses: Taskmaster, Silver Sable, Tombstone, Tombstone (Chained), Mister Negative, Hammerhead.

## UI (Scaleform)

The HUD is a Scaleform (Flash/ActionScript) movie. We drive it from code the way the game's own script actions do.
Full chronology, with every recording: [UI_HUNT.md](UI_HUNT.md). Summary of what works:

### HUD root and elements
- Root for player i: `qword [exe+7c2b5a0 + i*8]` (a static `GUI::PlayerHUD`). Elements hang off it at fixed offsets:
  `0x188 Ammo, 0x190 Weapon, 0x198 Reticule, 0x1a0 Message, 0x1a8 POI, 0x1b0 ProgressBar, 0x1b8 QuickSelect, 0x1c0 Health (hosts the activity panel), 0x1d8 InteractButton, 0x1e0 SuitPower, 0x1e8 Activities (don't call blind), 0x208 CountDown, 0x210 Mission, 0x218 Counter, 0x240 Communicator`.
- Calling ActionScript: `exe+1d1cee0 Invoke(movie, &as_object, "Method", result, args, count)`. We address a method as
  "root offset : AS object offset" (e.g. `1c0:6e8` Refresh). `ui::Call` / `gfx::Invoke`.
- The movie wrapper is `[[root+0x1A0]+0x300]+8`; `+0x18` of that is the GFx Movie. `gfx::Ready()` self-checks before any call.

### Building GFx values from code (`gfx.cpp`)
- A value is 0x30 bytes: `+0x00/+0x08` links of the movie's list of live managed values (the game initialises them to all-ones),
  `+0x10` interface, `+0x18` type (2 bool, 3 int, 4 uint, 5 number, 6 string, 8 object, 9 array; `0x40` flag = managed), `+0x20` data.
- Create an object with exe+319f7d0, a string with exe+319f7e0, set fields via the object interface `SetMember` (vtable +0x28), push to
  arrays via the array interface, then pass the object as the single argument of a method. `gfx::Node` builds trees of these.
- **The movie's teardown (exe+319b340) walks the managed-value list and releases every node.** A Value you registered and then
  destroyed without releasing leaves a dangling node and crashes the game on the next HUD reload (crash at death, 2026-10-15,
  fixed by keeping captured values on the heap and never freeing them).
- `SetMember` hook at exe+3529e30 (prologue `40 55 53 56 57 41 54 41`): used by `uilog` to record data objects and by the text
  capture to learn which object holds which localized strings.
- **Text capture**: the HUD loads its fixed texts (button labels, titles) into per-panel objects at load time. `gfx::InstallTextCapture`
  remembers them (call it before the HUD loads), and `SetText`/`RestoreText` change one by key, e.g. relabelling RETRY to START
  (`CHALLENGE_GEN_BTN_TRYAGAIN`). `gfx caps` lists captured objects. Stale captures are detected when the managed flag is gone.

### Panels and messages that work
- **Notification box**: `exe+7793d0 Show(hud, slot, text, seconds, 0,0,0,1)`, `exe+77a1e0 Clear`. Slot 0 is the general box.
  It renders button glyph tokens like `[BTN_Y]` and `<span class='emphasis'>`. `hud::Show`. **[verified]**
- **Medium message** (top banner): `exe+77db70(element, type, text, data, 1, 0, 0.0f)` on the Message element. Type 7 = custom text;
  21 = wave banner `{icon, currentWave, maxWave}` (the movie prints "WAVE n", "FINAL WAVE" when current == max);
  13 = base-complete `{icon}` + label; 6 = level up. `hud::Banner`, `gfx::Message`. **[verified]**
- **Big countdown** `208:30 ShowBigCountDown(int)` / `HideBigCountDown`; **counter** `218:30 ShowCounter("ENEMIES", cur, max, ...)` / `HideCounter`. **[verified]**
- **Crime pop-up** `210:38 ShowCrimeScanner(title, subtitle, 2, 5, array)`; hide with `HideMission()`. Used for boss entrances. **[verified]**
- **Activity panel** `1c0:6e8` with `Refresh(obj)` / `HideActivity(obj)`; `obj = {activity, panelType, panelData}`:
  - activity 3 (hideout): panelType 0 = **preview on the circle** (label, icon, time, enemiesDefeated, subObjectives `{label, currentCount, maxCount, reward}`, rewards); 1 = in progress; 2 = completed.
  - activity 1 (challenge): panelType 0 preview; 1 = **live score** `{type 2, label, icon, score, scoreItems}`; 2 = **results** `{label, icon, score, highScore, summaryData[{label,value}], rewards, noRewards, awardedRewards, rewardTiers, taskMaster...}`.
  - A completed subObjective has `currentCount == maxCount`. `HideActivity` must use the same activity as the panel you showed (we hid the wrong one once).
  - The panels animate on game time. Freezing the world mid-animation leaves them translucent; freeze only after they have faded in.
  - **Results panel data shape (found by disassembling the panel's ActionScript, 2026-10-16, `tools/sigtools/abcdump.py`):** `rewards` must be an
    **array of arrays**, one inner array per tier (`rewards[tier][0].value` is read, `rewards[tier][0].icon` shown). A flat list makes
    `ActivityChallengeAfterAction.Refresh` throw (`undefined.value`) as soon as `highScore > score` and `highScore >= rewardTiers[0]`.
    A throw aborts the rest of Refresh silently, so the tier "min score" labels keep their placeholder 500, the `intro` animation never plays
    (the panel stays on the hidden end frame: invisible from the second time on, flashing only when `HideActivity` plays the outro) and
    the delayed `ShowBtnHint` (which sets the Continue and Retry hints) is never scheduled. Other fields it reads: `label`, `icon`, `summaryData`,
    `score`, `highScore`, `rewardTiers` (3 numbers), `rewards`, `noRewards`, `awardedRewards` (flat list shown in the reward list when non-empty).
    The Taskmaster progress fields are not read by Refresh. `ShowBtnHint` always sets both a Continue (bottom face button) and a Retry (top) hint.
- Localization strings the HUD uses: `HUD_WAVE`, `HUD_TASK_COMPLETED`, `CHALLENGE_GEN_BTN_CONTINUE`, `CHALLENGE_GEN_BTN_TRYAGAIN`,
  `HUD_TASK_COMPLETE_REWARDS`, `HUD_TASK_COMPLETE_BONUSOBJECTIVES`, `CHALLENGE_GEN_RANK_*`, and more (full list in UI_HUNT).

### Not found / dead ends
- Map icons: icons come from `UIMinimapConfig` (element Type -> icon) and static tables; no UI call adds them. Abandoned.
- `ShowActivityMessage` and `ShowAwardTrackerToast` don't exist in any HUD movie (leftovers). `ShowAlert` needs menu state.
- A near-fullscreen pop-up for reward picks (to show the suit menu's video/picture) has not been found.

## Time, camera and input

- **Time scale**: the `TimeScaleSystem` static at `exe+621b890` (current scale at `+0x18`). Start a scale change with exe+19bbaa0
  `(sys, channel, target, rate, name, mode)` on channel 5, as `TimeScaleAction` does for scripted slow motion. `timescale::Set`,
  `Freeze` (not exactly 0, to avoid divisions), `Unfreeze`. Used for reward-pick freeze, the abandon prompt, and slow-mo at death. **[verified]**
- **Screen fade**: `ScreenFadeAction` exe+205bb20 -> `MsgSend` exe+2142440 with message types at `exe+7c50c80` (fade out) and
  `exe+7c50c78` (fade in), payload `{float seconds, u8 0}`. `hud::FadeOut/FadeIn`. **[verified]**
- **Camera**: its address was never found directly (`camscan` finds float3 positions by memory scan). Spawn logic uses the hero position plus a
  camera cone of about 78 degrees, taken from the hero heading, to keep spawns out of view. **[?]** for exact FOV.
- **Input**: keyboard via `GetAsyncKeyState` (game focused only), Xbox pads via XInput (also PlayStation pads exposed through Steam Input),
  PlayStation pads over HID (`psinput.cpp`: face buttons `0x10` square, `0x20` cross, `0x40` circle, `0x80` triangle on one byte, D-pad hat in the low nibble).
  `input::Glyph/Name` name the button for the last-used device.

## Inventory, suit powers and mods

- Perks, suit mods, suit powers and gear are **inventory items keyed by their config asset id**. `GameInventoryManager`
  (vtable exe+38e3148) routes each item to a sub-inventory. Vfuncs: 16 (exe+1f5b720) `Add(mgr, &result, asset_id, count)`;
  19 (exe+1f5b9d0) `Remove`; 18 (exe+1f5b880) probably a count query; 45 (exe+bbf790) `CanAdd`. **[RE/verified for Add/Remove]**
- The suit loadout slots (power = slot 1, mods = slots 2..4) can be emptied and locked; `runloadout.cpp` saves, empties, restores. A
  hardware write watchpoint on the loadout block found the code that equips mods. **[verified]**
- **Item text** (names/descriptions come from the game; the mod never hard-codes them): item data record -> name hash `+0x24`,
  description hash `+0x34`, format argument `+0x38`, icon `char* +0x48`, preview video config `char* +0xA0`, preview image `char* +0xB8`,
  category `+0x1D0`. Localization lookup `exe+1749970(hash, 0)` returns a `char*`; formatting `exe+1bc1b10` (snprintf-like). Item
  manager at `exe+70957c0`. `inv text <id>` prints both.
- Suit powers and mods do nothing useful in an arena include: Quips, Sound Of Silence, Combat Analyzer, Proximity Sensor, Long Range
  Scanner, Silent Step, Bio Scanner (see the curation notes in LEDGER).

## World and arena

- **Ryker's Island** is a piece of the open world. The arena is its yard: the mod teleports the hero there (`arena::Teleport`), keeps
  them inside an invisible fence, and uses the 139 designer placements from `GP_A2_PRISON_EXT_GAMEPLAY.zone` as spawn points
  (`rykers_points.h`). The Raft interior (zone streaming via `ZoneAssetManager`) was tried; see `arena.cpp`.
- **Dock circles** in Manhattan are drawn with a spawned UI marker actor (`mis_ui_base_replayreticle`); the mod itself does the "standing on the circle" logic.
- Mission identifiers seen in community tools (the old Debug Menu mod): `GP_A2_PRISON` (the prison-break mission; the yard we use),
  `GP_A2_BLACKOUT`, `GP_A3_NYPD`, `OM_BASE_FISK_GREENWICH`, `OM_DEMONS_STORY_LI`, boss fights `GP_A1_FISK`, `VN_ELECTRO_FIGHT`,
  `VN_SCORPION_02`, `VN_TOMBSTONE_FIGHT`, `GP_A3_OTTOFIGHT`. Zone and level paths we actually use are in `arena.cpp`.
- The engine's own `ProximitySpawnSystem` (what Spider-Man 2's Chaos City uses) exists in this build with natives resolved in `natives.cpp` (`Prox*`); the mod does not use it (it needs a config and has no AI control we wanted).

## Crash and hang catalogue

| Symptom | Cause | Fix |
|---|---|---|
| Street grenadier (`enm_grenadierthug`): stack overflow in `GrenadierBotAiming` (exe+43fc00) | unknown; only this unit uses the class | removed from the street faction |
| Boss spawn crash at `exe+1dd6660` | `BotStateGotoLocal` null `MoverStandard`; root cause was dependency assets (heads) not loaded | `spawn::Preload` + `guards.cpp` null check |
| Boss acts dead | spawned with 0 health | director tops up health at spawn |
| Heavies (SlayerBot) crash when spawned engaged (null mover, exe+1fe51b5) | same dependency root cause (likely) | they spawn calm (`CALM_CLASSES`) |
| Mid-run asset release: bodiless enemies, crash `exe+15f9bc9` | released a model in use | never release mid-run |
| Respawn after a long Horde run hangs on load | too many assets held | type budget + release 30 s after the run |
| Crash at death in a Horde run, `exe+319b6a9` | GFx teardown walked a dangling managed Value | persistent heap values in `gfx.cpp` |
| Hang at 160-170 enemies | per-component update loop `exe+1d6cc90` | `horde_max = 80` |
| Hang during a Horde run, 2026-10-16 (one occurrence) | main thread waiting on a lock inside `TargetQuerySystem::ProcessJob`, worker threads idle | **[?]** record only |
| White cubes / floating heads | model capacity or missing dependency actors | dependency preloads, type budget |
| Screen stays black after the exit circle (once) | fade-in not applied | **[?]** record only |

## Things we deliberately don't know yet

See [`docs/issues/`](issues/) for the open questions drafted as issues, and [BUGS.md](BUGS.md) for raw records.
