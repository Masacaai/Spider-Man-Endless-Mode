#include "plan.h"

#include <algorithm>
#include <set>
#include <cmath>
#include <cstdio>

namespace plan {
    // ---- tuning ----
    // Base share of each role when unlocked (relative; normalised per unit so a role with many variants isn't
    // overrepresented). Elite is handled separately via elite_chance.
    static const float kBaseWeight[kRoles] = {
        /* Melee   */ 10.0f, /* Baton  */ 6.0f, /* Pistol */ 5.0f, /* Rifle */ 4.0f, /* Shield */ 3.0f,
        /* Rocket  */ 2.0f,  /* Sniper */ 1.2f, /* Heavy  */ 1.5f, /* Special */ 2.5f, /* Elite */ 0.0f,
    };

    // Wave length is capped; later difficulty comes from health, elites, mix and field size instead.
    static int GoonCount(const Config& cfg, int wave, int step, int cycle) {
        if (cfg.riot) return std::min(40 + 8 * (wave - 1), 120);
        static const float kStepMult[] = { 0.85f, 1.0f, 1.15f, 1.0f };
        int base = std::min(6 + 2 * wave, 30 + 10 * cycle);   // later cycles: bigger waves (numbers, not health)
        return (int)std::lround(base * kStepMult[std::min(step, 3)]);
    }

    // On the field at once: grows with the run up to the field cap (horde_max, set from the stress test).
    static int MaxAlive(const Config& cfg, int wave, int cycle) {
        if (cfg.riot) return cfg.riot_max;
        return std::clamp(8 + wave + 3 * cycle, 8, std::max(8, cfg.horde_max));
    }

    int UnitTypes(const roster::Faction& f) {
        std::set<std::string> paths;
        for (const auto& u : f.units) paths.insert(u.path);
        return (int)paths.size();
    }

    // Greedy: factions in the given order, each added if it still fits the budget (so a big faction that doesn't fit is
    // skipped, not the end of the pool), up to max_factions. If that leaves fewer than min_factions, the smallest of the
    // rest are added anyway (Horde starts with three factions in play).
    std::vector<const roster::Faction*> BudgetedPool(std::vector<const roster::Faction*> shuffled, int max_types, int max_factions,
                                                     int min_factions) {
        std::vector<const roster::Faction*> pool;
        int used = 0;
        for (const auto* f : shuffled) {
            if ((int)pool.size() >= max_factions) break;
            int n = UnitTypes(*f);
            if (used + n > max_types) continue;
            pool.push_back(f);
            used += n;
        }
        if ((int)pool.size() < min_factions) {
            std::vector<const roster::Faction*> rest;
            for (const auto* f : shuffled) if (std::find(pool.begin(), pool.end(), f) == pool.end()) rest.push_back(f);
            std::sort(rest.begin(), rest.end(), [](auto* x, auto* y) { return UnitTypes(*x) < UnitTypes(*y); });
            for (const auto* f : rest) { if ((int)pool.size() >= min_factions) break; pool.push_back(f); }
        }
        return pool;
    }

    std::vector<const roster::Faction*> CampaignOrder(bool shuffle, std::mt19937& rng, int max_types) {
        std::vector<const roster::Faction*> order;
        for (const auto& f : roster::Factions()) order.push_back(&f);
        if (shuffle && order.size() > 2) std::shuffle(order.begin() + 1, order.end(), rng);
        // Memory bound: the first faction (Street Thugs), plus a random pick of the rest that fits the unit-type budget,
        // kept in campaign order so the difficulty still climbs (unless shuffled); then the run cycles through them, tougher.
        std::vector<const roster::Faction*> rest(order.begin() + 1, order.end()), picked;
        std::shuffle(rest.begin(), rest.end(), rng);
        std::vector<const roster::Faction*> head{ order[0] };
        for (auto* f : rest) head.push_back(f);
        auto pool = BudgetedPool(head, max_types, kRunPool, 3);
        if (!shuffle)
            std::sort(pool.begin(), pool.end(), [&](auto* x, auto* y) {
                return std::find(order.begin(), order.end(), x) < std::find(order.begin(), order.end(), y);
            });
        return pool;
    }

