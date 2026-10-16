---
title: Screen stayed black after travelling to Manhattan (one occurrence)
labels: bug, needs-repro
---

After leaving Ryker's by the exit circle, the screen stayed black; the fade-in apparently did not take effect (`FadeTeleport` in `portal.cpp`; nothing was logged after "returned to the rykers dock"). The dev command `fade in 0.6` cleared it.

**Status: record only** until it repeats. If it does, report the log around the travel.
