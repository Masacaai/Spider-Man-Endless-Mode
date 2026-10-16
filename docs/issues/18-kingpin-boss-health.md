---
title: Kingpin takes no damage outside his story phases
labels: bug, bosses, research
---

Kingpin appears and fights but is invulnerable (benched). His `BossHealth` layout differs (`+0x90` max, `+0x98` current, versus `+0x80/+0x88` for the others) and writing current health didn't stick.
`entity::ReadHealth` also reads garbage for him. Find the gate that makes him invulnerable and whether it can be cleared.
