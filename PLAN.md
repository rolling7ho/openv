# OpenV: open-source ScriptHookV replacement

The project name is **OpenV**. "Script Hook V" is Alexander Blade's name and must not be
used in branding.

## Motivation

ScriptHookV v3889.0 (built 15 July 2026) supports game version 1158.13. The current game
version is 1158.16. On 1158.16, ScriptHookV logs
`INIT: Success, game version is VER_EN_1_0_1158_13` and registers `Menyoo.asi`, but the
script never starts, so story mode remains at "Entering Story Mode". The version check
therefore passes while the script-tick hook no longer fires. Every game patch breaks
ScriptHookV until an update is published. The purpose of this project is to make patch
breakage **inexpensive to repair**: signatures and native tables are data files, not code.

## Goals

1. **Drop-in ABI compatibility.** Existing `.asi` plugins (Menyoo, NativeTrainer,
   ScriptHookVDotNet, GameplayFixesV and others) load unmodified against the provided
   `ScriptHookV.dll`.
2. **GTA V Enhanced first** (`GTA5_Enhanced.exe`, Direct3D 12). Legacy support is a stretch
   goal.
3. **Patch resilience.** Pattern-scan all targets, validate every match, and keep per-build
   data (signatures and the native crossmap) in a versioned file that can be updated without
   recompilation.
4. **Single-player only.** Refuse to run in GTA Online or when BattlEye is active. This
   protects users from bans and the project from takedown requests.
5. **Operate under Wine/Gmac and on Windows.**

Non-goals: online features, anti-cheat bypass, redistribution of game assets or code, and a
mod menu.

## ABI surface

The shipped `ScriptHookV.dll` export table contains 24 functions. All are MSVC C++-mangled,
so the project **must build with the MSVC ABI** (clang-cl, lld-link and the xwin SDK on
macOS, or MSVC on Windows). MinGW mangling does not match.

| Group | Exports |
|---|---|
| Scripts | `scriptRegister(HMODULE, void(*)())`, `scriptRegisterAdditionalThread`, `scriptUnregister(HMODULE)`, `scriptUnregister(void(*)())`, `scriptWait(DWORD)`, `scriptsAreLaunchedUsingReloading()` |
| Natives | `nativeInit(UINT64)`, `nativePush64(UINT64)`, `nativeCall() -> PUINT64`, `nativeCanExecuteInThisContext() -> bool` |
| Input | `keyboardHandlerRegister/Unregister(void(*)(DWORD,WORD,BYTE,BOOL,BOOL,BOOL,BOOL))` |
| World | `worldGetAllVehicles/Peds/Objects/Pickups(int*, int) -> int`, `getScriptHandleBaseAddress(int) -> BYTE*` |
| Memory | `getGlobalPtr(int) -> UINT64*` |
| Version | `getGameVersion() -> eGameVersion`, `getGameVersionInfo() -> GameVersionInfo` (structure by value; take the layout from the SDK header) |
| Render | `presentCallbackRegister/Unregister(void(*)(void*))`, `createTexture(const char*) -> int`, `drawTexture(int,int,int,int, float×12)` |

The SDK headers (`main.h`, `types.h`, `enums.h`) constitute the interface specification.
The project writes its own headers and generates its own `natives.h` rather than copying
them.

## Architecture

```
dinput8.dll / version.dll   ASI loader (use Ultimate ASI Loader, MIT; do not write one)
        │ LoadLibrary
ScriptHookV.dll (this project)
  ├─ boot      : waits until the Arxan-protected .text is unpacked, then detects the build
  │              (PE version resource and .text hash)
  ├─ scanner   : runs signatures from data/<build>.toml; every hit is validated (operand
  │              sanity, cross-reference checks); fails closed
  ├─ natives   : registration-table walk, hash-to-handler map; crossmap original-to-current
  │              hashes; scrNativeCallContext (return pointer, arguments, argument count,
  │              Vector3 fixup buffer); handler call
  ├─ scheduler : one fiber per registered script; ticks on the main thread inside a game
  │              script-thread context (swaps the TLS "active scrThread" to a borrowed
  │              GtaThread); scriptWait yields until the deadline
  ├─ input     : WndProc subclass of the game window, dispatching keyboard handlers
  ├─ world     : entity pools (fwBasePool) and the script GUID pool, producing handles and
  │              CEntity pointers
  ├─ globals   : scrProgram globals base, backing getGlobalPtr
  ├─ render    : IDXGISwapChain::Present vtable hook for present callbacks; D3D12 sprite
  │              overlay for textures
  ├─ guard     : online and BattlEye detection; unloads scripts and closes the game as
  │              ScriptHookV does
  └─ log       : openv.log, with the INIT lines ScriptHookV emits plus every signature's
                 hit or miss
```

