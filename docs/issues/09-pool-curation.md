---
title: Curate the boon pools (owner decision)
labels: design, owner-decision
---

All unlocked suit powers and mods are currently offered as boons. Some do nothing useful in the arena and should come out. Candidates (see LEDGER.md): Quips, Sound Of Silence, Blur Projector,
Combat Analyzer, Proximity Sensor, Long Range Scanner, Silent Step, Bio Scanner; mods that need gadgets (Resupply, Battle Tech, Scavenger, Metabolic Converter). Equalizer ("everyone goes down in one hit, including you") is
a special case: it is better as a curse.

The blocklist code exists in `boons.cpp` behind `kCuratePools` (off). **The maintainer has the final say on the list**; this issue is for discussion, then a PR once decided.
