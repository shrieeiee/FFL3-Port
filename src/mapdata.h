// Map data, mirroring the original loader at 00:027d / 00:0f42.
#pragma once
#include "rom.h"
#include <array>

namespace ffl3 {

constexpr int kMapCount = 192;        // entries in the 06:6C58 header pointer table

struct MapHeader {                    // 06:6C58[map] - 6 bytes
    uint8_t layout;                   // wCA15 (low 7 bits)
    bool flag80;                      // wCA6C
    uint8_t gfxset;                   // wCA1D
    uint8_t unk1e;                    // wCA1E
    uint8_t hook;                     // wCA18 - per-map behaviour (01:54a0 jumptable)
    uint8_t music;                    // hCurrentMusic (low 5 bits, $1F = keep current)
    uint8_t music_hi;                 // top 3 bits -> 00:119a table -> wCA2B
    uint8_t unk2d;                    // wCA2D
};

struct GfxSet {                       // 03:731C[gfxset] - 7 bytes
    uint8_t hmask, wmask;             // wCA05 / wCA04: map wrap masks
    std::array<uint8_t, 4> pages;     // wCA19..wCA1C: bank 5 pages ($200 each) -> VRAM $9000
    uint8_t tileattr;                 // wCA17: index into 06:5ACC metatile attribute tables
    uint8_t unk;
};

struct Layout {
    int bank = 0;
    int w = 0, h = 0;                 // in metatiles (16x16 px)
    std::vector<uint8_t> cells;       // metatile index per cell
    std::vector<uint8_t> tiles;       // expanded 8x8 tile numbers, (w*2) x (h*2)
    bool valid = false;
};

MapHeader loadMapHeader(const Rom& rom, int map);
GfxSet loadGfxSet(const Rom& rom, int idx);
Layout loadLayout(const Rom& rom, int layout);

// 256 BG tiles (2bpp, 16 bytes each) as the game has them in VRAM for this gfx set
// (tiles 00-7F from the gfx set pages, 80-FF shared tiles from 03:4000).
std::array<std::array<uint8_t, 16>, 256> loadBgTiles(const Rom& rom, const GfxSet& g);

} // namespace ffl3
