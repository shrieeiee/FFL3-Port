// FFL3 / SaGa 3 PC port - helpers for the menu-style windows (shop, inn, field menu).
// Windows are drawn like the game's (00:1E49 from the list at wC8B0): a frame of tiles
// $F7 $FC $F9 / $FB / $F8 $FC $FA around an inner area, later windows over earlier ones.
#pragma once
#include "battledata.h"
#include <functional>

namespace ffl3 {

using UiScreen = std::array<uint8_t, 20 * 18>;

struct UiSprite { int x, y; std::array<Tile, 4> tiles; };   // 16x16, tiles TL BL TR BR

void uiBox(UiScreen& s, int x, int y, int w, int h);          // inner area at (x, y), w x h
void uiPut(UiScreen& s, int col, int row, const Glyphs& g);
void uiNum(UiScreen& s, int endCol, int row, int n);          // right-aligned, last digit at endCol
// Static text from bank 9 (dictionary words expanded, {09 n} = n spaces); codes with
// arguments are handed to `special` (code, argument) or skipped. $06 = line break.
Glyphs uiText(const Rom& rom, uint16_t addr,
              const std::function<void(uint8_t code, uint8_t arg, Glyphs& out)>& special = {});
Glyphs uiTrim(Glyphs g);                                      // drop trailing spaces
Glyphs uiNumber(int n);
// 00:04DD: the member's sprite page in bank 4 (0B:748F[id] & $3F) + $40
int spritePage(const Rom& rom, const CharRecord& r);
UiSprite memberSprite(const Rom& rom, const CharRecord& r, int x, int y);

} // namespace ffl3
