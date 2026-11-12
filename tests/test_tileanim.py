"""Field tile animation (01:54A0) vs the real game's VRAM on map $10 (dumps p100-p103).
usage: test_tileanim.py build/ffl3 rom.gb dumpdir"""
import os, subprocess, sys, tempfile
exe, rom, dumps = sys.argv[1:4]
tmp = tempfile.mkdtemp(); fails = 0
mems = [open(os.path.join(dumps, 'p%03d.mem' % i), 'rb').read() for i in range(100, 104)]
seen = set()
for n in (0, 16):
    out = os.path.join(tmp, 'v%d.bin' % n)
    subprocess.run([exe, rom, '--map', '10', '--pos', '22,12', '--walk', 'U'], capture_output=True,
                   env=dict(os.environ, FFL3_HOOKFRAMES=str(n), FFL3_VRAMDUMP=out), timeout=30)
    v = open(out, 'rb').read()
    for k, m in enumerate(mems):
        if v[0x800:0x1800] == m[0x8800:0x9800]: seen.add(k)
ok = seen == {0, 1, 2, 3}
print(('PASS' if ok else 'FAIL'), 'map 10 animated BG tiles = real VRAM in both phases', sorted(seen))
sys.exit(0 if ok else 1)
