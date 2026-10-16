---
title: Heavies and miniguns stand around instead of attacking
labels: bug, ai, help wanted
---

Players report heavies (miniguns and other `SlayerBot`/`SlayerRangedBot` units) standing around without ever noticing Spider-Man, in both modes.

**Context.** Enemies fight because we set `BotData.SpawnEngagedWithHero` right after `SpawnRaw` (see GAME_INTERNALS, "Enemy AI and aggro"). Units whose bot
class is in `CALM_CLASSES` (`tools/roster/gen_roster.py`: `SlayerBot`, `SlayerRangedBot`) are deliberately spawned *without* the flag: a workaround for crashes
(`exe+1fe51b5`, null `MoverStandard`) seen on 2026-10-13. A later investigation found that crash class was really caused by **dependency assets not being loaded**
(heads, weapons), which `spawn::Preload` now handles (BUGS.md, "Boss round 2 results"). The calm workaround may no longer be needed.

**Try.**
1. Spawn one `enm_heavy_maggia` engaged with preloads in place, several times. Watch the log line `MoverStandard present at spawn (engaged)`.
2. If stable, drop `SlayerBot`/`SlayerRangedBot` from `CALM_CLASSES`, regenerate `roster_data.inc`, and run a Campaign full-force wave and a Horde run.
3. If they still idle, look at what makes bots notice the hero (`LineOfSightAwareness`, `damage::Nudge`, `events::GroupAlert`).

**Done when** heavies join the fight within a few seconds in 10 consecutive spawns, with no crashes.
