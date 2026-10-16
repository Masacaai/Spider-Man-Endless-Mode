#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Run telemetry. Everything a run produces goes to <scripts>\EndlessMode_runs\run_<date>_<time>.json (rewritten
// every few seconds while the run is live, so a crash still leaves data) plus one line in runs.csv.
// Thread-safe; the damage hooks call in from the game thread.
namespace stats {
    struct RunInfo {
        std::string arena;
        std::string mode;                 // "campaign", "riot", "faction:<id>", "boss:<name>", "stress", ...
        uint32_t seed = 0;
        bool engaged = true;
        std::vector<std::string> faction_order;
    };

    // Installs the hero/bot damage hooks (once). Returns false if HeroOnDamage could not be hooked.
    bool InstallHooks();

    void BeginRun(const RunInfo& info);
    void EndRun(const char* reason);      // "hero_down", "stopped", "exit", ...
    bool Active();

    void SetHero(uint32_t handle);
    void WaveStart(int wave, const std::string& faction, const std::string& kind, int count, int max_alive,
                   float health_mult, const std::vector<std::string>& bosses);
    void WaveCleared(int wave);
    void EnemySpawned(uint32_t handle, const std::string& type, int wave, int point, float x, float y, float z,
                      bool boss, float start_health);
    void EnemyKilled(uint32_t handle);
    // True (once) if the bot whose Health component is `health_comp` was finished off by another enemy, not the hero.
    bool TakeKilledByEnemy(uintptr_t health_comp);
    void EnemyStuck(uint32_t handle, int point);
    void PointBlacklisted(int point);
    void SpawnFailed(const std::string& type);
    void Event(const std::string& type, const std::string& detail);
    // Hero sample (called ~2x/s by the director).
    void Sample(float x, float y, float z, float health, float max_health, int alive);

    // Live counters for the HUD.
    struct Live { int wave = 0; int kills = 0; int hits_taken = 0; float damage_taken = 0; int hits_dealt = 0;
                   int infight_hits = 0; float infight_damage = 0; };   // enemies hurting each other
    Live Current();

    // Writes the current run file now (also done automatically).
    void Flush();
}
