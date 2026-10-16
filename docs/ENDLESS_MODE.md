# Endless Mode: design, controls, and dev reference

> **Parts of this document are historical** (it was written 2026-10-12/13). The code is the source of truth: wave formulas, the roster, the portal flow and the UI have all changed since. For current how-tos see [HOW_TO.md](HOW_TO.md), [ARCHITECTURE.md](ARCHITECTURE.md) and [LEDGER.md](LEDGER.md).

Game build 4.0630.0.0 only. Everything in this document is built in `EndlessMode/`. Status tags: ✅ verified in game, 🧪 built but untested in game.

## Playing it

| What | How | Status |
|---|---|---|
| Enter Ryker's and start a run | Stand still on the **dock pad** (Manhattan) for 3 s, or hold **F8** for 1 s | 🧪 |
| Leave Ryker's | Stand still on the **exit pad** at Ryker's, or hold **F9** for 1 s (also ends a run) | 🧪 |
| After you go down | The results are shown; once you're back on your feet you're returned to the dock | 🧪 |
| Run options | `scripts\EndlessMode.ini` `[run]`: mode, faction, shuffle, riot, first_wave | 🧪 |

The pads are set in game: stand on the spot and the dev runs `portal setdock` / `portal setexit`.

## Run structure (campaign)

A run is a sequence of **chapters**, one faction each, **4 waves per chapter**:

1. **Skirmish:** melee and batons, plus pistols from wave 3 on.
2. **Assault:** adds pistols, rifles and shields.
3. **Full Force:** adds rockets, snipers, heavies and specials (jetpacks, whips, swordsmen), plus some elites.
4. **Boss wave:** escalates over the run:
   - boss waves 1–2: a boss alone;
   - boss waves 3–4: a boss plus their crew;
   - boss wave 5 onward: 2–3 bosses at once, mixing the chapter's boss with bosses from factions already fought.

Default faction order is the user's list: Street Thugs → Fisk's Men → Inner Demons → Ryker's Inmates → Sable International → Hammerhead's Maggia, then Tombstone's Crew → Taskmaster's Drills. `shuffle` randomizes everything after the first faction (the seed is recorded). Once all 8 factions have had a chapter, the **cycle** repeats tougher: every role is in play from wave 1 of each chapter, health goes up a step, and more elites appear.

Other modes: `faction <id>` (one faction forever), `bossrush` (every wave is a boss wave), and `riot` on top of any mode (40 on the field, long waves).

### Difficulty knobs (all in `src/plan.cpp`, printable offline with `tests/plan_offline`)

| Knob | Formula | Why |
|---|---|---|
| Goons per wave | `min(6 + 2w, 30 + 4·cycle)` × step factor (0.85 / 1.0 / 1.15) | Waves stay a few minutes long; difficulty comes from what spawns, not from length |
| On the field at once | `clamp(8 + w + 2·cycle, 8, 20)` (riot: 40) | Insomniac's AI uses attack tokens: more bodies mostly means more pressure from ranged enemies |
| Goon health | `×(1 + 0.025·(w−1) + 0.35·cycle)`, capped at ×4 | Applied with the game's own `Health::SetHealth` after raising max health |
| Boss health | `×(1 + 0.5·cycle)` | Bosses are already tuned |
| Elite chance | Full Force: 10% + 5%·cycle; from cycle 2 on: 5% + 8%·cycle; capped at 50% | Elites are "super" or SWAT variants |
| Role caps on field | snipers 2+cycle, rockets 3+cycle, heavies 2+cycle (max 4), specials 4, elites 4+2·cycle (×2 in riot) | Readability: never 8 snipers at once |

Roster data comes from the game files. `tools/roster/mine_actors.py` decodes all 287 enemy `.actor` files (221 bots: class, base health, configs, BotData flags) into `tools/roster/enemies.txt`. `tools/roster/gen_roster.py` holds the curated factions and generates `src/roster_data.inc`.

### Bosses

| Boss | Actor | Class | Base HP | Note |
|---|---|---|---|---|
| Bounty Hunter | enm_baton_thug_bounty | ThugBot | 400 | safe mini-boss |
| Big Brute | coop_bc_08_enm_heavy | SlayerBot | 400 | safe mini-boss |
| Wilson Fisk | enm_kingpin_boss | FiskBot | ? | scripted fight in the story |
| Mister Negative | enm_mrnegative_swordsman | MrNegativeSwordsman | 300 | |
| Rhino / Scorpion / Shocker | rhino / scorpion / shocker | own classes | 600 / 450 / 600 | likely need their setpieces |
| Silver Sable | enm_silver_sable_ctns | SilverSable | 400 | DLC |
| Hammerhead / Mecha-Hammerhead | enm_hammerhead_boss_rough / enm_mechahammerhead | own classes | 900 / 440 | DLC |
| Tombstone | enm_tombstone | SlayerBot | 1000 | probably works standalone |
| Taskmaster | npc_taskmaster | TaskmasterBot | 500 | |
| Doctor Octopus | docock | DocOck | 1700 | reserved |

None of the bosses is verified yet. The director has a **boss watchdog**: if a boss never loses health and never moves for 40 s, or its health can't be read, it is removed and skipped for the rest of the run. The run never soft-locks on a scripted boss. `boss verify <name> 1|0` persists a verdict to the ini, and `run verified` uses only verified bosses.

## Aggro (why spawned enemies fight)

Every spawn sets `BotData.SpawnEngagedWithHero` (Bot component +0x168) right after `SpawnRaw` and before activation. This is the same flag the game sets per spawn on hideout waves. Verified in game on 2026-10-12. Details are in `recon/M1_natives_4.0630.md`.

## Telemetry

