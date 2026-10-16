---
title: Mister Negative sometimes does not engage
labels: bug, ai, bosses
---

In Campaign, Mister Negative appears (the boss pop-up shows) but sometimes does not fight until much later. He is a `MrNegativeSwordsman` bot spawned outside his mission.

**Try.** Compare awareness and group state with a boss that works (Tombstone). Check the engaged-flag path in `spawn.cpp` for this class, and `LineOfSightAwareness`. Log his position every second for the first 20 s of the fight.
