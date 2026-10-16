#include "stats.h"

#include "boons.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "paths.h"

#include <MinHook.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace stats {
    // ---- run state ----

    struct Enemy {
        uint32_t handle;
        std::string type;
        int wave, point;
        float x, y, z, start_health;
        double spawn_t, death_t = -1;
        bool boss, stuck = false;
        int hits_on_hero = 0;
        float damage_to_hero = 0;
    };
    struct Wave {
        int n;
        std::string faction, kind;
        std::vector<std::string> bosses;
        int count, max_alive;
        float health_mult;
        double start_t, clear_t = -1;
        int kills = 0, hits_taken = 0;
        float damage_taken = 0;
    };
    struct Hit {
        double t;
        std::string by;
        uint32_t attacker;
        float amount, before, after, x, y, z;
        int wave;
    };
    struct Sample { double t; float x, y, z, health, max_health; int alive; };
    struct Ev { double t; std::string type, detail; };

    static std::mutex g_mutex;
    static std::atomic<bool> g_active = false;
    static RunInfo g_info;
    static std::string g_file, g_started;
    static LARGE_INTEGER g_t0, g_freq;
    static std::vector<Enemy> g_enemies;
    static std::unordered_map<uint32_t, size_t> g_by_handle;
    static std::unordered_set<uintptr_t> g_killed_by_enemy;   // Health components finished off by another enemy
    static std::vector<Wave> g_waves;
    static std::vector<Hit> g_hits;
    static std::vector<Sample> g_samples;
    static std::vector<Ev> g_events;
    static std::vector<int> g_blacklisted;
    static Live g_live;
    static std::atomic<uint32_t> g_hero = 0;
    static float g_last_pos[3] = {};
    static std::string g_end_reason;
    static double g_last_flush = 0;

    static double Now() {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return double(t.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
    }

    static std::string Stamp(const char* fmt) {
        time_t t = time(nullptr);
        tm lt;
        localtime_s(&lt, &t);
        char buf[64];
        strftime(buf, sizeof(buf), fmt, &lt);
        return buf;
    }

    static std::string Esc(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((unsigned char)c < 0x20) o += ' ';
            else o += c;
        }
        return o;
    }

    static Wave* CurrentWave() { return g_waves.empty() ? nullptr : &g_waves.back(); }

    // ---- damage hooks (game thread) ----

    using OnDamageFn = bool (*)(void* self, void* msg);
    static OnDamageFn g_hero_orig = nullptr, g_bot_orig = nullptr;

    static void RecordHeroHit(uint32_t attacker, float amount, float before, float after) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_active) return;
        Hit h{ Now(), "", attacker, amount, before, after, g_last_pos[0], g_last_pos[1], g_last_pos[2],
               CurrentWave() ? CurrentWave()->n : 0 };
        auto it = g_by_handle.find(attacker);
        if (it != g_by_handle.end()) {
            Enemy& e = g_enemies[it->second];
            h.by = e.type;
            e.hits_on_hero++;
            e.damage_to_hero += before - after;
        } else {
            char b[32];
            snprintf(b, sizeof(b), attacker ? "other:%08x" : "environment", attacker);
            h.by = b;
        }
        g_hits.push_back(h);
        g_live.hits_taken++;
        g_live.damage_taken += before - after;
        if (Wave* w = CurrentWave()) { w->hits_taken++; w->damage_taken += before - after; }
    }

    // Damage scaling for roguelike boons and curses (boons::IncomingMult / OutgoingMult). The damage amount in the
    // message (+0x144) is scaled before the game applies it; if that turns out not to drive the health change, the
    // result is corrected afterwards with Health::SetHealth (logged once either way, so we learn which one works).
    static void ScaleHit(void* self, void* msg, float before, float mult, float& after, const char* who) {
        static std::atomic<int> reported = 0;
        float dmg = before - after;
        float amount = 0;
        memory::Read((uintptr_t)msg + 0x144, amount);   // already scaled
        if (dmg <= 0.0f || std::fabs(mult - 1.0f) < 0.01f) return;
        float expected = amount;                         // what the scaled message should have done
        bool prescale_worked = std::fabs(dmg - expected) < 0.5f || after <= 0.0f;
        if (reported.fetch_add(1) < 4)
            LOG("stats: %s damage x%.2f: health -%.1f, message amount %.1f -> %s", who, mult, dmg, amount,
                prescale_worked ? "scaling the message works" : "corrected with SetHealth");
        if (prescale_worked) return;
        using SetFn = void (*)(uintptr_t, float);
        auto set = (SetFn)natives::Get(natives::HealthSet);
        if (!set) return;
        float target = std::max(0.0f, before - dmg * mult);
        set((uintptr_t)self, target);
        after = target;
    }

    static bool HeroDetour(void* self, void* msg) {
        float before = 0, after = 0, amount = 0;
        uint32_t attacker = 0;
        memory::Read((uintptr_t)self + 0x88, before);
        float mult = boons::IncomingMult();
        if (msg && std::fabs(mult - 1.0f) > 0.01f && memory::Read((uintptr_t)msg + 0x144, amount))
            memory::Write<float>((uintptr_t)msg + 0x144, amount * mult);
        bool r = g_hero_orig(self, msg);
        memory::Read((uintptr_t)self + 0x88, after);
        if (msg && std::fabs(mult - 1.0f) > 0.01f) ScaleHit(self, msg, before, mult, after, "incoming");
        if (g_active && msg) {
            memory::Read((uintptr_t)msg + 0x138, attacker);
            memory::Read((uintptr_t)msg + 0x144, amount);
            if (after < before) RecordHeroHit(attacker, amount, before, after);
        }
        return r;
    }

    static bool BotDetour(void* self, void* msg) {
        float before = 0, after = 0, amount = 0;
        uint32_t attacker = 0;
        memory::Read((uintptr_t)self + 0x88, before);
        bool from_hero = msg && memory::Read((uintptr_t)msg + 0x138, attacker) && attacker && attacker == g_hero;
        float mult = from_hero ? boons::OutgoingMult() : 1.0f;
        if (std::fabs(mult - 1.0f) > 0.01f && memory::Read((uintptr_t)msg + 0x144, amount))
            memory::Write<float>((uintptr_t)msg + 0x144, amount * mult);
        bool r = g_bot_orig(self, msg);
        if (from_hero && std::fabs(mult - 1.0f) > 0.01f) {
            memory::Read((uintptr_t)self + 0x88, after);
            ScaleHit(self, msg, before, mult, after, "outgoing");
        }
        if (g_active && msg && attacker) {
            memory::Read((uintptr_t)self + 0x88, after);
            if (after < before) {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (attacker == g_hero) {
                    g_live.hits_dealt++;
                } else if (auto it = g_by_handle.find(attacker); it != g_by_handle.end()) {
                    // One of our enemies hurt something that isn't the hero: factions fighting each other?
                    g_live.infight_hits++;
                    g_live.infight_damage += before - after;
                    if (after <= 0.0f) g_killed_by_enemy.insert((uintptr_t)self);
                    if (g_live.infight_hits <= 20 || g_live.infight_hits % 50 == 0)
                        LOG("stats: infight #%d: %s hit a bot for %.0f (%.0f -> %.0f)", g_live.infight_hits,
                            g_enemies[it->second].type.c_str(), before - after, before, after);
                }
            }
        }
        return r;
    }

    bool InstallHooks() {
        static bool done = false, hero_ok = false;
        if (done) return hero_ok;
        done = true;
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { LOG("stats: MH_Initialize failed"); return false; }
        if (void* t = (void*)natives::Get(natives::HeroOnDamage)) {
            hero_ok = MH_CreateHook(t, (void*)&HeroDetour, (void**)&g_hero_orig) == MH_OK && MH_EnableHook(t) == MH_OK;
        }
        if (void* t = (void*)natives::Get(natives::HealthOnDamage)) {
            // Shared with observer's research hook; if that one got there first we just skip hits-dealt counting.
            if (MH_CreateHook(t, (void*)&BotDetour, (void**)&g_bot_orig) == MH_OK) MH_EnableHook(t);
            else LOG("stats: bot damage hook unavailable (observer's dmgwatch owns it); hits dealt won't be counted");
        }
        LOG("stats: hero damage hook %s", hero_ok ? "on" : "FAILED");
        return hero_ok;
    }

    // ---- writing ----

    static void WriteLocked() {
        if (g_file.empty()) return;
        std::ostringstream o;
        o.setf(std::ios::fixed);
        o.precision(2);
        double now = Now();
        o << "{\n\"version\": 1,\n\"started\": \"" << g_started << "\",\n\"duration_s\": " << now << ",\n";
        o << "\"end_reason\": \"" << Esc(g_end_reason) << "\",\n";
        o << "\"mode\": {\"arena\": \"" << Esc(g_info.arena) << "\", \"mode\": \"" << Esc(g_info.mode)
          << "\", \"seed\": " << g_info.seed << ", \"engaged\": " << (g_info.engaged ? "true" : "false")
          << ", \"faction_order\": [";
        for (size_t i = 0; i < g_info.faction_order.size(); i++)
            o << (i ? ", " : "") << "\"" << Esc(g_info.faction_order[i]) << "\"";
        o << "]},\n";
        int cleared = 0, bosses_killed = 0, stuck = 0;
        for (auto& w : g_waves) cleared += w.clear_t >= 0;
        for (auto& e : g_enemies) { bosses_killed += e.boss && e.death_t >= 0 && !e.stuck; stuck += e.stuck; }
        o << "\"summary\": {\"waves_started\": " << g_waves.size() << ", \"waves_cleared\": " << cleared
          << ", \"kills\": " << g_live.kills << ", \"hits_taken\": " << g_live.hits_taken
          << ", \"damage_taken\": " << g_live.damage_taken << ", \"hits_dealt\": " << g_live.hits_dealt << ", \"infight_hits\": " << g_live.infight_hits
          << ", \"infight_damage\": " << g_live.infight_damage
          << ", \"bosses_defeated\": " << bosses_killed << ", \"enemies_spawned\": " << g_enemies.size()
          << ", \"stuck_removed\": " << stuck << "},\n";
        o << "\"waves\": [";
        for (size_t i = 0; i < g_waves.size(); i++) {
            auto& w = g_waves[i];
            o << (i ? ",\n " : "\n ") << "{\"n\": " << w.n << ", \"faction\": \"" << Esc(w.faction) << "\", \"kind\": \""
              << Esc(w.kind) << "\", \"bosses\": [";
            for (size_t b = 0; b < w.bosses.size(); b++) o << (b ? ", " : "") << "\"" << Esc(w.bosses[b]) << "\"";
            o << "], \"count\": " << w.count << ", \"max_alive\": " << w.max_alive << ", \"health_mult\": " << w.health_mult
              << ", \"start_t\": " << w.start_t << ", \"clear_t\": " << w.clear_t << ", \"kills\": " << w.kills
              << ", \"hits_taken\": " << w.hits_taken << ", \"damage_taken\": " << w.damage_taken << "}";
        }
        o << "],\n\"enemies\": [";
        for (size_t i = 0; i < g_enemies.size(); i++) {
            auto& e = g_enemies[i];
            o << (i ? ",\n " : "\n ") << "{\"type\": \"" << Esc(e.type) << "\", \"wave\": " << e.wave << ", \"point\": "
              << e.point << ", \"boss\": " << (e.boss ? "true" : "false") << ", \"x\": " << e.x << ", \"y\": " << e.y
              << ", \"z\": " << e.z << ", \"start_health\": " << e.start_health << ", \"spawn_t\": " << e.spawn_t
              << ", \"death_t\": " << e.death_t << ", \"stuck\": " << (e.stuck ? "true" : "false")
              << ", \"hits_on_hero\": " << e.hits_on_hero << ", \"damage_to_hero\": " << e.damage_to_hero << "}";
        }
        o << "],\n\"hits_taken\": [";
        for (size_t i = 0; i < g_hits.size(); i++) {
            auto& h = g_hits[i];
            o << (i ? ",\n " : "\n ") << "{\"t\": " << h.t << ", \"wave\": " << h.wave << ", \"by\": \"" << Esc(h.by)
              << "\", \"amount\": " << h.amount << ", \"before\": " << h.before << ", \"after\": " << h.after
              << ", \"x\": " << h.x << ", \"y\": " << h.y << ", \"z\": " << h.z << "}";
        }
        o << "],\n\"samples\": [";   // [t, x, y, z, health, max_health, alive]
        for (size_t i = 0; i < g_samples.size(); i++) {
            auto& s = g_samples[i];
            o << (i ? "," : "") << (i % 8 == 0 ? "\n " : "") << "[" << s.t << "," << s.x << "," << s.y << "," << s.z
              << "," << s.health << "," << s.max_health << "," << s.alive << "]";
        }
        o << "],\n\"events\": [";
        for (size_t i = 0; i < g_events.size(); i++) {
            auto& e = g_events[i];
            o << (i ? ",\n " : "\n ") << "{\"t\": " << e.t << ", \"type\": \"" << Esc(e.type) << "\", \"detail\": \""
              << Esc(e.detail) << "\"}";
        }
        o << "],\n\"blacklisted_points\": [";
        for (size_t i = 0; i < g_blacklisted.size(); i++) o << (i ? ", " : "") << g_blacklisted[i];
        o << "]\n}\n";
        std::string tmp = g_file + ".tmp";
        { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << o.str(); }
        MoveFileExA(tmp.c_str(), g_file.c_str(), MOVEFILE_REPLACE_EXISTING);
        g_last_flush = now;
    }

    void Flush() {
        std::lock_guard<std::mutex> lock(g_mutex);
        WriteLocked();
    }

    // ---- run lifecycle ----

    void BeginRun(const RunInfo& info) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_active) WriteLocked();
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_t0);
        g_info = info;
        g_enemies.clear(); g_by_handle.clear(); g_waves.clear(); g_hits.clear(); g_samples.clear();
        g_events.clear(); g_blacklisted.clear();
        g_live = {};
        g_end_reason = "running";
        g_started = Stamp("%Y-%m-%dT%H:%M:%S");
        std::string dir = DataDir() + "\\EndlessMode_runs";
        CreateDirectoryA(dir.c_str(), nullptr);
        g_file = dir + "\\run_" + Stamp("%Y%m%d_%H%M%S") + ".json";
        g_active = true;
        WriteLocked();
        LOG("stats: run started -> %s", g_file.c_str());
    }

    void EndRun(const char* reason) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_active) return;
        g_end_reason = reason;
        g_active = false;
        WriteLocked();
        int cleared = 0;
        for (auto& w : g_waves) cleared += w.clear_t >= 0;
        std::string csv = DataDir() + "\\EndlessMode_runs\\runs.csv";
        bool fresh = GetFileAttributesA(csv.c_str()) == INVALID_FILE_ATTRIBUTES;
        std::ofstream f(csv, std::ios::app);
        if (fresh) f << "started,mode,seed,end_reason,duration_s,waves_started,waves_cleared,kills,hits_taken,damage_taken,hits_dealt,file\n";
        f << g_started << "," << g_info.mode << "," << g_info.seed << "," << reason << "," << (int)Now() << ","
          << g_waves.size() << "," << cleared << "," << g_live.kills << "," << g_live.hits_taken << ","
          << (int)g_live.damage_taken << "," << g_live.hits_dealt << "," << g_file.substr(g_file.find_last_of('\\') + 1) << "\n";
        LOG("stats: run ended (%s): %zu waves, %d kills, %d hits taken", reason, g_waves.size(), g_live.kills, g_live.hits_taken);
    }

    bool Active() { return g_active; }
    void SetHero(uint32_t handle) { g_hero = handle; }

    void WaveStart(int wave, const std::string& faction, const std::string& kind, int count, int max_alive,
                   float health_mult, const std::vector<std::string>& bosses) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_active) return;
        g_waves.push_back({ wave, faction, kind, bosses, count, max_alive, health_mult, Now() });
        g_live.wave = wave;
    }

    void WaveCleared(int wave) {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& w : g_waves) if (w.n == wave && w.clear_t < 0) w.clear_t = Now();
    }

    void EnemySpawned(uint32_t handle, const std::string& type, int wave, int point, float x, float y, float z,
                      bool boss, float start_health) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_active) return;
        g_by_handle[handle] = g_enemies.size();
        g_enemies.push_back({ handle, type, wave, point, x, y, z, start_health, Now(), -1, boss });
    }

    bool TakeKilledByEnemy(uintptr_t health_comp) {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_killed_by_enemy.erase(health_comp) > 0;
    }

    void EnemyKilled(uint32_t handle) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_by_handle.find(handle);
        if (it == g_by_handle.end() || g_enemies[it->second].death_t >= 0) return;
        Enemy& e = g_enemies[it->second];
        e.death_t = Now();
        g_live.kills++;
        for (auto& w : g_waves) if (w.n == e.wave) w.kills++;
    }

    void EnemyStuck(uint32_t handle, int point) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_by_handle.find(handle);
        if (it != g_by_handle.end()) { g_enemies[it->second].stuck = true; g_enemies[it->second].death_t = Now(); }
        g_events.push_back({ Now(), "stuck", std::to_string(point) });
    }

    void PointBlacklisted(int point) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_blacklisted.push_back(point);
        g_events.push_back({ Now(), "blacklist", std::to_string(point) });
    }

    void SpawnFailed(const std::string& type) { Event("spawn_failed", type); }

    void Event(const std::string& type, const std::string& detail) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_active) return;
        g_events.push_back({ Now(), type, detail });
    }

    void Sample(float x, float y, float z, float health, float max_health, int alive) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last_pos[0] = x; g_last_pos[1] = y; g_last_pos[2] = z;
        if (!g_active) return;
        g_samples.push_back({ Now(), x, y, z, health, max_health, alive });
        if (Now() - g_last_flush > 10.0) WriteLocked();
    }

    Live Current() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_live;
    }
}
