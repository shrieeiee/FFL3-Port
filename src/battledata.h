// Battle data read from the ROM: formations, monsters (stats, names, pictures), party records,
// item / talent names, random-encounter tables. Addresses in docs/RE_NOTES.md ("Battles").
#pragma once
#include "rom.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace ffl3 {

using Glyphs = std::vector<uint8_t>;          // tile numbers in the font ($B0-$FF etc.)
using Tile = std::array<uint8_t, 64>;         // 8x8 colour indices 0-3

void decodeTile(const uint8_t* t16, Tile& out);      // GB 2bpp, 16 bytes
Glyphs asciiGlyphs(const char* s);                   // A-Z a-z 0-9 space . , ! ? - ' / :

struct Monster {
    int id = 0;
    Glyphs name;                         // 0E:7533 + m*8
    int genus = 0, hp = 0, mp = 0;       // 0B:4A19 + m*12
    int atk = 0, def = 0, mag = 0, agi = 0, eva = 0, mdef = 0, mev = 0;
    int element = 0, level = 0, xp = 0, gp = 0, drop = 0xff;   // 0B:470D + m*4
    std::vector<uint8_t> talents;        // 0B:533D + m*9 (byte 0 = mask, then ids)
    int sizeClass = 0, picW = 4, picH = 4;
    std::vector<Tile> tiles;             // decoded picture tiles (0B:43C4[class] of them)
    std::vector<int> cell;               // picW*picH: tile index, or -1 for an empty corner
};

// 0B:758B + n*5: three monster ids ($FF = none), flags, counts.
struct Formation {
    int index = 0;
    int ids[3] = {0xff, 0xff, 0xff};
    uint8_t flags = 0;                   // $40 = can't run, $30 = music
    uint8_t counts = 0;                  // fixed: 2 bits per group; random: $C0 = group 0 spread
    bool canRun() const { return !(flags & 0x40); }
    int music() const { return ((flags >> 4) & 3) + 1; }
};

// Where each monster's picture goes on the BG (tile col/row), as built by 0B:5a18.
struct EnemySlot { int group, col, row; };
struct EnemyLayout {
    int units[3] = {0, 0, 0};            // wCC33: columns per group
    int count[3] = {0, 0, 0};            // wCC36: monsters per group
    std::vector<EnemySlot> slots;
};

// Character record: wC500 + i*$60 in RAM, 07:7A81 + i*$60 in the ROM (initial state).
struct CharRecord {
    std::array<uint8_t, 0x60> raw{};
    int status() const { return raw[0]; }
    Glyphs name() const;
    int hp() const { return raw[0x10] | raw[0x11] << 8; }
    int maxHp() const { return raw[0x12] | raw[0x13] << 8; }
    int mp() const { return raw[0x14] | raw[0x15] << 8; }
    int maxMp() const { return raw[0x16] | raw[0x17] << 8; }
    int str() const { return raw[0x18]; }
    int def() const { return raw[0x19]; }
    int mag() const { return raw[0x1a]; }
    int agi() const { return raw[0x1b]; }
    void setHp(int v) { raw[0x10] = uint8_t(v); raw[0x11] = uint8_t(v >> 8); }
    void setMp(int v) { raw[0x14] = uint8_t(v); raw[0x15] = uint8_t(v >> 8); }
    uint8_t item(int slot) const { return raw[0x40 + slot]; }   // 16 slots, $FF = empty
};

Monster loadMonster(const Rom& rom, int id);
Formation loadFormation(const Rom& rom, int n);
CharRecord loadCharacter(const Rom& rom, int i);
Glyphs itemName(const Rom& rom, int id);             // < $30: 0E:6D9B x6, else 0E:6EBB x8
Glyphs talentName(const Rom& rom, int id);           // 09:67F7 x8

// 0B:5a18: monster counts and picture positions. rnd(n) = 00:1d2e, a random number 0..n-1.
// fixed = hFFCA bit 7 (counts come from the formation's count bits).
EnemyLayout layoutEnemies(const Rom& rom, const Formation& f, bool fixed, const std::function<int(int)>& rnd);

// 00:10f5: random encounter. area = map header byte 5 (wCA2D), vehicle = wC6E3, r = random byte.
// Returns the hFFCA value: formation index | $80 when the counts are fixed.
int randomFormation(const Rom& rom, int area, int vehicle, int r);

// The picture loader decompresses plane B of each group's picture into wD400; the monster
// record builder never writes wD684 (record +4), so it keeps byte $284 of the last plane.
uint8_t pictureResidue(const Rom& rom, const int ids[3], uint8_t before = 0xff);

// 00:119A[(header byte 4) >> 5]: chance (out of 256) of a battle after each step.
int encounterRate(const Rom& rom, int musicHi);

} // namespace ffl3
