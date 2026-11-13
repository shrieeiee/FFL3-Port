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
check "map 10 \$FF block boards the Talon (game)" "map 0B layout 0F gfx 21 sprites 01  8x16  player 26,7" --map 10 --pos 7,16 --walk D
check "town door in and back out"               "map 02 layout 01 gfx 00 sprites 1C  32x32  player 30,12" --map 02 --pos 30,12 --walk UU
check "NPC text parses: magic book line"        "North Tower."   --text 0c
check "talk to NPC opens window (map 12)"       "npcs 4"         --map 12 --pos 47,23 --walk UA
check "map 10 object slots + proxies (game RAM)" "slots: 0:01 2:E0(hidden) 3:E0(hidden)(proxy) 4:E1(hidden) 5:E1(hidden)(proxy) 6:B1(hidden) 7:B1(hidden)(proxy) 8:21(hidden) 9:21(hidden)(proxy) 10:08" --map 10 --pos 5,16 --slots --dump-blocks /dev/null
check "Elder cutscene: leader faces down (game)" "leader 5,16 facing 2"                 --map 10 --pos 5,16 --run-text 02
check "Elder cutscene: actor 2 walks to 21,20 (game)" "slot 2 id 00 pos 21,20 facing 2" --map 10 --pos 5,16 --run-text 02
check "Elder cutscene: proxy below actor (game)" "slot 3 id 00 pos 16,20"               --map 10 --pos 5,16 --run-text 02
check "new game starts aboard the Talon"         "map 0B layout 0F gfx 21 sprites 01  8x16  player 26,7" --dump-blocks /dev/null
check "new game: step off the Talon -> map 10 at 7,16 (game)" "map 10 layout 04 gfx 04 sprites 04  16x16  player 7,16" --walk DW
check "Talon round trip: out, back on board (game; fades take 60 frames)" "map 0B layout 0F gfx 21 sprites 01  8x16  player 26,7" --walk DWWD
# Every NPC text must parse exactly up to the next one (validates control-code lengths).
check "shop: Buy, pick, Yes (01:7187)" "shop open, gold 560" --shop 32 --walk AAA
check "inn: Stay costs the missing HP + MP (01:7731)" "shop open, gold 683" --shop 40 --party-hp 5 --walk A
check "field menu: Item, mark + use Cure on member 2 (09:521A)" "party: 5/50,5/5,s00 40/40,45/45,s00" --party-hp 5 --ram c6ff:c0,c700:02 --walk MAAADA
check "Talon cockpit: Depart needs flag 5 (01:79B3)" "map 10 layout 04 gfx 04 sprites 04  16x16  player 5,16" --map 10 --pos 5,16 --walk UAA
check "Talon: takes off from wC6E9 (01:63E6)" "map 01 layout 1C gfx 17 sprites 19  32x32  player 21,21  npcs 1  [took off]" --flag 5 --map 10 --pos 5,16 --walk UAA
check "Talon: flies over mountains (mode 6)" "player 25,25" --flag 5 --flag 3a --map 10 --pos 5,16 --walk UAARRRRDDDD
check "Talon: B lands, back in the cockpit (01:4250)" "player 5,16  npcs 1  [landed]" --flag 5 --flag 3a --map 10 --pos 5,16 --walk UAARRRRDDDDB
check "opening: prologue ends where the game hands over" "end: map 0B at 26,7" --opening
check "opening: flags at hand-over = the real game's wC760" "flags: 01 02 80 78 03 c8 c0 00 40 00 0c 91 1a 00 00 01 81 00 02 a2 a0 02 0f 40 15 20 02 10 00 00 03 ff" --opening
check "opening: gold and bag at hand-over" "gold 960
bag: E6 x1" --opening
python3 -I "$(dirname "$0")/../tools/ffl3text.py" "$ROM" | grep -q "239 NPC texts: 239 end exactly" \
  && echo "ok   all 239 NPC texts parse exactly" || { echo "FAIL NPC text parse"; fail=1; }
exit $fail
