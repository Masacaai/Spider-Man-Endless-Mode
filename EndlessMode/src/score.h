#pragma once

#include "roster.h"

#include <string>
#include <vector>

// Horde scoring and style ranks (design: docs/SCORING.md, user 2026-10-14). A style meter (0..700, one rank per 100)
// rises with hits and kills, drops when the player is hit and decays when they stop fighting; every kill is worth
// role points times the rank multiplier. Pure logic: the director feeds it events, the panels read it.
namespace score {
    constexpr int kRanks = 7;
    const char* RankLetter(int rank);   // "D" .. "SSS"
    const char* RankTitle(int rank);    // "Puny Parker" .. "Superior"

    void Begin(int best_score);                              // new run
    void Tick(unsigned now_ms, int hits_dealt, int hits_taken, int level);   // totals so far, every director tick
    void OnKill(roster::Role role);                          // a counted kill

    int Score();                                             // running total (kills x rank multiplier)
    int Rank();                                              // current rank 0..6
    float Meter();                                           // 0..700

    struct Row { std::string label; int value; };
    struct Results {
        int total = 0, best = 0;
        bool new_best = false;
        int final_rank = 0;
        bool campaign = false;          // Campaign summary: score = 10/enemy + 100/wave - 5/hit taken, label carries the wave
        int wave = 0;
        std::vector<Row> rows;
    };
    // Score needed for the results panel's three tier banners (it prints AMAZING / SPECTACULAR / ULTIMATE LEVEL).
    constexpr int kTierScores[3] = { 2000, 5000, 10000 };   // both modes (Horde used 8000/20000/40000 before 2026-10-16)
    Results CampaignEnd(int wave, int kills, int hits_dealt, int hits_taken);   // summary of a Campaign run (best kills kept in a file)
    Results End(unsigned now_ms, int kills);                 // kills go into the history file

    // Finished Horde runs, oldest first (scripts\EndlessMode_horde_scores.csv).
    struct Past { std::string date; int total = 0, rank = 0, seconds = 0, kills = 0; };
    std::vector<Past> History();                 // closes the run, persists the best score
}
