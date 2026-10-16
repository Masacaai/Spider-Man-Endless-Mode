#include "portal.h"

#include "arena.h"
#include "hud.h"
#include "log.h"
#include "paths.h"
#include "gfx.h"
#include "panels.h"
#include "timescale.h"
#include "score.h"
#include "input.h"
#include "runloadout.h"
#include "psinput.h"
#include "spawn.h"
#include "stats.h"

#include <algorithm>

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace portal {
    struct Pad { bool set = false; float x = 0, y = 0, z = 0; };

    struct Config {
        bool enabled = true;
        Pad dock, raft_dock;
        float radius = 2.5f;
        float hold_s = 3.0f;
        int key_enter = VK_F8, key_leave = VK_F9;
        director::Options run;
    };

    static Config g_cfg;
    static std::mutex g_cfg_mutex;
    static std::thread g_thread;
    static std::atomic<bool> g_running = false;
    static std::function<director::HeroPos()> g_hero;
    static std::atomic<bool> g_pending_return = false;
    static std::atomic<bool> g_portal_run = false;   // the current run was started by the portal (pads/hotkey)
    static std::string g_end_reason;

    // ---- ini ----

    static std::string Ini() { return ScriptsDir() + "\\EndlessMode.ini"; }

    static std::string ReadStr(const char* sec, const char* key, const char* def) {
        char buf[256];
        GetPrivateProfileStringA(sec, key, def, buf, sizeof(buf), Ini().c_str());
        return buf;
    }
    static float ReadF(const char* sec, const char* key, float def) {
        std::string s = ReadStr(sec, key, "");
        return s.empty() ? def : (float)atof(s.c_str());
    }
    static int ReadI(const char* sec, const char* key, int def) {
        std::string s = ReadStr(sec, key, "");
        return s.empty() ? def : (int)strtol(s.c_str(), nullptr, 0);
    }
    static void WriteStr(const char* sec, const char* key, const std::string& v) {
        WritePrivateProfileStringA(sec, key, v.c_str(), Ini().c_str());
    }
    static void WriteF(const char* sec, const char* key, float v) {
        char b[32];
        snprintf(b, sizeof(b), "%.2f", v);
        WriteStr(sec, key, b);
    }

    static Pad ReadPad(const char* prefix) {
        Pad p;
        std::string k = prefix;
        p.set = ReadI("portal", (k + "_set").c_str(), 0) != 0;
        p.x = ReadF("portal", (k + "_x").c_str(), 0);
        p.y = ReadF("portal", (k + "_y").c_str(), 0);
        p.z = ReadF("portal", (k + "_z").c_str(), 0);
        return p;
    }
    static void WritePad(const char* prefix, const Pad& p) {
        std::string k = prefix;
        WriteStr("portal", (k + "_set").c_str(), p.set ? "1" : "0");
        WriteF("portal", (k + "_x").c_str(), p.x);
        WriteF("portal", (k + "_y").c_str(), p.y);
        WriteF("portal", (k + "_z").c_str(), p.z);
    }

    static void Load() {
        Config c;
        c.enabled = ReadI("portal", "enabled", 1) != 0;
        c.dock = ReadPad("dock");
        c.raft_dock = ReadPad("raft_dock");
        c.radius = ReadF("portal", "radius", 2.5f);
        c.hold_s = ReadF("portal", "hold_seconds", 3.0f);
        c.key_enter = ReadI("portal", "key_enter", VK_F8);
        c.key_leave = ReadI("portal", "key_leave", VK_F9);
        c.run.mode = ReadStr("run", "mode", "campaign");
        c.run.faction = ReadStr("run", "faction", "prisoners");
        c.run.shuffle = ReadI("run", "shuffle", 0) != 0;
        c.run.riot = ReadI("run", "riot", 0) != 0;
        c.run.riot_max = ReadI("run", "riot_max", 40);
        c.run.first_wave = ReadI("run", "first_wave", 1);
        c.run.countdown_s = ReadI("run", "countdown", 5);
        c.run.allow_unverified_bosses = ReadI("run", "allow_unverified_bosses", 0) != 0;
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        g_cfg = c;
    }

    // Writes a commented default file once so the options are discoverable.
    static void WriteDefaultsIfMissing() {
        if (GetFileAttributesA(Ini().c_str()) != INVALID_FILE_ATTRIBUTES) return;
        FILE* f = nullptr;
        if (fopen_s(&f, Ini().c_str(), "w") || !f) return;
        fputs("; Endless Mode settings. Pads are set in game with the 'portal setdock' / 'portal setexit' commands.\n"
              "[portal]\nenabled=1\nradius=2.5\nhold_seconds=3\n; virtual-key codes: 0x77 = F8, 0x78 = F9\nkey_enter=0x77\nkey_leave=0x78\n"
              "dock_set=0\nexit_set=0\n\n"
              "[run]\n; mode: campaign | faction | bossrush\nmode=campaign\n; faction mode: street fisk demons prisoners sable hammerhead tombstone taskmaster\n"
              "faction=prisoners\n; shuffle: random faction order after the first (campaign)\nshuffle=0\nriot=0\nriot_max=40\nfirst_wave=1\ncountdown=5\n"
              "allow_unverified_bosses=0\n", f);
        fclose(f);
    }

    // ---- venues: a Manhattan dock and the arena it leads to ----

    struct Venue {
        const char* dock_key;    // ini prefix of its dock pad
        const char* arena;       // arena::Find name
        const char* mode;        // run mode started there ("" = [run] mode from the ini)
        const char* prompt;      // shown on the dock pad, followed by "Press <interact button>"
    };
    // 2026-10-13: both docks lead to Ryker's. The first runs Campaign (waves, factions, bosses; modifiers later), the
    // second Horde (an endless, escalating, mixed-faction stream with no bosses). The Raft roof arena was scrapped.
    static const Venue kVenues[] = {
        { "dock", "rykers", "", "Ryker's Island - Campaign." },
        { "raft_dock", "rykers", "horde", "Ryker's Island - Horde Mode." },
    };    constexpr int kVenueCount = (int)std::size(kVenues);

    static std::atomic<int> g_armed = -1;   // venue whose arena the hero was sent to (run starts on walking in)
    static std::atomic<int> g_run_venue = -1;

    // The dock the hero last left from, kept in the ini ([portal] last_venue) so a game loaded at Ryker's still knows
    // which run to start and which dock the exit circle leads back to (user, 2026-10-14).
    static std::atomic<int> g_last_venue = -1;   // cache of the ini value
    static int LastVenue() {
        if (g_last_venue < 0) g_last_venue = std::clamp(ReadI("portal", "last_venue", 0), 0, kVenueCount - 1);
        return g_last_venue;
    }
    static void SetLastVenue(int v) {
        if (v == LastVenue()) return;
        g_last_venue = v;
        WriteStr("portal", "last_venue", std::to_string(v));
    }

    static Pad VenueDock(int v) {
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        return v == 0 ? g_cfg.dock : g_cfg.raft_dock;
    }

    // Teleport behind a fade to black, like fast travel.
    static bool FadeTeleport(float x, float y, float z) {
        bool faded = hud::FadeOut(0.5f);
        if (faded) Sleep(650);
        bool ok = arena::Teleport(x, y, z);
        if (faded) {
            Sleep(2000);  // let the destination stream in (and Spidey appear) behind the black screen
            hud::FadeIn(0.6f);
        }
        return ok;
    }

    static bool TeleportTo(const Pad& p) {
        return FadeTeleport(p.x, p.y + 0.5f, p.z);
    }

    // Dock -> arena arrival point. The run itself starts when the hero walks into the start area.
    static void Go(int v) {
        if (director::Running()) { LOG("portal: a run is already going"); return; }
        const arena::Bounds* a = arena::Find(kVenues[v].arena);
        if (!a) return;
        Load();
        LOG("portal: going to %s", a->title);
        if (!FadeTeleport(a->tp[0], a->tp[1], a->tp[2])) return;
        g_armed = v;
        SetLastVenue(v);
        hud::Show(std::string(a->title) + (*kVenues[v].mode ? " - Horde Mode: walk into the yard when you're ready"
                                                             : " - walk into the yard to begin"), 5.0f);
    }

    static void StartRun(int v) {
        const arena::Bounds* a = arena::Find(kVenues[v].arena);
        if (!a || director::Running()) return;
        director::Options run;
        { std::lock_guard<std::mutex> lock(g_cfg_mutex); run = g_cfg.run; }
        if (*kVenues[v].mode) run.mode = kVenues[v].mode;
        run.horde_max = ReadI("run", "horde_max", 40);
        run.max_types = std::clamp(ReadI("run", "max_types", 44), 20, 80);
        run.body_pool = std::clamp(ReadI("run", "body_pool", 150), 40, 200);
        if (std::string(a->mode) == "bossrush") {
            run.mode = "bossrush";
            run.riot = false;
            // Story bosses can hang the game when spawned outside their mission, so the Raft only uses bosses that
            // were checked one by one (boss verify <name> 1). Override with [run] raft_unverified=1.
            run.allow_unverified_bosses = ReadI("run", "raft_unverified", 0) != 0;
        }
        if (a->has_first_spawn) {
            run.has_first_spawn = true;
            for (int i = 0; i < 3; i++) run.first_spawn[i] = a->first_spawn[i];
        }
        run.countdown_s = 3;
        arena::Activate(a->name);
        g_run_venue = v;
        g_portal_run = true;
        LOG("portal: starting %s at %s", run.mode.c_str(), a->name);
        director::Start(run, g_hero);
    }

    void Enter() { Go(0); }

    void Leave() {
        if (director::Running()) director::Stop("exit");
        int v = g_armed >= 0 ? g_armed.load() : g_run_venue >= 0 ? g_run_venue.load() : LastVenue();
        g_armed = -1;
        g_pending_return = false;
        arena::Clear();
        Pad dock = VenueDock(v);
        if (!dock.set) { LOG("portal: no dock set for %s", kVenues[v].arena); hud::Show("No dock set yet", 3.0f); return; }
        // Land a few metres inland of the circle (outside its prompt radius) so its prompt doesn't pop up at once
        // (user, 2026-10-13). Both docks sit on Manhattan's east shore facing Ryker's, so inland is -x.
        Pad landing = dock;
        landing.x -= 4.0f;
        TeleportTo(landing);
        LOG("portal: returned to the %s dock", kVenues[v].arena);
    }

    bool SetDockHere() {
        director::HeroPos h = g_hero ? g_hero() : director::HeroPos{};
        if (!h.ok) return false;
        WritePad("dock", Pad{ true, h.x, h.y, h.z });
        Load();
        LOG("portal: Ryker's dock set at (%.2f, %.2f, %.2f)", h.x, h.y, h.z);
        hud::Show("Ryker's dock set here", 3.0f);
        return true;
    }

    // ---- run records (shown on the Manhattan dock circles) ----

    struct RunRow { std::string date, mode, end; int waves_started = 0, waves_cleared = 0, kills = 0, secs = 0; };

    // EndlessMode_runs\runs.csv (stats.cpp): started,mode,seed,end_reason,duration_s,waves_started,waves_cleared,
    // kills,hits_taken,damage_taken,hits_dealt,file
    static std::vector<RunRow> ReadRuns() {
        std::vector<RunRow> rows;
        std::ifstream in(ScriptsDir() + "\\EndlessMode_runs\\runs.csv");
        std::string line;
        std::getline(in, line);   // header
        while (std::getline(in, line)) {
            std::vector<std::string> f;
            std::stringstream ss(line);
            for (std::string x; std::getline(ss, x, ','); ) f.push_back(x);
            if (f.size() < 8) continue;
            RunRow r;
            r.date = f[0].substr(0, 10);
            r.mode = f[1];
            r.end = f[3];
            r.secs = atoi(f[4].c_str());
            r.waves_started = atoi(f[5].c_str());
            r.waves_cleared = atoi(f[6].c_str());
            r.kills = atoi(f[7].c_str());
            if (r.secs < 20 && r.kills == 0) continue;   // aborted / test starts
            rows.push_back(r);
        }
        return rows;
    }

    static bool IsHorde(const RunRow& r) { return r.mode.rfind("horde", 0) == 0; }

    static std::string Describe(const RunRow& r) {
        char buf[128];
        if (IsHorde(r)) snprintf(buf, sizeof(buf), "%d kills", r.kills);
        else snprintf(buf, sizeof(buf), "Campaign wave %d, %d kills", std::max(r.waves_cleared + (r.end == "hero_down" ? 1 : 0), 1), r.kills);
        return buf;
    }

    struct BoardData { std::string title; int best_seconds = 0, best_kills = 0; std::vector<panels::Goal> goals; };

    static std::string Pretty(int n) {
        std::string s = std::to_string(n);
        for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ",");
        return s;
    }

    // What the dock panel shows for a mode (user, 2026-10-15): the best run only in the header (time and kills), real goals
    // with progress drawn from it, and the description in the retitled Rewards box. No lists of past runs.
    static BoardData BoardFor(int venue) {
        BoardData b;
        bool horde = *kVenues[venue].mode != 0;
        b.title = std::string("Ryker's Island - ") + (horde ? "Horde Mode" : "Campaign");
        if (horde) {
            auto hist = score::History();
            const score::Past* best = nullptr;
            int best_rank = 0;
            for (auto& p : hist) {
                if (!best || p.total > best->total) best = &p;
                best_rank = std::max(best_rank, p.rank);
            }
            int best_total = best ? best->total : 0;
            b.best_seconds = best ? best->seconds : 0;
            b.best_kills = best ? best->kills : 0;
            // Score tiers: the highest one reached shows as completed, then the next one with progress.
            std::vector<int> tiers = { 8000, 20000, 40000, 60000, 80000, 100000 };
            int done = 0, goal = tiers.back();
            for (int t : tiers) { if (t <= best_total) done = t; else { goal = t; break; } }
            if (best_total >= tiers.back()) { done = tiers.back(); goal = done + 40000 * ((best_total - done) / 40000 + 1); }
            // Two rows only (user, 2026-10-15): one completed (the highest score tier reached) and one to go. With nothing
            // completed yet, the second row is the next rank instead.
            if (done) b.goals.push_back({ "Score " + Pretty(done) + " points in one run", done, done });
            b.goals.push_back({ "Score " + Pretty(goal) + " points in one run", best_total, goal });
            if (!done && best_rank < score::kRanks - 1) {
                int target = std::max(best_rank + 1, 3);
                b.goals.push_back({ std::string("Reach ") + score::RankLetter(target) + " rank - " + score::RankTitle(target), best_rank, target });
            }
            return b;
        }
        auto rows = ReadRuns();
        int best_wave = 0, best_kills = 0, best_secs = 0;
        for (auto& r : rows) {
            if (r.mode.rfind("campaign", 0) != 0) continue;
            int wave = std::max(r.waves_cleared + (r.end == "hero_down" ? 1 : 0), 1);
            if (wave > best_wave || (wave == best_wave && r.kills > best_kills)) { best_secs = r.secs; }
            best_wave = std::max(best_wave, wave);
            best_kills = std::max(best_kills, r.kills);
        }
        b.best_seconds = best_secs;
        b.best_kills = best_kills;
        // Two rows only (user, 2026-10-15): one completed (the last wave milestone, else the last kills milestone) and one to
        // go (the next wave milestone). With nothing completed yet, the second row is the next kills milestone.
        int wave_done = best_wave / 5 * 5, wave_goal = wave_done + 5, kill_done = best_kills / 100 * 100, kill_goal = kill_done + 100;
        if (wave_done) b.goals.push_back({ "Reach wave " + std::to_string(wave_done), wave_done, wave_done });
        else if (kill_done) b.goals.push_back({ "Defeat " + std::to_string(kill_done) + " enemies in one run", kill_done, kill_done });
        b.goals.push_back({ "Reach wave " + std::to_string(wave_goal), best_wave, wave_goal });
        if (b.goals.size() < 2) b.goals.push_back({ "Defeat " + std::to_string(kill_goal) + " enemies in one run", best_kills, kill_goal });
        return b;
    }

    bool SetExitHere() {
        director::HeroPos h = g_hero ? g_hero() : director::HeroPos{};
        if (!h.ok) return false;
        WritePad("raft_dock", Pad{ true, h.x, h.y, h.z });
        Load();
        LOG("portal: Raft dock set at (%.2f, %.2f, %.2f)", h.x, h.y, h.z);
        hud::Show("Raft dock set here", 3.0f);
        return true;
    }

    void Enable(bool on) {
        WriteStr("portal", "enabled", on ? "1" : "0");
        Load();
        LOG("portal: %s", on ? "enabled" : "disabled");
    }

    void LogConfig() {
        Load();
        std::lock_guard<std::mutex> lock(g_cfg_mutex);
        LOG("portal: enabled=%d rykers dock=%s (%.1f, %.1f, %.1f) raft dock=%s (%.1f, %.1f, %.1f) radius=%.1f keys=0x%x/0x%x",
            g_cfg.enabled, g_cfg.dock.set ? "set" : "unset", g_cfg.dock.x, g_cfg.dock.y, g_cfg.dock.z,
            g_cfg.raft_dock.set ? "set" : "unset", g_cfg.raft_dock.x, g_cfg.raft_dock.y, g_cfg.raft_dock.z, g_cfg.radius,
            g_cfg.key_enter, g_cfg.key_leave);
        LOG("portal: run mode=%s faction=%s shuffle=%d riot=%d/%d first_wave=%d", g_cfg.run.mode.c_str(),
            g_cfg.run.faction.c_str(), g_cfg.run.shuffle, g_cfg.run.riot, g_cfg.run.riot_max, g_cfg.run.first_wave);
    }
    // ---- loop ----

    static bool GameFocused() {
        HWND w = GetForegroundWindow();
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        return pid == GetCurrentProcessId();
    }

    // The game's replay reticle (blue ground circle that marks replayable activities), spawned on a pad while the
    // hero is within kMarkerShow m and removed beyond kMarkerHide m.
    static const char* const kReticle = "visualeffect\\mission\\mis_ui_base_replayreticle\\mis_ui_base_replayreticle.actor";
    constexpr float kMarkerShow = 120.0f, kMarkerHide = 160.0f;
    struct Marker { uintptr_t entity = 0; float x = 0, y = 0, z = 0; };

    static void UpdateMarker(Marker& m, const Pad& p, const director::HeroPos& h) {
        if (m.entity && (!p.set || m.x != p.x || m.z != p.z)) {   // pad moved or cleared
            spawn::DespawnActor(m.entity);
            m.entity = 0;
        }
        if (!p.set) return;
        float dx = h.x - p.x, dz = h.z - p.z, d = sqrtf(dx * dx + dz * dz);
        if (!m.entity && d < kMarkerShow) {
            m.entity = spawn::SpawnActor(kReticle, { p.x, p.y, p.z }, 0.0f, false);
            m.x = p.x; m.y = p.y; m.z = p.z;
            if (m.entity) LOG("portal: marker shown at (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
        } else if (m.entity && d > kMarkerHide) {
            spawn::DespawnActor(m.entity);
            m.entity = 0;
        }
    }

    static bool OnPad(const Pad& p, const director::HeroPos& h, float radius) {
        if (!p.set) return false;
        float dx = h.x - p.x, dz = h.z - p.z;
        return sqrtf(dx * dx + dz * dz) < radius && fabsf(h.y - p.y) < 3.0f;
    }

    static bool Near(const float p[3], const director::HeroPos& h, float radius, float dy = 3.0f) {
        float dx = h.x - p[0], dz = h.z - p[2];
        return sqrtf(dx * dx + dz * dz) < radius && fabsf(h.y - p[1]) < dy;
    }

    static Marker g_markers[kVenueCount * 2];   // [v*2] dock, [v*2+1] arena exit (arrival point)

    static void Loop() {
        bool enter_down = false, leave_down = false;
        DWORD enter_since = 0, leave_since = 0;
        bool interact_down = true;   // ignore a button already held when the loop starts
        DWORD prompt_at = 0, quiet_until = 0;
        int records_for = 0;         // dock circle whose records panel is up (1+venue)
        bool records_for_reset = false;
        int prompt_id = 0;           // 1+v dock prompt, 10+v exit prompt
        DWORD alive_since = 0, last_rescue = 0;
        while (g_running) {
            Sleep(100);
            input::Update();
            if (!g_hero) continue;
            director::HeroPos h = g_hero();
            // A run loadout left behind by a crash: put the player's own power and mods back once they're in game.
            static bool recovered = false;
            if (!recovered && h.ok && !director::Running()) {
                recovered = true;
                runloadout::Recover();
            }
            DWORD now = GetTickCount();
            Config cfg;
            { std::lock_guard<std::mutex> lock(g_cfg_mutex); cfg = g_cfg; }

            // Hotkeys, held for 1 s so a stray press mid-mission doesn't teleport anyone (game window focused only).
            bool focused = GameFocused();
            bool e = focused && (GetAsyncKeyState(cfg.key_enter) & 0x8000);
            bool l = focused && (GetAsyncKeyState(cfg.key_leave) & 0x8000);
            if (e && !enter_down) enter_since = now;
            if (l && !leave_down) leave_since = now;
            if (cfg.enabled && e && enter_since && now - enter_since > 1000) { enter_since = 0; Go(0); }
            if (cfg.enabled && l && leave_since && now - leave_since > 1000) { leave_since = 0; Leave(); }
            if (!e) enter_since = 0;
            if (!l) leave_since = 0;
            enter_down = e;
            leave_down = l;

            if (!h.ok) continue;
            if (records_for_reset) { records_for = 0; records_for_reset = false; }

            // The Horde results panel stays up until the player presses Continue (Cross / A / 4, or Interact).
            if (panels::ResultsUp() && focused &&
                (input::Pressed(input::Action::Choice4) || input::Pressed(input::Action::Interact))) {
                panels::Hide();
                hud::Show("Cross the line to go again, or step into the circle to leave Ryker's.", 6.0f);
            }
            panels::Tick();

            // Invisible walls (fenced arenas, e.g. the Raft roof): put the hero back if they leave or fall.
            int venue = director::Running() ? g_run_venue.load() : g_armed.load();
            if (venue >= 0) {
                const arena::Bounds* a = arena::Find(kVenues[venue].arena);
                if (a && a->fence && now - last_rescue > 1500 && (h.has_health ? h.health > 0 : true) &&
                    (h.y < a->fall_y || !arena::InsideFence(*a, h.x, h.z, 1.5f))) {
                    last_rescue = now;
                    arena::Teleport(a->rescue[0], a->rescue[1], a->rescue[2]);
                    LOG("portal: hero left the %s fence at (%.1f, %.1f, %.1f), put back", a->name, h.x, h.y, h.z);
                }
            }

            // Stepping out of the yard past the start line (user, 2026-10-15): not an instant end. After 1.5 s the game freezes and
            // a panel asks "Abandon this run?" (interact = abandon, the Choice3 button = stay). Staying outside starts a 10 s
            // "return to the yard" countdown; reaching zero ends the run with a finished message, going back in cancels it.
            if (director::Running() && g_run_venue >= 0) {
                const arena::Bounds* a = arena::Find(kVenues[g_run_venue].arena);
                static DWORD out_since = 0, count_start = 0;
                static int out_state = 0, shown_sec = -1;   // 0 inside, 1 asking, 2 counting down
                bool outside = a && a->wall && arena::WallSide(*a, h.x, h.z) < -3.0f;
                auto finish = [&] {
                    out_state = 0; out_since = 0; shown_sec = -1;
                    hud::HideCountdown();
                    hud::Clear();
                    director::Stop("left_arena");   // the results panel (Horde) comes from the run-ended callback
                    // Both modes now show the results panel (from the run-ended callback, which may land after this returns):
                    // no "run finished" banner or notification here, they piled up on it (user, 2026-10-16). The hint
                    // comes when the player presses Continue.
                };
                if (out_state == 0) {
                    if (outside) {
                        if (!out_since) out_since = now;
                        else if (now - out_since > 1500) {
                            out_state = 1;
                            director::SetPaused(true);
                            timescale::Freeze();
                            // The notification box alone drives the prompt (user, 2026-10-15: the panel hid behind the score panel).
                            hud::Show(std::string("Abandon this run? Press ") + input::Glyph(input::Action::Interact) +
                                      " to abandon, or " + input::Glyph(input::Action::Choice3) + " to return to the run.", 600.0f);
                        }
                    } else {
                        out_since = 0;
                    }
                } else if (out_state == 1) {
                    if (focused && input::Pressed(input::Action::Interact)) {          // yes: abandon
                        hud::Clear();
                        timescale::Unfreeze();
                        director::SetPaused(false);
                        finish();
                    } else if (focused && input::Pressed(input::Action::Choice3)) {    // no: stay in the run
                        hud::Clear();
                        timescale::Unfreeze();
                        director::SetPaused(false);
                        if (outside) {
                            out_state = 2;
                            count_start = now;
                            shown_sec = -1;
                            hud::Show("Please return to the yard.", 12.0f);
                        } else {
                            out_state = 0; out_since = 0;
                        }
                    }
                } else if (out_state == 2) {
                    if (!outside) {                                  // came back: cancelled
                        out_state = 0; out_since = 0; shown_sec = -1;
                        hud::HideCountdown();
                        hud::Clear();
                    } else {
                        int left = 10 - (int)((now - count_start) / 1000);
                        if (left <= 0) finish();
                        else if (left != shown_sec) { shown_sec = left; hud::Countdown(left); }
                    }
                }
            }

            // After a run ends (hero down): once the hero is back on their feet for 3 s, wherever the game respawned
            // them, bring them back to the arena's arrival point. Leaving is the player's call (user, 2026-10-13):
            // cross the line for another run, or take the exit circle home.
            // The game respawns Spidey perched, and teleporting out of a perch drags him through the air in the
            // perch pose (2026-10-13). So wait until he has left the perch (moved away from the respawn spot) and
            // come to rest (standing still for a moment), then teleport.
            if (g_pending_return && !director::Running()) {
                static float rest_pos[3], spawn_pos[3];
                static bool left_perch = false;
                static DWORD still_since = 0;
                bool alive = !h.has_health || h.health > 0.0f;
                if (!alive) { alive_since = 0; continue; }
                if (!alive_since) {
                    alive_since = now;
                    left_perch = false;
                    still_since = 0;
                    spawn_pos[0] = rest_pos[0] = h.x; spawn_pos[1] = rest_pos[1] = h.y; spawn_pos[2] = rest_pos[2] = h.z;
                    hud::Show("Drop down to head to the dock.", 6.0f);
                }
                auto dist = [&](const float* p) {
                    float dx = h.x - p[0], dy = h.y - p[1], dz = h.z - p[2];
                    return sqrtf(dx * dx + dy * dy + dz * dz);
                };
                if (!left_perch && dist(spawn_pos) > 1.5f) left_perch = true;
                if (dist(rest_pos) > 0.3f) {
                    rest_pos[0] = h.x; rest_pos[1] = h.y; rest_pos[2] = h.z;
                    still_since = now;
                }
                if (left_perch && still_since && now - still_since > 800) {
                    g_pending_return = false;
                    alive_since = 0;
                    int v = g_run_venue >= 0 ? g_run_venue.load() : 0;
                    const arena::Bounds* a = arena::Find(kVenues[v].arena);
                    if (a && FadeTeleport(a->tp[0], a->tp[1], a->tp[2])) {
                        arena::Activate(a->name);
                        g_armed = v;
                        LOG("portal: back at %s after the run", a->name);
                        score::Results r;
                        int level = 0;
                        if (director::TakeResults(r, level)) panels::Results(r, level);
                        if (!panels::ResultsUp())
                            hud::Show(std::string(a->title) + ": cross the line to go again, or step into the circle to leave.", 6.0f);
                    } else {
                        Leave();
                    }
                }
                continue;
            }
            if (director::Running() || !cfg.enabled) continue;

            // Markers: the game's replay reticle (blue ground circle) on each dock and each arena's exit pad.
            for (int v = 0; v < kVenueCount; v++) {
                Pad dock = v == 0 ? cfg.dock : cfg.raft_dock;
                UpdateMarker(g_markers[v * 2], dock, h);
                const arena::Bounds* a = arena::Find(kVenues[v].arena);
                bool shared = false;   // one exit circle per arena, even when several docks lead there
                for (int u = 0; u < v; u++) shared |= std::string(kVenues[u].arena) == kVenues[v].arena;
                if (shared) a = nullptr;
                const float* ex = a ? (a->exit[0] != 0 ? a->exit : a->tp) : nullptr;
                Pad exit{ ex != nullptr, ex ? ex[0] : 0, ex ? ex[1] : 0, ex ? ex[2] : 0 };
                UpdateMarker(g_markers[v * 2 + 1], exit, h);
            }

            // Already at Ryker's without the dock teleport (a save loaded at the dock, or got there on their own):
            // arm it for the venue they last came from, so crossing the line starts a run (user, 2026-10-14). Only on
            // the dock side of the line and near the arrival point, so it never fires mid-yard.
            if (g_armed < 0 && !g_pending_return) {
                int v = LastVenue();
                const arena::Bounds* a = arena::Find(kVenues[v].arena);
                if (a && a->wall && arena::WallSide(*a, h.x, h.z) < -1.0f && Near(a->tp, h, 150.0f, 40.0f)) {
                    g_armed = v;
                    arena::Activate(a->name);
                    LOG("portal: at %s without the dock teleport; armed for %s", a->name, *kVenues[v].mode ? kVenues[v].mode : "campaign");
                }
            }

            // Armed (sent to an arena, run not started): crossing the start line starts it. With a wall the line spans
            // the whole horizon at any height (walk through the gate, run up a building, swing over: user, 2026-10-13).
            if (g_armed >= 0) {
                const arena::Bounds* a = arena::Find(kVenues[g_armed].arena);
                bool crossed = a && (a->wall ? arena::WallSide(*a, h.x, h.z) >= 0.0f
                                             : a->start_radius > 0 && Near(a->start, h, a->start_radius, 4.0f));
                if (crossed) {
                    int v = g_armed;
                    g_armed = -1;
                    hud::Clear();
                    StartRun(v);
                    continue;
                }
            }

            // Prompts: dock circles and the arena exit circle; the interact button (F / controller Y or triangle) confirms.
            // Each shows the game's activity panel (user, 2026-10-15: no notification-box prompts): its button is the game's
            // RETRY on the same key, relabelled START / LEAVE. The notification box is only the fallback.
            int id = 0;
            for (int v = 0; v < kVenueCount && !id; v++) {
                Pad dock = v == 0 ? cfg.dock : cfg.raft_dock;
                float dp[3] = { dock.x, dock.y, dock.z };
                if (dock.set && Near(dp, h, cfg.radius)) id = 1 + v;
                const arena::Bounds* a = arena::Find(kVenues[v].arena);
                if (!id && a && Near(a->exit[0] != 0 ? a->exit : a->tp, h, cfg.radius)) id = 10 + v;
            }
            bool interact = focused && input::Down(input::Action::Interact);
            bool panel_ok = false;
            if (id) {
                if (records_for != id) {
                    records_for = id;
                    if (id < 10) {
                        BoardData b = BoardFor(id - 1);
                        panel_ok = panels::Records(b.title, b.best_seconds, b.best_kills, b.goals, "START");
                    }
                    // The Ryker's exit circle keeps the notification-box prompt (user, 2026-10-15).
                } else {
                    panel_ok = panels::RecordsUp();
                }
            } else if (records_for) {
                records_for = 0;
                panels::HideRecords();
            }
            if (id) {
                if (!panel_ok && (id != prompt_id || now - prompt_at > 1500) && (LONG)(now - quiet_until) >= 0) {   // refresh so it stays up
                    prompt_id = id;
                    prompt_at = now;
                    // Button named for the device in use (user: never hard-code "F" / "Triangle").
                    std::string press = std::string("  Press ") + input::Glyph(input::Action::Interact);
                    if (id < 10) hud::Show(kVenues[id - 1].prompt + press, 2.0f);
                    else hud::Show(std::string("Leave ") + arena::Find(kVenues[id - 10].arena)->title + "?" + press, 2.0f);
                }
                if (interact && !interact_down) {
                    hud::Clear();
                    prompt_id = 0;
                    panels::HideRecords();
                    records_for = 0;
                    if (id < 10) Go(id - 1);
                    else {
                        // Back to the dock the hero came from (several docks can share this arena).
                        int from = g_armed >= 0 ? g_armed.load() : g_run_venue >= 0 ? g_run_venue.load() : LastVenue();
                        g_armed = from;
                        Leave();
                    }
                }
            } else if (prompt_id) {
                hud::Clear();
                prompt_id = 0;
            }
            interact_down = interact;
        }
    }
    void Start(std::function<director::HeroPos()> hero) {
        if (g_running) return;
        g_hero = std::move(hero);
        WriteDefaultsIfMissing();
        Load();
        director::OnRunEnded([](const std::string& reason) {
            g_end_reason = reason;
            // Only portal runs send the hero back ("exit" already teleports in Leave()).
            if (reason != "exit" && reason != "left_arena" && g_portal_run) g_pending_return = true;
            if (reason != "hero_down") {
                score::Results r;
                int level = 0;
                if (director::TakeResults(r, level)) panels::Results(r, level);
            }
            if (reason == "left_arena") g_armed = g_run_venue.load();   // walking back in starts a fresh run
            g_portal_run = false;
        });
        psinput::Start();
        g_running = true;
        g_thread = std::thread(Loop);
        LogConfig();
    }

    void Stop() {
        g_running = false;
        if (g_thread.joinable()) g_thread.join();
    }
}
