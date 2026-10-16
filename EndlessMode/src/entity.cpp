#include "entity.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "scan.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace entity {
    static bool ReadCString(uintptr_t addr, char* out, size_t cap) {
        for (size_t i = 0; i + 1 < cap; i++) {
            if (!memory::Read(addr + i, out[i])) return false;
            if (out[i] == 0) return true;
        }
        out[cap - 1] = 0;
        return true;
    }

    void ForEachComponent(uintptr_t entity, const std::function<void(const std::string&, uintptr_t)>& fn) {
        uintptr_t comps = 0;
        int16_t count = 0;
        if (!memory::Read(entity + 0x68, comps) || !memory::Read(entity + 0x70, count) || !comps) return;
        for (int i = 0; i < count && i < 512; i++) {
            uintptr_t desc = 0, inst = 0, name_ptr = 0;
            if (!memory::Read(comps + i * 16, desc) || !memory::Read(comps + i * 16 + 8, inst)) return;
            char name[128] = "?";
            if (desc && memory::Read(desc + 0x60, name_ptr) && name_ptr) ReadCString(name_ptr, name, sizeof(name));
            fn(name, inst);
        }
    }

    uintptr_t FindComponent(uintptr_t entity, const char* name) {
        uintptr_t found = 0;
        ForEachComponent(entity, [&](const std::string& n, uintptr_t inst) {
            if (!found && n == name) found = inst;
        });
        return found;
    }

    bool ReadBotHealth(uintptr_t entity, float& health) {
        uintptr_t c = FindComponent(entity, "BotHealth");
        return c && memory::Read(c + 0x88, health);
    }

    uintptr_t HealthComponent(uintptr_t entity) {
        uintptr_t bot = 0, any = 0;
        ForEachComponent(entity, [&](const std::string& n, uintptr_t inst) {
            if (!inst) return;
            if (n == "BotHealth" && !bot) bot = inst;
            if (!any && n.size() >= 6 && n.compare(n.size() - 6, 6, "Health") == 0 && n != "HeroHealth") any = inst;
        });
        return bot ? bot : any;
    }

    bool ReadHealth(uintptr_t entity, float& health, float& max_health) {
        uintptr_t c = HealthComponent(entity);
        return c && memory::Read(c + 0x88, health) && memory::Read(c + 0x80, max_health);
    }

    float ScaleHealth(uintptr_t entity, float mult) {
        using SetFn = void (*)(uintptr_t self, float value);
        auto set = (SetFn)natives::Get(natives::HealthSet);
        uintptr_t c = HealthComponent(entity);
        float max_hp = 0;
        if (!set || !c || !memory::Read(c + 0x80, max_hp) || !(max_hp > 0.0f && max_hp < 1.0e6f)) return 0;
        float target = max_hp * mult;
        if (!memory::Write<float>(c + 0x80, target)) return 0;
        set(c, target);
        return target;
    }

    bool ReadHeroHealth(uintptr_t hero, float& health, float& max_health) {
        uintptr_t c = FindComponent(hero, "HeroHealth");
        return c && memory::Read(c + 0x88, health) && memory::Read(c + 0x80, max_health);
    }

    uint32_t HandleOf(uintptr_t entity) {
        uint16_t hi = 0;
        uint32_t lo = 0;
        if (!memory::Read(entity + 0x10, hi) || !memory::Read(entity + 0xC, lo)) return 0;
        return ((uint32_t)(hi & 0x7FF) << 20) | (lo & 0xFFFFF);
    }

    uintptr_t FromHandle(uint32_t handle) {
        uintptr_t base = 0;
        uint32_t count = 0;
        if (!handle || !memory::Read(natives::Get(natives::ActorTable), base) || !base ||
            !memory::Read(natives::Get(natives::ActorCount), count))
            return 0;
        uint32_t idx = handle & 0xFFFFF;
        if (idx < count && HandleOf(base + (uintptr_t)idx * 0xC0) == handle) return base + (uintptr_t)idx * 0xC0;
        for (uint32_t i = 0; i < count && i < 0x100000; i++)
            if (HandleOf(base + (uintptr_t)i * 0xC0) == handle) return base + (uintptr_t)i * 0xC0;
        return 0;
    }

    static bool Pos(uintptr_t e, float p[3]) {
        uintptr_t tr = 0;
        return memory::Read(e, tr) && tr && memory::SafeRead(tr + 0x30, p, 3 * sizeof(float));
    }

    uintptr_t FindHead(uintptr_t body, float max_dist, int* offset) {
        // The body spawns its head right after itself: the next actor-table slot, handle + 1 (live 2026-10-14, every
        // goon checked). A head has 1-2 components (AnimControllerComponent, sometimes one more) and sits on the body.
        // The weapon can also follow the body; it has BotWeapon / BotMeleeWeapon and more components.
        float bp[3], hp[3];
        uint32_t own = HandleOf(body);
        if (!own || !Pos(body, bp)) return 0;
        for (int k = 1; k <= 2; k++) {
            uintptr_t e = body + (uintptr_t)k * 0xC0;
            if (HandleOf(e) != own + (uint32_t)k || !Pos(e, hp)) continue;
            int n = 0;
            bool anim = false, other = false;
            ForEachComponent(e, [&](const std::string& name, uintptr_t) {
                n++;
                if (name == "AnimControllerComponent") anim = true;
                else if (name.find("Weapon") != std::string::npos || name == "BotHealth") other = true;
            });
            float dx = hp[0] - bp[0], dy = hp[1] - bp[1], dz = hp[2] - bp[2];
            if (!anim || other || n > 3 || sqrtf(dx * dx + dy * dy + dz * dz) > max_dist) continue;
            if (offset) *offset = k;
            return e;
        }
        return 0;
    }

    uintptr_t Camera() {
        static std::atomic<uintptr_t> cached = 0;
        static std::atomic<DWORD> last_search = 0;
        uintptr_t c = cached;
        if (c && FindComponent(c, "FollowCamera")) return c;
        DWORD now = GetTickCount();
        if (now - last_search < 2000) return 0;
        last_search = now;
        uintptr_t base = 0;
        uint32_t count = 0;
        if (!memory::Read(natives::Get(natives::ActorTable), base) || !base ||
            !memory::Read(natives::Get(natives::ActorCount), count))
            return 0;
        for (uint32_t i = 0; i < count && i < 0x100000; i++) {
            uintptr_t e = base + (uintptr_t)i * 0xC0;
            uint16_t gen = 0;
            if (!memory::Read(e + 0x10, gen) || (gen & 0x7FF) == 0) continue;
            if (FindComponent(e, "FollowCamera")) {
                cached = e;
                LOG("camera: FollowCamera actor %llx", (unsigned long long)e);
                return e;
            }
        }
        cached = 0;
        return 0;
    }

    int AreaHandles(uintptr_t entity, uint32_t out[4], std::string names[4]) {
        int n = 0;
        ForEachComponent(entity, [&](const std::string& name, uintptr_t inst) {
            if (n >= 4 || (name != "BotAreaActor" && name != "BotAreaActorGroup")) return;
            uint32_t h = 0;
            if (inst && memory::Read(inst + 0x48, h)) { names[n] = name; out[n++] = h; }
        });
        return n;
    }

    // Shape test for a BotData struct at p: +0 a vtable in the exe image, the bool fields 0/1, the floats sane.
    static bool LooksLikeBotData(uintptr_t p) {
        uint8_t b[0xA0];
        if (!memory::SafeRead(p, b, sizeof(b))) return false;
        uintptr_t vt = *(uintptr_t*)b;
        if (scan::Rva(vt) == 0 || scan::Rva(vt) > 0x8000000) return false;
        for (int off : { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x14, 0x80, 0x81, 0x98, 0x99 })
            if (b[off] > 1) return false;
        for (int off : { 0x10, 0x78, 0x7C }) {
            float f = *(float*)(b + off);
            if (!(f >= -1000.0f && f <= 1000.0f)) return false;
        }
        return true;
    }

    std::vector<BotDataHit> FindBotData(uintptr_t entity) {
        std::vector<BotDataHit> hits;
        ForEachComponent(entity, [&](const std::string& name, uintptr_t inst) {
            if (!inst) return;
            uint8_t buf[0x1000];
            if (!memory::SafeRead(inst, buf, sizeof(buf))) {
                if (!memory::SafeRead(inst, buf, 0x400)) return;
                memset(buf + 0x400, 0, sizeof(buf) - 0x400);
            }
            for (uint32_t off = 8; off + 0xA0 <= sizeof(buf); off += 8)
                if (LooksLikeBotData(inst + off)) hits.push_back({ name, inst + off, 0, 0, off });
            for (uint32_t off = 8; off < 0x400; off += 8) {
                uintptr_t q = *(uintptr_t*)(buf + off);
                if (q < 0x10000000000ull || q > 0x7FF000000000ull || (q & 7)) continue;   // heap pointers only
                if (LooksLikeBotData(q)) hits.push_back({ name, q, 1, off, 0 });
                // BotPrius (per-type config, ctor exe+ee6330) embeds its BotData at +0x2C0.
                if (LooksLikeBotData(q + 0x2C0)) hits.push_back({ name, q + 0x2C0, 1, off, 0x2C0 });
            }
        });
        for (auto& h : hits) {
            uintptr_t vt = 0;
            memory::Read(h.addr, vt);
            uintptr_t rva = scan::Rva(vt);
            h.exact = rva == 0x395e988 || rva == 0x3970d90 || rva == 0x39885c8;
        }
        return hits;
    }

    void ForceAwareHack(uintptr_t entity, uint32_t hero_handle, int parts) {
        if (!hero_handle) return;
        uintptr_t tiered = FindComponent(entity, "TieredAggroTargetingGame");
        uintptr_t db = FindComponent(entity, "GameAwarenessDB");
        uintptr_t skirmish = FindComponent(entity, "SkirmishAwareness");
        gamethread::Post([=] {
            int done = 0;
            if ((parts & kAwareTiered) && tiered) {
                // Values exactly as seen on a bot that had just noticed the hero.
                memory::Write<uint32_t>(tiered + 0x60, 0x00010000);
                memory::Write<float>(tiered + 0x64, 45.0f);
                memory::Write<uint32_t>(tiered + 0x6C, 0x2038);
                memory::Write<uint32_t>(tiered + 0x70, 0x00010000);
                memory::Write<uint32_t>(tiered + 0x78, hero_handle);
                done |= kAwareTiered;
            }
            if ((parts & kAwareDB) && db) { memory::Write<uint32_t>(db + 0x8C, hero_handle); done |= kAwareDB; }
            if ((parts & kAwareSkirmish) && skirmish) { memory::Write<uint32_t>(skirmish + 0x180, hero_handle); done |= kAwareSkirmish; }
            LOG("aggro hack: entity=%llx parts=%d written=%d", (unsigned long long)entity, parts, done);
        });
    }
}
