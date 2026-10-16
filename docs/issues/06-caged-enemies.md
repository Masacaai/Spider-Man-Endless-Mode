---
title: Enemies get stuck inside cages
labels: bug, arena
---

Enemies sometimes spawn or end up inside the cages in the Ryker's yard and can't get out (for example a Sable agent and a demon). The relocation rules in `director.cpp`
(stationary rule and never-left-spawn rule) mitigate it, but caged enemies still happen.

**Ideas.** Remove the offending designer points from `rykers_points.h`; detect "inside a cage" using the navmesh (`NavEdgeResolver`); or reject spawn points whose surroundings match a cage pattern.
