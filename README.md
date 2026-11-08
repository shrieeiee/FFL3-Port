# FFL3 PC port

A native PC port of **Final Fantasy Legend III** (SaGa 3, Game Boy, 1991), written in C++17 + SDL2.

No game data lives in this repo. The port reads graphics, maps, text and so on straight out of
**your own ROM** at runtime, the same way the original code does.

## Status

**Milestone 1 – map viewer** ✅
- ROM loading + validation (title `SAGA3`, global checksum)
- Original bitmask decompressor (00:3d76)
- Map headers, gfx sets, layouts, metatiles, BG tilesets
- All 192 maps load; tilemaps are verified tile-for-tile against the real game's RAM
- Viewer: scroll, switch maps, zoom, palettes, metatile grid, screenshots

Next up: sprites and NPCs, collision/tile attributes, exits, walking the party around.

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
ffl3 [rom.gb] [--map HEX] [--scale N] [--shot out.bmp] [--dump-tiles out.bin]
```

| Key | Action |
|---|---|
| Arrows / WASD | scroll |
| PgUp / PgDn, `[` / `]` | previous / next map |
| `+` / `-` | zoom |
| P | cycle palette (grey / DMG / Pocket) |
| G | metatile grid |
| F12 | save the whole map as BMP |

## Tests

`tests/test_maps.sh saga3.gb` checks the C++ loader against the Python reference
(`tools/ffl3data.py`) for every map.

## Reverse-engineering tools (`tools/`)

- `ffl3data.py` – Python reference for the data formats (decompressor, maps, tilesets)
- `script.py` – dialogue decoder (dictionary compression + charmap)
- `cov.c` – Peanut-GB with per-instruction coverage, data-read tracking and RAM/VRAM dumps.
  Build: `gcc -O2 -o cov cov.c` (needs `peanut_gb.h` from github.com/deltabeard/Peanut-GB)
- `analyze.py`, `readers.py` – map coverage/data reads onto the disassembly's labels
- `playthrough.txt` – input script for `cov` (title → intro → battle → town)

See `docs/RE_NOTES.md` for engine structure and addresses. It builds on
[daid/FFL3-Disassembly](https://github.com/daid/FFL3-Disassembly) and the tables in
[abyssonym/ffl3rando](https://github.com/abyssonym/ffl3rando).
