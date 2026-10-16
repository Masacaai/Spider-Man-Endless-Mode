# Bugs to chase down later

Open issues found in game. Each entry has what we know, what we don't, and how to reproduce or test it.

## 1. Street grenadier (`enm_grenadierthug`) crashes the game (stack overflow)

- **Seen:** 2026-10-13, during a Ryker's run, about 6 s after the first `character\enemy\enm_grenadier\enm_grenadierthug.actor` spawned. It was engaged, with health ×1.05.
- **Crash:** `EXCEPTION_STACK_OVERFLOW` on the game thread, at exe+43fc24, inside a `GrenadierBotAiming` method (exe+43fc00, vtable exe+38523f8 slot 11). That method resolves a weak ref at component+0x48, probably its target, then calls that target's virtuals; the loop goes back into itself. The dump only captured 16 KB of stack, so the repeating frames aren't visible.
- **Scope:** only this unit uses the `GrenadierBot` class. The prison RPG units (`enm_thug_prison_rpg`, `enm_thug_super_prison_rpg`) are ThugBots and work fine.
- **Workaround:** removed from the street faction (`tools/roster/gen_roster.py`).
- **Suspects:** (a) the engaged flag (`SpawnEngagedWithHero`) on a class that only appears in scripted street encounters; (b) health scaling through `Health::SetHealth`.
- **Test plan:**
  1. `spawn character\enemy\enm_grenadier\enm_grenadierthug.actor 12 0 calm`, no scaling. Does it crash?
  2. Same, engaged.
  3. If (2) crashes, look at `GrenadierBotAiming` slot 11 with a full-memory dump.

## 2. Tombstone (and probably other story bosses) crash in `BotStateGotoLocal`

- **Seen:** 2026-10-13. `boss tombstone 12` (engaged) crashed instantly. An access violation at exe+1dd6a38 reads `[MoverStandard + 0x144]` with a null MoverStandard. The state caches MoverStandard, TargetingBase and NavEdgeResolver from the bot (type descriptors exe+7c40bf0, exe+7bfa670, exe+7c42890).
- **Fix in place, untested:** `src/guards.cpp` patches a null check into exe+1dd6a33 (25 bytes, applied only if the original bytes match). It's on at every launch.
- **Still unknown:** which entity lacks a MoverStandard (Tombstone himself, or a sub-actor like his chain weapon), and whether the bot works properly after the guard skips the update.
- **Test:** `boss tombstone 12`, then `boss tombstone 12 calm`; check the log for `guard` lines.

## 3. Mister Negative hung the game (Raft boss rush)

- **Seen:** 2026-10-13. He spawned with 0 health (the old code then counted him as dead immediately), and the game froze right after, while the next goon was loading.
- **Since fixed (untested):** spawns with 0 health are now filled to max before activation; "dead" requires having been seen alive. The hang may have been the same `BotStateGotoLocal` crash with the crash reporter deadlocking: the hung main thread showed the crash-reporter frames exe+1ce9e67 and exe+1cd0e4f.
- **Test:** `boss mrnegative 12` near the dock, after Tombstone works.

## 4. Bosses spawn with 0 health

- **Seen:** 2026-10-13. Bounty Hunter (0/400) and Mister Negative (0/300) died the moment they spawned; the game killed them. Their mission scripts normally set boss health.
- **Fix in place, untested:** `spawn.cpp` fills health to max right after `SpawnRaw` when it's 0.

## 5. Hideout-style wave counter at the top of the screen

- Not a bug, a missing feature. None of HUD slots 0–10 shows at the top (2, 3, 4, 6 and 7 display; 0 is used, just below the middle of the screen). Slots 11–20 are untested.
- **Plan:** replay a hideout and trace how its top-of-screen wave number is drawn.

## 6. Heavies (SlayerBot class) crash when spawned engaged

