#include "scan.h"

#include <Windows.h>
#include <cstdlib>
#include <cstring>

namespace scan {
    static uintptr_t g_base_override = 0;

    void OverrideGameBaseForTesting(uintptr_t base) { g_base_override = base; }

    const Module& Game() {
        static Module m = [] {
            Module r;
            r.base = g_base_override ? g_base_override : (uintptr_t)GetModuleHandleA(nullptr);
            auto dos = (IMAGE_DOS_HEADER*)r.base;
            auto nt = (IMAGE_NT_HEADERS64*)(r.base + dos->e_lfanew);
            auto sec = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (strncmp((const char*)sec[i].Name, ".text", 5) == 0) {
                    r.text = r.base + sec[i].VirtualAddress;
                    r.text_size = sec[i].Misc.VirtualSize;
                    break;
                }
            }
            return r;
        }();
        return m;
    }

    std::vector<int> Parse(const char* pattern) {
        std::vector<int> out;
        for (const char* p = pattern; *p; ) {
            if (*p == ' ') { p++; continue; }
            if (*p == '?') {
                out.push_back(-1);
                while (*p == '?') p++;
                continue;
            }
            char* end = nullptr;
            out.push_back((int)strtol(p, &end, 16) & 0xFF);
            p = end;
        }
        return out;
    }

    std::vector<uintptr_t> FindAll(const char* pattern, size_t max_hits) {
        std::vector<uintptr_t> hits;
        const auto pat = Parse(pattern);
        const Module& m = Game();
        if (!m.text || pat.empty() || pat[0] < 0) return hits;

        const uint8_t* data = (const uint8_t*)m.text;
        const uint8_t first = (uint8_t)pat[0];
        const size_t n = pat.size();
        for (size_t i = 0; i + n <= m.text_size; i++) {
            if (data[i] != first) continue;
            size_t j = 1;
            for (; j < n; j++) {
                if (pat[j] >= 0 && data[i + j] != (uint8_t)pat[j]) break;
            }
            if (j == n) {
                hits.push_back(m.text + i);
                if (hits.size() >= max_hits) break;
            }
        }
        return hits;
    }

    uintptr_t Rel32Target(uintptr_t match, int off) {
        int32_t rel;
        memcpy(&rel, (const void*)(match + off), sizeof(rel));
        return match + off + 4 + rel;
    }
}
