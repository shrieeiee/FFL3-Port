#!/bin/sh
# Field behaviour checks. Expected values come from the real game (Peanut-GB coverage
# emulator driving the ROM), see docs/RE_NOTES.md.
# usage: tests/test_field.sh path/to/saga3.gb [build/ffl3]
ROM="$1"; BIN="${2:-build/ffl3}"; fail=0
check() {  # name, expected substring, args...
  name="$1"; want="$2"; shift 2
  out=$("$BIN" "$ROM" "$@")
  case "$out" in *"$want"*) echo "ok   $name";; *) echo "FAIL $name: got [$out] want [$want]"; fail=1;; esac
}
check "map 10 door -> map 11 at 6,12 (game)"   "map 11 layout 05 gfx 05 sprites 1B  16x8  player 6,12"  --map 10 --pos 22,12 --walk D
check "map 10 \$03 block is solid (game)"       "player 12,12"   --map 10 --pos 12,12 --walk L
check "map 10 \$02 block is walkable (game)"    "player 13,14"   --map 10 --pos 12,14 --walk D
check "map 10 \$FF block fires event \$3F"      "event 3F"       --map 10 --pos 7,16 --walk D
check "town door in and back out"               "map 02 layout 01 gfx 00 sprites 1C  32x32  player 30,12" --map 02 --pos 30,12 --walk UU
check "NPC text parses: magic book line"        "North Tower."   --text 0c
check "talk to NPC opens window (map 12)"       "npcs 4"         --map 12 --pos 47,23 --walk UA
# Every NPC text must parse exactly up to the next one (validates control-code lengths).
python3 -I "$(dirname "$0")/../tools/ffl3text.py" "$ROM" | grep -q "239 NPC texts: 239 end exactly" \
  && echo "ok   all 239 NPC texts parse exactly" || { echo "FAIL NPC text parse"; fail=1; }
exit $fail
