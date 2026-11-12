# FFL3 / SaGa 3 PC port — reverse-engineering notes

ROM: Final Fantasy Legend III (U), MD5 db156bc96b528996ce1bf771195171af
MBC1 + 8 KB battery SRAM, 256 KB, DMG only.

## External resources (all in ext/)
- **daid/FFL3-Disassembly** — full code/data split, rebuilds byte-identical with **RGBDS v0.6.1**
  (newer RGBDS breaks on old syntax; v0.6.1 built in tools/rgbds061). Sound engine + all
  19 songs decoded and named. ~1,070 functions still auto-named (call_BB_AAAA).
- **abyssonym/ffl3rando** — `tables/` has offsets + struct layouts for ~25 data tables:
  monsters (2ca19, 12 bytes), weapons, armour, shops (780c), prices (7e4f), chests (f629),
  formations, map meta/objects (1ac58), map exits (1a308), transformations (2feb3..),
  element matrix (320ac), spell/item/monster names (3ad9b / 3aebb / 3b533, 8-byte strings).
- **gameboycolorizations/ffl3-color** — GBC colour hack (WLA-DX patch). Has free-space map
  per bank and names for metatile / battle-load / menu routines.

## Engine structure found so far
| What | Where | Notes |
|---|---|---|
| Bank stack | rst $20 push / rst $28 pop | wBankStack, wCurrentBank |
| Far call | rst $18 + 1 byte | table at 00:3806, bank select table 00:3802 = 01,09,0B,0F |
| Field main loop | 00:0265 | 07bb → 01:4f24 → 01:4f66 (objects, 33 behaviours) → 01a0 → 3a6b (frame wait) → 01:54a0 |
| Map load | 00:027d | map header table 06:6C58 indexed by wC6E8 (map id) |
| Per-map hook | 01:54a0 | 36 hardcoded per-map behaviours, index = map header byte → wCA18 |
| Event commands | 00:1236 | 63-way switch on wCA29 & $3F |
| Text engine | 00:32cb | codes < $4E = control (table 00:3175, ~45 handlers); code $3E fires event cmd |
| Text compression | 00:308c | bytes $4E+ = dictionary, entry (b−$4E), length-prefixed list at 03:71C5 |
| Text encoding | — | A–Z $BA–$D3, a–z $D4–$ED, 0–9 $B0–$B9, space $FF |
| Script text | bank 09 (from ~09:5EB2) | dialogue with embedded event codes |
| Battle effect VM | 0C:4008 | 5-bit opcodes (byte & $1F), $1F = end, 32-entry table 0C:4320, sub-table 0C:433C |
| Tile decompressor | 00:3d76 / 3e07 | bitmask scheme; compressed gfx in bank 0D |
| Raw 2bpp gfx | banks 02–05 | font in 03, monsters/sprites in 04, tilesets in 05 |

**Key finding:** there is no separate event-script language. Cutscenes/events are dialogue text
with embedded control codes (incl. "run event command N" and flag branches). So decoding the
~45 text control codes + 63 event commands ≈ decoding the whole story logic.

## Coverage tooling (tools/)
- `cov.c` — Peanut-GB patched with a per-instruction hook. Logs every executed ROM instruction
  (per bank) and RAM-executed code, split into named phases, plus "who read this data byte".
  Runs ~10 min of gameplay in ~2–3 s. Build: `gcc -O2 -o cov cov.c`.
  Usage: `cov rom.gb outdir script.txt` (tokens: wN, a/b/start/select/up/down/left/right[*N],
  hold:btn:frames, shot:name, phase:name).
- `analyze.py` — maps coverage onto daid's labels, per-phase function lists.
- `readers.py` — which functions read each bank as data.
- `script.py` — dialogue decoder (dictionary + charmap).
- run/s.txt — input script: title → intro → naming → scripted battle → town → ship.

So far 48% of daid's identified code instructions have executed (13,944 / 28,872).
Not yet covered: random battles, shops, inns, saving, transformations, most menus, endgame.

## Suggested port route
1. Name things: feed coverage + per-phase lists back into daid's disasm (his ;@ annotation
   system), starting with field loop, text engine, event commands, battle VM.
