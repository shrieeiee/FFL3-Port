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

Not yet: event $10 and most $12 effects (logged in the title bar), chests, shops, menus,
music, NPC wandering, saving, the opening sequence itself.

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
