# Build a native map (data/natives-<build>.txt format) from a game image dump and a live-table TSV.
# Runs the whole crossmap pipeline: features -> registration order -> two solves -> merge -> text map.
# usage: rebuild.py <dump.bin> <natives.tsv> <build e.g. 1158.16> <out.txt> [workdir]
# Exit status: 0 good map, 2 map too weak to trust, 1 a step failed.
import sys, os, json, subprocess, collections, time

dump, tsv, build, out = (os.path.abspath(a) if i != 2 else a for i, a in enumerate(sys.argv[1:5]))
work = sys.argv[5] if len(sys.argv) > 5 else os.path.dirname(os.path.abspath(out))
os.makedirs(work, exist_ok=True)
py = sys.executable
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # scripts read research/nativedb from here
env = dict(os.environ, OPENV_DUMP=dump)
w = lambda name: os.path.join(work, name)


def step(msg, *args):
    print(f'  {msg}...', flush=True)
    t = time.time()
    r = subprocess.run([py, '-I', *args], env=env, capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        sys.stderr.write(r.stdout[-2000:] + r.stderr[-4000:])
        sys.exit(1)
    return r.stdout, time.time() - t


step('Reading every native function (the slow part, a few minutes)', 'research/features.py', tsv, w('features.json'))
o, _ = step('Finding the order the game registers natives in', 'research/regorder.py', dump, 'auto', w('regorder.json'))
step('Matching natives by position', 'research/solve.py', tsv, w('features.json'), w('crossmap-addr.json'))
step('Matching natives by registration order', 'research/solve.py', tsv, w('features.json'), w('crossmap-reg.json'),
     '--order', w('regorder.json'))
step('Combining the two', 'research/merge.py', w('crossmap-reg.json'), w('crossmap-addr.json'), w('crossmap-final.json'), build)

nat = json.load(open(w('crossmap-final.json')))['natives']
conf = collections.Counter(v['conf'] for v in nat.values())
with open(out, 'w') as f:
    f.write(f'# OpenV native crossmap for GTA V Enhanced {build}: universal_hash handler_rva confidence\n')
    for u in sorted(nat, key=lambda k: int(k, 16)):
        f.write(f"{int(u, 16):016X} {int(nat[u]['handler'], 16):x} {nat[u]['conf']}\n")

live = sum(1 for l in open(tsv) if l.strip())
trusted = conf['anchor'] + conf['solid'] + conf['high']
print(f'  mapped {len(nat)} of {live} natives ({trusted} with high confidence): {dict(conf)}')
# 1158.16 for reference: 6674 mapped, 5713 high-confidence. Far below that means the game changed in a way
# the matcher does not understand, and a wrong map is worse than none (mods would call the wrong functions).
sys.exit(0 if len(nat) >= 6000 and trusted >= 5000 else 2)
