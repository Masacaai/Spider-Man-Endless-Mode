---
title: Results panel: missing Continue/Retry buttons and wrong tier labels when a score tier is reached
labels: bug, ui, help wanted
---

In Horde, a run that reached the first score tier (about 15,000 points) showed the results panel with the first tier marked complete, but **no Continue/Retry buttons**
and all three tier labels read "500". Runs below the first tier (and Campaign runs) showed the buttons.

**Context.** The panel is the game's challenge results panel (activity 1, panelType 2) driven by `panels::Results`. The real game also sends
`awardedRewards` and `taskMasterCurrentValue/MaxValue/Delta`, and sends scores as unsigned ints (`UI_HUNT.md`, "Taskmaster combat challenge recording").
We currently send tier thresholds as doubles and `awardedRewards` listing the reached tiers (the latest guess, **untested when this was written**), and no Taskmaster fields.

**Try.**
1. Re-test with a score above tier 1. If the buttons are back, close this.
2. If not, replay the game's own challenge with `uilog on` and diff every field against ours (`gfx panel results` shows the panel on demand).
3. Candidate fixes: send the Taskmaster fields; send `rewardTiers` as the same type the game uses; find the panel step that waits on an award animation.

**Done when** the results panel always shows Continue and Retry and the three tiers show our thresholds.


**Resolved 2026-10-16.** Root cause: `rewards` must be an array of arrays (the panel reads `rewards[tier][0].value`); see GAME_INTERNALS, "Results panel data shape".
