#include "ui.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "scan.h"

#include <Windows.h>
#include <cstring>
#include <memory>

namespace ui {
    constexpr uintptr_t kRootsRva = 0x7c2b5a0, kInvokeRva = 0x1d1cee0;

    uintptr_t Root(int player) {
        uintptr_t r = 0;
        memory::Read(scan::FromRva(kRootsRva) + player * 8, r);
        return r;
    }

    std::string ClassName(uintptr_t obj) {
        // x64 MSVC: [vtable-8] -> CompleteObjectLocator {sig, offset, cdOffset, TypeDescriptor rva, ...};
        // TypeDescriptor name at +0x10 is ".?AVName@@".
        uintptr_t base = scan::Game().base, vt = 0, col = 0;
        uint32_t td_rva = 0, sig = 0;
        if (!memory::Read(obj, vt) || vt < base || vt > base + 0x10000000) return "?";
        if (!memory::Read(vt - 8, col) || col < base || col > base + 0x10000000) return "?";
        if (!memory::Read(col, sig) || sig != 1 || !memory::Read(col + 0xC, td_rva) || !td_rva) return "?";
        char name[128] = {};
        if (!memory::SafeRead(base + td_rva + 0x10, name, sizeof(name) - 1)) return "?";
        std::string s(name);
        if (s.rfind(".?AV", 0) == 0) s = s.substr(4);
        else if (s.rfind(".?AU", 0) == 0) s = s.substr(4);
        if (auto at = s.find("@@"); at != std::string::npos) s = s.substr(0, at);
        return s;
    }

    static void Dump(uintptr_t obj, unsigned bytes, const char* label) {
        for (unsigned off = 0; off < bytes; off += 8) {
            uintptr_t p = 0;
            if (!memory::Read(obj + off, p) || p < 0x10000 || p > 0x7FFFFFFFFFFF) continue;
            std::string cls = ClassName(p);
            if (cls != "?") LOG("  %s+0x%03x -> %llx  %s", label, off, (unsigned long long)p, cls.c_str());
        }
    }

    void DumpRoot(int player, unsigned bytes) {
        uintptr_t r = Root(player);
        LOG("ui: HUD root[%d] = %llx (%s)", player, (unsigned long long)r, r ? ClassName(r).c_str() : "null");
        if (r) Dump(r, bytes, "root");
    }

    void DumpObject(uintptr_t obj, unsigned bytes) {
        LOG("ui: object %llx (%s)", (unsigned long long)obj, ClassName(obj).c_str());
        Dump(obj, bytes, "obj");
    }

    struct Value {   // the game's 0x30-byte GFx value wrapper (layout from exe+77d130)
        uint64_t pad0 = 0, pad1 = 0;
        uint64_t obj_iface = 0;
        uint32_t type = 0, pad2 = 0;
        uint64_t data = 0;
        uint64_t aux = 0;
    };
    static_assert(sizeof(Value) == 0x30);

    using InvokeFn = bool (*)(uintptr_t movie, void* target, const char* method, void* result, void* args, uint32_t n);

