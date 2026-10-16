#include "events.h"

#include "gamethread.h"
#include "log.h"
#include "natives.h"

#include <cstdlib>
#include <map>

namespace events {
    using SendEventFn = uintptr_t (*)(uintptr_t sys, uint32_t type, uint32_t target, const uint32_t* source,
                                      int32_t a5, uint8_t a6, uint64_t a7, uint32_t a8, uint8_t a9, float f1,
                                      float f2, const void* payload, uint64_t payload_size);
    using GroupAlertFn = void (*)(uintptr_t group, uint32_t alerter, uint8_t flag);

    uint32_t TypeHash(const std::string& s) {
        static const std::map<std::string, uint32_t> known = {
            { "ScriptedGroupAlertEvent", 0x3A57F5AD },
            { "BotAlertGroupEvent", 0xF9C1D45E },
            { "BotAlertedEvent", 0xD8ABB208 },
            { "GroupAlertedEvent", 0x0FD8F64B },
            { "HeroPositionTriangulatedEvent", 0x2C438ACA },
            { "BotHeardLoudSoundEvent", 0x3D05C385 },
            { "DeathSeenEvent", 0x3E9BA834 },
            { "WeaponFiredEvent", 0xF6607648 },
            { "HeroSetAlertedEvent", 0x80B26DFF },
        };
        auto it = known.find(s);
        if (it != known.end()) return it->second;
        return (uint32_t)strtoul(s.c_str(), nullptr, 16);
    }

    bool Send(uint32_t type, uint32_t target, uint32_t source, float f1, float f2) {
        auto send = (SendEventFn)natives::Get(natives::SendEvent);
        uintptr_t sys = natives::Get(natives::EventSys);
        if (!send || !sys || !gamethread::Installed()) { LOG("event: not ready"); return false; }
        gamethread::Post([=] {
            uint32_t src = source;
            uintptr_t ev = send(sys, type, target, source ? &src : nullptr, 1, 0, 0, 0, 1, f1, f2, nullptr, 0);
            LOG("event: sent %08x to %08x (source %08x) -> %llx", type, target, source, (unsigned long long)ev);
        });
        return true;
    }

    bool AlertGroup(uintptr_t group, uint32_t alerter) {
        auto alert = (GroupAlertFn)natives::Get(natives::GroupAlert);
        if (!alert || !group || !gamethread::Installed()) { LOG("grpalert: not ready"); return false; }
        gamethread::Post([=] {
            alert(group, alerter, 1);
            LOG("grpalert: group %llx alerted by %08x", (unsigned long long)group, alerter);
        });
        return true;
    }
}
