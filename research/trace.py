# Follow Arxan-obfuscated native handlers and extract a signature:
#   args  = set of arg slots read from the args array ([ctx+0x10] + i*8)
#   ret   = kind of value written through the return pointer [ctx+0]: None, 'int', 'float', 'vec3'
# Handles jmp chains and the push/lea/xchg [rsp]/ret indirect-jump idiom by tracking a tiny symbolic
# state: which registers hold ctx / args ptr / ret ptr / known constants, plus a small stack model.
import capstone, re, json

import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image import dump, TEXT, BASE as base
import os; DEBUG = os.environ.get("TRACE_DEBUG"); DESCEND = not os.environ.get("TRACE_NODESCEND")
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MEM = re.compile(r'\[(\w+)(?: ([+-]) (0x[0-9a-f]+|\d+))?\]')

R64 = {}
for full, aliases in {
    'rax': 'eax ax al', 'rbx': 'ebx bx bl', 'rcx': 'ecx cx cl', 'rdx': 'edx dx dl', 'rsi': 'esi si sil',
    'rdi': 'edi di dil', 'rbp': 'ebp bp bpl', 'rsp': 'esp sp spl'}.items():
    R64[full] = full
    for a in aliases.split(): R64[a] = full
for i in range(8, 16):
    for s in ('', 'd', 'w', 'b'): R64[f'r{i}{s}'] = f'r{i}'


def intext(a): return any(s <= a < e for s, e in TEXT)


def ins_at(pc):
    return next(md.disasm(dump[pc:pc + 16], base + pc), None)


def trace(entry, limit=600):
    args, ret, seen, steps = set(), set(), set(), 0
    # state: (regs dict name->tag, stack list of tags); tags: 'ctx', 'args', 'ret', ('c', value), ('spill', tag)
    work = [(entry, {'rcx': 'ctx'}, [], {})]
    while work and steps < limit:
        pc, regs, stack, slots = work.pop()
        regs, stack, slots = dict(regs), list(stack), dict(slots)
        popped = None
        while steps < limit and intext(pc) and pc not in seen:
            seen.add(pc); steps += 1
            ins = ins_at(pc)
            if DEBUG and ins: print(f"{pc:08x} {ins.mnemonic} {ins.op_str}   regs={ {k:v for k,v in regs.items() if v} } stack={stack[-3:]}")
            if ins is None: break
            m, op = ins.mnemonic, ins.op_str
            ops = [o.strip() for o in op.split(',')] if op else []
            dst = R64.get(ops[0]) if ops else None
            src = R64.get(ops[1]) if len(ops) > 1 else None
            mem = MEM.search(op)
            nxt = pc + ins.size

            def memoff():
                return int(mem.group(3) or '0', 0) * (-1 if mem.group(2) == '-' else 1)

            if m == 'push':
                stack.append(regs.get(dst) if dst else None)
            elif m == 'pop':
                v = stack.pop() if stack else None
                if dst: regs[dst] = v
            elif m == 'lea' and dst == 'rsp' and mem and mem.group(1) == 'rsp':
                k = memoff()
                if k > 0:
                    for _ in range(k // 8): popped = stack.pop() if stack else None
                elif k < 0: stack.extend([None] * (-k // 8))
            elif m == 'jmp' and mem and mem.group(1) == 'rsp' and memoff() == -8:
                if isinstance(popped, tuple) and popped[0] == 'c': pc = popped[1]; continue
                break
            elif m == 'lea' and dst and mem and mem.group(1) == 'rip':
                regs[dst] = ('c', nxt + memoff())
            elif m == 'xchg' and mem and mem.group(1) == 'rsp' and memoff() == 0 and (src or dst):
                r = src if src else dst
                if stack: stack[-1], regs[r] = regs.get(r), stack[-1]
            elif m in ('mov', 'movsxd', 'movzx') and dst and src and not mem:
                regs[dst] = regs.get(src)
            elif m == 'mov' and dst and mem and '[' not in ops[0]:
                b = R64.get(mem.group(1)); tag = regs.get(b); off = memoff()
                if tag == 'ctx' and off == 0x10: regs[dst] = 'args'
                elif tag == 'ctx' and off == 0: regs[dst] = 'ret'
                elif tag == 'args' and off >= 0 and off % 8 == 0: args.add(off // 8); regs[dst] = None
                elif b in ('rsp', 'rbp') and (b, off) in slots: regs[dst] = slots[(b, off)]
                else: regs[dst] = None
            elif m == 'mov' and mem and '[' in ops[0] and src:
                b = R64.get(mem.group(1)); off = memoff()
                if b in ('rsp', 'rbp') and regs.get(src) in ('ctx', 'args', 'ret'): slots[(b, off)] = regs[src]
                elif regs.get(b) == 'ret':
                    ret.add(('int', off))
            elif mem and '[' not in (ops[0] if ops else ''):
                b = R64.get(mem.group(1)); off = memoff()
                if regs.get(b) == 'args' and off >= 0 and off % 8 == 0: args.add(off // 8)
                if dst: regs[dst] = None
            elif mem and '[' in ops[0] and len(ops) > 1:
                b = R64.get(mem.group(1))
                if regs.get(b) == 'ret':
                    ret.add(('float' if ops[1].startswith('xmm') else 'int', memoff()))
            elif dst and m not in ('cmp', 'test') and not m.startswith('j'):
                if m in ('sub', 'add') and dst == 'rsp': pass
                else: regs[dst] = None

            if m == 'ret':
                tgt = stack.pop() if stack else None
                if isinstance(tgt, tuple) and tgt[0] == 'c': pc = tgt[1]; continue
                break
            if m == 'jmp':
                if op.startswith('0x'): pc = int(op, 16) - base; continue
                t = regs.get(dst)
                if isinstance(t, tuple) and t[0] == 'c': pc = t[1]; continue
                break
            if m == 'call':
                tgt = int(op, 16) - base if op.startswith('0x') else None
                if tgt is not None and intext(tgt) and DESCEND:
                    # descend with the return address on the stack (Arxan uses call as a jump and later
                    # pops/returns through it); ctx may be passed on to a helper in rcx
                    stack.append(('c', nxt)); pc = tgt; continue
                for r in ('rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'): regs[r] = None
            if m.startswith('j') and op.startswith('0x'):
                work.append((int(op, 16) - base, dict(regs), list(stack), dict(slots)))
            pc = nxt
    kinds = {k for k, _ in ret}; offs = {o for _, o in ret}
    rk = None
    if offs >= {0, 8, 16}: rk = 'vec3'
    elif ret: rk = 'float' if kinds == {'float'} else 'int'
    return args, rk


def db_ret(t):
    if t == 'void': return None
    if t == 'float': return 'float'
    if t == 'Vector3': return 'vec3'
    return 'int'


if __name__ == '__main__':
    db = json.load(open('research/nativedb/natives.json'))
    uni = {int(h, 16): v for d in db.values() for h, v in d.items()}
    rows = {int(h, 16): int(x, 16) for h, x in (l.split('\t') for l in open('research/natives-1158.16.tsv').read().split('\n') if l)}
    ok = rok = tot = 0; bad = []
    for h, v in uni.items():
        if h not in rows: continue
        a, rk = trace(rows[h])
        want = len(v['params']); got = max(a) + 1 if a else 0
        tot += 1; ok += got == want; rok += rk == db_ret(v['return_type'])
        if got != want or rk != db_ret(v['return_type']): bad.append((v['name'], want, got, v['return_type'], rk))
    print(f'param count exact: {ok}/{tot}   return kind exact: {rok}/{tot}')
    for b in bad[:20]: print(b)
