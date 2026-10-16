#pragma once

#include <cstdint>
#include <string>

// The hero's inventory (perks, suit mods, suit powers and gear are inventory items keyed by their config's asset id).
// GameInventoryManager (vtable exe+38e3148) routes each item to a sub-inventory (vfunc 0x3B8/8) and:
//   vfunc 16 (exe+1f5b720): Add(mgr, &result, asset_id, count)      - what AddToInventoryAction calls
//   vfunc 19 (exe+1f5b9d0): Remove(mgr, &result, asset_id, count)   - MultiInventoryRemove with one item
//   vfunc 18 (exe+1f5b880): (mgr, asset_id, int) -> sub vfunc 0x90  - probably a count query [?]
//   vfunc 45 (exe+bbf790):  CanAdd(mgr, asset_id, count) -> bool
// Static RE only (2026-10-13); the `inv` command exists to verify it live. Game thread only.
namespace inventory {
    uintptr_t Manager(uintptr_t hero);   // the hero's GameInventoryManager component, or 0
    void Command(const char* sub, uint64_t asset_id, int count, uintptr_t hero);
    // Logs every item the game itself adds or removes (manager Add / Remove / slots 17-18, MultiInventoryRemove), with
    // the caller, to learn what equipping a suit power or mod does. Hooks are installed on first use.
    void Watch(bool on);

    // Equip through the game's own path (verified by watchpoint 2026-10-13: the menu's mod equip writes the loadout
    // block from exe+cc5fc0). Equip manager = exe+70957c0 (loadout block at +0xD068, second copy at +0xD0A8, owned
    // items table at +0x8808).
    //   exe+cc6200 EquipById(mgr, asset_id): owned + unlocked only; slot by category (suit 0, power 1, mods first free
    //              of 2..4, else 4); no-op if already equipped.
    //   exe+cc5fc0 Equip(mgr, record, slot, also_second_copy): unequips the previous item (exe+cca680), writes the
    //              slot, fires the equip event. record = *lookup(mgr+0x8808, asset_id) (lookup exe+1bcfaf0).
    //   exe+cbf380 MenuEquip(record, slot, flag): Equip + activate the item (mods need this; powers don't).
    // slot >= 0 goes through MenuEquip (effects verified to need it); slot < 0 -> EquipById (no activation). Game
    // thread only.
    bool Equip(uint64_t asset_id, int slot);
    // True when the hero exists and has health (equips are refused otherwise). Game thread only.
    bool Ready();
    // Unequip through exe+cca680 (zeroes its slot and deactivates the item), like the menu's unequip button.
    bool Unequip(uint64_t asset_id);

    // Loadout lock for roguelike runs: while on, Equip (exe+cc5fc0) refuses every call that isn't ours, so the pause
    // menu still shows the loadout but can't change it (the game's own UIEnableChangeSuitAction flag, byte +0x1C4 of
    // the UI object at exe+6d88f10, did NOT stop menu equips when tested). Hook installed on first use.
    void Lock(bool on, bool allow_suit = true);   // allow_suit: suit changes (slot 0) still go through
    bool Locked();

    // The game's own classification of an item (owned-items table, mgr+0x8808): category 0 suit, 1 suit power,
    // 2 suit mod (other values: other item kinds). Game thread only.
    struct ItemInfo { bool found = false; bool owned = false; bool locked = false; int category = -1; };
    ItemInfo Info(uint64_t asset_id);

    // The game's own display text for an item, as the suit menu shows it (uilog recording 2026-10-15, builder exe+7b8150):
    // on the item data record, name = localized string for the hash at +0x24, description = the string for the hash at
    // +0x34 formatted with the number at +0x38, icon name at +0x48, preview image path at +0xB8, preview video config
    // at +0xA0 (all char*). This includes mods' renamed text (a user's Iron Arms replacement shows its own name).
    // Runs on the game thread (blocks, up to 2 s).
    struct ItemText { bool ok = false; std::string name, desc, icon, image, video; };
    ItemText Text(uint64_t asset_id);

    // Current loadout block A: [0] suit, [1] power, [2..4] mods (asset ids, 0 = empty). Any thread (plain reads).
    void Loadout(uint64_t out[5]);
    // Empties the suit power slot (A[1] and its copy B[1]); the game's unequip refuses to. Game thread only.
    void ClearPowerSlot();
}