2. Write data exporters (text, dictionary, maps, metatiles, monsters, items) → JSON/PNG.
3. New engine in C++ (SDL2): renderer for metatiles/sprites at any scale, text box, menus.
4. Re-implement: text control codes + event commands (story), field objects (33),
   per-map hooks (36), battle system + effect VM (31 ops), music (driver already documented).
5. Verify against the emulator: run the same input script in both, diff RAM state.

## Map format (solved + verified against VRAM, milestone 2)
**Correction:** milestone 1 treated wD000 as an 8x8 tilemap. It is a grid of 16x16 *blocks*.
Three levels, both 4-byte tables column-major (TL, BL, TR, BR):
1. Layout L: compressed **super-blocks** (32x32 px). Bank 7 if L < $10 (idx L-1) else bank 8
   (idx L-$10). Data ptrs `bank:4000[idx]` (end = next ptr), bitmask compressor 00:3d76.
   `07:7EA0[L]` = wD000 row stride in blocks, `07:7EC0[L]` word = block count.
2. Super-block -> 4 **blocks** via `bank:4022[idx]` (00:0f42 loop). Result = wD000.
3. Block -> 4 **tiles** via `06:4000[blockset]` (00:0a3a). blockset = gfx set byte 5 (wCA17).
   Block attribute = `06:5ACC[blockset][block & $3F]`.
   Bit 6 of a block = "inside" layer: drawn as block 1 from outside, block 0 from inside (01:5ea6).
- Header `06:6C58[map]` (192 maps), 6 bytes: layout|flag80, gfxset, spriteset, hook, music, ?
  followed by 6-byte objects, $FF-terminated (00:05bf).
- Gfx set `03:731C[gfxset]`, 7 bytes: wrap masks, 4 tile pages (bank 5, page<<8 -> $9000),
  blockset, ?. Shared tiles `03:4000` x $800 -> $8800.
- Field palettes: BGP $D2, OBP0 $D0.
- Screen: positions are pixels; leader = scroll + $50; screen origin = scroll + (16, 8).
  Verified: our render of the Talon interior = 99.6% pixel match with the emulator frame.

## Objects (00:05bf)
6 bytes: flag (0 = always, else event flag via 00:3cb2), id, row|flags, col|flags,
movement (low 5) + speed (top 2), sprite (`(b&7)<<4` tile base, or `$60+(b&$3F)` single frame).
Stored as parallel arrays at $C200 (16 slots; field accessors 00:3c03..3c98, slot in hFF96).

## Sprites (00:0438)
OBJ VRAM: 07:7f00 fixed tiles; sprite set `03:751C[spriteset]` = page mask + page list
(idx < $40 -> 04:(40+idx)00, else 03:(49+idx-40)00) + single 4-tile frames from 03:4900 -> $8600;
leader page -> $8000 (00:04b1; depends on party). Frames per 16-tile page: front, back, side, side-step.
Verified tile-for-tile against VRAM.

## Collision & events (00:1024 for the leader, 00:0ffd / 01:4b5e for objects)
- Event blocks (attr & $C0 == $C0): command = attr & $3F via table 00:124A
  (0/1 exit table warp 00:13f1; 2/3 push return pos + warp 00:13ac; 4/5 pop 00:14aa).
  Emulator: $E3 blocks are walked onto; $C2/$C4/$FF blocks are bumped.
- Otherwise blocked when attr & wC804 & wCA0F != 0. Leader flags wC804 = $45 (slot wC2E0);
  wCA0F per vehicle mode from table 00:087A ($FF, $BC, $40, ...). Emulator-verified on map $10:
  $02 walkable 168/180 (rest were NPCs), $03 blocked 67/67.
- Exits `06:6308[map]`, 4 bytes: region (row>>2, col>>2 nibbles; 64 px), dest map,
  dest row|facing, dest col|facing; leader lands at (row+5, col+5).

## Dialogue & event bytecode (milestone 3)
- A at an object (00:097f) dispatches on kind (byte 4 top bits): 0 talk -> `0A:4004[id-1]`,
  1 chest (00:09d4, items 03:762A), 2 shop (0A:6FD4), 3 special (`0A:791E[id-1]`, e.g. Talon console).
