#!/bin/sh
# Regression test: C++ map loader vs the Python reference loader, for every map.
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
open(sys.argv[4], 'wb').write(bytes(b for row in L['tiles'] for b in row))
EOF
  cmp -s "$TMP/c.bin" "$TMP/p.bin" || { echo "map $h MISMATCH"; fail=1; }
done
rm -rf "$TMP"
[ $fail = 0 ] && echo "all 192 maps match" || exit 1
