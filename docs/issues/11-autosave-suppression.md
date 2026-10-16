---
title: Suppress the game's autosave during a run
labels: feature, roguelike, research
---

The run changes the player's loadout; if the game autosaves mid-run and then crashes, the save contains the run loadout. `runloadout` restores on the next launch, but suppressing autosaves during runs
would make a crash harmless. Find the autosave trigger and block it while `director::Running()`.
