# Horde scoring and style ranks (design, 2026-10-14)

User request: Devil May Cry style rankings at the end of Horde runs, with our own scoring system and Spidey-themed ranks.
Presented with the game's own Taskmaster challenge UI (docs/UI_HUNT.md): the live score panel on the left during the
run, and the expanded results panel (score breakdown, total, best) that stays until the player presses Continue.

## Live style meter (DMC-like)
A meter from 0 to 700, shown as a rank (each rank is 100 points wide).
- **Rises with:** each hit dealt (+4), each kill (+15 to +40 by enemy role), and combo length (read from the game's own combo counter; we already see `SetComboCounter(n)` calls). Every 10 hits in a combo adds +10.
- **Falls when you're hit:** -60 per hit (shown as a rank drop).
- **Decays:** about 15 per second after 2 s without dealing damage; faster at high ranks.
- **Variety:** repeating the same enemy role with no other kill in between earns less (x0.5 after 3 in a row). This keeps "farming one type" from being stylish.

| Rank | Title | Kill-score multiplier |
|---|---|---|
| D | Puny Parker | x1.0 |
| C | Human Spider | x1.2 |
| B | Wall-Crawler | x1.5 |
| A | Amazing | x2.0 |
| S | Spectacular | x2.5 |
| SS | Sensational | x3.0 |
| SSS | Superior | x4.0 |

The live panel shows the score and the current rank title as its label (e.g. "SPECTACULAR"). Rank changes also
get a medium-message banner.

## Score
Kill points by role, multiplied by the style rank at the moment of the kill:

| Role | Points |
|---|---|
| Melee, Baton | 100 |
| Pistol | 120 |
| Rifle | 150 |
| Shield | 200 |
| Rocket, Sniper | 250 |
| Special (whips, jetpacks, ...) | 300 |
| Elite | 400 |
| Heavy | 500 |

Infighting kills score nothing (they're already excluded from stats).

## Results panel (end of a Horde run, stays until Continue)
Four rows (user, 2026-10-15; Horde has no levels):
1. **Enemy Score:** raw kill points.
2. **Style Bonus:** what the style-rank multipliers added on top.
3. **Combo Bonus:** for each combo that ended, length^2 / 2 (max 2000 each). A combo breaks after 3 s without a hit or when the hero is hit.
4. **Survival Bonus:** 2 points per second survived, deliberately small (dodging around shouldn't pay much).

Total, best total (saved between runs), and the **final rank**: the time-weighted average style rank over the run,
bumped up one rank for a new best. The panel's three tier banners use the game's AMAZING / SPECTACULAR / ULTIMATE
LEVEL texts with our score thresholds (8000 / 20000 / 40000). Title: "HORDE - S - SPECTACULAR".

## No levels in Horde (user, 2026-10-15)
Horde is one continuous riot with factions joining in; nothing the player sees mentions levels. Internally the
difficulty still steps up every minute (more enemies, another faction), but silently: a banner appears only when a
faction joins ("Street Thugs join the riot"). Run history for the dock panel is `EndlessMode_horde_scores.csv`
(date, total, final rank, seconds); the panel shows the best score and the last two as "23,200 - S Spectacular (date)".

## Open points
- Values are first guesses; tune after a few runs (the run JSON already records kills by type, hits, time).
- Campaign could use the same meter later (wave-by-wave grades).
- Needs GFx object and array building from code before any of the panels work (next UI step).
