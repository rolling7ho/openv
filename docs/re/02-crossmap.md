# RE 02: generating the native crossmap (1158.16)

Objective: map the universal (nativedb) hash to this build's handler without using
ScriptHookV's data.

## Pipeline

| Step | Tool | Output |
|---|---|---|
| Dump the image (game running, after initialisation) | `tools/dump/openv-dump.exe GTA5_Enhanced.exe out.bin` | `research/dumps/<build>-a.bin` |
| Walk the live registration table | `tools/dump/openv-natives.exe 3ED4C20 out.tsv` | current hash to handler (6748) |
| Recover registration order | `research/regorder.py <dump> 920d50 out.json` | 6671 natives in source order |
| Extract handler signatures | `research/features.py <tsv> out.json` (uses `trace.py`) | argument count and return kind per handler |
| Align | `research/solve.py <tsv> <features> out.json --order <regorder>` (`--holdout 0.2` to self-test) | crossmap |
| Merge fallback | `research/merge.py reg.json addr.json final.json` | fills gaps from the handler-address pass |
| In-game check | `tools/probe/inject.exe openv-probe.dll` | `research/probe.log` |

## Key finding: the registration spine

Arxan scatters the registration code, but its execution order is intact:

- A run of `call 0x140920d50` instructions in the main `.text`, one per native, spaced
  0x16 bytes apart, forms a spine.
- Each call is entered from a scattered chunk:
  `lea rdx,[handler]; movabs rcx,<hash>; jmp spine_i`.
- Sorting the spine by address yields source and registration order. That order matches
  nativedb order in 235 of 236 anchor steps.
- It also covers natives whose handlers were merged by identical-code folding, which
  address order cannot place.

The functions at `0x140920d50` and `0x140920d70` both insert into the same table.

## Handler signatures

`research/trace.py` follows Arxan jump chains:

- direct `jmp`;
- `push r; lea r,[X]; xchg [rsp],r; ret`;
- `... lea rsp,[rsp+8]; jmp [rsp-8]`;
- calls used as jumps;
- context spilled to `rsp` or `rbp` slots.

It recovers the argument count for 93 percent of anchors and the return kind for 99 percent.

## Accuracy

- **Holdout:** 20 percent of anchors hidden and then recovered. The registration-order solve
  returns 46 of 47 exact, 0 wrong and 1 unmapped.
- **In game** (story mode, 30 October 2026): 31 checks through `openv-probe.dll`, all
  passing, with no faults. Coverage included:
  - pure math and string natives;
  - platform flags;
  - timers;
  - `PLAYER_ID`, `PLAYER_PED_ID` and a `GET_PLAYER_PED` cross-check;
  - entity health, model and coordinates (Michael at his house);
  - `GET_PLAYER_NAME`.
- **Known incorrect:** `GET_CURRENT_LANGUAGE` and `LOCALIZATION_GET_SYSTEM_DATE_TYPE`.
  - The game registers two natives in that slot that appear to be newer DLC natives absent
    from nativedb.
  - One takes a pointer argument and returns a bool; the other returns a bool.
  - Lesson: at namespace boundaries, natives unknown to nativedb displace the alignment. A
    common signature (zero arguments, integer return) is not evidence on its own.

## Confidence tiers (in the crossmap JSON)

| Tier | Meaning |
|---|---|
| `anchor` | Hash identical to the universal hash |
| `solid` | Features match and the entry is aligned inside a segment where both sides have equal counts with every entry paired |
| `high` | Features match strongly, but the alignment had gaps nearby |
| `medium`, `low` | Weaker feature agreement |

OpenV should load the `anchor` and `solid` tiers by default. For the other tiers it should
verify the handler signature at runtime and log any mismatch when a script first calls that
native.

## Open items

- Natives present in the game but absent from nativedb (approximately 50): detect them as
  unpaired game entries and exclude them from the alignment.
- Script context: some natives read the current script thread from TLS (`gs:[0x58]`, for
  example TIMERA). The probe avoided them; the real hook must run scripts inside a game
  script thread (Phase 2).
- Patch day: rerun the pipeline. Anchors from the previous build's crossmap (old current
  hash to new current hash where the handler code is identical) should provide thousands of
  additional anchors.
