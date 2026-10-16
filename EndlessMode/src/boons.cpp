#include "boons.h"

#include "entity.h"
#include "gamethread.h"
#include "hud.h"
#include "input.h"
#include "inventory.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "runloadout.h"
#include "scan.h"
#include "stats.h"
#include "timescale.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <random>

namespace boons {
    struct Item { uint64_t id; const char* internal; const char* display; };
    static const Item kItems[] = {
#include "boon_items.inc"
    };

    // ---- stat boons (levers we control directly) ----
    enum class Stat { Power, Toughness, Vampire, Recovery };
    struct StatDef { Stat stat; const char* name; const char* desc; int max_stacks; };
    static const StatDef kStats[] = {
        { Stat::Power, "Heavy Hitter", "+20% damage dealt", 3 },
        { Stat::Toughness, "Reinforced Suit", "-15% damage taken", 3 },
        { Stat::Vampire, "Adrenaline Rush", "heal 4% of max health per kill", 3 },
        { Stat::Recovery, "Second Wind", "heal 30% of max health after each wave", 2 },
    };

    // ---- bittersweet curses (user: always an upside AND a downside; downsides never add enemy health) ----
    enum class Curse { GlassCannon, RiotCall, OpenSeason, BloodPrice };
    struct CurseDef { Curse curse; const char* name; const char* up; const char* down; };
    static const CurseDef kCurses[] = {
        { Curse::GlassCannon, "Glass Cannon", "+40% damage dealt", "+30% damage taken" },
        { Curse::RiotCall, "Riot Call", "4 choices on every reward", "waves bring 40% more enemies" },
        { Curse::OpenSeason, "Open Season", "heal 25% after each wave", "snipers and rockets from the first wave" },
        { Curse::BloodPrice, "Blood Price", "heal 6% per kill", "elites twice as often" },
    };

    // ---- state ----
    enum class Kind { Power, Mod, Stat, Curse, Decline };
    struct Option { Kind kind; uint64_t id = 0; int index = 0; std::string label; };
    struct Offer { std::string title; std::vector<Option> options; bool curse = false; DWORD shown_at = 0; DWORD ready_at = 0; };
    constexpr DWORD kOfferDelayMs = 3500;   // after the wave clears, let finishers and animations play out (user)

    static std::mutex g_mutex;
    static std::atomic<bool> g_active = false;
    static std::mt19937 g_rng;
    static std::vector<uint64_t> g_powers, g_mods;   // owned, by the game's own category
    static int g_stacks[4] = {};
    static std::vector<Curse> g_curses;
    static std::vector<std::string> g_taken;          // history for the loadout view
    static std::deque<Offer> g_queue;
    static std::atomic<bool> g_offering = false;
    static std::atomic<bool> g_frozen = false;          // world time scale held near 0 while an offer is up
    static DWORD g_last_refresh = 0;
    constexpr DWORD kOfferTimeoutMs = 45000;          // AFK: take a random option (curses: decline)

    static bool HasCurse(Curse c) { return std::find(g_curses.begin(), g_curses.end(), c) != g_curses.end(); }

    // The game's own name and description for an item (cached), so rewards read exactly as in the suit menu, including a
    // user's renamed or modded powers. Falls back to our table.
    static const inventory::ItemText& GameText(uint64_t id) {
        static std::unordered_map<uint64_t, inventory::ItemText> cache;
        auto it = cache.find(id);
        if (it == cache.end()) it = cache.emplace(id, inventory::Text(id)).first;
        return it->second;
    }

    static const char* TableName(uint64_t id);
    static std::string Label(uint64_t id) {
        const auto& t = GameText(id);
        return t.ok ? t.name : std::string(TableName(id));
    }
    // "Name: description" when the game has a description.
    static std::string Described(uint64_t id) {
        const auto& t = GameText(id);
        if (!t.ok) return TableName(id);
        return t.desc.empty() || t.desc == "INVALID" ? t.name : t.name + ": " + t.desc;
    }

    static const char* Name(uint64_t id) { return TableName(id); }
    static const char* TableName(uint64_t id) {
        for (const auto& it : kItems)
            if (it.id == id) {
                if (*it.display) return it.display;
                static thread_local char buf[40];
                snprintf(buf, sizeof(buf), "Item %04llX", (unsigned long long)(id & 0xFFFF));
                return buf;
            }
        return "?";
    }

    float OutgoingMult() {
        if (!g_active) return 1.0f;
        float m = 1.0f + 0.20f * g_stacks[(int)Stat::Power];
        if (HasCurse(Curse::GlassCannon)) m *= 1.40f;
        return m;
    }

