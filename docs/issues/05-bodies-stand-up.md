---
title: Ragdolled bodies get back up into a fight pose
labels: bug, ai, help wanted
---

Corpses ragdoll, then stand up again into a combat idle pose and stay like that for the rest of the run (seen consistently in a full Campaign run).

**Context.** The mod does nothing to dead bodies; they stay until culled out of the camera's view (`director.cpp`, body pool). Their health is 0 (`bots` shows `health=0.0`).
Candidates: the animation graph returning to its combat idle once the ragdoll settles; bodies killed by other enemies (not the hero) behaving differently; physics streaming at a distance; our time-scale freezes.

**Try.** Compare a body killed by the hero with one killed by another enemy (`bots`, `near <r> animcontroller`). Look at the `AnimControllerComponent` state of a standing body vs a lying one.
