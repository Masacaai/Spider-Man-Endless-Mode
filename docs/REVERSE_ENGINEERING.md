# Reverse-engineering guide: how we found things, and how you can too

This is the method behind [GAME_INTERNALS.md](GAME_INTERNALS.md). You don't need to be an expert; every address in the mod
was found with the small toolbox below. Work on a **copy of the game folder** when you experiment, never your only copy.

> **Rules:** don't use leaked Insomniac material (the ALERT glossary mentions a 2023 leak; it's not needed and not allowed here).
> Use public, community-documented formats and your own analysis. Don't commit game files.

## The toolbox

| Tool | Where | What for |
|---|---|---|
| Visual Studio 2022 | system | `cl`, `dumpbin` (`dumpbin /disasm`, `/headers`, `/exports`) |
| Python 3.12 + `pip install minidump pefile capstone` | system | the scripts in `tools/sigtools` |
| ALERT (Tkachov) | clone `https://github.com/Tkachov/ALERT` into `tools/ALERT` | reads the game's archives and `.config`/`.actor` files |
| Your own mod | this repo | a log, a command file and memory-reading commands are the best debugger |

`tools/sigtools` (run the `.py` files with Python; the `.cpp` files build with `cl`, e.g. `cl /O2 /EHsc xrefs.cpp`):

| Script | Usage | Purpose |
|---|---|---|
| `disasm.py` | `disasm.py <rva> <len>` | disassemble the exe at an address (capstone) |
| `funcstrings.py` | `funcstrings.py <rva>...` | strings referenced just before an address: names a function from its logging/profiling strings |
| `strrva.py` | `strrva.py <exe> "<string>"` | RVA of a string in the exe |
| `xrefs.cpp` | `xrefs <exe> <rva>...` | code that references an RVA (rip-relative `lea`/`mov`/`call`) |
| `owner.py` | `owner.py <exe> <rva>...` | function start containing an address, the vtables pointing at it, direct callers |
| `vt2name.py` / `rtti.cpp` | `vt2name.py <exe> <vtable rva>`; `rtti <exe> <Class>` | MSVC RTTI: class name of a vtable, or the vtable and methods of a class |
| `dumpq.py` | `dumpq.py <exe> <rva> <count>` | qwords at an RVA, pointers shown as RVAs / strings / class names |
| `sigscan.cpp` | `sigscan <exe> patterns.txt` | check signatures: hit count, RVA, rip-relative target |
| `findimm.cpp` | `findimm <exe> <hex u32>` | every use of a constant in code |
| `strings.cpp` | `strings <file> [min]` | printable strings with file offsets |
| `asinvokes.py` | `asinvokes.py <exe>` | every ActionScript `Invoke` call site with its method name |
| `dumpstack.py` | `dumpstack.py <crash.mdmp>` | exception and a heuristic stack walk |
| `allstacks.py` | `allstacks.py <dump> [module]` | every thread's stack (for hangs) |
| `snapdump.py` | `snapdump.py <pid> <out.dmp>` | small snapshot of a live or hung process |
| `actor2json.py`, `hash2name.py` | | decode an `.actor`; look up CRC32 name hashes |
| `tools/toctool` | `toctool hash <path>`, `grepall <toc> <needle>` | asset id of a path; which assets contain a byte string |
| `tools/assets/extract.py` | `extract.py <out dir> <path or id>...` | extract assets (outputs are never committed) |

## 1. Finding a game function

### Start from a known landmark

The cheapest route is from something that *names* itself:

1. **A string.** The exe has logging, profiling and ActionScript names. `strrva.py Spider-Man.exe "TargetQuerySystem::ProcessJob"`
   gives an RVA; `xrefs Spider-Man.exe <rva>` lists code that loads it; `owner.py` gives the containing function.
2. **A class.** `rtti Spider-Man.exe Health` prints the vtable and method RVAs. Reading the virtual methods tells you what
   a class does (`Health::OnDamage` was found as vtable slot +0x58).
