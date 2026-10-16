#pragma once

#include "roster.h"

#include <array>
#include <memory>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

// Wave planning: pure functions of (config, wave number, rng), so the whole difficulty curve can be printed and
// tuned offline (tests/plan_offline.cpp). The director executes plans; it never decides balance on its own.
//
// Structure (campaign): the run is a sequence of chapters, one faction each, 4 waves per chapter:
//   chapter wave 1  "skirmish"  - cheap roles (melee, baton; pistols after the first cycle)
//   chapter wave 2  "assault"   - + pistols, rifles, shields
//   chapter wave 3  "full force"- + rockets, snipers, heavies, specials, some elites
//   chapter wave 4  "boss"      - boss wave; escalates over the run:
//        boss waves 1-2: a boss alone; 3-4: boss + goons; 5+: several bosses + goons.
// After every faction has had a chapter, the cycle repeats tougher (health and elite chance up, all roles from
// the start of each chapter).
namespace plan {
    using roster::Role;
    constexpr int kRoles = (int)Role::Count;

    constexpr int kMaxTypes = 44;     // default type budget for a run's faction pool (see BudgetedPool)

    enum class Mode { Campaign, Faction, BossRush, Classic, Horde };

    struct Config {
        Mode mode = Mode::Campaign;
        std::vector<const roster::Faction*> order;   // campaign order (Faction mode: one entry)
        bool riot = false;
        int riot_max = 40;
        bool allow_unverified_bosses = false;        // benched bosses stay out (ini [bosses])
        std::set<std::string> failed_bosses;         // bosses that misbehaved this run (skipped from now on)
        int waves_per_chapter = 4;
        int horde_max = 80;                          // cap on enemies on the field (stress test: hang at ~165)
    };

    struct WavePlan {
        int wave = 0, chapter = 0, cycle = 0, step = 0;   // step: 0..waves_per_chapter-1
        const roster::Faction* faction = nullptr;
        bool boss_wave = false;
        std::vector<const roster::Boss*> bosses;
        int count = 0;               // non-boss enemies in the wave
        int max_alive = 0;           // on the field at once (bosses included)
        float health_mult = 1.0f;    // goons
        float boss_health_mult = 1.0f;
        float elite_chance = 0.0f;   // chance a goon is drawn from the faction's Elite units instead
        std::array<float, kRoles> weights{};
        std::array<int, kRoles> caps{};   // max alive per role (readability: few snipers/rockets at once)
        std::string kind;            // "skirmish", "assault", "full force", "boss", "boss+goons", "multi-boss"
        std::string title;           // HUD banner
        std::shared_ptr<roster::Faction> mixed;      // Horde: the merged roster of all factions in play (owns faction)
    };

    // Builds the campaign order: the first faction plus kRunPool-1 others picked at random (roster order, or shuffled).
    std::vector<const roster::Faction*> CampaignOrder(bool shuffle, std::mt19937& rng, int max_types = kMaxTypes);

    // The game can only keep so many enemy models in memory while the prison is loaded: past roughly 40-50 held unit types
    // whatever loads last comes out as white cubes / floating heads and the game crashes (user's Horde run 2026-10-15: 39
    // types fine, 50 broke Olympus; parade at the dock handled 57). Pools are therefore chosen by a budget of unit types,
    // not by number of factions. `[run] max_types` in the ini overrides it.
    int UnitTypes(const roster::Faction& f);                       // distinct actor paths in a faction
    std::vector<const roster::Faction*> BudgetedPool(std::vector<const roster::Faction*> shuffled, int max_types, int max_factions = 5,
                                                     int min_factions = 3);

    WavePlan Make(const Config& cfg, int wave, std::mt19937& rng);

    // Horde mode (user design, 2026-10-13): no waves or bosses, a continuous stream that escalates by level. Level 1
    // is three factions; another joins every level (cfg.order, shuffled per run), at most kHordeWindow at once.
    WavePlan MakeHorde(const Config& cfg, int level);
    constexpr int kHordeWindow = 4;   // factions fighting at once
    constexpr int kRunPool = 5;       // factions a run draws from, Horde and Campaign (all held in memory all run)
    constexpr int kHordePool = kRunPool;
    std::vector<const roster::Faction*> HordeFactions(const Config& cfg, int level);

    // Picks one goon for a plan given how many of each role are alive. Returns nullptr if nothing fits the caps.
    const roster::Unit* PickUnit(const WavePlan& p, const std::array<int, kRoles>& alive_by_role, std::mt19937& rng);

    // One-line description for logs.
    std::string Describe(const WavePlan& p);
}
