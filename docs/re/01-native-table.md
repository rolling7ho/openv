# RE 01 — Native registration table (GTA V Enhanced 1158.16)

Spike date: 2026-10-30. Dump: `research/dumps/1158.16-a.bin`, image base `0x140000000`, SizeOfImage `0x5b45200`.
Never commit dumps (see `.gitignore`).

## Image layout

Two executable sections:
- `.text` @ `0x1000`, size `0x2479400`. Normal code, including every native handler.
- second `.text` @ `0x5123000`, size `0xa22200`. Arxan-obfuscated code. Most native *registration* sites live here.

`.retplne` and `.voltbl` sections are present, so this build is clang/LLVM-compiled.
The image was not relocated under Wine; it loaded at the preferred base.

## Registration function

`register(table, hash, handler)` @ rva `0x920d70` (`rcx` = table, `rdx` = hash, `r8` = handler).
It has 25 direct callers, all BUILTIN natives (e.g. the block at `0x922f20`). The rest of the registration is
inlined or obfuscated in the second `.text`.

Table @ rva `0x3ED4C20` (from `lea rsi, [rip+0x35b1cf4]` at `0x922f25`). It is 256 plain bucket pointers,
indexed by `hash & 0xff`.

Node (0x100 bytes, 7 entries max):

| Offset | Content |
|---|---|
| `+0x00/+0x04` | next ptr lo/hi, each XOR `((u32)node ^ dword[+0x08])` |
| `+0x10 + i*8` | handler i (plain pointer) |
| `+0x48` | count XOR `dword[+0x4c]` XOR `(u32)(node+0x48)` |
| `+0x54 + i*16` | hash lo/hi, each XOR `((u32)(node+0x54+i*16) ^ dword[+0x5c+i*16])` |
| `+0xc8 + i*8` | zeroed at registration (cache slot?) |

The keys come from an LCG (`x*0x343fd + 0x269ec3`) stored at rva `0x286a96c`. This is the same design as Legacy.

`tools/dump/openv-natives.exe` walks the table live:
- 6748 natives in 1076 nodes, max chain length 6
- 6524 unique handlers, because some natives share a stub

## Hash randomization

Only **270 / 6748** table hashes match alloc8or nativedb "universal" hashes:
- mostly BUILTIN
- plus natives first added in builds ~2944–3889

The remaining ~96% are randomized for this build. A crossmap is required.

## Context ABI (same as Legacy)

`handler(ctx)`:
- `[ctx+0x00]` = return-value pointer
- `[ctx+0x10]` = args array, 8 bytes per arg

Floats are read as `movss [args + i*8]`.

## Crossmap feasibility — signals found

1. **Handler address order ≈ nativedb order.**
   - Sorted by handler address, 264/269 anchor steps increase in nativedb order.
   - Of the longest increasing anchor chain (233 anchors, 232 gaps), 185 gaps have *exactly* equal sizes on both sides. That pins ~2026 natives plus the anchors by position alone.
   - A few huge mismatches (±1290) look like whole namespaces ordered differently. Next step: align per namespace block.
2. **Handler signatures.** 6496/6524 handlers start with an Arxan `jmp` chain.
   - `research/trace.py` follows the jumps and tracks the ctx and args registers.
   - It recovers the exact param count for 224/270 anchors (83%).
   - Misses are 0-arg readings: deeper obfuscation (args ptr spilled, indirect jumps).
3. Unused so far: return-type writes (`[ctx]` usage, xmm vs int), the Vector3 return pattern, and call targets shared with
   known handlers.

## Next steps

- Namespace-block alignment (DP or Needleman–Wunsch), scored with param count + return kind. Then measure how many natives are uniquely resolved.
- Improve the tracer: stack spills, `push/ret`, `lea`-based jumps.
- Validate the result in-game: call ~20 resolved natives through a test ASI and check behaviour.
- Legal: nativedb-data has **no LICENSE file**. Use only names/hashes/signatures (facts), never comments.
  Ask the maintainer before publishing generated headers.