3. **A call site you already know.** `tools/sigtools/asinvokes.py` lists all 167 ActionScript `Invoke` calls with method names;
   each is a way into a UI feature (that is how `ShowCounter`'s arguments were found).
4. **Community maps.** Script Hook (old mod loader) stored byte-pattern signatures for ~25 natives. We decoded them from its disassembly
   (`tools/sigtools/decode_sh.ps1`, method in `recon/M1_natives_4.0630.md`) and checked each against the exe. Its names told us what to look for.

### Turn an address into a robust signature

A signature is a byte pattern with wildcards that matches exactly once in `.text`, so it survives small code shifts:

1. Disassemble the function (`disasm.py`). Pick 16-30 bytes that include fixed instructions, wildcarding (`??`) any rip-relative
   displacements and immediates that vary.
2. Put it in a patterns file (`name rel32_offset hexbytes`) and run `sigscan`. You want **1 hit**. If 2, lengthen the pattern.
3. Add it to `src/natives.cpp` with the **expected RVA**. The runtime resolver only accepts a match that lands on that RVA; that is what
   protects players on other builds.
4. `tests\run_offline.cmd` resolves all natives against the exe on disk, no game needed.

## 2. Understanding a data structure

1. Find code that reads the field (`findimm` for a constant offset like `0x168`; a watchpoint for "who writes this").
2. Dump the object live: dev commands `peek <addr|exe+rva> <len>` and `deref <addr> <off>...` follow pointer chains; `comps <entity>` lists components.
3. Change a value and see what happens (`poke`). One at a time, and note it.
4. Write the layout down immediately, with a confidence tag, in GAME_INTERNALS.md.

**Hardware watchpoint** (`wp set <addr>`, `wp dump`): arms a debug register on every game thread and records the code that
writes an address. This is how the suit-mod equip function was found: arm the loadout block, equip a mod in the menu, `wp dump`.

**Passive hooks** (`observe on`, `dmgwatch`): log every actor/zone the game loads and every damage change, with the caller's RVA. Play the
game normally (a hideout, a crime) and read what the *game itself* does; then copy it.

## 3. Reading the UI

The HUD is Scaleform. Method: turn on `uilog on`, do the thing in game (open the map, start a hideout, finish a challenge), then read
`EndlessMode_uilog.txt`. It records every ActionScript method call (`invoke Refresh(<obj type 8>) at 1c0:6e8`) and, through the
`SetMember` hook, every field set on the data objects (`set obj=... score = 68702u`). Replicate the same calls with `gfx::Node`.
[UI_HUNT.md](UI_HUNT.md) walks through every recording and what we did with it.

## 4. Reading a crash dump

The game writes dumps to `%USERPROFILE%\Documents\Marvel's Spider-Man Remastered\*.mdmp`.

```
python tools\sigtools\dumpstack.py "<dump>.mdmp"
```

It prints the exception code, the faulting address as `Spider-Man.exe+RVA` and a heuristic stack walk (return addresses found on the
stack; some entries are noise). Then:

1. `owner.py <exe> <fault rva>` names the function and class; `disasm.py <fault rva - 0x40> 0x80` shows the instructions around the crash.
2. `funcstrings.py <rva>...` for the callers on the stack.
3. Is the crash on a null pointer? Which register was null, and where did it come from? That points to the missing precondition.
4. Check `EndlessMode.log`: what was the mod doing in the last seconds?

Worked examples are in [BUGS.md](BUGS.md): grenadier stack overflow (an infinite virtual-call loop), heavies' null mover (missing dependency
assets), crash at death (`exe+319b6a9`, the GFx teardown walking a dangling value, fixed by never freeing registered values).

## 5. Diagnosing a hang

A frozen game writes no dump. If the game stops responding:

1. Check whether it is spinning or deadlocked: Task Manager CPU (or `Get-Process Spider-Man | % CPU` twice, 3 s apart). Rising = spinning;
   flat = waiting.
2. Take **two** snapshots 5 s apart **before closing the game**: `python tools\sigtools\snapdump.py <pid> a.dmp`, again `b.dmp`.
3. `python tools\sigtools\allstacks.py a.dmp Spider-Man`. The main thread is the one running `SharedMain` frames. Look at what it is waiting
   on (`ntdll`/`KERNELBASE` wait frames on top, a game wrapper below) and use `funcstrings.py` on the frames.
4. Compare the two snapshots: same frames = stuck; different = spinning (find the loop).

Example (2026-10-16): the main thread sat in a lock wait inside `TargetQuerySystem::ProcessJob` with all workers idle; see BUGS.md. We
have not found the cause; a hang we can't reproduce stays a record until it repeats.

## 6. Archives and assets

Assets are in `asset_archive` (found via the `toc`). Ids are a CRC64 of the path (ALERT's `crc64` does it; `toctool hash` too).
Common tasks:

- Does this path exist? `toctool check <toc> candidates.txt` or `toctool hash <path>`.
- What references this string? `grepall <toc> "kChallengeCombat"` decompresses every archive and reports assets containing it (slow, but
  found where map icons come from).
- Read an actor: `extract.py out some\path.actor`, then `actor2json.py out\...actor`. `tools/roster/mine_actors.py` does it for all enemy actors
  (class, health, configs, BotData flags).
- Dependencies of an actor (weapons, heads): `tools/assets/deps.py` walks referenced configs.

## 7. Porting to a new game build

A game update moves code. The mod is built so this fails loudly, not silently:

1. Run `tests\run_offline.cmd "<new Spider-Man.exe>"`. It lists each native that no longer resolves at its expected RVA.
2. For each, find the function again (section 1: its signature is probably close; look for the same strings, the same RTTI vtable slot,
   the same callers) and update the signature **and** the expected RVA.
3. Re-verify the data layouts that matter (entity components, handle layout, Health `+0x88`, BotData flag, GFx value layout, HUD
   element offsets). If the SDK (`Tkachov/MSMR-Scripting`) has been updated for the new build, its hardcoded values (`HeroSystem`, `GetActor`)
   are a free cross-check.
4. Everything marked **[RE]** in GAME_INTERNALS is the likeliest to be wrong.
5. Change the version check (`4.0630.0.0`) only after the above.

## Credit and prior art

- **Overstrike and the MSMR Script SDK** (Tkachov): script loading, and the reference for `GetActor` and `HeroSystem`.
- **ALERT** (Tkachov): file formats, archive access, CRC64/CRC32. `tools/toctool` follows its toc format (GPLv3 reference).
- **Script Hook** for MSMR (community-made) and its 4.0630 compatibility patch: its native list was read as a map; no code is used. jedijosh920's Mod Menu showed the correct spawn sequence.
- **ArkWeb** (luki-1): 4.0630-specific recon (frame hook `exe+16244c0`, Havok world pointers) and a workflow worth copying.
- **LDD565 / hbgda** SM2 script template: the signature-scanning approach.
- The Spider-Man PC modding Discord `#scripting` channel holds more docs than we could read.
