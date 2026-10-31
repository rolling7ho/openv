# Candidate RVAs for the native registration table, best first, one per line. The caller checks each
# against the live game (tools/dump/openv-natives.exe) and keeps the first that walks to >6000 natives.
#   1. OpenV's pattern (src/openv.c): lea rsi,[table]; lea r8,[...]; movabs rdx,0x4EDE34FBADD967A6
#   2. any rip-relative lea/mov near that universal hash that points into a writable section
#   3. rip-relative operands in the first instructions of the registration function
# usage: findtable.py <dump> [REG hex]
import sys, struct, re, os
os.environ['OPENV_DUMP'] = sys.argv[1]; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image import dump, TEXT, data_sections
import capstone

DATA = data_sections(dump)
writable = lambda a: any(s <= a < e for s, e in DATA)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
out = []


def add(a):
    if writable(a) and a not in out: out.append(a)


def rip_targets(code_off, length):
    for ins in md.disasm(dump[code_off:code_off + length], code_off):
        m = RIP.search(ins.op_str)
        if m: yield ins, ins.address + ins.size + int(m.group(2), 16) * (1 if m.group(1) == '+' else -1)


MOVABS = b'\x48\xba' + struct.pack('<Q', 0x4EDE34FBADD967A6)
for s, e in TEXT:
    for m in re.finditer(rb'\x48\x8d\x35(.{4})\x4c\x8d\x05.{4}' + re.escape(MOVABS), dump[s:e], re.S):
        p = s + m.start()
        add(p + 7 + struct.unpack('<i', m.group(1))[0])
for s, e in TEXT:
    for m in re.finditer(re.escape(MOVABS), dump[s:e]):
        p = s + m.start()
        for back in range(1, 48):            # decode from each start offset; keep leas that end before the movabs
            for ins, t in rip_targets(p - back, back):
                if ins.mnemonic in ('lea', 'mov'): add(t)
if len(sys.argv) > 2:
    reg = int(sys.argv[2], 16)
    for _ in range(8):                        # follow a few jmps from the function start
        ins = next(md.disasm(dump[reg:reg + 16], reg), None)
        if ins and ins.mnemonic == 'jmp' and ins.op_str.startswith('0x'): reg = int(ins.op_str, 16)
        else: break
    for ins, t in rip_targets(reg, 400): add(t)
for a in out: print(f'{a:x}')
