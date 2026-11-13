# FFL3 PC port

A native PC port of **Final Fantasy Legend III** (SaGa 3, Game Boy, 1991), written in C++17 + SDL2.

> **You need your own, legally obtained ROM of Final Fantasy Legend III (U).** No game data,
> graphics, text or ROM dumps are included in this repository, and none will be accepted.
> The port reads everything (graphics, maps, text, the battle code itself) straight out of
> your ROM at runtime. This is an unofficial fan project, not affiliated with or endorsed by
> Square Enix. Final Fantasy and SaGa are trademarks of Square Enix.

Expected ROM: `Final Fantasy Legend III (U)`, 256 KB, MD5 `db156bc96b528996ce1bf771195171af`.

## Status

**Milestone 1 – maps** ✅
- ROM loading + validation (title `SAGA3`, global checksum), original bitmask decompressor
- Three-level map format (super-blocks → blocks → tiles), all 192 maps

**Milestone 2 – walking the field** ✅
- Rendering verified against the real game: 99.6% pixel match with an emulator frame
- Party leader + NPCs with the game's own sprite sets (OBJ VRAM rebuilt tile-for-tile)
- Collision from block attributes, inside/outside layers (roofs hide interiors)
- Event blocks: exits, entering buildings and coming back out (return stack)
- Exit landing positions verified against the real game

**Milestone 3 – talking** ✅
- Full text/event bytecode: dictionary compression, all control-code lengths (every one of the
  239 NPC texts parses exactly), flag branches, text calls, Yes/No prompts, item/spell names,
  event-call shorthands
- Dialogue window, typewriter, auto-scroll and speaker waits - pixel-identical to the real game
- Talk to NPCs (they turn to face you) and special objects

**Milestone 4 – events & new game** ✅
- New Game starts where the real game hands you control (aboard the Talon), with the real
  event-flag state; flag-gated NPCs appear or stay hidden like the original
- Object slots like the game, including the invisible "talk across the counter" companions
- Field events: exits, buildings, town ↔ world (vehicle modes), teleports, signs/per-map texts,
  the Talon lift and boarding, scripted cutscenes (actors face/walk/hide/show; scripts wait)
- Verified against the real game: Talon exit/boarding round trip, house door, the Elder's
  cutscene (actor ends at the same block, same facing)

**Milestone 5 – battles (stage 1)** ✅
- Random encounters with the game's own per-map rates and encounter tables, and scripted
  battles (event $1B) – boss fights included
- Formations, monster counts and placement exactly as the original works them out (checked
  against VRAM), monster stats/names/talents, every monster picture decoded from the ROM
- Battle screen pixel-identical to the real game for the Fight/Run/Auto menu and the status
  panel; command menus, target brackets, typed battle messages in the game's own wording
- Fight (Weapon) → pick a target, Run, Auto; win / run / party wiped (back to the start)
- **Milestone 6 (in progress) – the real battle rules.** The whole battle is bytecode in the
  ROM; the port runs it on a C++ interpreter of the original VM, with every helper routine
  it calls translated to C++. Damage, hits, criticals, enemy AI, turn order, EXP, level-ups and
  the battle messages are now the original's. Checked by replaying 12 battles recorded in the
  real game, statement for statement.
- Talent / Magic / Item: lists, usability, MP checks and target cursor rules from the
  original menu code (one ally / whole party / one enemy / an enemy group).
- Auto Set (0B:5F2F): members picked there are flagged (record bit 7), show "Auto" and keep
  last round's command. A wipe in a random-battle area asks "Fight again?" (0F:7F8F): Yes
  retries with the gold etc. as they were (wC6E0 from wDD00), No goes back to the title.

