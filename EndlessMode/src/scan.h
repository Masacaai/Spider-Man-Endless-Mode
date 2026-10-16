#pragma once

#include <cstdint>
#include <vector>

namespace scan {
    struct Module {
        uintptr_t base = 0;
        uintptr_t text = 0;      // .text start
        size_t text_size = 0;
    };

    // The game executable (the process's main module).
    const Module& Game();

    // Offline tests only: treat an image mapped elsewhere as the game. Call before Game().
    void OverrideGameBaseForTesting(uintptr_t base);

    // "48 8B ?? E8" -> bytes, -1 = wildcard.
    std::vector<int> Parse(const char* pattern);

    // All matches of pattern in the game's .text (capped at max_hits).
    std::vector<uintptr_t> FindAll(const char* pattern, size_t max_hits = 4);

    // Rip-relative operand: match + off + 4 + rel32(match + off).
    uintptr_t Rel32Target(uintptr_t match, int off);

    inline uintptr_t Rva(uintptr_t addr) { return addr - Game().base; }
    inline uintptr_t FromRva(uintptr_t rva) { return Game().base + rva; }
}
