# Build installed 2026-10-15 ~12:00 (after the Horde crash): check first
0. Faction-join lines now use the notification box (not the top banner).
1. **No crash at death** (the capture Value fix): die in a Horde run, respawn, repeat once.
2. **Faction joins at budget 44:** the log's `preloads for ...: N unit types held` should show 4 factions and a "X join the riot" banner at step 2. N must stay <= 44; the question is whether a run with ~44 types shows floating heads, white cubes or a crash (it was fine at 33; 50 broke Olympus).
3. Dock panel: header, goals with completed rows (cur == max), START button, empty default Rewards box; no description; the Ryker's exit circle is a notification-box prompt again.
4. `gfx caps` lists captured text objects (the START relabel depends on it).
5. Still pending from before: boss pop-up, abandon prompt (freeze, Choice3 stays, countdown), results panel after death (dismiss with Continue), never-left-spawn lines in the log, no cages occupied.

# Build installed 2026-10-15 (after the Campaign and Horde tests): what to check first
1. `gfx caps` in the log lists text objects (the dock panel's ABOUT / START retitle depends on it).
2. Dock circles: panel only while standing on the circle, gone after travelling; Ryker's exit circle uses the same panel (LEAVE).
3. Pools by unit-type budget (`[run] max_types=40`): Horde and Campaign runs show `preloads for ...: N unit types held` with N <= ~40; no floating heads, white cubes or crash through a long Horde run (the Olympus case). If it holds comfortably try 44, then 48.
4. Boss pop-up ("<boss> has joined the fight!") and no per-wave summary banner; Horde opening line in the notification box, no rank-up banners.
5. Abandon flow: step out of the yard: after 1.5 s freeze + "Abandon this run?" panel; interact abandons; the Choice3 button stays, then a 10 s countdown with "Please return to the yard."; going back in cancels.
6. Caged enemies: look for `never left its spawn spot` lines and no one stuck in the cells around x 1306-1310, z -818..-823.
7. Horde: live score panel, "<faction> join the riot" banners, results panel after death (does Continue dismiss it; do the summary rows show).

# Next session: start here (written 2026-10-15, end of the UI session)

The build installed at 01:06 on 2026-10-15 is the one to test first. Nothing below has run in game yet unless marked verified.

## Verified live this session (old build, dev commands)
- GFx data-object building (`gfx selftest` passed), the real wave banner (message type 21), the live score panel on the left, the results panel (it appeared; title cut off, summary rows blank, duplicate text popup: all fixed in the new build, not re-checked), the hideout-style preview panel, the "base complete" banner (type 13), the medium message with our text (type 7).
- The suit menu recording (names, descriptions, icons, preview paths) and the map recording (no UI calls for icons).

## Test pass for the new build (in this order)
1. **Startup:** log shows the natives table OK and `gfx: self-check passed` on first panel use. Any `FAILED` there means panels fall back to text.
2. **Dock circles (Manhattan, both) and the Ryker's exit circle:** the redesigned panel (see ledger): best run in the header (time, kills), goals with progress, the Rewards box retitled ABOUT with the description, the button reading START (LEAVE at Ryker's). First check `gfx caps` in the log (should list text objects captured at HUD load); if it lists none the retitle can't work. Does the panel hide on stepping off, and do the texts return to REWARDS/RETRY (check a real hideout circle)? Does pressing interact still travel?
3. **Campaign run, first waves:** the faction icon on each wave banner (Street Thugs use the crime icon; Tombstone and Taskmaster borrow icons); the counter reads WAVE n of 4 and the boss wave says FINAL WAVE; the 5-second pre-wave Get Ready countdown; the ENEMIES counter during waves and its removal between them; the reward offer text now includes the game's descriptions (does it fit the notification box? it may overflow).
4. **Horde run (`run horde fast god` first):** Get Ready countdown at the start; the live score panel with rank title; rank-up banners; "X join the riot" banners (no level wording anywhere); no wave banners; the death message "OVERWHELMED - n kills"; after the respawn teleport the **results panel** (title "HORDE - S - SPECTACULAR", four rows Enemy / Style / Combo / Survival, tier banners AMAZING / SPECTACULAR / ULTIMATE with 8000 / 20000 / 40000). Questions: do the summary rows show now? Do the token-style tier rewards look like we hand out tokens (change if so)? Does Continue (Cross / A / 4, or Interact) dismiss it? Is `EndlessMode_horde_scores.csv` written and shown on the dock panel afterwards?
5. **Scoring feel (`run horde` for real, no god):** is the style meter too fast or slow, are ranks reachable, are the Survival and Combo amounts sensible? Tune from `docs/SCORING.md`.
6. **Memory and models (long run, 15+ minutes, or the parade):** no white cubes, no invisible enemies, no crash. Both modes now use 5 factions per run and release assets 30 s after the run ends. `parade all` still shows cubes by design (86 types).
7. **Enemy placement:** nobody pops in in front of you (spawns are 18 m+ away, 45 m+ if in view); stationary enemies get moved closer out of view; bodies stay on the ground until the pool is nearly full and nobody is looking.
8. **Loading a save at Ryker's dock:** walking across the start line starts a run without leaving first; the exit circle leads to the right dock after a restart (`[portal] last_venue`).
9. **Dev checks:** `inv text <asset id>` for a couple of powers and mods (names should match the suit menu, including the modded Iron Arms); `gfx wave 3 6 icon_map_demons_base`; `uilog on` still works.

