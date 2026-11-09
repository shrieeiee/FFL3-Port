// CPU compositor: draws the field into a shade-index framebuffer (0-3 = GB shades),
// exactly like the LCD would, at GB size or a wider view.
#pragma once
#include "field.h"

namespace ffl3 {

struct Frame {
    int w = 160, h = 144;
    std::vector<uint8_t> px;        // shade 0 (white) .. 3 (black)
    void resize(int W, int H) { w = W; h = H; px.assign(size_t(W) * H, 0); }
};

struct RenderOpts {
    uint8_t bgp = 0xd2, obp = 0xd0;     // field palettes from the game
    bool showAttrs = false;             // debug: tint by collision
};

// Decoded tile cache (2bpp -> 64 colour indices) for the current map.
struct TileCache {
    std::array<std::array<uint8_t, 64>, 256> bg{};
    std::array<std::array<uint8_t, 64>, 128> obj{};
    int map = -1;
    void build(const Rom& rom, const Field& f);
};

void renderField(const Field& f, const TileCache& tc, const RenderOpts& o, Frame& out,
                 std::vector<uint8_t>* debugTint = nullptr);

} // namespace ffl3
