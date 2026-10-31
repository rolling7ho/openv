# print the jmp-followed linear instruction stream of a native handler by name
import sys, json; sys.path.insert(0,'research')
name = sys.argv[1]; n = int(sys.argv[2]) if len(sys.argv) > 2 else 50
sys.argv = ['x']; import trace as t
db = json.load(open('research/nativedb/natives.json'))
rows = {int(h,16): int(x,16) for h, x in (l.split('\t') for l in open('research/natives-1158.16.tsv').read().split('\n') if l)}
h = [int(k,16) for d in db.values() for k,v in d.items() if v['name']==name][0]
pc = rows[h]
for _ in range(n):
    ins = next(t.md.disasm(t.dump[pc:pc+16], t.base+pc)); print(f'{pc:08x} {ins.mnemonic} {ins.op_str}')
    if ins.mnemonic=='jmp' and ins.op_str.startswith('0x'): pc=int(ins.op_str,16)-t.base; continue
    if ins.mnemonic=='ret': break
    pc += ins.size
