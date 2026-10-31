# Recover native registration order: the registration spine is a run of `call REG` (one per native) in
# address order; each is entered from a scattered chunk `lea rdx,[handler]; movabs rcx,hash; jmp spine_i`.
# usage: regorder.py <dump> <REG hex | auto> <out.json>
# With `auto`, REG is the call target most of those chunks jump to (1158.16: 0x920d50).
import sys, struct, re, json, os, collections
os.environ['OPENV_DUMP'] = sys.argv[1]; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from image import dump, TEXT

CHUNK = re.compile(rb'\x48\x8d\x15(.{4})\x48\xb9(.{8})\xe9(.{4})', re.S)
chunks = []                      # (jmp site, target, hash, handler)
for s, e in TEXT:
    for m in CHUNK.finditer(dump, s, e):
        p = m.start() + 17       # the jmp
        t = p + 5 + struct.unpack('<i', m.group(3))[0]
        h = struct.unpack('<Q', m.group(2))[0]
        handler = m.start() + 7 + struct.unpack('<i', m.group(1))[0]
        chunks.append((p, t, h, handler))

if sys.argv[2] == 'auto':
    votes = collections.Counter(t + 5 + struct.unpack_from('<i', dump, t + 1)[0]
                                for _, t, _, _ in chunks if 0 <= t < len(dump) - 5 and dump[t] == 0xe8)
    if not votes: sys.exit('registration function not found')
    REG, n = votes.most_common(1)[0]
    print(f'registration function {REG:#x} ({n} of {len(chunks)} chunks)')
else:
    REG = int(sys.argv[2], 16)

calls = set()
for s, e in TEXT:
    for m in re.finditer(rb'\xe8', dump[s:e]):
        o = s + m.start()
        if o + 5 + struct.unpack_from('<i', dump, o + 1)[0] == REG: calls.add(o)
print('spine calls', len(calls))
order = {t: (h, handler) for _, t, h, handler in chunks if t in calls}
print('sites resolved', len(order))
seq = [(f'0x{h:016X}', hex(x)) for _, (h, x) in sorted(order.items())]
json.dump(seq, open(sys.argv[3], 'w'))
