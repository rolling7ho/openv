# RE 01: native registration table (GTA V Enhanced 1158.16)

Research date: 30 October 2026. Dump: `research/dumps/1158.16-a.bin`, image base
`0x140000000`, SizeOfImage `0x5b45200`. Dumps must never be committed (see `.gitignore`).

## Image layout

Two executable sections:

- `.text` at `0x1000`, size `0x2479400`. Ordinary code, including every native handler.
- A second `.text` at `0x5123000`, size `0xa22200`. Arxan-obfuscated code. Most native
  *registration* sites reside here.

The `.retplne` and `.voltbl` sections are present, which identifies the build as
clang/LLVM-compiled. The image was not relocated under Wine and loaded at its preferred
base.

## Registration function

`register(table, hash, handler)` is at RVA `0x920d70`, with `rcx` holding the table, `rdx`
the hash and `r8` the handler. It has 25 direct callers, all of them BUILTIN natives, for
example the block at `0x922f20`. The remaining registration is inlined or obfuscated in the
second `.text`.

The table is at RVA `0x3ED4C20`, established from `lea rsi, [rip+0x35b1cf4]` at `0x922f25`.
It consists of 256 plain bucket pointers indexed by `hash & 0xff`.

Node layout (0x100 bytes, maximum 7 entries):

| Offset | Content |
|---|---|
| `+0x00/+0x04` | Next pointer low and high, each XORed with `((u32)node ^ dword[+0x08])` |
| `+0x10 + i*8` | Handler i (plain pointer) |
| `+0x48` | Count, XORed with `dword[+0x4c]` and `(u32)(node+0x48)` |
| `+0x54 + i*16` | Hash low and high, each XORed with `((u32)(node+0x54+i*16) ^ dword[+0x5c+i*16])` |
| `+0xc8 + i*8` | Zero at registration (cache slot) |

The keys originate from an LCG (`x*0x343fd + 0x269ec3`) stored at RVA `0x286a96c`. The
design matches Legacy.

`tools/dump/openv-natives.exe` walks the table in a live process:

- 6748 natives in 1076 nodes, with a maximum chain length of 6.
- 6524 unique handlers, because some natives share a stub.

## Hash randomisation

Only **270 of 6748** table hashes match the alloc8or nativedb "universal" hashes:

- mostly BUILTIN;
- plus natives first added in builds approximately 2944 to 3889.

The remaining 96 percent are randomised for this build, so a crossmap is required.

## Context ABI (identical to Legacy)

`handler(ctx)`:

- `[ctx+0x00]` is the return-value pointer.
- `[ctx+0x10]` is the argument array, 8 bytes per argument.

Floats are read with `movss [args + i*8]`.

## Crossmap feasibility: signals found

1. **Handler address order approximates nativedb order.**
   - Sorted by handler address, 264 of 269 anchor steps increase in nativedb order.
   - Of the longest increasing anchor chain (233 anchors, 232 gaps), 185 gaps have exactly
     equal sizes on both sides, which fixes approximately 2026 natives plus the anchors by
     position alone.
   - A few large mismatches (plus or minus 1290) appear to be whole namespaces ordered
     differently. The next step is per-namespace block alignment.
2. **Handler signatures.** 6496 of 6524 handlers begin with an Arxan `jmp` chain.
   - `research/trace.py` follows the jumps and tracks the context and argument registers.
   - It recovers the exact parameter count for 224 of 270 anchors (83 percent).
   - The misses are zero-argument readings caused by deeper obfuscation (spilled argument
     pointers, indirect jumps).
3. Not yet used: return-type writes (`[ctx]` usage, xmm versus integer), the Vector3 return
   pattern, and call targets shared with known handlers.

## Next steps

- Align namespace blocks (dynamic programming or Needleman-Wunsch), scored by parameter
  count and return kind, then measure how many natives are uniquely resolved.
- Improve the tracer for stack spills, `push`/`ret` and `lea`-based jumps.
- Validate the result in game: call approximately 20 resolved natives from a test ASI and
  check the behaviour.
- Legal: nativedb-data has **no LICENSE file**. Use only names, hashes and signatures
  (facts), never comments. Ask the maintainer before publishing generated headers.
