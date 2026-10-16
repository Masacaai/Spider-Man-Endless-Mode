---
title: Recheck white cubes on Sable jetpacks and Hammerhead guns
labels: bug, assets, good first issue
---

Sable jetpack troopers and Hammerhead's gunmen showed white cubes where jetpacks and guns should be, because their weapon/jetpack actors were not preloaded. `tools/assets/deps.py`
and `preload_deps.inc` now cover the dependency chain (`.actor -> rifle_firing config -> WeaponItemConfig -> ItemActor`), and nothing is released mid-run any more.

**Do.** Run a Horde run with both factions and confirm no cubes. If any remain, run `deps.py` again and compare the missing actor's path against the log's "not fully loaded" lines.
