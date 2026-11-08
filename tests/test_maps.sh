#!/bin/sh
# Regression test: C++ map loader vs Python reference loader, for every map.
# usage: tests/test_maps.sh path/to/saga3.gb [build/ffl3]
set -e
ROM="$1"; BIN="${2:-build/ffl3}"; TMP=$(mktemp -d)
fail=0
for i in $(seq 0 191); do
  h=$(printf %02x $i)
  "$BIN" "$ROM" --map $h --dump-tiles "$TMP/c.bin" >/dev/null
  python3 -I -c "
import sys; sys.path.insert(0,'$(dirname "$0")/../tools')
from ffl3data import Rom
r=Rom('$ROM'); h=r.map_header(0x$h); L=r.layout(h['layout'])
open('$TMP/p.bin','wb').write(bytes(t for row in L['tiles'] for t in row))"
  cmp -s "$TMP/c.bin" "$TMP/p.bin" || { echo "map $h MISMATCH"; fail=1; }
done
rm -rf "$TMP"
[ $fail = 0 ] && echo "all 192 maps match" || exit 1
