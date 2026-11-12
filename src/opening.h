// FFL3 / SaGa 3 PC port - title screen and naming screen (the parts of the opening that are
// not text scripts).
//   Title (01:7C79): logo = 128 tiles packed at 03:5C00-6287 (unpacked by 00:3D76), texts
//   09:7215 / 09:7227, cursor on New Game / Continue (starts on Continue).
//   New game (01:7D40): records 07:7A81 (x5), wC6E0 block 07:7E41, flags 01:7DDD; story crawl
//   09:5EAD; naming 0F:7A44 for members 0-3; record 3 -> record 4 (guest), character 2 into
//   slot 3 (0B:6DE4), slot 4 leaves (0B:6E33); then the prologue script 09:6015.
#pragma once
#include "battledata.h"
#include "uitext.h"
#include <array>
#include <vector>

namespace ffl3 {

class Rom;

struct OpeningInput { bool a = false, b = false, start = false, up = false, down = false, left = false, right = false; };

// 00:3D76: blocks of 4 << (n + 1) bytes (n = first byte): a fill byte, a bit mask (MSB first,
// 1 = literal byte follows), then the literals. Stops when the source reaches `end`.
std::vector<uint8_t> unpackMasked(const Rom& rom, int bank, uint16_t src, uint16_t end);

// 00:29C0: the class label of a record (09:78D7 + 00:29D7[(+$0D >> 3) & $0F], 7 glyphs, trimmed)
std::vector<uint8_t> classLabel(const Rom& rom, const CharRecord& r);

class TitleScreen {
public:
    explicit TitleScreen(const Rom& rom);
    void tick(const OpeningInput& in);
    int choice() const { return choice_; }        // 0 = still up, 1 = New Game, 2 = Continue
    std::array<uint8_t, 20 * 18> screen() const;  // tiles $00-$7F = logo (logoTiles())
    std::vector<UiSprite> sprites() const { return {}; }
    int cursorRow() const { return newGame_ ? 11 : 13; }   // OAM y $68 / $78
    int cursorCol() const { return 6; }
    const std::vector<Tile>& logoTiles() const { return logo_; }
    const Tile& registered() const { return reg_; }   // the (R) sprite: OBJ $18 from 03:71B5 at (74, 67)

private:
    const Rom& rom_;
    std::vector<Tile> logo_;
    Tile reg_{};
    bool newGame_ = false;
    int choice_ = 0;
};

// 0F:7A44: one member's name. The record being named is a fresh copy of the ROM record
// (0F:7B5D), shown with its class label (00:29C0) and face.
class NamingScreen {
public:
    NamingScreen(const Rom& rom, int member);
    void tick(const OpeningInput& in);
    bool done() const { return done_; }
    bool back() const { return back_; }           // B on an empty name: previous member
    const std::array<uint8_t, 6>& name() const { return name_; }
    std::array<uint8_t, 20 * 18> screen() const;
    std::vector<UiSprite> sprites() const;
    int cursorRow() const { return 6 + row_ * 2; }
    int cursorCol() const { return 2 + col_ * 2; }

private:
    const Rom& rom_;
    CharRecord rec_;
    std::array<uint8_t, 6> name_{};
    int len_ = 0, row_ = 0, col_ = 0;
    bool done_ = false, back_ = false;
    std::function<void(int)> sfx_;
public:
    std::function<void(int id)> playSfx;
};

} // namespace ffl3
