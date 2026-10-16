# How-to recipes

Each recipe says which file to edit, what to look for, and how to check it worked. Setup and the build/install loop are in
[GETTING_STARTED.md](GETTING_STARTED.md). The code is the source of truth: older docs ([ENDLESS_MODE.md](ENDLESS_MODE.md)) still show
some formulas that have since changed.

## Contents

[Print the difficulty curve offline](#print-the-difficulty-curve-offline) | [Change wave size, field cap, role mix](#change-wave-size-field-cap-and-role-mix) | [Change Horde's curve](#change-hordes-curve) | [Add or change an enemy or faction](#add-or-change-an-enemy-faction-or-boss) | [Tune settings without code](#tune-settings-without-code-the-ini) | [Change scoring](#change-scoring-and-ranks) | [Edit the dock panel or results panel](#edit-the-dock-panel-or-results-panel) | [Show a message or banner](#show-a-message-banner-or-pop-up) | [Add a boon or curse](#add-a-boon-or-curse) | [Add a dev command](#add-a-dev-command) | [Call a new game function](#call-a-new-game-function) | [Add an arena](#add-an-arena-or-change-spawn-points) | [Read run telemetry](#read-run-telemetry) | [Debug a spawn](#debug-a-spawn-that-doesnt-work)

## Print the difficulty curve offline

```
EndlessMode\tests\run_plan.cmd 20 campaign 1        waves, mode (campaign|riot|bossrush|faction:<id>), seed, shuffle 0|1
```

It builds `plan.cpp` + `roster.cpp` into a small console program and prints, per wave: chapter, faction, kind (skirmish/assault/full
force/elite assault/boss+goons), goons, field cap, elite chance, bosses, unlocked roles, the actual unit mix, and the banner title.
No game needed. Use it for every balance change before you ever launch.

## Change wave size, field cap and role mix

All in `EndlessMode/src/plan.cpp`:

| Want | Edit |
|---|---|
| Bigger/smaller waves | `GoonCount` (`base = min(6 + 2*wave, 30 + 10*cycle)`, times a per-step factor `kStepMult`) |
| More/fewer enemies at once | `MaxAlive` (`clamp(8 + wave + 3*cycle, 8, horde_max)`); `horde_max` is also an ini setting |
| How often each role appears | `kBaseWeight[]` at the top (Melee, Baton, Pistol, Rifle, Shield, Rocket, Sniper, Heavy, Special, Elite) |
| When roles unlock | `Make`: the `on = ...` block (step 0 = skirmish: melee/baton; step 1 adds pistol/rifle/shield; step 2+ everything; later cycles everything from wave 1) |
| Caps so you never see 8 snipers | the `p.caps[...]` block in `Make` |
| Elite chance | later in `Make` (look for `elite`) |
| Boss waves | `Make` (`boss_wave`, `kind = "boss+goons"`); a boss never fights alone |

Design rule: **no health sponges**. Don't scale enemy health for difficulty; use numbers, mix and elites (see LEDGER.md).
Then run `tests\run_plan.cmd`, rebuild, test.

## Change Horde's curve

`plan.cpp`, `MakeHorde` and `HordeFactions`: the field cap (`24 + 8*(level-1)`, capped by `horde_max`), role unlocks by level,
factions joining (`kHordeWindow` in `plan.h` = factions fighting at once). Horde has **no visible levels**: the level is internal and
only drives how fast things escalate (`Options::horde_level_ms`, `run horde level_s=N` to speed it up while testing).

## Add or change an enemy, faction or boss

The roster is generated: `tools/roster/gen_roster.py` -> `src/roster_data.inc`. Don't edit the `.inc` by hand.

1. Edit the lists in `gen_roster.py` (factions are lists of `(actor file name, Role)`; bosses are `(display name, actor name)`).
2. The script needs `tools/roster/enemy_paths.txt` (in the repo) and `tools/roster/enemies.json` (**not** in the repo: mined from your
   own copy of the game):
   ```
   set MSMR_ARCHIVE=<game>\asset_archive          (only if the game isn't in the default Steam path)
   python tools\assets\extract.py tools\roster\actors <paths from enemy_paths.txt>     (extracts the .actor files; slow, big)
   python tools\roster\mine_actors.py tools\roster\actors tools\roster\enemy_paths.txt tools\roster\enemies.json
   python tools\roster\gen_roster.py
   ```
   (each script's first lines say what it takes; requires `tools/ALERT`, a clone of Tkachov/ALERT)
3. **Regenerate dependencies** (weapons, jetpacks, heads the actor creates at runtime), or the new enemy shows white cubes:
   `python tools\assets\deps.py EndlessMode\src\roster_data.inc EndlessMode\src\preload_deps.inc`
4. Test the enemy alone first: `spawn <path>` in the command file; watch the log for the `spawn: [1]..[8]` steps and
   `MoverStandard present at spawn`. Test a boss with `boss <name>`.
5. Check the memory budget: factions with many unit types eat the per-run type budget (`plan::UnitTypes`).
6. Special handling lives in `gen_roster.py`: `CALM_CLASSES` (spawn without the engaged flag), `GATE`/`HEALTH_BONUS` for bosses, `MIN_CYCLE`.

## Tune settings without code (the ini)

`<game>\scripts\EndlessMode.ini` is written with defaults on first launch.

| Key | Meaning |
|---|---|
| `[portal] enabled` | master switch for the circles and hotkeys |
| `[portal] radius`, `hold_seconds` | circle size, hold time |
| `[portal] key_enter`, `key_leave` | virtual-key codes (F8/F9 by default) |
| `[run] mode`, `faction` | dev default for runs started without a dock |
| `[run] shuffle`, `riot`, `riot_max`, `first_wave`, `countdown` | self-explanatory |
| `[run] horde_max` | max enemies at once in Horde (80 is about half the point where the game hung) |
| `[run] body_pool` | cap on live enemies + corpses together |
| `[run] max_types` | unit-type budget of a run's faction pool (20-80). Lower it if you see cubes or floating heads |
| `[bosses]` | per-boss on/off |
| circle positions | `dock_*`, `raft_dock_*` (set in game with the `portal setdock`/`setexit` commands) |

## Change scoring and ranks

`EndlessMode/src/score.cpp`. The design is in [SCORING.md](SCORING.md): style meter 0..700 (one rank per 100), hits +4, kills by role,
getting hit -60, decay after 2 s without hitting; kill points by role times the rank multiplier; combo bonus; survival bonus; final
rank = time-weighted average. Constants (`kTierScores` in `score.h`, role points, decay) are at the top of the file.
Campaign's summary is `score::CampaignEnd` (10 per enemy, 100 per wave reached, minus 5 per hit taken).

## Edit the dock panel or results panel

- **Dock panel (the circle in Manhattan)**: `portal.cpp`, `BoardFor(venue)` builds the title, best run and the goals shown; `panels::Records` sends it.
  It is the game's *hideout preview* panel (activity 3), so it has a header (time, kills), sub-objectives (`label`, `currentCount`, `maxCount`;
  equal means completed) and a Rewards box we leave default. Only the START button text is changed (`gfx::SetText` on the RETRY key).
- **Results / live score**: `panels.cpp` (`Results`, `LiveScore`), the challenge panel (activity 1). Rows are `{label, value}` pairs.
  The title is cut off at about 30 characters.
- Panels animate on game time: don't freeze the world until they have faded in (`panels::Tick` waits 3 s).
- Anything you can show must exist in the game's UI; see [UI_HUNT.md](UI_HUNT.md) for what each panel accepts. Add a quick test
  command in `main.cpp` (`gfx panel ...`) before wiring a new panel into a mode.

## Show a message, banner or pop-up

| What | Call |
|---|---|
| General notification box (centre-low, supports button glyph tokens) | `hud::Show(text, seconds)` |
| Top-of-screen banner | `hud::Banner(text, seconds)` (falls back to the notification box) |
| Wave banner with faction icon | `gfx::Message(21, "", {icon, currentWave, maxWave})` |
| Crime pop-up (boss entrance) | `gfx::Invoke(0x210, 0x38, "ShowCrimeScanner", {...})`, then `HideMission` later |
| Big countdown / counter | `hud::Countdown(n)` / `hud::Counter(label, cur, max)` |
| Slow-motion / freeze | `timescale::Set(0.25f, 4.0f)`, `Freeze()`, `Unfreeze()` |

Never hard-code button names in text: `input::Glyph(input::Action::Interact)` gives the glyph token for the device in use.
Use the notification box for general messages, and skip it only where a panel is already doing the same job.

## Add a boon or curse

`EndlessMode/src/boons.cpp`: the pool is built at run start from the game's own inventory categories (suit powers and suit mods, never skills),
plus a few stat boons; names and descriptions come from the game's text (`inventory::Text`), not from us.

- **Stat boons**: look for the `Stat` kind and the multiplier helpers (damage dealt/taken multipliers near line 100).
- **Curses**: the `kCurses` table (`name`, upside, downside) and where each `HasCurse(...)` is read. Rule: every curse has an upside and a
  downside, and downsides never add enemy health.
- **Removing items from the pool**: `kCuratePools` and the blocklist in `boons.cpp` (currently off; the candidates are in LEDGER.md).
- The offer UI is the notification box plus the choice buttons; `kOfferTimeoutMs` is the AFK timeout.

## Add a dev command

In `src/main.cpp`, find `RunCommand` and the chain of `else if (cmd == "...")`. The arguments come from a stream named `in`:

```cpp
} else if (cmd == "mything") {
    // mything <n>: what it does, in one line (the dev command list reads these)
    int n = 0;
    in >> n;
    gamethread::Post([n] { /* touch game state here */ LOG("mything: %d", n); });
}
```

Log the result (`LOG`); commands have no other output. Read-only commands are safe; for anything that writes game memory, say so in the comment.

## Call a new game function

1. Find it ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)) and make a unique signature.
2. Add an enum value in `natives.h`, a table entry in `natives.cpp` (name, pattern, rel32 offset or `kDirect`, **expected RVA**).
3. `tests\run_offline.cmd`: it must say the new native resolved at the expected RVA with 1 hit.
4. Wrap the call in a small function with a typed signature, call it only through `gamethread::Run/Post`, and handle failure
   (`natives::Get(id)` returns 0 if it didn't resolve).
5. Write what you learned (arguments, side effects) in GAME_INTERNALS.md with a confidence tag.

## Add an arena, or change spawn points

`arena.h/.cpp` (`Bounds`): teleport point, bounds, designer spawn points (`rykers_points.h`, generated from the level's gameplay `.zone`), start
line and radius, exit circle, invisible fence polygon, rescue point, and the invisible wall at the start line. `recon/portal_points.txt` and `recon/*_walk_*.csv` are recorded examples of the point data (positions logged by walking the arena with `hero`); the
designer placements themselves were read from the level's gameplay `.zone`.

## Read run telemetry

Each run writes `scripts\EndlessMode_runs\run_<date>_<time>.json` (rewritten every 10 s, so a crash keeps the data). Drag it onto
`tools/runviewer/index.html` in a browser: health over time with wave bands and hit markers, enemies on the field, a top-down map,
who hurt Spidey, kills per wave. Fields are listed in [ENDLESS_MODE.md](ENDLESS_MODE.md#telemetry).

## Debug a spawn that doesn't work

The spawn log has 8 numbered steps (`spawn: [1] load ...` to `[8] done`):

| Stuck at | Meaning |
|---|---|
| `[1] failed (timeout=1 hash=0 asset=0)` | the game thread isn't running our jobs (game unfocused, hung, or the hook isn't installed) |
| `[1]` loads but `[2]` never shows "asset loaded" | wrong path, or the asset isn't in the archive (`toctool hash <path>` and `check`) |
| `[3] SpawnRaw` returns 0 | bad transform, or the world isn't ready |
| spawns but is a white cube / bodiless | missing dependency actors: regenerate `preload_deps.inc`; check the "not fully loaded" line |
| spawns, never fights | not engaged (`engaged:` line), or the unit is in `CALM_CLASSES` |
| spawns and crashes | take the dump through REVERSE_ENGINEERING section 4 |
