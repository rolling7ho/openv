# AGENTS.md

Operating manual for contributors and coding agents working in `rolling7ho/openv`.

## Safety contract

1. OpenV is single-player only. GTA Online and BattlEye must remain hard-refused.
2. Do not add anti-cheat bypasses or redistribute game assets or proprietary code.
3. Keep all 24 exported symbols MSVC C++-mangled and ABI-compatible with existing `.asi`
   plugins (see `PLAN.md`).

## Environment and layout

- `src/`: C implementation.
- `data/`: shipped per-build native and game-internals maps.
- `tools/`: dump and probe helpers (`tools/dump/*.exe` are committed for `Rebuild OpenV Map.command`).
- `research/`: Python scripts using `.venv` (untracked; Python 3.9+ with `capstone`).
- `Rebuild OpenV Map.command` requires GTA V Enhanced installed in a Gmac bottle and is an
  interactive, slow procedure rather than an automated CI check.

## Writing and communication

Use the formal technical register for documentation, commit messages, pull requests, and
responses: complete sentences, no contractions, no em-dashes, no exclamation marks, and no
decorative emoji. Plain-language user guides such as
`HOW TO FIX MODS AFTER A GTA UPDATE.txt` may keep simple phrasing while avoiding slang.

## Git workflow

- Land every change through a pull request targeting `main`; do not push directly to `main`.
- Delete the working branch after a pull request merges or closes.
- Stage explicit paths so untracked `.venv/`, `research/dumps/`, and temporary `.exe` outputs
  are never committed.
