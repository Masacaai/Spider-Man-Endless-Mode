# Campaign roguelike layer: research and plan (2026-10-13)

## What the game already has: the perk system
Suit mods, suit powers and skill-tree upgrades are all **perks** in MSMR. The exe has 117 perk-related classes (`PerkSystem`, `PerkItem`, `PerkItemConfig`, `PerkEquipItem`, plus one class per effect). The asset archive has **119 `PerkItemConfig` assets**, extracted to `tools/assets/perks/` (JSON next to each; tool: `tools/assets/extract.py`).

A perk config is a list of levels, each a small, data-driven effect primitive. Examples, read from the files:

| Perk (internal name) | Effect primitive | Values |
|---|---|---|
| Fitness | `HealthPerkPrius` | +25 / +50 / +50 max health |
| DEFENSE_BALLISTIC | `IncomingDamageModPerkPrius` | bullets x0.65 |
| InsulatedFibers | `DamageModPerkMultiPrius` | explosions and RPGs x0.5 |
| Resolve | `AssaultMeterSegmentsPerkPrius` | +1/2/3 focus segments |
| Sustained_Assault | `AssaultModeDrainPerkPrius` | focus drain x0.7 |
| Vengeance | `VengeancePerkPrius` | assault boost 3 |
| Second Wind | `HealthOnFinisherPerkPrius` | heal on finisher |
| DeathBlossom (Web Blossom) | `DeathBlossomPerkPrius` | 50 random + 10 targeted web shots/s, 12 m |

Effect primitives available (classes): health, health on kill / finisher / accurate dodge, incoming and outgoing damage mods (filtered by damage or weapon type), status-effect duration cuts, focus meter segments / drain / fill, gadget ammo / resupply / recharge, velocity damage, vengeance, low gravity, holo decoy, Spider-Bro, storm bolt, electric blast, invisibility, Noir, loot drop rate.

Suit powers found (internal names; in-game names still to be confirmed from localization): DeathBlossom, DefenseShield, BlurProjector, ElectroBlast, IronArms (two), LowGravity, NegativeBlast, Quip, Vibranium_alloy, ReducedBulletDamage, SpiderBro, RockNRoll, Velocity, Overpowerd, FillFocusMeter, NoirSuitPower, GadgetRefill, ConcussionPunch, ElectricPunch, Charged mesh, SpiritFire, FinalSuitPower, DoubleDamage.

Runtime hook into it [I]: the exe contains `SuperiorSpiderManActionHandleAddPerkLevel` and the action name `AddPerkLevel`, which are mission-script actions that grant perk levels. There are also `RemoveSuitPower` / `OnRemoveSuitPower`. If we can call the AddPerkLevel handler ourselves, any suit mod or suit power becomes a run reward, granted and removed at runtime, with the game doing all the gameplay work. **Not verified yet: this is the main RE spike.**

No prior art: no PC mod found that grants or stacks suit powers at runtime.

## Levers by readiness
**Already in hand** (hooks and natives we use today):
- Incoming damage scale: hero damage hook (`HeroOnDamage`).
- Outgoing damage scale: bot damage hook. This scales the player's damage; the no-sponge rule is about enemy health, so player power is fair game.
- Healing and heal-on-event: `Health::SetHealth` on HeroHealth.
- Kill and hit events (lifesteal, streaks) from the stats hooks.
- Enemy numbers, mix, elites, factions, spawn rules: the director.
- HUD text, fades, input (F / 1-2-3 keys, controller via XInput and HID).

**Needs RE:**
1. AddPerkLevel and RemoveSuitPower: grant real suit mods and powers. This is the big one.
2. Focus meter: read and write, for "start each wave with full focus" or "focus on dodge".
3. Gadget ammo: refill, infinite gadgets.
4. Max health: HeroHealth +0x80. Writing max might just work; untested.

## Proposed run structure
- Keep Campaign as is: factions in chapters of 4 waves, boss waves, endless cycles.
- **After each cleared wave: pick 1 of 3 boons.** HUD shows three cards, chosen with 1/2/3 or D-pad left/up/right.
- **After a boss: pick 1 of 3 suit powers** (legendary tier), once perks work.
- **Chapter end: an optional bittersweet curse.** Every curse pairs an upside with a downside (user, 2026-10-13: no pure curses; bittersweet balances better), e.g. "+30% damage dealt, but snipers from wave 1", or "heal fully between waves, but elites double". Downsides follow the no-sponge rule: more enemies, more elites, snipers from the start, no healing between waves, gadgets locked, a wave timer. Never extra enemy health.
- Boons stack and level (I/II/III, like perk levels), and rarity rises with depth.
- Death ends the run. Score, seed and boons go into the run log. **True zero-start (user): no meta-progression.** Every run starts identical; only records (best wave, seeds, run history) persist.
- Baseline reset (user undecided): (a) strip perks at run start and restore them at run end (needs perk read/add/remove, plus a save-safety snapshot and restore), or (b) fake a baseline with damage scaling. Research first.

## Build order
1. Choice UI and the boon framework, using only the levers already in hand (damage in/out, healing, lifesteal, enemy-side pacts). Testable right away.
2. RE spike on AddPerkLevel, then suit mods and powers as boons.
3. Focus and gadget levers, then more boons.
4. Balance passes using run telemetry.

## Research log (2026-10-13, static RE; details in recon/M1_natives_4.0630.md)
- **Granting and removing items (perks included) is mapped:** the game's own `AddToInventoryAction` calls the hero's inventory component `vfunc[16](inv, &result, asset_id, count)`, after a `vfunc[45]` can-add check. `RemoveFromInventoryAction` uses a MultiInventoryRemove builder. All 119 perk asset ids are known, so in principle any perk can be given or taken for a run.
- **Open:** owning a perk vs **equipping** it (suit mods sit in slots, one suit power at a time). Leads: `ProgUpgradeEquipEvent` / `ProgUpgradeUnequipEvent`, `ProgressionUpgradesAction` (a mission action that drives upgrades, possibly the game's own "lock or strip upgrades for a mission" control), `EquipManager`, `SpiderSkillEquipItem`, `PerkEquipItem`, `HeroLoadoutCloner`.
- Saves backed up to backups/saves_20261013_pre_perks before any live test.
