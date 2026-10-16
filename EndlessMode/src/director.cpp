#include "director.h"

#include "arena.h"
#include "boons.h"
#include "entity.h"
#include "gamethread.h"
#include "hud.h"
#include "timescale.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "paths.h"
#include "gfx.h"
#include "panels.h"
#include "plan.h"
#include "score.h"
#include "scan.h"
#include "spawn.h"
#include "stats.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace director {
    constexpr DWORD kIntermissionMs = 7000;
    constexpr DWORD kChapterIntermissionMs = 10000;
    constexpr DWORD kCorpseLingerMs = 1500;     // end of run: let the dropped crowd lie a moment before cleanup (was 6 s; user 2026-10-16: too slow)
    // Bodies stay on the ground (user, 2026-10-14). Live enemies and bodies share one pool (Options::body_pool, kept
    // under the ~160-actor hang from the stress test). Bodies are cleared oldest first, only once the pool is within
    // kPoolHeadroom of full, and only while the camera can't see them (spawns never happen in view either).
    constexpr int kPoolHeadroom = 12;
    constexpr DWORD kTickMs = 400;
    constexpr float kSpawnRadiusMin = 6.0f, kSpawnRadiusMax = 10.0f;   // no arena: ring around the hero
    constexpr float kArenaSpawnMin = 8.0f, kArenaSpawnMax = 40.0f;    // arena: distance from the hero
    constexpr float kMinSpawnM = 18.0f, kFarSpawnM = 45.0f;            // arena spawns: never closer; in view only beyond
    constexpr float kSpawnDrop = 1.5f;                                 // spawn this far above ground and let them land

    constexpr DWORD kStationaryMs = 20000;   // not moved 2 m in this long (and > kStationaryM away): moved closer
    constexpr float kStationaryM = 25.0f;    // closer than this they're probably waiting their turn to attack
    // Boss watchdog: a boss that never loses health and never moves for this long isn't fighting (scripted boss
    // that needs its mission). It is removed and skipped for the rest of the run.
    constexpr DWORD kBossIdleMs = 40000;
    constexpr DWORD kNoHealthMs = 15000;          // health never readable: we can't tell when it dies

    struct Enemy {
        uintptr_t entity = 0;
        uint32_t handle = 0;
        const char* path = nullptr;
        roster::Role role = roster::Role::Melee;
        const roster::Boss* boss = nullptr;
        DWORD dead_since = 0;         // 0 while alive
        bool seen_health = false;     // health was readable at least once (unreadable before that is not a death)
        bool seen_alive = false;      // health was above 0 at least once (0 right after spawn is not a death)
        float last_health = -1;
        DWORD last_change = 0;        // last health change (bosses: activity)
        int point = -1;               // arena spawn point index for stuck checks, -1 = not checked
        spawn::Vec3 spawned_at{};
        DWORD spawn_time = 0;
        float max_moved = 0.0f;
        bool stuck_checked = false;
        bool never_left_checked = false;   // the "never left its spawn spot" check (cages) was done
        int spawn_point = -1;         // arena point it spawned at (any role), for blacklisting stragglers
        DWORD floor_since = 0;        // boss finisher: when it first sat at <= 1 HP
        spawn::Vec3 anchor{};         // straggler check: where it last was 2 m away from
        DWORD anchor_time = 0;
        uintptr_t head = 0;           // separate head actor (found once it's alive), removed along with the body
        uint32_t head_handle = 0;
        bool head_checked = false;
        bool must_go = false;         // failed spawn (stuck, never alive, idle boss): remove once out of view
        bool infight = false;         // killed by another enemy: a body, but never counted
    };

    static std::mutex g_mutex;
    static std::vector<Enemy> g_enemies;
    static int g_heads_logged = 0;
    static std::atomic<bool> g_running = false;
    static std::atomic<bool> g_stop = false;
    static std::string g_stop_reason;
    static std::thread g_thread;
    static std::function<HeroPos()> g_hero;
    static std::function<void(const std::string&)> g_on_end;
    static score::Results g_results;                 // Horde results waiting for the portal to show them
    static int g_results_level = 0;
    static std::atomic<bool> g_results_ready = false;
    static std::atomic<bool> g_paused = false;
    static std::atomic<DWORD> g_pause_start{ 0 };
    static std::atomic<bool> g_force_aggro = true;   // spawn with BotData.SpawnEngagedWithHero set
    static std::atomic<int> g_riot_max = 0;          // SetRiot override for the next run / current run
    static std::mt19937 g_rng{ std::random_device{}() };

    // Current run (director thread only, except status reads under g_mutex).
    static plan::Config g_cfg;
    static plan::WavePlan g_plan;
    static int g_remaining = 0;                         // goons of the current wave not yet spawned
    static std::vector<const roster::Boss*> g_boss_queue;
    static int g_kills = 0;
    static int g_last_alive = 99;                       // alive count from the previous tick (straggler check)
    static DWORD g_preload_time = 0;                    // when boss assets were requested (bosses wait >= 20 s)
    static Options g_opt;                               // options of the current run
    // Spawn steering (director thread). Bodies due for cleanup that the camera is looking at: new enemies come from
    // the other side, which turns the camera away so they can be cleared (g_cull_dir: hero -> those bodies, unit).
    // Roaming: g_roam_* follows the hero slowly (~45 s); points far from it are favoured so the fight keeps moving
    // around the prison instead of pinning the player in one spot.
    // Spawn points where an enemy never left its spawn spot (cages, blocked cells): run-local, forgotten at the next run (user,
    // 2026-10-15: no saved blacklist). Struck points are strongly avoided as spawn and relocation targets.
    static std::map<int, int> g_run_strikes;
    static DWORD g_boss_banner_hide = 0;   // when to hide the boss pop-up (0 = none showing)
    static bool g_cull_blocked = false;
    static float g_cull_dir[2] = {};
    static bool g_roam_ok = false;
    static float g_roam_x = 0, g_roam_z = 0;

    // No spawn-point blacklist any more (user, 2026-10-14): enemies that stand still are quietly moved closer instead
    // (see "Stationary" in Update). The old EndlessMode_badpoints_<arena>.txt files are no longer read.

    // In an arena: a designer point (or random point in bounds) 8..40 m from the hero, preferably behind them
    // (out of view) so enemies don't pop into existence on screen. No arena: a ring around the hero.
    static spawn::Vec3 PickSpawnPoint(const HeroPos& h, int& point_out, float max_d = 1e9f) {
        point_out = -1;
        const arena::Bounds* a = arena::Current();
        if (!a) {
            std::uniform_real_distribution<float> angle(0.0f, 6.2831853f), radius(kSpawnRadiusMin, kSpawnRadiusMax);
            float t = angle(g_rng), r = radius(g_rng);
            return { h.x + r * cosf(t), h.y, h.z + r * sinf(t) };
        }
        float flen = sqrtf(h.fwd_x * h.fwd_x + h.fwd_z * h.fwd_z);
        auto behind = [&](float dx, float dz, float d) {
            return flen < 0.01f || (dx * h.fwd_x + dz * h.fwd_z) / (d * flen) < -0.2f;
        };
        if (a->points && a->point_count) {
            // "Anywhere, everywhere" (user, 2026-10-13): any point in the arena far enough from the hero, so the crowd
            // spreads over the whole yard as numbers grow (they're engaged, so they all come running). Weights: behind
            // the hero x3, far (> 30 m) x2, away from bodies waiting for cleanup x4, away from where the hero has been
            // x2. Points within 2 m of the start-line wall are skipped (they'd be pushed back anyway).
            // Never in view (user, 2026-10-14: pop-ins still happened right in front of them): spawns are at least
            // kMinSpawnM away, and a point the camera may see (within ~78 degrees of where it looks, measured from the
            // camera; without the camera, Spidey's facing) is only used if it's kFarSpawnM+ away. If nothing qualifies,
            // there is no spawn this tick (point_out = -2).
            std::vector<std::pair<size_t, float>> near_cand, any_cand, far_view;
            float t_near = 0, t_any = 0, t_view = 0;
            for (size_t i = 0; i < a->point_count; i++) {
                const auto& sp = a->points[i];
                if (a->wall && arena::WallSide(*a, sp.x, sp.z) < 2.0f) continue;
                float dx = sp.x - h.x, dz = sp.z - h.z, d = sqrtf(dx * dx + dz * dz);
                if (d < kMinSpawnM) continue;
                bool near_strike = false;
                for (auto& [pi, n] : g_run_strikes) {
                    float sx = a->points[pi].x - sp.x, sz = a->points[pi].z - sp.z;
                    if (sx * sx + sz * sz < 4.0f * 4.0f) { near_strike = true; break; }   // same cell
                }
                if (near_strike) continue;
                float w = (behind(dx, dz, d) ? 3.0f : 1.0f) * (d > 30.0f ? 2.0f : 1.0f);
                if (g_cull_blocked && (dx * g_cull_dir[0] + dz * g_cull_dir[1]) / d < -0.3f) w *= 4.0f;
                if (g_roam_ok) {
                    float rx = sp.x - g_roam_x, rz = sp.z - g_roam_z;
                    if (rx * rx + rz * rz > 35.0f * 35.0f) w *= 2.0f;
                }
                bool front;
                if (float clen = sqrtf(h.cam_fx * h.cam_fx + h.cam_fz * h.cam_fz); h.cam_ok && clen > 0.01f) {
                    float cx = sp.x - h.cam_x, cz = sp.z - h.cam_z, cd = sqrtf(cx * cx + cz * cz);
                    front = cd < 0.01f || (cx * h.cam_fx + cz * h.cam_fz) / (cd * clen) > 0.2f;
                } else {
                    front = flen < 0.01f || (dx * h.fwd_x + dz * h.fwd_z) / (d * flen) > 0.0f;
                }
                if (front) { if (d >= kFarSpawnM) { far_view.push_back({ i, w }); t_view += w; } continue; }
                any_cand.push_back({ i, w }); t_any += w;
                if (d <= max_d) { near_cand.push_back({ i, w }); t_near += w; }
            }
            auto pick_from = [&](std::vector<std::pair<size_t, float>>& c, float total) {
                float x = std::uniform_real_distribution<float>(0.0f, total)(g_rng);
                size_t pick = c.back().first;
                for (auto& [i, w] : c) { if (x < w) { pick = i; break; } x -= w; }
                point_out = (int)pick;
                return spawn::Vec3{ a->points[pick].x, a->points[pick].y + kSpawnDrop, a->points[pick].z };
            };
            if (!near_cand.empty()) return pick_from(near_cand, t_near);
            if (!any_cand.empty()) return pick_from(any_cand, t_any);
            if (!far_view.empty()) return pick_from(far_view, t_view);
            point_out = -2;
            return { h.x, h.y, h.z };
        }
        std::uniform_real_distribution<float> rx(a->min_x, a->max_x), rz(a->min_z, a->max_z);
        spawn::Vec3 best{ rx(g_rng), a->ground_y + kSpawnDrop, rz(g_rng) };
        for (int attempt = 0; attempt < 60; attempt++) {
            spawn::Vec3 p{ rx(g_rng), a->ground_y + kSpawnDrop, rz(g_rng) };
            float dx = p.x - h.x, dz = p.z - h.z, d = sqrtf(dx * dx + dz * dz);
            if (d < kArenaSpawnMin || d > kArenaSpawnMax) continue;
            best = p;
            if (behind(dx, dz, d) || attempt >= 40) return p;
        }
        return best;
    }

    static bool ReadPos(uintptr_t entity, float p[3]) {
        uintptr_t tr = 0;
        return memory::Read(entity, tr) && tr && memory::SafeRead(tr + 0x30, p, 3 * sizeof(float));
    }

    // Out of the camera's view (the same ~78 degree cone spawning avoids). Without the camera: Spidey's facing.
    static bool Hidden(const HeroPos& h, float x, float z) {
        float fx = h.cam_fx, fz = h.cam_fz, ox = h.cam_x, oz = h.cam_z, lim = 0.2f;
        if (!h.cam_ok || fx * fx + fz * fz < 1e-4f) { fx = h.fwd_x; fz = h.fwd_z; ox = h.x; oz = h.z; lim = 0.0f; }
        float flen = sqrtf(fx * fx + fz * fz), dx = x - ox, dz = z - oz, d = sqrtf(dx * dx + dz * dz);
        if (flen < 0.01f || d < 0.01f) return false;
        return (dx * fx + dz * fz) / (d * flen) <= lim;
    }

    // ---- spawning ----

    static bool g_spawn_deferred = false;   // the last SpawnOne found no hidden point (director thread)

    static bool SpawnOne(const HeroPos& h, const char* path, roster::Role role, const roster::Boss* boss, bool calm = false) {
        int point = -1;
        spawn::Vec3 pos = PickSpawnPoint(h, point);
        g_spawn_deferred = point == -2;
        if (g_spawn_deferred) return false;   // nowhere out of sight right now: try again next tick (not a failure)
        // Every wave, wave 1 included, spawns everywhere (user, 2026-10-13: the wave-1 corner is gone).
        // Face the hero: transform row 2 is forward, heading = atan2(dz, dx) (verified in game).
        float heading = atan2f(h.z - pos.z, h.x - pos.x) * 57.29578f;
        uintptr_t e = spawn::SpawnActor(path, pos, heading, g_force_aggro && !calm);
        std::string type = roster::ShortName(path);
        if (!e) {
            LOG("director: spawn failed: %s", type.c_str());
            stats::SpawnFailed(type);
            return false;
        }
        Enemy en;
        en.entity = e;
        en.handle = entity::HandleOf(e);
        en.path = path;
        en.role = role;
        en.boss = boss;
        // Ranged enemies may hold position and shoot, so only melee types are judged for stuck spawns.
        en.point = (!boss && strstr(path, "enm_meleethug")) ? point : -1;
        en.spawn_point = point;
        en.spawned_at = pos;
        en.spawn_time = en.last_change = GetTickCount();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_enemies.push_back(en);
        }
        stats::EnemySpawned(en.handle, type, g_plan.wave, point, pos.x, pos.y, pos.z, boss != nullptr, 0);
        if (boss) {
            // Fanfare (user, 2026-10-15: Mister Negative spawned and nobody noticed): the crime pop-up, red, with the boss's name.
            // Recorded call: ShowCrimeScanner(title, subtitle, 2, 5, <array>); it stays until HideMission().
            gfx::Invoke(0x210, 0x38, "ShowCrimeScanner", { gfx::Node::Of(std::string(boss->name)), gfx::Node::Of("has joined the fight!"),
                                                           gfx::Node::Of(2), gfx::Node::Of(5), gfx::Node::Arr() });
            g_boss_banner_hide = GetTickCount() + 7000;
        }
        return true;
    }

    // Test option `god`: refill HeroHealth (as the stress test does).
    static void TopUpHero() {
        gamethread::Post([] {
            using SetFn = void (*)(uintptr_t, float);
            auto set = (SetFn)natives::Get(natives::HealthSet);
            uintptr_t system = scan::FromRva(natives::kHeroSystemRva), actor = 0;
            uint32_t handle = 0;
            auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
            if (!set || !get_actor || !memory::Read(system + 0x1C, handle) || !handle) return;
            actor = get_actor(&handle);
            uintptr_t c = actor ? entity::FindComponent(actor, "HeroHealth") : 0;
            float max_hp = 0;
            if (c && memory::Read(c + 0x80, max_hp) && max_hp > 0) set(c, max_hp);
        });
    }

    // ---- per-tick bookkeeping ----

    static int g_pool_used = 0;   // live enemies + bodies after this tick's removals (spawning stays under body_pool)

    // The shared pool (g_mutex held). Failed spawns go as soon as the camera can't see them; bodies only when the pool
    // is nearly full, oldest first, and also only out of view. Bodies that must go but are in view steer the next
    // spawns to the opposite side (g_cull_dir). Also moves the roaming centre.
    static void Cull(const HeroPos& hero, DWORD now, std::vector<uintptr_t>& out) {
        g_cull_blocked = false;
        if (!hero.ok) return;
        if (!g_roam_ok) { g_roam_x = hero.x; g_roam_z = hero.z; g_roam_ok = true; }
        const float k = (float)kTickMs / 45000.0f;
        g_roam_x += (hero.x - g_roam_x) * k;
        g_roam_z += (hero.z - g_roam_z) * k;

        // The game clears some bodies itself (their slot's generation changes): forget those (2026-10-14: 43 culls
        // found no sync object because the body was already gone).
        std::erase_if(g_enemies, [](const Enemy& en) { return en.dead_since && entity::HandleOf(en.entity) != en.handle; });
        int over = (int)g_enemies.size() - (std::max(g_opt.body_pool, 2 * kPoolHeadroom) - kPoolHeadroom);
        std::vector<Enemy*> bodies;
        float sx = 0, sz = 0;
        int blocked = 0;
        auto visible_at = [&](Enemy& en, float p[3]) { return ReadPos(en.entity, p) && !Hidden(hero, p[0], p[2]); };
        for (auto& en : g_enemies) {
            if (!en.dead_since) continue;
            if (!en.must_go) { bodies.push_back(&en); continue; }
            float p[3];
            if (visible_at(en, p)) { sx += p[0] - hero.x; sz += p[2] - hero.z; blocked++; continue; }
            out.push_back(en.entity);
            over--;
        }
        std::sort(bodies.begin(), bodies.end(), [](Enemy* a, Enemy* b) { return a->dead_since < b->dead_since; });
        for (Enemy* en : bodies) {
            if (over <= 0) break;
            if (now - en->dead_since < 3000) continue;   // let it finish falling first
            float p[3];
            if (visible_at(*en, p)) { sx += p[0] - hero.x; sz += p[2] - hero.z; blocked++; continue; }
            out.push_back(en->entity);
            over--;
        }
        if (blocked) {
            float len = sqrtf(sx * sx + sz * sz);
            if (len > 0.5f) { g_cull_blocked = true; g_cull_dir[0] = sx / len; g_cull_dir[1] = sz / len; }
            static DWORD last_log = 0;
            if (now - last_log > 10000) {
                last_log = now;
                LOG("director: pool %zu/%d, %d bodies due but in view; steering spawns away", g_enemies.size(),
                    g_opt.body_pool, blocked);
            }
        }
    }

    // Marks deaths, applies health scaling, removes stuck spawns and idle bosses, returns enemies still alive.
    static int Update(const HeroPos& hero, std::array<int, plan::kRoles>& alive_by_role, int& bosses_alive) {
        std::vector<uintptr_t> to_remove;
        int alive = 0;
        alive_by_role = {};
        bosses_alive = 0;
        std::vector<std::string> hud_msgs;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            DWORD now = GetTickCount();
            for (auto& en : g_enemies) {
                if (en.dead_since != 0) continue;   // bodies: see the pool below
                float p[3];
                bool have_pos = ReadPos(en.entity, p);
                // Start-line wall: enemies can't follow the hero out of the arena; anyone past it is put back on the line.
                if (const arena::Bounds* ar = arena::Current(); have_pos && ar && ar->wall) {
                    float side = arena::WallSide(*ar, p[0], p[2]);
                    if (side < 0.5f) {
                        const float* e = ar->exit[0] != 0 ? ar->exit : ar->tp;
                        float nx = ar->start[0] - e[0], nz = ar->start[2] - e[2], len = sqrtf(nx * nx + nz * nz);
                        nx /= len; nz /= len;
                        float push = 1.0f - side;   // to 1 m inside the line
                        arena::MoveEntity(en.entity, p[0] + nx * push, p[1], p[2] + nz * push);
                    }
                }
                // Fenced arena (Raft roof): anyone knocked off or out is put back (not while ragdolled to death).
                if (const arena::Bounds* ar = arena::Current(); have_pos && ar && ar->fence &&
                    (p[1] < ar->fall_y || !arena::InsideFence(*ar, p[0], p[2], 2.0f))) {
                    arena::MoveEntity(en.entity, ar->rescue[0], ar->rescue[1], ar->rescue[2]);
                    LOG("director: %s left the fence, put back", roster::ShortName(en.path).c_str());
                }
                if (have_pos) {
                    float mx = p[0] - en.spawned_at.x, mz = p[2] - en.spawned_at.z;
                    en.max_moved = std::max(en.max_moved, sqrtf(mx * mx + mz * mz));
                    float ax = p[0] - en.anchor.x, az = p[2] - en.anchor.z;
                    if (!en.anchor_time || ax * ax + az * az > 4.0f) { en.anchor = { p[0], p[1], p[2] }; en.anchor_time = now; }
                }
                // Never left its spawn spot (cages, blocked cells; user report 2026-10-15: enemies stuck behind bars in front of
                // the player, which my relocation then shuffled between cage points). 25 s for melee types, 45 s for ranged (they
                // may hold position). The spawn point and everything within 4 m of it is avoided for the rest of the run, and the
                // enemy is removed once out of view and replaced (not a kill).
                if (have_pos && !en.boss && !en.never_left_checked && en.spawn_point >= 0 && en.max_moved < 2.0f) {
                    bool ranged = en.role == roster::Role::Pistol || en.role == roster::Role::Rifle || en.role == roster::Role::Rocket ||
                                  en.role == roster::Role::Sniper;
                    if (now - en.spawn_time > (ranged ? 45000u : 25000u)) {
                        en.never_left_checked = true;
                        g_run_strikes[en.spawn_point]++;
                        LOG("director: %s never left its spawn spot (point %d at %.0f, %.0f, %.0f); point struck for this run",
                            roster::ShortName(en.path).c_str(), en.spawn_point, p[0], p[1], p[2]);
                        stats::Event("never_left_spawn", roster::ShortName(en.path));
                        en.dead_since = now;       // removed (out of view) and replaced, not a kill
                        en.must_go = true;
                        if (g_plan.kind != "horde") g_remaining++;
                        continue;
                    }
                } else if (en.max_moved >= 2.0f) {
                    en.never_left_checked = true;
                }
                // Stationary (user, 2026-10-14): anyone who hasn't moved 2 m in 20 s while more than 25 m from the hero is
                // probably stuck (cages, rooftops, a bad spawn). Quietly move them to a spawn point 12-30 m from the hero
                // so they come running - only while the camera can see neither spot (the picker only returns hidden
                // points). Replaces the spawn-point blacklist, which ended up excluding 130 of Ryker's 139 points.
                if (have_pos && !en.boss && en.anchor_time && now - en.anchor_time > kStationaryMs && Hidden(hero, p[0], p[2])) {
                    float hx = p[0] - hero.x, hz = p[2] - hero.z;
                    if (sqrtf(hx * hx + hz * hz) > kStationaryM) {
                        int pt = -1;
                        spawn::Vec3 np = PickSpawnPoint(hero, pt, 30.0f);
                        if (Hidden(hero, np.x, np.z)) {
                            arena::MoveEntity(en.entity, np.x, np.y, np.z);
                            en.anchor = np;
                            en.anchor_time = now;
                            LOG("director: stationary %s at (%.0f, %.0f, %.0f) moved to (%.0f, %.0f, %.0f)",
                                roster::ShortName(en.path).c_str(), p[0], p[1], p[2], np.x, np.y, np.z);
                            stats::Event("stationary_moved", roster::ShortName(en.path));
                        }
                    }
                }
                float hp = 0.0f, max_hp = 0.0f;
                bool readable = entity::ReadHealth(en.entity, hp, max_hp);
                if (readable && !en.seen_health) {
                    en.seen_health = true;
                    en.last_health = hp;
                    float mult = en.boss ? g_plan.boss_health_mult * en.boss->health_bonus : g_plan.health_mult;
                    std::string type = roster::ShortName(en.path);
                    LOG("director: %s starts with %.0f/%.0f health%s", type.c_str(), hp, max_hp,
                        mult > 1.01f ? " (scaling)" : "");
                    if (mult > 1.01f) {
                        uintptr_t e = en.entity;
                        gamethread::Post([e, mult] { entity::ScaleHealth(e, mult); });
                    }
                }
                if (readable && hp != en.last_health) { en.last_health = hp; en.last_change = now; }
                if (readable && hp > 0.0f) en.seen_alive = true;
                // Boss finisher: outside their missions bosses stop taking damage at a known gate (1 HP for most,
                // a fraction of max for some: Hammerhead 55%), waiting for a scripted phase change. Reaching the gate
                // = defeated: drop them to 0 (verified on Taskmaster, Sable, Hammerhead, 2026-10-13).
                if (en.boss && readable && hp > 0.0f && max_hp > 1.0f) {
                    float gate_hp = en.boss->gate > 0.0f ? max_hp * en.boss->gate : 1.0f;
                    if (hp <= gate_hp + 1.0f) {
                        if (!en.floor_since) en.floor_since = now;
                        else if (now - en.floor_since > 1000 && now - en.floor_since < 60000) {
                            uintptr_t e = en.entity;
                            gamethread::Post([e] {
                                using SetFn = void (*)(uintptr_t, float);
                                auto set = (SetFn)natives::Get(natives::HealthSet);
                                if (uintptr_t hc = entity::HealthComponent(e); hc && set) set(hc, 0.0f);
                            });
                            LOG("director: finisher on %s at %.0f/%.0f (gate %.0f)", en.boss->name, hp, max_hp, gate_hp);
                            en.floor_since = now - 120000;   // once
                        }
                    } else {
                        en.floor_since = 0;
                    }
                }
                if (en.seen_alive && !en.head_checked && readable && hp > 0.0f && now - en.spawn_time > 3000) {
                    en.head_checked = true;
                    int off = -1;
                    en.head = entity::FindHead(en.entity, 3.0f, &off);
                    en.head_handle = en.head ? entity::HandleOf(en.head) : 0;
                    if (en.head && g_heads_logged++ < 5)
                        LOG("director: %s head actor h%08x (slot +%d)", roster::ShortName(en.path).c_str(),
                            en.head_handle, off);
                }
                if (en.seen_alive && ((readable && hp <= 0.0f) || !readable)) {
                    // Killed by another enemy (infighting): no kill for the player's stats (user), and campaign waves
                    // get the spawn back. The body stays on the ground like any other (user, 2026-10-14: vanishing
                    // right away looked funny). Its separate head actor stays floating where it was (bug #11), so the
                    // head is dropped far underground at once and the body leaves as soon as the camera looks away.
                    if (uintptr_t hc = entity::HealthComponent(en.entity); !en.boss && hc && stats::TakeKilledByEnemy(hc)) {
                        en.dead_since = now;
                        en.infight = true;
                        en.must_go = true;
                        if (!en.head) { en.head = entity::FindHead(en.entity, 3.0f); en.head_handle = en.head ? entity::HandleOf(en.head) : 0; }
                        if (en.head && entity::HandleOf(en.head) == en.head_handle && have_pos)
                            arena::MoveEntity(en.head, p[0], p[1] - 500.0f, p[2]);
                        if (g_plan.kind != "horde") g_remaining++;
                        stats::Event("infight_kill", roster::ShortName(en.path));
                        LOG("director: %s killed by another enemy (not counted)", roster::ShortName(en.path).c_str());
                        continue;
                    }
                    en.dead_since = now;
                    g_kills++;
                    stats::EnemyKilled(en.handle);
                    boons::OnKill();
                    if (g_plan.kind == "horde") score::OnKill(en.role);
                    if (en.boss) {
                        LOG("director: BOSS DOWN: %s", en.boss->name);
                        hud_msgs.push_back(std::string(en.boss->name) + " defeated!");
                    }
                    continue;
                }
                if (!en.seen_alive && now - en.spawn_time > 8000) {
                    // Never had positive health (dead on arrival or never initialised): not a kill, just remove it.
                    LOG("director: %s never came alive; removing it", roster::ShortName(en.path).c_str());
                    stats::Event("dead_on_arrival", roster::ShortName(en.path));
                    if (en.boss) g_cfg.failed_bosses.insert(en.boss->name);
                    else g_remaining++;
                    en.dead_since = now;
                    en.must_go = true;
                    continue;
                }
                if (en.boss) {
                    bool no_health = !en.seen_health && now - en.spawn_time > kNoHealthMs;
                    bool idle = now - en.last_change > kBossIdleMs && en.max_moved < 3.0f;
                    if (no_health || idle) {
                        LOG("director: boss %s %s; removing it and skipping it for this run", en.boss->name,
                            no_health ? "has no readable health" : "never fought");
                        g_cfg.failed_bosses.insert(en.boss->name);
                        stats::Event("boss_failed", std::string(en.boss->name) + (no_health ? " (no health)" : " (idle)"));
                        hud_msgs.push_back(std::string(en.boss->name) + " couldn't join the fight");
                        en.dead_since = now;
                        en.must_go = true;
                        continue;
                    }
                    bosses_alive++;
                } else {
                    alive_by_role[(int)en.role]++;
                }
                alive++;
            }
            Cull(hero, now, to_remove);
            g_pool_used = (int)g_enemies.size() - (int)to_remove.size();
        }
        for (auto& m : hud_msgs) hud::Banner(m, 4.0f);
        for (uintptr_t e : to_remove) {
            uintptr_t head = 0;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                for (auto& en : g_enemies)
                    if (en.entity == e && en.head && entity::HandleOf(en.head) == en.head_handle) head = en.head;
            }
            if (head) spawn::DespawnActor(head);
            spawn::DespawnActor(e);
            std::lock_guard<std::mutex> lock(g_mutex);
            std::erase_if(g_enemies, [e](const Enemy& en) { return en.entity == e; });
        }
        return alive;
    }

    static void DespawnAll() {
        std::vector<Enemy> enemies;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            enemies.swap(g_enemies);
        }
        for (auto& en : enemies) {
            if (en.head && entity::HandleOf(en.head) == en.head_handle) spawn::DespawnActor(en.head);
            spawn::DespawnActor(en.entity);
        }
        if (!enemies.empty()) LOG("director: despawned %zu", enemies.size());
    }

    // ---- preloads ----

    // Goon types we hold preloaded: the factions in play now and next. Nothing is released during a run (2026-10-14):
    // releasing a departed faction's assets crashed the game twice (exe+15f9bc9) and turned re-joining units into white
    // cubes, even when none of its enemies were left - dropped weapons, projectiles and effects outlive them. Memory is
    // bounded instead by the run's faction pool (Horde: kHordePool factions). Everything is released after the run.
    static std::set<std::string> g_unit_preloads;
    // Types no longer wanted, released only once none of them is left on the map (alive or a body): releasing them
    // while some were still around made those enemies invisible (user, 2026-10-14). Their model goes with our reference.
    static std::set<std::string> g_release_pending;
    static std::atomic<int> g_release_gen = 0;   // post-run release is cancelled when a new run starts

    // Director thread, each tick.
    static void ProcessReleases() {
        if (g_release_pending.empty()) return;
        std::vector<std::string> go;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& p : g_release_pending) {
                bool present = std::any_of(g_enemies.begin(), g_enemies.end(), [&](const Enemy& en) { return en.path && p == en.path; });
                if (!present) go.push_back(p);
            }
        }
        for (auto& p : go) { g_release_pending.erase(p); spawn::Release(p); }
        if (!go.empty()) LOG("director: released %zu unit types (none left on the map), %zu still waiting", go.size(), g_release_pending.size());
    }

    static void SyncPreloads(const std::vector<const roster::Faction*>& factions) {
        std::set<std::string> want;
        for (auto* f : factions) for (const auto& u : f->units) want.insert(u.path);
        int added = 0;
        std::vector<std::string> load;
        {
            std::lock_guard<std::mutex> lock(g_mutex);   // UnitsInPlay reads it from the command thread
            for (auto& path : want) if (g_unit_preloads.insert(path).second) { load.push_back(path); added++; }
        }
        for (auto& p : load) spawn::Ready(p);
        std::string names;
        for (auto* f : factions) names += std::string(names.empty() ? "" : ", ") + f->id;
        LOG("director: preloads for %s: %d unit types held (+%d)", names.c_str(), (int)g_unit_preloads.size(), added);
    }

    // Campaign / faction: this chapter's faction and the next one.
    static void SyncCampaignPreloads(int chapter) {
        int n = (int)g_cfg.order.size();
        if (n == 0) return;
        std::vector<const roster::Faction*> f{ g_cfg.order[chapter % n] };
        if (n > 1) f.push_back(g_cfg.order[(chapter + 1) % n]);
        SyncPreloads(f);
    }

    // Horde: the factions of this level and the one joining next.
    static void SyncHordePreloads(int level) {
        auto f = plan::HordeFactions(g_cfg, level);
        for (auto* x : plan::HordeFactions(g_cfg, level + 1))
            if (std::find(f.begin(), f.end(), x) == f.end()) f.push_back(x);
        SyncPreloads(f);
    }

    // ---- run ----

    static plan::Config MakeConfig(const Options& opt, std::mt19937& rng) {
        plan::Config cfg;
        cfg.riot = opt.riot || g_riot_max > 0;
        cfg.riot_max = g_riot_max > 0 ? g_riot_max.load() : opt.riot_max;
        cfg.allow_unverified_bosses = opt.allow_unverified_bosses;
        cfg.horde_max = opt.horde_max;
        if (opt.mode == "horde") {
            cfg.mode = plan::Mode::Horde;
            std::vector<const roster::Faction*> all;
            for (const auto& f : roster::Factions()) all.push_back(&f);
            std::shuffle(all.begin(), all.end(), rng);   // a different invasion order every run
            all = plan::BudgetedPool(all, opt.max_types);   // memory bound: a unit-type budget (see plan.h)
            cfg.order = all;
        } else if (opt.mode == "faction") {
            cfg.mode = plan::Mode::Faction;
            const roster::Faction* f = roster::FindFaction(opt.faction);
            cfg.order = { f ? f : &roster::Factions().front() };
        } else {
            cfg.mode = opt.mode == "bossrush" ? plan::Mode::BossRush : plan::Mode::Campaign;
            cfg.order = plan::CampaignOrder(opt.shuffle, rng, opt.max_types);
        }
        return cfg;
    }

    // The map icon the game shows for a faction's hideout (wave banners carry one; recorded 2026-10-14). Street thugs,
    // Tombstone and Taskmaster have no base icon of their own, so they borrow the closest one (user can change these).
    static const char* FactionIcon(const std::string& id) {
        if (id == "fisk") return "icon_map_fisk_base";
        if (id == "demons") return "icon_map_demons_base";
        if (id == "hammerhead") return "icon_map_hh_base";
        if (id == "olympus") return "icon_map_olympus_base";
        if (id == "prisoners") return "icon_map_prisoners_base";
        if (id == "sable") return "icon_map_sable_base";
        if (id == "taskmaster") return "icon_map_task_challenge";
        if (id == "street") return "icon_map_crime";
        return "icon_map_objective";   // tombstone and anything new
    }

    static void StartWave(int wave) {
        g_plan = plan::Make(g_cfg, wave, g_rng);
        if (g_cfg.mode != plan::Mode::BossRush) SyncCampaignPreloads(g_plan.chapter);
        boons::AdjustWave(g_plan, g_cfg.horde_max);   // curses: more enemies, mix, elites (never more health)
        g_remaining = g_plan.count;
        g_boss_queue = g_plan.bosses;
        LOG("director: === %s ===", g_plan.title.c_str());
        LOG("director: plan %s", plan::Describe(g_plan).c_str());
        std::vector<std::string> bosses;
        for (auto* b : g_plan.bosses) bosses.push_back(b->name);
        stats::WaveStart(wave, g_plan.faction ? g_plan.faction->id : "", g_plan.kind, g_plan.count, g_plan.max_alive,
                         g_plan.health_mult, bosses);
        // The game's own wave banner (message type 21) with the faction's icon (user, 2026-10-15). The counter shows the
        // wave's place in its chapter, so a chapter's last wave (the boss wave) reads FINAL WAVE. Campaign and boss rush
        // only: Horde has no wave banners (user).
        gfx::Node d = gfx::Node::Obj({ { "icon", gfx::Node::Of(FactionIcon(g_plan.faction ? g_plan.faction->id : "")) },
                                       { "currentWave", gfx::Node::Of(g_plan.step + 1) },
                                       { "maxWave", gfx::Node::Of(g_cfg.waves_per_chapter) } });
        if (!gfx::Message(21, "", &d)) hud::Banner(g_plan.title, 5.0f);
    }

    // End of a run (both modes): results, the "left the yard" drop, cleanup, portal callback.
    static void FinishRun(std::string end_reason) {
        if (end_reason.empty()) end_reason = g_stop_reason.empty() ? "stopped" : g_stop_reason;
    
        stats::Live l = stats::Current();
        if (g_cfg.mode == plan::Mode::Horde) {
            g_results = score::End(GetTickCount(), l.kills);
            g_results_level = g_plan.wave;
            g_results_ready = true;
            // The live score panel is NOT hidden here: the game's own flow goes live panel -> results panel with no hide in between,
            // and hiding it first left the results panel without buttons, with placeholder tiers, or invisible (user, 2026-10-16).
        } else {
            // Campaign summary (user, 2026-10-15): the same results panel, shown after the run.
            g_results = score::CampaignEnd(g_plan.wave, l.kills, l.hits_dealt, l.hits_taken);
            g_results_level = g_plan.wave;
            g_results_ready = true;
        }
        if (end_reason == "hero_down") {
            LOG("director: *** HERO DOWN *** wave %d, %d kills", g_plan.wave, l.kills);
            char buf[160];
            if (g_cfg.mode == plan::Mode::Horde)
                snprintf(buf, sizeof(buf), "OVERWHELMED - %d kills, %d hits taken", l.kills, l.hits_taken);
            else
                snprintf(buf, sizeof(buf), "RUN OVER - reached wave %d, %d kills, %d hits taken", g_plan.wave, l.kills, l.hits_taken);
            stats::EndRun("hero_down");
            hud::Banner(buf, 8.0f);
            // Slow motion for the first moments, like the main game's death, so the banner can be read before the load.
            timescale::Set(0.25f, 4.0f);
            Sleep(3500);
            timescale::Unfreeze();
            Sleep(2500);   // let the death play out before the crowd vanishes
        } else {
            stats::EndRun(end_reason.c_str());
            char buf[160];
            if (g_cfg.mode == plan::Mode::Horde) snprintf(buf, sizeof(buf), "Run ended - %d kills", l.kills);   // no levels in Horde
            else snprintf(buf, sizeof(buf), "Run ended at wave %d - %d kills", g_plan.wave, l.kills);
            hud::Banner(buf, 5.0f);
        }
        if (end_reason == "left_arena") {
            // Walking out of the arena: the crowd drops (health 0) instead of vanishing, then the bodies are cleared.
            std::vector<uintptr_t> alive;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                for (auto& en : g_enemies) if (en.dead_since == 0) alive.push_back(en.entity);
            }
            for (uintptr_t e : alive) {
                gamethread::Post([e] {
                    using SetFn = void (*)(uintptr_t, float);
                    auto set = (SetFn)natives::Get(natives::HealthSet);
                    if (uintptr_t hc = entity::HealthComponent(e); hc && set) set(hc, 0.0f);
                });
            }
            LOG("director: %zu enemies dropped (left the arena)", alive.size());
            Sleep(kCorpseLingerMs);
        }
        hud::HideCounter();
        hud::HideCountdown();
        DespawnAll();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_unit_preloads.clear();
        }
        g_release_pending.clear();
        // Every asset reference we took (goons, dependencies, bosses), 30 s later: dropped weapons and effects fade out
        // first. A run started in the meantime keeps them (its preloads find them still held).
        int gen = ++g_release_gen;
        std::thread([gen] {
            Sleep(30000);
            if (gen == g_release_gen && !g_running) spawn::ReleaseAll();
            else LOG("director: post-run release skipped (a new run started)");
        }).detach();
        boons::End();   // restores the player's own suit power and mods
        LOG("director: run ended (%s) at wave %d, %d kills", end_reason.c_str(), g_plan.wave, g_kills);
        g_running = false;
        if (g_on_end) g_on_end(end_reason);
    }

    // Horde mode: one endless stream; the level goes up every kHordeKills kills or kHordeMs, whichever comes first.
    constexpr int kHordeKills = 25;

    static std::string RunHorde() {
        int level = 1, level_kills = 0;
        DWORD level_start = GetTickCount(), last_sample = 0;
        auto start_level = [&](int l) {
            g_plan = plan::MakeHorde(g_cfg, l);
            SyncHordePreloads(l);
            g_remaining = 1 << 30;
            LOG("director: === horde step %d: %s === (up to %d on the field, hp x%.2f)", l,
                g_plan.title.empty() ? "(silent)" : g_plan.title.c_str(), g_plan.max_alive, g_plan.health_mult);
            stats::WaveStart(l, "horde", "horde", 0, g_plan.max_alive, g_plan.health_mult, {});
            // Only when a faction joins. Both the opening line and the "X join the riot" lines use the notification box, for
            // consistency (user, 2026-10-15; the opening line is too long for the banner anyway).
            if (!g_plan.title.empty()) hud::Show(g_plan.title, l == 1 ? 6.0f : 5.0f);
        };
        start_level(level);
        score::Begin(0);
        int last_rank = 0;
        int kills_at_level = g_kills;
        while (!g_stop) {
            if (g_paused) { Sleep(100); continue; }   // the abandon prompt has the world frozen
            HeroPos hero = g_hero();
            stats::SetHero(hero.handle);
            DWORD now = GetTickCount();
            if (hero.ok && now - last_sample >= 500) {
                last_sample = now;
                int alive_now;
                { std::lock_guard<std::mutex> lock(g_mutex); alive_now = (int)std::count_if(g_enemies.begin(), g_enemies.end(), [](const Enemy& e) { return e.dead_since == 0; }); }
                stats::Sample(hero.x, hero.y, hero.z, hero.has_health ? hero.health : -1, hero.max_health, alive_now);
            }
            if (g_opt.god && hero.ok) TopUpHero();
            if (hero.has_health && hero.health <= 0.0f) return "hero_down";
            {
                // Style meter and the live score panel (docs/SCORING.md).
                stats::Live sl = stats::Current();
                score::Tick(now, sl.hits_dealt, sl.hits_taken, level);
                panels::LiveScore(score::Score(), score::Rank());
                // No rank-up banners (user, 2026-10-15): the side panel shows the rank.
                last_rank = score::Rank();
            }
            std::array<int, plan::kRoles> by_role{};
            int bosses_alive = 0;
            int alive = Update(hero, by_role, bosses_alive);
            ProcessReleases();
            g_last_alive = 99;   // no "end of wave" in horde: stragglers are left alone
            int room = std::min(g_plan.max_alive - alive, g_opt.body_pool - g_pool_used);
            int batch = std::min(room, 6);
            for (int i = 0; i < batch && !g_stop; i++) {
                const roster::Unit* u = plan::PickUnit(g_plan, by_role, g_rng);
                if (!u) break;
                if (!spawn::Ready(u->path)) continue;   // still streaming in: pick again next tick
                if (SpawnOne(hero, u->path, u->role, nullptr, u->calm)) by_role[(int)u->role]++;
                else if (g_spawn_deferred) break;      // nowhere out of sight this tick
            }
            level_kills = g_kills - kills_at_level;
            if (level_kills >= kHordeKills || now - level_start > (DWORD)g_opt.horde_level_ms) {
                stats::WaveCleared(level);
                level++;
                level_start = now;
                kills_at_level = g_kills;
                start_level(level);
            }
            Sleep(kTickMs);
        }
        return "";
    }
    static void Run(Options opt) {
        g_opt = opt;
        uint32_t seed = opt.seed ? opt.seed : std::random_device{}();
        g_rng.seed(seed);
        g_cfg = MakeConfig(opt, g_rng);
        g_kills = 0;
        g_plan = {};
        g_cull_blocked = g_roam_ok = false;
        g_run_strikes.clear();
        g_pool_used = 0;

        stats::InstallHooks();
        stats::RunInfo info;
        const arena::Bounds* a = arena::Current();
        info.arena = a ? a->name : "open world";
        info.mode = opt.mode + (opt.mode == "faction" ? ":" + opt.faction : "") + (g_cfg.riot ? "+riot" : "") +
                    (opt.shuffle ? "+shuffle" : "");
        info.seed = seed;
        info.engaged = g_force_aggro;
        for (auto* f : g_cfg.order) info.faction_order.push_back(f->id);
        stats::BeginRun(info);
        // Bosses (and their separate head actors) must be fully streamed in before they spawn, or they come out
        // without a mover / head and crash (2026-10-13). Request every boss this run may use now.
        // Same for every goon type this run can field (all factions in its order), well before they're needed.
        g_unit_preloads.clear();
        g_release_pending.clear();
        if (g_cfg.mode == plan::Mode::Horde) SyncHordePreloads(1);
        else if (g_cfg.mode == plan::Mode::BossRush) SyncPreloads(g_cfg.order);   // boss+goons waves draw from all
        else SyncCampaignPreloads(std::max(opt.first_wave, 1) > 1 ? plan::Make(g_cfg, opt.first_wave, g_rng).chapter : 0);
        for (const auto& b : roster::Bosses()) {
            if (opt.mode == "horde") break;   // no bosses in Horde
            if (!(b.verified || g_cfg.allow_unverified_bosses)) continue;
            spawn::Preload(b.path);
            if (b.head && *b.head) spawn::Preload(b.head);
        }
        g_preload_time = GetTickCount();
        LOG("director: run start (%s, seed %u, first wave %d)", info.mode.c_str(), seed, opt.first_wave);
        if (opt.roguelike && (g_cfg.mode == plan::Mode::Campaign || g_cfg.mode == plan::Mode::Faction))
            boons::Begin(seed);   // zero-start: power and mods cleared and locked; earned wave by wave
        TopUpHero();   // every run starts at full health (user, 2026-10-16)

        // The game's big "Get Ready" countdown (both modes; user, 2026-10-14). The widget is re-sent every second.
        for (int s = opt.countdown_s; s > 0 && !g_stop; s--) {
            hud::Countdown(s);
            Sleep(1000);
        }
        TopUpHero();   // and again as the fight begins, in case something hit the player during the countdown
        hud::HideCountdown();

        if (g_cfg.mode == plan::Mode::Horde) {
            std::string r = RunHorde();
            FinishRun(r);
            return;
        }

        int wave = std::max(opt.first_wave, 1) - 1;
        bool in_wave = false, few_left_shown = false;
        DWORD cleared_at = GetTickCount() - kIntermissionMs;   // first wave right after the countdown
        DWORD next_delay = kIntermissionMs, last_sample = 0;
        std::string end_reason;
        bool chapter_announced = false;
        int shown_left = -1, shown_count = -1;   // campaign HUD: enemy counter and pre-wave countdown last sent

        while (!g_stop) {
            if (g_paused) { Sleep(100); continue; }   // the abandon prompt has the world frozen
            HeroPos hero = g_hero();
            stats::SetHero(hero.handle);
            DWORD now = GetTickCount();
            if (hero.ok && now - last_sample >= 500) {
                last_sample = now;
                int alive_now;
                { std::lock_guard<std::mutex> lock(g_mutex); alive_now = (int)std::count_if(g_enemies.begin(), g_enemies.end(), [](const Enemy& e) { return e.dead_since == 0; }); }
                stats::Sample(hero.x, hero.y, hero.z, hero.has_health ? hero.health : -1, hero.max_health, alive_now);
            }
            if (g_opt.god && hero.ok) TopUpHero();
            if (hero.has_health && hero.health <= 0.0f) { end_reason = "hero_down"; break; }

            std::array<int, plan::kRoles> by_role{};
            int bosses_alive = 0;
            int alive = Update(hero, by_role, bosses_alive);
            ProcessReleases();
            g_last_alive = alive;
            if (g_boss_banner_hide && (LONG)(GetTickCount() - g_boss_banner_hide) >= 0) {
                g_boss_banner_hide = 0;
                gfx::Invoke(0x210, 0x38, "HideMission", {});
            }

            if (in_wave) {
                // Bosses first, one per tick, then goons up to the field cap (a few per tick so they trickle in).
                bool spawned = false;
                if (!g_boss_queue.empty() && GetTickCount() - g_preload_time < 20000) {
                    // Give the boss assets time to stream in (rare: only if the first boss wave comes very early).
                } else if (!g_boss_queue.empty()) {
                    spawned = true;
                    const roster::Boss* b = g_boss_queue.front();
                    g_boss_queue.erase(g_boss_queue.begin());
                    if (!SpawnOne(hero, b->path, roster::Role::Count, b, b->calm)) {
                        if (g_spawn_deferred) g_boss_queue.insert(g_boss_queue.begin(), b);   // next tick
                        else g_cfg.failed_bosses.insert(b->name);
                    }
                } else if (g_remaining > 0) {
                    int room = std::min(g_plan.max_alive - alive, g_opt.body_pool - g_pool_used);
                    int batch = std::min({ room, g_remaining, g_cfg.riot ? 8 : 4 });
                    for (int i = 0; i < batch && !g_stop; i++) {
                        spawned = true;
                        const roster::Unit* u = plan::PickUnit(g_plan, by_role, g_rng);
                        if (!u) break;
                        if (!spawn::Ready(u->path)) continue;   // still streaming in
                        g_remaining--;
                        if (SpawnOne(hero, u->path, u->role, nullptr, u->calm)) by_role[(int)u->role]++;
                        else if (g_spawn_deferred) { g_remaining++; break; }
                    }
                }
                // Enemy counter (campaign only; user, 2026-10-14): everyone left in this wave - on the field, still to
                // spawn, and bosses - out of the wave's total. Sent only when it changes.
                int total = g_plan.count + (int)g_plan.bosses.size();
                int left = std::min(total, alive + g_remaining + (int)g_boss_queue.size());
                if (left != shown_left && total > 0) { shown_left = left; hud::Counter("ENEMIES", left, total); }
                if (!spawned && g_remaining == 0 && g_boss_queue.empty()) {
                    if (alive == 0) {
                        in_wave = false;
                        cleared_at = now;
                        hud::HideCounter();
                        shown_left = -1;
                        stats::WaveCleared(g_plan.wave);
                        boons::OnWaveCleared(g_plan.wave, g_plan.boss_wave);
                        int next_step = (g_plan.step + 1) % std::max(1, g_cfg.waves_per_chapter);
                        bool chapter_end = g_cfg.mode != plan::Mode::BossRush && next_step == 0;
                        next_delay = chapter_end ? kChapterIntermissionMs : kIntermissionMs;
                        chapter_announced = false;
                        // No "Wave n cleared - kills, hits" banner any more (user, 2026-10-15: not necessary).
                        stats::Live l = stats::Current();
                        LOG("director: Wave %d cleared - %d kills, %d hits taken", g_plan.wave, l.kills, l.hits_taken);
                    }
                    // No "n enemies left" pop-up (user, 2026-10-15): the ENEMIES counter already shows it.
                }
            } else if (boons::Offering()) {
                // Reward picks hold the intermission: the next wave starts a few seconds after the last pick.
                // Poll input continuously until every pick is made: with gaps of a director tick (400 ms) between
                // polling bursts, quick taps were missed (user, 2026-10-13). The fight is frozen meanwhile anyway.
                while (boons::Offering() && !g_stop) {
                    boons::Tick();
                    Sleep(15);
                }
                cleared_at = now - next_delay / 2 - 1;   // chapter banner next, then the wave
            } else {
                // Intermission: announce a new chapter half way through, then start the next wave.
                if (!chapter_announced && now - cleared_at > next_delay / 2 && g_cfg.mode == plan::Mode::Campaign) {
                    chapter_announced = true;
                    plan::WavePlan peek = plan::Make(g_cfg, wave + 1, g_rng);   // rng use only affects boss order
                    if (peek.step == 0 && peek.faction) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "CHAPTER %d: %s%s", peek.chapter + 1, peek.faction->name,
                                 peek.cycle ? " (they're tougher now)" : "");
                        hud::Banner(buf, 4.0f);
                    }
                }
                // The big countdown for the last seconds before the wave.
                DWORD waited = now - cleared_at;
                if (waited + 5000 >= next_delay && waited < next_delay) {
                    int sec = (int)((next_delay - waited + 999) / 1000);
                    if (sec != shown_count) { shown_count = sec; hud::Countdown(sec); }
                }
                if (now - cleared_at > next_delay) {
                    if (shown_count >= 0) { hud::HideCountdown(); shown_count = -1; }
                    StartWave(++wave);
                    in_wave = true;
                    few_left_shown = false;
                }
            }
            Sleep(kTickMs);
        }
        FinishRun(end_reason);
    }

    // ---- API ----

    void Start(const Options& opt, std::function<HeroPos()> hero) {
        if (g_running) { LOG("director: already running"); return; }
        if (!gamethread::Installed()) { LOG("director: game-thread hook not installed"); return; }
        if (g_thread.joinable()) g_thread.join();
        g_hero = std::move(hero);
        g_stop = false;
        g_stop_reason.clear();
        g_running = true;
        g_thread = std::thread(Run, opt);
    }

    void Stop(const char* reason) {
        if (!g_running && !g_thread.joinable()) return;
        if (g_thread.joinable() && g_thread.get_id() == std::this_thread::get_id()) return;   // from the end callback
        g_stop_reason = reason ? reason : "stopped";
        g_stop = true;
        if (g_thread.joinable()) g_thread.join();
    }

    bool Running() { return g_running; }
    void SetPaused(bool on) {
        if (on == g_paused) return;
        if (on) { g_pause_start = GetTickCount(); g_paused = true; return; }
        // Everything timed by the wall clock (stationary checks, spawn times) shifts by the time spent paused.
        DWORD paused = GetTickCount() - g_pause_start;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto& en : g_enemies) {
                if (en.anchor_time) en.anchor_time += paused;
                en.spawn_time += paused;
                en.last_change += paused;
                if (en.dead_since) en.dead_since += paused;
            }
        }
        g_paused = false;
    }
    bool TakeResults(score::Results& out, int& level) {
        if (!g_results_ready.exchange(false)) return false;
        out = g_results;
        level = g_results_level;
        return true;
    }
    std::vector<std::string> UnitsInPlay() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return { g_unit_preloads.begin(), g_unit_preloads.end() };
    }

    void OnRunEnded(std::function<void(const std::string&)> cb) { g_on_end = std::move(cb); }

    void LogStatus() {
        std::lock_guard<std::mutex> lock(g_mutex);
        int alive = 0, bosses = 0;
        for (auto& en : g_enemies) { alive += en.dead_since == 0; bosses += en.boss && en.dead_since == 0; }
        LOG("director: running=%d wave=%d (%s) alive=%d bosses=%d remaining=%d tracked=%zu kills=%d", g_running.load(),
            g_plan.wave, g_plan.kind.c_str(), alive, bosses, g_remaining, g_enemies.size(), g_kills);
    }

    void SetRiot(int max_alive) {
        g_riot_max = max_alive;
        if (g_running) { g_cfg.riot = max_alive > 0; if (max_alive > 0) g_cfg.riot_max = max_alive; }
        if (max_alive > 0) LOG("director: RIOT on: up to %d at once", max_alive);
        else LOG("director: riot off");
    }

    void SetForceAggro(bool on) {
        g_force_aggro = on;
        LOG("director: engaged spawns %s (SpawnEngagedWithHero)", on ? "on" : "off");
    }

    void PreviewPlans(const Options& opt, int from, int to) {
        std::mt19937 rng(opt.seed ? opt.seed : 1);
        plan::Config cfg = MakeConfig(opt, rng);
        for (int w = std::max(1, from); w <= to; w++) LOG("plan %s", plan::Describe(plan::Make(cfg, w, rng)).c_str());
    }
}
