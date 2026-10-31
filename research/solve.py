# Crossmap solver: map universal native hashes (nativedb) to this build's handlers.
#
# Signals:
#   1. anchors   - table hashes that equal a universal hash (fixed points)
#   2. order     - handlers are laid out in source order, which matches nativedb order within a namespace
#   3. features  - per-handler argc / return kind from research/trace.py, vs nativedb signatures
# Method: game natives sorted by handler address; db natives arranged in the game's namespace order;
# anchor-constrained Needleman-Wunsch per segment. Namespaces with no anchors are placed greedily into
# the slot that maximizes the alignment score.
#
# usage: solve.py <natives.tsv> <features.json> <out.json> [--holdout FRACTION] [--order regorder.json]
import json, sys, random, bisect, collections

tsv, featf, outf = sys.argv[1:4]
HOLD = float(sys.argv[sys.argv.index('--holdout') + 1]) if '--holdout' in sys.argv else 0.0

db = json.load(open('research/nativedb/natives.json'))


def db_ret(t):
    return None if t == 'void' else 'float' if t == 'float' else 'vec3' if t == 'Vector3' else 'int'


D = {}  # universal hash -> info
NS = {}  # namespace -> [universal hashes in db order]
for ns, d in db.items():
    NS[ns] = []
    for h, v in d.items():
        u = int(h, 16)
        D[u] = {'ns': ns, 'name': v['name'], 'argc': len(v['params']), 'ret': db_ret(v['return_type'])}
        NS[ns].append(u)

feat = {int(k, 16): v for k, v in json.load(open(featf)).items()}
game = sorted(((int(h, 16), int(x, 16)) for h, x in (l.split('\t') for l in open(tsv).read().split('\n') if l)),
              key=lambda t: (t[1], t[0]))
handler = dict(game)
if '--order' in sys.argv:
    # registration order (research/regorder.py)
    reg = [int(h, 16) for h, _ in json.load(open(sys.argv[sys.argv.index('--order') + 1]))]
    inreg = set(reg)
    G = reg  # natives registered outside the spine (BUILTIN etc.) are left to the address-order pass
else:
    G = [h for h, _ in game]             # game sequence of current hashes, in handler-address order

anchors_all = [h for h in G if h in D]
random.seed(1)
hidden = set(random.sample(anchors_all, int(len(anchors_all) * HOLD))) if HOLD else set()
anchors = {h for h in anchors_all if h not in hidden}