- **Seen:** 2026-10-13. A Ryker's run crashed as `enm_heavy_maggia` spawned. It was an access violation at exe+1fe51b5: the function at exe+1fe5190 was called with a null object (reads +0x12c). Its callers are bot behaviours (`BehaviorExitCover`, `BehaviorRangedCombat`, `BotComboConditionHaveClearPositionNearTarget`), all passing the bot's mover. Same root cause as #2: a null `MoverStandard`.
- **Theory:** engaged bots start combat behaviours before their mover exists; big classes (SlayerBot: heavies, Tombstone) set up slower.
- **Workaround:** SlayerBot and SlayerRangedBot units (16 entries: heavies, miniguns, Tombstone) spawn *calm* (`CALM_CLASSES` in gen_roster.py).
- **Diagnosis built in:** every spawn logs `MoverStandard present/MISSING at spawn (engaged|calm)`. Compare thugs vs heavies across a run.

## 7. Story bosses: Mister Negative crash, Kingpin no-show (2026-10-13 Ryker's run)

- **Mister Negative** (`MrNegativeSwordsman`) spawned engaged with `MoverStandard MISSING`: the only one of ~220 spawns that run without it. The game crashed 5 s later at exe+1dd0ecb (a write through null +0x5a8, also in the bot-state code around exe+1dd...). Bosses use their own movement setup; the generic engaged states assume `MoverStandard`.
- **Kingpin** (`FiskBot`) spawned as an entity but never appeared or fought; the watchdog removed him after 40 s. He has no `BotHealth`, so the mover diagnostic didn't log for him.
- **Correction to #6:** heavies *do* have `MoverStandard` at spawn (5/5, spawned calm, no crash). The engaged-heavy crash is something else; calm stays the workaround.

## Next session plan (user, 2026-10-13): bosses and heavies alone

