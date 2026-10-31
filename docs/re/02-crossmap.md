# RE 02 — Generating the native crossmap (1158.16)

Goal: universal (nativedb) hash → this build's handler, without using ScriptHookV's data.

## Pipeline

| Step | Tool | Output |
|---|---|---|
| Dump image (game running, after init) | `tools/dump/openv-dump.exe GTA5_Enhanced.exe out.bin` | `research/dumps/<build>-a.bin` |
| Walk live registration table | `tools/dump/openv-natives.exe 3ED4C20 out.tsv` | current hash → handler (6748) |
| Registration order | `research/regorder.py <dump> 920d50 out.json` | 6671 natives in source order |
| Handler signatures | `research/features.py <tsv> out.json` (uses `trace.py`) | argc + return kind per handler |
| Align | `research/solve.py <tsv> <features> out.json --order <regorder>` (`--holdout 0.2` to self-test) | crossmap |
| Fallback merge | `research/merge.py reg.json addr.json final.json` | fills gaps from the handler-address pass |
| In-game check | `tools/probe/inject.exe openv-probe.dll` | `research/probe.log` |

## Key finding: the registration spine

Arxan scatters the registration code, but its *execution* order is intact:
- A run of `call 0x140920d50` instructions in the main `.text` (one per native, 0x16 bytes apart) forms a spine.
- Each call is entered from a scattered chunk: `lea rdx,[handler]; movabs rcx,<hash>; jmp spine_i`.
- Sorting the spine by address gives source/registration order. That order matches nativedb order in 235/236 anchor steps.
- It also covers natives whose handlers were merged by identical-code folding, which address order can't place.

The `0x140920d50` and `0x140920d70` functions both insert into the same table.

## Handler signatures

`research/trace.py` follows Arxan jump chains:
- direct `jmp`
- `push r; lea r,[X]; xchg [rsp],r; ret`
- `... lea rsp,[rsp+8]; jmp [rsp-8]`
- calls used as jumps
- ctx spilled to rsp/rbp slots

It recovers argc for 93% and return kind for 99% of the anchors.

## Accuracy

- **Holdout:** 20% of anchors hidden, then recovered. Registration-order solve: 46/47 exact, 0 wrong, 1 unmapped.
- **In game (story mode, 2026-10-30):** 31 checks via `openv-probe.dll`, 31 pass, no faults. Covered:
  - pure math/string natives
  - platform flags
  - timers
  - `PLAYER_ID` / `PLAYER_PED_ID` / `GET_PLAYER_PED` cross-check
  - entity health/model/coords (Michael at his house)
  - `GET_PLAYER_NAME`
- **Known wrong:** `GET_CURRENT_LANGUAGE` and `LOCALIZATION_GET_SYSTEM_DATE_TYPE`.
  - The game registers two natives in that slot that look like newer DLC natives missing from nativedb.
  - One has a pointer arg plus a bool return; the other returns a bool.
  - Lesson: at namespace boundaries, natives unknown to nativedb push the alignment off.
  - A common signature (0 args, int) is not evidence on its own.

## Confidence tiers (in the crossmap JSON)

| Tier | Meaning |
|---|---|
| `anchor` | hash identical to universal |
| `solid` | features match, and aligned inside a segment where both sides have equal counts with everything paired |
| `high` | features match strongly, but the alignment had gaps nearby |
| `medium` / `low` | weaker feature agreement |

OpenV should load `anchor` + `solid` by default. For the other tiers it should check the handler signature at runtime and log
any mismatch when a script first calls that native.

## Open items

- Natives the game has but nativedb doesn't (~50): detect them as unpaired game entries, and keep them out of the alignment.
- Script context: some natives read the current script thread from TLS (`gs:[0x58]`, e.g. TIMERA). The probe avoided them;
  the real hook must run scripts inside a game script thread (Phase 2).
- Patch day: re-run the pipeline. Anchors from the previous build's crossmap (old current hash → new current hash where
  handler code is identical) should add thousands of extra anchors.
