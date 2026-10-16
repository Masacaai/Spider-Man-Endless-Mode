# Releasing

Releases are GitHub Releases with one asset, `EndlessMode.script` (the Overstrike package built by `pack.ps1`). Alphas are marked **pre-release**.

## Checklist

1. `git status` is clean and `main` is pushed.
2. `EndlessMode\build.cmd` prints `BUILD OK`. The `.script` must be built from the commit you tag: check that no file in `src/` is newer than
   `build\EndlessMode.dll`.
3. Test the exact `.script` you will upload: install it with Overstrike into the game and play one Campaign and one Horde run.
4. Update the version in `EndlessMode/pack.ps1` (`info.json` inside the package) if it changed. Keep it plain `x.y.z`; Overstrike compares versions.
5. Write the notes in `docs/releases/<tag>.md` (what's in it, known issues with issue numbers, what is untested). Leave the line `SHA-256 of ...: __SHA256__` as is.
6. Create the release as a **draft** first, review it on GitHub, then publish:

```
sha256sum EndlessMode/build/EndlessMode.script
sed "s/__SHA256__/<hash>/" docs/releases/v0.1.0-alpha.1.md > notes.md        (don't commit notes.md)
gh release create v0.1.0-alpha.1 EndlessMode/build/EndlessMode.script --draft --prerelease --target main --title "Endless Mode 0.1.0 alpha 1" --notes-file notes.md
```

Publishing the draft creates the tag at `main`. To see/edit/publish: the repository's **Releases** page, or `gh release edit <tag> --draft=false`.

## Tag names

`v<major>.<minor>.<patch>[-alpha.N | -beta.N]`. The game build the release targets is stated in the notes; if a future release supports another build, say
so in the title.
