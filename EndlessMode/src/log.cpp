#include "log.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace logging {
    static FILE* g_file = nullptr;
    static std::mutex g_mutex;

    void Init(const std::string& path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) return;
        g_file = _fsopen(path.c_str(), "w", _SH_DENYNO);
    }

    void Write(const char* fmt, ...) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_file) return;

        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

        va_list args;
        va_start(args, fmt);
        vfprintf(g_file, fmt, args);
        va_end(args);

        fputc('\n', g_file);
        fflush(g_file);
    }
}
