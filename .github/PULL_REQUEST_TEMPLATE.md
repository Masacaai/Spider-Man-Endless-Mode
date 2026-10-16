## What this changes

## How it was tested

- [ ] Built with `EndlessMode\build.cmd`
- [ ] Tested in game on 4.0630.0.0 (describe the steps and what the log showed)
- [ ] `tests\run_offline.cmd` still resolves all natives (if natives changed)
- [ ] `tests\run_plan.cmd` output checked (if `plan.cpp` changed)

## Checklist

- [ ] No game assets, extracted game data, dumps or saves are included (`git status` checked)
- [ ] New game addresses/layouts are documented in `docs/GAME_INTERNALS.md` with a confidence tag and how they were found
- [ ] New native calls go through `natives.cpp` with an expected RVA and run on the game thread
- [ ] Docs updated where this change makes them wrong
- [ ] Prompts use `input::Glyph`, not hard-coded button names
