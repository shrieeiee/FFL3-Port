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
- Not yet: the real "Auto set" mode (Auto = everyone attacks), what happens after a wipe
  (the port goes back to the start), battle animations (short pauses instead), MP cost
  shown in the Magic list.

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
- Townsfolk wander (01:4F66 / 5060): move types 0-3 and $1B-$1E take a random step about
  once a second at half walking speed, blocked by walls, event blocks and people.
- Map event blocks: damage floors ($20), status-cure ($21) and healing ($22) spots, the
  slide-and-exit $08/$09.

Not yet: event $10 A/C/F/5/8 and most $12 effects, the shop / menu screens' exact
original layouts (the port's own for now), the battle
"Auto set" mode, battle animations, the opening sequence.

## Building

Needs CMake 3.16+ and a C++17 compiler. SDL2 is picked up from the system, or from
`third_party/SDL2`, or fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
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
