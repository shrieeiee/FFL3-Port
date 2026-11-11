#!/bin/sh
# Regression test: C++ map loader vs the Python reference loader, for every map
# (plus the 0B:78F5 block patches for the start-of-game flags).
# usage: tests/test_maps.sh path/to/saga3.gb [build/ffl3]
set -e
ROM="$1"; BIN="${2:-build/ffl3}"; TMP=$(mktemp -d); TOOLS="$(dirname "$0")/../tools"
fail=0
for i in $(seq 0 191); do
  h=$(printf %02x $i)
  "$BIN" "$ROM" --map $h --dump-blocks "$TMP/c.bin" >/dev/null
  python3 -I - "$TOOLS" "$ROM" "$h" "$TMP/p.bin" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
from ffl3data import Rom
r = Rom(sys.argv[2]); h = r.map_header(int(sys.argv[3], 16)); L = r.layout(h['layout'])
blocks = [b for row in L['tiles'] for b in row]; w = len(L['tiles'][0]); m = int(sys.argv[3], 16)
F = bytes([0x01,0x02,0x80,0x78,0x03,0xc8,0xc0,0x00,0x40,0x00,0x0c,0x91,0x1a,0x00,0x00,0x01,
           0x81,0x00,0x02,0xa2,0xa0,0x02,0x0f,0x40,0x15,0x20,0x02,0x10,0x00,0x00,0x03,0xff])   # main.cpp kStartFlags
for e in range(0x2f):   # 0B:78F5 patches (flag $FF = always)
    f = r.b(0xb, 0x79b1 + e)
    if r.b(0xb, 0x7982 + e) != m or (f != 0xff and not (F[f >> 3] >> (7 - (f & 7))) & 1): continue
    p = r.w(0xb, 0x79e0 + e * 2); col, row, hdr = r.b(0xb, p), r.b(0xb, p + 1), r.b(0xb, p + 2); p += 3
    for y in range(hdr & 15):
        for x in range(hdr >> 4):
            if (row + y) * w + col + x < len(blocks): blocks[(row + y) * w + col + x] = r.b(0xb, p)
            p += 1
open(sys.argv[4], 'wb').write(bytes(blocks))
EOF
  cmp -s "$TMP/c.bin" "$TMP/p.bin" || { echo "map $h MISMATCH"; fail=1; }
done
rm -rf "$TMP"
[ $fail = 0 ] && echo "all 192 maps match" || exit 1
