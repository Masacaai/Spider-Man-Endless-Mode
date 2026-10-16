# EndlessMode

Overstrike `.script` mod for Marvel's Spider-Man Remastered **4.0630.0.0**: wave survival, prison-break enemy roster.

This file is the original early-development README and its command table is partly out of date. Start at the repository [README](../README.md) and [docs/GETTING_STARTED.md](../docs/GETTING_STARTED.md); the command list there and in `src/main.cpp` is current.

## Build

```
build.cmd              -> build\EndlessMode.dll, build\EndlessMode.script
tests\run_offline.cmd  -> resolves every native against Spider-Man.exe on disk (no game code runs)
```

Needs VS 2022 (Desktop C++). MinHook comes from vcpkg (manifest + baseline in this folder).
The DLL depends only on KERNEL32 (static CRT, static MinHook).

## How it loads

Overstrike's `winmm.dll` proxy hooks the game's command-line parsing; with `-scripts` it `LoadLibrary`s each name in
`<game>\scripts.txt` from `<game>\scripts\`, then calls `script_enable()`. We start a worker thread there.
That is early in startup, before the world exists.

## Dev control (command file)

The worker polls `<game>\scripts\EndlessMode.cmd` every 250 ms. It runs each line once, then truncates the file.
Output goes to `<game>\scripts\EndlessMode.log`, which is truncated on each launch.

| Command | Effect | Risk |
|---|---|---|
| `ping` | liveness, hook state | none |
| `addrs` | re-resolve natives | none |
| `hero` | hero handle/actor/transform/position | read-only, guarded |
| `peek <addr\|exe+rva> [len]` | hexdump | read-only, guarded |
| `deref <addr\|exe+rva> <off>...` | follow pointer chain | read-only, guarded |
| `hook` / `unhook` | install/remove game-thread hook on `exe+1f745f0` | low: same hook Script Hook uses |
| `ticks` | hook calls total / per second | none |
| `gthero` | `hero`, but run on the game thread | needs `hook` |
| `roster` | list prison enemy paths | none |
| `spawn <roster# \| path> [dist=4] [heading=0]` | spawn at hero + (dist, 0, 0) | **may crash**; logs each step |
| `list` | spawned entities, their sync objects, and BotHealth | read-only |
| `comps <i \| hero>` | dump every component name and pointer of an entity | read-only |
| `despawn <i \| all>` | delete sync object and destroy entity | **may crash** |
| `tp <x> <y> <z>` | teleport the hero (game thread) | low |
| `zone <path>` | stream in one `.zone` via the zone asset manager, wait until loaded | **experimental** |
| `arena rykers` / `arena off` | teleport to Ryker's prison yard (open world); the director then spawns at random points in the yard, 8–30 m away, preferably behind the hero | low |
| `observe on` / `observe off` | log every game spawn, actor/zone load, and sync creation with the calling code's RVA (research) | low: pass-through hooks |
| `arena raft [full]` | load the Raft prison interior zones (art, lighting, nav; `full` adds mission gameplay zones), teleport to the cell block at about (1367, 40.5, -1210) | **experimental** |
| `wave start [n]` / `wave stop` / `wave status` | wave director: rings of prison enemies around the hero, tier mix scales with wave, every 5th wave is heavier; health ≤ 0 counts as a kill and the body is cleaned up after 6 s | **experimental**; needs `hook` |

## Test plan (in game, one step at a time)

1. Launch, load a save, stand on a rooftop. Then `ping`, `hero`: proves loading, logging, and the hero read.
2. `hook`, `ticks`: proves the game-thread hook runs about once per frame. `gthero` proves jobs run.
3. `spawn 0`: one prison melee thug. Watch: does it appear, and does it fight?
4. `despawn 0`.

Back up saves before step 3.
