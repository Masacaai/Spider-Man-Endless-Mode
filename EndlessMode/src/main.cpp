// EndlessMode: wave-survival mode for Marvel's Spider-Man Remastered (Spider-Man.exe 4.0630.0.0).
// Loaded by Overstrike's scripts proxy (LoadLibrary from scripts\, then script_enable()).
//
// Dev control: write commands to scripts\EndlessMode.cmd (one per line); results go to scripts\EndlessMode.log.

#include "arena.h"
#include "damage.h"
#include "director.h"
#include "entity.h"
#include "events.h"
#include "guards.h"
#include "hud.h"
#include "portal.h"
#include "psinput.h"
#include "roster.h"
#include "stats.h"
#include "stress.h"
#include "camscan.h"
#include "inventory.h"
#include "watchpoint.h"
#include "boons.h"
#include "timescale.h"
#include "gfx.h"
#include "panels.h"
#include "score.h"
#include "ui.h"
#include "uilog.h"
#include "runloadout.h"
#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "observer.h"
#include "scan.h"
#include "spawn.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::string g_dir;   // folder containing this DLL (the game's scripts\ folder)
static std::vector<uintptr_t> g_spawned;   // entities spawned via the spawn command

// Enemies from the prison-break mission (paths from jedijosh920's Mod Menu Actors.txt).
static const char* const kPrisonRoster[] = {
    "character\\enemy\\enm_meleethug\\enm_prison_meleethug.actor",
    "character\\enemy\\enm_meleethug\\enm_prison_baton_thug.actor",
    "character\\enemy\\enm_meleethug\\enm_prison_shield_thug.actor",
    "character\\enemy\\enm_meleethug\\enm_prison_whip.actor",
    "character\\enemy\\enm_meleethug\\enm_thug_prison_heavy.actor",
    "character\\enemy\\enm_grenadier\\enm_thug_prison_rpg.actor",
    "character\\enemy\\enm_meleethug\\enm_prison_super_meleethug.actor",
    "character\\enemy\\enm_meleethug\\enm_prison_super_baton_thug.actor",
    "character\\enemy\\enm_meleethug\\enm_prison_super_shield_thug.actor",
    "character\\enemy\\enm_meleethug\\enm_thug_prison_super_heavy.actor",
    "character\\enemy\\enm_grenadier\\enm_thug_super_prison_rpg.actor",
    "character\\enemy\\enm_meleethug\\enm_melee_fighting_inmate.actor",
    "character\\enemy\\enm_meleethug\\enm_meleethug.actor",
};

static std::string ModuleDir() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ModuleDir, &self);
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string s(path);
    return s.substr(0, s.find_last_of("\\/"));
}

// "exe+1f745f0" -> absolute; plain hex -> as is.
static uintptr_t ParseAddr(const std::string& s) {
    if (s.rfind("exe+", 0) == 0) return scan::FromRva(strtoull(s.c_str() + 4, nullptr, 16));
    return strtoull(s.c_str(), nullptr, 16);
}

// ---- hero ----

struct HeroInfo {
    bool ok = false;
    uint32_t handle = 0;
    uintptr_t actor = 0;
    uintptr_t transform = 0;
    float pos[3] = {};
};

static HeroInfo ReadHero() {
    HeroInfo h;
    uintptr_t system = scan::FromRva(natives::kHeroSystemRva);
    auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
    if (!get_actor || !memory::Read(system + 0x1C, h.handle) || h.handle == 0) return h;
    if (!memory::SafeCall(get_actor, &h.handle, h.actor)) return h;
    if (!h.actor || !memory::Read(h.actor, h.transform) || !h.transform) return h;
    h.ok = memory::SafeRead(h.transform + 0x30, h.pos, sizeof(h.pos));
    return h;
}

static void LogHero(const char* where) {
    HeroInfo h = ReadHero();
    if (!h.ok) {
        LOG("hero (%s): not available (handle=%08x actor=%llx)", where, h.handle, (unsigned long long)h.actor);
        return;
    }
    float hp = -1, max_hp = -1;
    entity::ReadHeroHealth(h.actor, hp, max_hp);
    LOG("hero (%s): handle=%08x actor=%llx transform=%llx pos=(%.2f, %.2f, %.2f) health=%.1f/%.1f", where, h.handle,
        (unsigned long long)h.actor, (unsigned long long)h.transform, h.pos[0], h.pos[1], h.pos[2], hp, max_hp);
}

// ---- targets and groups ----

// "hero" | spawned index | 0x<entity> | h<handle>  ->  entity (may be 0 for a bare handle that doesn't resolve)
static uintptr_t ParseEntity(const std::string& s, uint32_t* handle_out = nullptr) {
    auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
    uintptr_t e = 0;
    uint32_t handle = 0;
    if (s == "hero") {
        HeroInfo h = ReadHero();
        e = h.actor; handle = h.handle;
    } else if (s.rfind("0x", 0) == 0) {
        e = strtoull(s.c_str() + 2, nullptr, 16);
    } else if (!s.empty() && s[0] == 'h') {
        handle = (uint32_t)strtoul(s.c_str() + 1, nullptr, 16);
        if (get_actor && handle) memory::SafeCall(get_actor, &handle, e);
    } else if (!s.empty() && isdigit((unsigned char)s[0])) {
        size_t i = strtoul(s.c_str(), nullptr, 10);
        if (i < g_spawned.size()) e = g_spawned[i];
    }
    if (!handle && e) handle = entity::HandleOf(e);
    if (handle_out) *handle_out = handle;
    return e;
}

// A bot group as Group::Alert (exe+496b40) sees it: member records at [g+0x50+i*8], count u32 at g+0x450;
// a record with u32 type 2 at +0x10 is a bot whose actor handle is at +0x70.
struct GroupInfo {
    uintptr_t addr = 0;
    uint32_t ref_index = 0;
    std::vector<uint32_t> bots;   // handles of type-2 members
    uint32_t members = 0;
};

static bool ReadGroup(uintptr_t g, GroupInfo& out) {
    uint32_t count = 0;
    if (!memory::Read(g + 0x450, count) || count == 0 || count > 128) return false;
    uintptr_t recs[128];
    if (!memory::SafeRead(g + 0x50, recs, count * 8)) return false;
    out.addr = g;
    out.members = count;
    out.bots.clear();
    for (uint32_t i = 0; i < count; i++) {
        uint32_t type = 0, h = 0;
        if (recs[i] < 0x10000 || !memory::Read(recs[i] + 0x10, type) || type > 16) return false;
        if (type == 2) {
            if (!memory::Read(recs[i] + 0x70, h) || !h) return false;
            out.bots.push_back(h);
        }
    }
    return !out.bots.empty();
}

// Walks the engine's weak-ref table (where group refs resolve, see exe+642420) for objects shaped like a group.
static std::vector<GroupInfo> ScanGroups() {
    std::vector<GroupInfo> found;
    uintptr_t table = natives::Get(natives::RefTable), entries = 0;
    int32_t count = 0;
    if (!table || !memory::Read(table, entries) || !memory::Read(table + 0x20, count) || !entries || count <= 0) {
        LOG("groups: ref table unavailable");
        return found;
    }
    std::vector<uintptr_t> chunk(2 * 4096);
    for (int32_t i0 = 0; i0 < count; i0 += 4096) {
        int32_t n = std::min(4096, count - i0);
        if (!memory::SafeRead(entries + (uintptr_t)i0 * 16, chunk.data(), (size_t)n * 16)) continue;
        for (int32_t k = 0; k < n; k++) {
            uintptr_t obj = chunk[k * 2];
            GroupInfo g;
            if (obj < 0x10000 || !ReadGroup(obj, g)) continue;
            g.ref_index = (uint32_t)(i0 + k);
            found.push_back(g);
        }
    }
    LOG("groups: scanned %d refs, %zu group-shaped objects", count, found.size());
    return found;
}

static void LogGroup(const GroupInfo& g, uint32_t mark) {
    std::string hs;
    char buf[16];
    for (uint32_t h : g.bots) { snprintf(buf, sizeof(buf), h == mark ? " *%08x" : " %08x", h); hs += buf; }
    LOG("  group 0x%llx (ref #%u): %u members, %zu bots:%s", (unsigned long long)g.addr, g.ref_index, g.members,
        g.bots.size(), hs.c_str());
}