### Design rules

- **Prefer data and vtable hooks to inline `.text` detours.** Arxan integrity checks
  penalise patched code. The script-tick entry should be a function-pointer or vtable swap
  where one exists; otherwise use a detour on a function that the guards do not cover
  (to be determined in Phase 1).
- **Fail loudly and never hang.** If a required signature is missing, log which one, skip
  loading scripts and let the game run unmodified. ScriptHookV's failure mode, an indefinite
  "Entering Story Mode", is the behaviour to avoid.
- **Data-driven per build.** `data/1158.16.toml` holds signatures, offsets and a crossmap
  reference. Patch day should reduce to rerunning the tools and committing a data file.

## Tooling

- `tools/dump`: dumps the decrypted `.text` of a running game, as was done for the NOD3D12
  fix with `memdump.exe`.
- `tools/sigcheck`: runs all signatures against a dump and reports unique, missing and
  ambiguous hits. CI runs it against stored dumps, which are kept privately and never
  committed.
- `tools/crossmap`: extracts the native registration table from two builds' dumps and diffs
  handler bodies to map hashes from old to new. Seed data is the alloc8or nativedb (names
  and universal hashes); confirm its license before generating headers from it.
- `tools/shvabi`: a test ASI that calls every export and asserts the results, detecting ABI
  drift.

## Phases

| # | Deliverable | Exit test |
|---|---|---|
| 0 | Repository, clang-cl/xwin cross-build on macOS, stub DLL exporting all 24 mangled names that logs calls | Ultimate ASI Loader and Menyoo load the stub without missing-import errors |
| 1 | Research on 1158.16: script-thread tick, active-thread TLS, native table, globals, pools and Arxan-safe hook points; notes in `docs/re/` | Every item has a signature and validation that `sigcheck` reports as unique |
| 2 | Natives, scheduler and scriptWait | NativeTrainer.asi (SDK sample) runs: the menu opens and vehicle spawning works |
| 3 | Keyboard, globals, world pools, script handle to address, version information | Menyoo fully functional (spawner, vehicle spawn, teleport) |
| 4 | Present callback and createTexture/drawTexture (Direct3D 12 overlay) | Plugins that draw textures render correctly, including on D3DMetal under Gmac |
| 5 | Guard (online/BattlEye), reload support (`scriptsAreLaunchedUsingReloading`) and crash-safe unload | Entering Online closes the game cleanly; reloading ASIs works |
| 6 | ScriptHookVDotNet (Enhanced fork) compatibility | SHVDN scripts load. SHVDN has its own memory patterns that can break independently |
| 7 | Patch-day drill: update for 1158.17 and later using only the tools | A new build is supported in hours, with a documented runbook |
| 8 | (Optional) Public release: README, license and contribution guide for signature and crossmap pull requests | Not yet started |

## Risks and unknowns

- **The native hash crossmap for Enhanced is the largest data dependency.** Hashes are
  randomised per build, and no maintained public Enhanced crossmap was identified. The plan
  is to generate one with `tools/crossmap`. This is the principal long-term maintenance
  cost.
- **Arxan.** Runtime decryption and integrity checks are present. Scan only after unpacking;
  hook data and vtables rather than code.
- **Wine and D3DMetal specifics.** Vtable hooking of D3DMetal's swapchain under Wine is
  untested. Fibers and TLS should work under Wine but require verification in Phase 2.
- **Plugins with their own patterns** (Menyoo memory features, SHVDN) can break on patch day
  even when the hook continues to work.
- **Legal.** Ship no game code, dumps or SDK files; only original headers and generated
  data. Rockstar's policy tolerates single-player PC mods, and the online guard keeps the
  project on that side of it.
- **Prior art:** [OpenVHook](https://github.com/DKingAlpha/OpenVHook) is an open-source
  drop-in replacement, apparently from the Legacy era. Read it in Phase 0 for structure and
  check its license before reusing anything.

## Decisions (30 October 2026)

1. Enhanced only.
2. MIT license.
3. Drop-in `ScriptHookV.dll` only. Stability has priority: per-script crash isolation,
   fail-closed boot and no hangs.
4. Integration into Gmac as a per-game "Mods" toggle, after the hook works standalone.
5. Implementation has begun. The source in `src/`, the per-build data in `data/` and the
   research notes in `docs/re/` document the current state; the plan itself is still under
   review.
