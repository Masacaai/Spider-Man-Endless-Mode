#include "input.h"

#include "psinput.h"

#include <Windows.h>
#include <atomic>

namespace input {
    static std::atomic<Device> g_last = Device::Keyboard;
    static bool g_prev[5] = {};

    struct PadState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };

    static bool XPad(PadState& out) {
        using GetStateFn = DWORD(WINAPI*)(DWORD, PadState*);
        static GetStateFn get = [] {
            HMODULE m = LoadLibraryA("xinput1_4.dll");
            if (!m) m = LoadLibraryA("xinput9_1_0.dll");
            return m ? (GetStateFn)GetProcAddress(m, "XInputGetState") : nullptr;
        }();
        if (!get) return false;
        for (DWORD i = 0; i < 4; i++)
            if (get(i, &out) == ERROR_SUCCESS) return true;
        return false;
    }

    static bool Focused() {
        HWND w = GetForegroundWindow();
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        return pid == GetCurrentProcessId();
    }

    void Update() {
        if (!Focused()) return;
        // PlayStation pads also show up through Steam Input as XInput; HID activity wins when it's recent.
        unsigned ps = psinput::LastActivityTick();
        if (ps && GetTickCount() - ps < 1500) { g_last = Device::PlayStation; return; }
        PadState s{};
        if (XPad(s)) {
            bool active = s.buttons != 0 || s.lt > 30 || s.rt > 30 || abs(s.lx) > 12000 || abs(s.ly) > 12000 ||
                          abs(s.rx) > 12000 || abs(s.ry) > 12000;
            if (active) { g_last = psinput::Connected() ? Device::PlayStation : Device::Xbox; return; }
        }
        static const int keys[] = { 'W', 'A', 'S', 'D', 'F', 'E', 'Q', VK_SPACE, VK_SHIFT, VK_LBUTTON, VK_RBUTTON, '1', '2', '3' };
        for (int k : keys)
            if (GetAsyncKeyState(k) & 0x8000) { g_last = Device::Keyboard; return; }
    }

    Device Last() { return g_last; }

    bool Down(Action a) {
        if (!Focused()) return false;
        PadState s{};
        bool x = XPad(s);
        auto xb = [&](WORD m) { return x && (s.buttons & m); };
        switch (a) {
        case Action::Interact:
            return (GetAsyncKeyState('F') & 0x8000) || xb(0x8000 /* Y */) || psinput::Down(psinput::kTriangle);
        // Reward picks on the face buttons (user, 2026-10-13): Square / Triangle / Circle / Cross = Xbox X / Y / B / A.
        case Action::Choice1:
            return (GetAsyncKeyState('1') & 0x8000) || xb(0x4000 /* X */) || psinput::Down(psinput::kSquare);
        case Action::Choice2:
            return (GetAsyncKeyState('2') & 0x8000) || xb(0x8000 /* Y */) || psinput::Down(psinput::kTriangle);
        case Action::Choice3:
            return (GetAsyncKeyState('3') & 0x8000) || xb(0x2000 /* B */) || psinput::Down(psinput::kCircle);
        case Action::Choice4:
            return (GetAsyncKeyState('4') & 0x8000) || xb(0x1000 /* A */) || psinput::Down(psinput::kCross);
        }
        return false;
    }

    bool Pressed(Action a) {
        int i = (int)a;
        bool d = Down(a);
        bool p = d && !g_prev[i];
        g_prev[i] = d;
        return p;
    }

    const char* Name(Action a) {
        Device d = g_last;
        switch (a) {
        case Action::Interact: return d == Device::Keyboard ? "F" : d == Device::Xbox ? "Y" : "Triangle";
        case Action::Choice1: return d == Device::Keyboard ? "1" : d == Device::Xbox ? "X" : "Square";
        case Action::Choice2: return d == Device::Keyboard ? "2" : d == Device::Xbox ? "Y" : "Triangle";
        case Action::Choice3: return d == Device::Keyboard ? "3" : d == Device::Xbox ? "B" : "Circle";
        case Action::Choice4: return d == Device::Keyboard ? "4" : d == Device::Xbox ? "A" : "Cross";
        }
        return "?";
    }

    const char* Glyph(Action a) {
        // The game's own button tokens (Xbox-named; drawn as the active controller's icon). Keyboard: plain keys.
        if (g_last == Device::Keyboard) {
            switch (a) {
            case Action::Interact: return "[F]";
            case Action::Choice1: return "[1]";
            case Action::Choice2: return "[2]";
            case Action::Choice3: return "[3]";
            case Action::Choice4: return "[4]";
            }
        }
        switch (a) {
        case Action::Interact: return "[BTN_Y]";
        case Action::Choice1: return "[BTN_X]";
        case Action::Choice2: return "[BTN_Y]";
        case Action::Choice3: return "[BTN_B]";
        case Action::Choice4: return "[BTN_A]";
        }
        return "";
    }
}
