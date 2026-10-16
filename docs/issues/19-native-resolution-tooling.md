---
title: Tooling: report which natives moved after a game update
labels: tooling, good first issue
---

`tests/run_offline.cmd` resolves all natives against an exe on disk. After a game update many will fail at once. Extend it (or add a script) to print, for each failing native, the nearest similar match
(shorter pattern, same string references) so porting is faster. See REVERSE_ENGINEERING.md, "Porting to a new game build".
