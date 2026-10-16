---
title: Find the real model-capacity limit (white cubes / floating heads / invisible enemies)
labels: research, stability, help wanted
---

Past some number of held enemy models the game breaks "whatever loads last": white cubes, floating heads, invisible enemies, and sometimes crashes. We contain it with a
unit-type budget (`plan::kMaxTypes` = 44, `[run] max_types`), but the real limit and cause are unknown.

**Observations.** 33 types fine; 39 fine; 50 broke Olympus in Horde; a 57-type parade at Ryker's was fine; 86 broke. Candidates: number of live bodies and enemies, loading models mid-run,
something specific to a faction (Olympus), or a fixed pool that overflows.

**Ideas.**
- Experiments with the ini only: `max_types` 44 vs 57, `body_pool` 60 vs 150. Record which combination breaks and how.
- Detect a broken model at spawn time and skip that unit type for the run. Needs RE around `exe+15f9af0` (the null-mesh crash site is `exe+15f9bc9`; the null comes from around `[rsp+0E0h]`/`r15`).
- Find the pool the models come from (allocator tables near the actor asset manager `exe+60421e0`).

**Done when** we can say what the limit is, or detect and avoid a broken model at runtime.
