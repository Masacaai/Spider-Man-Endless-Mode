#pragma once

#include "rykers_points.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Arena loading: stream in an instanced interior's .zone files and teleport there.
// Same mechanism as jedijosh920's Mod Menu "Enter Peter's Apartment": LoadAssetByHash(&ZoneAssetManager, hash).
namespace arena {
    // Loads one zone by path; blocks until loaded or timeout. Returns true if loaded.
    bool LoadZone(const std::string& path);

    // Writes the hero position on the game thread.
    bool Teleport(float x, float y, float z);

    struct Point2 { float x, z; };

    // Playable area of an arena: enemies spawn at random points inside it, at ground_y (+ a small drop).
    struct Bounds {
        const char* name;
        float tp[3];                // where the hero is placed on entry (and the exit pad)
        float min_x, max_x, min_z, max_z;
        float ground_y;
        const SpawnPoint* points = nullptr;   // designer-placed walkable points; preferred over random bounds
        size_t point_count = 0;
        // Endless Mode flow (2026-10-13, points recorded in game, recon/portal_points.txt):
        const char* title = "";               // HUD name
        const char* mode = "campaign";        // run mode started here: "campaign" (horde) or "bossrush"
        float start[3] = {};                  // walking within start_radius of this starts the run (0 = on arrival)
        float start_radius = 0;
        bool has_first_spawn = false;         // wave 1 comes from here (they run to the hero)
        float first_spawn[3] = {};
        const Point2* fence = nullptr;        // invisible walls: polygon (x, z); anyone outside or below fall_y is put back
        size_t fence_count = 0;
        float fall_y = -1.0e9f;
        float rescue[3] = {};                 // where people are put back
        float exit[3] = {};                   // the exit circle (the hero arrives a few metres past it); 0 = at tp
        // Invisible wall at the start line (2026-10-13): during a run enemies can't cross back over it (they're pushed
        // back to it), and a hero who walks out past it for a few seconds ends the run. Its normal points from the
        // exit circle toward the start point.
        bool wall = false;
    };

    const Bounds* Find(const std::string& name);
    // Makes an arena current without teleporting (the run starts once the hero walks in).
    bool Activate(const std::string& name);
    // True if (x, z) is inside the fence polygon grown by `margin` metres (no fence: always true).
    bool InsideFence(const Bounds& a, float x, float z, float margin);
    // Signed distance past the start-line wall (positive = arena side); +1e9 if the arena has no wall.
    float WallSide(const Bounds& a, float x, float z);
    // Moves any entity (bots included) by writing its transform position (game thread).
    bool MoveEntity(uintptr_t entity, float x, float y, float z);
    // "rykers": Ryker's Island prison yard (open world). "raft": the prison-break interior
    // (levels/i20_city/instance/Prison_Interior; dark, has an invisible wall). full = also mission gameplay zones.
    bool Enter(const std::string& name, bool full);

    const Bounds* Current();   // arena the director spawns in, or null (ring around the hero)
    void Clear();
}