- Loop 00:3211: $00 end, $10 wait+end, < $88 control/dictionary (00:32cb, table 00:3175), >= $88 glyph.
  Glyph byte = tile number of the shared font (03:4000 -> $8800). $AC , $AD ' $AE … $AF .
  $EE/$EF quotes, $F0 : $F1 / $F2 - $F3 ! $F4 ?
- Dictionary 03:71C5: 80 length-prefixed entries. $4E-$87 -> entries 0-57; the 22 low codes
  listed at 00:30D1 -> entries 58-79. Entries can contain codes ($06, $1C $1F $00 ...).
- Arg lengths: see tools/ffl3text.py ARGS. Validated: all 239 NPC texts parse to exactly the
  start of the next text.
- Refs (00:2ef6), 2 bytes lo,hi: hi < $40 event command hi (param lo -> wCA28, 00:1236);
  $40-7F text in bank 9, $80-BF bank $A (hi|$40), $C0-FF bank 3 (hi&$7F).
- $39 f A B (if flag), $3A f g A B C, $3C A B (Yes/No), $3E A, $36/$37 set/clear flag (wC760).
- Shorthand event codes (0B:6C35/6C4C): $32->$26 $3D->$28 $3B->$27 $13->$1D $12->$1C
  $3F->$1B $0A->$10 $0D->$0E $38->$12, next byte = param.
- $1F n party member name; $2A n spell (< $30, 0E:6D9B x6) / item (0E:6EBB x8) name.
- Window: WY=80, 20x8 tiles, frame $F7 $FC $F9 / $FB / $F8 $FC $FA, text rows 2,4,6 from col 1.
  Full window scrolls by itself; $1C (before each new speaker) waits for A.
  Verified: port's window = real game's frame, pixel for pixel (Float text).
- Emulator trick (tools/talkcap.py): rewrite the Talon-console object (slot 10, wC20A/wC35A)
  into any NPC id + kind 0, press A -> the real game shows that NPC's text.

## Event commands (milestone 4) - table 00:124A, dispatcher 00:1236, param = wCA28
Usage counts over all 369 reachable NPC/special texts and every map's event blocks:
- $00/$01 exit table (13f1) · $02/$03 push return + exit (13ac) · $04/$05 pop return (14aa)
- $0A vehicle := 0, push, exit (13a8, world -> town) · $0B/$0C vehicle := 2, pop (14a5, town -> world)
- $0D/$0E teleport: 06:6BA4 + (p-1)*3 = map, row|facing, col|facing (1384/1389 -> 1468)
- $0F/$2F per-map text: map list 01:6503 (ptrs 01:6523) / 01:6563 (01:6569), bank $A text
- $12 sub-commands (16b9): 1 vehicle 0, 2 nop, 4 redraw map, $B = $0E $37, $C = $0D $02, rest effects
- $17 SHOP p-1 (19ae → 01:6FD4; was mislabelled "battle" in milestone 4) · $1B battle (19d0) · $18 give item 03:762A[p-1] (1a02) · $1C music · $1D SFX
- $25 Talon lift (1a95): carry the leader one block on · $3F (past the table end): boards the Talon
- $26 actor (01:66e9): p low nibble = slot (0 = leader), high nibble 0-3 face (0 up 1 right
  2 down 3 left), 4 hide (id := 0), 5 show (id := wC370), 6/7 animation mode
- $27/$28 walk actor (01:67f5 / 01:691e): slot = low nibble, steps = high nibble
- Most used: $26 (245), $28 (98), $17 (64), $27 (44), $1D (44), $10 (30), $1B (25), $12 (24)
  ($10 is a 16-way sub-dispatcher on p >> 4, not decoded yet.)

## Facing / directions
wC6EE >> 6 and object wC2D0 >> 6: 0 up, 1 right, 2 down, 3 left (emulator-checked).
Exits/teleports (1468): dir = (b3 bit7) << 1 | (b2 bit7). Objects: dir = (b2 bit7) << 1 | (b3 bit7).

## Object slots (00:05bf / 00:0730)
Slot 0 = leader, 1 = follower; map objects fill slots from 2 in order. Shopkeepers (kind 2) and
kind-0 objects with id >= $DF get a companion slot right after them: a copy one block in front
(talk-across-counter proxy). Verified against RAM on maps $0B and $10.