    float IncomingMult() {
        if (!g_active) return 1.0f;
        float m = std::pow(0.85f, (float)g_stacks[(int)Stat::Toughness]);
        if (HasCurse(Curse::GlassCannon)) m *= 1.30f;
        return m;
    }

    // Heal the hero by a fraction of max health (game thread; Health::SetHealth clamps to max).
    static void HealHero(float frac) {
        if (frac <= 0) return;
        gamethread::Post([frac] {
            using SetFn = void (*)(uintptr_t, float);
            auto set = (SetFn)natives::Get(natives::HealthSet);
            auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
            uintptr_t system = scan::FromRva(natives::kHeroSystemRva);
            uint32_t handle = 0;
            if (!set || !get_actor || !memory::Read(system + 0x1C, handle) || !handle) return;
            uintptr_t actor = get_actor(&handle);
            uintptr_t c = actor ? entity::FindComponent(actor, "HeroHealth") : 0;
            float max_hp = 0, hp = 0;
            if (!c || !memory::Read(c + 0x80, max_hp) || !memory::Read(c + 0x88, hp) || max_hp <= 0 || hp <= 0) return;
            set(c, std::min(max_hp, hp + max_hp * frac));
        });
    }

    void Begin(uint32_t seed) {
        // The game's own item category decides what's a power (1) or a mod (2); skills never qualify.
        auto pools = std::make_shared<std::pair<std::vector<uint64_t>, std::vector<uint64_t>>>();
        gamethread::Run([pools] {
            for (const auto& it : kItems) {
                inventory::ItemInfo info = inventory::Info(it.id);
                if (!info.found || !info.owned || info.locked) continue;
                if (info.category == 1) pools->first.push_back(it.id);
                else if (info.category == 2) pools->second.push_back(it.id);
            }
        }, 5000);
        // Curated pools (2026-10-15, from the suit menu texts in tools/assets/perk_text.json): leave out suit powers and mods that do
        // nothing useful in the arena. Matched by the game's own display name (so a renamed mod isn't filtered by accident, and
        // the English names are the ones the suit menu uses).
        static const char* const kBlocked[] = {
            // no effect against enemies that are already engaged / not combat
            "Equalizer",                // "Everyone goes down in one hit, including you": a curse, not a reward
            "Quips", "Sound Of Silence", "Blur Projector",
            "Combat Analyzer", "Proximity Sensor", "Long Range Scanner", "Silent Step", "Bio Scanner",
            // need gadgets, which are locked during runs
            "Resupply", "Battle Tech", "Scavenger", "Metabolic Converter",
        };
        [[maybe_unused]] auto blocked = [](uint64_t id) {
            const inventory::ItemText& t = GameText(id);
            if (!t.ok) return false;
            for (const char* b : kBlocked) if (_stricmp(t.name.c_str(), b) == 0) return true;
            return false;
        };
        // OFF for now (user, 2026-10-15): everything is included until the technical work is done; the user has the final say
        // on curation. Flip kCuratePools to apply the list above.
        constexpr bool kCuratePools = false;
        size_t before_p = pools->first.size(), before_m = pools->second.size();
        if (kCuratePools) {
            std::erase_if(pools->first, blocked);
            std::erase_if(pools->second, blocked);
        }
        LOG("boons: curated pools: %zu -> %zu suit powers, %zu -> %zu suit mods", before_p, pools->first.size(), before_m, pools->second.size());
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_rng.seed(seed ^ 0xB0057u);
            g_powers = pools->first;
            g_mods = pools->second;
            std::fill(std::begin(g_stacks), std::end(g_stacks), 0);
            g_curses.clear();
            g_taken.clear();
            g_queue.clear();
            g_offering = false;
        }
        LOG("boons: pools: %zu suit powers, %zu suit mods (owned, by game category)", g_powers.size(), g_mods.size());
        runloadout::Begin();   // snapshot, clear power and mods, lock the menu (blocking game-thread work)
        g_active = true;
    }

    void End() {
        if (!g_active) return;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_active = false;
            g_queue.clear();
            g_offering = false;
        }
        if (g_frozen.exchange(false)) timescale::Unfreeze();
        hud::Clear();
        runloadout::End();
    }

    bool Active() { return g_active; }

    // ---- offers ----

    static Offer MakeRewardOffer() {
        Offer o;
        o.title = "Choose a reward";
        int n = HasCurse(Curse::RiotCall) ? 4 : 3;
        std::vector<Option> pool;
        uint64_t cur[5];
        inventory::Loadout(cur);
        bool power_empty = cur[1] == 0;
        if (runloadout::FreeModSlot() >= 0)
            for (uint64_t id : g_mods)
                if (!runloadout::HasItem(id)) pool.push_back({ Kind::Mod, id, 0, Described(id) });
        for (int i = 0; i < (int)std::size(kStats); i++)
            if (g_stacks[i] < kStats[i].max_stacks)
                pool.push_back({ Kind::Stat, 0, i, std::string(kStats[i].name) + " (" + kStats[i].desc + ")" });
        std::shuffle(pool.begin(), pool.end(), g_rng);
        // Until the run has a suit power, one option is always a power.
        if (power_empty && !g_powers.empty()) {
            uint64_t id = g_powers[std::uniform_int_distribution<size_t>(0, g_powers.size() - 1)(g_rng)];
            o.options.push_back({ Kind::Power, id, 0, Described(id) });
        }
        for (auto& op : pool) {
            if ((int)o.options.size() >= n) break;
            o.options.push_back(op);
        }
        return o;
    }

    static Offer MakePowerOffer() {
        Offer o;
        o.title = "Boss down! Choose a suit power";
        uint64_t cur[5];
        inventory::Loadout(cur);
        std::vector<uint64_t> pool;
        for (uint64_t id : g_powers)
            if (id != cur[1]) pool.push_back(id);
        std::shuffle(pool.begin(), pool.end(), g_rng);
        int n = HasCurse(Curse::RiotCall) ? 4 : 3;
        for (int i = 0; i < (int)pool.size() && i < n; i++)
            o.options.push_back({ Kind::Power, pool[i], 0, Described(pool[i]) + (cur[1] ? " (replaces your power)" : "") });
        return o;
    }

    static Offer MakeCurseOffer() {
        Offer o;
        o.title = "A bittersweet deal? (optional)";
        o.curse = true;
        std::vector<int> pool;
        for (int i = 0; i < (int)std::size(kCurses); i++)
            if (!HasCurse(kCurses[i].curse)) pool.push_back(i);
        std::shuffle(pool.begin(), pool.end(), g_rng);
        for (int i = 0; i < (int)pool.size() && i < 2; i++) {
            const CurseDef& c = kCurses[pool[i]];
            o.options.push_back({ Kind::Curse, 0, pool[i], std::string(c.name) + ": " + c.up + ", but " + c.down });
        }
        if (!o.options.empty()) o.options.push_back({ Kind::Decline, 0, 0, "No deal" });
        return o;
    }

    void OnWaveCleared(int wave, bool boss_wave) {
        if (!g_active) return;
        // Heals first, so they're felt during the pick.
        float heal = 0.30f * g_stacks[(int)Stat::Recovery] + (HasCurse(Curse::OpenSeason) ? 0.25f : 0.0f);
        HealHero(heal);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (boss_wave) {
            Offer p = MakePowerOffer();
            if (!p.options.empty()) g_queue.push_back(p);
            Offer c = MakeCurseOffer();
            if (!c.options.empty()) g_queue.push_back(c);
        } else {
            Offer r = MakeRewardOffer();
            if (!r.options.empty()) g_queue.push_back(r);
        }
        if (!g_queue.empty()) g_queue.front().ready_at = GetTickCount() + kOfferDelayMs;
        g_offering = !g_queue.empty();
        g_last_refresh = 0;
        LOG("boons: wave %d cleared, %zu offer(s) queued", wave, g_queue.size());
    }

    void OnKill() {
        if (!g_active) return;
        float frac = 0.04f * g_stacks[(int)Stat::Vampire] + (HasCurse(Curse::BloodPrice) ? 0.06f : 0.0f);
        HealHero(frac);
    }

    bool Offering() { return g_offering; }

    static void Show(const Offer& o) {
        static const input::Action acts[] = { input::Action::Choice1, input::Action::Choice2, input::Action::Choice3,
                                              input::Action::Choice4 };
        // One message, one option per line (HUD slots 2-6 share one spot on screen and overwrite each other, and a
        // single line wraps unevenly: user, 2026-10-13). Controllers get the game's button icons, keyboards "[1]".
        // (Padding with blank lines got the box cut off: 2026-10-13. A proper choice screen is a UI-hunt item.)
        std::string text = o.title;
        for (size_t i = 0; i < o.options.size() && i < 4; i++)
            text += std::string("\n") + input::Glyph(acts[i]) + "  " + o.options[i].label;
        hud::Show(text, 0.0f);   // stays until the pick clears it (user: it went away too soon)
    }

    static void Apply(const Option& op) {
        std::string what;
        switch (op.kind) {
        case Kind::Power:
            runloadout::SetPower(op.id);
            what = std::string("Suit power: ") + Label(op.id);
            break;
        case Kind::Mod:
            runloadout::AddMod(op.id);
            what = std::string("Suit mod: ") + Label(op.id);
            break;
        case Kind::Stat:
            g_stacks[op.index]++;
            what = std::string(kStats[op.index].name) + " " + std::to_string(g_stacks[op.index]) + "/" +
                   std::to_string(kStats[op.index].max_stacks);
            break;
        case Kind::Curse:
            g_curses.push_back(kCurses[op.index].curse);
            what = std::string("Curse: ") + kCurses[op.index].name;
            break;
        case Kind::Decline:
            what = "No deal";
            break;
        }
        if (op.kind != Kind::Decline) g_taken.push_back(what);
        stats::Event("boon", what);
        LOG("boons: took %s", what.c_str());
        hud::Show(what, 3.0f);
    }

    void Tick() {
        if (!g_offering) return;
        static const input::Action acts[] = { input::Action::Choice1, input::Action::Choice2, input::Action::Choice3,
                                              input::Action::Choice4 };
        input::Update();
        Option chosen{ Kind::Decline };
        bool have = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_queue.empty()) { g_offering = false; return; }
            Offer& o = g_queue.front();
            DWORD now = GetTickCount();
            if (o.ready_at && (int)(now - o.ready_at) < 0) return;   // still the post-wave breather
            if (!o.shown_at) {
                o.shown_at = now;
                if (!g_frozen) { g_frozen = true; timescale::Freeze(); }   // like the pause menu: the fight waits
            }
            for (size_t i = 0; i < o.options.size() && i < 4; i++)
                if (input::Pressed(acts[i])) { chosen = o.options[i]; have = true; break; }
            if (!have && now - o.shown_at > kOfferTimeoutMs) {
                // Nobody's picking (AFK): curses default to "No deal", rewards to a random option.
                chosen = o.curse ? o.options.back()
                                 : o.options[std::uniform_int_distribution<size_t>(0, o.options.size() - 1)(g_rng)];
                have = true;
                LOG("boons: offer timed out");
            }
            if (!have && (!g_last_refresh || now - g_last_refresh > 8000)) {   // shown once; re-shown rarely in case
                g_last_refresh = now;                                       // something else replaced it
                Show(o);
            }
            if (have) g_queue.pop_front();
        }
        if (have) {
            hud::Clear();
            Apply(chosen);   // may block on game-thread work (equips)
            std::lock_guard<std::mutex> lock(g_mutex);
            g_offering = !g_queue.empty();
            g_last_refresh = 0;
            if (!g_offering && g_frozen) { g_frozen = false; timescale::Unfreeze(); }
        }
    }

    void AdjustWave(plan::WavePlan& p, int horde_max) {
        if (!g_active) return;
        if (HasCurse(Curse::RiotCall)) {
            p.count = (int)std::ceil(p.count * 1.4f);
            p.max_alive = std::min(p.max_alive + 4, std::max(8, horde_max));
        }
        if (HasCurse(Curse::OpenSeason)) {
            using roster::Role;
            p.weights[(int)Role::Sniper] = std::max(p.weights[(int)Role::Sniper], 1.0f);
            p.weights[(int)Role::Rocket] = std::max(p.weights[(int)Role::Rocket], 1.0f);
            p.caps[(int)Role::Sniper] = std::max(p.caps[(int)Role::Sniper], 2);
            p.caps[(int)Role::Rocket] = std::max(p.caps[(int)Role::Rocket], 2);
        }
        if (HasCurse(Curse::BloodPrice)) p.elite_chance = std::min(0.6f, std::max(0.1f, p.elite_chance * 2.0f));
    }

    std::vector<std::string> Summary() {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<std::string> out;
        uint64_t cur[5];
        inventory::Loadout(cur);
        out.push_back(std::string("Power: ") + (cur[1] ? Label(cur[1]) : "none"));
        for (int s = 2; s <= 4; s++) out.push_back(std::string("Mod ") + std::to_string(s - 1) + ": " + (cur[s] ? Label(cur[s]) : "empty"));
        for (int i = 0; i < (int)std::size(kStats); i++)
            if (g_stacks[i]) out.push_back(std::string(kStats[i].name) + " x" + std::to_string(g_stacks[i]));
        for (Curse c : g_curses)
            for (const auto& d : kCurses)
                if (d.curse == c) out.push_back(std::string("Curse: ") + d.name);
        return out;
    }
}
