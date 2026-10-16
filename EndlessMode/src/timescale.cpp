#include "timescale.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "scan.h"

#include <Windows.h>
#include <cstring>

namespace timescale {
    constexpr uintptr_t kSystemRva = 0x621b890, kStartRva = 0x19bbaa0;
    constexpr int kChannel = 5;   // the scripts' channel (TimeScaleAction uses it)

    using StartFn = void (*)(uintptr_t sys, int channel, float target, float rate, const char* name, int mode);

    static bool Callable() {
        static const uint8_t pro[] = { 0x83, 0xFA, 0x16, 0x0F, 0x87 };   // cmp edx,16h; ja ...
        uint8_t cur[sizeof(pro)] = {};
        static bool logged = false;
        bool ok = memory::SafeRead(scan::FromRva(kStartRva), cur, sizeof(cur)) && !memcmp(cur, pro, sizeof(pro));
        if (!ok && !logged) { logged = true; LOG("timescale: exe+19bbaa0 doesn't match; disabled"); }
        return ok;
    }

    static bool CallStart(StartFn fn, uintptr_t sys, float target, float rate) {
        __try { fn(sys, kChannel, target, rate, "EndlessMode", 2); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool Set(float target, float rate) {
        if (!Callable() || !gamethread::Installed()) return false;
        gamethread::Post([target, rate] {
            bool ok = CallStart((StartFn)scan::FromRva(kStartRva), scan::FromRva(kSystemRva), target, rate);
            LOG("timescale: -> %.3f (rate %.1f)%s", target, rate, ok ? "" : " CRASHED (caught)");
        });
        return true;
    }

    float Current() {
        float v = 1.0f;
        memory::Read(scan::FromRva(kSystemRva) + 0x18, v);
        return v;
    }

    bool Freeze() { return Set(0.0005f, 20.0f); }   // ramps down in ~0.05 s
    bool Unfreeze() { return Set(1.0f, 20.0f); }
}
