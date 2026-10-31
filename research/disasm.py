import sys, capstone
dump = open(sys.argv[1], 'rb').read(); base = 0x140000000
start = int(sys.argv[2], 16); n = int(sys.argv[3])
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for i, ins in enumerate(md.disasm(dump[start:start+n*16], base+start)):
    if i >= n: break
    print(f'{ins.address-base:08x}: {ins.bytes.hex():<24} {ins.mnemonic} {ins.op_str}')
