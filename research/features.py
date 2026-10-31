# Extract (argc, return kind) for every native handler in the live-table TSV; both tracer modes combined.
import sys, json, os
sys.argv = sys.argv[:1] + sys.argv[1:]; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace as t
rows = [(int(h, 16), int(x, 16)) for h, x in (l.split('\t') for l in open(sys.argv[1]).read().split('\n') if l)]
out = {}
for i, a in enumerate(sorted({x for _, x in rows})):
    t.DESCEND = False; a1, r1 = t.trace(a)
    t.DESCEND = True;  a2, r2 = t.trace(a)
    args = a1 | a2
    out[hex(a)] = {'argc': max(args) + 1 if args else 0, 'ret': r1 if r1 is not None else r2}
    if i % 500 == 0: print(i, flush=True)
json.dump(out, open(sys.argv[2], 'w'))
print('done', len(out))
