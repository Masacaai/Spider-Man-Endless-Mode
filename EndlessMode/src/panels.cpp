#include "panels.h"

#include "gfx.h"
#include "hud.h"
#include "log.h"

#include <Windows.h>
#include <atomic>
#include <string>

namespace panels {
    using gfx::Node;
    constexpr unsigned kEl = 0x1C0, kObj = 0x6E8;       // HUD root offset / ActionScript object offset (uilog "at 1c0:6e8")
    constexpr const char* kIcon = "icon_map_combat_challenge";

    void HideRecords();
    static std::atomic<bool> g_results_up = false;
    static int g_last_score = -1, g_last_rank = -1;
    static DWORD g_last_sent = 0;

    static std::string RankLabel(int rank) {
        std::string s = std::string(score::RankLetter(rank)) + " - " + score::RankTitle(rank);
        for (auto& c : s) c = (char)toupper((unsigned char)c);
        return s;
    }

    static Node Wrap(int panel_type, Node data) {
        return Node::Obj({ { "activity", Node::Of(1) }, { "panelType", Node::Of(panel_type) }, { "panelData", std::move(data) } });
    }

    void LiveScore(int s, int rank) {
        DWORD now = GetTickCount();
        if (g_results_up || (s == g_last_score && rank == g_last_rank) || (g_last_sent && now - g_last_sent < 1000)) return;
        g_last_score = s; g_last_rank = rank; g_last_sent = now;
        Node data = Node::Obj({ { "type", Node::Of(2) }, { "label", Node::Of(RankLabel(rank)) }, { "icon", Node::Of(kIcon) },
                                { "score", Node::Of(s) }, { "scoreItems", Node::Arr() } });
        gfx::Invoke(kEl, kObj, "Refresh", { Wrap(1, std::move(data)) });
    }

    void Results(const score::Results& r, int level) {
        // Tier banners: thresholds as the game does (rewardTiers = three scores) and one reward entry per tier, shaped like
        // the game's token rewards. Whether the panel needs these to show the summary rows is what this tests.
        Node tiers = Node::Arr();
        Node tier_rewards = Node::Arr();
        const char* const names[3] = { "Amazing", "Spectacular", "Superior" };
        Node awarded = Node::Arr();
        for (int i = 0; i < 3; i++) {
            int threshold = score::kTierScores[i];   // same tiers in both modes (user 2026-10-16)
            // Tier thresholds as numbers (double), like the game's own floats; the tiers the player reached are also listed as
            // awarded (user, 2026-10-16: with a tier reached the buttons were missing and the tier labels said 500).
            tiers.Push(Node::Of((double)threshold));
            Node entry = Node::Obj({ { "label", Node::Of(names[i]) }, { "icon", Node::Of("resource_challenge_token") },
                                     { "value", Node::Of(i + 1) }, { "id", Node::U(0) }, { "totalCount", Node::U(0) } });
            if (r.total >= threshold) awarded.Push(std::move(entry));
            tier_rewards.Push(Node::Obj({ { "label", Node::Of(names[i]) }, { "icon", Node::Of("resource_challenge_token") },
                                          { "value", Node::Of(i + 1) }, { "id", Node::U(0) }, { "totalCount", Node::U(0) } }));
        }
        Node rows = Node::Arr();
        for (auto& row : r.rows) rows.Push(Node::Obj({ { "label", Node::Of(row.label) }, { "value", Node::Of(row.value) } }));
        // The panel title cuts off at about 30 characters.
        (void)level;
        std::string label = r.campaign ? "CAMPAIGN - WAVE " + std::to_string(r.wave) : "HORDE - " + RankLabel(r.final_rank);
        Node data = Node::Obj({ { "label", Node::Of(label) }, { "icon", Node::Of(kIcon) },
                                { "score", Node::U((unsigned)r.total) }, { "highScore", Node::U((unsigned)r.best) },
                                { "summaryData", rows }, { "rewards", tier_rewards }, { "noRewards", Node::Of(false) },
                                { "awardedRewards", awarded }, { "rewardTiers", tiers } });
        if (!gfx::Invoke(kEl, kObj, "Refresh", { Wrap(2, std::move(data)) })) {
            // Panel unavailable: the same numbers in the notification box.
            std::string t = "RUN RESULTS - " + RankLabel(r.final_rank) + "\nScore " + std::to_string(r.total) +
                            (r.new_best ? " (NEW BEST)" : " (best " + std::to_string(r.best) + ")");
            for (auto& row : r.rows) t += "\n" + row.label + ": " + std::to_string(row.value);
            hud::Show(t, 15.0f);
            return;
        }
        g_results_up = true;
        LOG("panels: results shown (%d points, %s)", r.total, RankLabel(r.final_rank).c_str());
    }

