#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The game's Scaleform UI from code (UI hunt, docs/UI_HUNT.md). Static RE 2026-10-13:
//  - HUD root for player i: qword [exe+7c2b5a0 + i*8] (exe+1f063a0 / exe+75e680 return it).
//  - Elements hang off the root; the generic flat message is [root+0x1A0]. An element keeps its movie at +0x10, its
//    ActionScript object (a GFx value) at +0x320 and visibility flags at +0x338 (seen on the message element).
//  - exe+1d1cee0 Invoke(movie, &as_object, "MethodName", result, args, count) calls an ActionScript method; the game
//    uses it 167 times (e.g. "ShowGenericMessage" with a string and two 0..1 numbers).
//  - Argument values are 0x30 bytes: type u32 at +0x18 (2 bool, 5 number, 6 string), data at +0x20.
// Everything here runs on the game thread (it posts and waits), so call it from worker threads.
namespace ui {
    uintptr_t Root(int player = 0);
    std::string ClassName(uintptr_t object);   // MSVC RTTI name of a C++ object ("?" if none)

    void DumpRoot(int player = 0, unsigned bytes = 0x800);   // offset -> pointer -> class, to the log
    void DumpObject(uintptr_t object, unsigned bytes = 0x400);

    struct Arg {
        enum Kind { Number, Int, String, Bool } kind = Number;
        double num = 0;
        std::string str;
        bool b = false;
    };
    // Invoke an ActionScript method on an element (the object at root+offset, or an absolute address if
    // offset_or_addr > 0x10000). Logs the result. Element layout (+0x10 movie, +0x320 AS object) assumed.
    // as_offset: where the element keeps its ActionScript object (seen: +0x30 counter/countdown/alert, +0x38
    // mission objective/option, +0x230 activity message, +0x320 generic message). Value type 3 = int.
    // The activity-complete banner through the game's own wrapper (exe+77b640). type: the int the game passes (0 seen).
    bool ShowActivityBanner(const std::string& title, const std::string& name, int type = 0);

    // The award-tracker toast via the game's wrapper (exe+77b7f0): two ints and three strings (meanings being tested).
    bool ShowAwardToast(int a, int b, const std::string& s1, const std::string& s2, const std::string& s3);

    // The game's typed HUD message (exe+77db70); type 7 = medium message with text (UIMediumMessageAction).
    bool ShowTypedMessage(int type, const std::string& text);

    bool Call(uintptr_t offset_or_addr, unsigned as_offset, const std::string& method, const std::vector<Arg>& args);
}
