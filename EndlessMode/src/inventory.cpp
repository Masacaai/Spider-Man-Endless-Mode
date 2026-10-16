#include "inventory.h"

#include "entity.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "scan.h"
#include "hud.h"
#include "gamethread.h"

#include <MinHook.h>
#include <atomic>
#include <intrin.h>

#include <Windows.h>
#include <cstring>
#include <memory>
#include <string>

namespace inventory {
    using AddFn = void* (*)(uintptr_t mgr, void* result, uint64_t id, int count);
    using CanAddFn = bool (*)(uintptr_t mgr, uint64_t id, int count);
    using QueryFn = uintptr_t (*)(uintptr_t mgr, uint64_t id, int arg);

    static uintptr_t VFunc(uintptr_t obj, int slot) {
        uintptr_t vt = 0, fn = 0;
        if (!memory::Read(obj, vt) || !vt || !memory::Read(vt + slot * 8, fn)) return 0;
        return fn;
    }

    // SEH-guarded calls (no C++ objects with destructors in these frames).
    static bool CallAdd(uintptr_t fn, uintptr_t mgr, void* res, uint64_t id, int n) {
        __try { ((AddFn)fn)(mgr, res, id, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool CallCanAdd(uintptr_t fn, uintptr_t mgr, uint64_t id, int n, bool& out) {
        __try { out = ((CanAddFn)fn)(mgr, id, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool CallQuery(uintptr_t fn, uintptr_t mgr, uint64_t id, int arg, uintptr_t& out) {
        __try { out = ((QueryFn)fn)(mgr, id, arg); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    uintptr_t Manager(uintptr_t hero) {
        return hero ? entity::FindComponent(hero, "GameInventoryManager") : 0;
    }

    static void LogResult(const char* what, const uint8_t* res) {
        char hex[3 * 48 + 1] = {};
        for (int i = 0; i < 48; i++) snprintf(hex + i * 3, 4, "%02X ", res[i]);
        LOG("inv: %s result: %s", what, hex);
    }

    void Command(const char* sub, uint64_t id, int count, uintptr_t hero) {
        uintptr_t mgr = Manager(hero);
        if (!mgr) {
            std::string names;
            entity::ForEachComponent(hero, [&](const std::string& n, uintptr_t inst) {
                if (n.find("nvent") != std::string::npos || n.find("Loadout") != std::string::npos || n.find("Equip") != std::string::npos) {
                    char b[96];
                    snprintf(b, sizeof(b), " %s@%llx", n.c_str(), (unsigned long long)inst);
                    names += b;
                }
            });
            LOG("inv: no GameInventoryManager on the hero; related components:%s", names.empty() ? " none" : names.c_str());
            return;
        }
        LOG("inv: GameInventoryManager @%llx", (unsigned long long)mgr);
        if (!strcmp(sub, "find")) return;
        if (!strcmp(sub, "can")) {
            bool ok = false;
            bool called = CallCanAdd(VFunc(mgr, 45), mgr, id, count, ok);
            LOG("inv: can-add %016llx x%d -> %s", (unsigned long long)id, count, called ? (ok ? "yes" : "no") : "CRASHED (caught)");
        } else if (!strcmp(sub, "count")) {
            uintptr_t r = 0;
            bool called = CallQuery(VFunc(mgr, 18), mgr, id, count, r);
            LOG("inv: query(18) %016llx arg %d -> %llx%s", (unsigned long long)id, count, (unsigned long long)r,
                called ? "" : " CRASHED (caught)");
        } else if (!strcmp(sub, "add") || !strcmp(sub, "remove")) {
            alignas(16) uint8_t res[0x200] = {};
            int slot = !strcmp(sub, "add") ? 16 : 19;
            bool called = CallAdd(VFunc(mgr, slot), mgr, res, id, count);
            LOG("inv: %s %016llx x%d -> %s", sub, (unsigned long long)id, count, called ? "called" : "CRASHED (caught)");
            LogResult(sub, res);
        } else {
            LOG("inv: find | can <id> [n] | count <id> [arg] | add <id> [n] | remove <id> [n]");
        }
    }

    // ---- watch ----
    static std::atomic<bool> g_watch = false;
    using MgrAddFn = void* (*)(uintptr_t, void*, uint64_t, int);
    using Slot17Fn = void (*)(uintptr_t, uintptr_t, int);
    using Slot18Fn = uintptr_t (*)(uintptr_t, uint64_t, int);
    using MultiRemoveFn = void* (*)(uint8_t*, void*, uintptr_t, bool);
    static MgrAddFn g_add_orig = nullptr, g_remove_orig = nullptr;
    static Slot17Fn g_s17_orig = nullptr;
    static Slot18Fn g_s18_orig = nullptr;
    static MultiRemoveFn g_mrem_orig = nullptr;

    static uintptr_t Rva(void* ret) { return (uintptr_t)ret - scan::Game().base; }

    static void* AddDetour(uintptr_t mgr, void* res, uint64_t id, int n) {
        if (g_watch) LOG("invwatch: ADD %016llx x%d (from exe+%llx)", (unsigned long long)id, n, (unsigned long long)Rva(_ReturnAddress()));
        return g_add_orig(mgr, res, id, n);
    }
    static void* RemoveDetour(uintptr_t mgr, void* res, uint64_t id, int n) {
        if (g_watch) LOG("invwatch: REMOVE %016llx x%d (from exe+%llx)", (unsigned long long)id, n, (unsigned long long)Rva(_ReturnAddress()));
        return g_remove_orig(mgr, res, id, n);
    }
    static void S17Detour(uintptr_t mgr, uintptr_t item, int n) {
        uint64_t id = 0;
        if (g_watch && item) memory::Read(item + 0x30, id);
        if (g_watch) LOG("invwatch: SLOT17 item %016llx arg %d (from exe+%llx)", (unsigned long long)id, n, (unsigned long long)Rva(_ReturnAddress()));
        g_s17_orig(mgr, item, n);
    }
    static uintptr_t S18Detour(uintptr_t mgr, uint64_t id, int n) {
        uintptr_t r = g_s18_orig(mgr, id, n);
        static std::atomic<int> logged = 0;   // likely polled by UI every frame: cap the spam
        if (g_watch && logged++ < 150) LOG("invwatch: SLOT18 %016llx arg %d -> %llx (from exe+%llx)", (unsigned long long)id, n, (unsigned long long)r, (unsigned long long)Rva(_ReturnAddress()));
        return r;
    }
    static void* MultiRemoveDetour(uint8_t* b, void* res, uintptr_t owner, bool flag) {
        if (g_watch && b) {
            int items = 0;
            memory::Read((uintptr_t)b + 0x108, items);
            for (int i = 0; i < items && i < 16; i++) {
                uint64_t id = 0;
                int n = 0;
                memory::Read((uintptr_t)b + 0x48 + i * 8, id);
                memory::Read((uintptr_t)b + 0xC8 + i * 4, n);
                LOG("invwatch: MULTIREMOVE %016llx x%d (from exe+%llx)", (unsigned long long)id, n, (unsigned long long)Rva(_ReturnAddress()));
            }
        }
        return g_mrem_orig(b, res, owner, flag);
    }

    static bool Hook(uintptr_t rva, const uint8_t* expect, size_t len, void* detour, void** orig, const char* name) {
        uintptr_t t = scan::FromRva(rva);
        uint8_t cur[16] = {};
        if (!memory::SafeRead(t, cur, len) || memcmp(cur, expect, len) != 0) {
            LOG("invwatch: %s at exe+%llx doesn't match the expected bytes; not hooking", name, (unsigned long long)rva);
            return false;
        }
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return false;
        if (MH_CreateHook((void*)t, detour, orig) != MH_OK || MH_EnableHook((void*)t) != MH_OK) {
            LOG("invwatch: hooking %s failed", name);
            return false;
        }
        return true;
    }

    void Watch(bool on) {
        static bool installed = false;
        if (on && !installed) {
            installed = true;
            // Prologue bytes from the 4.0630 disassembly (2026-10-13), checked before hooking.
            static const uint8_t add[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };
            static const uint8_t rem[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10 };
            static const uint8_t s17[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x48, 0x8B, 0xDA };
            static const uint8_t s18[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x41, 0x8B, 0xF8 };
            static const uint8_t mrem[] = { 0x44, 0x88, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18 };
            int ok = Hook(0x1f5b720, add, sizeof(add), (void*)&AddDetour, (void**)&g_add_orig, "Add") +
                     Hook(0x1f5b9d0, rem, sizeof(rem), (void*)&RemoveDetour, (void**)&g_remove_orig, "Remove") +
                     Hook(0x1f5b8d0, s17, sizeof(s17), (void*)&S17Detour, (void**)&g_s17_orig, "Slot17") +
                     Hook(0x1f5b880, s18, sizeof(s18), (void*)&S18Detour, (void**)&g_s18_orig, "Slot18") +
                     Hook(0x1f60aa0, mrem, sizeof(mrem), (void*)&MultiRemoveDetour, (void**)&g_mrem_orig, "MultiRemove");
            LOG("invwatch: %d/5 hooks installed", ok);
        }
        g_watch = on;
        LOG("invwatch: %s", on ? "on" : "off");
    }

    // ---- equip ----
    static thread_local bool t_ours = false;   // set around our own equip calls so the lock lets them through
    static std::atomic<bool> g_lock = false, g_allow_suit = true;
    using EquipHookFn = void (*)(uintptr_t, uintptr_t, int, bool);
    static EquipHookFn g_equip_orig = nullptr;
    // exe+cbf380 MenuEquip(record, slot, flag): the pause menu's equip. Calls Equip (exe+cc5fc0) and then, for every
    // slot except the power (1), finds the hero's inventory and ACTIVATES the item (inv->vfunc[0x198/8](asset id) ->
    // exe+bc03e0(inv, item, 1)), which is what switches a mod's effect on (verified: Equip alone removed Ballistic
    // Inserts' effect but didn't add it back).
    using MenuEquipFn = void (*)(uintptr_t record, int slot, bool flag);
    static MenuEquipFn g_menu_equip_orig = nullptr;

    static void RefuseMessage(int slot) {
        static DWORD last_msg = 0;
        if (slot >= 0) LOG("lock: refused an equip into slot %d (loadout locked)", slot);
        else LOG("lock: refused an unequip (loadout locked)");
        if (GetTickCount() - last_msg > 3000) {
            last_msg = GetTickCount();
            hud::Show("Your loadout is locked during a Campaign run.", 3.0f);
        }
    }

    // exe+cca680 MenuUnequip(record, flag, force, ?): the menu's unequip button (called from exe+7b738f) and Equip's
    // removal of the previous item. Calls the core unequip exe+cc6700(mgr, record, flag) (zeroes the slot), then
    // deactivates the item on the hero's inventory. Refuses locked items ([*record+0x44]) unless force.
    using MenuUnequipFn = uintptr_t (*)(uintptr_t record, uint8_t flag, uint8_t force, uintptr_t r9);
    static MenuUnequipFn g_menu_unequip_orig = nullptr;

    static uintptr_t MenuUnequipDetour(uintptr_t record, uint8_t flag, uint8_t force, uintptr_t r9) {
        if (g_lock && !t_ours) { RefuseMessage(-1); return 0; }
        return g_menu_unequip_orig(record, flag, force, r9);
    }

    static void MenuEquipDetour(uintptr_t record, int slot, bool flag) {
        if (g_lock && !t_ours && !(slot == 0 && g_allow_suit)) { RefuseMessage(slot); return; }   // whole thing, activation included
        g_menu_equip_orig(record, slot, flag);
    }

    static void EquipDetour(uintptr_t mgr, uintptr_t record, int slot, bool both) {
        if (g_lock && !t_ours && !(slot == 0 && g_allow_suit)) { RefuseMessage(slot); return; }
        g_equip_orig(mgr, record, slot, both);
    }

    void Lock(bool on, bool allow_suit) {
        g_allow_suit = allow_suit;
        static bool installed = false;
        if (on && !installed) {
            static const uint8_t pro[] = { 0x4C, 0x8B, 0xDC, 0x53, 0x56, 0x57, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 };
            static const uint8_t menu[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x8B, 0xFA };
            installed = Hook(0xcc5fc0, pro, sizeof(pro), (void*)&EquipDetour, (void**)&g_equip_orig, "Equip") &&
                        Hook(0xcbf380, menu, sizeof(menu), (void*)&MenuEquipDetour, (void**)&g_menu_equip_orig, "MenuEquip");
            static const uint8_t unq[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x48, 0x8B, 0xD9 };
            installed = installed &&
                        Hook(0xcca680, unq, sizeof(unq), (void*)&MenuUnequipDetour, (void**)&g_menu_unequip_orig, "MenuUnequip");
            if (!installed) { LOG("lock: hook failed; not locking"); return; }
        }
        g_lock = on;
        LOG("lock: loadout %s", on ? "LOCKED" : "unlocked");
    }

    bool Locked() { return g_lock; }
    using EquipByIdFn = void (*)(uintptr_t mgr, uint64_t id);
    using LookupFn = uintptr_t* (*)(uintptr_t table, uint64_t key);

    static bool CallEquipById(uintptr_t fn, uintptr_t mgr, uint64_t id) {
        __try { ((EquipByIdFn)fn)(mgr, id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool CallLookup(uintptr_t fn, uintptr_t table, uint64_t id, uintptr_t& record) {
        __try {
            uintptr_t* p = ((LookupFn)fn)(table, id);
            record = p ? *p : 0;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool CallMenuEquip(uintptr_t fn, uintptr_t record, int slot) {
        __try { ((MenuEquipFn)fn)(record, slot, true); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // Equip/unequip reach into the hero's inventory; while the hero is dead or respawning that crashes (caught, but a
    // half-run function can leave the game's state inconsistent: 2026-10-13). Game thread only.
    static bool HeroReady() {
        auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
        uint32_t handle = 0;
        if (!get_actor || !memory::Read(scan::FromRva(natives::kHeroSystemRva) + 0x1C, handle) || !handle) return false;
        uintptr_t actor = 0;
        if (!memory::SafeCall(get_actor, &handle, actor) || !actor) return false;
        uintptr_t c = entity::FindComponent(actor, "HeroHealth");
        float hp = 0;
        return c && memory::Read(c + 0x88, hp) && hp > 0.0f;
    }

    bool Ready() { return HeroReady(); }

    bool Equip(uint64_t id, int slot) {
        if (!HeroReady()) { LOG("equip %016llx: hero not ready (dead or respawning); skipped", (unsigned long long)id); return false; }
        const uintptr_t mgr = scan::FromRva(0x70957c0);
        uint64_t before[5] = {};
        memory::SafeRead(mgr + 0xD068, before, sizeof(before));
        bool ok;
        t_ours = true;
        if (slot < 0) {
            ok = CallEquipById(scan::FromRva(0xcc6200), mgr, id);
        } else {
            uintptr_t record = 0;
            ok = CallLookup(scan::FromRva(0x1bcfaf0), mgr + 0x8808, id, record);
            if (!ok || !record) {
                t_ours = false;
                LOG("equip: %016llx not found in the owned items table%s", (unsigned long long)id, ok ? "" : " (lookup crashed, caught)");
                return false;
            }
            ok = CallMenuEquip(scan::FromRva(0xcbf380), record, slot);   // equip + activate, like the menu
        }
        t_ours = false;
        uint64_t after[5] = {};
        memory::SafeRead(mgr + 0xD068, after, sizeof(after));
        LOG("equip %016llx slot %d: %s", (unsigned long long)id, slot, ok ? "called" : "CRASHED (caught)");
        for (int i = 0; i < 5; i++)
            if (before[i] != after[i])
                LOG("  slot %d: %016llx -> %016llx", i, (unsigned long long)before[i], (unsigned long long)after[i]);
        return ok;
    }

    static bool CallMenuUnequip(uintptr_t fn, uintptr_t record) {
        __try { ((MenuUnequipFn)fn)(record, 0, 0, 0); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Unequip(uint64_t id) {
        if (!HeroReady()) { LOG("unequip %016llx: hero not ready (dead or respawning); skipped", (unsigned long long)id); return false; }
        const uintptr_t mgr = scan::FromRva(0x70957c0);
        uint64_t before[5] = {}, after[5] = {};
        memory::SafeRead(mgr + 0xD068, before, sizeof(before));
        uintptr_t record = 0;
        bool ok = CallLookup(scan::FromRva(0x1bcfaf0), mgr + 0x8808, id, record);
        if (!ok || !record) {
            LOG("unequip: %016llx not found in the owned items table", (unsigned long long)id);
            return false;
        }
        t_ours = true;
        ok = CallMenuUnequip(scan::FromRva(0xcca680), record);   // via the hook when the lock is installed
        t_ours = false;
        memory::SafeRead(mgr + 0xD068, after, sizeof(after));
        LOG("unequip %016llx: %s", (unsigned long long)id, ok ? "called" : "CRASHED (caught)");
        for (int i = 0; i < 5; i++)
            if (before[i] != after[i])
                LOG("  slot %d: %016llx -> %016llx", i, (unsigned long long)before[i], (unsigned long long)after[i]);
        return ok;
    }

    static std::string CStr(uintptr_t p, size_t max = 400) {
        char buf[404] = {};
        if (!p || max > 400 || !memory::SafeRead(p, buf, max)) return "";
        buf[max] = 0;
        return buf;
    }

    ItemText Text(uint64_t id) {
        auto out = std::make_shared<ItemText>();
        gamethread::Run([id, out] {
            try {
                uintptr_t record = 0, data = 0;
                const uintptr_t mgr = scan::FromRva(0x70957c0);
                if (!CallLookup(scan::FromRva(0x1bcfaf0), mgr + 0x8808, id, record) || !record) return;
                if (!memory::Read(record, data) || !data) return;
                using LocFn = const char* (*)(uint32_t hash, uint32_t zero);
                using FmtFn = int (*)(char* buf, size_t n, const char* fmt, ...);
                auto loc = (LocFn)scan::FromRva(0x1749970);
                auto fmt = (FmtFn)scan::FromRva(0x1bc1b10);
                uint32_t name_hash = 0, desc_hash = 0, arg = 0;
                memory::Read(data + 0x24, name_hash);
                memory::Read(data + 0x34, desc_hash);
                memory::Read(data + 0x38, arg);
                if (const char* n = loc(name_hash, 0)) out->name = CStr((uintptr_t)n);
                if (const char* d = loc(desc_hash, 0)) {
                    char buf[260] = {};
                    fmt(buf, 256, CStr((uintptr_t)d).c_str(), arg);
                    out->desc = buf;
                }
                uintptr_t p = 0;
                if (memory::Read(data + 0x48, p)) out->icon = CStr(p);
                if (memory::Read(data + 0xB8, p)) out->image = CStr(p);
                if (memory::Read(data + 0xA0, p)) out->video = CStr(p);
                out->ok = !out->name.empty();
            } catch (...) {
                out->ok = false;
            }
        }, 2000);
        return *out;
    }

    ItemInfo Info(uint64_t id) {
        ItemInfo info;
        uintptr_t record = 0, data = 0;
        const uintptr_t mgr = scan::FromRva(0x70957c0);
        if (!CallLookup(scan::FromRva(0x1bcfaf0), mgr + 0x8808, id, record) || !record) return info;
        info.found = true;
        uint8_t owned = 0, locked = 0;
        memory::Read(record + 0x34, owned);
        info.owned = owned != 0;
        if (memory::Read(record, data) && data) {
            memory::Read(data + 0x1D0, info.category);
            memory::Read(data + 0x44, locked);
            info.locked = locked != 0;
        }
        return info;
    }

    void Loadout(uint64_t out[5]) {
        for (int i = 0; i < 5; i++) out[i] = 0;
        memory::SafeRead(scan::FromRva(0x70957c0) + 0xD068, out, 5 * sizeof(uint64_t));
    }

    void ClearPowerSlot() {
        const uintptr_t mgr = scan::FromRva(0x70957c0);
        uint64_t zero = 0;
        memory::Write(mgr + 0xD068 + 8, zero);
        memory::Write(mgr + 0xD0A8 + 8, zero);
        LOG("loadout: suit power slot emptied");
    }
}
