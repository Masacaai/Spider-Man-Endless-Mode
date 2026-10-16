#include "spawn.h"

#include "entity.h"
#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "scan.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Mirrors ScriptHookSMPC's SpawnActor (disassembled from the 4.0630 community build; see recon/).
namespace spawn {
    using ConvertStringToHashFn = uint64_t* (*)(uint64_t* out, const char* path);
    using LoadAssetByHashFn = uintptr_t (*)(uintptr_t manager, uint64_t hash, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using SpawnRawFn = uintptr_t (*)(uint64_t hash, uintptr_t zero, void* transform);
    using CreateSyncObjectFn = uintptr_t (*)(void* desc);
    using EntityFn = uintptr_t (*)(uintptr_t entity);
    using SyncRegisterHandleFn = uintptr_t (*)(uintptr_t sync, uint32_t* handle);
    using SyncLookupFn = uintptr_t (*)(int64_t a, uint8_t b);
    using EntityAttachFn = uintptr_t (*)(uintptr_t entity, uintptr_t x);
    using SyncBoolFn = uintptr_t (*)(uintptr_t sync, uint32_t flag);
    using SyncFlag2Fn = uintptr_t (*)(int64_t id);

    template <typename T>
    static T Fn(natives::Id id) { return (T)natives::Get(id); }

    static bool AllResolved() {
        const natives::Id needed[] = {
            natives::ConvertStringToHash, natives::LoadAssetByHash, natives::ActorAssetManager, natives::SpawnRaw, natives::CreateSyncObject,
            natives::SyncSlotTable, natives::EntityInit, natives::SyncRegisterHandle, natives::EntityIsSetUp,
            natives::SyncLookup, natives::EntityAttach, natives::SyncActivate, natives::SyncFlag2,
            natives::SyncFinish1, natives::SyncFinish0,
        };
        for (auto id : needed) {
            if (!natives::Get(id)) {
                LOG("spawn: native %s not resolved", natives::table[id].name);
                return false;
            }
        }
        return true;
    }

    // Runs fn on the game thread and waits for it (timeout in ms). Returns false on timeout.
    template <typename F>
    static bool RunOnGameThread(F fn, DWORD timeout_ms) {
        auto done = std::make_shared<std::atomic<bool>>(false);
        gamethread::Post([fn, done] { fn(); *done = true; });
        for (DWORD waited = 0; !*done; waited += 5) {
            if (waited >= timeout_ms) return false;
            Sleep(5);
        }
        return true;
    }

    // 3x4 transform (row-major, 16-byte rows) exactly as Script Hook builds it:
    // rotation about +Y from heading, scale 1, position in the last row.
    static void BuildTransform(float out[16], Vec3 pos, float heading_deg) {
        memset(out, 0, 16 * sizeof(float));
        double t = (heading_deg / 180.0f) * 3.14159265358979;
        float s = (float)sin(t), c = (float)cos(t);
        out[0] = s;   out[2] = -c;
        out[5] = 1.0f;
        out[8] = c;   out[10] = s;
        out[12] = pos.x; out[13] = pos.y; out[14] = pos.z; out[15] = 1.0f;
    }

    static int FindFreeSyncSlot() {
        uintptr_t table = 0;
        if (!memory::Read(natives::Get(natives::SyncSlotTable), table) || !table) return -1;
        for (int i = 0; i < 0x2EE0; i++) {
            uintptr_t slot = 0;
            if (!memory::Read(table + i * 8, slot)) return -1;
            if (slot == 0) return i;
        }
        return -1;
    }

    // Actors each roster actor creates at runtime: weapon items, jetpacks (a weapon item too), heads, effect volumes.
    // Unloaded, they show up as white cubes (Hammerhead's rifles, the Sable/Olympus jetpacks, 2026-10-13).
    struct Deps { const char* actor; std::vector<const char*> extra; };
    static const Deps kDeps[] = {
#include "preload_deps.inc"
    };

    static std::string Lower(std::string s) {
        for (char& c : s) c = (char)tolower((unsigned char)c);
        return s;
    }

    // Our references on game assets, counted per path: an actor and its dependencies are held while any preloaded
    // actor needs them, and released (one AssetRelease per LoadAssetByHash) when the last one lets go. Holding
    // everything forever (1000+ assets after a long Horde run) left the respawn load stuck and bodies without
    // models (2026-10-14).
    struct Pending { DWORD requested = 0; std::shared_ptr<std::atomic<uintptr_t>> asset; int users = 0; };
    static std::mutex g_preload_mutex;
    static std::unordered_map<std::string, Pending> g_preloaded;   // path -> request time, asset, users
    static std::unordered_map<std::string, bool> g_held;           // actors preloaded via Preload()

    static const Deps* FindDeps(const std::string& path) {
        std::string key = Lower(path);
        for (const auto& d : kDeps) if (key == d.actor) return &d;
        return nullptr;
    }

    // g_preload_mutex held.
    static void Acquire(const std::string& path) {
        Pending& p = g_preloaded[path];
        if (p.users++ > 0) return;
        p.requested = GetTickCount();
        p.asset = std::make_shared<std::atomic<uintptr_t>>(0);
        auto asset = p.asset;
        gamethread::Post([path, asset] {
            uint64_t h = 0;
            Fn<ConvertStringToHashFn>(natives::ConvertStringToHash)(&h, path.c_str());
            *asset = Fn<LoadAssetByHashFn>(natives::LoadAssetByHash)(natives::Get(natives::ActorAssetManager), h, 0, 0, 0, 0, 0);
        });
    }

    // g_preload_mutex held. The release job runs after the load job (the game-thread queue is FIFO).
    static void Drop(const std::string& path) {
        auto it = g_preloaded.find(path);
        if (it == g_preloaded.end() || --it->second.users > 0) return;
        auto asset = it->second.asset;
        g_preloaded.erase(it);
        using ReleaseFn = bool (*)(uintptr_t manager, uintptr_t asset);
        auto release = (ReleaseFn)natives::Get(natives::AssetRelease);
        if (!release) return;   // can't release: the reference just stays (the old behaviour)
        gamethread::Post([release, asset] {
            if (uintptr_t a = *asset) release(natives::Get(natives::ActorAssetManager), a);
        });
    }

    void Preload(const std::string& path) {
        if (path.empty() || !gamethread::Installed() || !AllResolved()) return;
        size_t n = 0;
        {
            std::lock_guard<std::mutex> lock(g_preload_mutex);
            if (g_held.count(path)) return;
            g_held[path] = true;
            Acquire(path);
            if (auto* d = FindDeps(path)) for (const char* x : d->extra) { Acquire(x); n++; }
        }
        LOG("preload: %s (+%zu dependencies)", path.c_str(), n);
    }

    void Release(const std::string& path) {
        std::lock_guard<std::mutex> lock(g_preload_mutex);
        if (!g_held.erase(path)) return;
        Drop(path);
        if (auto* d = FindDeps(path)) for (const char* x : d->extra) Drop(x);
    }

    void ReleaseAll() {
        size_t actors = 0, assets = 0;
        {
            std::lock_guard<std::mutex> lock(g_preload_mutex);
            actors = g_held.size();
            assets = g_preloaded.size();
            std::vector<std::string> held;
            for (auto& [p, _] : g_held) held.push_back(p);
            g_held.clear();
            for (auto& p : held) {
                Drop(p);
                if (auto* d = FindDeps(p)) for (const char* x : d->extra) Drop(x);
            }
        }
        LOG("preload: released %zu actors (%zu assets)", actors, assets);
    }

    void LogPreloads() {
        std::lock_guard<std::mutex> lock(g_preload_mutex);
        size_t loaded = 0;
        for (auto& [path, _] : g_preloaded) {
            auto it = g_preloaded.find(path);
            uint8_t st = 0;
            uintptr_t a = *it->second.asset;
            if (a && memory::Read(a + 1, st) && st == 1) loaded++;
        }
        LOG("preloads: %zu actors held, %zu assets referenced (%zu loaded)", g_held.size(), g_preloaded.size(), loaded);
    }

    // Loaded = byte [asset+1] == 1 (Script Hook's WaitForAssetToLoad).
    static bool Loaded(const std::string& path) {
        auto it = g_preloaded.find(path);
        if (it == g_preloaded.end()) return false;
        uint8_t state = 0;
        uintptr_t a = *it->second.asset;
        return a && memory::Read(a + 1, state) && state == 1;
    }

    bool Ready(const std::string& path, unsigned min_ms) {
        {
            std::lock_guard<std::mutex> lock(g_preload_mutex);
            if (g_held.count(path)) {
                DWORD age = GetTickCount() - g_preloaded[path].requested;
                if (age < min_ms) return false;
                // Everything the actor spawns must be in memory too. A dependency that never reports loaded (one
                // the game streams some other way) must not hold the unit back forever: after 20 s, go anyway.
                if (age >= 20000) {
                    static std::unordered_map<std::string, bool> warned;
                    bool ok = Loaded(path);
                    std::string missing;
                    if (auto* d = FindDeps(path)) for (const char* x : d->extra) if (!Loaded(x)) { missing = x; ok = false; break; }
                    if (!ok && !warned[path]) {
                        warned[path] = true;
                        LOG("preload: %s not fully loaded after 20 s (%s); spawning anyway", path.c_str(),
                            missing.empty() ? "the actor itself" : missing.c_str());
                    }
                    return true;
                }
                if (!Loaded(path)) return false;
                if (auto* d = FindDeps(path)) for (const char* x : d->extra) if (!Loaded(x)) return false;
                return true;
            }
        }
        Preload(path);
        return false;
    }

    uintptr_t FindSyncFor(uintptr_t entity) {
        uintptr_t table = 0;
        auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
        if (!get_actor || !memory::Read(natives::Get(natives::SyncSlotTable), table) || !table) return 0;
        for (int i = 0; i < 0x2EE0; i++) {
            uintptr_t sync = 0, actor = 0;
            if (!memory::Read(table + i * 8, sync)) return 0;
            // A sync object starts with the actor handle, so GetActor works on it directly (as in Script Hook).
            if (sync && memory::SafeCall(get_actor, (void*)sync, actor) && actor == entity) return sync;
        }
        return 0;
    }

    bool DespawnActor(uintptr_t entity) {
        if (!gamethread::Installed()) { LOG("despawn: game-thread hook not installed"); return false; }
        if (!natives::Get(natives::DeleteSyncObject) || !natives::Get(natives::EntityDestroy)) {
            LOG("despawn: natives not resolved");
            return false;
        }
        uintptr_t sync = FindSyncFor(entity);
        LOG("despawn: entity=%llx sync=%llx", (unsigned long long)entity, (unsigned long long)sync);
        if (!sync) return false;
        bool ok = RunOnGameThread([entity, sync] {
            Fn<EntityFn>(natives::DeleteSyncObject)(sync);
            LOG("despawn: sync deleted");
            Fn<EntityFn>(natives::EntityDestroy)(entity);
            LOG("despawn: entity destroyed");
        }, 5000);
        return ok;
    }

    uintptr_t SpawnActor(const std::string& path, Vec3 pos, float heading_deg, bool engaged) {
        if (!gamethread::Installed()) { LOG("spawn: game-thread hook not installed"); return 0; }
        if (!AllResolved()) return 0;

        // Shared with game-thread jobs so a late job never touches a dead stack frame.
        struct State { std::string path; uint64_t hash = 0; uintptr_t asset = 0; uintptr_t entity = 0; };
        auto st = std::make_shared<State>();
        st->path = path;

        // 1. Hash the path and load the asset (game thread). Same sequence as jedijosh920's Mod Menu:
        //    ConvertStringToHash -> LoadAssetByHash(GetActorAssetManager(), hash) -> WaitForAssetToLoad -> SpawnActor(hash).
        uintptr_t& asset = st->asset;
        LOG("spawn: [1] load %s", path.c_str());
        bool ok = RunOnGameThread([st] {
            uint64_t out = 0;
            if (st->path.size() == 17 && st->path[0] == '#')   // "#<16 hex>": a raw asset id (no path known)
                out = strtoull(st->path.c_str() + 1, nullptr, 16);
            else
                Fn<ConvertStringToHashFn>(natives::ConvertStringToHash)(&out, st->path.c_str());
            st->hash = out;
            st->asset = Fn<LoadAssetByHashFn>(natives::LoadAssetByHash)(
                natives::Get(natives::ActorAssetManager), st->hash, 0, 0, 0, 0, 0);
        }, 5000);
        if (!ok || !asset) {
            LOG("spawn: [1] failed (timeout=%d hash=%016llx asset=%llx)", !ok, (unsigned long long)st->hash, (unsigned long long)asset);
            return 0;
        }
        LOG("spawn: [1] hash=%016llx asset=%llx", (unsigned long long)st->hash, (unsigned long long)asset);

        // 2. Wait until loaded: byte [asset+1] == 1 (Script Hook's WaitForAssetToLoad).
        for (int waited = 0;; waited += 10) {
            uint8_t state = 0;
            if (!memory::Read(asset + 1, state)) { LOG("spawn: [2] asset unreadable"); return 0; }
            if (state == 1) break;
            if (waited >= 15000) { LOG("spawn: [2] asset not loaded after 15s (state=%u)", state); return 0; }
            Sleep(10);
        }
        LOG("spawn: [2] asset loaded");

        // 3. Spawn + sync setup (game thread), step by step.
        ok = RunOnGameThread([st, pos, heading_deg, engaged] {
            uintptr_t& entity = st->entity;
            alignas(16) float transform[16];
            BuildTransform(transform, pos, heading_deg);

            LOG("spawn: [3] SpawnRaw");
            entity = Fn<SpawnRawFn>(natives::SpawnRaw)(st->hash, 0, transform);
            LOG("spawn: [3] entity=%llx", (unsigned long long)entity);
            if (!entity) return;

            if (engaged) {
                int written = 0;
                for (const auto& h : entity::FindBotData(entity)) {
                    // Only the Bot-derived component's own copy (Bot+0x160); the scan can overrun small components
                    // into neighbouring objects.
                    if (!h.exact || h.depth != 0 || h.inline_off != 0x160) continue;
                    uint8_t was = 0;
                    memory::Read(h.addr + 8, was);
                    memory::Write<uint8_t>(h.addr + 8, 1);
                    LOG("spawn: [3] engaged: %s BotData @%llx (depth %d) SpawnEngagedWithHero %u -> 1", h.component.c_str(),
                        (unsigned long long)h.addr, h.depth, was);
                    written++;
                }
                if (!written) LOG("spawn: [3] WARNING engaged requested but no Bot+0x160 BotData found (%s)", st->path.c_str());
            }

            // Story bosses (and some mission actors) come out of SpawnRaw with 0 current health: their mission script
            // normally sets it. Standalone, the game would kill them on the spot, so fill them up before activation.
            if (uintptr_t hc = entity::HealthComponent(entity)) {
                float cur = 0, mx = 0;
                if (memory::Read(hc + 0x88, cur) && memory::Read(hc + 0x80, mx) && cur <= 0.0f && mx > 0.0f && mx < 1.0e6f) {
                    memory::Write<float>(hc + 0x88, mx);
                    LOG("spawn: [3] health was 0/%.0f, filled to max", mx);
                }
            }

            // Diagnosis (docs/BUGS.md #2, #6): does the bot have its MoverStandard yet?
            if (entity::FindComponent(entity, "BotHealth"))
                LOG("spawn: [3] MoverStandard %s at spawn (%s)", entity::FindComponent(entity, "MoverStandard") ? "present" : "MISSING",
                    engaged ? "engaged" : "calm");

            int slot = FindFreeSyncSlot();
            LOG("spawn: [4] CreateSyncObject (slot %d)", slot);
            alignas(8) uint8_t desc[16] = {};
            uint16_t slot16 = (uint16_t)slot;
            memcpy(desc + 4, &slot16, 2);
            uintptr_t sync = Fn<CreateSyncObjectFn>(natives::CreateSyncObject)(desc);
            LOG("spawn: [4] sync=%llx", (unsigned long long)sync);
            if (!sync) return;

            uint16_t hi = *(uint16_t*)(entity + 0x10);
            uint32_t lo = *(uint32_t*)(entity + 0xC);
            uint32_t handle = ((uint32_t)(hi & 0x7FF) << 20) | (lo & 0xFFFFF);
            uint8_t flags = *(uint8_t*)(entity + 8);
            LOG("spawn: [5] handle=%08x flags=%02x", handle, flags);

            if (!(flags & 4)) Fn<EntityFn>(natives::EntityInit)(entity);
            Fn<SyncRegisterHandleFn>(natives::SyncRegisterHandle)(sync, &handle);
            LOG("spawn: [6] registered");

            if (Fn<EntityFn>(natives::EntityIsSetUp)(entity) == 0) {
                uintptr_t x = Fn<SyncLookupFn>(natives::SyncLookup)(*(int16_t*)(sync + 0x114), *(uint8_t*)(sync + 0x11D));
                Fn<EntityAttachFn>(natives::EntityAttach)(entity, x);
                LOG("spawn: [7] attached");
            }

            flags = *(uint8_t*)(entity + 8);
            if (!(flags & 4) && (flags & 8)) Fn<SyncBoolFn>(natives::SyncActivate)(sync, 1);
            if (*(uint8_t*)(sync + 0x11F) & 2) Fn<SyncFlag2Fn>(natives::SyncFlag2)(*(int16_t*)(sync + 0x114));
            Fn<SyncBoolFn>(natives::SyncFinish1)(sync, 1);
            Fn<SyncBoolFn>(natives::SyncFinish0)(sync, 0);
            LOG("spawn: [8] done");
        }, 5000);
        if (!ok) { LOG("spawn: [3] game thread did not run the job within 5s (it may still run later)"); return 0; }
        return st->entity;
    }
}