## Flags (00:3cb2)
wC760 bit n = byte n>>3, bit 7-(n&7) (MSB first); carry = set. Flag-gated objects appear when set.
Vehicle mask table 00:087A: ff,bc,40,ff,bc,bc for modes 0-5 (+ wC6F1 byte each).

## New game
Port starts where the real game first hands over control: map $0B (Talon) at (26,7) facing up,
with the event flags captured from wC760 at that moment. Party default names = character
records 0-3 at 07:7A88 ($60 bytes each): Arthur, Curtis, Gloria, Sharon; then Myron, Lara,
Dion, Faye, Dion, Borgin. Text code $1F n: n < 4 party, $10-$1F story character (07:7A88).

## Battles (milestone 5)

Entry
- Event $1B (19d0): p ≠ $FF → hFFCA = p | $80 (fixed counts); p = $FF → wCA2D = 0, random (10f5).
- Random encounter: after every step (01:4617), battle if random < 00:119A[header byte 4 >> 5]
  (00 03 04 05 06 07 08 09). Area wCA2D = header byte 5 (00:02e7); 0 if event flag $31 clear;
  $43 on map 1 with flag $CE (00:0409). Towns have rate 0.
- 10f5: pack = area + (vehicle 1 or ≥4: +1, vehicle 2: +2); r = random; c = number of
  thresholds 03:76BC (E5 BF 8C 4C 26 0C) that r is below; hFFCA = 03:76C2[pack*7 + c]
  (bit 7 set = fixed counts).
- 1126: formation = hFFCA & $7F, 5 bytes 0B:758B → wCC59, then 1d62 (0B:6CEE init, 0B:4000).

Monster counts and placement (0B:5a18, matches VRAM)
- wCC30[g] = size class + 1; 0B:5BD8[class+1] = (w-1)<<4 | (h-1): 4x4, 8x4, 4x8, 6x8, 8x8, 16x8.
- Fixed: units[g] = ((counts & $3F) << 2g >> 4 & 3) + 1. Random: units0 = rnd(((counts&$C0)>>6)+1)+1;
  room = 20 - w0*units0 (-2 if both group 0 and 1 are 4 wide); fit = room / w1;
  total = rnd(fit+1); units1 = rnd(total)+1, units2 = total - units1 (if those groups exist).
  rnd(n) = 00:1d2e.
- Classes 0/1 (4 tall) stack two per column (rows 0 and 4): monsters = units*2.
- First column = (20 - Σ w*units) / 2; groups left to right.

Pictures
- 0B:4301[m] = meta<<3 | variant; 0B:43CC[meta] = bank<<4 | class; tiles 0B:43C4[class];
  planes = decompress [p0,p1) and [p1,next p0) from 0B:43E4+meta*4; tile i = bytes
  (variant*count+i)*8.. of each plane interleaved. Row-major; class 4 skips the 2 top corners,
  class 5 all 4 corners.

Other tables
- Talents 0B:533D + m*9: mask byte then ids; names 09:67F7 x8 (Nail, Tusk, …, $70+ ●/◆ elements).
- Drops 0B:470D + m*4: element<<4|level, xp, gp, drop item.
- Battle message fragments 09:79C6-7D57 ("Strike first!", "Unexpected attack!", "Ran away." /
  "Can't run!", "Received N GP.", "X fell.", "A mighty blow!", "Missed!", "Won!",
  "<leader>'s party / was beat……").
- OBJ tiles: hand cursor $20 = 03:5430, target bracket $24 = 03:57B0. Party portraits = front
  frame of each character's field sprite page (04:7000/7400/7200/7100/7300 for the starting
  party; where the page comes from is not traced yet).

Screen (VRAM, formation 00)
- Fight menu: WY 64, two 10x10 boxes; group names rows 2/4/6 col 1; Fight/Run/Auto col 12;
  cursor x 88, y 80+16k.
