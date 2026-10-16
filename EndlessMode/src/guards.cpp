#include "guards.h"

#include "log.h"
#include "natives.h"
#include "scan.h"

#include <Windows.h>
#include <cstring>

namespace guards {
    // BotStateGotoLocal init, exe+1dd6a33 (25 bytes):
    //   call ResolveRef(&this->mover) ; mov ecx,[rax+144h] ; btr ecx,11 ; bts ecx,10 ; mov [rax+144h],ecx
    // crashes when the bot has no MoverStandard (rax = 0). Patched in place to the same operations plus a null check:
    //   call ResolveRef ; test rax,rax ; jz +15 ; and byte [rax+145h],0F7h ; or byte [rax+145h],04h ; nop
    // (bits 11 and 10 of the dword at +0x144 are bits 3 and 2 of the byte at +0x145.)
    // A function hook was tried first and corrupted the call's float arguments (crash at load, 2026-10-13).
    static const uint8_t kOriginal[25] = {
        0xE8, 0xB8, 0x2E, 0x8A, 0xFF, 0x8B, 0x88, 0x44, 0x01, 0x00, 0x00, 0x0F, 0xBA, 0xF1, 0x0B,
        0x0F, 0xBA, 0xE9, 0x0A, 0x89, 0x88, 0x44, 0x01, 0x00, 0x00,
    };
    static const uint8_t kPatched[25] = {
        0xE8, 0xB8, 0x2E, 0x8A, 0xFF,                    // call ResolveRef (same rel32, same address)
        0x48, 0x85, 0xC0,                                // test rax,rax
        0x74, 0x0F,                                      // jz end
        0x80, 0xA0, 0x45, 0x01, 0x00, 0x00, 0xF7,        // and byte [rax+145h],0F7h
        0x80, 0x88, 0x45, 0x01, 0x00, 0x00, 0x04,        // or  byte [rax+145h],04h
        0x90,                                            // nop
    };

    bool Install() {
        if (!natives::Get(natives::GotoStateInit)) { LOG("guard: GotoStateInit not resolved, no guard"); return false; }
        uint8_t* site = (uint8_t*)scan::FromRva(0x1dd6a33);
        if (memcmp(site, kPatched, sizeof(kPatched)) == 0) { LOG("guard: already patched"); return true; }
        if (memcmp(site, kOriginal, sizeof(kOriginal)) != 0) { LOG("guard: unexpected bytes at exe+1dd6a33, not patching"); return false; }
        DWORD old = 0;
        if (!VirtualProtect(site, sizeof(kPatched), PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(site, kPatched, sizeof(kPatched));
        VirtualProtect(site, sizeof(kPatched), old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(kPatched));
        LOG("guard: BotStateGotoLocal null check patched at exe+1dd6a33");
        return true;
    }
}
