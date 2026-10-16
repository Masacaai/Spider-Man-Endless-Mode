#pragma once

#include <string>
#include <vector>

// Enemy factions and bosses. Paths are .actor assets (all present in the 4.0630 toc; base health and bot class
// mined with tools/roster/mine_actors.py, see tools/roster/enemies.txt).
namespace roster {
    // What a unit does in a fight; drives the wave mix (cheap roles first, specialists later).
    enum class Role { Melee, Baton, Pistol, Rifle, Shield, Rocket, Sniper, Heavy, Special, Elite, Count };
    const char* RoleName(Role r);

    struct Unit {
        const char* path;
        Role role;
        bool calm = false;      // spawn without the engaged flag (classes that crash engaged; see docs/BUGS.md)
    };

    struct Boss {
        const char* path;
        const char* name;       // shown in the HUD
        int base_health;        // from the actor file (0 = not set there)
        bool verified;          // fought standalone in game; unverified bosses are only used when allowed
        bool calm = false;      // spawn without the engaged flag
        const char* head = "";  // separate head actor (must be loaded too, or the boss spawns headless and crashes)
        int min_cycle = 0;      // harder variants only appear from this cycle on
        float gate = 0.0f;      // health fraction where it stops taking damage outside its mission (0 = the 1 HP floor)
        float health_bonus = 1.0f;   // extra max-health multiplier (so the fight up to the gate is long enough)
    };

    struct Faction {
        const char* id;         // short id for commands and stats
        const char* name;       // shown in the HUD
        std::vector<Unit> units;
        std::vector<const Boss*> bosses;
    };

    const std::vector<Faction>& Factions();       // in campaign order
    const std::vector<Boss>& Bosses();
    const Faction* FindFaction(const std::string& id);
    const Boss* FindBoss(const std::string& name_or_path);

    // Marks a boss verified/unverified at runtime (persisted by the caller).
    void SetVerified(const std::string& name_or_path, bool v);

    // Short display name for an actor path ("enm_prison_meleethug").
    std::string ShortName(const char* path);
}
