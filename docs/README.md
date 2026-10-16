# Documentation index

If you are new, read these in order:

1. **[GETTING_STARTED.md](GETTING_STARTED.md)**: install the tools, build, install into the game, the command-file dev loop, your first change.
2. **[ARCHITECTURE.md](ARCHITECTURE.md)**: what every source file does, the threads, how a run flows, which files the mod writes.
3. **[HOW_TO.md](HOW_TO.md)**: recipes for the changes people actually want to make.
4. **[GAME_INTERNALS.md](GAME_INTERNALS.md)**: the knowledge base about the game itself (addresses, structures, UI, crashes).
5. **[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)**: how that knowledge was found, with the tools in `tools/`; how to port to a new build.

## Reference and history

| Doc | What it holds |
|---|---|
| [LEDGER.md](LEDGER.md) | Design decisions (read before proposing gameplay changes) and the backlog, with dates |
| [BUGS.md](BUGS.md) | Every crash, hang and oddity: symptom, dump analysis, cause, status. "Record only" entries are one-offs |
| [UI_HUNT.md](UI_HUNT.md) | The chronology of finding and driving the game's HUD: every recording, what each panel accepts |
| [ENDLESS_MODE.md](ENDLESS_MODE.md) | Original design and dev reference (historical in places) |
| [ROGUELIKE.md](ROGUELIKE.md) | The Campaign boon/curse system: design |
| [SCORING.md](SCORING.md) | Horde style meter, ranks and scoring: design |
| [NEXT_SESSION_TESTS.md](NEXT_SESSION_TESTS.md) | Test checklists written after each batch of changes (also a model for how to write a test plan) |
| [issues/](issues/) | Drafted GitHub issues |
| [RELEASING.md](RELEASING.md) | How to cut a release |
| [../AGENTS.md](../AGENTS.md) | Rules and commands for AI coding agents |
| [`../recon/M1_natives_4.0630.md`](../recon/M1_natives_4.0630.md) | The first reverse-engineering pass: how the natives were decoded |

## Conventions in these docs

- **Dates** are the day something was found or decided (`2026-10-15`).
- **Confidence tags** in GAME_INTERNALS: `[verified]` tested in game, `[RE]` read from disassembly only, `[?]` unknown or a guess.
- Addresses are `exe+RVA` for `Spider-Man.exe` **4.0630.0.0**.
- "User" in older docs means the project's owner, who did all the in-game testing.