Every run writes `scripts\EndlessMode_runs\run_<date>_<time>.json`, rewritten every 10 s so a crash keeps the data, and adds a line to `runs.csv`. Each run file records:

- **Summary:** waves started and cleared, kills, hits taken, damage taken, hits landed, bosses defeated, stuck spawns removed.
- **Per wave:** faction, kind, bosses, goon count, field cap, health multiplier, start and clear time, kills, hits taken, damage.
- **Per enemy:** type, wave, spawn point and position, spawn and death time, hits on the hero, damage to the hero, whether it got stuck.
- **Every hit taken:** time, wave, attacker type, damage, health before and after, and the hero's position. The data comes from a hook on `HeroHealth::OnDamage` (exe+82cf70), with the attacker read from the damage message +0x138.
- **Samples at 2 Hz:** time, hero position, health, max health, enemies alive.
- **Events:** stuck spawns, blacklisted points, failed spawns, failed bosses.

Open the files in **`tools/runviewer/index.html`** by dragging run JSONs onto it. It shows health over time with wave bands and hit markers, enemies on the field, a top-down map (path, hits, spawns, blacklisted points), who hurt Spidey, kills per wave, and tables of waves and events.

## UI reuse (found in the game)

- **HUD text slots:** `exe+7793d0 Show(hud, slot, text, seconds, 0, 0, 0, 1)` and `exe+77a1e0 Clear(hud, slot, 0, 0)`. The `hud` object is at `exe+6d28f80` (one 0x5F8 element per player). These are what Script Hook's `AddMessage` wrapped. There are 21 slots (0–20). Slot 0 is the general notification box, which the game uses with 5 s durations. Slots 11, 14 and 20 are used by other HUD widgets. In game (2026-10-13): slot 0 is shown just below the middle of the screen (good; used for everything). Slots 2, 3, 4, 6 and 7 also display; 1, 5, 8, 9 and 10 showed nothing; 11-20 are untested. None of them is at the top. **To do:** replay a hideout and trace how its top-of-screen wave counter is drawn.
- **Localized text swap:** Script Hook's `AddLocalizedTextSwap` swaps the string behind a localization key. It could relabel existing UI (for example, the hideout objective text). Not wired yet.
- **Hideout UI:** hideouts show their own objective and bonus UI. It's driven by mission data, so reusing it would need the data route.

## Stress test

`stress [step=10] [max=200] [engaged] [floor=25] [faction]`. It measures a baseline, then adds `step` enemies every ~10 s and measures for 4 s (tick rate, which is the frame-rate proxy, and the worst frame). It stops below the floor, when a whole step fails to spawn, or at `max`. In engaged mode the hero's health is kept topped up. Results go to the HUD, the log and `scripts\EndlessMode_runs\stress_*.csv`. Still to confirm: does the tick rate track the actual frame rate? Compare against the Steam overlay FPS.

## Dev commands (`scripts\EndlessMode.cmd`)

| Command | What it does |
|---|---|
| `run [campaign\|faction <id>\|bossrush\|<faction id>] [riot[=N]] [shuffle] [seed=N] [wave=N] [verified]` | Start a run |
| `wave stop` / `wave status` / `wave riot [N\|off]` / `wave aggro on\|off` | Run control |
| `plan [from] [to] [options]` | Log the planned waves, no spawning |
| `portal setdock\|setexit\|enter\|leave\|on\|off\|show` | Portal |
| `boss list` / `boss <name> [dist]` / `boss verify <name> 1\|0` | Boss testing |
| `stress …` / `stress stop` | Stress test |
| `hud <text>` / `hud slot <n> <text>` / `hud clear` | HUD testing |
| `stats` / `fps [s]` | Live counters, tick rate |
| `arena rykers\|off`, `tp x y z`, `spawn …`, `despawn …`, `bots`, `botdata`, … | Older research tools |

The game only ticks while it is focused, so dev commands that need the game thread must be written while the game window is active (or queued with a delay).

## Not done yet (next ideas)

- Combo multiplier and Arkham-style score and medals (find the combo counter in memory).
- A bigger, more readable HUD for wave titles and counters (slot survey), and a results screen.
- Modifiers, currency, and the Aunt May shop.
- More arenas: an NYC tour, and Ryker's/Raft as visitable places.
- Visual markers for the dock and exit pads (none yet; the HUD shows a countdown while you stand on one).

## Modes (user design, 2026-10-13)

Both Manhattan docks lead to Ryker's (same arrival spot, start line and exit circle):

| Dock | Mode | What it is |
|---|---|---|
| Ryker's dock (first) | **Campaign** | 4-wave chapters per faction, boss waves (six verified bosses), cycles getting tougher, endless. The roguelike modifiers will go here between waves. |
| Second dock (was the Raft) | **Horde** | One endless stream, no waves, no bosses, no modifiers. The level goes up every 30 kills or 90 s. Level 1 is one faction; another joins every 2 levels (random order per run) until all 9 are mixed. On the field: 12 + 4/level up to `[run] horde_max` (40; tune it with the stress test). Roles unlock by level (heavies and snipers at level 4), elites from level 4 (+5%/level, cap 40%), health +6%/level. Ends when you go down ("OVERWHELMED"). |

Dev: `run horde` starts Horde without the portal; `plan_offline 18 horde` prints the curve.

**Difficulty rule (user, 2026-10-13): no health sponges.** Enemy health is never scaled for difficulty, in either mode. Difficulty comes from numbers on the field (growing up to `horde_max`, the stress-test cap), wave sizes, enemy-type mix and elites. The only health change is Hammerhead's 1.8x, which restores his fight length because his story gate cuts him off at 55%. The older 'Goon health' / 'Boss health' rows in the knob table above are obsolete.
