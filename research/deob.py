# Print de-obfuscated code for a function: follows Arxan jmp chains, push/lea/xchg/ret and
# lea rsp,[rsp+8]; jmp [rsp-8] trampolines; conditional branches are explored as separate blocks.
# usage: deob.py <dump> <rva hex | native NAME> [max_instructions] [--calls]  (--calls: also descend into direct calls)
import sys, os, json, capstone, re
os.environ['OPENV_DUMP'] = sys.argv[1]; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image import dump, TEXT, BASE as base
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
intext = lambda a: any(s <= a < e for s, e in TEXT)
target = sys.argv[2]
if target.startswith('native'):
    name = target.split()[1]
    c = json.load(open('research/crossmap-final-1158.16.json'))['natives']
    m = [v for v in c.values() if v['name'] == name][0]
    start = int(m['handler'], 16); print(f'; {name} handler {start:#x} ({m["conf"]})')
else:
    start = int(target, 16)
limit = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith('--') else 300
calls = '--calls' in sys.argv
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')

def rip_abs(ins):
    m = RIP.search(ins.op_str)
    if not m: return None
    off = int(m.group(2), 16) * (1 if m.group(1) == '+' else -1)
    return ins.address - base + ins.size + off

seen, work, printed = set(), [start], 0
while work and printed < limit:
    pc = work.pop(0)
    if pc in seen: continue
    print(f'loc_{pc:x}:')
    pending = None  # trampoline target from lea r,[rip+X]
    while printed < limit and intext(pc):
        if pc in seen: print(f'    -> loc_{pc:x}'); break
        seen.add(pc)
        ins = next(md.disasm(dump[pc:pc + 16], base + pc), None)
        if ins is None: print('    ??'); break
        m, op, nxt = ins.mnemonic, ins.op_str, pc + ins.size
        tramp = False
        if m == 'push' and not op.startswith('0x'): tramp = True
        elif m == 'lea' and 'rip' in op:
            nxt_ins = next(md.disasm(dump[nxt:nxt + 16], base + nxt), None)
            if nxt_ins and nxt_ins.mnemonic == 'xchg' and '[rsp]' in nxt_ins.op_str:
                pending = rip_abs(ins); tramp = True
        elif m == 'xchg' and '[rsp]' in op: tramp = True
        elif m == 'lea' and op.startswith('rsp, [rsp + 8]') and pending is not None: tramp = True
        elif m == 'jmp' and op == 'qword ptr [rsp - 8]' and pending is not None:
            pc, pending = pending, None; continue
        elif m == 'ret' and pending is not None:
            pc, pending = pending, None; continue
        if m == 'jmp' and op.startswith('0x'):
            pc = int(op, 16) - base; continue
        if tramp and pending is not None or (tramp and m == 'push'):
            # keep pushes visible unless they are part of a trampoline we resolved
            if m == 'push':
                nx = next(md.disasm(dump[nxt:nxt + 16], base + nxt), None)
                if not (nx and nx.mnemonic == 'lea' and 'rip' in nx.op_str):
                    print(f'    {pc:08x}  {m} {op}'); printed += 1
            pc = nxt; continue
        extra = ''
        a = rip_abs(ins)
        if a is not None: extra = f'    ; [{a:#x}]'
        print(f'    {pc:08x}  {m} {op}{extra}'); printed += 1
        if m == 'ret' or m == 'int3': break
        if m == 'jmp': print('    ; indirect jmp'); break
        if m.startswith('j') and op.startswith('0x'): work.append(int(op, 16) - base)
        if m == 'call' and calls and op.startswith('0x'): work.append(int(op, 16) - base)
        pc = nxt
