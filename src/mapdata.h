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
    uint8_t unk1e;                    // wCA1E - sprite set (03:751C)
    uint8_t hook;                    // wCA18 - per-map behaviour (01:54a0 jumptable)
    uint8_t music;                    // hCurrentMusic (low 5 bits, $1F = keep current)
    uint8_t music_hi;                 // top 3 bits -> 00:119a table -> wCA2B
    uint8_t unk2d;                    // wCA2D
};

struct GfxSet {                       // 03:731C[gfxset] - 7 bytes
    uint8_t hmask, wmask;             // wCA05 / wCA04: map wrap masks
    std::array<uint8_t, 4> pages;     // wCA19..wCA1C: bank 5 pages ($200 each) -> VRAM $9000
    uint8_t blockset;                 // wCA17: block defs 06:4000[n], block attributes 06:5ACC[n]
    uint8_t unk;
};

// Map layouts are three levels deep:
//   super-block (32x32 px, compressed in bank 7/8)  -> 4 blocks via bank:4022[layout]
//   block       (16x16 px, what the game keeps at wD000; collision/events work on these)
//               -> 4 BG tiles via 06:4000[blockset]
// Both 4-byte tables are column-major: TL, BL, TR, BR.
struct Layout {
    int bank = 0;
    int w = 0, h = 0;                 // in super-blocks
    std::vector<uint8_t> cells;       // super-block index per cell
    int bw = 0, bh = 0;               // in blocks (= 2w x 2h)
    std::vector<uint8_t> blocks;      // wD000: block id per 16x16 cell (bit 6 = inside layer)
    bool valid = false;
};

// 6-byte object entries that follow the header in the 06:6C58 record, $FF-terminated.
// Unpacked by 00:05bf into the object slot arrays at $C200.
struct MapObject {
    uint8_t flag;                     // 0 = always there, else event flag (00:3cb2) decides
    uint8_t id;                       // wC200/wC370: object id (dialogue / event key)
    uint8_t row, col;                 // block coords (low 6 bits of bytes 2/3)
    uint8_t flagsY, flagsX;           // top 2 bits of bytes 2/3 (wC2D0 / wC360)
    uint8_t move;                     // wC320/wC340: movement type (low 5 bits)
    uint8_t speed;                    // wC350: top 2 bits of byte 4
    uint8_t sprite;                   // raw byte 5
    uint8_t spriteTile() const {      // wC280: OBJ tile base
        return (sprite & 0xc0) ? uint8_t((sprite & 0x3f) + 0x60) : uint8_t((sprite & 0x07) << 4);
    }
};

// 06:6308[map] - 4-byte exits, used by event commands 0-3 (00:13f1).
struct MapExit {
    uint8_t regionRow, regionCol;     // 4x4-block (64 px) region the leader must be in
    uint8_t dest;                     // destination map
    uint8_t row, col;                 // destination scroll origin in blocks (leader = +5,+5)
    uint8_t facing;                   // top bits of bytes 2/3 -> wC6EE
};

MapHeader loadMapHeader(const Rom& rom, int map);
std::vector<MapObject> loadMapObjects(const Rom& rom, int map);
std::vector<MapExit> loadMapExits(const Rom& rom, int map);
// 06:5ACC[blockset]: attribute per block (& $3F).
std::array<uint8_t, 64> loadBlockAttrs(const Rom& rom, const GfxSet& g);
// 06:4000[blockset]: 4 BG tiles per block (& $3F), TL BL TR BR.
std::array<std::array<uint8_t, 4>, 64> loadBlockDefs(const Rom& rom, const GfxSet& g);
// OBJ VRAM $8000-$87FF as the field loader leaves it (07:7f00, 00:0438, 00:04b1).
std::array<std::array<uint8_t, 16>, 128> loadObjTiles(const Rom& rom, int spriteset, int playerPage = 0x70);
GfxSet loadGfxSet(const Rom& rom, int idx);
Layout loadLayout(const Rom& rom, int layout);

// 256 BG tiles (2bpp, 16 bytes each) as the game has them in VRAM for this gfx set
// (tiles 00-7F from the gfx set pages, 80-FF shared tiles from 03:4000).
std::array<std::array<uint8_t, 16>, 256> loadBgTiles(const Rom& rom, const GfxSet& g);

} // namespace ffl3
