#include "camscan.h"

#include "log.h"
#include "memory.h"
#include "scan.h"

#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>

namespace camscan {
    struct Cand {
        uintptr_t addr;
        float v[3];
    };
    static std::vector<Cand> g_cands;
    constexpr size_t kMaxCands = 2000000;

    static float Dist(const float a[3], const float b[3]) {
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }

    static bool InRange(const float v[3], const float hero[3]) {
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) return false;
        float d = Dist(v, hero);
        return d > 1.0f && d < 8.0f;
    }

    static void Start(const float hero[3]) {
        g_cands.clear();
        size_t regions = 0, bytes = 0;
        MEMORY_BASIC_INFORMATION mbi;
        for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
            if (mbi.RegionSize > 2048ull << 20) continue;   // skip giant pools (textures, streaming)
            regions++;
            bytes += mbi.RegionSize;
            // Read in 1 MB chunks (8 bytes of overlap so a float3 straddling a chunk edge is still seen).
            constexpr size_t kChunk = 1 << 20;
            std::vector<float> buf(kChunk / 4 + 2);
            for (size_t off = 0; off < mbi.RegionSize && g_cands.size() < kMaxCands; off += kChunk) {
                size_t len = std::min(kChunk + 8, mbi.RegionSize - off);
                uintptr_t base = (uintptr_t)mbi.BaseAddress + off;
                if (!memory::SafeRead(base, buf.data(), len)) continue;
                size_t nf = len / 4;
                for (size_t i = 0; i + 2 < nf && i < kChunk / 4; i++) {
                    if (fabsf(buf[i] - hero[0]) > 8.0f || fabsf(buf[i + 2] - hero[2]) > 8.0f) continue;
                    if (!InRange(&buf[i], hero)) continue;
                    g_cands.push_back(Cand{ base + i * 4, { buf[i], buf[i + 1], buf[i + 2] } });
                    if (g_cands.size() >= kMaxCands) break;
                }
            }
            if (g_cands.size() >= kMaxCands) break;
        }
        LOG("camscan: %zu candidates in %zu regions (%zu MB) near (%.1f, %.1f, %.1f)", g_cands.size(), regions,
            bytes >> 20, hero[0], hero[1], hero[2]);
    }

    static void Filter(bool want_moved, const float hero[3]) {
        size_t kept = 0;
        for (auto& c : g_cands) {
            float v[3];
            if (!memory::SafeRead(c.addr, v, sizeof(v)) || !InRange(v, hero)) continue;
            bool moved = Dist(v, c.v) > 0.5f;
            if (moved != want_moved) continue;
            memcpy(c.v, v, sizeof(v));
            g_cands[kept++] = c;
        }
        g_cands.resize(kept);
        LOG("camscan: %zu candidates left (kept %s)", kept, want_moved ? "moved" : "unchanged");
    }

    static void List(const float hero[3]) {
        size_t n = 0;
        for (auto& c : g_cands) {
            if (n++ >= 25) break;
            float row[16];
            if (!memory::SafeRead(c.addr - 0x30, row, sizeof(row))) continue;
            LOG("  %llx pos=(%.2f, %.2f, %.2f) d=%.2f | -30: %.2f %.2f %.2f %.2f | -20: %.2f %.2f %.2f %.2f | -10: %.2f %.2f %.2f %.2f | +0: %.2f %.2f %.2f %.2f",
                (unsigned long long)c.addr, c.v[0], c.v[1], c.v[2], Dist(c.v, hero), row[0], row[1], row[2], row[3],
                row[4], row[5], row[6], row[7], row[8], row[9], row[10], row[11], row[12], row[13], row[14], row[15]);
        }
        LOG("camscan: %zu candidates", g_cands.size());
    }

    void Command(const char* sub, const float hero[3]) {
        if (!strcmp(sub, "start")) Start(hero);
        else if (!strcmp(sub, "moved")) Filter(true, hero);
        else if (!strcmp(sub, "same")) Filter(false, hero);
        else List(hero);
    }
}

namespace vscan {
    static std::vector<uintptr_t> g_addrs;

    static void Find(uint64_t v) {
        g_addrs.clear();
        MEMORY_BASIC_INFORMATION mbi;
        size_t bytes = 0;
        constexpr size_t kChunk = 1 << 20;
        std::vector<uint64_t> buf(kChunk / 8);
        for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery((void*)a, &mbi, sizeof(mbi)); a = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            if (mbi.State != MEM_COMMIT || mbi.Protect != PAGE_READWRITE) continue;
            if (mbi.Type != MEM_PRIVATE && mbi.Type != MEM_IMAGE) continue;   // heaps and the exe's .data
            bytes += mbi.RegionSize;
            for (size_t off = 0; off < mbi.RegionSize; off += kChunk) {
                size_t len = std::min(kChunk, mbi.RegionSize - off);
                uintptr_t base = (uintptr_t)mbi.BaseAddress + off;
                if (!memory::SafeRead(base, buf.data(), len)) continue;
                for (size_t i = 0; i < len / 8; i++)
                    if (buf[i] == v && g_addrs.size() < 100000) g_addrs.push_back(base + i * 8);
            }
        }
        LOG("vscan: %zu addresses hold %016llx (%zu MB scanned)", g_addrs.size(), (unsigned long long)v, bytes >> 20);
    }

    static void Keep(uint64_t v) {
        size_t kept = 0;
        for (uintptr_t a : g_addrs) {
            uint64_t cur = 0;
            if (memory::Read(a, cur) && cur == v) g_addrs[kept++] = a;
        }
        g_addrs.resize(kept);
        LOG("vscan: %zu addresses now hold %016llx", kept, (unsigned long long)v);
    }

    static void List() {
        size_t n = 0;
        for (uintptr_t a : g_addrs) {
            if (n++ >= 30) break;
            uint64_t ctx[8] = {};
            memory::SafeRead(a - 0x20, ctx, sizeof(ctx));
            uintptr_t exe = scan::Game().base;
            bool in_exe = a >= exe && a < exe + 0x10000000;
            LOG("  %llx%s | %016llx %016llx %016llx %016llx [%016llx] %016llx %016llx %016llx", (unsigned long long)a,
                in_exe ? " (exe .data)" : "", (unsigned long long)ctx[0], (unsigned long long)ctx[1], (unsigned long long)ctx[2],
                (unsigned long long)ctx[3], (unsigned long long)ctx[4], (unsigned long long)ctx[5], (unsigned long long)ctx[6],
                (unsigned long long)ctx[7]);
        }
        LOG("vscan: %zu addresses", g_addrs.size());
    }

    void Command(const char* sub, uint64_t value) {
        if (!strcmp(sub, "find")) Find(value);
        else if (!strcmp(sub, "keep")) Keep(value);
        else List();
    }
}
