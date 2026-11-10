#!/bin/sh
# Replays battles recorded in the real game through the C++ battle VM (tests/vmreplay.cpp).
# usage: tests/test_vm.sh saga3.gb build/vmreplay RECDIR   (RECDIR from tools/rec.py)
ROM=$1; EXE=$2; DIR=$3; fail=0; n=0
for t in "$DIR"/*.txt; do
  name=$(basename "$t" .txt); [ -f "$DIR/${name}_enter.mem" ] || continue
  n=$((n+1))
  out=$("$EXE" "$ROM" "$DIR/${name}_enter.mem" "$t" | tail -1)
  echo "$name: $out"
  case "$out" in PASS*) ;; *) fail=1;; esac
done
[ $n -gt 0 ] || echo "SKIP (no recordings)"
exit $fail
