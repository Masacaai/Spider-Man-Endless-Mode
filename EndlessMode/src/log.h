#pragma once

#include <string>

namespace logging {
    // Opens <dll dir>\EndlessMode.log (truncated on each game launch).
    void Init(const std::string& path);
    void Write(const char* fmt, ...);
}

#define LOG(fmt, ...) logging::Write(fmt, __VA_ARGS__)