**Milestone 7 – the rest of the game loop** ✅
- Chests (item from 03:762A, "Received X." / "Empty", opened chests stay open)
- Shops and inns (wares 01:7804, prices 01:7E4F, sell for half, inn = missing HP + MP)
- Field menu: Equip (bag <-> equipment slots, stats recomputed by the game's own routines),
  Items, Status, Save
- Save / load (F5 / F9, `--save` / `--load`)
- Battles: a beaten party fights again in random-battle areas; story fights you lose carry
  on at 1 HP; targets blink when hit; MP costs in the Magic list

**Milestone 8 – music and sound** ✅
- The bank-0F sound driver translated to C++ (`src/sound.cpp`): all 20 songs and 65 sound
  effects, vibrato / envelope lists, SFX borrowing pulse 1 and noise. Checked against the real
  game: 30,818 driver frames replayed with identical APU register writes and RAM state
  (`tests/sndreplay.cpp`, logs from `tools/sndtrace.c`).
- A Game Boy APU synth (2 pulse with sweep, wave, noise, NR50/51 mixing, output high-pass)
  on SDL audio, one driver tick per GB frame.
- Hooked up: map music, event $1C/$1D, battle music + the encounter sound, the battle
  bytecode's own sound effects (it writes hFFBC), chest / shop jingles and error buzz.
- `--mute` turns sound off; `--wav music|sfx HEXID SECONDS` renders `ffl3.wav`.

**Milestone 9 – using things outside battle** ✅
- Field menu Items / Magic: items and spells run through the game's own field code (09:521A):
  Cure-type healing (fixed for items, % of max HP for spells), Elixir, MP refills, status
  cures, revive, stat and max-HP seeds, whole-party effects, MP paid by the caster. Usability
  and targets come from 09:5D87 like in battle.
- Event $10: characters joining / leaving (0B:6DE4 / 6E33, 4th member stays for good), the
  saved return spot (01:6A74 table, save / restore), facing flags, object reload; $12 $10.
- Text codes $0C (timed pause), $11 / $27 (cosmetic). Fixed $27's length (it takes no
  argument): it was knocking 18 NPC scripts off the rails (the Dwelg fight text among them).
- Story changes to maps (0B:78F5): 47 flag-gated block patches applied on map load and by
  event $1F, e.g. opened passages.
- Pushable boulders (move type $0B, 01:51FE) and the hole puzzle (event $2B).
- The Talon: the cockpit (event $23, 01:7972) with its own windows and texts - Depart,
  Engine, Weapon and Option (Warp not yet); parts in the bag get attached with their story
  flags; take-off mode from flags $39/$3A and the engine (01:63E6); flying (modes 6-8 go
  anywhere), B lands back in the cockpit, A on the parking spot goes aboard.
- Shops and inns redone with the game's own screens and texts, pixel-identical to the real
  thing apart from the party's walk animation: Exit / Buy / Sell / Mix, Yes / No boxes,
  both kinds of Mix (Light + Dark stones for gear; two elements for a spell), inns $40-$4F.
- Field menu redone to the game's screens (layouts read from the real game's window tiles):
  Item (mark, swap, use), Magic, Talent, Status (class, NEXT / EXP, all stats), Equip (two
  columns of slots, the bag filtered by slot), Auto (record +0 bit 7), Save (three files).
  Guessed, not checked: status names for anything but Good, classes other than Human /
  Mutant, which of record +$3D-3F is Evade / M.Def / M.Evade; the Talent screen's
  resistance marks and the item list's bin aren't in.
- Townsfolk wander (01:4F66 / 5060): move types 0-3 and $1B-$1E take a random step about
  once a second at half walking speed, blocked by walls, event blocks and people.
- Map event blocks: damage floors ($20), status-cure ($21) and healing ($22) spots, the
  slide-and-exit $08/$09.

**Milestone 10 – the opening** ✅
- Title screen (01:7C79): the logo unpacked from 03:5C00 (00:3D76), New Game / Continue;
  matches the real screen pixel for pixel. Continue loads `ffl3.sav`.