// Hero state for the director, portal and stress test (any thread; guarded reads).
static director::HeroPos HeroPosNow() {
    HeroInfo h = ReadHero();
    float hp = 1, max_hp = 1;
    bool has_hp = h.ok && entity::ReadHeroHealth(h.actor, hp, max_hp);
    float fwd[3] = { 0, 0, 1 };
    if (h.ok) memory::SafeRead(h.transform + 0x20, fwd, sizeof(fwd));
    director::HeroPos p{ h.ok, h.pos[0], h.pos[1], h.pos[2], has_hp, hp, fwd[0], fwd[2], h.handle };
    p.max_health = has_hp ? max_hp : 0;
    uintptr_t cam = h.ok ? entity::Camera() : 0, tr = 0;
    float m[8];   // row 2 (forward) and row 3 (position)
    if (cam && memory::Read(cam, tr) && tr && memory::SafeRead(tr + 0x20, m, sizeof(m))) {
        p.cam_ok = true;
        p.cam_fx = m[0];
        p.cam_fz = m[2];
        p.cam_x = m[4];
        p.cam_y = m[5];
        p.cam_z = m[6];
    }
    return p;
}

// Boss verification flags persist in EndlessMode.ini [bosses] (key = boss name, 1 = works standalone, 0 = broken).
static void LoadBossFlags() {
    std::string ini = g_dir + "\\EndlessMode.ini";
    for (const auto& b : roster::Bosses()) {
        int v = GetPrivateProfileIntA("bosses", b.name, -1, ini.c_str());
        if (v >= 0) roster::SetVerified(b.name, v != 0);
    }
}

// ---- commands ----

static void CmdPeek(uintptr_t addr, size_t len) {
    if (len == 0 || len > 512) len = 64;
    std::vector<uint8_t> buf(len);
    if (!memory::SafeRead(addr, buf.data(), len)) {
        LOG("peek %llx: unreadable", (unsigned long long)addr);
        return;
    }
    for (size_t i = 0; i < len; i += 16) {
        char line[128];
        int n = snprintf(line, sizeof(line), "  %llx:", (unsigned long long)(addr + i));
        for (size_t j = i; j < i + 16 && j < len; j++) n += snprintf(line + n, sizeof(line) - n, " %02X", buf[j]);
        LOG("%s", line);
    }
}

// deref <addr> <off1> <off2> ...: p = [addr]; p = [p+off1]; ... logging each step.
static void CmdDeref(uintptr_t addr, const std::vector<uintptr_t>& offs) {
    uintptr_t p = 0;
    if (!memory::Read(addr, p)) { LOG("deref: [%llx] unreadable", (unsigned long long)addr); return; }
    LOG("deref: [%llx] = %llx", (unsigned long long)addr, (unsigned long long)p);
    for (uintptr_t off : offs) {
        uintptr_t next = 0;
        if (!memory::Read(p + off, next)) { LOG("deref: [%llx+%llx] unreadable", (unsigned long long)p, (unsigned long long)off); return; }
        LOG("deref: [%llx+%llx] = %llx", (unsigned long long)p, (unsigned long long)off, (unsigned long long)next);
        p = next;
    }
}

