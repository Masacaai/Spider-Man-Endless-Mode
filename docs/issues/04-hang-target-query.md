---
title: Hang inside TargetQuerySystem::ProcessJob (one occurrence)
labels: bug, needs-repro, stability
---

On 2026-10-16 the game hung 3 minutes into a Horde run, at low health fighting a minigunner. In the dump the main thread is blocked in a lock wait (`exe+1bbf3f0`, polled every 100 ms) reached from a
frame function (`exe+187e320`) via `exe+211c057` (which references the strings `Target::QuerySystem` and `TargetQuerySystem::ProcessJob`). All worker threads were idle and CPU was 0%: a deadlock or a lost
wakeup. No crash dump is written. See BUGS.md.

**Status: record only.** Work starts when this repeats.

**If you see it:** take two `tools/sigtools/snapdump.py` snapshots 5 s apart before closing the game, run `allstacks.py` on them, and attach them (redact paths) plus `EndlessMode.log`.
Suspects: our relocation of stationary enemies (teleports), enemy-on-enemy fighting, a bot targeting the hero at low health, despawns during an in-flight target query.
