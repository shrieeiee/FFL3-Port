#!/usr/bin/env python3
"""Battle checks against the real game's VRAM (emulator dumps of formation 00, see RE_NOTES).
Usage: test_battle.py <ffl3 exe> <rom> <dump dir>. Skips the VRAM checks if the dumps are absent."""
import os, subprocess, sys, tempfile
exe, rom, dumps = sys.argv[1:4]
fails = 0
def check(name, ok, info=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else '  ' + info))
    fails += not ok

def run(args, view=None):
    a = [exe, rom] + args + (['--dump-view', view] if view else [])
    return subprocess.run(a, capture_output=True, text=True).stdout

def tiles2bpp(mem, base, n):
    out = []
    for i in range(n):
        t = mem[base + i * 16: base + i * 16 + 16]
        out.append(bytes(((t[y*2] >> (7-x)) & 1) | (((t[y*2+1] >> (7-x)) & 1) << 1) for y in range(8) for x in range(8)))
    return out

tmp = tempfile.mkdtemp()
out = run(['--battle', '80'])
check('formation 00 layout = real game', 'slots: g0@4,0 g0@8,0 g1@12,0 g1@12,4' in out, out)
out = run(['--battle', 'da'])           # Dwelg (formation 5A, fixed)
check('Dwelg: one, 8x8 picture, HP 2500', 'B5 x1 (HP 2500, class 4 8x8)' in out, out)
out = run(['--battle', '80', '--keys', 'WW*'])
check('formation 00 auto-fight ends', 'result: won' in out or 'result: lost' in out, out[-300:])

for name, keys in (('g00_07', 'WW'), ('g00_09', 'WWAA')):
    p = os.path.join(dumps, name + '.mem')
    if not os.path.exists(p):
        print('SKIP %s (no dump)' % name); continue
    mem = open(p, 'rb').read()
    v = os.path.join(tmp, name + '.bin')
    run(['--battle', '80', '--keys', keys], v)
    d = open(v, 'rb').read()
    wy, win = d[0], d[1:201]
    real = bytes(mem[0x9c00 + r * 32 + c] for r in range(10) for c in range(20))
    diff = [i for i in range(200) if win[i] != real[i]]
    check('%s window WY' % name, wy == mem[0xff4a], '%d vs %d' % (wy, mem[0xff4a]))
    check('%s window tiles' % name, not diff, 'differs at ' + ', '.join('r%dc%d %02x/%02x' % (i // 20, i % 20, win[i], real[i]) for i in diff[:8]))
    if name == 'g00_07':
        o = 201; ok = True; vram = tiles2bpp(mem, 0x9000, 0x30); base = 0
        for g in range(2):
            n = d[o]; o += 1
            mine = [d[o + i * 64: o + i * 64 + 64] for i in range(n)]; o += n * 64
            ok &= mine == vram[base:base + n]; base += n
        check('monster picture tiles = VRAM $9000', ok)
# Bank 2 animations: the party row (OAM wC078 + OBJ tiles $6C-$7F) during the turn's messages,
# and effect sprites (weapon grid, impact, death mask) that appear somewhere in an auto fight.
p = os.path.join(dumps, 'g00_19.mem')
if os.path.exists(p):
    mem = open(p, 'rb').read()
    v = os.path.join(tmp, 'row.bin')
    run(['--battle', '80', '--keys', 'WWA' + 'AA' * 5 + 'W'], v)
    d = open(v, 'rb').read(); o = 201
    for g in range(3): o += 1 + d[o] * 64
    oam, tiles = d[o:o + 160], d[o + 160:o + 160 + 4096]
    check('party row OAM = real game', oam[0x78:0xa0] == mem[0xfe78:0xfea0], oam[0x78:0xa0].hex())
    check('party row OBJ tiles = real VRAM', tiles[0x6c0:0x800] == mem[0x86c0:0x8800])
    log = os.path.join(tmp, 'oam.bin')
    subprocess.run([exe, rom, '--battle', '80', '--keys', 'WW*'], capture_output=True, env=dict(os.environ, FFL3_OAMLOG=log))
    fr = open(log, 'rb').read()
    eff = lambda o: tuple(sorted(tuple(o[i:i + 4]) for i in range(0, 0x78, 4) if o[i]))
    seen = set(eff(fr[i:i + 160]) for i in range(0, len(fr), 160))
    for n, what in ((62, 'weapon grid'), (47, 'impact'), (31, 'death mask')):
        q = os.path.join(dumps, 'g00_%02d.mem' % n)
        if os.path.exists(q):
            r = eff(open(q, 'rb').read()[0xfe00:0xfea0])
            check('anim %s sprites (g00_%02d) = real game' % (what, n), r in seen, str(r[:3]))
sys.exit(1 if fails else 0)
