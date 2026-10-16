#include "hud.h"

#include "gamethread.h"
#include "log.h"
#include "memory.h"
#include "natives.h"
#include "ui.h"

#include <memory>

namespace hud {
    using ShowFn = void (*)(uintptr_t hud, int slot, const char* text, float seconds, int a5, int a6, uint8_t a7, uint8_t a8);
    using ClearFn = void (*)(uintptr_t hud, int slot, uintptr_t a3, uint8_t a4);

    constexpr int kSlots = 0x15;

    bool Show(const std::string& text, float seconds, int slot) {
        auto show = (ShowFn)natives::Get(natives::HudShow);
        uintptr_t obj = natives::Get(natives::HudMessages);
        if (!show || !obj || !gamethread::Installed() || slot < 0 || slot >= kSlots) return false;
        // The game may keep the pointer for the message's lifetime, so the text lives in a buffer we never free
        // while it can be on screen: one persistent buffer per slot, replaced on the next Show.
        static std::shared_ptr<std::string> buffers[kSlots];
        // The game formats the text printf-style (exe+1bc1b10), so a bare '%' ("+20% damage") consumed garbage and
        // printed nonsense numbers (user, 2026-10-13). Escape it.
        std::string escaped;
        for (char c : text) {
            escaped += c;
            if (c == '%') escaped += '%';
        }
        auto buf = std::make_shared<std::string>(escaped);
        gamethread::Post([show, obj, slot, seconds, buf] {
            buffers[slot] = buf;
            show(obj, slot, buffers[slot]->c_str(), seconds, 0, 0, 0, 1);
        });
        LOG("hud[%d]: %s", slot, text.c_str());
        return true;
    }

    bool Banner(const std::string& text, float fallback_seconds) {
        if (ui::ShowTypedMessage(7, text)) { LOG("banner: %s", text.c_str()); return true; }
        return Show(text, fallback_seconds);
    }

    void Countdown(int seconds) {
        ui::Call(0x208, 0x30, "ShowBigCountDown", { ui::Arg{ ui::Arg::Int, (double)seconds } });
    }
    void HideCountdown() { ui::Call(0x208, 0x30, "HideBigCountDown", {}); }

    void Counter(const std::string& label, int current, int max) {
        ui::Arg l; l.kind = ui::Arg::String; l.str = label;
        ui::Arg c{ ui::Arg::Int, (double)current }, m{ ui::Arg::Int, (double)max };
        auto b = [](bool v) { ui::Arg a; a.kind = ui::Arg::Bool; a.b = v; return a; };
        // Args as the game passes them (exe+7708a0): label, current, max, bool, current < max, bool, bool.
        ui::Call(0x218, 0x30, "ShowCounter", { l, c, m, b(false), b(current < max), b(false), b(false) });
    }
    void HideCounter() { ui::Call(0x218, 0x30, "HideCounter", {}); }

    bool Clear(int slot) {
        auto clear = (ClearFn)natives::Get(natives::HudClear);
        uintptr_t obj = natives::Get(natives::HudMessages);
        if (!clear || !obj || !gamethread::Installed() || slot < 0 || slot >= kSlots) return false;
        gamethread::Post([clear, obj, slot] { clear(obj, slot, 0, 0); });
        return true;
    }

    bool Visible() {
        uintptr_t obj = natives::Get(natives::HudMessages);
        uint8_t v = 0;
        return obj && memory::Read(obj + 0x48, v) && v;
    }

    // ScreenFadeAction (exe+205bb20) sends MsgSend(1, type, target.q, target.d, &{float seconds, u8}, 8) where target is
    // its owner's, or the default from TargetLookup(&TargetSys, &out, 0) - which is what we use.
    static bool Fade(natives::Id type_id, float seconds) {
        using SendFn = void (*)(int a, uint64_t type, uint64_t target_q, uint32_t target_d, const void* payload, int size);
        using LookupFn = void (*)(uintptr_t sys, void* out, int zero);
        auto send = (SendFn)natives::Get(natives::MsgSend);
        auto lookup = (LookupFn)natives::Get(natives::TargetLookup);
        uintptr_t sys = natives::Get(natives::TargetSys), type_ptr = natives::Get(type_id);
        uint64_t type = 0;
        if (!send || !lookup || !sys || !type_ptr || !gamethread::Installed() || !memory::Read(type_ptr, type) || !type) return false;
        gamethread::Post([send, lookup, sys, type, seconds] {
            struct { uint64_t q; uint32_t d; uint32_t pad; } target{};
            lookup(sys, &target, 0);
            struct { float seconds; uint8_t flag; uint8_t pad[3]; } payload{ seconds, 0, {} };
            send(1, type, target.q, target.d, &payload, 8);
        });
        return true;
    }
    bool FadeOut(float seconds) { return Fade(natives::FadeOutMsg, seconds); }
    bool FadeIn(float seconds) { return Fade(natives::FadeInMsg, seconds); }
}