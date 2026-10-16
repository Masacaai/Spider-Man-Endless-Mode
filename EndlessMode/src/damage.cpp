#include "damage.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "observer.h"

#include <cstring>

#include <cmath>

namespace damage {
    using CreateFn = uintptr_t (*)(uintptr_t sys, const uint32_t* target_handle, const float* pos, const float* vec,
                                   const float* dir, int32_t unk);
    using CommitFn = void (*)(uintptr_t sys);

    static uint32_t HandleOf(uintptr_t entity) {
        uint16_t hi = 0;
        uint32_t lo = 0;
        if (!memory::Read(entity + 0x10, hi) || !memory::Read(entity + 0xC, lo)) return 0;
        return ((uint32_t)(hi & 0x7FF) << 20) | (lo & 0xFFFFF);
    }

    bool Nudge(uintptr_t target_entity, uint32_t attacker_handle, const float attacker_pos[3], float amount, int mode) {
        uint8_t tmpl[0x1C0] = {};
        bool have_tmpl = mode > 0 && observer::CopyHitTemplate(tmpl);
        if (mode > 0 && !have_tmpl) { LOG("damage: no hit template yet (land one real hit with dmgwatch on)"); return false; }
        uintptr_t sys = natives::Get(natives::DmgSys);
        auto create = (CreateFn)natives::Get(natives::DmgCreate);
        auto commit = (CommitFn)natives::Get(natives::DmgCommit);
        if (!sys || !create || !commit || !gamethread::Installed()) { LOG("damage: not ready"); return false; }

        float from[3] = { attacker_pos[0], attacker_pos[1], attacker_pos[2] };
        gamethread::Post([=]() mutable {
            uint32_t target = HandleOf(target_entity);
            uintptr_t transform = 0;
            float pos[3] = {};
            if (!target || !memory::Read(target_entity, transform) || !memory::SafeRead(transform + 0x30, pos, sizeof(pos))) {
                LOG("damage: target %llx unreadable", (unsigned long long)target_entity);
                return;
            }
            float dir[3] = { pos[0] - from[0], pos[1] - from[1], pos[2] - from[2] };
            float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (len > 0.001f) { dir[0] /= len; dir[1] /= len; dir[2] /= len; }
            float zero[3] = {};

            uintptr_t msg = create(sys, &target, pos, zero, dir, -1);
            if (!msg || msg == sys + 0x123868) { LOG("damage: create failed (pool full?)"); return; }
            // Same header fields the melee code writes (exe+69c12d..69c14f).
            memory::Write<uint32_t>(msg + 0x138, attacker_handle);
            memory::Write<uint32_t>(msg + 0x150, 3);
            memory::Write<uint32_t>(msg + 0x13C, 1);
            uint64_t f8 = 0, f18 = 0;
            memory::Read(msg + 0x8, f8); memory::Read(msg + 0x18, f18);
            uint64_t bits = 0x4300;
            if (amount > 0.0f) {
                // Damage amount and its field-present bit, as the damage code sets them (exe+1ed1681..169d).
                memory::Write<float>(msg + 0x144, amount);
                bits |= 0x800;
            }
            if (have_tmpl) {
                // Attack fields as a real hero punch sets them (exe+8cdf60), with their field-present bits.
                memcpy((void*)(msg + 0x154), tmpl + 0x154, 4);   bits |= 0x8000;
                memcpy((void*)(msg + 0x168), tmpl + 0x168, 4);   bits |= 0x100000;
                memcpy((void*)(msg + 0x19C), tmpl + 0x19C, 4);   bits |= 0x4000000;
                if (mode >= 2) { memcpy((void*)(msg + 0x180), tmpl + 0x180, 16); bits |= 0x400000; }
                if (mode >= 3) {
                    // Replay the learned hit wholesale: every field from +0x13C up to the target slot (+0x1A8),
                    // including the two lists (+0x170, +0x180) and all field-present bits.
                    memcpy((void*)(msg + 0x13C), tmpl + 0x13C, 0x1A8 - 0x13C);
                    uint64_t t8 = 0, t18 = 0;
                    memcpy(&t8, tmpl + 0x8, 8); memcpy(&t18, tmpl + 0x18, 8);
                    bits |= (t8 | t18);
                    memory::Write<float>(msg + 0x144, amount);   // our amount, not the learned hit's
                }
            }
            memory::Write<uint64_t>(msg + 0x8, f8 | bits);
            memory::Write<uint64_t>(msg + 0x18, f18 | bits);
            commit(sys);
            LOG("damage: nudged target %08x from attacker %08x, %.1f dmg (msg %llx)", target, attacker_handle, amount,
                (unsigned long long)msg);
        });
        return true;
    }
}
