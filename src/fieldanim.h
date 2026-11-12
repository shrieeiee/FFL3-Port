// Field tile animation: the per-map hook 01:54A0 (water flowing, waterfalls, flickering
// lights...), hand-translated. It works on a copy of VRAM $8000-$97FF.
#pragma once
#include <cstdint>

namespace ffl3 {

// hook = map header byte 3 (wCA18), t = the frame counter hFF93.
void runMapHook(int hook, uint8_t t, uint8_t* vram8000);

} // namespace ffl3
