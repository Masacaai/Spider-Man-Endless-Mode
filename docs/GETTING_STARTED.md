# Getting started: build, install, test, repeat

This is the whole loop for changing the mod yourself. Plan on about 30 minutes the first time, then edits take a minute.

## 0. What you need

| Need | Notes |
|---|---|
| Marvel's Spider-Man Remastered (Steam) | `Spider-Man.exe` file version **4.0630.0.0** exactly (right-click the exe, Properties, Details). The mod refuses to run on another build. Turn off automatic updates for the game, or an update will shift every address. |
| Windows 10/11 | The game and all tools are Windows-only |
| Visual Studio 2022 | Community is fine; install the **Desktop development with C++** workload. `build.cmd` expects it at `C:\Program Files\Microsoft Visual Studio\2022\Community` (edit the `VS=` line in `EndlessMode\build.cmd` if yours differs) |
| [Overstrike](https://www.nexusmods.com/marvelsspidermanremastered/mods/4199) 1.8.1 | The mod manager that loads `.script` mods. Needs the .NET 7 Desktop Runtime |
| Python 3.12 | Only for the tools in `tools/` (optional for plain mod changes) |
| Git | obviously |

vcpkg ships with Visual Studio; `build.cmd` uses it to fetch MinHook (the only dependency) the first time you build.

**Back up your game folder (at least `Spider-Man.exe`) and your saves** before you start:
`%USERPROFILE%\Documents\Marvel's Spider-Man Remastered\<steam id>\`.

## 1. Build

```
EndlessMode\build.cmd
```

Output:

- `EndlessMode\build\EndlessMode.dll`: the mod itself (links only KERNEL32; static CRT, static MinHook)
- `EndlessMode\build\EndlessMode.script`: the same DLL zipped with an `info.json`, the format Overstrike installs (made by `pack.ps1`)

It prints `BUILD OK` at the end. A full build takes under a minute. If it fails with a compiler error, that is real;
read the message. Don't build while the game is running and you intend to install: the game locks the DLL.

## 2. Overstrike and the scripts proxy

Overstrike runs script mods through a proxy DLL (`winmm.dll`, copied into the game folder). In Overstrike's settings enable:

- **Enable .script support**
- **Add '-scripts' to commandline.txt**

Then add `EndlessMode.script` and press **Install mods**. Launch the game from **Steam** (the commandline option does the rest).

**Known issue with the stock proxy (as of Overstrike 1.8.0 and 1.8.1):** it can initialize game scanning inside helper
processes (for example `crs-video.exe`). Options, safest first:

1. Build the proxy yourself from upstream (`Tkachov/Overstrike`, folder `ScriptsProxy/`) with the one behavioral change
   "only scan and hook when the host process is exactly `Spider-Man.exe`".
2. Use the community fix (`saltyboosack-blip/MSMR-Overstrike-Script-Proxy-Fix`). It's third-party, unsigned native code
   that the author calls beta; read it before you trust it.
3. Run the stock proxy and watch for oddities.

Don't combine Overstrike's proxy with another mod that also installs a `winmm.dll` (ArkWeb does).

## 3. Install a build for development

After the first Overstrike install, you do **not** need Overstrike again for code changes. With the game **closed**, copy
the new DLL over the installed one:

```
copy EndlessMode\build\EndlessMode.dll "<game folder>\scripts\EndlessMode.dll"
```

The game folder is usually `C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered`.

## 4. Run it

1. Start the game, load a save, and be in free roam.
2. The mod writes `<game>\scripts\EndlessMode.log` (truncated every launch). First check: the top of the log lists every
   native it resolved with `OK`. Anything not `OK` means a signature didn't match: you're on the wrong build or you broke
   `natives.cpp`.
3. Go to a Manhattan dock circle (Campaign or Horde). Stand on it: a hideout-style panel shows your best run and goals. Press the
   START button shown (interact) to travel to Ryker's, then walk into the yard to begin.

Player-facing settings live in `<game>\scripts\EndlessMode.ini` (`[portal]` for circles and keys, `[run]` for pool sizes,
horde cap, body cap, countdown; written with defaults on first launch).

## 5. The dev loop: the command file

You don't need a debugger. The mod polls `<game>\scripts\EndlessMode.cmd` every 250 ms, runs each line once, then
truncates the file. Output goes to `EndlessMode.log`.

```
echo ping > "<game>\scripts\EndlessMode.cmd"
```

Useful commands (all of them are in `src/main.cpp`; search for `cmd == "`):

| Command | What it does |
|---|---|
| `ping` | liveness and hook state |
| `hero` | hero handle, position, health |
| `hook` | install the game-thread hook (the mod does this itself on a run; manual use is for experiments) |
| `near <radius> [component]` | every actor within a radius, with its component names |
| `bots` | every enemy with distance and health |
| `run campaign` / `run horde` | start a run without the circles; options like `wave=N`, `seed=N`, `fast`, `god` |
| `wave stop` | end the run |
| `plan [from] [to]` | print the planned waves without spawning anything |
| `spawn <roster# or path>` / `despawn all` | spawn one enemy next to you / remove everything |
| `stress step=10 max=200` | find how many enemies the game tolerates |
| `gfx selftest` / `gfx caps` / `gfx panel results` | test the UI panel code (see [GAME_INTERNALS.md](GAME_INTERNALS.md#ui-scaleform)) |
| `hud <text>`, `banner <text>`, `toast` | test HUD output |
| `timescale <t> [rate]` | set the world speed (0.25 is slow motion) |
| `tp x y z` | teleport |
| `uilog on` | record every UI call the game makes (to `EndlessMode_uilog.txt`) |
| `fade in|out [s]` | screen fade |
| `inv text <id>` | name and description of an inventory item |
| `peek`, `deref`, `poke` | read or write memory (careful) |

The game only runs mod jobs while its window is focused (it stops ticking in the background), so click into the game
before expecting output.

## 6. Make your first change

Goal: make Campaign waves 20% bigger.

1. Open `EndlessMode/src/plan.cpp`, find `GoonCount` and multiply the result by 1.2.
2. Print the curve without launching the game: `EndlessMode	estsun_plan.cmd 20` (waves 1 to 20). Compare before and after.
3. Close the game, run `build.cmd`, copy the DLL (step 3), launch, start a Campaign run from the dock.
4. Look at `EndlessMode.log`: each wave logs a `plan wN ...` line with its goon count.

More recipes: [HOW_TO.md](HOW_TO.md). How the code is laid out: [ARCHITECTURE.md](ARCHITECTURE.md).

## 7. When something goes wrong

| Symptom | Look at |
|---|---|
| Game crashes at launch | `EndlessMode.log` (last lines), and `%USERPROFILE%\Documents\Marvel's Spider-Man Remastered\*.mdmp`. Wrong build, or a changed native |
| Crash during a run | The `.mdmp` next to the log time. [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#4-reading-a-crash-dump) shows how to find the faulting function |
| Game freezes | [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#5-diagnosing-a-hang). Take two thread snapshots before closing the game |
| Enemies invisible, white cubes, floating heads | Model memory limit; see [GAME_INTERNALS.md](GAME_INTERNALS.md#model-capacity-and-asset-lifetime). Lower `max_types` in the ini |
| Nothing happens at the circles | Check `[portal] enabled=1` and that the circle positions in the ini exist (`dock_set=1`) |
| Mod does nothing at all | The proxy isn't loading it: check Overstrike's settings and that you launched through Steam |
