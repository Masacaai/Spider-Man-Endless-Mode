#include "gfx.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "scan.h"
#include "ui.h"

#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include <cstring>
#include <memory>
#include <mutex>

namespace gfx {
    constexpr uintptr_t kInvokeRva = 0x1d1cee0, kTypedRva = 0x77db70;

    struct Value {   // the game's 0x30-byte GFx value
        // +0x00/+0x08: links of the movie's list of live managed values (the game initialises them to all-ones; the movie's
        // teardown, exe+319b340, walks that list and releases every node: a Value that is registered then destroyed without
        // ObjectRelease leaves a dangling node and crashes the game on the next HUD reload, 2026-10-15).
        uint64_t pad0 = ~0ull, pad1 = ~0ull;
        uintptr_t iface = 0;
        uint32_t type = 0, pad2 = 0;
        uint64_t data = 0;
        uint64_t aux = 0;
    };
    static_assert(sizeof(Value) == 0x30);

    using CreateStringFn = void (*)(uintptr_t movie, Value* out, const char* s);
    using CreateObjectFn = void (*)(uintptr_t movie, Value* out, const char* cls, const Value* args, uint32_t n);
    using CreateArrayFn = void (*)(uintptr_t movie, Value* out);
    using ReleaseFn = void (*)(uintptr_t iface, Value* v, uint64_t data);
    using SetMemberFn = bool (*)(uintptr_t iface, uint64_t data, const char* name, const Value* v, bool isdobj);
    using PushBackFn = bool (*)(uintptr_t iface, uint64_t data, const Value* v);

    // The HUD movie (all elements share it): [[root+0x1A0]+0x300]+8 is the view wrapper, +0x18 the GFx Movie.
    static uintptr_t Movie() {
        uintptr_t root = ui::Root(0), el = 0, view = 0, wrapper = 0, movie = 0;
        if (!root || !memory::Read(root + 0x1A0, el) || !el || !memory::Read(el + 0x300, view) || !view ||
            !memory::Read(view + 8, wrapper) || !wrapper || !memory::Read(wrapper + 0x18, movie))
            return 0;
        return movie;
    }

    template <typename F>
    static F Slot(uintptr_t obj, unsigned off) {
        uintptr_t vt = 0, fn = 0;
        if (!obj || !memory::Read(obj, vt) || !vt || !memory::Read(vt + off, fn)) return nullptr;
        return (F)fn;
    }

    static void Release(Value& v) {
        if ((v.type & 0x40) && v.iface)
            if (auto rel = Slot<ReleaseFn>(v.iface, 0x10)) rel(v.iface, &v, v.data);
        v = Value{};
    }

    // Game thread only.
    static bool Build(uintptr_t movie, const Node& n, Value& out) {
        out = Value{};
        switch (n.kind) {
            case Node::Undefined: return true;
            case Node::Bool: out.type = 2; out.data = n.b ? 1 : 0; return true;
            case Node::Int: out.type = 3; out.data = (uint32_t)(int32_t)n.i; return true;
            case Node::UInt: out.type = 4; out.data = (uint32_t)n.i; return true;
            case Node::Number: out.type = 5; memcpy(&out.data, &n.d, 8); return true;
            case Node::String: {
                auto f = Slot<CreateStringFn>(movie, 0x160);
                if (!f) return false;
                f(movie, &out, n.s.c_str());
                return out.type != 0;
            }
            case Node::Object: {
                auto f = Slot<CreateObjectFn>(movie, 0x170);
                if (!f) return false;
                f(movie, &out, nullptr, nullptr, 0);
                auto set = Slot<SetMemberFn>(out.iface, 0x28);
                if (!set) return false;
                for (auto& [name, child] : n.fields) {
                    Value v;
                    if (Build(movie, child, v)) set(out.iface, out.data, name.c_str(), &v, false);
                    Release(v);
                }
                return true;
            }
            case Node::Array: {
                auto f = Slot<CreateArrayFn>(movie, 0x178);
                if (!f) return false;
                f(movie, &out);
                auto push = Slot<PushBackFn>(out.iface, 0x78);
                if (!push) return false;
                for (auto& child : n.items) {
                    Value v;
                    if (Build(movie, child, v)) push(out.iface, out.data, &v);
                    Release(v);
                }
                return true;
            }
        }
        return false;
    }

    static std::atomic<int> g_state{ 0 };   // 0 unchecked, 1 ok, 2 failed
    static std::mutex g_check_mutex;

    // Game thread: build and tear down one object of every kind. Any exception or a failed build = not ready.
    static bool Check() {
        bool ok = false;
        try {
            uintptr_t movie = Movie();
            if (movie) {
                Node n = Node::Obj({ { "label", Node::Of("check") }, { "count", Node::Of(3) }, { "on", Node::Of(true) } });
                n.Set("list", Node::Arr({ Node::Of(1), Node::Of("two") }));
                Value v;
                ok = Build(movie, n, v) && v.type != 0 && v.iface != 0;
                Release(v);
            }
        } catch (...) {
            ok = false;
        }
        LOG("gfx: self-check %s", ok ? "passed" : "FAILED (panels fall back to text)");
        return ok;
    }

