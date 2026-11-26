# OpenV

An open-source ScriptHookV replacement for Grand Theft Auto V Enhanced (`GTA5_Enhanced.exe`),
designed so that game patch updates require only updated data files rather than recompilation.

## Safety contract

OpenV is **single-player only**. It refuses to run in GTA Online or when BattlEye is active,
contains no anti-cheat bypass, and redistributes no game assets or proprietary code. See
[`PLAN.md`](PLAN.md) for the full design and ABI specification.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | C implementation and MSVC-mangled export definitions |
| `data/` | Per-build native registration map and game-internals signatures (for example `1158.16`) |
| `tools/dump/` | Helpers that dump a running game image and native registration table |
| `tools/probe/` | In-game probe DLL and injector |
| `research/` | Reverse-engineering scripts, crossmaps, and solver experiments |
| `docs/re/` | Documentation for the native table and crossmap derivation |
| `Rebuild OpenV Map.command` | Automated rebuild helper for updating the native map after a game patch |
| `HOW TO FIX MODS AFTER A GTA UPDATE.txt` | Plain-language user guide |

## Building and updating

`ScriptHookV.dll` exports 24 functions using MSVC C++ name mangling and must be built with the
MSVC ABI (`clang-cl`, `lld-link`, and the Windows SDK, or MSVC on Windows).

After a GTA V Enhanced update, `Rebuild OpenV Map.command` launches the game inside a
[Gmac](https://github.com/rolling7ho/gmac) bottle, dumps the updated image and live native
table, and regenerates the data file.

## Related repositories

- [rolling7ho/gmac](https://github.com/rolling7ho/gmac): native macOS application for
  running Windows games on Apple Silicon.

## License

OpenV is licensed under the [MIT License](LICENSE).
