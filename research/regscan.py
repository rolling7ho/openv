# Find all native registrations: calls to the register fn, recover (hash, handler) from preceding movabs rdx / lea r8.
import sys, struct, json, re, capstone
dump = open(sys.argv[1], 'rb').read(); base = 0x140000000
REG = int(sys.argv[2], 16)
text = (0x1000, 0x1000 + 0x2479400)
calls = []
for m in re.finditer(rb'\xe8', dump[text[0]:text[1]]):
    off = text[0] + m.start()
    if off + 5 + struct.unpack_from('<i', dump, off + 1)[0] == REG: calls.append(off)
print('call sites', len(calls))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = False
regs, bad = [], 0
for c in calls:
    h = hdl = None
    for ins in md.disasm(dump[c-40:c], base + c - 40):
        if ins.mnemonic == 'movabs' and ins.op_str.startswith('rdx,'): h = int(ins.op_str.split(',')[1], 16)
        if ins.mnemonic == 'lea' and ins.op_str.startswith('r8, [rip'):
            hdl = ins.address + ins.size + int(ins.op_str.split('rip')[1].strip(' +]').replace('- ', '-').replace(' ', ''), 16) - base
    if h is None or hdl is None: bad += 1
    else: regs.append((c, h, hdl))
print('recovered', len(regs), 'unparsed', bad)
fns = {}
db = json.load(open('research/nativedb/natives.json'))
uni = {int(h, 16): (nsn, v['name']) for nsn, ns in db.items() for h, v in ns.items()}
known = sum(1 for _, h, _ in regs if h in uni)
print('hashes matching universal', known, 'unique hashes', len({h for _, h, _ in regs}), 'unique handlers', len({x for *_, x in regs}))
json.dump([{'site': hex(c), 'hash': f'0x{h:016X}', 'handler': hex(x), 'universal': uni.get(h, [None, None])[1]} for c, h, x in regs], open(sys.argv[3], 'w'), indent=0)