- Status panel: box rows 0-6, guest box rows 6-9 (row 6 = f8…fa); member k at col 9*(k&1),
  row 1+3*(k>>1): HP cols +3..5, '/' +6, max +7..9; MP next row; chosen command ("Weap")
  replaces "/max" on the MP row. Command box (10x10) covers the other column; portraits there
  are moved off-screen.
- Messages: WY 88, 7-row box, two text lines (rows 2/4), typed and scrolled; party field
  sprites at y 72, x 8+32k.

Still to decode: battle effect VM (0C:4008; opcode = byte&$1F, tables 0C:4320/0C:433C) for
hit/damage/critical/escape, enemy AI, Talent/Magic/Item, EXP/level-ups.

## Battle VM (milestone 6, in progress)

The whole battle is bytecode in bank 0C. 0B:4044 enters the VM once (0C:4000, de = 0C:4A2D);
when the program ends (op $1F at statement level) 0B re-enters at 4A2D for a retry after a
defeat (RAM changes in between are recorded as "E" lines). Instruction set: tools/battlevm.py;
C++ interpreter: src/battlevm.cpp (exact, including the original's quirks: a signed multiply
whose negation drops the low byte to $80, x/0 = 0, the statement-skip table at 0C:437C that
skips only 1 byte after an "if" for op $0B).

Testing: tools/trace.c (Peanut-GB) with REPLAY=out.txt records every VM statement (S), native
call + its RAM changes (N/D), random numbers drawn by the VM (R, at 0C:4416 / 0C:42FA) and
re-entries (E); enter.mem = RAM at the first entry. tools/rec.py drives a battle; tests/vmreplay
replays it: 9 recordings (mash A, boss defeat + retry, Run, Auto, Item, Talent, three
formations) - every statement matches, ~1.1k native calls replayed.

Variables (wC900+): $00-1F actor record copy, then actor/target pairs: $20 actor HP, $22
target HP, $24/$26 max HP, $28/$2A MP, $30-33 actor str/def/mag/agi, $34-37 target's; $9B
action type<<4, $9D action id; $AD-$BE action data (09:5466); $B6 = power; $BC = kind;
$BF message id (09:78FE table -> battle text); $C0 = damage being built; $D6 = actor index,
$DE target index; $68 hit chance, $69 roll (rnd 100 + 1). Damage table wD822 + 2*target
($8080 = none); wCC5E-60 alive bitmasks of the 3 enemy groups; combatants wD000 + i*$80.

Weapon damage example (Myron, ●Battle, crit on a Hooligan): $C0 = power + str + mag
(0C:7724, by weapon kind $BC), x2 for a critical (0C:7BDC: roll <= 5), x2 again for the
genus bonus (0C:75B9), then $C0 / targets - def / targets (0C:75D9) = 204.

Natives (VM op 03), 45 in all:
- logic, translated in src/battlenatives.cpp and checked against the recordings: 445F 4490
  44AC 4865 49DB 4B9F 4BF6 4D41 5DB6 5F9D 5FCC 5FD3 5FDB 5FF1 602B 6042 606D 6083 6851,
  6B49 (= 09:5466 action data), 6B64/6B61 (09:5D7F/5D64 item info -> wCEDB, wCE28/29),
  6B41 (0B:701A name of item wCE8C -> wCE3C), 6B4C/605D (0B:6EAE/6EB6 bit range ->
  wCE8F/90), 6B52 (0B:7E89), 6B58 (0B:6947), 6B2B/6B3B (0B:706C/7037 inventory take/add;
  inventory = 24 (id, count) pairs at wC6FF). Not yet exercised by a recording: 6B4C 6B52
  6B58 6B2B 6B3B and the spell path of 6B64.
- still to do: 6B44 (0B:7349 + 0B:728B equipment/status), 6B25/6B28 (0B:44F0/4502 build a
  monster record at wD680: stats 0B:4A19, talents 0B:533D, drops 0B:470D, calls 0B:6E74,
  7349, 728B), 1D71/1D74/1D77 (rst18 1D/19/1C bank 0B 6E40/6ED9/5BDF), 1E46, 2499, 24C7
