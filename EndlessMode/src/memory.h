#pragma once

#include <cstddef>
#include <cstdint>

// Reads that can't crash the game: an access violation returns false instead.
namespace memory {
    bool SafeRead(uintptr_t addr, void* out, size_t len);

    // Calls fn(arg) and catches access violations (for read-only game lookups).
    bool SafeCall(uintptr_t (*fn)(void*), void* arg, uintptr_t& out);

    bool SafeWrite(uintptr_t addr, const void* in, size_t len);

    template <typename T>
    bool Write(uintptr_t addr, const T& v) { return SafeWrite(addr, &v, sizeof(T)); }

    template <typename T>
    bool Read(uintptr_t addr, T& out) { return SafeRead(addr, &out, sizeof(T)); }
}
