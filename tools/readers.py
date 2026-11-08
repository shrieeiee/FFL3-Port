import numpy as np, bisect, collections
romsz=0x40000
sym=[]
for l in open('ext/FFL3-Disassembly/rom.sym'):
    if l.startswith(';') or ':' not in l[:3]: continue
    a,n=l.split()[:2]; b,ad=a.split(':'); b=int(b,16); ad=int(ad,16)
    if ad>=0x8000: continue
    sym.append((ad if ad<0x4000 else b*0x4000+ad-0x4000, n))
sym.sort(); offs=[o for o,_ in sym]
def owner(o):
    i=bisect.bisect_right(offs,o)-1
    while i>=0 and '.' in sym[i][1]: i-=1
    return sym[i][1]
rb=np.fromfile('run/reader_banks.bin',np.uint32).reshape(romsz,16)
rd=np.fromfile('run/readers.bin',np.uint32)
cov=np.zeros(romsz,bool)
import glob
for f in glob.glob('run/cov_*.bin'): cov |= np.fromfile(f,np.uint32)[:romsz]>0
# per data bank: which functions read it (by count)
for bank in range(16):
    col=rb[:,bank]; nz=np.nonzero(col)[0]
    fc=collections.Counter()
    for o in nz: fc[owner(int(o))]+=int(col[o])
    touched=int((rd[bank*0x4000:(bank+1)*0x4000]>0).sum())
    print(f"bank {bank:02x}: {touched:5d} bytes read as data; readers:", ', '.join(f"{n}({c})" for n,c in fc.most_common(6)))
