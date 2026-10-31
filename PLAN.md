# OpenV — open-source ScriptHookV replacement — plan

Name: **OpenV**. Avoid using "Script Hook V" in branding; that is Alexander Blade's name.

## Why

ScriptHookV v3889.0 (built 2026-07-15) supports game 1158.13. The game is now 1158.16. On .16, SHV logs
`INIT: Success, game version is VER_EN_1_0_1158_13` and registers `Menyoo.asi`, but the script never starts,
so story mode hangs at "Entering Story Mode". In other words, its version check passes while its script-tick hook
no longer fires. Every game patch breaks SHV until one person ships an update. The point of this project is
to make patch breakage **cheap to fix**: signatures and native tables are data files, not code.

## Goals

1. **Drop-in ABI compatibility.** Existing `.asi` plugins (Menyoo, NativeTrainer, ScriptHookVDotNet,
   GameplayFixesV, …) load unmodified, against our `ScriptHookV.dll`.
2. **GTA V Enhanced first** (`GTA5_Enhanced.exe`, D3D12). Legacy is a stretch goal.
3. **Patch resilience.** Pattern-scan everything, validate every hit, and keep per-build data
   (signatures and the native crossmap) in a versioned file that can be updated without recompiling.
4. **Single-player only.** Hard-refuse to run in GTA Online or when BattlEye is active. This protects users from bans and the project from takedowns.
5. **Works under Wine/Vodka and on Windows.**

Non-goals: online features, anti-cheat bypass, game asset/code redistribution, a mod menu.

## ABI surface (from the shipped ScriptHookV.dll export table, 24 functions)

All are MSVC C++-mangled. We must export these exact names, so we **must build with the MSVC ABI**
(clang-cl + lld-link + xwin SDK on macOS, or MSVC on Windows). MinGW mangling will not match.

| Group | Exports |
|---|---|
| Scripts | `scriptRegister(HMODULE, void(*)())`, `scriptRegisterAdditionalThread`, `scriptUnregister(HMODULE)`, `scriptUnregister(void(*)())`, `scriptWait(DWORD)`, `scriptsAreLaunchedUsingReloading()` |
| Natives | `nativeInit(UINT64)`, `nativePush64(UINT64)`, `nativeCall() -> PUINT64`, `nativeCanExecuteInThisContext() -> bool` |
| Input | `keyboardHandlerRegister/Unregister(void(*)(DWORD,WORD,BYTE,BOOL,BOOL,BOOL,BOOL))` |
| World | `worldGetAllVehicles/Peds/Objects/Pickups(int*, int) -> int`, `getScriptHandleBaseAddress(int) -> BYTE*` |
| Memory | `getGlobalPtr(int) -> UINT64*` |
| Version | `getGameVersion() -> eGameVersion`, `getGameVersionInfo() -> GameVersionInfo` (struct by value: take the layout from the SDK header) |
| Render | `presentCallbackRegister/Unregister(void(*)(void*))`, `createTexture(const char*) -> int`, `drawTexture(int,int,int,int, float×12)` |

The SDK headers (`main.h`, `types.h`, `enums.h`) are the interface spec. We write our own headers
and generate our own `natives.h` instead of copying theirs.

## Architecture

```
dinput8.dll / version.dll   ASI loader (use Ultimate ASI Loader, MIT; don't write our own)
        │ LoadLibrary
ScriptHookV.dll (ours)
  ├─ boot      : waits until the Arxan-protected .text is unpacked, then detects the build (PE version resource + .text hash)
  ├─ scanner   : runs signatures from data/<build>.toml; every hit is validated (operand sanity, xref checks); fails closed
  ├─ natives   : registration-table walk, hash→handler map; crossmap original→current hashes; scrNativeCallContext
  │              (return ptr, args, arg count, Vector3 fixup buffer); handler call
  ├─ scheduler : one fiber per registered script; ticks on the main thread inside a game script-thread context
  │              (swap the TLS "active scrThread" to a borrowed GtaThread); scriptWait = yield until the deadline
  ├─ input     : WndProc subclass of the game window → keyboard handlers
  ├─ world     : entity pools (fwBasePool) + script GUID pool → handles / CEntity*
  ├─ globals   : scrProgram globals base → getGlobalPtr
  ├─ render    : IDXGISwapChain::Present vtable hook → present callbacks; D3D12 sprite overlay for textures
  ├─ guard     : online/BattlEye detection → unload scripts + close the game, as SHV does
  └─ log       : openv.log, with the same useful INIT lines as SHV plus every signature's hit/miss
```

