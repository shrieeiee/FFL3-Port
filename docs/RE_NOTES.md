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
- $17 battle, formation p-1 (19ae) · $18 give item 03:762A[p-1] (1a02) · $1C music · $1D SFX
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
