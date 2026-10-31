# Merge: registration-order solution is primary; address-order solution fills natives it left unmapped.
import json, sys, collections
reg = json.load(open(sys.argv[1]))['natives']; addr = json.load(open(sys.argv[2]))['natives']
out = dict(reg); added = 0; agree = disagree = 0
taken = {v['hash'] for v in reg.values()}
for u, v in addr.items():
    if u in reg:
        if reg[u]['handler'] == v['handler']: agree += 1
        else: disagree += 1
    elif v['hash'] not in taken:
        out[u] = dict(v, conf=v['conf'] if v['conf'] == 'anchor' else 'low'); added += 1
print(f'both passes mapped: agree {agree}, disagree {disagree}; filled from address pass: {added}; total {len(out)}')
print(collections.Counter(v['conf'] for v in out.values()))
json.dump({'build': sys.argv[4] if len(sys.argv) > 4 else '1158.16', 'natives': out}, open(sys.argv[3], 'w'), indent=0)