static void RunCommand(const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    if (cmd.empty() || cmd[0] == '#') return;
    LOG("> %s", line.c_str());

    if (cmd == "ping") {
        LOG("pong (hook=%d ticks=%llu)", gamethread::Installed(), (unsigned long long)gamethread::Ticks());
    } else if (cmd == "addrs") {
        natives::ResolveAll();
    } else if (cmd == "hero") {
        LogHero("worker thread");
    } else if (cmd == "peek") {
        std::string a; size_t len = 64;
        in >> a >> len;
        CmdPeek(ParseAddr(a), len);
    } else if (cmd == "deref") {
        std::string a, o;
        in >> a;
        std::vector<uintptr_t> offs;
        while (in >> o) offs.push_back(strtoull(o.c_str(), nullptr, 16));
        CmdDeref(ParseAddr(a), offs);
    } else if (cmd == "hook") {
        LOG("hook: %s", gamethread::Install() ? "ok" : "FAILED");
    } else if (cmd == "unhook") {
        gamethread::Uninstall();
    } else if (cmd == "ticks") {
        uint64_t t0 = gamethread::Ticks();
        Sleep(1000);
        LOG("ticks: %llu total, %llu in the last second", (unsigned long long)gamethread::Ticks(),
            (unsigned long long)(gamethread::Ticks() - t0));
    } else if (cmd == "roster") {
        for (size_t i = 0; i < std::size(kPrisonRoster); i++) LOG("  %zu: %s", i, kPrisonRoster[i]);
    } else if (cmd == "spawn") {
        // spawn <roster index | asset path> [distance=4] [heading=0] [engaged] [cold]
        // Waits (up to 25 s) for the actor and its runtime dependencies (weapons, jetpack, head) to load first, unless
        // "cold" is given (spawns right away, as the director did before 2026-10-14: white-cube weapons).
        std::string what, flag, tok; float dist = 4.0f, heading = 0.0f; bool cold = false;
        in >> what >> dist >> heading;
        while (in >> tok) { if (tok == "cold") cold = true; else flag = tok; }
        std::string path = what;
        if (!what.empty() && isdigit((unsigned char)what[0])) {
            size_t i = strtoul(what.c_str(), nullptr, 10);
            if (i >= std::size(kPrisonRoster)) { LOG("spawn: roster index out of range"); return; }
            path = kPrisonRoster[i];
        }
        HeroInfo h = ReadHero();
        if (!h.ok) { LOG("spawn: hero not available"); return; }
        if (!cold) {
            DWORD t0 = GetTickCount();
            while (!spawn::Ready(path, 0) && GetTickCount() - t0 < 25000) Sleep(100);
            LOG("spawn: dependencies ready after %lu ms", GetTickCount() - t0);
        }
        spawn::Vec3 pos{ h.pos[0] + dist, h.pos[1], h.pos[2] };
        uintptr_t e = spawn::SpawnActor(path, pos, heading, flag == "engaged");
        LOG("spawn: result entity=%llx", (unsigned long long)e);
        if (e) g_spawned.push_back(e);
    } else if (cmd == "list") {
        for (size_t i = 0; i < g_spawned.size(); i++) {
            uintptr_t sync = spawn::FindSyncFor(g_spawned[i]);
            float hp = -1.0f;
            bool has_hp = entity::ReadBotHealth(g_spawned[i], hp);
            LOG("  %zu: entity=%llx sync=%llx health=%s%.1f", i, (unsigned long long)g_spawned[i],
                (unsigned long long)sync, has_hp ? "" : "n/a ", hp);
        }
    } else if (cmd == "comps") {
        // comps <spawned index | hero>
        std::string which;
        in >> which;
        uintptr_t e = 0;
        if (which == "hero") e = ReadHero().actor;
        else if (which.rfind("0x", 0) == 0) e = strtoull(which.c_str() + 2, nullptr, 16);
        else if (!which.empty() && strtoul(which.c_str(), nullptr, 10) < g_spawned.size()) e = g_spawned[strtoul(which.c_str(), nullptr, 10)];
        if (!e) { LOG("comps: no such entity"); return; }
        int n = 0;
        entity::ForEachComponent(e, [&](const std::string& name, uintptr_t inst) {
            LOG("  [%d] %s @ %llx", n++, name.c_str(), (unsigned long long)inst);
        });
        LOG("comps: %d components", n);
    } else if (cmd == "despawn") {
        // despawn <index | all>
        std::string which;
        in >> which;
        std::vector<size_t> idx;
        if (which == "all") { for (size_t i = g_spawned.size(); i-- > 0; ) idx.push_back(i); }
        else if (!which.empty()) idx.push_back(strtoul(which.c_str(), nullptr, 10));
        for (size_t i : idx) {
            if (i >= g_spawned.size()) { LOG("despawn: index out of range"); continue; }
            LOG("despawn %zu: %s", i, spawn::DespawnActor(g_spawned[i]) ? "ok" : "FAILED");
            g_spawned.erase(g_spawned.begin() + i);
        }
    } else if (cmd == "prox") {
        // prox on [config path] | prox off | prox stop | prox reset | prox tier <n> | prox state
        // Drives the engine's ProximitySpawnSystem. With no path, Enable falls back to the system's default
        // config (configs/ProxSpawnConfig_Default, which our .stage replaces).
        std::string sub, arg;
        in >> sub >> arg;
        uintptr_t sys = natives::Get(natives::ProxSystem);
        if (!sys || !gamethread::Installed()) { LOG("prox: need hook + ProxSystem"); return; }
        if (sub == "state") {
            uint32_t state = 0, activator = 0, tiers = 0;
            uint8_t flag = 0;
            memory::Read(sys + 0x18, state); memory::Read(sys + 0x2C, activator);
            memory::Read(sys + 0x44, tiers); memory::Read(sys + 0x1D, flag);
            LOG("prox: state=%u activator=%08x tiers=%u flag1D=%u", state, activator, tiers, flag);
            return;
        }
        if (sub == "peek") {
            // vtable (expected exe+3909470) and the system-config handle at +0x1F20 {data*, asset*, ids, loaded}
            uintptr_t vt = 0, cfg_data = 0, cfg_asset = 0;
            uint8_t loaded = 0;
            memory::Read(sys, vt); memory::Read(sys + 0x1F20, cfg_data);
            memory::Read(sys + 0x1F28, cfg_asset); memory::Read(sys + 0x1F38, loaded);
            LOG("prox: vtable=exe+%llx cfg_data=%llx cfg_asset=%llx loaded=%u", (unsigned long long)scan::Rva(vt),
                (unsigned long long)cfg_data, (unsigned long long)cfg_asset, loaded);
            return;
        }
        if (sub == "init") {
            // ProximitySpawnSystem::Init = vtable[1] (exe+d3aaa0): loads ProximitySpawnSystemConfig asynchronously.
            uintptr_t vt = 0, init = 0;
            if (!memory::Read(sys, vt) || !memory::Read(vt + 8, init) || scan::Rva(init) != 0xd3aaa0) {
                LOG("prox: vtable[1] is not the expected Init (exe+%llx), refusing", (unsigned long long)scan::Rva(init));
                return;
            }
            gamethread::Post([sys, init] {
                ((void (*)(uintptr_t))init)(sys);
                LOG("prox: Init called");
            });
            return;
        }
        HeroInfo h = ReadHero();
        uint64_t config = 0;
        if (sub == "on") {
            // Always pass an explicit asset id (top bit set) so Enable doesn't read the system's default-config
            // pointer, which is null when the system was never initialised (crash at exe+d3c030).
            std::string path = arg.empty() ? "configs/ProxSpawnConfig_Default/ProxSpawnConfig_Default.config" : arg;
            auto hash_fn = (uint64_t* (*)(uint64_t*, const char*))natives::Get(natives::ConvertStringToHash);
            if (hash_fn) hash_fn(&config, path.c_str());
            uintptr_t cfg_data = 0;
            memory::Read(sys + 0x1F20, cfg_data);
            if (!cfg_data) { LOG("prox: system config not loaded (run 'prox init' first), refusing to enable"); return; }
        }
        uint32_t handle = h.ok ? h.handle : 0;
        uint32_t tier = (uint32_t)strtoul(arg.c_str(), nullptr, 10);
        gamethread::Post([sub, sys, config, handle, tier] {
            if (sub == "on") {
                ((void (*)(uintptr_t, uint64_t, uint32_t, uint8_t))natives::Get(natives::ProxEnable))(sys, config, handle, 2);
                LOG("prox: enabled (config=%016llx activator=%08x)", (unsigned long long)config, handle);
            } else if (sub == "off") {
                ((void (*)(uintptr_t, uint32_t, uint32_t))natives::Get(natives::ProxDisable))(sys, handle, 0);
                LOG("prox: disabled");
            } else if (sub == "stop") {
                ((void (*)(uintptr_t))natives::Get(natives::ProxStop))(sys);
                LOG("prox: stop");
            } else if (sub == "reset") {
                ((void (*)(uintptr_t))natives::Get(natives::ProxReset))(sys);
                LOG("prox: reset");
            } else if (sub == "tier") {
                ((void (*)(uintptr_t))natives::Get(natives::ProxClearTiers))(sys);
                ((void (*)(uintptr_t, uint32_t))natives::Get(natives::ProxPushTier))(sys, tier);
                LOG("prox: tier %u", tier);
            } else {
                LOG("prox: unknown subcommand");
            }
        });
    } else if (cmd == "vsearch") {
        // vsearch <window bytes> <f32> <f32> ...: finds places in committed private RW memory where all the
        // given floats occur within one window (e.g. config structs). Logs up to 20 hits with the window's floats.
        size_t window = 64;
        in >> window;
        std::vector<float> want;
        for (float f; in >> f; ) want.push_back(f);
        if (want.empty() || window < 8 || window > 1024) { LOG("vsearch: usage vsearch <window> <f32>..."); return; }
        int hits = 0;
        size_t scanned = 0;
        MEMORY_BASIC_INFORMATION mbi;
        std::vector<float> chunk;
        for (uintptr_t a = 0x10000; hits < 20 && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            // Plain RW private memory only. Never touch guard pages (thread stacks): a read there raises
            // STATUS_GUARD_PAGE_VIOLATION, which is not an access violation and crashed the game once.
            if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE) continue;
            if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) continue;
            const uintptr_t base = (uintptr_t)mbi.BaseAddress, end = base + mbi.RegionSize;
            // Copy the region in chunks (overlapping by one window) and search the copy.
            const size_t step = 1 << 20;
            for (uintptr_t c0 = base; c0 < end && hits < 20; c0 += step) {
                uintptr_t c1 = std::min(end, c0 + step + window);
                chunk.resize((c1 - c0) / 4);
                if (!memory::SafeRead(c0, chunk.data(), chunk.size() * 4)) continue;
                scanned += chunk.size() * 4;
                for (size_t i = 0; i < chunk.size() && hits < 20; i++) {
                    if (chunk[i] != want[0] || c0 + i * 4 >= c0 + step) continue;
                    size_t w0 = i >= window / 8 ? i - window / 8 : 0, w1 = std::min(chunk.size(), w0 + window / 4);
                    bool all = true;
                    for (size_t k = 1; k < want.size() && all; k++)
                        all = std::find(chunk.begin() + w0, chunk.begin() + w1, want[k]) != chunk.begin() + w1;
                    if (!all) continue;
                    hits++;
                    char line[1024];
                    int n = snprintf(line, sizeof(line), "  hit %llx (window %llx):", (unsigned long long)(c0 + i * 4),
                                     (unsigned long long)(c0 + w0 * 4));
                    for (size_t k = w0; k < w1 && n < 960; k++) n += snprintf(line + n, sizeof(line) - n, " %g", chunk[k]);
                    LOG("%s", line);
                    i = w1;   // don't report the same window twice
                }
            }
        }
        LOG("vsearch: scanned %zu MB", scanned >> 20);
        LOG("vsearch: %d hits", hits);
    } else if (cmd == "poke") {
        // poke <addr|exe+rva> <u32|f32|u64> <value>   (game thread)
        std::string a, type, v;
        in >> a >> type >> v;
        uintptr_t addr = ParseAddr(a);
        if (!gamethread::Installed()) { LOG("poke: run 'hook' first"); return; }
        gamethread::Post([addr, type, v] {
            bool ok = false;
            if (type == "f32") ok = memory::Write<float>(addr, strtof(v.c_str(), nullptr));
            else if (type == "u64") ok = memory::Write<uint64_t>(addr, strtoull(v.c_str(), nullptr, 16));
            else ok = memory::Write<uint32_t>(addr, (uint32_t)strtoul(v.c_str(), nullptr, 16));
            LOG("poke %llx %s %s: %s", (unsigned long long)addr, type.c_str(), v.c_str(), ok ? "ok" : "FAILED");
        });
    } else if (cmd == "aggro") {
        // aggro <spawned index> [tiered|db|skirmish|all]   EXPERIMENTAL
        size_t i = 99999; std::string part = "all";
        in >> i >> part;
        if (i >= g_spawned.size()) { LOG("aggro: no such spawned index"); return; }
        int parts = part == "tiered" ? entity::kAwareTiered : part == "db" ? entity::kAwareDB
                  : part == "skirmish" ? entity::kAwareSkirmish : entity::kAwareAll;
        HeroInfo h = ReadHero();
        if (!gamethread::Installed() || !h.ok) { LOG("aggro: need hook + hero"); return; }
        entity::ForceAwareHack(g_spawned[i], h.handle, parts);
    } else if (cmd == "actor") {
        // actor <addr|exe+rva of a u32 handle> | actor h<hex handle>: resolve an actor handle to its entity
        std::string a;
        in >> a;
        auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
        uint32_t handle = 0;
        if (!a.empty() && a[0] == 'h') handle = (uint32_t)strtoul(a.c_str() + 1, nullptr, 16);
        else memory::Read(ParseAddr(a), handle);
        uintptr_t e = 0;
        if (get_actor && handle) memory::SafeCall(get_actor, &handle, e);
        LOG("actor: handle %08x -> entity 0x%llx", handle, (unsigned long long)e);
    } else if (cmd == "bots") {
        // bots [radius=60]: every live entity with a BotHealth component within radius of the hero (via the sync table)
        float radius = 60.0f;
        in >> radius;
        HeroInfo h = ReadHero();
        uintptr_t table = 0;
        auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
        if (!h.ok || !get_actor || !memory::Read(natives::Get(natives::SyncSlotTable), table) || !table) { LOG("bots: unavailable"); return; }
        int found = 0;
        for (int i = 0; i < 0x2EE0; i++) {
            uintptr_t sync = 0, e = 0, tr = 0;
            if (!memory::Read(table + i * 8, sync) || !sync) continue;
            if (!memory::SafeCall(get_actor, (void*)sync, e) || !e) continue;
            float hp = 0;
            if (!entity::ReadBotHealth(e, hp)) continue;
            float p[3] = {};
            if (!memory::Read(e, tr) || !tr || !memory::SafeRead(tr + 0x30, p, sizeof(p))) continue;
            float dx = p[0] - h.pos[0], dy = p[1] - h.pos[1], dz = p[2] - h.pos[2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d > radius) continue;
            bool ours = std::find(g_spawned.begin(), g_spawned.end(), e) != g_spawned.end();
            uint32_t areas[4];
            std::string area_names[4];
            int na = entity::AreaHandles(e, areas, area_names);
            std::string as;
            char buf[48];
            for (int k = 0; k < na; k++) { snprintf(buf, sizeof(buf), " %s=%08x", area_names[k] == "BotAreaActor" ? "area" : "grp", areas[k]); as += buf; }
            LOG("  bot 0x%llx h%08x dist=%.1f health=%.1f%s%s", (unsigned long long)e, entity::HandleOf(e), d, hp,
                ours ? " (ours)" : "", as.c_str());
            found++;
        }
        LOG("bots: %d within %.0f m", found, radius);
    } else if (cmd == "aggro2") {
        // aggro2 <spawned index> [amount=2] [mode=0]: hit on the bot attributed to the hero (game damage pipeline)
        size_t i = 99999;
        float amount = 2.0f;
        int mode = 0;
        in >> i >> amount >> mode;
        if (i >= g_spawned.size()) { LOG("aggro2: no such spawned index"); return; }
        HeroInfo h = ReadHero();
        if (!h.ok) { LOG("aggro2: hero unavailable"); return; }
        damage::Nudge(g_spawned[i], h.handle, h.pos, amount, mode);
    } else if (cmd == "dmgwatch") {
        std::string mode;
        in >> mode;
        { HeroInfo hh = ReadHero(); if (hh.ok) observer::SetHeroHandle(hh.handle); }
        observer::WatchDamage(mode != "off");
    } else if (cmd == "observe") {
        std::string mode;
        in >> mode;
        observer::Enable(mode != "off");
    } else if (cmd == "tp") {
        float x = 0, y = 0, z = 0;
        if (!(in >> x >> y >> z)) { LOG("tp: usage tp <x> <y> <z>"); return; }
        arena::Teleport(x, y, z);
    } else if (cmd == "zone") {
        std::string path;
        in >> path;
        arena::LoadZone(path);
    } else if (cmd == "arena") {
        // arena rykers | arena raft [full] | arena off
        std::string name, mode;
        in >> name >> mode;
        if (name == "off") arena::Clear();
        else arena::Enter(name, mode == "full");
    } else if (cmd == "wave") {
        // wave start [first wave] | wave stop | wave status
        std::string sub; int first = 1;
        in >> sub >> first;
        if (sub == "start") {
            // Legacy form: campaign from the given wave (see "run" for the full set of options).
            director::Options opt;
            opt.first_wave = first;
            opt.countdown_s = 3;
            director::Start(opt, HeroPosNow);
        } else if (sub == "plan") {
            // wave plan [from=1] [to=from+15]: logs the planned waves of a default campaign
            int to = first + 15;
            in.clear();
            in >> to;
            director::Options opt;
            director::PreviewPlans(opt, first, to);
        } else if (sub == "stop") {
            director::Stop();
        } else if (sub == "riot") {
            // wave riot [max alive=40] | wave riot off
            std::string arg;
            in.clear();
            in >> arg;
            // A numeric argument was already consumed into `first` by the generic parse above.
            int n = arg == "off" ? 0 : !arg.empty() ? atoi(arg.c_str()) : first > 1 ? first : 40;
            director::SetRiot(n == 0 ? 0 : std::clamp(n, 1, 80));
        } else if (sub == "aggro") {
            std::string mode;
            in.clear();
            in >> mode;
            director::SetForceAggro(mode != "off");
        } else {
            director::LogStatus();
        }
    } else if (cmd == "sendevent") {
        // sendevent <target: hero|idx|0xEntity|hHandle> <EventName|hex hash> [source: hero|self|none|hHandle] [f1] [f2]
        std::string target, type, source = "hero";
        float f1 = 0.0f, f2 = 0.0f;
        in >> target >> type >> source >> f1 >> f2;
        uint32_t th = 0, sh = 0, hash = events::TypeHash(type);
        ParseEntity(target, &th);
        if (source == "self") sh = th;
        else if (source != "none") ParseEntity(source, &sh);
        if (!th || !hash) { LOG("sendevent: bad target or event type"); return; }
        events::Send(hash, th, sh, f1, f2);
    } else if (cmd == "groups") {
        // groups [target]: every group-shaped object, or only those containing the target bot (marked with *)
        std::string target;
        in >> target;
        uint32_t th = 0;
        if (!target.empty()) ParseEntity(target, &th);
        if (!target.empty() && !th) { LOG("groups: no such target"); return; }
        int shown = 0;
        for (const GroupInfo& g : ScanGroups()) {
            if (th && std::find(g.bots.begin(), g.bots.end(), th) == g.bots.end()) continue;
            LogGroup(g, th);
            shown++;
        }
        LOG("groups: %d shown", shown);
    } else if (cmd == "group") {
        // group <0xGroup>: its members with entity, distance and health
        std::string a;
        in >> a;
        GroupInfo g;
        if (!ReadGroup(ParseAddr(a), g)) { LOG("group: %s is not group-shaped", a.c_str()); return; }
        LogGroup(g, 0);
        HeroInfo h = ReadHero();
        auto get_actor = (uintptr_t(*)(void*))natives::Get(natives::GetActor);
        for (uint32_t bh : g.bots) {
            uintptr_t e = 0;
            if (get_actor) memory::SafeCall(get_actor, &bh, e);
            float hp = -1, p[3] = {};
            uintptr_t tr = 0;
            entity::ReadBotHealth(e, hp);
            if (e && memory::Read(e, tr) && tr) memory::SafeRead(tr + 0x30, p, sizeof(p));
            float d = h.ok ? sqrtf(powf(p[0] - h.pos[0], 2) + powf(p[1] - h.pos[1], 2) + powf(p[2] - h.pos[2], 2)) : -1;
            LOG("    %08x entity=0x%llx dist=%.1f health=%.1f", bh, (unsigned long long)e, d, hp);
        }
    } else if (cmd == "grpalert") {
        // grpalert <0xGroup | bot target> [alerter: hero|hHandle]: Group::Alert(group, alerter, 1)
        std::string a, alerter = "hero";
        in >> a >> alerter;
        GroupInfo g;
        if (!ReadGroup(ParseAddr(a), g)) {
            uint32_t th = 0;
            ParseEntity(a, &th);
            bool ok = false;
            for (const GroupInfo& c : ScanGroups())
                if (th && std::find(c.bots.begin(), c.bots.end(), th) != c.bots.end()) { g = c; ok = true; break; }
            if (!ok) { LOG("grpalert: no group for %s", a.c_str()); return; }
        }
        uint32_t ah = 0;
        ParseEntity(alerter, &ah);
        LogGroup(g, 0);
        events::AlertGroup(g.addr, ah);
    } else if (cmd == "botdata") {
        // botdata <target>: BotData-shaped structs in the bot's components (see entity::FindBotData)
        std::string target;
        in >> target;
        uintptr_t e = ParseEntity(target);
        if (!e) { LOG("botdata: no such target"); return; }
        auto hits = entity::FindBotData(e);
        for (size_t i = 0; i < hits.size(); i++) {
            const auto& h = hits[i];
            uint8_t b[0xA0] = {};
            uintptr_t vt = 0;
            memory::SafeRead(h.addr, b, sizeof(b));
            memcpy(&vt, b, 8);
            LOG("  [%zu]%s %s %s+%x @%llx vt=exe+%llx engaged=%u intro=%u%u%u%u%u speed=%.2f wander=%u aggr=%.2f def=%.2f share=%u proj=%u qte=%u",
                i, h.exact ? " EXACT" : "", h.component.c_str(), h.depth ? (h.inline_off ? "ptr(+2c0)" : "ptr") : "inline",
                h.depth ? h.ptr_off : h.inline_off,
                (unsigned long long)h.addr, (unsigned long long)scan::Rva(vt), b[8], b[9], b[10], b[11], b[12], b[13],
                *(float*)(b + 0x10), b[0x14], *(float*)(b + 0x78), *(float*)(b + 0x7C), b[0x80], b[0x81], b[0x98]);
        }
        LOG("botdata: %zu candidates", hits.size());
    } else if (cmd == "run") {
        // run [campaign|faction <id>|bossrush|horde] [riot[=N]] [shuffle] [seed=N] [wave=N] [verified] [fast|level_s=N] [god]
        director::Options opt;
        for (std::string tok; in >> tok; ) {
            if (tok == "campaign" || tok == "bossrush" || tok == "horde") opt.mode = tok;
            else if (tok.rfind("max=", 0) == 0) opt.horde_max = std::clamp(atoi(tok.c_str() + 4), 4, 120);
            else if (tok == "faction") { opt.mode = "faction"; in >> opt.faction; }
            else if (tok == "riot") opt.riot = true;
            else if (tok.rfind("riot=", 0) == 0) { opt.riot = true; opt.riot_max = std::clamp(atoi(tok.c_str() + 5), 1, 80); }
            else if (tok == "shuffle") opt.shuffle = true;
            else if (tok.rfind("seed=", 0) == 0) opt.seed = (uint32_t)strtoul(tok.c_str() + 5, nullptr, 10);
            else if (tok.rfind("wave=", 0) == 0) opt.first_wave = std::max(1, atoi(tok.c_str() + 5));
            else if (tok == "verified") opt.allow_unverified_bosses = false;
            else if (tok == "unverified") opt.allow_unverified_bosses = true;
            else if (tok == "noboons") opt.roguelike = false;
            else if (tok == "fast") opt.horde_level_ms = 15000;
            else if (tok.rfind("level_s=", 0) == 0) opt.horde_level_ms = std::clamp(atoi(tok.c_str() + 8), 5, 600) * 1000;
            else if (tok == "god") opt.god = true;
            else if (roster::FindFaction(tok)) { opt.mode = "faction"; opt.faction = tok; }
            else LOG("run: ignoring '%s'", tok.c_str());
        }
        director::Start(opt, HeroPosNow);
    } else if (cmd == "plan") {
        // plan [from] [to] [same options as run]: logs the wave plans without spawning
        int from = 1, to = 16;
        in >> from >> to;
        in.clear();
        director::Options opt;
        for (std::string tok; in >> tok; ) {
            if (tok == "bossrush") opt.mode = tok;
            else if (tok == "riot") opt.riot = true;
            else if (tok == "shuffle") opt.shuffle = true;
            else if (tok.rfind("seed=", 0) == 0) opt.seed = (uint32_t)strtoul(tok.c_str() + 5, nullptr, 10);
            else if (roster::FindFaction(tok)) { opt.mode = "faction"; opt.faction = tok; }
        }
        director::PreviewPlans(opt, from, to);
    } else if (cmd == "portal") {
        // portal setdock (Ryker's dock) | setraft (Raft dock) | enter | leave | on | off | show
        std::string sub;
        in >> sub;
        if (sub == "setdock") portal::SetDockHere();
        else if (sub == "setraft" || sub == "setexit") portal::SetExitHere();
        else if (sub == "enter") portal::Enter();
        else if (sub == "leave") portal::Leave();
        else if (sub == "on" || sub == "off") portal::Enable(sub == "on");
        else portal::LogConfig();
    } else if (cmd == "stress") {
        // stress [step=10] [max=200] [engaged] [floor=25] [faction|mixed] [seed=N] [start=N] [bodies=%] | stress stop
        std::string a;
        stress::Params p;
        std::vector<std::string> toks;
        for (std::string tok; in >> tok; ) toks.push_back(tok);
        if (!toks.empty() && toks[0] == "stop") { stress::Stop(); return; }
        int nums = 0;
        for (auto& tok : toks) {
            if (tok == "engaged") p.engaged = true;
            else if (tok.rfind("floor=", 0) == 0) p.min_hz = (float)atof(tok.c_str() + 6);
            else if (tok.rfind("seed=", 0) == 0) p.seed = (uint32_t)strtoul(tok.c_str() + 5, nullptr, 10);
            else if (tok.rfind("start=", 0) == 0) p.start = std::max(0, atoi(tok.c_str() + 6));
            else if (tok.rfind("bodies=", 0) == 0) p.bodies = std::clamp(atoi(tok.c_str() + 7), 0, 100);
            else if (tok == "mixed") p.mixed = true;
            else if (roster::FindFaction(tok)) p.faction = tok;
            else if (isdigit((unsigned char)tok[0])) { (nums++ == 0 ? p.step : p.max) = atoi(tok.c_str()); }
        }
        stress::Start(p, HeroPosNow);
    } else if (cmd == "boss") {
        // boss list | boss <name> [dist=15]: spawn one boss engaged near the hero to check it fights standalone
        // boss verify <name> 1|0: mark it (persisted; "run verified" only uses verified bosses)
        std::string name;
        in >> name;
        if (name == "list" || name.empty()) {
            for (const auto& b : roster::Bosses())
                LOG("  %-18s %-48s hp %4d %s", b.name, b.path, b.base_health, b.verified ? "VERIFIED" : "");
            return;
        }
        if (name == "verify") {
            std::string who; int v = 1;
            in >> who >> v;
            const roster::Boss* b = roster::FindBoss(who);
            if (!b) { LOG("boss: unknown '%s' (use the short actor name, e.g. rhino)", who.c_str()); return; }
            WritePrivateProfileStringA("bosses", b->name, v ? "1" : "0", (g_dir + "\\EndlessMode.ini").c_str());
            roster::SetVerified(b->name, v != 0);
            LOG("boss: %s marked %s", b->name, v ? "verified" : "broken");
            return;
        }
        float dist = 15.0f;
        std::string calm;
        in >> dist >> calm;
        const roster::Boss* b = roster::FindBoss(name);
        if (!b) { LOG("boss: unknown '%s' (boss list)", name.c_str()); return; }
        HeroInfo h = ReadHero();
        if (!h.ok) { LOG("boss: hero not available"); return; }
        spawn::Preload(b->path);
        if (b->head && *b->head) spawn::Preload(b->head);
        LOG("boss: loading %s (15 s)...", b->name);
        Sleep(15000);   // let the body and its head stream in (otherwise: no mover / no head, then a crash)
        h = ReadHero();
        spawn::Vec3 pos{ h.pos[0] + dist, h.pos[1] + 1.0f, h.pos[2] };
        uintptr_t e = spawn::SpawnActor(b->path, pos, 180.0f, calm != "calm");
        LOG("boss: %s -> entity %llx", b->name, (unsigned long long)e);
        if (e) {
            g_spawned.push_back(e);
            hud::Show(std::string(b->name) + " spawned", 3.0f);
        }
    } else if (cmd == "hud") {
        // hud <text...> | hud clear | hud slot <n> <text...>
        std::string rest;
        std::getline(in, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        if (rest == "clear") hud::Clear();
        else if (rest.rfind("slot ", 0) == 0) {
            std::istringstream s(rest.substr(5));
            int slot = 0;
            s >> slot;
            std::string text;
            std::getline(s, text);
            hud::Show(text, 5.0f, slot);
        } else hud::Show(rest, 5.0f);
    } else if (cmd == "stats") {
        stats::Live l = stats::Current();
        LOG("stats: active=%d wave=%d kills=%d hits_taken=%d damage_taken=%.0f hits_dealt=%d infight_hits=%d (%.0f dmg)",
            stats::Active(), l.wave, l.kills, l.hits_taken, l.damage_taken, l.hits_dealt, l.infight_hits, l.infight_damage);
    } else if (cmd == "fps") {
        // fps [seconds=3]: tick rate (frame-rate proxy) over a window
        int secs = 3;
        in >> secs;
        gamethread::TakeFrameStats();
        Sleep(std::clamp(secs, 1, 30) * 1000);
        auto fs = gamethread::TakeFrameStats();
        LOG("fps: %.1f ticks/s over %.1f s, worst frame %.1f ms", fs.avg_hz, fs.seconds, fs.worst_ms);
    } else if (cmd == "pad") {
        // pad [seconds=5]: log PlayStation controller state (HID) a few times a second
        int secs = 5;
        in >> secs;
        LOG("pad: connected=%d", psinput::Connected());
        bool last = false;
        for (int i = 0; i < std::clamp(secs, 1, 30) * 10; i++) {
            bool t = psinput::TriangleDown();
            if (t != last) LOG("pad: triangle %s", t ? "DOWN" : "up");
            last = t;
            Sleep(100);
        }
    } else if (cmd == "preload") {
        // preload <actor path>: LoadAssetByHash only (no spawn), to let a heavy actor's dependencies stream in first
        std::string path;
        in >> path;
        auto hash_fn = (uint64_t* (*)(uint64_t*, const char*))natives::Get(natives::ConvertStringToHash);
        auto load = (uintptr_t(*)(uintptr_t, uint64_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))natives::Get(natives::LoadAssetByHash);
        uintptr_t mgr = natives::Get(natives::ActorAssetManager);
        if (!hash_fn || !load || !mgr || path.empty()) { LOG("preload: unavailable"); return; }
        gamethread::Post([=] {
            uint64_t h = 0;
            hash_fn(&h, path.c_str());
            uintptr_t a = load(mgr, h, 0, 0, 0, 0, 0);
            LOG("preload: %s -> asset %llx", path.c_str(), (unsigned long long)a);
        });
    } else if (cmd == "god") {
        // god on|off: keep the hero's health full (for testing), via Health::SetHealth on the game thread every 0.3 s
        static std::atomic<bool> god = false;
        std::string mode;
        in >> mode;
        bool on = mode != "off";
        if (on && !god.exchange(true)) {
            std::thread([] {
                while (god) {
                    gamethread::Post([] {
                        using SetFn = void (*)(uintptr_t, float);
                        auto set = (SetFn)natives::Get(natives::HealthSet);
                        HeroInfo h = ReadHero();
                        uintptr_t hc = h.ok ? entity::FindComponent(h.actor, "HeroHealth") : 0;
                        float mx = 0;
                        if (set && hc && memory::Read(hc + 0x80, mx) && mx > 0) set(hc, mx);
                    });
                    Sleep(300);
                }
            }).detach();
        } else if (!on) {
            god = false;
        }
        LOG("god: %s", on ? "on" : "off");
    } else if (cmd == "fade") {
        // fade out|in [seconds=0.5]
        std::string which;
        float secs = 0.5f;
        in >> which >> secs;
        bool ok = which == "in" ? hud::FadeIn(secs) : hud::FadeOut(secs);
        LOG("fade %s %.2f: %s", which.c_str(), secs, ok ? "sent" : "unavailable");
    } else if (cmd == "typename") {
        // typename <rva>...: name of a component type descriptor in .data/.bss (name pointer at +0x60)
        for (std::string a; in >> a; ) {
            uintptr_t desc = scan::FromRva(strtoull(a.c_str(), nullptr, 16)), np = 0;
            char name[96] = "?";
            if (memory::Read(desc + 0x60, np) && np) {
                for (int i = 0; i < 95; i++) { if (!memory::Read(np + i, name[i]) || !name[i]) { name[i] = 0; break; } }
                name[95] = 0;
            }
            LOG("typename exe+%s: %s", a.c_str(), name);
        }
    } else if (cmd == "near") {
        // near [radius=15] [component filter]: every live actor within radius (whole actor table, not just bots),
        // with its components (or only those matching the filter, case-insensitive).
        float radius = 15.0f;
        std::string filter;
        in >> radius >> filter;
        for (auto& ch : filter) ch = (char)tolower((unsigned char)ch);
        HeroInfo h = ReadHero();
        uintptr_t base = 0;
        uint32_t count = 0;
        if (!h.ok || !memory::Read(natives::Get(natives::ActorTable), base) || !base ||
            !memory::Read(natives::Get(natives::ActorCount), count)) { LOG("near: unavailable"); return; }
        int shown = 0;
        for (uint32_t i = 0; i < count && i < 0x100000 && shown < 60; i++) {
            uintptr_t e = base + (uintptr_t)i * 0xC0, tr = 0;
            uint16_t gen = 0;
            if (!memory::Read(e + 0x10, gen) || (gen & 0x7FF) == 0) continue;
            float p[3];
            if (!memory::Read(e, tr) || !tr || !memory::SafeRead(tr + 0x30, p, sizeof(p))) continue;
            float d = sqrtf(powf(p[0] - h.pos[0], 2) + powf(p[1] - h.pos[1], 2) + powf(p[2] - h.pos[2], 2));
            if (d > radius || e == h.actor) continue;
            std::string comps, hit;
            int n = 0;
            entity::ForEachComponent(e, [&](const std::string& name, uintptr_t inst) {
                n++;
                std::string lower = name;
                for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
                if (filter.empty() ? comps.size() < 400 : lower.find(filter) != std::string::npos) {
                    char b[96];
                    snprintf(b, sizeof(b), " %s@%llx", name.c_str(), (unsigned long long)inst);
                    (filter.empty() ? comps : hit) += b;
                }
            });
            if (!filter.empty() && hit.empty()) continue;
            uint64_t asset = 0;
            memory::Read(e + 0x30, asset);
            LOG("  actor 0x%llx h%08x dist=%.1f pos=(%.1f, %.1f, %.1f) comps=%d asset=%llx%s", (unsigned long long)e,
                entity::HandleOf(e), d, p[0], p[1], p[2], n, (unsigned long long)asset,
                (filter.empty() ? comps : hit).c_str());
            shown++;
        }
        LOG("near: %d actors within %.0f m%s%s", shown, radius, filter.empty() ? "" : " matching ", filter.c_str());
    } else if (cmd == "inv") {
        // inv find | can <id> [n] | count <id> [arg] | add <id> [n] | remove <id> [n]  (id = 16 hex digits, asset id)
        std::string sub = "find", hex;
        int n = 1;
        in >> sub >> hex >> n;
        if (sub == "watch") { inventory::Watch(hex != "off"); return; }
        uint64_t id = hex.empty() ? 0 : strtoull(hex.c_str(), nullptr, 16);
        if (sub == "text") {   // inv text <id>: the game's own name/description/icon/preview for an item
            inventory::ItemText t = inventory::Text(id);
            LOG("inv text %016llx: %s name='%s' desc='%s' icon='%s' image='%s' video='%s'", (unsigned long long)id, t.ok ? "ok" : "NOT FOUND",
                t.name.c_str(), t.desc.c_str(), t.icon.c_str(), t.image.c_str(), t.video.c_str());
            return;
        }
        if (!gamethread::Installed()) { LOG("inv: hook not installed"); return; }
        gamethread::Post([sub, id, n] {
            HeroInfo h = ReadHero();
            if (!h.ok) { LOG("inv: no hero"); return; }
            inventory::Command(sub.c_str(), id, n, h.actor);
        });
    } else if (cmd == "equip") {
        // equip <asset id hex> [slot]: through the game's equip path (no slot: by category, like the menu's auto-equip)
        std::string hex;
        int slot = -1;
        in >> hex >> slot;
        uint64_t id = strtoull(hex.c_str(), nullptr, 16);
        if (!gamethread::Installed()) { LOG("equip: hook not installed"); return; }
        gamethread::Post([id, slot] { inventory::Equip(id, slot); });
    } else if (cmd == "unequip") {
        // unequip <asset id hex>: through the game's unequip path
        std::string hex;
        in >> hex;
        uint64_t id = strtoull(hex.c_str(), nullptr, 16);
        if (!gamethread::Installed()) { LOG("unequip: hook not installed"); return; }
        gamethread::Post([id] { inventory::Unequip(id); });
    } else if (cmd == "uiroot") {
        // uiroot [bytes=0x800]: the HUD root's pointer fields with their C++ class names (UI hunt)
        std::string b;
        in >> b;
        unsigned bytes = b.empty() ? 0x800 : (unsigned)strtoul(b.c_str(), nullptr, 16);
        ui::DumpRoot(0, bytes);
    } else if (cmd == "uiobj") {
        // uiobj <addr hex> [bytes hex]: pointer fields of any object with class names
        std::string a, b;
        in >> a >> b;
        ui::DumpObject(ParseAddr(a), b.empty() ? 0x400 : (unsigned)strtoul(b.c_str(), nullptr, 16));
    } else if (cmd == "uicall") {
        // uicall <root offset | address>[:<AS object offset>] <Method> [args...] (hex; AS offset default 320):
        // whole numbers -> int, decimals -> number, true/false, or strings ("quoted", \n ok)
        std::string where, method;
        in >> where >> method;
        std::string rest;
        std::getline(in, rest);
        std::vector<ui::Arg> args;
        for (size_t i = 0; i < rest.size();) {
            if (rest[i] == ' ') { i++; continue; }
            ui::Arg a;
            if (rest[i] == '"') {
                size_t j = i + 1;
                std::string str;
                for (; j < rest.size() && rest[j] != '"'; j++) {
                    if (rest[j] == '\\' && j + 1 < rest.size() && rest[j + 1] == 'n') { str += '\n'; j++; }
                    else str += rest[j];
                }
                a.kind = ui::Arg::String;
                a.str = str;
                i = j + 1;
            } else {
                size_t j = rest.find(' ', i);
                std::string tok = rest.substr(i, j == std::string::npos ? std::string::npos : j - i);
                i = j == std::string::npos ? rest.size() : j;
                char* end = nullptr;
                double v = strtod(tok.c_str(), &end);
                if (tok == "true" || tok == "false") { a.kind = ui::Arg::Bool; a.b = tok == "true"; }
                else if (end && *end == 0) {
                    a.kind = tok.find('.') == std::string::npos ? ui::Arg::Int : ui::Arg::Number;
                    a.num = v;
                }
                else { a.kind = ui::Arg::String; a.str = tok; }
            }
            args.push_back(a);
        }
        unsigned as_off = 0x320;
        if (auto colon = where.find(':'); colon != std::string::npos) {
            as_off = (unsigned)strtoul(where.c_str() + colon + 1, nullptr, 16);
            where = where.substr(0, colon);
        }
        uintptr_t w = strtoull(where.c_str(), nullptr, 16);
        std::thread([w, as_off, method, args] { ui::Call(w, as_off, method, args); }).detach();
    } else if (cmd == "timescale") {
        // timescale <target> [rate]: world time scale through the game's TimeScaleSystem (channel 5); no args: read
        float t = -1, r = 50.0f;
        in >> t >> r;
        if (t < 0) LOG("timescale: current %.4f", timescale::Current());
        else timescale::Set(t, r);
    } else if (cmd == "hudtest") {
        // hudtest <slot> <text...>: show text in a HUD slot (e.g. to check [BTN_X] icon tokens)
        int slot = 0;
        in >> slot;
        std::string text, out;
        std::getline(in, text);
        for (size_t i = 0; i < text.size(); i++) {   // "\n" in the command = a line break
            if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') { out += '\n'; i++; }
            else out += text[i];
        }
        hud::Show(out, 6.0f, slot);
    } else if (cmd == "boons") {
        // boons: current run loadout, boons and curses
        for (const auto& line : boons::Summary()) LOG("  %s", line.c_str());
        LOG("boons: active=%d dmg out x%.2f, in x%.2f", boons::Active(), boons::OutgoingMult(), boons::IncomingMult());
    } else if (cmd == "runloadout") {
        // runloadout restore: put back the snapshot from EndlessMode_loadout.txt (only when no run is active)
        std::string sub;
        in >> sub;
        if (sub == "restore") std::thread([] { runloadout::Recover(); }).detach();
    } else if (cmd == "lock") {
        // lock on|off: refuse menu equips (roguelike loadout lock)
        std::string v;
        in >> v;
        inventory::Lock(v != "off");
    } else if (cmd == "wp") {
        // wp set <addr|exe+rva> | dump | clear  (hardware write watchpoint, see watchpoint.h)
        std::string sub = "dump", a;
        in >> sub >> a;
        if (sub == "set") watchpoint::Set(ParseAddr(a));
        else if (sub == "clear") watchpoint::Clear();
        else watchpoint::Dump();
    } else if (cmd == "vscan") {
        // vscan find <hex64> | keep <hex64> | list  (see camscan.h)
        std::string sub = "list", hex;
        in >> sub >> hex;
        vscan::Command(sub.c_str(), hex.empty() ? 0 : strtoull(hex.c_str(), nullptr, 16));
    } else if (cmd == "camscan") {
        // camscan start | moved | same | list: find the camera position by elimination (see camscan.h)
        std::string sub = "list";
        in >> sub;
        HeroInfo h = ReadHero();
        if (!h.ok) { LOG("camscan: no hero"); return; }
        camscan::Command(sub.c_str(), h.pos);
    } else if (cmd == "parade") {
        // parade [faction ids... | all]: one calm enemy of every type, in rows of 8 (2.5 m apart, rows 3 m apart) from 8 m in
        // front of the camera, to check models by eye (white cubes). Without arguments: the types the running Horde/campaign has in
        // play. Each slot is logged left to right. Clean up with `despawn all`.
        std::vector<std::string> paths;
        for (std::string tok; in >> tok; ) {
            for (const auto& f : roster::Factions())
                if (tok == "all" || tok == f.id)
                    for (const auto& u : f.units)
                        if (std::find(paths.begin(), paths.end(), u.path) == paths.end()) paths.push_back(u.path);
        }
        if (paths.empty()) paths = director::UnitsInPlay();
        if (paths.empty()) { LOG("parade: nothing in play; name factions or 'all'"); return; }
        director::HeroPos h = HeroPosNow();
        if (!h.ok) { LOG("parade: no hero"); return; }
        float fx = h.cam_ok ? h.cam_fx : h.fwd_x, fz = h.cam_ok ? h.cam_fz : h.fwd_z, len = sqrtf(fx * fx + fz * fz);
        if (len < 0.01f) { fx = 1; fz = 0; len = 1; }
        fx /= len; fz /= len;
        float rx = -fz, rz = fx;   // to the right of the view
        float heading = atan2f(-fz, -fx) * 57.29578f;   // facing back at the hero
        DWORD t0 = GetTickCount();
        for (auto& p : paths) spawn::Ready(p);
        for (size_t i = 0; i < paths.size(); i++) {
            while (!spawn::Ready(paths[i], 0) && GetTickCount() - t0 < 30000) Sleep(100);
            int row = (int)i / 8, col = (int)i % 8;
            float off = (col - 3.5f) * 2.5f, depth = 8.0f + row * 3.0f;
            spawn::Vec3 pos{ h.x + fx * depth + rx * off, h.y + 1.0f, h.z + fz * depth + rz * off };
            uintptr_t e = spawn::SpawnActor(paths[i], pos, heading, false);
            if (e) g_spawned.push_back(e);
            LOG("parade: row %d (from the front), #%d from the left: %s%s", row + 1, col + 1,
                roster::ShortName(paths[i].c_str()).c_str(), e ? "" : " FAILED");
        }
        hud::Show("Parade: " + std::to_string(paths.size()) + " enemy types lined up", 4.0f);
    } else if (cmd == "banner") {
        // banner [type] "title" "name": the activity-complete banner via the game's wrapper (UI hunt)
        int type = 0;
        std::string title = "Successfully Resolved", name = "Endless Mode";
        if (in >> std::ws && isdigit(in.peek())) in >> type;
        in >> std::quoted(title) >> std::quoted(name);
        ui::ShowActivityBanner(title, name, type);
    } else if (cmd == "toast") {
        // toast a b "s1" "s2" "s3": the award-tracker toast via the game's wrapper (UI hunt)
        int a = 3, b = 10;
        std::string s1 = "Endless Mode", s2 = "Wave 3", s3 = "";
        in >> a >> b >> std::quoted(s1) >> std::quoted(s2) >> std::quoted(s3);
        ui::ShowAwardToast(a, b, s1, s2, s3);
    } else if (cmd == "msgtype") {
        // msgtype <type> ["text"]: the game's typed HUD message (exe+77db70); 7 = medium message (UI hunt)
        int type = 7;
        std::string text;
        in >> type >> std::quoted(text);
        ui::ShowTypedMessage(type, text);
    } else if (cmd == "uilog") {
        // uilog on|off: record the game's UI calls to scripts\EndlessMode_uilog.txt (UI hunt)
        std::string mode;
        in >> mode;
        uilog::Enable(mode != "off");
    } else if (cmd == "gfx") {
        // gfx selftest | wave <cur> <max> [icon] | complete "label" | panel preview|score|results [score] | hide [activity]
        // Drives the game's own panels with built data objects (UI hunt, 2026-10-15; docs/UI_HUNT.md recordings).
        using gfx::Node;
        std::string sub;
        in >> sub;
        if (sub == "selftest") { gfx::SelfTest(); return; }
        if (sub == "results") {   // a fake results panel (stays until you press Continue / Interact)
            score::Results r;
            r.rows = { { "Enemy Score", 9800 }, { "Style Bonus", 4200 }, { "Combo Bonus", 2100 }, { "Level Bonus", 4000 } };
            for (auto& row : r.rows) r.total += row.value;
            r.best = 21000; r.final_rank = 4;
            panels::Results(r, 8);
            return;
        }
        if (sub == "live") {      // a fake live score panel
            int sc = 5400, rank = 3;
            in >> sc >> rank;
            panels::LiveScore(sc, rank);
            return;
        }
        if (sub == "hidepanel") { panels::Hide(); return; }
        if (sub == "caps") { gfx::LogCaptured(); return; }
        if (sub == "text") {   // gfx text <KEY> "<value>" [require] [forbid]: change a captured panel text; "gfx text <KEY> restore"
            std::string key, val, req, forb;
            in >> key >> std::quoted(val) >> req >> forb;
            int n = val == "restore" ? gfx::RestoreText(key, req == "-" ? "" : req, forb == "-" ? "" : forb)
                                     : gfx::SetText(key, val, req == "-" ? "" : req, forb == "-" ? "" : forb);
            LOG("gfx: text %s -> %d object(s)", key.c_str(), n);
            return;
        }
        if (sub == "dock") {   // gfx dock: the dock panel as it will look (campaign), without walking there
            panels::Records("Ryker's Island - Campaign", 1230, 426,
                            { { "Reach wave 15", 15, 15 }, { "Reach wave 20", 19, 20 }, { "Defeat 500 enemies in one run", 426, 500 } }, "START");
            return;
        }
        if (sub == "wave") {
            int cur = 1, max = 6;
            std::string icon = "icon_map_fisk_base";
            in >> cur >> max >> icon;
            Node d = Node::Obj({ { "icon", Node::Of(icon) }, { "currentWave", Node::Of(cur) }, { "maxWave", Node::Of(max) } });
            gfx::Message(21, "", &d);
            return;
        }
        if (sub == "complete") {
            std::string label = "Ryker's Island";
            in >> std::quoted(label);
            Node d = Node::Obj({ { "icon", Node::Of("icon_map_combat_challenge") } });
            gfx::Message(13, label, &d);
            return;
        }
        if (sub == "hide") {
            int activity = 1, type = 2;
            in >> activity >> type;
            gfx::Invoke(0x1C0, 0x6E8, "HideActivity", { Node::Obj({ { "activity", Node::Of(activity) }, { "panelType", Node::Of(type) } }) });
            return;
        }
        if (sub == "panel") {
            std::string which = "preview";
            int score = 12345;
            in >> which >> score;
            const char* icon = "icon_map_combat_challenge";
            Node token = Node::Obj({ { "label", Node::Of("Challenge Tokens") }, { "icon", Node::Of("resource_challenge_token") },
                                     { "value", Node::Of(1) }, { "id", Node::U(0) }, { "totalCount", Node::U(12) } });
            Node data;
            int activity = 1, type = 0;
            if (which == "preview") {          // the hideout circle preview (activity 3, panelType 0)
                activity = 3; type = 0;
                Node sub1 = Node::Obj({ { "label", Node::Of("Reach Horde level 10") }, { "currentCount", Node::Of(8) },
                                        { "maxCount", Node::Of(10) }, { "reward", Node::Arr() } });
                data = Node::Obj({ { "label", Node::Of("Ryker's Island - Horde") }, { "icon", Node::Of(icon) },
                                   { "time", Node::Of(754.5) }, { "enemiesDefeated", Node::Of(212) },
                                   { "subObjectives", Node::Arr({ sub1 }) }, { "rewards", Node::Arr({ token }) } });
            } else if (which == "score") {     // the Taskmaster live score panel (activity 1, panelType 1, type 2)
                activity = 1; type = 1;
                data = Node::Obj({ { "type", Node::Of(2) }, { "label", Node::Of("SPECTACULAR") }, { "icon", Node::Of(icon) },
                                   { "score", Node::Of(score) }, { "scoreItems", Node::Arr() } });
            } else {                           // the expanded results panel (activity 1, panelType 2)
                activity = 1; type = 2;
                auto row = [](const char* l, int v) { return Node::Obj({ { "label", Node::Of(l) }, { "value", Node::Of(v) } }); };
                data = Node::Obj({ { "label", Node::Of("Horde Mode") }, { "icon", Node::Of(icon) },
                                   { "score", Node::U((unsigned)score) }, { "highScore", Node::U(68702) },
                                   { "summaryData", Node::Arr({ row("Base Enemy Score", 9800), row("Style Bonus", 4200),
                                                                row("Combo Bonus", 1650), row("Survival Bonus", 3100) }) },
                                   { "rewards", Node::Arr() }, { "noRewards", Node::Of(true) }, { "awardedRewards", Node::Arr() },
                                   { "rewardTiers", Node::Arr() } });
            }
            gfx::Invoke(0x1C0, 0x6E8, "Refresh", { Node::Obj({ { "activity", Node::Of(activity) }, { "panelType", Node::Of(type) },
                                                               { "panelData", data } }) });
            return;
        }
        LOG("gfx: unknown subcommand '%s'", sub.c_str());
    } else if (cmd == "preloads") {
        spawn::LogPreloads();
    } else if (cmd == "release") {
        // release: drop every asset reference the mod holds (e.g. if a load hangs after a long run)
        if (director::Running() || stress::Running()) { LOG("release: stop the run / stress test first"); return; }
        spawn::ReleaseAll();
    } else if (cmd == "findref") {
        // findref <entity> <hex value>: search the entity's 0xC0 record and its components' first 0x400 bytes for the
        // value as a u32 or u64 (e.g. a head entity's handle or address).
        std::string a, v;
        in >> a >> v;
        uintptr_t e = ParseAddr(a);
        uint64_t val = strtoull(v.c_str() + (v.rfind("0x", 0) == 0 ? 2 : 0), nullptr, 16);
        auto scan_block = [&](uintptr_t at, size_t len, const std::string& what) {
            std::vector<uint8_t> buf(len);
            if (!memory::SafeRead(at, buf.data(), len)) return;
            for (size_t off = 0; off + 4 <= len; off += 4) {
                uint64_t q = 0; uint32_t d = 0;
                memcpy(&d, &buf[off], 4);
                if (off + 8 <= len) memcpy(&q, &buf[off], 8);
                if ((val > 0xFFFFFFFF && q == val) || (val <= 0xFFFFFFFF && d == (uint32_t)val))
                    LOG("  %s+0x%zx (%llx)", what.c_str(), off, (unsigned long long)(at + off));
            }
        };
        scan_block(e, 0xC0, "entity");
        entity::ForEachComponent(e, [&](const std::string& name, uintptr_t inst) { scan_block(inst, 0x400, name); });
        LOG("findref: done");
    } else if (cmd == "gthero") {
        if (!gamethread::Installed()) { LOG("gthero: hook not installed"); return; }
        gamethread::Post([] { LogHero("game thread"); });
    } else {
        LOG("unknown command '%s'", cmd.c_str());
    }
}

