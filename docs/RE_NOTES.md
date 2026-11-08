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

## Map format (solved, milestone 1)
- Header `06:6C58[map]` (192 maps), 6 bytes: layout|flag80, gfxset, ?, hook, music|hi3, ?
- Gfx set `03:731C[gfxset]`, 7 bytes: wrap masks (nibbles), 4 tile pages, tile-attr index, ?
- Tiles: `03:4000` x $800 → VRAM $8800 (tiles $80–$FF, shared); four $200 pages from
  bank 5 (`page<<8`) → $9000 (tiles $00–$7F). NB: daid's labels say bank 07, but the
  `rst $18 $31`/`$51` args select banks 3/5.
- Layout L: bank 7 if L < $10 (idx L−1) else bank 8 (idx L−$10).
  Data ptrs at `bank:4000[idx]` (end = next ptr), metatile defs at `bank:4022[idx]`.
  Row stride (bytes of tilemap) `07:7EA0[L]`, tile count `07:7EC0[L]` (word, /4 = metatiles).
  Decompressed bytes = metatile index per cell; metatile = 4 bytes TL, BL, TR, BR.
- Tile attributes / collision: `06:5ACC[gfxset.tileattr]` + (metatile & $3F) (00:0e13, 00:1060).
