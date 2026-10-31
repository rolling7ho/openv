import json, struct, sys, pefile
dump = open(sys.argv[1], 'rb').read()
db = json.load(open('research/nativedb/natives.json'))
pe = pefile.PE(data=dump[:0x1000], fast_load=True)
for s in pe.sections: print(s.Name.rstrip(b'\0').decode(errors='replace'), hex(s.VirtualAddress), hex(s.Misc_VirtualSize), hex(s.Characteristics))
found = {}
for ns in db.values():
    for h, v in ns.items():
        b = struct.pack('<Q', int(h, 16))
        i = dump.find(b)
        if i >= 0: found[v['name']] = (h, hex(i), dump.count(b))
print('universal hashes present:', len(found), 'of', sum(len(v) for v in db.values()))
for k in list(found)[:15]: print(' ', k, found[k])
