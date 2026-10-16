# Endless Mode for Marvel's Spider-Man Remastered

An endless-survival mod for the PC version of *Marvel's Spider-Man Remastered*. Take the Manhattan docks to **Ryker's
Island** and fight wave after wave of enemies from every faction in the game.

> **Unofficial fan project.** Not affiliated with or endorsed by Insomniac Games, Sony or Marvel. It ships **no game
> assets**; you need your own legitimate copy of the game. See [Legal](#legal).

**Works with game build 4.0630.0.0 only** (`Spider-Man.exe` file version must be exactly `4.0630.0.0`). The mod finds game
functions by signature and checks each one against an expected address, so on any other build it refuses to call
into the game instead of crashing it.

## What it does

Two modes, each from its own dock circle in Manhattan:

| | **Campaign** | **Horde** |
|---|---|---|
| Structure | Chapters of 4 waves, one faction per chapter, a boss on every 4th wave, endless cycles that get tougher | One continuous riot; factions join as it goes |
| Between waves | Pick 1 of 3 **boons** (suit powers, suit mods, stat boons); bittersweet **curses** after bosses | none |
| Bosses | Mister Negative, Tombstone, Taskmaster, Hammerhead, Silver Sable | none |
| Scoring | Results panel: enemy score, wave bonus, hits taken | Style meter with DMC-style ranks, combo bonus, survival bonus, tiers |
| Ends | You go down, or you step out of the yard and abandon | same |

You start every Campaign run with **no suit power and no suit mods** (your real loadout is saved and restored at the end)
and earn them wave by wave. The mod uses the game's own HUD panels: the hideout-style circle panel at the docks, the
Taskmaster-challenge score panel and results panel, crime pop-ups for bosses, and so on.

Design rules we hold to (see [docs/LEDGER.md](docs/LEDGER.md)): no health sponges (difficulty is numbers, mix and elites,
never extra enemy health), boons never touch Spider-Man's skills, every curse has an upside.

## Install (players)

**Download:** [Releases](https://github.com/Masacaai/Spider-Man-Endless-Mode/releases) (latest: alpha; grab `EndlessMode.script`).

1. Install [Overstrike](https://www.nexusmods.com/marvelsspidermanremastered/mods/4199) 1.8.1 and enable
   **"Enable .script support"** and **"Add '-scripts' to commandline.txt"** in its settings.
2. Overstrike's scripts proxy has a known issue where it can start game scanning inside helper processes; see
   [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md#2-overstrike-and-the-scripts-proxy) for the options.
3. Add `EndlessMode.script` (from the Releases page, or built with `build.cmd`) in Overstrike and press **Install mods**.
4. Launch the game through Steam, load a save, and go to a Manhattan dock circle.

Back up your saves first. The mod changes your suit loadout during Campaign runs and restores it afterwards; if the game
crashes mid-run it restores on the next launch. Your settings and records live in `Documents\Marvel's Spider-Man Remastered\EndlessMode\` (not in `scripts\`, which Overstrike wipes every time you press Install mods), so they survive updates and reinstalls.

## Build (contributors)

Needs Visual Studio 2022 (Desktop C++) and a copy of the game for testing.

```
EndlessMode\build.cmd               -> EndlessMode\build\EndlessMode.dll and EndlessMode.script
EndlessMode\tests\run_offline.cmd   -> resolves every game function against Spider-Man.exe on disk (game need not be running)
```

Full walkthrough: **[docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)**.

## Repository map

| Path | What |
|---|---|
| `EndlessMode/src/` | The mod (C++20, about 8,000 lines). Module-by-module tour in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| `EndlessMode/tests/` | Offline checks: signature resolution and the wave planner |
| `EndlessMode/data/` | Source for an optional Overstrike `.stage` (spawner config) |
| `docs/` | Everything we learned and decided; start at [docs/README.md](docs/README.md) |
| `recon/` | Reverse-engineering notes and recorded paths (addresses are build-specific) |
| `tools/sigtools/` | Python/C++ helpers for crash dumps, signatures, vtables, call sites |
| `tools/roster/` | Mines the game's enemy actor files into the mod's roster |
| `tools/assets/` | Extracts item/dependency data from the game's archives (run locally; output is never committed) |
| `tools/toctool/` | Searches the game's asset archive table of contents |
| `tools/runviewer/` | Browser viewer for the telemetry JSON each run writes |

## AI-friendly

This project was built with an AI coding assistant (Claude Code) and is set up for AI-assisted work. [AGENTS.md](AGENTS.md) tells coding agents
how to work here (rules, commands, what they can and can't verify), and the documentation is written so an agent, or a person, can pick it up cold.
AI-assisted contributions are welcome; say so in the PR, and make sure a human has built and tested the result.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). The whole point of the docs is that you can change things yourself:
[docs/HOW_TO.md](docs/HOW_TO.md) has recipes ("change the difficulty curve", "add an enemy", "add a dev command"), and
[docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) explains how the addresses here were found, so you can find
your own. Open issues are drafted in [docs/issues/](docs/issues/).

## Legal

- No game assets, extracted game data, or Insomniac source or leaked material is in this repository, and none must be
  added. `.gitignore` excludes the extraction outputs; keep it that way.
- The mod is a script for Overstrike (GPLv3, by Tkachov) and uses MinHook (BSD-2). It does not include code from the MSMR
  Script SDK or from Script Hook; those were read only as references for how the game works (see
  [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md#credit-and-prior-art)).
- **License: GNU General Public License v3.0 or later** ([LICENSE](LICENSE)). Anyone may use, study, change and share this software, and
  changed versions must stay free under the same terms. There is no individual owner: it belongs to everyone who contributes. Notices for
  the statically linked MinHook library are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
