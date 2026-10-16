#pragma once

#include <cstdint>
#include <functional>

// Runs work on the game's main thread by hooking the same per-frame function
// ScriptHookSMPC uses. Nothing is hooked until Install() is called.
namespace gamethread {
    bool Install();
    bool Uninstall();
    bool Installed();

    // Number of times the hooked function has run since Install().
    uint64_t Ticks();

    // Ticks per second and the longest gap between ticks since the previous call (frame-time proxy).
    struct FrameStats { uint64_t ticks = 0; double seconds = 0, avg_hz = 0, worst_ms = 0; };
    FrameStats TakeFrameStats();

    // Queue a job; it runs inside the next tick.
    void Post(std::function<void()> job);

    // Queue a job and wait for it (from a worker thread; never from the game thread). false on timeout, in which case
    // the job may still run later.
    bool Run(std::function<void()> job, unsigned timeout_ms = 3000);
}