Design rules:
- **Prefer data and vtable hooks to inline `.text` detours.** Arxan integrity checks punish patched code.
  The script-tick entry should be a function-pointer/vtable swap if one exists. Otherwise, use a detour
  on a function the guards don't cover (find this out in Phase 1).
- **Fail loudly, never hang.** If a required signature misses, log which one, skip loading the scripts, and let the game run vanilla.
  (SHV's failure mode, an infinite "Entering Story Mode", is the thing to beat.)
- **Data-driven per build:** `data/1158.16.toml` = signatures + offsets + crossmap reference. A patch day
  ideally means "re-run tools, commit a data file".

## Tooling (part of the project, not optional)

- `tools/dump` — dumps the decrypted `.text` of a running game (we already did this for the NOD3D12 fix with `memdump.exe`).
- `tools/sigcheck` — runs all signatures against a dump and reports unique/missing/ambiguous hits. CI runs it against stored dumps (kept privately; never committed).
- `tools/crossmap` — extracts the native registration table from two builds' dumps and diffs handler
  bodies to map hashes old→new. Seed: alloc8or nativedb (names + universal hashes); confirm its license before generating headers from it.
- `tools/shvabi` — a test ASI that calls every export and asserts results (catches ABI drift).

## Phases

| # | Deliverable | Exit test |
|---|---|---|
| 0 | Repo, clang-cl/xwin cross-build on macOS, stub DLL exporting all 24 mangled names that logs calls | Ultimate ASI Loader + Menyoo load the stub without missing-import errors |
| 1 | Research on 1158.16: script-thread tick, active-thread TLS, native table, globals, pools, Arxan-safe hook points. Notes go in `docs/re/` | Every item has a signature + validation that `sigcheck` reports as unique |
| 2 | Natives + scheduler + scriptWait | NativeTrainer.asi (SDK sample) runs: menu opens, spawning a car works |
| 3 | Keyboard, globals, world pools, script handle → address, version info | Menyoo fully functional (spooner, vehicle spawn, teleport) |
| 4 | Present callback + createTexture/drawTexture (D3D12 overlay) | Plugins that draw textures render correctly, on D3DMetal under Vodka |
| 5 | Guard (online/BattlEye), reload support (`scriptsAreLaunchedUsingReloading`), crash-safe unload | Entering Online closes the game cleanly; reloading ASIs works |
| 6 | ScriptHookVDotNet (Enhanced fork) compatibility | SHVDN scripts load. Caveat: SHVDN has its own memory patterns that can break independently |
| 7 | Patch-day drill: when 1158.17+ ships, update using only the tools | New build supported in hours, not weeks; documented runbook |
| 8 | (Optional) Public release: README, license, contribution guide for signature/crossmap PRs | — |

## Risks and unknowns

- **Native hash crossmap for Enhanced** is the biggest data dependency. Hashes are re-randomized per build,
  and I found no maintained public Enhanced crossmap. Plan to generate our own (`tools/crossmap`).
  This is the main long-term maintenance cost.
- **Arxan.** Runtime decryption plus integrity checks. Scan only after unpack; hook data and vtables, not code.
- **Wine/D3DMetal specifics.** Present-vtable hooking on D3DMetal's swapchain is untested. Fibers and TLS under
  Wine should be fine but need verifying in Phase 2.
- **Plugins with their own patterns** (Menyoo memory features, SHVDN) can still break on patch day even if the hook doesn't.
- **Legal.** Ship no game code, dumps or SDK files. Our own headers and generated data only. Rockstar's policy
  tolerates single-player PC mods. The online guard keeps us clearly on that side.
- **Prior art:** [OpenVHook](https://github.com/DKingAlpha/OpenVHook) (open-source drop-in, apparently Legacy-era).
  Read it in Phase 0 for structure, and check its license before borrowing anything.

## Decisions (2026-10-30)

1. Enhanced only.
2. MIT license.
3. Drop-in `ScriptHookV.dll` only. Priority is stability: per-script crash isolation, fail-closed boot, no hangs.
4. Yes, integrate into Vodka as a per-game "Mods" toggle, after the hook works standalone.
5. Not started yet. The plan is still being reviewed.
