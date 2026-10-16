#include "stress.h"

#include "arena.h"
#include "entity.h"
#include "gamethread.h"
#include "hud.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "scan.h"
#include "paths.h"
#include "roster.h"
#include "spawn.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

namespace stress {
    static std::atomic<bool> g_running = false, g_stop = false;
    static std::thread g_thread;

    static std::string Stamp() {
        time_t t = time(nullptr);
        tm lt;
        localtime_s(&lt, &t);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &lt);
        return buf;
    }

    static void TopUpHero() {
        // Keep the hero alive during an engaged stress test: refill HeroHealth via Health::SetHealth.
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

    static void Run(Params p, std::function<director::HeroPos()> hero) {
        const roster::Faction* f = roster::FindFaction(p.faction);
        if (!f) f = &roster::Factions().front();
        std::vector<const roster::Unit*> units;
        if (p.mixed) {
            for (const auto& fac : roster::Factions())
                for (const auto& u : fac.units)
                    if (!u.calm) units.push_back(&u);
        } else {
            for (const auto& u : f->units)
                if (u.role == roster::Role::Melee || u.role == roster::Role::Baton || u.role == roster::Role::Pistol)
                    units.push_back(&u);
        }
        std::mt19937 rng(p.seed);
        std::vector<uintptr_t> spawned;
        std::vector<float> spawn_y;
        std::vector<int> spawn_point;
        std::string file = DataDir() + "\\EndlessMode_runs";
        CreateDirectoryA(file.c_str(), nullptr);
        file += "\\stress_" + Stamp() + ".csv";
        std::ofstream csv(file);
        csv << "enemies,alive,failures,avg_hz,worst_ms,spawn_ms_avg,engaged\n";
        LOG("stress: start step=%d max=%d start=%d floor=%.0f Hz engaged=%d roster=%s (%zu units) seed=%u bodies=%d%% -> %s",
            p.step, p.max, p.start, p.min_hz, p.engaged, p.mixed ? "mixed" : f->id, units.size(), p.seed, p.bodies, file.c_str());
        for (auto* u : units) spawn::Ready(u->path);   // stream every type in first (cold spawns can crash)
        hud::Show("Stress test: measuring baseline...", 4.0f);

        auto measure = [&](int alive_target, int failures, double spawn_ms) {
            for (int s = 0; s < p.settle_s * 10 && !g_stop; s++) {
                if (p.engaged && s % 5 == 0) TopUpHero();
                Sleep(100);
            }
            gamethread::TakeFrameStats();
            for (int s = 0; s < p.measure_s * 10 && !g_stop; s++) {
                if (p.engaged && s % 5 == 0) TopUpHero();
                Sleep(100);
            }
            gamethread::FrameStats fs = gamethread::TakeFrameStats();
            int alive = 0;
            for (size_t i = 0; i < spawned.size(); i++) {
                float hp = 0, mx = 0;
                if (entity::ReadHealth(spawned[i], hp, mx) && hp > 0) alive++;
                // Falling check: well below where it was dropped in = fell off/through something.
                uintptr_t tr = 0;
                float pos[3];
                if (memory::Read(spawned[i], tr) && tr && memory::SafeRead(tr + 0x30, pos, sizeof(pos)) &&
                    pos[1] < spawn_y[i] - 4.0f)
                    LOG("stress: #%zu (point %d) is %.1f m below its spawn height (y=%.1f)", i, spawn_point[i],
                        spawn_y[i] - pos[1], pos[1]);
            }
            csv << alive_target << "," << alive << "," << failures << "," << fs.avg_hz << "," << fs.worst_ms << ","
                << spawn_ms << "," << (p.engaged ? 1 : 0) << "\n";
            csv.flush();
            LOG("stress: %3d enemies (%d alive, %d failed): %.1f Hz, worst frame %.1f ms, spawn %.1f ms each",
                alive_target, alive, failures, fs.avg_hz, fs.worst_ms, spawn_ms);
            char buf[128];
            snprintf(buf, sizeof(buf), "Stress: %d enemies - %.0f ticks/s, worst %.0f ms", alive_target, fs.avg_hz, fs.worst_ms);
            hud::Show(buf, 3.0f);
            return fs;
        };

        gamethread::FrameStats base = measure(0, 0, 0);
        int failures = 0, best_ok = 0;
        while (!g_stop && (int)spawned.size() < p.max) {
            director::HeroPos h = hero();
            if (!h.ok) { Sleep(500); continue; }
            LARGE_INTEGER t0, t1, fq;
            QueryPerformanceFrequency(&fq);
            QueryPerformanceCounter(&t0);
            int made = 0, failed = 0;
            const int batch = spawned.empty() && p.start > 0 ? p.start : p.step;
            for (int i = 0; i < batch && !g_stop; i++) {
                std::uniform_real_distribution<float> ang(0.0f, 6.2831853f), rad(12.0f, 35.0f);
                float a = ang(rng), r = rad(rng);
                spawn::Vec3 pos{ h.x + r * cosf(a), h.y + 1.5f, h.z + r * sinf(a) };
                int point = -1;
                if (const arena::Bounds* ar = arena::Current(); ar && ar->points && ar->point_count) {
                    point = (int)std::uniform_int_distribution<size_t>(0, ar->point_count - 1)(rng);
                    const auto& sp = ar->points[point];
                    pos = { sp.x, sp.y + 1.5f, sp.z };
                }
                const roster::Unit* u = units.empty() ? &f->units[0] : units[std::uniform_int_distribution<size_t>(0, units.size() - 1)(rng)];
                float heading = atan2f(h.z - pos.z, h.x - pos.x) * 57.29578f;
                uintptr_t e = spawn::SpawnActor(u->path, pos, heading, p.engaged);
                if (e) {
                    spawned.push_back(e);
                    spawn_y.push_back(pos.y);
                    spawn_point.push_back(point);
                    LOG("stress: #%zu %s at point %d", spawned.size() - 1, roster::ShortName(u->path).c_str(), point);
                    made++;
                }
                else failed++;
            }
            QueryPerformanceCounter(&t1);
            failures += failed;
            if (p.bodies > 0 && made > 0) {
                // Kill this step's share right away so it ends up as bodies (they ragdoll during the settle wait).
                int kill = (made * p.bodies + 50) / 100;
                for (int k = 0; k < kill; k++) {
                    uintptr_t e = spawned[spawned.size() - 1 - k];
                    gamethread::Post([e] {
                        using SetFn = void (*)(uintptr_t, float);
                        auto set = (SetFn)natives::Get(natives::HealthSet);
                        if (uintptr_t hc = entity::HealthComponent(e); hc && set) set(hc, 0.0f);
                    });
                }
                LOG("stress: killed %d of this step's %d (bodies)", kill, made);
            }
            double spawn_ms = made ? double(t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart / made : 0;
            gamethread::FrameStats fs = measure((int)spawned.size(), failures, spawn_ms);
            if (fs.avg_hz >= p.min_hz) best_ok = (int)spawned.size();
            if (fs.avg_hz < p.min_hz) { LOG("stress: below the %.0f Hz floor, stopping", p.min_hz); break; }
            if (failed >= batch) { LOG("stress: a whole step failed to spawn, stopping"); break; }
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "Stress test done: held %.0f+ ticks/s up to %d enemies (baseline %.0f)", p.min_hz,
                 best_ok, base.avg_hz);
        LOG("stress: %s", buf);
        hud::Show(buf, 8.0f);
        for (uintptr_t e : spawned) spawn::DespawnActor(e);
        LOG("stress: despawned %zu", spawned.size());
        // Release our asset references 30 s later, once dropped weapons and effects have faded (releasing under them
        // crashed the game, 2026-10-14), unless the director started a run in the meantime.
        std::thread([] { Sleep(30000); if (!director::Running() && !g_running) spawn::ReleaseAll(); }).detach();
        g_running = false;
    }

    void Start(const Params& p, std::function<director::HeroPos()> hero) {
        if (g_running) { LOG("stress: already running"); return; }
        if (director::Running()) { LOG("stress: stop the wave director first"); return; }
        if (g_thread.joinable()) g_thread.join();
        g_stop = false;
        g_running = true;
        g_thread = std::thread(Run, p, std::move(hero));
    }

    void Stop() {
        g_stop = true;
        if (g_thread.joinable()) g_thread.join();
    }

    bool Running() { return g_running; }
}
