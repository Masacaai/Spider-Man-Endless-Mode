#include "roster.h"

#include <cstring>
#include <mutex>

namespace roster {
#include "roster_data.inc"

    const char* RoleName(Role r) {
        static const char* const names[] = { "melee", "baton", "pistol", "rifle", "shield", "rocket", "sniper",
                                              "heavy", "special", "elite" };
        return (int)r < (int)Role::Count ? names[(int)r] : "?";
    }

    static std::vector<Boss>& MutableBosses() {
        static std::vector<Boss> bosses(std::begin(kBosses), std::end(kBosses));
        return bosses;
    }

    const std::vector<Boss>& Bosses() { return MutableBosses(); }

    const std::vector<Faction>& Factions() {
        static std::vector<Faction> factions = [] {
            std::vector<Faction> out;
            for (const auto& f : kFactions) {
                Faction x{ f.id, f.name, f.units, {} };
                for (const char* b : f.bosses)
                    if (const Boss* boss = FindBoss(b)) x.bosses.push_back(boss);
                out.push_back(std::move(x));
            }
            return out;
        }();
        return factions;
    }

    const Faction* FindFaction(const std::string& id) {
        for (const auto& f : Factions()) if (id == f.id) return &f;
        return nullptr;
    }

    static bool IEquals(const std::string& a, const char* b) {
        return _stricmp(a.c_str(), b) == 0;
    }

    const Boss* FindBoss(const std::string& key) {
        for (const auto& b : Bosses()) {
            if (IEquals(key, b.name) || IEquals(key, b.path) || IEquals(key, ShortName(b.path).c_str())) return &b;
        }
        return nullptr;
    }

    void SetVerified(const std::string& key, bool v) {
        for (auto& b : MutableBosses()) {
            if (IEquals(key, b.name) || IEquals(key, b.path) || IEquals(key, ShortName(b.path).c_str())) b.verified = v;
        }
    }

    std::string ShortName(const char* path) {
        const char* s = strrchr(path, '\\');
        std::string n = s ? s + 1 : path;
        if (n.size() > 6 && n.compare(n.size() - 6, 6, ".actor") == 0) n.resize(n.size() - 6);
        return n;
    }
}
