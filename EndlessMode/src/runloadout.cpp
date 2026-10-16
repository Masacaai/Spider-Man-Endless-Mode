#include "runloadout.h"

#include "gamethread.h"
#include "inventory.h"
#include "log.h"
#include "paths.h"

#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace runloadout {
    static std::atomic<bool> g_active = false;

    static std::string File() { return DataDir() + "\\EndlessMode_loadout.txt"; }

    static bool ReadSnapshot(uint64_t out[5]) {
        std::ifstream f(File());
        if (!f) return false;
        for (int i = 0; i < 5; i++) {
            std::string hex;
            if (!(f >> hex)) return false;
            out[i] = strtoull(hex.c_str(), nullptr, 16);
        }
        return true;
    }

    static bool WriteSnapshot(const uint64_t v[5]) {
        std::ofstream f(File(), std::ios::trunc);
        if (!f) return false;
        char buf[24];
        for (int i = 0; i < 5; i++) {
            snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)v[i]);
            f << buf << "\n";
        }
        f << "# EndlessArena: your suit power and mods before the current run (restored at run end).\n";
        return (bool)f;
    }

    static void Cur(uint64_t out[5]) { inventory::Loadout(out); }

    // Puts slots 1..4 back to `want` (unequip what differs, then equip the saved item into its slot).
    static void RestoreSlots(const uint64_t want[5]) {
        uint64_t cur[5];
        Cur(cur);
        for (int s = 1; s <= 4; s++) {
            if (cur[s] == want[s]) continue;
            // Powers: equipping replaces the old one; the game's unequip can't empty that slot, so write it.
            if (cur[s] && s > 1) gamethread::Run([id = cur[s]] { inventory::Unequip(id); });
            if (want[s]) gamethread::Run([id = want[s], s] { inventory::Equip(id, s); });
            else if (s == 1) gamethread::Run([] { inventory::ClearPowerSlot(); });
        }
        Cur(cur);
        bool ok = true;
        for (int s = 1; s <= 4; s++) ok &= cur[s] == want[s];
        LOG("runloadout: restore %s", ok ? "complete" : "INCOMPLETE (check the loadout in the menu)");
    }

    // Waits (worker thread) until the hero has been alive for `steady_ms`, up to `timeout_ms`. Equips while the hero
    // is dead or respawning crash inside the game (2026-10-13: power restore after death failed that way).
    static bool WaitForHero(unsigned steady_ms, unsigned timeout_ms) {
        DWORD start = GetTickCount(), alive_since = 0;
        while (GetTickCount() - start < timeout_ms) {
            auto ready = std::make_shared<bool>(false);
            gamethread::Run([ready] { *ready = inventory::Ready(); }, 1000);
            DWORD now = GetTickCount();
            if (!*ready) alive_since = 0;
            else if (!alive_since) alive_since = now;
            else if (now - alive_since >= steady_ms) return true;
            Sleep(250);
        }
        return false;
    }

    static std::atomic<bool> g_restoring = false;

    static void FinishEnd() {
        uint64_t want[5];
        if (!WaitForHero(2000, 180000)) {
            LOG("runloadout: hero never came back; the snapshot stays for the next launch");
        } else if (ReadSnapshot(want)) {
            RestoreSlots(want);
            DeleteFileA(File().c_str());
        } else {
            LOG("runloadout: no snapshot to restore");
        }
        inventory::Lock(false);
        g_restoring = false;
    }

    bool Begin() {
        for (int i = 0; i < 400 && g_restoring; i++) Sleep(50);   // a previous run's restore still pending
        if (!WaitForHero(0, 10000)) { LOG("runloadout: hero not ready; loadout untouched"); return false; }
        uint64_t cur[5];
        Cur(cur);
        uint64_t prev[5];
        if (ReadSnapshot(prev)) {
            // A snapshot from a run that never ended (crash): keep it, it's the real loadout.
            LOG("runloadout: keeping the existing snapshot (previous run didn't end cleanly)");
        } else if (!WriteSnapshot(cur)) {
            LOG("runloadout: couldn't write the snapshot; not touching the loadout");
            return false;
        }
        inventory::Lock(true, true);
        for (int s = 1; s <= 4; s++)
            if (cur[s]) gamethread::Run([id = cur[s]] { inventory::Unequip(id); });
        // The game's unequip won't empty the power slot (verified 2026-10-13), but the power is read from the slot
        // when it's triggered, so writing 0 there empties it.
        uint64_t after[5];
        Cur(after);
        if (after[1]) gamethread::Run([] { inventory::ClearPowerSlot(); });
        g_active = true;
        LOG("runloadout: run loadout started (power and mods cleared, menu locked)");
        return true;
    }

    void End() {
        if (!g_active) return;
        g_active = false;
        // Restore once the hero is up (after a death that's after the respawn); the lock stays on until then.
        g_restoring = true;
        std::thread(FinishEnd).detach();
    }

    void Recover() {
        if (g_active || g_restoring) return;
        uint64_t want[5];
        if (!ReadSnapshot(want)) return;
        if (!WaitForHero(2000, 60000)) return;
        LOG("runloadout: found a snapshot from an unfinished run; restoring your loadout");
        RestoreSlots(want);
        DeleteFileA(File().c_str());
    }

    bool Active() { return g_active; }

    bool SetPower(uint64_t id) {
        auto ok = std::make_shared<bool>(false);   // shared: a timed-out job may still run later
        gamethread::Run([id, ok] { *ok = inventory::Equip(id, 1); });
        return *ok;
    }

    int FreeModSlot() {
        uint64_t cur[5];
        Cur(cur);
        for (int s = 2; s <= 4; s++)
            if (!cur[s]) return s;
        return -1;
    }

    bool AddMod(uint64_t id) {
        int s = FreeModSlot();
        if (s < 0) return false;
        auto ok = std::make_shared<bool>(false);
        gamethread::Run([id, s, ok] { *ok = inventory::Equip(id, s); });
        return *ok;
    }

    bool HasItem(uint64_t id) {
        uint64_t cur[5];
        Cur(cur);
        for (int s = 1; s <= 4; s++)
            if (cur[s] == id) return true;
        return false;
    }
}
