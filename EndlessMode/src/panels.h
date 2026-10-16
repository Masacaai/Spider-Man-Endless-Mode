#pragma once

#include "gfx.h"

#include "score.h"

#include <string>
#include <vector>

// Our use of the game's activity panel (HUD element 0x1C0, object +0x6E8: the Taskmaster challenge panel, recorded
// 2026-10-14, docs/UI_HUNT.md) for Horde: a live score panel during the run, and the expanded results panel that
// stays on screen until the player dismisses it (the game's own "Continue").
namespace panels {
    // Live score on the left. Cheap to call every tick: only sent when the score/rank changed, at most once a second.
    void LiveScore(int score, int rank);
    // Results with the breakdown rows, total, best and final rank. Stays until Hide() (the portal loop calls it when
    // the player presses Continue).
    void Results(const score::Results& r, int level);
    // The activity panel as a dock/exit prompt (user, 2026-10-15: no notification box for prompts). It is the game's
    // hideout preview: title, a header with the best time and kills, and goals with progress (a goal with cur == max shows
    // as completed). The Rewards box stays the game's default, empty (user, 2026-10-15: a description there didn't work).
    // Its button (the game's RETRY, on our interact key) is relabelled.
    // Returns false if the panel can't be shown (the caller then falls back to a text prompt).
    struct Goal { std::string label; int cur, max; };
    bool Records(const std::string& title, int best_seconds, int best_kills, const std::vector<Goal>& goals,
                 const std::string& button);
    void HideRecords();
    bool RecordsUp();
    void Hide();           // both panels
    void Tick();           // call regularly: freezes the game once the results panel has animated in
    // Experiments (dev command 'gfx rtest'): show a hand-built results panelData as the results panel.
    bool ShowRaw(gfx::Node data);
    bool ResultsUp();      // the results panel is showing, waiting for the player
}