    bool Ready() {
        int s = g_state;
        if (s) return s == 1;
        std::lock_guard<std::mutex> lock(g_check_mutex);
        if (g_state) return g_state == 1;
        auto result = std::make_shared<std::atomic<int>>(0);
        gamethread::Run([result] { *result = Check() ? 1 : 2; }, 3000);
        g_state = *result == 1 ? 1 : 2;
        return g_state == 1;
    }

    // ---------------- SetMember hook: text capture ----------------
    constexpr uintptr_t kSetMemberRva = 0x3529e30;
    static SetMemberFn g_sm_orig = nullptr;
    static std::atomic<SetMemberObserver> g_observer{ nullptr };
    static std::mutex g_cap_mutex;
    // `val` is a heap Value we never free: it is our registered reference to the object. When the HUD movie is destroyed
    // the movie's teardown releases it (type becomes 0x20, no longer managed), which is how we notice the capture is stale.
    struct Cap { uintptr_t iface; uint64_t pdata; std::vector<std::string> keys; Value* val; };
    static std::vector<Cap> g_caps;

    struct Watch { const char* key; const char* original; };
    static const Watch kWatch[] = {
        { "HUD_TASK_COMPLETE_REWARDS", "REWARDS" },
        { "CHALLENGE_GEN_BTN_TRYAGAIN", "RETRY" },
        { "CHALLENGE_GEN_BTN_CONTINUE", "CONTINUE" },
        { "HUD_TASK_COMPLETE_BONUSOBJECTIVES", "BONUS OBJECTIVES" },
        { "HUD_TASK_COMPLETED", "COMPLETED!" },
    };

    static bool Watched(const char* n) {
        if (!n || (n[0] != 'H' && n[0] != 'C')) return false;
        for (auto& w : kWatch) if (!strcmp(n, w.key)) return true;
        return false;
    }

    static void Record(uintptr_t iface, uint64_t pdata, const char* name) {
        std::lock_guard<std::mutex> lock(g_cap_mutex);
        for (auto& c : g_caps)
            if (c.iface == iface && c.pdata == pdata) {
                for (auto& k : c.keys) if (k == name) return;
                c.keys.push_back(name);
                return;
            }
        Value* v = new Value();   // deliberately leaked (see Cap)
        v->iface = iface; v->type = 0x48; v->data = pdata;   // an object value, managed
        if (auto add = Slot<void (*)(uintptr_t, Value*, uint64_t)>(iface, 0x08)) add(iface, v, pdata);
        g_caps.push_back({ iface, pdata, { name }, v });
    }

    static bool SMDetour(uintptr_t self, uintptr_t pdata, const char* name, const Value* v, bool isd) {
        if (Watched(name)) { try { Record(self, pdata, name); } catch (...) {} }
        if (auto o = g_observer.load()) { try { o(self, pdata, name, v); } catch (...) {} }
        return g_sm_orig(self, pdata, name, v, isd);
    }

    void SetObserver(SetMemberObserver fn) { g_observer = fn; }

