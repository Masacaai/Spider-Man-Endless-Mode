// Prints the wave plans of a run so the difficulty curve can be read and tuned without the game.
// Usage: plan_offline [waves=32] [mode: campaign|riot|bossrush|faction:<id>] [seed=1] [shuffle 0|1]
#include "../src/plan.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

int main(int argc, char** argv) {
    int waves = argc > 1 ? atoi(argv[1]) : 32;
    std::string mode = argc > 2 ? argv[2] : "campaign";
    uint32_t seed = argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 10) : 1;
    bool shuffle = argc > 4 && atoi(argv[4]) != 0;

    std::mt19937 rng(seed);
    plan::Config cfg;
    cfg.order = plan::CampaignOrder(shuffle, rng);
    if (mode == "riot") cfg.riot = true;
    if (mode == "bossrush") cfg.mode = plan::Mode::BossRush;
    if (mode.rfind("faction:", 0) == 0) {
        cfg.mode = plan::Mode::Faction;
        cfg.order = { roster::FindFaction(mode.substr(8)) };
        if (!cfg.order[0]) { printf("unknown faction\n"); return 1; }
    }

    if (mode == "horde") {
        cfg.mode = plan::Mode::Horde;
        cfg.order = plan::CampaignOrder(true, rng);
        for (int l = 1; l <= waves; l++) {
            plan::WavePlan p = plan::MakeHorde(cfg, l);
            printf("%s\n      title: %s\n", plan::Describe(p).c_str(), p.title.c_str());
        }
        return 0;
    }
    printf("order:");
    for (auto* f : cfg.order) printf(" %s", f->id);
    printf("\n");
    int total = 0;
    for (int w = 1; w <= waves; w++) {
        plan::WavePlan p = plan::Make(cfg, w, rng);
        printf("%s\n", plan::Describe(p).c_str());
        // Simulate the composition of the goons (nobody dies; caps apply to everything spawned so far in a field
        // of max_alive, which is what the director sees at the start of a wave).
        std::array<int, plan::kRoles> alive{};
        std::map<std::string, int> mix;
        int spawned = 0;
        for (int i = 0; i < p.count; i++) {
            if (spawned >= p.max_alive) alive = {};   // field turned over
            const roster::Unit* u = plan::PickUnit(p, alive, rng);
            if (!u) break;
            alive[(int)u->role]++;
            mix[roster::RoleName(u->role)]++;
            spawned++;
        }
        printf("      mix:");
        for (auto& [k, v] : mix) printf(" %s=%d", k.c_str(), v);
        printf("\n      title: %s\n", p.title.c_str());
        total += p.count + (int)p.bosses.size();
    }
    printf("total enemies over %d waves: %d\n", waves, total);
    return 0;
}
