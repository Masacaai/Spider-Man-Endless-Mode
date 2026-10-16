---
title: Balance Campaign and Horde from run telemetry
labels: design, data, good first issue
---

Every run writes `EndlessMode_runs/run_*.json` (hits taken per wave, attackers, health over time), viewable with `tools/runviewer`. Collect runs and propose changes to `plan.cpp`/`score.cpp` constants
backed by data: waves where players take disproportionate damage, roles that kill the most, how long waves take. Attach anonymized JSON files.
