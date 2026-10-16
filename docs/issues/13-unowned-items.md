---
title: Offer suit powers and mods the player has not unlocked
labels: research, roguelike
---

`EquipById` requires the item to be owned (`[record+0x34]`). Offering unowned items probably needs a temporary inventory grant (`Add`, vfunc 16) and removal at run end. Test on a save that has not unlocked everything
(the maintainer's saves are all 100%/NG+, so a save from early in the game is needed).