# --- namespace order observed in the game, from anchors (longest increasing run per namespace) ---
pos_of = {h: i for i, h in enumerate(G)}
ns_pos = collections.defaultdict(list)
for h in anchors: ns_pos[D[h]['ns']].append(pos_of[h])
anchored_ns = sorted(ns_pos, key=lambda ns: sorted(ns_pos[ns])[len(ns_pos[ns]) // 2])
free_ns = [ns for ns in NS if ns not in ns_pos]


def score(u, g):
    """match score of universal native u against game native g (current hash)"""
    f = feat.get(handler[g]); d = D[u]
    if f is None: return 0.0
    s = 0.0
    if f['argc'] == d['argc']: s += 3
    elif f['argc'] < d['argc']: s += 0.5          # handler may ignore trailing params
    else: s -= 3
    if f['ret'] == d['ret']: s += 2
    elif d['ret'] is not None and f['ret'] is None: s -= 1
    else: s -= 2
    return s


GAP = -2.0
EXACT = {}  # universal hash -> aligned inside a segment with equal counts on both sides, all paired


def nw(us, gs):
    """global alignment of db natives us vs game natives gs. returns (score, pairs)"""
    n, m = len(us), len(gs)
    if n == 0 or m == 0: return GAP * (n + m), []
    prev = [GAP * j for j in range(m + 1)]
    back = [[0] * (m + 1) for _ in range(n + 1)]
    for j in range(1, m + 1): back[0][j] = 2
    for i in range(1, n + 1):
        cur = [GAP * i] + [0.0] * m; bi = back[i]; bi[0] = 1; ui = us[i - 1]
        for j in range(1, m + 1):
            a = prev[j - 1] + score(ui, gs[j - 1]); b = prev[j] + GAP; c = cur[j - 1] + GAP
            if a >= b and a >= c: cur[j] = a; bi[j] = 0
            elif b >= c: cur[j] = b; bi[j] = 1
            else: cur[j] = c; bi[j] = 2
        prev = cur
    pairs, i, j = [], n, m
    while i > 0 or j > 0:
        k = back[i][j]
        if k == 0: pairs.append((us[i - 1], gs[j - 1])); i -= 1; j -= 1
        elif k == 1: i -= 1
        else: j -= 1
    return prev[m], pairs[::-1]


def db_sequence(order):
    return [u for ns in order for u in NS[ns]]


def solve(order):
    """anchor-constrained alignment over the full sequence for a namespace order. returns (score, mapping)"""
    seq = db_sequence(order)
    didx = {u: i for i, u in enumerate(seq)}
    # longest chain of anchors increasing in both game position and db position
    cand = sorted((pos_of[h], didx[h], h) for h in anchors if h in didx)
    tails, tidx, prev = [], [], {}
    for k, (_, d, _) in enumerate(cand):
        j = bisect.bisect_left(tails, d)
        if j == len(tails): tails.append(d); tidx.append(k)
        else: tails[j] = d; tidx[j] = k
        prev[k] = tidx[j - 1] if j else None
    chain = []
    k = tidx[-1] if tidx else None
    while k is not None: chain.append(cand[k]); k = prev[k]
    chain.reverse()
    total, mapping = 0.0, {}
    bounds = [(-1, -1)] + [(g, d) for g, d, _ in chain] + [(len(G), len(seq))]
    for (g0, d0), (g1, d1) in zip(bounds, bounds[1:]):
        us = [u for u in seq[d0 + 1:d1] if u not in anchors]
        gs = [g for g in G[g0 + 1:g1] if g not in anchors]
        s, pairs = nw(us, gs)
        total += s
        exact = len(us) == len(gs) and len(pairs) == len(us)
        for u, g in pairs: mapping[u] = g; EXACT[u] = exact
    for _, _, h in chain: mapping[h] = h
    return total, mapping


# --- place free namespaces greedily ---
order = list(anchored_ns)
base_score, _ = solve(order)
for ns in sorted(free_ns, key=lambda n: -len(NS[n])):
    best = None
    for slot in range(len(order) + 1):
        trial = order[:slot] + [ns] + order[slot:]
        s, _ = solve(trial)
        if best is None or s > best[0]: best = (s, slot)
    order.insert(best[1], ns)
    print(f'placed {ns:<14} at slot {best[1]:>2} score {best[0]:.0f}', flush=True)

final_score, mapping = solve(order)
print('namespace order:', ' '.join(order))

# --- confidence: features must agree for a mapped pair ---
res, conf = {}, collections.Counter()
for u, g in mapping.items():
    if u in anchors: c = 'anchor'
    else:
        s = score(u, g)
        c = 'high' if s >= 5 else 'medium' if s >= 2.5 else 'low'
        if c == 'high' and EXACT.get(u): c = 'solid'
    conf[c] += 1
    res[f'0x{u:016X}'] = {'name': D[u]['name'], 'ns': D[u]['ns'], 'hash': f'0x{g:016X}', 'handler': hex(handler[g]), 'conf': c}
print('mapped', len(mapping), 'of', len(D), dict(conf))

if hidden:
    ok = sum(1 for h in hidden if mapping.get(h) == h)
    same_handler = sum(1 for h in hidden if h in mapping and handler[mapping[h]] == handler[h])
    print(f'holdout: {len(hidden)} hidden anchors, exact {ok}, same handler {same_handler}, unmapped {sum(1 for h in hidden if h not in mapping)}')
    by = collections.Counter((res[f"0x{h:016X}"]["conf"] if h in mapping else 'unmapped', mapping.get(h) == h) for h in hidden)
    print('holdout by confidence (conf, correct):', dict(by))

json.dump({'order': order, 'natives': res}, open(outf, 'w'), indent=0)