- UI: 1D69 = 0B:5BDF, the whole player command phase (Fight/Run/Auto, member commands,
  targets) -> per-member commands at wCE00 + 4i (4D41 copies them to record +$62-64);
  5FB7 = 0B:6FA1 print battle message (pointer wCE8D/8E); 6001, 6B68/6B6C/6B71/6B7F =
  animations and sound (wDE00 = sound effect).

### Battle setup before the VM (0B:4000)
ffBA = ((wCC5C >> 4) & 3) + 1; call 0B:405A; call 0B:4444; rst18 6E; call 00:1DFD;
wDDFF = 4 or 5 (guest present: wD203 != $FF) ... then VM at 4A2D. These build the combatant
table wD000 (party copies + monsters, likely via 0B:44F0) and must be ported too.

### Command phase output (1D69 = 0B:5BDF with wCE88 = 1)
Per party member i: wCE00+4i = action type (record +$62 -> VM $9B: type<<4 | flags; 0 =
attack with the slot-0 weapon), +1 unused, +2 target (record +$63 -> $9C: combatant index,
or $80 | group+1 for a whole group; $FF = none), +3 action id (record +$64 -> $9D; $FF for
the default weapon). Run: wD061 bit 7 set, targets $FF. Mashing A gives 00 00 05 FF
(first alive monster). wCE88 = 2 is the "retry / give up" menu after a defeat (1D77).

### Battle messages (5FB7 = 0B:6FA1)
Text id wC9BF -> 09:78FE pointer table -> bank 9 text in the normal text VM format. Battle
name codes: $1F $FF = combatant wCD36 (party 0-4, enemies $80+k = slot k+5), $1F $80 =
combatant wCD4C, $1E $80 = the name in wCE3C (built by 6B41), $30 = a number. A turn's
messages accumulate in a 2-line window (WY 88) and scroll. Ids: $34 "X used / Y against",
$35 "Z.", $3E "A mighty blow!", $1D "X fell.", $40 "Missed!", $43 "Won!", $06/$07 run.

## Sound (bank 0F)

Driver at 0F:4000 (per frame, from vblank) / 0F:4003 (init). hFFBA = song request (0 stop,
1-20 via 0F:482F: 4 channel pointers each), hFFBB = current song, hFFBC = SFX request (1-65,
pulse-1 script 0F:66F2, noise script 0F:6774). A song change is only taken while no SFX is
playing. Channel blocks wCB02 (pulse 2), wCB1A (pulse 1), wCB32 (wave), wCB4A (noise):
+0/+1 loop counters, +3 status (FF = ended), +4 note countdown, +5 note, +6 octave offset,
+7 data pointer. Tempo: wCB00 += wCB01, notes advance twice on carry. Note byte: low nibble =
length (0F:41BE), high nibble = note (0-B pitch via 0F:4114, C rest, D off); E0-EF octave;
F0-FF commands (tables 0F:4428/4448/4468/4488). Effects run on a 3-frame rotation.
SFX script entries: delay, then 5 bytes NR10-14 (pulse) / NR42, NR43 (noise); F0-FE set a
repeat count, EF xx xx loops, 00 ends and restores the music channel (40DD / 40FD).
Game-side writers: map header byte 4 & $1F (1F = keep), event $1C/$1D, 00:10D7 (battle:
save + stop music, $36 encounter), 0B:4000 battle song = ((wCC5C >> 4) & 3) + 1, the battle
bytecode writes hFFBC itself, 0B:6D63 receive ($35 / $3D for D8-E9 / $3B full), 00:1E7C $3A
OK, 00:1E83 $3B error.

## Battle animations (bank 2, 02:4000-52FF code, data from 02:5300)
Entry points from the battle bytecode natives:
- 0C:6B7F -> 02:4000 (c9bd bit 7 = has effect, id = c9bd & $7F -> wDE00; actor wC9D8 -> wDE01).
  Enemy actor (>= 5): 02:4023 with 02:4101/4105 bob patterns around 02:41B1. Party actor:
  bob table 02:40FA (y 58 5B 58 5A 58 59 58 57 56 55 54 55 56 57 58), neighbour swap 02:40A7,
  then 02:4110 (party row set-up) when the target is a party member.