## Recon and open work (after the tests)
- **Map icons and waypoints:** icons come from actors with a MapElement component (docs/UI_HUNT.md, "Map recording"). Stand next to a hideout or challenge start, `near 15 mapelement`, read the component and its config, then try it on a dock circle. Waypoints: UIUpdateCustomWaypointAction (exe+7efd10).
- **Reward-pick screen:** nothing near-fullscreen found yet. The suit menu's preview video and image paths are known (`tools/assets/perk_text.json`). Keep looking (PopupShell, tutorial pop-up exe+13f1670, ObjectiveScreenShell).
- **Which UI for which message:** decide with the crime pop-up (ShowCrimeScanner), objective line (ShowMissionObjective), medium message types and the notification box, for variety (ledger, "UI variety").
- **Curated pools** (drop or special-case Equalizer, Quips, Sound Of Silence, Combat Analyzer, Proximity Sensor, Long Range Scanner, Silent Step, Bio Scanner), gadget lock, autosave suppression, pause/resume runs, unowned-items test (needs an early save).
- **Still open from earlier runs:** Mister Negative idle, the mass electrocution curiosity, jetpack and gun white cubes (recheck now that nothing is released mid-run), Horde pacing under the new silent escalation.

---

# (older) In-game test plan (2026-10-13 build)

Run these one at a time. After each one, the user reports what they saw and the dev reads the log.

1. **Startup.** Launch and load free roam. Dev: the log shows `43/43 natives`, the hook installed automatically, and `stats: hero damage hook on`, with no crash.
2. **HUD.** Dev sends `hud Hello from Endless Mode`. User: does a notification box appear, and where? Then try `hud slot <n> text` for n = 1…20 to find a better wave-banner slot.
3. **Dock.** User swings to the chosen Manhattan dock and stands on the exact spot. Dev sends `portal setdock`.
4. **Exit pad.** Dev sends `portal enter`, or the user presses F8 (it should teleport and start a run). User walks to a sensible exit spot at Ryker's. Dev sends `wave stop`, then `portal setexit`.
5. **Pads.** The user stands on the exit pad (3 s countdown, back at the dock). Then on the dock pad (3 s, Ryker's, and the run starts with a countdown).
6. **Campaign.** Play a few waves. Check the wave titles in the HUD, that Street Thugs come first, the Skirmish → Assault → Full Force progression, and the boss at wave 4 (Bounty Hunter or Big Brute).
7. **Death.** Go down on purpose. Expected: the results message, and a return to the dock about 3 s after respawning. Dev: the run JSON exists and opens in `tools/runviewer/index.html`.
8. **Bosses, one at a time** (`boss <name> 15` near the user). For each: does it fight? Health readable? Killable? Then `boss verify <name> 1|0`. Order: tombstone, bounty, big brute (coop heavy), mrnegative, sable, taskmaster, hammerhead, mecha, rhino, scorpion, shocker, fisk, docock.
9. **Health scaling.** `run wave=9`: the log shows `starts with … (scaling)`, and the enemies take visibly more hits.
10. **Stress.** `stress 10 200` at Ryker's, then `stress 10 120 engaged`. The user also reads the Steam overlay FPS at a few steps to confirm the tick rate matches the frame rate.
11. **Riot and shuffle.** `run riot shuffle` for the next Discord clip.