    static bool SafeInvoke(InvokeFn fn, uintptr_t movie, void* target, const char* method, void* args, uint32_t n, bool& ok) {
        __try { ok = fn(movie, target, method, nullptr, args, n); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // The game's own wrapper for the activity banner (exe+77b640, called from exe+733e10 with "Successfully Resolved" and
    // the activity name): checks the element's flags (+0x248 & 0x8F), turns both strings into movie strings, invokes
    // ShowActivityMessage on the AS object at +0x230 and then shows that clip (exe+7655e0). A bare Invoke draws nothing.
    bool ShowActivityBanner(const std::string& title, const std::string& name, int type) {
        auto result = std::make_shared<int>(-1);
        auto t = std::make_shared<std::string>(title), n = std::make_shared<std::string>(name);
        gamethread::Run([t, n, type, result] {
            uintptr_t r = Root(0), el = 0;
            if (!r || !memory::Read(r + 0x1A0, el) || !el) { LOG("banner: no message element"); return; }
            using Fn = bool (*)(uintptr_t el, const char* title, const char* name, int type);
            auto fn = (Fn)scan::FromRva(0x77b640);
            bool ok = false;
            __try { ok = fn(el, t->c_str(), n->c_str(), type); *result = ok ? 1 : 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { *result = 2; }
            LOG("banner: '%s' / '%s' type %d -> %s", t->c_str(), n->c_str(), type,
                *result == 2 ? "CRASHED (caught)" : ok ? "true" : "false");
        }, 3000);
        return *result == 1;
    }

    // The award-tracker toast through the game's wrapper (exe+77b7f0, same pattern as the banner): (element, int a,
    // int b, title, text, extra) -> ShowAwardTrackerToast(a, b, title, text, extra). Meaning of a/b and extra unknown yet
    // (progress "a of b" and an icon/texture name are the guesses). Element: the message element [root+0x1A0].
    bool ShowAwardToast(int a, int b, const std::string& s1, const std::string& s2, const std::string& s3) {
        auto result = std::make_shared<int>(-1);
        auto x1 = std::make_shared<std::string>(s1), x2 = std::make_shared<std::string>(s2), x3 = std::make_shared<std::string>(s3);
        gamethread::Run([a, b, x1, x2, x3, result] {
            uintptr_t r = Root(0), el = 0;
            if (!r || !memory::Read(r + 0x1A0, el) || !el) { LOG("toast: no message element"); return; }
            using Fn = bool (*)(uintptr_t el, int a, int b, const char* s1, const char* s2, const char* s3);
            auto fn = (Fn)scan::FromRva(0x77b7f0);
            bool ok = false;
            __try { ok = fn(el, a, b, x1->c_str(), x2->c_str(), x3->c_str()); *result = ok ? 1 : 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { *result = 2; }
            LOG("toast: %d %d '%s' '%s' '%s' -> %s", a, b, x1->c_str(), x2->c_str(), x3->c_str(),
                *result == 2 ? "CRASHED (caught)" : ok ? "true" : "false");
        }, 3000);
        return *result == 1;
    }

    // exe+77db70(element, type, text, r9, u8 flag, int, float): the game's typed HUD message on the message element
    // [root+0x1A0]. UIMediumMessageAction (exe+7f3a50) uses it: type 7 with localized text (via exe+77e2f0), types 10 and
    // 11 without text. Its callers pass r9 = 0, flag = 1, int = 0, float = 0.
    bool ShowTypedMessage(int type, const std::string& text) {
        auto result = std::make_shared<int>(-1);
        auto t = std::make_shared<std::string>(text);
        gamethread::Run([type, t, result] {
            uintptr_t r = Root(0), el = 0;
            if (!r || !memory::Read(r + 0x1A0, el) || !el) { LOG("msgtype: no message element"); return; }
            using Fn = uint64_t (*)(uintptr_t el, int type, const char* text, uintptr_t r9, uint8_t flag, int i, float f);
            auto fn = (Fn)scan::FromRva(0x77db70);
            uint64_t ret = 0;
            __try { ret = fn(el, type, t->empty() ? nullptr : t->c_str(), 0, 1, 0, 0.0f); *result = 1; }
            __except (EXCEPTION_EXECUTE_HANDLER) { *result = 2; }
            LOG("msgtype %d '%s' -> %s (ret %llx)", type, t->c_str(), *result == 2 ? "CRASHED (caught)" : "called",
                (unsigned long long)ret);
        }, 3000);
        return *result == 1;
    }

    bool Call(uintptr_t where, unsigned as_off, const std::string& method, const std::vector<Arg>& args) {
        auto result = std::make_shared<int>(-1);
        auto a = std::make_shared<std::vector<Arg>>(args);
        auto m = std::make_shared<std::string>(method);
        gamethread::Run([where, as_off, a, m, result] {
            uintptr_t el = where;
            if (where < 0x10000) {
                uintptr_t r = Root(0);
                if (!r || !memory::Read(r + where, el) || !el) { LOG("ui: no element at root+0x%llx", (unsigned long long)where); return; }
            }
            uintptr_t movie = 0;
            if (!memory::Read(el + 0x10, movie) || !movie) { LOG("ui: element %llx has no movie at +0x10", (unsigned long long)el); return; }
            std::vector<Value> vals(a->size());
            for (size_t i = 0; i < a->size(); i++) {
                const Arg& x = (*a)[i];
                if (x.kind == Arg::Number) { vals[i].type = 5; memcpy(&vals[i].data, &x.num, 8); }
                else if (x.kind == Arg::Int) { vals[i].type = 3; vals[i].data = (uint64_t)(uint32_t)(int32_t)x.num; }
                else if (x.kind == Arg::Bool) { vals[i].type = 2; vals[i].data = x.b ? 1 : 0; }
                else { vals[i].type = 6; vals[i].data = (uint64_t)x.str.c_str(); }   // unmanaged string pointer
            }
            bool ok = false;
            bool called = SafeInvoke((InvokeFn)scan::FromRva(kInvokeRva), movie, (void*)(el + as_off), m->c_str(),
                                     vals.empty() ? nullptr : vals.data(), (uint32_t)vals.size(), ok);
            *result = called ? (ok ? 1 : 0) : 2;
            LOG("ui: %s on %llx (%s) with %zu args -> %s", m->c_str(), (unsigned long long)el, ClassName(el).c_str(),
                a->size(), called ? (ok ? "true" : "false") : "CRASHED (caught)");
        }, 3000);
        return *result == 1;
    }
}
