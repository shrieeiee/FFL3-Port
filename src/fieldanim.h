// Field tile animation: the per-map hook 01:54A0 (water flowing, waterfalls, flickering
// lights...), hand-translated. It works on a copy of VRAM $8000-$97FF.
#pragma once
#include <cstdint>

namespace ffl3 {

// hook = map header byte 3 (wCA18), t = the frame counter hFF93.
void runMapHook(int hook, uint8_t t, uint8_t* vram8000);

// 01:5501, right after a map loads (00:0542): on maps $0A / $0B the 16x16 picture at $9060
// (tiles $06 $07 $16 $17) is turned a quarter at a time into $9460, $94A0, $94C0 and $94E0,
// the frames hook $0F cycles through (the Talon's swirl).
void mapLoadTiles(int map, uint8_t* vram8000);

} // namespace ffl3
