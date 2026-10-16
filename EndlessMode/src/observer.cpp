#include "observer.h"

#include "log.h"
#include "memory.h"
#include "natives.h"
#include "scan.h"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace observer {
    using SpawnRawFn = uintptr_t (*)(uint64_t, uintptr_t, void*, uintptr_t);
    using LoadAssetByHashFn = uintptr_t (*)(uintptr_t, uint64_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using CreateSyncObjectFn = uintptr_t (*)(void*, uintptr_t, uintptr_t, uintptr_t);

    static SpawnRawFn g_spawn_orig = nullptr;
    static LoadAssetByHashFn g_load_orig = nullptr;
    static CreateSyncObjectFn g_sync_orig = nullptr;
    static std::atomic<bool> g_installed = false;
    static std::atomic<bool> g_on = false;
    static std::mutex g_seen_mutex;
    static std::unordered_set<uint64_t> g_seen_loads;   // log each (manager, hash) load once

    static unsigned long long CallerRva(void* ret) {
        return (unsigned long long)scan::Rva((uintptr_t)ret);
    }

    // Backtrace as exe RVAs. Each distinct chain is logged in full once as "bt#N: ..."; returns N.
    static std::mutex g_bt_mutex;
    static std::vector<std::string> g_bt_seen;

    static int Backtrace() {
        void* frames[20] = {};
        USHORT n = RtlCaptureStackBackTrace(1, 20, frames, nullptr);
        std::string s;
        const uintptr_t base = scan::Game().base;
        for (USHORT i = 0; i < n; i++) {
            uintptr_t f = (uintptr_t)frames[i];
            char part[24];
            if (f >= base && f < base + 0x8000000) snprintf(part, sizeof(part), " %llx", (unsigned long long)(f - base));
            else snprintf(part, sizeof(part), " ?");
            s += part;
        }
        std::lock_guard<std::mutex> lock(g_bt_mutex);
        for (size_t i = 0; i < g_bt_seen.size(); i++) if (g_bt_seen[i] == s) return (int)i;
        g_bt_seen.push_back(s);
        LOG("bt#%zu:%s", g_bt_seen.size() - 1, s.c_str());
        return (int)g_bt_seen.size() - 1;
    }

    static uintptr_t SpawnDetour(uint64_t hash, uintptr_t b, void* transform, uintptr_t d) {
        uintptr_t e = g_spawn_orig(hash, b, transform, d);
        if (g_on) {
            float pos[3] = {};
            if (transform) memory::SafeRead((uintptr_t)transform + 0x30, pos, sizeof(pos));
            int bt = Backtrace();
            LOG("obs spawn: hash=%016llx pos=(%.1f, %.1f, %.1f) entity=%llx caller=exe+%llx bt#%d", (unsigned long long)hash,
                pos[0], pos[1], pos[2], (unsigned long long)e, CallerRva(_ReturnAddress()), bt);
        }
        return e;
    }

    static uintptr_t LoadDetour(uintptr_t mgr, uint64_t hash, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t f) {
        uintptr_t r = g_load_orig(mgr, hash, a, b, c, d, f);
        if (g_on) {
            const char* kind = mgr == natives::Get(natives::ActorAssetManager) ? "actor"
                             : mgr == natives::Get(natives::ZoneAssetManager) ? "zone" : nullptr;
            if (kind) {
                bool first;
                {
                    std::lock_guard<std::mutex> lock(g_seen_mutex);
                    first = g_seen_loads.insert(hash ^ (uint64_t)mgr).second;
                }
                if (first) LOG("obs load %s: hash=%016llx asset=%llx caller=exe+%llx", kind, (unsigned long long)hash,
                               (unsigned long long)r, CallerRva(_ReturnAddress()));
            }
        }
        return r;
    }

    static uintptr_t SyncDetour(void* desc, uintptr_t b, uintptr_t c, uintptr_t d) {
        uintptr_t s = g_sync_orig(desc, b, c, d);
        if (g_on) {
            int bt = Backtrace();
            // The sync's handle resolves to its entity; log the entity so it can be inspected (comps) later.
            uintptr_t entity = 0;
            auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
            if (s && get_actor) memory::SafeCall(get_actor, (void*)s, entity);
            LOG("obs sync: sync=%llx entity=%llx caller=exe+%llx bt#%d", (unsigned long long)s, (unsigned long long)entity,
                CallerRva(_ReturnAddress()), bt);
        }
        return s;
    }

    using HealthSetFn = void (*)(void* self, float value);
    static HealthSetFn g_health_orig = nullptr;
    static std::atomic<bool> g_health_installed = false;
    static std::atomic<bool> g_health_on = false;

    static void HealthDetour(void* self, float value) {
        float before = 0.0f;
        bool watch = g_health_on && memory::Read((uintptr_t)self + 0x88, before) && value < before;
        if (watch) {
            void* frames[16] = {};
            USHORT n = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
            char line[512];
            int len = 0;
            for (USHORT i = 0; i < n && len < 480; i++) {
                uintptr_t f = (uintptr_t)frames[i];
                uintptr_t base = scan::Game().base;
                if (f >= base && f < base + 0x8000000) len += snprintf(line + len, sizeof(line) - len, " %llx", (unsigned long long)(f - base));
                else len += snprintf(line + len, sizeof(line) - len, " ?");
            }
            line[len] = 0;
            uintptr_t entity = 0;
            memory::Read((uintptr_t)self + 8, entity);
            LOG("dmg: comp=%llx entity=%llx %.1f -> %.1f bt:%s", (unsigned long long)self, (unsigned long long)entity,
                before, value, line);
        }
        g_health_orig(self, value);
    }

    // Health::OnDamage(health*, msg*): dump the damage message so real hits and our nudges can be diffed.
    using OnDamageFn = bool (*)(void* self, void* msg);
    static OnDamageFn g_ondamage_orig = nullptr;

    static std::mutex g_tmpl_mutex;
    static uint8_t g_tmpl[0x1C0];
    static bool g_tmpl_valid = false;
    static uint32_t g_hero_handle_for_tmpl = 0;

    static bool OnDamageDetour(void* self, void* msg) {
        // Learn a template from a real hero -> bot hit (attacker at +0x138 is the hero, target at +0x1A8 isn't).
        uint32_t attacker = 0, target = 0;
        if (msg && g_hero_handle_for_tmpl && memory::Read((uintptr_t)msg + 0x138, attacker) &&
            memory::Read((uintptr_t)msg + 0x1A8, target) && attacker == g_hero_handle_for_tmpl && target != attacker) {
            std::lock_guard<std::mutex> lock(g_tmpl_mutex);
            if (memory::SafeRead((uintptr_t)msg, g_tmpl, sizeof(g_tmpl))) {
                if (!g_tmpl_valid) LOG("dmgmsg: learned hit template from a real hero hit");
                g_tmpl_valid = true;
            }
        }
        if (g_health_on && msg) {
            uint8_t buf[0x1C0];
            if (memory::SafeRead((uintptr_t)msg, buf, sizeof(buf))) {
                LOG("dmgmsg: health=%llx msg=%llx", (unsigned long long)self, (unsigned long long)msg);
                for (size_t o = 0; o < sizeof(buf); o += 32) {
                    char line[160];
                    int n = snprintf(line, sizeof(line), "  dmgmsg +%03zx:", o);
                    for (size_t k = 0; k < 32; k += 4) {
                        uint32_t v; memcpy(&v, buf + o + k, 4);
                        n += snprintf(line + n, sizeof(line) - n, " %08x", v);
                    }
                    LOG("%s", line);
                }
            }
        }
        return g_ondamage_orig(self, msg);
    }

    void SetHeroHandle(uint32_t handle) { g_hero_handle_for_tmpl = handle; }

    bool CopyHitTemplate(uint8_t out[0x1C0]) {
        std::lock_guard<std::mutex> lock(g_tmpl_mutex);
        if (!g_tmpl_valid) return false;
        memcpy(out, g_tmpl, sizeof(g_tmpl));
        return true;
    }

    bool WatchDamage(bool on) {
        if (on && !g_health_installed) {
            MH_STATUS s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { LOG("dmgwatch: MH_Initialize failed"); return false; }
            void* target = (void*)natives::Get(natives::HealthSet);
            if (!target) { LOG("dmgwatch: HealthSet not resolved"); return false; }
            if (MH_CreateHook(target, (void*)&HealthDetour, (void**)&g_health_orig) != MH_OK || MH_EnableHook(target) != MH_OK) {
                LOG("dmgwatch: hook failed");
                return false;
            }
            void* ondmg = (void*)natives::Get(natives::HealthOnDamage);
            if (ondmg && (MH_CreateHook(ondmg, (void*)&OnDamageDetour, (void**)&g_ondamage_orig) != MH_OK ||
                          MH_EnableHook(ondmg) != MH_OK)) {
                LOG("dmgwatch: OnDamage hook failed (health hook still active)");
            }
            g_health_installed = true;
        }
        g_health_on = on;
        LOG("dmgwatch: %s", on ? "on" : "off");
        return true;
    }

    static bool Hook(natives::Id id, void* detour, void** orig) {
        void* target = (void*)natives::Get(id);
        if (!target) { LOG("observe: %s not resolved", natives::table[id].name); return false; }
        if (MH_CreateHook(target, detour, orig) != MH_OK || MH_EnableHook(target) != MH_OK) {
            LOG("observe: hook on %s failed", natives::table[id].name);
            return false;
        }
        return true;
    }

    bool Enable(bool on) {
        if (on && !g_installed) {
            MH_STATUS s = MH_Initialize();
            if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { LOG("observe: MH_Initialize failed"); return false; }
            bool ok = Hook(natives::SpawnRaw, (void*)&SpawnDetour, (void**)&g_spawn_orig)
                   && Hook(natives::LoadAssetByHash, (void*)&LoadDetour, (void**)&g_load_orig)
                   && Hook(natives::CreateSyncObject, (void*)&SyncDetour, (void**)&g_sync_orig);
            if (!ok) return false;
            g_installed = true;
        }
        g_on = on;
        LOG("observe: %s", on ? "on" : "off (hooks stay installed, logging stopped)");
        return true;
    }
}
