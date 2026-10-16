# Contributing

Thanks for helping. This mod pokes at a closed-source game through reverse-engineered addresses, so a few habits matter
more than usual.

## Ground rules

1. **Never commit game material.** No `.actor`/`.config`/texture/model files, no extracted JSON of them, no memory dumps
   (`*.dmp`), no save files. `.gitignore` covers the usual places; check `git status` before every commit. Facts you learned
   (an address, an offset, a field name, an asset *path*) belong in the docs; the bytes do not.
2. **One game build.** Everything targets `Spider-Man.exe` 4.0630.0.0. Addresses are written as `exe+RVA`. If you add one,
   say how you found it (see [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md)) and record it in
   [docs/GAME_INTERNALS.md](docs/GAME_INTERNALS.md) or `recon/`.
3. **Native calls are guarded.** New game functions go in `src/natives.cpp` with a signature *and* an expected RVA; the mod
   refuses to call anything that doesn't resolve exactly where expected. Reads of game memory go through `memory.h`
   (an access violation returns false instead of crashing).
4. **Game state is touched on the game thread.** Use `gamethread::Run`/`Post`; never block the game thread, never call game
   code from your own thread directly.
5. **Don't invent facts.** The docs mark things as verified in game, built but untested, or unknown. Keep that honesty:
   write "unknown" or "[?]" rather than guessing.
6. **A report is a record; a pattern is an issue.** One-off oddities (a single hang, a stuck enemy) go in
   [docs/BUGS.md](docs/BUGS.md) as records. We start fixing something when it keeps happening.

## Workflow

1. Fork, branch from `main`.
2. Follow [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) to build and install, then test in game. Say in the PR what you
   tested and how (the dev command, the steps, what the log showed).
3. Keep changes focused and match the surrounding code: comment density, naming, idiom. Comments explain *why*, with the
   evidence (a log line, an address, a date) when the reason isn't obvious.
4. Run `EndlessMode\tests\run_offline.cmd` if you touched `natives.cpp` or `scan.cpp`, and `plan_offline` if you touched
   `plan.cpp` (see [docs/HOW_TO.md](docs/HOW_TO.md)).
5. Update any docs your change makes wrong. If you learned something about the game, add it to
   `docs/GAME_INTERNALS.md`.
6. Open the PR using the template.

## Style

- C++20, MSVC, `/EHa` (the mod catches access violations on purpose). No exceptions for control flow.
- 4-space indent, braces on the same line, namespaces instead of classes for module state (`static` globals prefixed `g_`).
- Never hard-code a button name in a prompt: use `input::Glyph(...)` or `input::Name(...)`. Players use keyboard, Xbox and
  PlayStation pads.
- Log with `LOG(...)`, one line per event. The log is the main debugging tool.

## Where to start

- [docs/issues/](docs/issues/) lists drafted issues with context, including a few good first ones.
- [docs/LEDGER.md](docs/LEDGER.md) is the backlog and the list of design decisions already made.