- New game (01:7D40): party records, the wC6E0 block and the starting flags from the ROM,
  the story crawl (09:5EAD) over the Water Entity, naming the four (0F:7A44: the game's
  grid, labels and rules), then the prologue script 09:6015 - the simulator battle and every
  cutscene up to the Talon, ending in the same state as the real game (flags, party, gold,
  bag; `--opening` tests it). `--no-title` skips straight to the Talon as before.
- Big story sprites (00:3A7F counted layouts) such as the Water Entity; event $12 $00 (hide
  the leader on the next map); text at the game's speed (00:3593: 11 frames a character,
  A held = fast); scripts wait for a battle / shop an event opens; scripted walks no longer
  set off step events or random battles; event $18 sets its story flag (0F:7C74).

**Milestone 11 – battle animations** ✅
- The bank 2 animation engine translated to C++20 coroutines (`src/battleanim.cpp`) working on
  the battle's own memory image - OAM shadow, OBJ tile RAM, scroll and palette registers - so
  the battle waits on them exactly where the GB does. The battle screen is now drawn from that
  memory too: BG map $9800 with the monster tiles at $9000, the 8x16 OBJ layer, and SCX / SCY /
  palettes line by line (the raster effects wait for LY and change them part-way down).
- Done: the party row (walking on the spot, members stepping forward, status silhouettes), the
  sprite-grid scripts over monsters (weapons, claws, impacts), shake, flashes, fades, the LCD
  blink, damage numbers / "Miss" bouncing as particles, the monster death dissolve, the boss
  deaths (line drop-out; bounce, flash and tile melt), and the spell effects: converging
  sparks, the ripple (per-line SCX), fire, bubbles, falling rocks, orbs, clouds, the shimmer
  with rising sparks, the whirling disc (window edge per line), BG tile patches, a monster
  taking another's picture, the thrown object, and the boss changing form (death style 2).
  All 24 effect ids are translated. Checked against emulator OAM / VRAM dumps where there
  are recordings (`tests/test_battle.py`); the spell effects are checked by eye so far.
- Debug: `FFL3_FORCEFX=id` plays a sprite script on the next attack, `FFL3_FORCEEFFECT=id`
  (+ `FFL3_POKE=addr=val,...`) an effect, `FFL3_DEATHSTYLE=n` a death style.

- Animated map tiles (01:54A0, all 36 per-map hooks: flowing water, waterfalls, lights),
  checked against the real game's VRAM (`tests/test_tileanim.py`). Event $12 $03 (screen
  flash) and $05 / $06 / $0A (objects gliding off in formation).

Not yet: event $10 $5 / $8 / $A / $C / $F and $12 $07-$09 / $0D-$0F / $11 (vehicle changes,
a member transformation, the ending). The port names them on screen when one comes up. The last prologue walk on the Talon's map is placed at the
game's hand-over spot rather than walked exactly.

## Testing

`docs/TESTING.md` is the tester's guide: the F7 test mode (every item to check, set up with one
key, marked ok / bug), F8 video recording (AVI, RLE delta frames + PCM), F12 bug reports (the
last 30 s as video, screenshot, save, ten minutes of input, a game log), crash reports, the
F10 command line and a checklist. `tests/test_tester.py` drives it all with injected keys.

## Building

Needs CMake 3.16+ and a C++20 compiler (coroutines: GCC 10+, Clang 14+, MSVC 2019 16.8+). SDL2 is picked up from the system, or from
`third_party/SDL2`, or fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Windows build from Linux (MinGW-w64, one self-contained exe):

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --target ffl3
```

Windows (Visual Studio 2022): same commands from a Developer prompt, or open the folder in VS.

## Running

Put your ROM next to the exe as `saga3.gb` (or pass the path / drag it onto the exe).
It must be **Final Fantasy Legend III (U)**, MD5 `db156bc96b528996ce1bf771195171af`.

```
ffl3 [rom.gb] [--map HEX] [--pos ROW,COL] [--wide] [--scale N]
     [--walk UDLRAW...] [--shot out.bmp] [--debug] [--dump-blocks out.bin]
     [--text ID]                                                    (headless/testing)
     [--battle HH [--keys ABUDLRW*] [--shot out.bmp] [--shot-at N] [--dump-view f]]
