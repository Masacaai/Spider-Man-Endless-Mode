#pragma once

#include <cstdint>
#include "score.h"
#include <functional>
#include <string>
#include <vector>

// Wave director: executes plan:: wave plans. Countdown -> wave -> intermission -> ... -> hero down / stopped.
// Runs on its own thread; game work goes through spawn:: (which uses the game-thread hook).
namespace director {
    struct HeroPos {
        bool ok; float x, y, z;
        bool has_health; float health;
        float fwd_x, fwd_z;   // facing (transform row 2), for spawning out of sight
        uint32_t handle;
        float max_health = 0;
        bool cam_ok = false;   // gameplay camera (FollowCamera actor): position and horizontal forward
        float cam_x = 0, cam_y = 0, cam_z = 0, cam_fx = 0, cam_fz = 0;
    };

    struct Options {
        std::string mode = "campaign";   // campaign | faction | bossrush | horde
        int horde_max = 80;              // cap on enemies on the field (stress test 2026-10-13: hang at ~165)
        int horde_level_ms = 60000;      // Horde: a level lasts at most this long (test: `run horde fast` = 15 s)
        bool god = false;                // test: keep the hero's health topped up
        int max_types = 44;              // unit-type budget of a run's faction pool (model memory; see plan.h)
        int body_pool = 150;             // live enemies + bodies left on the ground, one shared cap (user, 2026-10-14)
        std::string faction;             // faction mode: faction id
        bool shuffle = false;            // campaign: random faction order (first kept)
        uint32_t seed = 0;               // 0 = random
        int first_wave = 1;
        bool riot = false;
        int riot_max = 40;
        bool allow_unverified_bosses = false;   // benched bosses (ini [bosses] =0) stay out unless asked
        int countdown_s = 5;
        bool roguelike = true;           // campaign/faction: boons, curses, run loadout (docs/ROGUELIKE.md)
        bool has_first_spawn = false;    // the first wave of the run spawns here (around the corner) and runs in
        float first_spawn[3] = {};
    };

    void Start(const Options& opt, std::function<HeroPos()> hero);
    void Stop(const char* reason = "stopped");   // stops spawning and despawns everything the director spawned
    bool Running();
    void SetPaused(bool on);   // freezes the director's loops (spawns, checks) while the world is frozen by a prompt
    // Horde results of the run that just ended (score breakdown, best, final rank), once; false if none waiting.
    bool TakeResults(score::Results& out, int& level);
    std::vector<std::string> UnitsInPlay();   // goon types currently preloaded by the run (for `parade`)
    void LogStatus();
    void SetForceAggro(bool on);
    void SetRiot(int max_alive);   // 0 = off; applies to the next run (and the current one from the next wave)

    // Called once when a run ends (reason: "hero_down", "stopped", ...), from the director thread.
    void OnRunEnded(std::function<void(const std::string& reason)> cb);

    // Logs the plans of waves [from, to] for the given options (no spawning).
    void PreviewPlans(const Options& opt, int from, int to);
}