1. Near the dock, one at a time: `boss <name> 12 calm`, then engaged. Order: Taskmaster, Silver Sable, Hammerhead, Tombstone, Mister Negative, Kingpin. For each, record whether it appears, fights, dies, crashes, and the log lines (`MoverStandard`, `guard`).
2. Heavies engaged on their own: `spawn character\enemy\enm_meleethug\enm_heavy_maggia.actor 12 0 engaged`, then `enm_slayer`, `enm_thug_prison_heavy`, and a minigun.
3. The grenadier, calm then engaged (#1).
4. From the results: per-boss spawn mode (calm or engaged), extra guards, or drop the boss.

## Boss test results (2026-10-13, at Ryker's)

| Boss | Calm spawn | Fights | Death | Notes |
|---|---|---|---|---|
| Taskmaster | OK (MoverStandard present, BossHealth) | yes | floors at 1/500; forced 0: goes down | |
| Silver Sable (ctns) | OK (MoverStandard present, BotHealth 860) | yes | floors at 1/860; forced 0: goes down | spawned inside a wall (12 m east of the hero); moved out |
| Hammerhead (rough) | OK (BossHealth 1300) | yes, takes damage | not reached | **crash when swing-kicked**: AV at exe+18046ea (fn exe+18046e0 got pointer 0x340, reads [rdx]). Two were up (a stale 2nd spawn command). God mode (pokes to HeroHealth +0x88) had just started; probably unrelated. |

**Pattern:** story bosses can't drop below 1 HP without their mission finisher. **Fix to build:** a director "finisher" that sets health to 0 once a boss has sat at <= 1 HP for ~2 s.

## 8. Webs on heavies and bosses look like deformed spikes (visual)

- **Seen:** 2026-10-13, sometimes on heavies and on Hammerhead; normal thugs wrap fine.
- **Suspect A (ours):** per-body-type web-wrap models (cf. `characters\npc\npc_shocker\webs\*.actor`) are normally streamed in by the encounter; our direct `SpawnActor` may not load them for big bodies.
- **Suspect B (not ours):** the user's faster-webbing mod changes web behaviour.
- **Test:** web a game-spawned heavy (hideout or street crime) with Endless Mode idle. Spikes there too: not ours. Clean there: find and preload the web assets per enemy type.

## Research notes (offline, 2026-10-13 after the boss tests)

- **Movement configs aren't the cause.** Tombstone's (`tombstonebossmovementconfig`) is field-for-field the same as Hammerhead's, and Hammerhead gets a `MoverStandard`. The street heavy `enm_slayer` is the same class as Tombstone (SlayerBot) and also gets one. So the missing mover is specific to how these boss actors are set up or spawned.
- **Actor diff:** Tombstone has `GroundActorSpawner`, `AOETelegraphSpawner`, `BotMeleeWeaponManager`, `WAMTarget` and others that the plain heavy lacks. Nothing mover-related.
- **`MoverStandard` is created through the component factory:** its constructor (exe+1fc18e0) has no direct callers. Type descriptor exe+7c40bf0; 11 `.data` component-list tables point to it, filled at runtime.
- **Tombstone floated** (no gravity). That fits "no mover", and possibly "spawned in the air, so the manager never attached a ground mover".

## Next session plan (boss round 2)

God mode on (`god on`) first.
1. **Tombstone, calm**: `comps` immediately after spawn (it crashed after ~3 s). Is `BotMoverManagerGame` present? Then try spawning him exactly at ground height (y = hero y, no +1).
2. **Mister Negative variants**: `spawn character\enemy\enm_riflethug\enm_mrnegative_truckchase_combat.actor 12 0 calm`, then `npc_mrnegative` (class MrNegative, 100 hp) and `npc_mrnegative_hallucination`. Check mover, health and behaviour.
3. **Kingpin, calm**: does he appear? `comps` (health component? mover?).
4. **Hammerhead, calm, no swing kick**: does he survive normal combat, and does the finisher drop him? Then a deliberate swing kick to confirm the trigger.
5. Roster now: only Taskmaster and Silver Sable are verified (`[bosses]` in the ini, `allow_unverified_bosses=0`).

## Boss round 2 results (2026-10-13) - ROOT CAUSE FOUND

- **Root cause of the missing mover, missing head and crashes:** the spawner waited only for the body asset to load. Heavy actors (bosses) depend on more assets, notably a separate head actor (`HeadActor`, e.g. `characters/npc/npc_hammerhead/heads/npc_hammerhead_v2_head.actor`), and the movement components only come together once everything has streamed in. Spawned too early, Tombstone had 99 components (no `BotMoverManagerGame`/`MoverStandard`) and floated; preloaded 15 s first, he had 132 and fought normally. Hammerhead spawned headless and crashed at exe+18046ea while fighting normally (no swing kick needed).
- **Fix (built):** `spawn::Preload`. The director preloads every usable boss and its head at run start, and boss spawns wait at least 20 s after the preload. The `boss` dev command preloads and waits 15 s. Heads come from the actor files (gen_roster.py `head_of`).

| Boss | Result with preload | Verified |
|---|---|---|
| Taskmaster | works; floors at 1 HP (finisher) | yes |
| Silver Sable | works; floors at 1 HP (finisher) | yes |
| Tombstone | works, dies normally | yes |
| Tombstone (Chained) | works, dies normally; goofy chain physics (anchor points from his arena?) | yes, min_cycle 1 (harder variant) |
| Mister Negative (swordsman) | works, dies normally | yes |
| Kingpin | appears and fights, but takes no damage outside his story phases (BossHealth at +0x90 max / +0x98 current, which differs from the +0x80/+0x88 the others use; writing current didn't stick) | no (benched) |
| Hammerhead | not yet tested *with* the head preloaded (my first two tries used the wrong folder: he's in `enm_riflethug`) | pending |

- **Note:** Kingpin's BossHealth layout differs, so `entity::ReadHealth` reads garbage for him. Fix it if he ever comes back.
- **Mister Negative alternatives** (`enm_mrnegative_truckchase_combat`, `npc_mrnegative`, `npc_mrnegative_hallucination`) are still untested; the swordsman works now.

## Gates and finisher (2026-10-13)

- Hammerhead stops taking damage at 715/1300 (55%). Writing 500 snapped back to 715; writing 0 kills him. Taskmaster and Silver Sable floor at 1 HP.
- **Finisher (built):** per-boss `gate` in gen_roster.py (`GATE`, default = the 1 HP floor). When a boss's health is at or below `gate * max + 1` for 1 s, the director sets it to 0. The user rejected a time-based "not taking damage while the hero is near" detector (dodging and fighting crews make it misfire).
- **Hammerhead `HEALTH_BONUS` 1.8**, so the fight up to the gate is full length. **Unverified:** whether his gate is relative (55% of the scaled max) or absolute (715). The log line `finisher on Hammerhead at X/Y (gate Z)` will tell. If it's absolute, the finisher fires early at 55% of the scaled max.
- **Head swap for a phase 2: not feasible yet.** The head isn't a separate world actor; `HeadManager` (no asset id in its fields) merges it into the body model. Stretch goal: RE `HeadManager` to rebuild with `npc_hammerhead_v2_head_damaged.actor`.
- **Appearance variants available:** Silver Sable `enm_silver_sable` (base game) vs `enm_silver_sable_ctns` (DLC); Hammerhead heads v2 / v2_damaged / npc_hammerhead.
- Verified bosses: Taskmaster, Silver Sable, Tombstone, Tombstone (Chained), Mister Negative, Hammerhead. Kingpin benched.

## Stress test 1 (2026-10-13, Ryker's yard, engaged inmates: melee/baton/pistol)
- Update rate: 115/s baseline, 80/s at 80 enemies, ~60/s at 130-160. Worst frames 16-24 ms; spawn cost 20-38 ms per enemy. No spawn failures.
- **Hard cliff between 160 and 170:** the game hung (zero updates in the 170 measurement), and the process kept one core busy. In the snapshot dumps, the main thread spins in a per-component update loop at exe+1d6cc90 (`call [vtable+0xA0]` per entry, then exe+1d6c290). It looks like a fixed pool overflowing rather than gradual slowdown [I]. Our despawns then timed out at ~5.7 s each.
- Cap set to **horde_max=80** (half the cliff, smooth at ~80/s). That leaves headroom for heavies, snipers, jetpacks and bosses, which weren't in this test. A second stress run with a mixed roster is still to do.
- **#9 Falling prisoner:** an inmate spawned at the edge of a cage and a wall and got stuck in an endless, very loud falling animation. Blacklist that spawn point. Also consider a "stuck falling" detector: no ground contact or y-velocity for N seconds → relocate.
- Jetpack note: the Hammerhead goon the user saw "without a jetpack" was `enm_rocket_hammerhead`. His actor uses the plain suit model; real jetpack goons are `enm_jetpack_hammerhead03` (Olympus). Unconfirmed whether he was hovering.

## Horde crash (2026-10-13 16:53): cold-spawned swordsman
- Horde level 3 brought Inner Demons in. The first `enm_swordsman` (SwordsmanBot) spawned with **MoverStandard MISSING**, and the game crashed 0.5 s later at exe+1d73e22: the weak-ref resolver exe+16798f0 returned null and was dereferenced (+0x238).
- Same root cause as the boss crashes: spawning before the actor's dependencies stream in.
- Fix: `spawn::Ready(path)`. Every unit type of the run's factions is preloaded at run start, and a type that was never preloaded is preloaded and skipped until it has had 8 s to stream in. The stress test preloads its roster too.
- Olympus note: that run never reached Olympus (factions joined in the order Fisk, Street, Hammerhead, Tombstone, Demons), so the suits the user saw were base Hammerhead's Maggia, which is correct.

## #10 PINNED: dress-shirt goons that jetpack around (user saw it twice, 2026-10-13)
- The user saw suit/dress-shirt goons moving like jetpack users, once in Hammerhead's boss wave and once in Horde. A plain Hammerhead's Maggia wave afterwards looked normal. **Not reproduced yet; investigate if it comes up again.**
- Lead [I]: a rocket-launcher unit spawned both times (`enm_rocket_hammerhead` in the boss wave, `enm_rocket_thug` in Horde). All rocket units, including base-game ones, use `configs/defaulthumanbotmovementconfig/rocketbotmovementconfig.config`, the same movement config family as the Sable/Olympus jetpack enemies may use, but with plain suit models and no jetpack parts. Next time: note which goon it is (gun type), and read rocketbotmovementconfig with config2json for jetpack/hover settings.

## #11 Floating head (Horde, 2026-10-13)
- The user saw a floating head next to a heavy from another faction. It looked like the heavy had been beating the victim before it turned into a floating head.
- Theory [I]: head actors are separate entities (`HeadActor`). If the body is destroyed (by our corpse cleanup, or an odd death) while the head isn't, the head is left floating. Check whether DespawnActor should also destroy the attached head.
- Related: infighting telemetry was added (`stats: infight #N` log lines, plus infight_hits/infight_damage in the run JSON) to confirm whether factions damage each other.

## Death return (2026-10-13)
- Fixed: after hero_down, the hero is brought back to the arena's arrival point instead of the dock.
- First try: teleporting straight out of the respawn perch dragged Spidey through the air in the perch pose. Now the teleport waits until he has left the perch (moved >1.5 m) and stood still for 0.8 s.
- Update (2026-10-13 17:27): **confirmed in-game.** Infighting is real: 47 enemy-on-enemy hits in two short Horde runs, with melee hits of 80-105 and finishing blows. The floating head shows up when an enemy is **killed by another enemy**: the body ragdolls down and the head stays where it was. Hero kills don't do this. Next: find out what differs in the bot-kill death path. Fallback: on a non-hero kill, remove the corpse and its head quickly.
- Cover-up (2026-10-13): the user asked to just hide it. When the bot hook sees an enemy finish off another enemy (attacker is a tracked enemy, health ≤ 0), the director removes that body **and its head actor** at once instead of letting the corpse linger. The head is found by `entity::FindHead`: a u32 in the body's HeadManager that is the handle of another live entity within 3 m. The offset isn't confirmed yet; the director logs it (`head actor hXXXXXXXX (HeadManager+0x..)`) for the first 5 enemies. Heads are also removed with normal corpses and at run end. Untested in game.

## Hang 2026-10-15 19:41 (Horde, 42 types held, installed 11:45 build)
- The game stopped responding at about 19:41:30, during step 3 -> 4 of a Horde run (3 min in; enemies mostly killing each other, "infight #200"; pool 57/150; no preload or load event near it, nothing logged by us 19:41:27.7 -> 35.7). From 19:41:35 every spawn timed out (hash=0): the game thread was gone. Later the process went to 0% CPU, i.e. a deadlock, not a spin.
- Main thread (tid 17640, dump `tools/sigtools/dumps/hang_20261015_1941.dmp`): blocked in the wait wrapper exe+1bbf3f0 (called from exe+1896b29, a lock/condvar wait polled every 100 ms), reached from exe+187e320 (a frame-level function: it calls exe+1896a70 first, then update work) via exe+1700bfd, 172305a, 211c057, 198f935, 1c5e91c. The other threads are idle job workers; no crash-reporter frames. Who holds what it waits on: not found yet.
- Not the same as the earlier hangs (zero-health Mister Negative = crash-reporter deadlock; 160-170 entities = spin at exe+1d6cc90).
- User: was on low health fighting a minigunner when it froze. No hero_down was logged afterwards, so the hero's health never read 0 (or the game stopped updating it first). No death had been completed in this build yet.
- Lead: the main thread's stack passes through exe+211c057, a function that references `TargetQuerySystem::ProcessJob`, then 198f935 -> 1c5e91c (a wait on an "Unnamed Critical Section"). So it looks like the frame is waiting for AI target-query jobs that never finish while the workers sit idle (lost wakeup or a job that died). Unproven. Candidates to check: our teleports/relocations of stationary enemies, enemies killing each other (infight #200), a minigunner engaged at low hero health. Next time it hangs, take two snapshots 5 s apart (tools/sigtools/snapdump.py) before closing the game.
