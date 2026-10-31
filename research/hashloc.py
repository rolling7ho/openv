import json, struct, sys, collections
dump = open(sys.argv[1], 'rb').read()
db = json.load(open('research/nativedb/natives.json'))
locs = []
for ns, d in db.items():
    for h, v in d.items():
        i = dump.find(struct.pack('<Q', int(h, 16)))
        if i >= 0: locs.append((i, ns, v['name']))
locs.sort()
by = collections.Counter((i >> 16) for i, *_ in locs)
for k, c in sorted(by.items()): print(hex(k << 16), c)
# gaps between consecutive hits
prev = None
for i, ns, n in locs[:5] + locs[-5:]: print(hex(i), ns, n)
print(collections.Counter(ns for _, ns, _ in locs).most_common(12))
