#pragma once

// UI call recorder (UI hunt, 2026-10-14): logs every ActionScript method the game invokes on its movies (exe+1d1cee0)
// and every typed HUD message (exe+77db70), with arguments, to scripts\EndlessMode_uilog.txt - so we can watch how
// the game drives hideout and Taskmaster challenge HUDs and copy it. The same call with the same arguments is logged
// at most once every 3 s (some run every frame). `uilog on|off`.
namespace uilog {
    bool Enable(bool on);
    bool Enabled();
}