    static bool BossUsable(const Config& cfg, const roster::Boss* b, int cycle = 99) {
        return b && (b->verified || cfg.allow_unverified_bosses) && !cfg.failed_bosses.count(b->name) && cycle >= b->min_cycle;
    }

    static bool HasRole(const roster::Faction* f, Role r) {
        for (const auto& u : f->units) if (u.role == r) return true;
        return false;
    }

    WavePlan Make(const Config& cfg, int wave, std::mt19937& rng) {
        WavePlan p;
        p.wave = wave;
        const int F = std::max<int>(1, (int)cfg.order.size());
        const int per = std::max(1, cfg.waves_per_chapter);
        int boss_index = 0;   // how many boss waves came before this one
        if (cfg.mode == Mode::BossRush) {
            p.chapter = wave - 1;
            p.step = per - 1;
            boss_index = wave - 1;
        } else {
            p.chapter = (wave - 1) / per;
            p.step = (wave - 1) % per;
            boss_index = p.chapter;
        }
        p.cycle = p.chapter / F;
        p.faction = cfg.order.empty() ? nullptr : cfg.order[p.chapter % F];
        p.boss_wave = p.step == per - 1;

        p.count = GoonCount(cfg, wave, p.step, p.cycle);
        p.max_alive = MaxAlive(cfg, wave, p.cycle);
        // No health sponges (user, 2026-10-13): difficulty comes from numbers and enemy mix, never from extra health.
        p.health_mult = 1.0f;
        p.boss_health_mult = 1.0f;

        // Role unlocks per chapter step; from the second cycle on everything is in play from the start.
        bool all = p.cycle >= 1 || p.step >= 2;
        for (int r = 0; r < kRoles; r++) {
            Role role = (Role)r;
            bool on = all;
            if (!all) {
                on = role == Role::Melee || role == Role::Baton ||
                     (p.step >= 1 && (role == Role::Pistol || role == Role::Rifle || role == Role::Shield)) ||
                     (p.step == 0 && wave >= 3 && role == Role::Pistol);
            }
            p.weights[r] = on ? kBaseWeight[r] : 0.0f;
        }
        // Early-chapter waves of later cycles still lean on the basics.
        if (p.cycle >= 1 && p.step == 0) { p.weights[(int)Role::Heavy] *= 0.5f; p.weights[(int)Role::Sniper] *= 0.5f; }

        const int riot_k = cfg.riot ? 2 : 1;
        for (int r = 0; r < kRoles; r++) p.caps[r] = 99;
        p.caps[(int)Role::Sniper] = (2 + p.cycle) * riot_k;
        p.caps[(int)Role::Rocket] = (3 + p.cycle) * riot_k;
        p.caps[(int)Role::Heavy] = std::min(2 + p.cycle, 4) * riot_k;
        p.caps[(int)Role::Special] = 4 * riot_k;
        p.caps[(int)Role::Elite] = (4 + 2 * p.cycle) * riot_k;

        bool has_elites = p.faction && HasRole(p.faction, Role::Elite);
        if (has_elites) {
            if (p.step >= 2) p.elite_chance = 0.10f + 0.05f * p.cycle;
            if (p.cycle >= 1) p.elite_chance = std::max(p.elite_chance, 0.05f + 0.08f * p.cycle);
            p.elite_chance = std::min(p.elite_chance, 0.5f);
        }

        char buf[160];
        static const char* const kStepName[] = { "Skirmish", "Assault", "Full Force" };
        const char* fname = p.faction ? p.faction->name : "?";

        if (p.boss_wave && p.faction) {
            std::vector<const roster::Boss*> pool;
            for (auto* b : p.faction->bosses) if (BossUsable(cfg, b, p.cycle)) pool.push_back(b);
            if (pool.empty()) {
                // No usable boss for this faction: an elite assault instead.
                p.boss_wave = false;
                p.kind = "elite assault";
                p.elite_chance = has_elites ? 0.6f : 0.0f;
                for (int r = (int)Role::Rocket; r < kRoles; r++) p.weights[r] *= 1.5f;
                snprintf(buf, sizeof(buf), "WAVE %d - %s: Elite Assault", wave, fname);
                p.title = buf;
                return p;
            }
            std::shuffle(pool.begin(), pool.end(), rng);
            int nb = 1;
            if (boss_index >= 4) {
                nb = std::min(2 + (boss_index - 4) / 3, 3);
                // Bring back bosses from factions already met (and still usable).
                std::vector<const roster::Boss*> met;
                for (int c = 0; c < p.chapter && c < (int)cfg.order.size() * 4; c++)
                    for (auto* b : cfg.order[c % F]->bosses)
                        if (BossUsable(cfg, b, p.cycle) && std::find(pool.begin(), pool.end(), b) == pool.end() &&
                            std::find(met.begin(), met.end(), b) == met.end())
                            met.push_back(b);
                std::shuffle(met.begin(), met.end(), rng);
                pool.insert(pool.end(), met.begin(), met.end());
            }
            for (int i = 0; i < nb && i < (int)pool.size(); i++) p.bosses.push_back(pool[i]);

            // MSMR's bosses rely on goons and throwables to be interesting, so a boss never fights alone.
            if (p.bosses.size() <= 1) {
                p.kind = "boss+goons";
                p.count = (int)std::lround(p.count * 0.5f);
            } else {
                p.kind = "multi-boss";
                p.count = (int)std::lround(p.count * 0.4f);
            }
            if (cfg.mode == Mode::BossRush && boss_index < 2) { p.kind = "boss"; p.count = 0; }
            if (p.count > 0 && has_elites) p.elite_chance = std::min(0.5f, 0.15f + 0.05f * p.cycle);
            p.max_alive = std::min(p.max_alive, (int)p.bosses.size() + std::max(4, p.max_alive / 2));
            std::string names;
            for (size_t i = 0; i < p.bosses.size(); i++) {
                if (i) names += i + 1 == p.bosses.size() ? " & " : ", ";
                names += p.bosses[i]->name;
            }
            snprintf(buf, sizeof(buf), "BOSS WAVE %d - %s%s", wave, names.c_str(), p.count ? " + crew" : "");
            p.title = buf;
            return p;
        }

        p.boss_wave = false;
        p.kind = p.step < 3 ? kStepName[p.step] : "Full Force";
        for (auto& c : p.kind) c = (char)tolower(c);
        snprintf(buf, sizeof(buf), "WAVE %d - %s: %s", wave, fname, p.step < 3 ? kStepName[p.step] : "Full Force");
        p.title = buf;
        return p;
    }

