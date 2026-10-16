#pragma once

#include <cstdint>

// Finding the game camera by scanning memory (the camera object's address is not known yet).
// The camera position is a float3 a few metres from the hero that changes when the camera is rotated while the hero
// stands still. Workflow:
//   camscan start         every float3 in writable private memory 1-8 m from the hero
//   camscan moved         keep those that moved > 0.5 m since the last step (rotate the camera first, don't walk)
//   camscan same          keep those that didn't move (leave the camera alone first)
//   camscan list          print the survivors with the 64 bytes around each
namespace camscan {
    void Command(const char* sub, const float hero[3]);
}

// Generic 8-byte value scan (e.g. which memory holds the equipped suit power's asset id):
//   vscan find <hex64>    every 8-aligned address in writable private memory holding the value
//   vscan keep <hex64>    keep the candidates that now hold this value (change state in game in between)
//   vscan list            print survivors with 64 bytes of context
namespace vscan {
    void Command(const char* sub, uint64_t value);
}
