import json, struct, collections, sys
dump = open('research/dumps/1158.16-a.bin','rb').read()
db = json.load(open('research/nativedb/natives.json'))
dborder, uni = [], {}
for ns, d in db.items():
    for h, v in d.items():
        uni[int(h,16)] = (ns, v['name']); dborder.append(int(h,16))
dbidx = {h:i for i,h in enumerate(dborder)}
rows = [l.split('\t') for l in open('research/natives-1158.16.tsv').read().split('\n') if l]
tab = [(int(h,16), int(x,16)) for h,x in rows]
# registration-site order: where each current hash appears as an immediate in the image
pos, nf = {}, 0
for h,_ in tab:
    i = dump.find(struct.pack('<Q', h))
    if i < 0: nf += 1
    else: pos[h] = i
print('hash immediates found in image', len(pos), 'not found', nf)
def mono(order, name):
    anchors = [dbidx[h] for h in order if h in dbidx]
    inc = sum(1 for a,b in zip(anchors, anchors[1:]) if b > a)
    print(f'{name}: anchors {len(anchors)}, increasing steps {inc}/{len(anchors)-1}')
    return anchors
mono([h for h,_ in sorted(pos.items(), key=lambda t:t[1])], 'registration-site order')
mono([h for h,_ in sorted(tab, key=lambda t:t[1])], 'handler-address order')

# gap analysis in handler order
seq = [h for h,_ in sorted(tab, key=lambda t:(t[1], t[0]))]
anc = [(i, dbidx[h]) for i,h in enumerate(seq) if h in dbidx]
# keep the longest increasing chain of anchors (drop the few out-of-order ones)
import bisect
best = []; prev = {}; tails = []; tidx = []
for k,(i,d) in enumerate(anc):
    j = bisect.bisect_left(tails, d)
    if j == len(tails): tails.append(d); tidx.append(k)
    else: tails[j] = d; tidx[j] = k
    prev[k] = tidx[j-1] if j else None
k = tidx[-1]; chain = []
while k is not None: chain.append(anc[k]); k = prev[k]
chain.reverse()
eq = cov = 0; diffs = collections.Counter()
for (i0,d0),(i1,d1) in zip(chain, chain[1:]):
    gt, gd = i1-i0-1, d1-d0-1
    diffs[gt-gd] += 1
    if gt == gd: eq += 1; cov += gt
print('chain anchors', len(chain), 'gaps', len(chain)-1, 'equal-size gaps', eq, 'natives covered by equal gaps', cov)
print('gap size difference (table - db) histogram', sorted(diffs.items())[:25])
print('before first anchor: table', chain[0][0], 'db', chain[0][1], '| after last: table', len(seq)-chain[-1][0]-1, 'db', len(dborder)-chain[-1][1]-1)