    bool InstallTextCapture() {
        static bool done = false;
        if (done) return true;
        void* target = (void*)scan::FromRva(kSetMemberRva);
        static const uint8_t expect[8] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41 };   // push rbp/rbx/rsi/rdi/r12/r13...
        uint8_t got[8] = {};
        if (!memory::SafeRead((uintptr_t)target, got, 8) || memcmp(got, expect, 8)) {
            LOG("gfx: SetMember prologue doesn't match, text capture off");
            return false;
        }
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) return false;
        if (MH_CreateHook(target, (void*)&SMDetour, (void**)&g_sm_orig) != MH_OK || MH_EnableHook(target) != MH_OK) {
            LOG("gfx: SetMember hook failed");
            return false;
        }
        done = true;
        LOG("gfx: SetMember hooked (text capture + observers)");
        return true;
    }

    static const char* Original(const std::string& key) {
        for (auto& w : kWatch) if (key == w.key) return w.original;
        return "";
    }

    static bool Has(const Cap& c, const std::string& k) {
        if (k.empty()) return true;
        for (auto& x : c.keys) if (x == k) return true;
        return false;
    }

    int SetText(const std::string& key, const std::string& value, const std::string& require, const std::string& forbid) {
        std::vector<Cap> caps;
        {
            std::lock_guard<std::mutex> lock(g_cap_mutex);
            // Captures from a HUD movie that has since been destroyed are stale: the movie released our Value.
            std::erase_if(g_caps, [](const Cap& c) { return !(c.val->type & 0x40); });
            for (auto& c : g_caps)
                if (Has(c, key) && Has(c, require) && (forbid.empty() || !Has(c, forbid))) caps.push_back(c);
        }
        if (caps.empty() || !Ready()) return 0;
        auto n = std::make_shared<std::atomic<int>>(0);
        gamethread::Run([caps, key, value, n] {
            try {
                uintptr_t movie = Movie();
                if (!movie) return;
                for (auto& c : caps) {
                    Value v;
                    if (Build(movie, Node::Of(value), v))
                        if (auto set = Slot<SetMemberFn>(c.iface, 0x28)) { set(c.iface, c.pdata, key.c_str(), &v, false); (*n)++; }
                    Release(v);
                }
            } catch (...) {}
        }, 3000);
        return *n;
    }

    int RestoreText(const std::string& key, const std::string& require, const std::string& forbid) {
        return SetText(key, Original(key), require, forbid);
    }

    void LogCaptured() {
        std::lock_guard<std::mutex> lock(g_cap_mutex);
        LOG("gfx: %zu text objects captured", g_caps.size());
        for (auto& c : g_caps) {
            std::string ks;
            for (auto& k : c.keys) ks += " " + k;
            LOG("  obj %llx:%s", (unsigned long long)c.pdata, ks.c_str());
        }
    }

    bool Invoke(unsigned where, unsigned as_off, const std::string& method, std::vector<Node> args) {
        if (!Ready()) return false;
        auto result = std::make_shared<int>(-1);
        auto a = std::make_shared<std::vector<Node>>(std::move(args));
        auto m = std::make_shared<std::string>(method);
        gamethread::Run([where, as_off, a, m, result] {
            try {   // /EHa: also catches access violations
                uintptr_t root = ui::Root(0), el = 0, owner = 0, movie = Movie();
                if (!root || !movie || !memory::Read(root + where, el) || !el || !memory::Read(el + 0x10, owner) || !owner) {
                    LOG("gfx: %s: element 0x%x or movie not found", m->c_str(), where);
                    return;
                }
                std::vector<Value> vals(a->size());
                for (size_t i = 0; i < a->size(); i++) Build(movie, (*a)[i], vals[i]);
                using InvokeFn = bool (*)(uintptr_t, void*, const char*, void*, void*, uint32_t);
                bool ok = ((InvokeFn)scan::FromRva(kInvokeRva))(owner, (void*)(el + as_off), m->c_str(), nullptr,
                                                               vals.empty() ? nullptr : vals.data(), (uint32_t)vals.size());
                for (auto& v : vals) Release(v);
                *result = ok ? 1 : 0;
            } catch (...) {
                *result = 2;
            }
            LOG("gfx: %s at %x:%x with %zu args -> %s", m->c_str(), where, as_off, a->size(),
                *result == 2 ? "CRASHED (caught)" : *result == 1 ? "true" : "false");
        }, 3000);
        // The game's own return value isn't a success flag (Refresh of the results panel returns false and works): ours
        // is "the call ran" (not crashed, element found).
        return *result == 1 || *result == 0;
    }

    bool Message(int type, const std::string& text, const Node* data) {
        if (!Ready()) return false;
        auto result = std::make_shared<int>(-1);
        auto t = std::make_shared<std::string>(text);
        auto d = std::make_shared<Node>(data ? *data : Node{});
        bool has_data = data != nullptr;
        gamethread::Run([type, t, d, has_data, result] {
            try {
                uintptr_t root = ui::Root(0), el = 0, movie = Movie();
                if (!root || !movie || !memory::Read(root + 0x1A0, el) || !el) { LOG("gfx: message: no element"); return; }
                Value v;
                if (has_data) Build(movie, *d, v);
                using Fn = uint64_t (*)(uintptr_t, int, const char*, const Value*, uint8_t, int, float);
                ((Fn)scan::FromRva(kTypedRva))(el, type, t->empty() ? nullptr : t->c_str(), has_data ? &v : nullptr, 1, 0, 0.0f);
                Release(v);
                *result = 1;
            } catch (...) {
                *result = 2;
            }
            LOG("gfx: message type %d '%s'%s -> %s", type, t->c_str(), has_data ? " +data" : "",
                *result == 2 ? "CRASHED (caught)" : "sent");
        }, 3000);
        return *result == 1;
    }

    void SelfTest() {
        gamethread::Run([] {
            try {
                uintptr_t movie = Movie();
                LOG("gfx: movie %llx", (unsigned long long)movie);
                if (!movie) return;
                Node n = Node::Obj({ { "label", Node::Of("test") }, { "count", Node::Of(3) }, { "on", Node::Of(true) } });
                n.Set("list", Node::Arr({ Node::Of(1), Node::Of(2), Node::Of("three") }));
                Value v;
                bool ok = Build(movie, n, v);
                LOG("gfx: self-test object: built=%d type=%x iface=%llx data=%llx", ok, v.type, (unsigned long long)v.iface,
                    (unsigned long long)v.data);
                Release(v);
                LOG("gfx: self-test released");
            } catch (...) {
                LOG("gfx: self-test CRASHED (caught)");
            }
        }, 3000);
    }
}
