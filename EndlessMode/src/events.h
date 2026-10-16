#pragma once

#include <cstdint>
#include <string>

// The engine's generic event system (exe+16d5e80) and bot groups (exe+496b40). Both run on the game thread.
namespace events {
    // Known event type hashes (Insomniac CRC32 of the class name, computed with ALERT's crc32).
    // Returns 0 for unknown names; hex strings are parsed as-is.
    uint32_t TypeHash(const std::string& name_or_hex);

    // SendEvent(&EventSys, type, target, source ? &source : null, 1, 0, 0, 0, 1, f1, f2, null, 0).
    bool Send(uint32_t type_hash, uint32_t target_handle, uint32_t source_handle, float f1 = 0.0f, float f2 = 0.0f);

    // Group::Alert(group, alerter, 1): what a bot that spots the hero calls on its group.
    bool AlertGroup(uintptr_t group, uint32_t alerter_handle);
}