```

| Key | Action |
|---|---|
| Arrows / WASD | walk |
| Z / Space / Enter | talk, advance text, confirm (A) |
| X / Backspace | back (B) – battle menus |
| Tab | GB 160×144 ↔ widescreen 256×144 |
| F1 | collision overlay (green walk, red solid, blue event) |
| F2 | no-clip |
| M / Shift | field menu (Equip, Items, Status, Save) |
| F5 / F9 | save / load `ffl3.sav` |
| F3 | random battles on / off |
| F4 | start a random battle for the current map |
| PgUp / PgDn, `[` / `]` | previous / next map |
| P | cycle palette (grey / DMG / Pocket) |
| F12 | screenshot (BMP) |

## Tests

`tests/run_all.py saga3.gb [--battle-dumps DIR] [--rec-dumps DIR] [--vm-recs DIR]` runs every
suite below plus the in-game autotest and prints a summary (exit 1 on failure). Suites that
need dumps from the real game skip when the folder isn't given.

- `ffl3 saga3.gb --autotest` – plays every test-mode item with scripted keys under its own
  pacing, checks the expected log line / VRAM change / flash / sound / save round trip, and
  writes `autotest/results.html` (per-item AVI + snapshots). `FFL3_AUTOTEST_ITEMS=1,9`
  picks items. Works headless with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

- `tests/test_maps.sh saga3.gb` – C++ map loader vs the Python reference, all 192 maps
- `tests/test_field.sh saga3.gb` – collision/exit behaviour, expected values taken from the
  real game running in the coverage emulator
- `tests/test_vm.sh saga3.gb build/vmreplay RECDIR` – replays battles recorded in the real game
  through the battle VM (recordings are made with `tools/rec.py`; they contain game data, so
  they are not in the repo)
- `tests/test_battle.py build/ffl3 saga3.gb DUMPS` – formation layout, monster data, battle
  windows and picture tiles vs VRAM dumps from the real game (VRAM checks skip without dumps)

## Reverse-engineering tools (`tools/`)

- `ffl3data.py` – Python reference for the data formats (decompressor, maps, tilesets)
- `script.py` – dialogue decoder (dictionary compression + charmap)
- `cov.c` – Peanut-GB with per-instruction coverage, data-read tracking and RAM/VRAM dumps.
  Build: `gcc -O2 -o cov cov.c` (needs `peanut_gb.h` from github.com/deltabeard/Peanut-GB)
- `analyze.py`, `readers.py` – map coverage/data reads onto the disassembly's labels
- `playthrough.txt` – input script for `cov` (title → intro → battle → town)
- `ffl3text.py` – text/event bytecode parser; run it on the ROM to validate every NPC text
- `talkcap.py` – make the real game show any NPC's text (rewrites an object in RAM) and capture it
- `route.py`, `drive.py` – plan a route with the port's rules and drive the real game along it
  in `cov`, one step at a time, to get ground truth

See `docs/RE_NOTES.md` for engine structure and addresses. It builds on
[daid/FFL3-Disassembly](https://github.com/daid/FFL3-Disassembly) and the tables in
[abyssonym/ffl3rando](https://github.com/abyssonym/ffl3rando).

## Research tools

`tools/` holds the scripts used to reverse-engineer the game (map/text decoders, the battle VM
disassembler, emulator-driven recorders). The emulator tools (`cov.c`, `trace.c`) build
against [Peanut-GB](https://github.com/deltabeard/Peanut-GB) (MIT), which is not included;
some scripts still point at the author's local working folders.

## Licence

The code in this repository is MIT licensed (see `LICENSE`). That covers this code only, not
the game it ports.
