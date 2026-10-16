#include "score.h"

#include "log.h"
#include "paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace score {
    static const char* const kLetters[kRanks] = { "D", "C", "B", "A", "S", "SS", "SSS" };
    static const char* const kTitles[kRanks] = { "Puny Parker", "Human Spider", "Wall-Crawler", "Amazing", "Spectacular",
                                                  "Sensational", "Superior" };
    static const float kMult[kRanks] = { 1.0f, 1.2f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f };

    const char* RankLetter(int r) { return kLetters[std::clamp(r, 0, kRanks - 1)]; }
    const char* RankTitle(int r) { return kTitles[std::clamp(r, 0, kRanks - 1)]; }

    static int KillPoints(roster::Role r) {
        using R = roster::Role;
        switch (r) {
            case R::Melee: case R::Baton: return 100;
            case R::Pistol: return 120;
            case R::Rifle: return 150;
            case R::Shield: return 200;
            case R::Rocket: case R::Sniper: return 250;
            case R::Special: return 300;
            case R::Elite: return 400;
            case R::Heavy: return 500;
            default: return 100;
        }
    }
    static int KillMeter(roster::Role r) {   // style gained per kill, 15..40
        int p = KillPoints(r);
        return std::clamp(15 + (p - 100) / 16, 15, 40);
    }

    // Run state.
    static float g_meter = 0;
    static int g_base = 0, g_style = 0;           // raw kill points, extra from rank multipliers
    static int g_last_dealt = 0, g_last_taken = 0;
    static unsigned g_last_tick = 0, g_last_gain = 0, g_start = 0, g_last_hit_taken = 0;
    static int g_combo = 0, g_combo_bonus = 0;
    static unsigned g_combo_last = 0;
    static unsigned g_untouched_best = 0;
    static double g_rank_time[kRanks] = {};
    static roster::Role g_last_role = roster::Role::Count;
    static int g_role_streak = 0;
    static int g_best = 0;
    static bool g_active = false;

    static int RankOf(float m) { return std::clamp((int)(m / 100.0f), 0, kRanks - 1); }

    static void CloseCombo() {
        if (g_combo > 1) g_combo_bonus += std::min(2000, g_combo * g_combo / 2);
        g_combo = 0;
    }

    void Begin(int best_score) {
        g_meter = 0; g_base = g_style = 0;
        g_last_dealt = g_last_taken = 0;
        g_last_tick = g_last_gain = g_start = g_last_hit_taken = 0;
        g_combo = g_combo_bonus = 0; g_combo_last = 0;
        g_untouched_best = 0;
        for (auto& t : g_rank_time) t = 0;
        g_last_role = roster::Role::Count; g_role_streak = 0;
        g_best = best_score;
        g_active = true;
    }

    void Tick(unsigned now, int dealt, int taken, int level) {
        (void)level;
        if (!g_active) return;
        if (!g_start) { g_start = g_last_tick = g_last_gain = g_last_hit_taken = now; g_last_dealt = dealt; g_last_taken = taken; }
        float dt = std::min(2.0f, (now - g_last_tick) / 1000.0f);
        g_last_tick = now;
        g_rank_time[RankOf(g_meter)] += dt;

        int hits = std::max(0, dealt - g_last_dealt), got_hit = std::max(0, taken - g_last_taken);
        g_last_dealt = dealt; g_last_taken = taken;
        if (hits) {
            g_meter += 4.0f * hits;
            g_last_gain = now;
            if (!g_combo || now - g_combo_last > 3000) { CloseCombo(); g_combo = 0; }
            int before = g_combo;
            g_combo += hits;
            g_meter += 10.0f * (g_combo / 10 - before / 10);   // every 10 hits in a chain: +10
            g_combo_last = now;
        } else if (g_combo && now - g_combo_last > 3000) {
            CloseCombo();
        }
        if (got_hit) {
            g_meter -= 60.0f * got_hit;
            g_last_hit_taken = now;
            CloseCombo();
        }
        g_untouched_best = std::max(g_untouched_best, now - g_last_hit_taken);
        // Decay: after 2 s without dealing damage, 15 per second, a bit faster at high ranks.
        if (now - g_last_gain > 2000) g_meter -= (15.0f + 3.0f * RankOf(g_meter)) * dt;
        g_meter = std::clamp(g_meter, 0.0f, 699.0f);
    }

    void OnKill(roster::Role role) {
        if (!g_active) return;
        // Variety: the same role again and again earns half after 3 in a row.
        g_role_streak = role == g_last_role ? g_role_streak + 1 : 1;
        g_last_role = role;
        float variety = g_role_streak > 3 ? 0.5f : 1.0f;
        int rank = RankOf(g_meter);
        int base = KillPoints(role);
        int with_rank = (int)(base * kMult[rank] * variety);
        g_base += base;
        g_style += with_rank - base;
        g_meter = std::min(699.0f, g_meter + KillMeter(role) * variety);
        g_last_gain = g_last_tick;
    }

    int Score() { return g_base + g_style; }
    int Rank() { return RankOf(g_meter); }
    float Meter() { return g_meter; }

    static std::string BestFile() { return ScriptsDir() + "\\EndlessMode_horde_best.txt"; }
    static std::string HistoryFile() { return ScriptsDir() + "\\EndlessMode_horde_scores.csv"; }

    std::vector<Past> History() {
        std::vector<Past> out;
        std::ifstream in(HistoryFile());
        for (std::string line; std::getline(in, line); ) {
            Past p;
            char date[16] = {};
            int rank = 0;
            if (sscanf_s(line.c_str(), "%10[^,],%d,%d,%d,%d", date, (unsigned)sizeof(date), &p.total, &rank, &p.seconds, &p.kills) >= 3) {
                p.date = date;
                p.rank = std::clamp(rank, 0, kRanks - 1);
                out.push_back(p);
            }
        }
        return out;
    }

    Results CampaignEnd(int wave, int kills, int hits_dealt, int hits_taken) {
        Results r;
        r.campaign = true;
        r.wave = wave;
        (void)hits_dealt;
        // Score (user, 2026-10-16: getting hit must cost points): 10 per enemy, 100 per wave reached, minus 5 per hit taken.
        r.rows = { { "Enemy Score", kills * 10 }, { "Wave Bonus", wave * 100 }, { "Hits Taken", -5 * hits_taken } };
        for (auto& row : r.rows) r.total += row.value;
        if (r.total < 0) r.total = 0;
        std::string file = ScriptsDir() + "\\EndlessMode_campaign_best.txt";
        std::ifstream in(file);
        int prev = 0;
        in >> prev;
        r.best = std::max(prev, kills);
        r.new_best = kills > prev;
        if (r.new_best) std::ofstream(file, std::ios::trunc) << r.best << "\n";
        return r;
    }

    Results End(unsigned now, int kills) {
        Results r;
        if (!g_active) return r;
        Tick(now, g_last_dealt, g_last_taken, 0);
        CloseCombo();
        g_active = false;
        int seconds = g_start ? (int)((now - g_start) / 1000) : 0;

        // Four rows (user, 2026-10-15): raw kill points, what the style ranks added, combos, and survival time. Survival
        // is deliberately small (2 per second): dodging around shouldn't pay much. Horde has no levels (user).
        r.rows = { { "Enemy Score", g_base }, { "Style Bonus", g_style }, { "Combo Bonus", g_combo_bonus },
                   { "Survival Bonus", seconds * 2 } };
        for (auto& row : r.rows) r.total += row.value;
        std::ifstream in(BestFile());
        int prev = 0;
        in >> prev;
        r.best = std::max(prev, g_best);
        r.new_best = r.total > r.best;
        if (r.new_best) {
            r.best = r.total;
            std::ofstream(BestFile(), std::ios::trunc) << r.best << "\n";
        }
        // Final rank: the time-weighted average style rank, one up for a new best (never above SSS).
        double total_t = 0, w = 0;
        for (int i = 0; i < kRanks; i++) { total_t += g_rank_time[i]; w += g_rank_time[i] * i; }
        r.final_rank = total_t > 0 ? (int)std::lround(w / total_t) : 0;
        if (r.new_best) r.final_rank++;
        r.final_rank = std::clamp(r.final_rank, 0, kRanks - 1);
        {   // history for the dock records panel: date, total, final rank, seconds
            time_t t = time(nullptr);
            tm lt;
            localtime_s(&lt, &t);
            char date[16];
            strftime(date, sizeof(date), "%Y-%m-%d", &lt);
            std::ofstream(HistoryFile(), std::ios::app) << date << "," << r.total << "," << r.final_rank << "," << seconds << "," << kills << "\n";
        }
        LOG("score: run total %d (best %d%s), final rank %s - %s", r.total, r.best, r.new_best ? ", NEW BEST" : "",
            RankLetter(r.final_rank), RankTitle(r.final_rank));
        return r;
    }
}
