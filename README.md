# FFL3 PC port

A native PC port of **Final Fantasy Legend III** (SaGa 3, Game Boy, 1991), written in C++17 + SDL2.

No game data lives in this repo. The port reads graphics, maps, text and so on straight out of
**your own ROM** at runtime, the same way the original code does.

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

Not yet: most event commands (they're logged in the title bar), chests, shops, NPC movement,
menus, battles, music, vehicles (the Talon), save data / real event-flag state, and overworld
collision is inferred rather than emulator-checked.

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
```

| Key | Action |
|---|---|
| Arrows / WASD | walk |
| Z / Space / Enter | talk, advance text, confirm |
| Tab | GB 160×144 ↔ widescreen 256×144 |
| F1 | collision overlay (green walk, red solid, blue event) |
| F2 | no-clip |
| PgUp / PgDn, `[` / `]` | previous / next map |
| P | cycle palette (grey / DMG / Pocket) |
| F12 | screenshot (BMP) |

## Tests

- `tests/test_maps.sh saga3.gb` – C++ map loader vs the Python reference, all 192 maps
- `tests/test_field.sh saga3.gb` – collision/exit behaviour, expected values taken from the
  real game running in the coverage emulator

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
