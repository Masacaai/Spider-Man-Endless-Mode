#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// Building Scaleform (GFx) data objects from code, so we can drive the game's own panels the way it does
// (docs/UI_HUNT.md, recordings 2026-10-14): fill an object field by field, then pass it to one method
// (e.g. Refresh(obj) on the activity panel, AddNewMessage(obj) on the medium message).
//
// Static RE (4.0630, 2026-10-15):
//  - All HUD elements share one movie. Its wrapper is [[root+0x1A0] + 0x300] + 8 (the message element's view, as
//    exe+77db70 uses it); the GFx Movie is [wrapper + 0x18]. Movie vtable: +0x160 CreateString(Value*, const char*),
//    +0x170 CreateObject(Value*, className, args, nargs), +0x178 CreateArray(Value*) (exe+319f7e0 / 319f7d0 jump to
//    the first two).
//  - A managed value's ObjectInterface ([value+0x10], vtable at exe+53cf368): slot 1 AddRef, slot 2 Release
//    (iface, value*, data), slot 5 SetMember(iface, data, name, const Value*, isDisplayObject) (exe+3529e30),
//    slot 14 VisitElements, slot 15 PushBack(iface, data, const Value*) (exe+351b670), slot 16 PopBack.
//  - Value layout (0x30): +0x10 interface, +0x18 type (2 bool, 3 int, 4 uint, 5 number, 6 string, 9/10 array/object;
//    0x40 = managed), +0x20 data.
namespace gfx {
    // A description of a value, built off the game thread and turned into GFx values on it.
    struct Node {
        enum Kind { Undefined, Bool, Int, UInt, Number, String, Object, Array } kind = Undefined;
        bool b = false;
        int64_t i = 0;
        double d = 0;
        std::string s;
        std::vector<std::pair<std::string, Node>> fields;   // Object
        std::vector<Node> items;                            // Array

        static Node Of(bool v) { Node n; n.kind = Bool; n.b = v; return n; }
        static Node Of(int v) { Node n; n.kind = Int; n.i = v; return n; }
        static Node U(unsigned v) { Node n; n.kind = UInt; n.i = v; return n; }
        static Node Of(double v) { Node n; n.kind = Number; n.d = v; return n; }
        static Node Of(const char* v) { Node n; n.kind = String; n.s = v; return n; }
        static Node Of(const std::string& v) { Node n; n.kind = String; n.s = v; return n; }
        static Node Obj(std::initializer_list<std::pair<std::string, Node>> f = {}) { Node n; n.kind = Object; n.fields = f; return n; }
        static Node Arr(std::vector<Node> items = {}) { Node n; n.kind = Array; n.items = std::move(items); return n; }

        Node& Set(const std::string& name, Node v) { fields.emplace_back(name, std::move(v)); return *this; }
        Node& Push(Node v) { items.push_back(std::move(v)); return *this; }
    };

    // Invokes an ActionScript method with built arguments on the game thread (blocks; up to 3 s). `where` is the
    // HUD root offset of the element (e.g. 0x1C0) and `as_off` the offset of its ActionScript object (e.g. 0x6E8),
    // as uilog prints them ("at 1c0:6e8"). Returns true if Invoke returned true.
    bool Invoke(unsigned where, unsigned as_off, const std::string& method, std::vector<Node> args);

    // The typed HUD message (exe+77db70) with a data object, as the game sends wave banners (type 21, data
    // {currentWave, maxWave}) and base complete (type 13, label + {icon}).
    bool Message(int type, const std::string& text, const Node* data);

    // True once the one-time self-check (builds an object with every field kind, game thread) passed; false forever if it
    // failed or threw. Every call below returns false without touching the game when this is false, so a wrong vtable
    // offset costs a missing panel, never a crash loop. Runs on first use.
    bool Ready();

    // ---- SetMember hook (exe+3529e30; the one hook on it, shared) ----
    // Text capture: the HUD pushes its fixed texts ("REWARDS", "RETRY", "CONTINUE", "BONUS OBJECTIVES"...) into per-panel
    // objects once, when the HUD loads. We note those objects (AddRef'd) so a panel's title or button text can be changed
    // later (SetText) and put back (RestoreText). Install early, at startup, before the HUD loads.
    bool InstallTextCapture();
    // Sets `key` on every captured object that also has the `require` key and not the `forbid` key (either may be
    // empty). Returns how many objects were changed (0 = nothing captured, or the HUD loaded before the hook).
    int SetText(const std::string& key, const std::string& value, const std::string& require = "", const std::string& forbid = "");
    int RestoreText(const std::string& key, const std::string& require = "", const std::string& forbid = "");
    void LogCaptured();   // objects and keys captured so far

    // A debugging observer called for every SetMember (uilog). Pass nullptr to remove it.
    using SetMemberObserver = void (*)(uintptr_t iface, uint64_t data, const char* name, const void* value);
    void SetObserver(SetMemberObserver fn);

    // Self-test: builds an object with every kind of field and logs what came back (no UI change).
    void SelfTest();
}
