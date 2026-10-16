#include "arena.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "scan.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <memory>
#include <vector>

namespace arena {
    using ConvertStringToHashFn = uint64_t* (*)(uint64_t* out, const char* path);
    using LoadAssetByHashFn = uintptr_t (*)(uintptr_t manager, uint64_t hash, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    using GetActorFn = uintptr_t (*)(void* handle);

    // Zone paths from levels/i20_city/i20_city.level (extracted offline with tools/toctool; all present in toc).
    static const char* const kRaftZones[] = {
        "levels/i20_city/instance/Prison_Interior/ContainerNavData.zone",
        "levels/I20_City/instance/Prison_Interior/Prison_Interior/Raft_Interior_Art_RP.zone",
        "levels/i20_city/instance/Prison_Interior/Prison_Interior/Raft_INT_lgt.zone",
        "levels/i20_city/instance/Prison_Interior/Prison_Interior/Raft_INT_light_grid.zone",
        "levels/i20_city/instance/Prison_Interior/Prison_Interior/Raft_Interior_FX.zone",
        "levels/I20_City/instance/Prison_Interior/Prison_Interior/Raft_EXT_atmosphere.zone",
        "levels/i20_city/instance/Prison_Interior/Prison_Interior/Raft_Shared_Airlock.zone",
        "levels/I20_City/instance/Prison_Interior/Prison_Interior/Raft_Int_Tower.zone",
    };
    static const char* const kRaftGameplayZones[] = {
        "levels/i20_city/instance/Prison_Interior/Prison_Interior/Raft_Int_Gameplay.zone",
        "levels/i20_city/instance/Prison_Interior/GP_A2_SHADOW_OVERLAY.zone",
    };
    // Cell-block floor: actor records in Raft_Int_Gameplay.zone cluster at x 1350..1395, z -1175..-1243, y 39 / 45.
    constexpr float kRaftSpawn[3] = { 1367.4f, 40.5f, -1210.0f };

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

    bool LoadZone(const std::string& path) {
        if (!gamethread::Installed()) { LOG("zone: run 'hook' first"); return false; }
        auto hash_fn = (ConvertStringToHashFn)natives::Get(natives::ConvertStringToHash);
        auto load = (LoadAssetByHashFn)natives::Get(natives::LoadAssetByHash);
        uintptr_t mgr = natives::Get(natives::ZoneAssetManager);
        if (!hash_fn || !load || !mgr) { LOG("zone: natives not resolved"); return false; }

        struct State { std::string path; uint64_t hash = 0; uintptr_t asset = 0; };
        auto st = std::make_shared<State>();
        st->path = path;
        bool ok = RunOnGameThread([st, hash_fn, load, mgr] {
            uint64_t out = 0;
            hash_fn(&out, st->path.c_str());
            st->hash = out;
            st->asset = load(mgr, st->hash, 0, 0, 0, 0, 0);
        }, 5000);
        if (!ok || !st->asset) {
            LOG("zone: load failed %s (timeout=%d hash=%016llx asset=%llx)", path.c_str(), !ok,
                (unsigned long long)st->hash, (unsigned long long)st->asset);
            return false;
        }
        for (int waited = 0; waited < 30000; waited += 20) {
            uint8_t state = 0;
            if (!memory::Read(st->asset + 1, state)) break;
            if (state == 1) {
                LOG("zone: loaded %s (hash=%016llx asset=%llx, %d ms)", path.c_str(), (unsigned long long)st->hash,
                    (unsigned long long)st->asset, waited);
                return true;
            }
            Sleep(20);
        }
        LOG("zone: %s not loaded after 30 s (asset=%llx)", path.c_str(), (unsigned long long)st->asset);
        return false;
    }

    bool Teleport(float x, float y, float z) {
        if (!gamethread::Installed()) { LOG("tp: run 'hook' first"); return false; }
        auto get_actor = (GetActorFn)natives::Get(natives::GetActor);
        if (!get_actor) return false;
        auto result = std::make_shared<std::atomic<int>>(0);
        bool ok = RunOnGameThread([get_actor, x, y, z, result] {
            uint32_t handle = 0;
            uintptr_t hero = 0, transform = 0;
            uintptr_t system = scan::FromRva(natives::kHeroSystemRva);
            if (!memory::Read(system + 0x1C, handle) || !handle) return;
            if (!memory::SafeCall(get_actor, &handle, hero) || !hero) return;
            if (!memory::Read(hero, transform) || !transform) return;
            float* pos = (float*)(transform + 0x30);   // same write as Script Hook's SetPosition
            pos[0] = x; pos[1] = y; pos[2] = z;
            *result = 1;
        }, 5000);
        LOG("tp: (%.1f, %.1f, %.1f) %s", x, y, z, ok && *result ? "ok" : "FAILED");
        return ok && *result;
    }

    // Raft roof "cage match": the top roof of the Raft, walked by the user on 2026-10-13 (one step in from the edge,
    // recon/raft_cage_perimeter.csv). Flat at y 187.0, about 13 x 18 m. The roofs below are partly hollow, so the fence
    // puts anyone who leaves it back on top.
    static const Point2 kRaftCageFence[] = {
        { 1360.0f, -1204.5f }, { 1356.9f, -1205.3f }, { 1355.3f, -1208.2f }, { 1355.5f, -1211.3f },
        { 1355.8f, -1216.7f }, { 1355.9f, -1219.0f }, { 1358.4f, -1221.3f }, { 1360.3f, -1221.9f },
        { 1363.7f, -1221.9f }, { 1366.8f, -1220.4f }, { 1368.5f, -1218.6f }, { 1368.6f, -1215.4f },
        { 1368.6f, -1208.8f }, { 1368.3f, -1206.7f }, { 1366.2f, -1204.8f }, { 1364.5f, -1204.2f },
    };
    // Boss spawn spots: a ring 4.5 m around the roof centre (1362.0, 187.0, -1213.1).
    static const SpawnPoint kRaftCagePoints[] = {
        { 1366.5f, 187.0f, -1213.1f }, { 1365.2f, 187.0f, -1209.9f }, { 1362.0f, 187.0f, -1208.6f },
        { 1358.8f, 187.0f, -1209.9f }, { 1357.5f, 187.0f, -1213.1f }, { 1358.8f, 187.0f, -1216.3f },
        { 1362.0f, 187.0f, -1217.6f }, { 1365.2f, 187.0f, -1216.3f },
    };

    // Open-world arenas: no zones to load, just bounds. Ryker's yard bounds come from the ground-level
    // placements in GP_A2_PRISON_EXT_GAMEPLAY.zone (middle 90%) plus a walk-through on 2026-10-12.
    static const Bounds kArenas[] = {
        {
            .name = "rykers", .tp = { 1136.9f, -0.5f, -750.6f },
            .min_x = 1205.0f, .max_x = 1300.0f, .min_z = -880.0f, .max_z = -745.0f, .ground_y = 1.0f,
            .points = kRykersSpawnPoints, .point_count = std::size(kRykersSpawnPoints),
            .title = "Ryker's Island", .mode = "campaign",
            .start = { 1208.1f, 1.0f, -749.5f }, .start_radius = 5.0f,
            .has_first_spawn = true, .first_spawn = { 1270.7f, 1.0f, -775.2f },
            .exit = { 1133.85f, -0.98f, -750.69f }, .wall = true,
        },
        {
            .name = "raft", .tp = { 1357.6f, 187.5f, -1206.6f },
            .min_x = 1355.3f, .max_x = 1368.6f, .min_z = -1221.9f, .max_z = -1204.2f, .ground_y = 187.0f,
            .points = kRaftCagePoints, .point_count = std::size(kRaftCagePoints),
            .title = "The Raft", .mode = "bossrush",
            .start = { 1362.0f, 187.0f, -1213.1f }, .start_radius = 3.5f,
            .fence = kRaftCageFence, .fence_count = std::size(kRaftCageFence), .fall_y = 182.0f,
            .rescue = { 1362.0f, 187.5f, -1213.1f },
        },
    };
    static const Bounds* g_current = nullptr;

    const Bounds* Current() { return g_current; }
    void Clear() { g_current = nullptr; LOG("arena: cleared (director spawns in a ring around the hero)"); }

    const Bounds* Find(const std::string& name) {
        for (const auto& a : kArenas) if (name == a.name) return &a;
        return nullptr;
    }

    bool Activate(const std::string& name) {
        const Bounds* a = Find(name);
        if (!a) return false;
        g_current = a;
        LOG("arena: %s active (%zu spawn points%s)", a->name, a->point_count, a->fence ? ", fenced" : "");
        return true;
    }

    // Point-in-polygon (ray casting); outside within `margin` of an edge also counts as inside.
    bool InsideFence(const Bounds& a, float x, float z, float margin) {
        if (!a.fence || a.fence_count < 3) return true;
        bool in = false;
        for (size_t i = 0, j = a.fence_count - 1; i < a.fence_count; j = i++) {
            const Point2 &p = a.fence[i], &q = a.fence[j];
            if ((p.z > z) != (q.z > z) && x < (q.x - p.x) * (z - p.z) / (q.z - p.z) + p.x) in = !in;
        }
        if (in || margin <= 0) return in;
        for (size_t i = 0, j = a.fence_count - 1; i < a.fence_count; j = i++) {
            const Point2 &p = a.fence[i], &q = a.fence[j];
            float ex = q.x - p.x, ez = q.z - p.z, len2 = ex * ex + ez * ez;
            float t = len2 > 0 ? std::clamp(((x - p.x) * ex + (z - p.z) * ez) / len2, 0.0f, 1.0f) : 0.0f;
            float dx = x - (p.x + t * ex), dz = z - (p.z + t * ez);
            if (dx * dx + dz * dz <= margin * margin) return true;
        }
        return false;
    }

    // Signed distance of (x, z) past the start-line wall (positive = inside the arena side). No wall: +1e9.
    float WallSide(const Bounds& a, float x, float z) {
        if (!a.wall) return 1.0e9f;
        const float* e = a.exit[0] != 0 ? a.exit : a.tp;
        float nx = a.start[0] - e[0], nz = a.start[2] - e[2], len = sqrtf(nx * nx + nz * nz);
        if (len < 0.01f) return 1.0e9f;
        nx /= len; nz /= len;
        return (x - a.start[0]) * nx + (z - a.start[2]) * nz;
    }

    bool MoveEntity(uintptr_t entity, float x, float y, float z) {
        if (!entity || !gamethread::Installed()) return false;
        gamethread::Post([entity, x, y, z] {
            uintptr_t transform = 0;
            if (!memory::Read(entity, transform) || !transform) return;
            float p[3] = { x, y, z };
            memory::SafeWrite(transform + 0x30, p, sizeof(p));
        });
        return true;
    }
    bool Enter(const std::string& name, bool full) {
        for (const auto& a : kArenas) {
            if (name != a.name) continue;
            if (!Teleport(a.tp[0], a.tp[1], a.tp[2])) return false;
            g_current = &a;
            LOG("arena: %s (x %.0f..%.0f, z %.0f..%.0f, ground %.1f, %zu spawn points)", a.name, a.min_x, a.max_x,
                a.min_z, a.max_z, a.ground_y, a.point_count);
            return true;
        }
        if (name != "raft") { LOG("arena: unknown arena '%s' (known: rykers, raft)", name.c_str()); return false; }
        std::vector<const char*> zones(std::begin(kRaftZones), std::end(kRaftZones));
        if (full) zones.insert(zones.end(), std::begin(kRaftGameplayZones), std::end(kRaftGameplayZones));
        int loaded = 0;
        for (const char* z : zones) loaded += LoadZone(z);
        LOG("arena: %d/%zu raft zones loaded", loaded, zones.size());
        if (loaded == 0) return false;
        Sleep(1000);
        return Teleport(kRaftSpawn[0], kRaftSpawn[1], kRaftSpawn[2]);
    }
}
