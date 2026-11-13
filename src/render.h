// CPU compositor: draws the field into a shade-index framebuffer (0-3 = GB shades),
// exactly like the LCD would, at GB size or a wider view.
#pragma once
#include "field.h"
#include "text.h"
#include "battle.h"
#include "shop.h"
#include "menu.h"

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
    std::array<uint8_t, 0x1800> vram{};       // $8000-$97FF as the game has it (animated)
    void build(const Rom& rom, const Field& f);
    void animate(int hook, uint8_t frame);   // 01:54A0, then re-decode the tiles
    void decodeAll();
};

// The dialogue window (WY=80, 20x8 tiles at $9C00): frame $F7/$FC/$F9/$FB/$F8/$FA,
// text lines on window rows 2, 4, 6. Centred horizontally in wide views.
void renderTextBox(const TextBox& tb, const TileCache& tc, const RenderOpts& o, Frame& out);

void renderToast(const TileCache& tc, const std::string& msg, Frame& out);
void renderLines(const TileCache& tc, const std::vector<std::string>& lines, Frame& out);

void renderField(const Field& f, const TileCache& tc, const RenderOpts& o, Frame& out,
                 std::vector<uint8_t>* debugTint = nullptr);

// Battle graphics: monster pictures per group, cursor / bracket OBJs, party sprite pages.
struct BattleGfx {
    std::array<std::vector<Tile>, 3> monster;
    Tile cursor{}, bracket{};
    std::vector<std::array<Tile, 4>> member;      // front frame of each member's field sprite
    void build(const Rom& rom, const Battle& b);
};

// Battle screen: pictures on BG rows 0-7 (blank tile $FF elsewhere), the window from WY down,
// then OBJs. Font / frame tiles come from the shared set at $8800 (tc.bg[$80-$FF]).
// Shop / inn window over the field.
void renderShop(const Shop& shop, const TileCache& tc, const RenderOpts& o, Frame& out);
void renderMenu(const FieldMenu& menu, const TileCache& tc, const RenderOpts& o, Frame& out);
class TalonConsole;
class TitleScreen;
class NamingScreen;
void renderTitle(const TitleScreen& t, const TileCache& tc, const RenderOpts& o, Frame& out);
void renderNaming(const NamingScreen& n, const TileCache& tc, const RenderOpts& o, Frame& out);
void renderConsole(const TalonConsole& c, const TileCache& tc, const RenderOpts& o, Frame& out);

void renderBattle(const Battle& b, const BattleGfx& g, const TileCache& tc, const RenderOpts& o, Frame& out);

} // namespace ffl3