static void PollCommands() {
    const std::string path = g_dir + "\\EndlessMode.cmd";
    std::vector<std::string> lines;
    {
        std::ifstream f(path);
        if (!f.good()) return;
        for (std::string l; std::getline(f, l); ) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) lines.push_back(l);
        }
    }
    if (lines.empty()) return;
    // Truncate so each command runs once.
    std::ofstream(path, std::ios::trunc).close();
    for (auto& l : lines) RunCommand(l);
}

static void Main() {
    g_dir = ModuleDir();
    logging::Init(g_dir + "\\EndlessMode.log");
    LOG("EndlessMode loaded from %s", g_dir.c_str());

    const auto& m = scan::Game();
    LOG("game module base=%llx .text=%llx size=%llx", (unsigned long long)m.base,
        (unsigned long long)m.text, (unsigned long long)m.text_size);

    LOG("resolving natives...");
    int ok = natives::ResolveAll();
    LOG("%d/%d natives at expected addresses", ok, (int)natives::Count);

    // Everything a player needs works without dev commands: hook the game thread, hook damage telemetry,
    // and start the portal (dock pad / hotkeys).
    if (gamethread::Install()) {
        stats::InstallHooks();
        guards::Install();
        LoadBossFlags();
        gfx::InstallTextCapture();   // before the HUD loads its texts (panel titles/buttons can then be changed)
        portal::Start(HeroPosNow);
    } else {
        LOG("game-thread hook failed; Endless Mode is disabled (dev commands still work)");
    }
    LOG("ready; write commands to %s\\EndlessMode.cmd", g_dir.c_str());

    for (;;) {
        PollCommands();
        Sleep(250);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}

extern "C" __declspec(dllexport) void script_enable() {
    std::thread(Main).detach();
}
