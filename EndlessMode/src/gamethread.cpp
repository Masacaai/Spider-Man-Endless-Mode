#include "gamethread.h"

#include "log.h"
#include "natives.h"

#include <MinHook.h>
#include <Windows.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

namespace gamethread {
    using TickFn = void* (*)(void*, void*, void*, void*);

    static TickFn g_original = nullptr;
    static void* g_target = nullptr;
    static std::atomic<bool> g_installed = false;
    static std::atomic<uint64_t> g_ticks = 0;
    static std::atomic<DWORD> g_thread_id = 0;
    static std::mutex g_mutex;
    static std::deque<std::function<void()>> g_jobs;
    // Frame timing (the hooked function runs once per game frame, ~92 Hz seen at 90+ fps; to be confirmed).
    static std::atomic<int64_t> g_last_qpc = 0, g_max_gap = 0;
    static std::atomic<uint64_t> g_window_ticks = 0;
    static int64_t g_window_start = 0;

    static void RunJobs() {
        std::deque<std::function<void()>> jobs;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            jobs.swap(g_jobs);
        }
        for (auto& job : jobs) job();
    }

    // Forward all four integer argument registers untouched; Script Hook's detour only preserved rcx,
    // so the original most likely takes one argument, but this costs nothing.
    static void* Detour(void* a, void* b, void* c, void* d) {
        g_ticks++;
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        int64_t last = g_last_qpc.exchange(q.QuadPart);
        if (last) {
            int64_t gap = q.QuadPart - last;
            int64_t m = g_max_gap.load();
            while (gap > m && !g_max_gap.compare_exchange_weak(m, gap)) {}
        }
        g_window_ticks++;
        if (g_thread_id == 0) {
            g_thread_id = GetCurrentThreadId();
            LOG("game thread tick: first call on thread %lu", g_thread_id.load());
        }
        RunJobs();
        return g_original(a, b, c, d);
    }

    bool Install() {
        if (g_installed) return true;
        g_target = (void*)natives::Get(natives::GameThreadTick);
        if (!g_target) {
            LOG("hook: GameThreadTick not resolved, refusing to hook");
            return false;
        }
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
            LOG("hook: MH_Initialize failed (%d)", s);
            return false;
        }
        s = MH_CreateHook(g_target, (void*)&Detour, (void**)&g_original);
        if (s != MH_OK) {
            LOG("hook: MH_CreateHook failed (%d)", s);
            return false;
        }
        s = MH_EnableHook(g_target);
        if (s != MH_OK) {
            LOG("hook: MH_EnableHook failed (%d)", s);
            MH_RemoveHook(g_target);
            return false;
        }
        g_installed = true;
        LOG("hook: installed on exe+%llx", (unsigned long long)((uintptr_t)g_target - (uintptr_t)GetModuleHandleA(nullptr)));
        return true;
    }

    bool Uninstall() {
        if (!g_installed) return true;
        MH_DisableHook(g_target);
        MH_RemoveHook(g_target);
        g_installed = false;
        LOG("hook: removed");
        return true;
    }

    bool Installed() { return g_installed; }
    uint64_t Ticks() { return g_ticks; }

    FrameStats TakeFrameStats() {
        LARGE_INTEGER f, q;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&q);
        FrameStats s;
        uint64_t n = g_window_ticks.exchange(0);
        int64_t start = g_window_start ? g_window_start : q.QuadPart;
        double secs = double(q.QuadPart - start) / double(f.QuadPart);
        g_window_start = q.QuadPart;
        s.ticks = n;
        s.seconds = secs;
        s.avg_hz = secs > 0 ? n / secs : 0;
        s.worst_ms = g_max_gap.exchange(0) * 1000.0 / double(f.QuadPart);
        return s;
    }

    void Post(std::function<void()> job) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_jobs.push_back(std::move(job));
    }

    bool Run(std::function<void()> job, unsigned timeout_ms) {
        auto done = std::make_shared<std::atomic<bool>>(false);
        Post([job = std::move(job), done] { job(); *done = true; });
        for (unsigned waited = 0; !*done; waited += 5) {
            if (waited >= timeout_ms) return false;
            Sleep(5);
        }
        return true;
    }
}
