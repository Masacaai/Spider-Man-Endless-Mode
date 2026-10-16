#include "watchpoint.h"

#include "log.h"
#include "scan.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>

namespace watchpoint {
    constexpr int kMaxHits = 32;
    constexpr int kFrames = 14;
    struct Hit {
        uint32_t thread;
        uintptr_t rip;
        uintptr_t frames[kFrames];
    };
    static Hit g_hits[kMaxHits];
    static std::atomic<int> g_count = 0;
    static std::atomic<uintptr_t> g_addr = 0;
    static PVOID g_veh = nullptr;

    static bool InText(uintptr_t v) {
        uintptr_t base = scan::Game().base;
        return v > base + 0x1000 && v < base + 0x37F0000;   // .text of Spider-Man.exe 4.0630
    }

    static LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
        if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
        CONTEXT* c = ep->ContextRecord;
        if (!(c->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;   // not our DR0
        c->Dr6 = 0;
        int i = g_count.fetch_add(1);
        if (i < kMaxHits) {
            Hit& h = g_hits[i];
            h.thread = GetCurrentThreadId();
            h.rip = c->Rip;
            int n = 0;
            // Heuristic stack walk: qwords on the stack that point into the exe's code.
            const uintptr_t* sp = (const uintptr_t*)c->Rsp;
            for (int k = 0; k < 512 && n < kFrames; k++) {
                uintptr_t v = 0;
                __try { v = sp[k]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
                if (InText(v)) h.frames[n++] = v;
            }
            for (; n < kFrames; n++) h.frames[n] = 0;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    static int ApplyToThreads(uintptr_t addr) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
        THREADENTRY32 te{ sizeof(te) };
        int n = 0;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!t) continue;
            if (SuspendThread(t) != (DWORD)-1) {
                CONTEXT c{};
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(t, &c)) {
                    c.Dr0 = addr;
                    // DR7: L0 enable (bit 0), RW0 = 01 write (bits 16-17), LEN0 = 10 eight bytes (bits 18-19).
                    c.Dr7 = addr ? ((c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (2ull << 18)) : (c.Dr7 & ~0xF0003ull);
                    if (SetThreadContext(t, &c)) n++;
                }
                ResumeThread(t);
            }
            CloseHandle(t);
        }
        CloseHandle(snap);
        return n;
    }

    void Set(uintptr_t addr) {
        if (!g_veh) g_veh = AddVectoredExceptionHandler(1, Handler);
        g_count = 0;
        g_addr = addr;
        int n = ApplyToThreads(addr);
        LOG("wp: write watch on %llx armed on %d threads", (unsigned long long)addr, n);
    }

    void Clear() {
        int n = ApplyToThreads(0);
        g_addr = 0;
        LOG("wp: cleared on %d threads", n);
    }

    void Dump() {
        int count = g_count.load();
        uintptr_t base = scan::Game().base;
        LOG("wp: %d hits on %llx", count, (unsigned long long)g_addr.load());
        for (int i = 0; i < count && i < kMaxHits; i++) {
            const Hit& h = g_hits[i];
            char frames[kFrames * 16 + 1] = {};
            int len = 0;
            for (int k = 0; k < kFrames && h.frames[k]; k++)
                len += snprintf(frames + len, sizeof(frames) - len, " %llx", (unsigned long long)(h.frames[k] - base));
            LOG("  hit %d thread %u rip exe+%llx | stack:%s", i, h.thread, (unsigned long long)(h.rip - base), frames);
        }
    }
}
