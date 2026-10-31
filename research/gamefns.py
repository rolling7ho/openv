# Find the game internals behind ScriptHookV's non-native exports, for OpenV's game-<build>.txt:
#   globals         scrProgram globals block array (getGlobalPtr)
#   handle_to_ptr   script handle -> entity pointer (getScriptHandleBaseAddress)
#   ptr_to_handle   entity pointer -> script handle (worldGetAll*)
#   vehicle_pool    pointer to the vehicle pool pointer
#   ped/object/pickup_pool  encrypted pool pointers: A B flag k1 k2, decoded as
#                   x = rol(A, k1); p = ~rol(rol(B ^ x, 32), (x & 31) + k2), valid while flag & 1
# Each comes from code the matching native handler runs, so a game update moves the addresses, not the method.
# usage: gamefns.py <dump> <crossmap-final.json> <out.txt>
import sys, os, re, json, struct, subprocess, collections
HERE = os.path.dirname(os.path.abspath(__file__))
os.environ['OPENV_DUMP'] = sys.argv[1]; sys.path.insert(0, HERE)
from image import dump, TEXT
import capstone

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
RIP = re.compile(r'\[rip ([+-]) (0x[0-9a-f]+)\]')
handlers = {v['name']: int(v['handler'], 16) for v in json.load(open(sys.argv[2]))['natives'].values()}
found, missing = {}, []


def rip_abs(ins):
    m = RIP.search(ins.op_str)
    return ins.address + ins.size + int(m.group(2), 16) * (1 if m.group(1) == '+' else -1) if m else None


def deob(native, n=1500, calls=True):
    """(address, mnemonic, operands) of the de-obfuscated code a native runs."""
    if native not in handlers: return []
    args = [sys.executable, '-I', os.path.join(HERE, 'deob.py'), sys.argv[1], hex(handlers[native]), str(n)]
    out = subprocess.run(args + (['--calls'] if calls else []), capture_output=True, text=True).stdout
    rows = []
    for l in out.splitlines():
        m = re.match(r'\s+([0-9a-f]{8})  (\S+) ?(.*?)(?:    ; \[(0x[0-9a-f]+)\])?$', l)
        if m: rows.append((int(m.group(1), 16), m.group(2), m.group(3), int(m.group(4), 16) if m.group(4) else None))
    return rows


def calls(rows):
    return [int(o, 16) - 0x140000000 for _, mn, o, _ in rows if mn == 'call' and o.startswith('0x')]


# --- encrypted pool pointers: every `and cl, 0x1f; add cl, k2` decode site in the game ---
pools = {}
for s, e in TEXT:
    for m in re.finditer(rb'\x80\xe1\x1f\x80\xc1(.)', dump[s:e], re.S):
        p, k2 = s + m.start(), m.group(1)[0]
        for back in range(64, 40, -1):
            ins = list(md.disasm(dump[p - back:p], p - back))
            if not ins or ins[-1].address + ins[-1].size != p: continue
            loads, flag, A, k1 = {}, None, None, None
            for i in ins:
                a = rip_abs(i)
                if a is not None and i.mnemonic == 'mov' and 'qword' in i.op_str: loads[i.op_str.split(',')[0]] = a
                elif a is not None and i.mnemonic == 'movzx' and 'byte' in i.op_str: flag = a
                elif i.mnemonic == 'rol' and i.op_str.split(', ')[0] in loads and i.op_str.split(', ')[1] not in ('0x20', 'cl'):
                    A, k1 = loads[i.op_str.split(', ')[0]], int(i.op_str.split(', ')[1], 16)
            others = [v for v in loads.values() if v != A]
            if A and others and flag:
                pools.setdefault(A, collections.Counter())[(others[-1], flag, k1, k2)] += 1
            break
pool_of = {A: c.most_common(1)[0][0] for A, c in pools.items()}


def pool_refs(native):
    return {a for *_, a in deob(native) if a in pool_of}


def put_pool(name, refs):
    if len(refs) == 1:
        A = refs.pop(); B, flag, k1, k2 = pool_of[A]
        found[name] = f'{A:x} {B:x} {flag:x} {k1} {k2}'
    else:
        missing.append(name)


shared = pool_refs('CLEAR_AREA_OF_VEHICLES')
put_pool('ped_pool', pool_refs('CLEAR_AREA_OF_PEDS') - shared)
put_pool('object_pool', pool_refs('CLEAR_AREA_OF_OBJECTS') - shared)
put_pool('pickup_pool', pool_refs('DOES_PICKUP_OF_TYPE_EXIST_IN_AREA'))

# --- handle -> pointer: the first call DOES_ENTITY_EXIST makes with the handle ---
c = calls(deob('DOES_ENTITY_EXIST', 40, calls=False))
if c: found['handle_to_ptr'] = f'{c[0]:x}'
else: missing.append('handle_to_ptr')

# --- vehicle pool and pointer -> handle: GET_ALL_VEHICLES walks the pool and converts each vehicle ---
worker = calls(deob('GET_ALL_VEHICLES', 40, calls=False))
rows = []
if worker:
    out = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'deob.py'), sys.argv[1], hex(worker[0]), '200'],
                         capture_output=True, text=True).stdout
    rows = [l.split(None, 2) for l in out.splitlines() if re.match(r'\s+[0-9a-f]{8}  ', l)]
vp = [r for r in rows if r[1] == 'mov' and 'qword ptr [rip' in r[2]]
if vp: found['vehicle_pool'] = vp[0][2].split('[')[-1].rstrip(']').replace('0x', '')
else: missing.append('vehicle_pool')
for i, r in enumerate(rows[:-1]):      # `call conv` followed by storing eax into the output array
    if r[1] == 'call' and rows[i + 1][1] == 'mov' and rows[i + 1][2].startswith('dword ptr [') and rows[i + 1][2].endswith('eax'):
        found['ptr_to_handle'] = f'{int(r[2], 16) - 0x140000000:x}'; break
else: missing.append('ptr_to_handle')

# --- globals: scrThread::Run passes the block array to the interpreter: lea rdx,[globals]; mov r8,rbx; mov r9,r14; call ---
g = [s + m.start() + 7 + struct.unpack('<i', m.group(1))[0]
     for s, e in TEXT for m in re.finditer(rb'\x48\x8d\x15(.{4})(?:\x49\x89\xd8|\x4c\x8b\xc3)(?:\x4d\x89\xf1|\x4d\x8b\xce)\xe8', dump[s:e], re.S)]
if len(set(g)) == 1: found['globals'] = f'{g[0]:x}'
else: missing.append('globals')

with open(sys.argv[3], 'w') as f:
    f.write('# OpenV game internals: name rva [pool: B flag k1 k2]\n')
    for k, v in found.items(): f.write(f'{k} {v}\n')
for k, v in found.items(): print(f'  {k:14} {v}')
if missing: print('  not found:', ', '.join(missing))
sys.exit(0 if not missing else 3)
