---
title: Add unit tests for score.cpp and plan.cpp
labels: tooling, good first issue
---

`plan.cpp` and large parts of `score.cpp` are pure logic. `tests/plan_offline.cpp` prints a curve but asserts nothing. Add a small test runner (plain C++ asserts, no framework needed) covering: boss waves land on every 4th wave,
role unlocks by step, `BudgetedPool` respects the budget and the minimum factions, score ranks and the combo cap, and the `CampaignEnd` rows.