    // Horde is meant to be chaos from the first second (user, 2026-10-13): several factions mixed from level 1, a big
    // field that grows quickly to the stress-test cap, and the whole roster unlocked within a couple of levels.
    // Factions in play at a level: one more joins every level (3 at L1), and at most kHordeWindow fight at once - the
    // oldest leaves as a new one joins, cycling through cfg.order forever. All 9 at once meant 86 enemy types in
    // memory; bodies came out as white cubes and the respawn load hung (2026-10-14).
    std::vector<const roster::Faction*> HordeFactions(const Config& cfg, int level) {
        std::vector<const roster::Faction*> out;
        int n = (int)cfg.order.size();
        if (n == 0) return out;
        int joined = 2 + std::max(level, 1);
        int k = std::min({ joined, n, kHordeWindow });
        for (int i = joined - k; i < joined; i++) out.push_back(cfg.order[i % n]);
        return out;
    }

    WavePlan MakeHorde(const Config& cfg, int level) {
        WavePlan p;
        level = std::max(level, 1);
        p.wave = level;
        p.kind = "horde";
        auto in_play = HordeFactions(cfg, level);
        auto mixed = std::make_shared<roster::Faction>();
        mixed->id = "horde";
        mixed->name = "The Horde";
        std::string names;
        for (const roster::Faction* f : in_play) {
            mixed->units.insert(mixed->units.end(), f->units.begin(), f->units.end());
            names += std::string(names.empty() ? "" : ", ") + f->name;
        }
        p.mixed = mixed;
        p.faction = mixed.get();
        p.count = 1 << 30;   // endless
        p.max_alive = std::min(24 + 8 * (level - 1), std::max(8, cfg.horde_max));   // 24, 32, 40 ... cap by L8
        p.health_mult = 1.0f;   // no health sponges: numbers are the challenge
        for (int r = 0; r < kRoles; r++) {
            Role role = (Role)r;
            bool on = role == Role::Melee || role == Role::Baton || role == Role::Pistol || role == Role::Rifle ||
                      role == Role::Shield ||
                      (level >= 2 && (role == Role::Rocket || role == Role::Special || role == Role::Sniper)) ||
                      (level >= 3 && role == Role::Heavy);
            p.weights[r] = on ? kBaseWeight[r] : 0.0f;
        }
        for (int r = 0; r < kRoles; r++) p.caps[r] = 99;
        p.caps[(int)Role::Sniper] = 3 + level / 2;
        p.caps[(int)Role::Rocket] = 4 + level / 2;
        p.caps[(int)Role::Heavy] = std::min(3 + level / 2, 10);
        p.caps[(int)Role::Special] = 4 + level;
        p.caps[(int)Role::Elite] = 8 + 2 * level;
        p.elite_chance = level >= 3 ? std::min(0.4f, 0.05f * (level - 2)) : 0.0f;
        auto before = level > 1 ? HordeFactions(cfg, level - 1) : std::vector<const roster::Faction*>{};
        const roster::Faction* newcomer = nullptr;
        for (auto* f : in_play) if (std::find(before.begin(), before.end(), f) == before.end()) newcomer = f;
        char buf[200];
        if (level == 1) snprintf(buf, sizeof(buf), "HORDE - %s are hunting you", names.c_str());
        else if (newcomer) snprintf(buf, sizeof(buf), "%s join the riot", newcomer->name);
        else buf[0] = 0;   // no announcement: the escalation is silent (user, 2026-10-15: Horde has no levels)
        p.title = buf;
        return p;
    }
    const roster::Unit* PickUnit(const WavePlan& p, const std::array<int, kRoles>& alive, std::mt19937& rng) {
        if (!p.faction) return nullptr;
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        // Elite roll first.
        if (p.elite_chance > 0 && u01(rng) < p.elite_chance && alive[(int)Role::Elite] < p.caps[(int)Role::Elite]) {
            std::vector<const roster::Unit*> elites;
            for (const auto& u : p.faction->units) if (u.role == Role::Elite) elites.push_back(&u);
            if (!elites.empty()) return elites[std::uniform_int_distribution<size_t>(0, elites.size() - 1)(rng)];
        }
        std::array<int, kRoles> per_role{};
        for (const auto& u : p.faction->units) per_role[(int)u.role]++;
        std::vector<std::pair<const roster::Unit*, float>> cand;
        float total = 0;
        for (const auto& u : p.faction->units) {
            int r = (int)u.role;
            float w = p.weights[r];
            if (w <= 0 || alive[r] >= p.caps[r]) continue;
            w /= per_role[r];
            cand.push_back({ &u, w });
            total += w;
        }
        if (cand.empty()) {
            // Everything capped or locked: fall back to the cheapest available melee/baton unit.
            for (const auto& u : p.faction->units)
                if (u.role == Role::Melee || u.role == Role::Baton) return &u;
            return p.faction->units.empty() ? nullptr : &p.faction->units[0];
        }
        float x = u01(rng) * total;
        for (auto& [unit, w] : cand) {
            if (x < w) return unit;
            x -= w;
        }
        return cand.back().first;
    }

    std::string Describe(const WavePlan& p) {
        char buf[512];
        std::string bosses;
        for (auto* b : p.bosses) bosses += std::string(bosses.empty() ? "" : ",") + b->name;
        std::string roles;
        for (int r = 0; r < kRoles; r++)
            if (p.weights[r] > 0) roles += std::string(roles.empty() ? "" : " ") + roster::RoleName((Role)r);
        snprintf(buf, sizeof(buf), "w%-3d ch%-2d cyc%d %-20s %-12s goons=%-3d alive<=%-2d hp x%.2f elite=%.2f bosses=[%s] (boss hp x%.2f) roles: %s",
                 p.wave, p.chapter + 1, p.cycle, p.faction ? p.faction->id : "?", p.kind.c_str(), p.count, p.max_alive,
                 p.health_mult, p.elite_chance, bosses.c_str(), p.boss_health_mult, roles.c_str());
        return buf;
    }
}