- 0C:6B68 / 6B6C / 6B71 -> 02:4394: id wDE00 & $3F through table 02:4364 (24 entries):
  0 43B1 1 43EA 2 43F8 3 439F 4 43C8 5 44B8 6 4437 7 463E 8 4697 9 458A A 47C6 B 48C6
  C 45C9 D 45C0 E 4BA9 F 4BCD 10 4D65 11 4D45 12 4F20 13 4F3E 14 41A0 15 4109 16 4142 17 4167.
  6B68 = id $11, 6B6C = id $10, 6B71 = current wDE00.
- 0C:6001: copies tiles (wD00D ... -> VRAM $86C0).
Party row: wDC80 (frame counter), wDC81, wDC82 = count, wDC83 + 8*i = two OAM entries
(y, x, tile, attr) per member; y $58 (screen 72), x = 12 + 40*i for 4 members (02:4139 x table
by wDDFF bit 0: 14 3C 64 8C / 10 50 70 90), tiles $6C + 2*i... (02:4154).
Script effects (02:41B1): tiles = 02:658C + $200 * 02:651C[id] -> OBJ $8000 ($200 bytes); then
script 02:53EE[id]: $FF end (-> 45AE clear), bit 7 = tile offset (a*2 -> wDE02), bit 6 = run
effect id (02:4397), else frame: a = layout code (wait = ((a&7)+1)*4 frames, flags (a<<1)&$70),
next byte = sprite grid index: 02:5A95[i] -> RLE ($80|n = n literal/repeat, $C0 = blanks)
decoded into a 6x5 grid at wDE40 ($1F = empty), optional x-flip (430B) / y-flip (433B),
drawn to OAM at the target position (02:42C0: enemy = wCC41/wCC51 slot rect, party = row).
Real-game reference: prologue battle, attack = actor rises 4 px (2 frames/step), weapon grid,
impact (tiles $64/$66), damage digits (OBJ $00-$02 reloaded) bouncing y 20-24, death = 4x4
grid of OBJ $00 masks.

Translated in src/battleanim.cpp (C++20 coroutines; 00:3A6D "wait a frames" = co_await).
- Vblank 00:3E26 (when wCBFF): wDC80 = (wDC80+1)&15; at 0 every member whose wDC81 bit is
  clear swaps the tiles of his two OBJs and xors attr $20; then wDC82*8 bytes DC83 -> wC078.
  02:4142 (id $16) clears it; bank 0B does the same before the command menus.
- 0C:6001 = 0B:7DCA(a=2): member i's sprite page (bank 4, page<<8 | $40, 64 bytes) -> $86C0
  + $40*i, 4 members if wD203 bit 7 else 5; if (rec+1 & $FC) | (rec+2 & $E0) the low bit
  plane is cleared (0B:7E4F: grey silhouette).
- Script tiles: $200 bytes from 02:658C + $20 * 02:651C[id]; 03:79C8 copies 03:79D4 -> $8200.
- 02:42C0 enemy anchor: x = col + ((w-1)>>1) - 1, y = row + ((h-1)>>1) from wCC41 (col,row
  per slot) / wCC51 ((w-1)<<4 | (h-1)); party: x = 5*i (4 members, a = 4) or 4*i (5, a = 0),
  y = 7. Grid sprites at OAM (y*8 + 8*r, x*8 + 8*c).
- Damage numbers: wD822 (13 words, $80xx none, $88xx miss, negative = heal) -> wDE40; digits
  from 00:2009 (a=2) into wCD2B-CD31; glyphs 03:78F8 as 8x16 (blank top) at $8000 (03:78D6);
  particles at wDF00 (30 x 8: age, life, motion ptr, y, x, tile, attr), motion 02:4E4B
  (life $18, then dy,dx pairs); a number every 16 frames, monsters first if wD873 >= 5.
- Raster split 02:441A: BGP / OBP0 = table value until LY 64, then $D2 (both).
- Death style 0 (02:4FA4): mask OBJ tile 0 over the picture (rows of 8x16, Y-flipped on
  alternate rows), patterns 02:52FE + 16k (k = 7..0) scrolled up a row 3 times each, then
  02:52C1 fills the picture's BG rect with $FF. Styles 1-3 (02:5055 / 50ED / 5167) rewrite BG
  tiles at $9000 and scroll - not translated yet.
