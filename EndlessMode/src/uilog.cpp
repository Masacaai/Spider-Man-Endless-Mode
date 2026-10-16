#include "uilog.h"

#include "log.h"
#include "memory.h"
#include "paths.h"
#include "scan.h"
#include "gfx.h"
#include "ui.h"

#include <MinHook.h>
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace uilog {
    constexpr uintptr_t kInvokeRva = 0x1d1cee0, kTypedRva = 0x77db70;

    using InvokeFn = bool (*)(uintptr_t movie, void* target, const char* method, void* result, void* args, uint32_t n);
    using TypedFn = uint64_t (*)(uintptr_t el, int type, const char* text, uintptr_t data, uint8_t flag, int i, float f);

    static InvokeFn g_invoke_orig = nullptr;
    static TypedFn g_typed_orig = nullptr;
    static std::atomic<bool> g_on = false, g_hooked = false;
    static std::mutex g_mutex;
    static FILE* g_file = nullptr;
    static std::unordered_map<std::string, DWORD> g_last;   // line -> last time written

    static std::string Str(uintptr_t p) {
        char buf[96] = {};
        if (!p || !memory::SafeRead(p, buf, sizeof(buf) - 1)) return "?";
        std::string s;
        for (char c : buf) {
            if (!c) break;
            if ((unsigned char)c < 0x20) { s += ' '; continue; }
            s += c;
        }
        return s;
    }

    // A GFx value (0x30 bytes): type u32 at +0x18 (low bits & 0x8F: 2 bool, 3 int, 4 uint, 5 number, 6 string, 8+
    // objects; 0x40 = managed), data at +0x20. A managed string's data points at a node whose first field is the text.
    static std::string Value(uintptr_t v) {
        uint32_t type = 0;
        uint64_t data = 0;
        if (!memory::Read(v + 0x18, type) || !memory::Read(v + 0x20, data)) return "?";
        uint32_t t = type & 0x8F;
        char buf[64];
        switch (t) {
            case 0: return "undefined";
            case 1: return "null";
            case 2: return (data & 0xFF) ? "true" : "false";
            case 3: snprintf(buf, sizeof(buf), "%d", (int32_t)data); return buf;
            case 4: snprintf(buf, sizeof(buf), "%uu", (uint32_t)data); return buf;
            case 5: { double d; memcpy(&d, &data, 8); snprintf(buf, sizeof(buf), "%g", d); return buf; }
            case 6: {
                uintptr_t p = (uintptr_t)data;
                if (type & 0x40) memory::Read(p, p);
                return "\"" + Str(p) + "\"";
            }
            default: snprintf(buf, sizeof(buf), "<obj type %u>", t); return buf;
        }
    }

    // SetMember calls (the fields the game fills into data objects before passing them to a method) come through the shared
    // hook in gfx.cpp.
    static void Write(const std::string& line);

    static void Observe(uintptr_t, uint64_t pdata, const char* name, const void* value) {
        if (!g_on) return;
        try {   // /EHa: catch(...) also catches access violations
            char head[64];
            snprintf(head, sizeof(head), "  set obj=%llx ", (unsigned long long)pdata);
            Write(head + Str((uintptr_t)name) + " = " + Value((uintptr_t)value));
        } catch (...) {}
    }

    static void Write(const std::string& line) {
        std::lock_guard<std::mutex> lock(g_mutex);
        DWORD now = GetTickCount();
        auto it = g_last.find(line);
        if (it != g_last.end() && now - it->second < 3000) return;
        g_last[line] = now;
        if (g_last.size() > 20000) g_last.clear();
        if (!g_file) return;
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_file, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line.c_str());
        fflush(g_file);
    }

    static bool InvokeDetour(uintptr_t movie, void* target, const char* method, void* result, void* args, uint32_t n) {
        if (g_on) {
            try {   // /EHa: catch(...) also catches access violations
                std::string line = "invoke " + Str((uintptr_t)method) + "(";
                for (uint32_t i = 0; i < n && i < 12; i++) line += (i ? ", " : "") + Value((uintptr_t)args + i * 0x30);
                // Which HUD element the call went to: the root's element whose object holds `target` (as uicall needs it:
                // "uicall <root offset>:<object offset>").
                char tail[96];
                std::string where = "?";
                uintptr_t root = ui::Root(0);
                for (unsigned off = 0x180; root && off <= 0x300; off += 8) {
                    uintptr_t el = 0;
                    if (!memory::Read(root + off, el) || !el) continue;
                    uintptr_t d = (uintptr_t)target - el;
                    if ((uintptr_t)target >= el && d < 0x800) {
                        char w[32];
                        snprintf(w, sizeof(w), "%x:%llx", off, (unsigned long long)d);
                        where = w;
                        break;
                    }
                }
                snprintf(tail, sizeof(tail), ")  at %s", where.c_str());
                Write(line + tail);
            } catch (...) {}
        }
        return g_invoke_orig(movie, target, method, result, args, n);
    }

    static uint64_t TypedDetour(uintptr_t el, int type, const char* text, uintptr_t data, uint8_t flag, int i, float f) {
        if (g_on) {
            try {   // /EHa: catch(...) also catches access violations
                char buf[256];
                snprintf(buf, sizeof(buf), "message type %d text=\"%s\" data=%s flag=%u i=%d f=%g", type,
                         text ? Str((uintptr_t)text).c_str() : "", data ? Value(data).c_str() : "none", flag, i, f);
                Write(buf);
            } catch (...) {}
        }
        return g_typed_orig(el, type, text, data, flag, i, f);
    }

    static bool Hook() {
        if (g_hooked) return true;
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return false;
        void* inv = (void*)scan::FromRva(kInvokeRva);
        void* typ = (void*)scan::FromRva(kTypedRva);
        if (MH_CreateHook(inv, (void*)&InvokeDetour, (void**)&g_invoke_orig) != MH_OK) { LOG("uilog: hook Invoke failed"); return false; }
        if (MH_CreateHook(typ, (void*)&TypedDetour, (void**)&g_typed_orig) != MH_OK) { LOG("uilog: hook typed message failed"); return false; }
        if (MH_EnableHook(inv) != MH_OK || MH_EnableHook(typ) != MH_OK) { LOG("uilog: enable failed"); return false; }
        g_hooked = true;
        return true;
    }

    bool Enable(bool on) {
        if (on) {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (!g_file) g_file = _fsopen((ScriptsDir() + "\\EndlessMode_uilog.txt").c_str(), "a", _SH_DENYNO);
                g_last.clear();
                if (g_file) fprintf(g_file, "---- uilog on ----\n");
            }
            if (!Hook()) { LOG("uilog: hooks not installed"); return false; }
            gfx::SetObserver(&Observe);
        }
        g_on = on;
        if (!on) gfx::SetObserver(nullptr);
        LOG("uilog: %s (scripts\\EndlessMode_uilog.txt)", on ? "on" : "off");
        return true;
    }

    bool Enabled() { return g_on; }
}
