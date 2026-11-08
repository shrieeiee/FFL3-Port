import sys, glob, struct, collections, bisect, os
import numpy as np
romsz = 0x40000
sym = []
for l in open('ext/FFL3-Disassembly/rom.sym'):
    if l.startswith(';') or ':' not in l[:3]: continue
    a, n = l.split()[:2]; b, ad = a.split(':'); b=int(b,16); ad=int(ad,16)
    if ad >= 0x8000: continue
    off = ad if ad < 0x4000 else b*0x4000 + ad-0x4000
    sym.append((off, n))
sym.sort(); offs=[o for o,_ in sym]
def owner(o):
    i = bisect.bisect_right(offs, o)-1
    # walk back past local labels
    while i>=0 and '.' in sym[i][1]: i-=1
    return sym[i][1] if i>=0 else '?'
phases = [os.path.basename(p)[4:-4] for p in sorted(glob.glob('run/cov_*.bin'), key=os.path.getmtime)]
total = np.zeros(romsz, np.uint64); ram=np.zeros(0x10000,np.uint64)
per = {}
for ph in phases:
    a = np.fromfile(f'run/cov_{ph}.bin', np.uint32)
    r, m = a[:romsz].astype(np.uint64), a[romsz:].astype(np.uint64)
    total += r; ram += m
    fn = collections.Counter()
    for o in np.nonzero(r)[0]: fn[owner(int(o))] += int(r[o])
    per[ph] = fn
allfn = set().union(*per.values())
code_funcs = {n for _,n in sym if n.startswith(('call_','jp_','jr_')) or n[0].islower() and not n.startswith(('data_','w','h'))}
print("phases:", phases)
print("distinct instr starts executed:", int((total>0).sum()))
print("functions/labels touched:", len(allfn))
for b in range(16):
    print(f"  bank {b:02x}: {int((total[b*0x4000:(b+1)*0x4000]>0).sum()):5d} instr")
print("RAM-executed ranges:", [hex(x) for x in np.nonzero(ram)[0][:40]])
print()
for ph in phases:
    uniq = set(per[ph]) - set().union(*(set(per[p]) for p in phases if p!=ph))
    print(f"{ph:14s} funcs={len(per[ph]):4d} unique={len(uniq):3d} top:", ', '.join(f"{n}({c})" for n,c in per[ph].most_common(6)))
import json; json.dump({p:dict(per[p]) for p in phases}, open('run/funcs.json','w'))
