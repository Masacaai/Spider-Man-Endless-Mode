# Drafted issues

These are issue drafts, one per file (`NN-slug.md`), with a small front matter block (`title`, `labels`) and the body.
They are the open questions and backlog from [LEDGER.md](../LEDGER.md) and [BUGS.md](../BUGS.md), written so a new contributor can pick
one up.

To publish them on GitHub after the repository exists:

```
gh auth login                                       (once)
powershell -File tools\github\create_issues.ps1 -DryRun     see what would be created
powershell -File tools\github\create_issues.ps1             create them (missing labels are created; existing titles are skipped)
```

Labels worth knowing: `good first issue` (small and well-bounded), `help wanted`, `needs-repro` (seen once; we only fix things that recur),
`owner-decision` (the maintainer decides), `research` (needs reverse engineering).

Once issues exist on GitHub, treat GitHub as the source of truth and update or delete the matching draft here.