    static std::atomic<bool> g_records_up = false;

    // The panel's fixed texts live in a per-panel object captured at HUD load (gfx::InstallTextCapture): this one has the
    // hideout preview's keys (Rewards, Retry) and not the completed panel's.
    static void Retitle(const std::string& button) {
        int b = gfx::SetText("CHALLENGE_GEN_BTN_TRYAGAIN", button, "HUD_TASK_COMPLETE_REWARDS", "HUD_TASK_COMPLETED");
        static bool logged = false;
        if (!logged) { logged = true; LOG("panels: relabelled %d button(s)", b); }
    }

    bool Records(const std::string& title, int best_seconds, int best_kills, const std::vector<Goal>& goals,
                 const std::string& button) {
        Retitle(button);
        Node subs = Node::Arr();
        for (auto& g : goals)
            subs.Push(Node::Obj({ { "label", Node::Of(g.label) }, { "currentCount", Node::Of(g.cur) }, { "maxCount", Node::Of(g.max) },
                                  { "reward", Node::Arr() } }));
        Node data = Node::Obj({ { "label", Node::Of(title) }, { "icon", Node::Of(kIcon) }, { "time", Node::Of((double)best_seconds) },
                                { "enemiesDefeated", Node::Of(best_kills) }, { "subObjectives", subs }, { "rewards", Node::Arr() } });
        bool ok = gfx::Invoke(kEl, kObj, "Refresh", { Node::Obj({ { "activity", Node::Of(3) }, { "panelType", Node::Of(0) },
                                                                  { "panelData", std::move(data) } }) });
        if (ok) g_records_up = true;
        else HideRecords();
        return ok;
    }

    void HideRecords() {
        if (!g_records_up.exchange(false)) return;
        // The dock panel is the hideout type (activity 3), not the challenge type Wrap() builds (activity 1): hiding the wrong
        // one left it on screen after travelling (2026-10-15).
        gfx::Invoke(kEl, kObj, "HideActivity", { Node::Obj({ { "activity", Node::Of(3) }, { "panelType", Node::Of(0) } }) });
        // Put the game's own button text back, so a real hideout isn't left saying START.
        gfx::RestoreText("CHALLENGE_GEN_BTN_TRYAGAIN", "HUD_TASK_COMPLETE_REWARDS", "HUD_TASK_COMPLETED");
    }

    bool RecordsUp() { return g_records_up; }

    void Hide() {
        bool was = g_results_up.exchange(false);
        g_last_score = g_last_rank = -1; g_last_sent = 0;
        gfx::Invoke(kEl, kObj, "HideActivity", { Wrap(was ? 2 : 1, Node::Obj()) });
        if (was) {
            LOG("panels: results dismissed");
        }
    }

    bool ResultsUp() { return g_results_up; }

    bool ShowRaw(Node data) {
        if (!gfx::Invoke(kEl, kObj, "Refresh", { Wrap(2, std::move(data)) })) return false;
        g_results_up = true;
        return true;
    }

    // (The results panel used to freeze the game once it had animated in; dropped again, user 2026-10-16.)
    void Tick() {}

}
