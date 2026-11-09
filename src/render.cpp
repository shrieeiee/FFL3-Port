#include "render.h"

namespace ffl3 {

namespace {
void decode(const uint8_t* t16, std::array<uint8_t, 64>& out) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            out[y * 8 + x] = uint8_t(((t16[y * 2] >> (7 - x)) & 1) | (((t16[y * 2 + 1] >> (7 - x)) & 1) << 1));
}
int wrap(int v, int n) { return ((v % n) + n) % n; }
}

void TileCache::build(const Rom& rom, const Field& f) {
    const auto bgt = loadBgTiles(rom, f.gfx());
    for (int i = 0; i < 256; i++) decode(bgt[i].data(), bg[i]);
    for (int i = 0; i < 128; i++) decode(f.objTiles()[i].data(), obj[i]);
    map = f.mapId();
}

// Sprite frames in a 16-tile page: +0 front, +4 back, +8 side (faces left), +12 side step.
// Tiles within a frame are column-major: TL, BL, TR, BR (as in OAM at 01:5cd3).
static void drawSprite(const TileCache& tc, uint8_t obp, Frame& out, int sx, int sy, int base, bool flipX) {
    for (int k = 0; k < 4; k++) {
        const int tx = (k >> 1) ^ (flipX ? 1 : 0), ty = k & 1;
        const auto& t = tc.obj[(base + k) & 0x7f];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const uint8_t c = t[y * 8 + (flipX ? 7 - x : x)];
                if (!c) continue;
                const int X = sx + tx * 8 + x, Y = sy + ty * 8 + y;
                if (X < 0 || Y < 0 || X >= out.w || Y >= out.h) continue;
                out.px[size_t(Y) * out.w + X] = uint8_t((obp >> (c * 2)) & 3);
            }
    }
}

static void frameFor(const Actor& m, int baseTile, int& tile, bool& flip) {
    tile = baseTile; flip = false;
    const bool step = m.moveFrames > 0 && ((m.animTick >> 3) & 1);
    switch (m.facing) {
    case Dir::Down:  tile = baseTile + 0; flip = step; break;
    case Dir::Up:    tile = baseTile + 4; flip = step; break;
    case Dir::Left:  tile = baseTile + (step ? 12 : 8); flip = false; break;
    case Dir::Right: tile = baseTile + (step ? 12 : 8); flip = true; break;
    }
}

void renderField(const Field& f, const TileCache& tc, const RenderOpts& o, Frame& out,
                 std::vector<uint8_t>* debugTint) {
    const Actor& p = f.player();
    const int mapW = f.cols() * 16, mapH = f.rows() * 16;
    // Camera: the game puts the leader at scroll + $50 and the screen at scroll + (16, 8),
    // i.e. the leader's body at (72, 64) on a 160x144 screen. Keep that centring for any width.
    const int pyPix = p.row * 16 + p.subY(), pxPix = p.col * 16 + p.subX();
    const int camY = pyPix - (out.h / 2 - 8), camX = pxPix - (out.w / 2 - 8);
    if (debugTint) debugTint->assign(out.px.size(), 0);

    for (int y = 0; y < out.h; y++) {
        const int my = wrap(camY + y, mapH);
        const int brow = my >> 4, sub = (my >> 3) & 1, ty = my & 7;
        for (int x = 0; x < out.w; x++) {
            const int mx = wrap(camX + x, mapW);
            const int bcol = mx >> 4;
            const uint8_t block = f.blockAt(brow, bcol);
            const auto& bt = f.drawnTiles(block);
            const uint8_t tile = bt[((mx >> 3) & 1) * 2 + sub];     // TL BL TR BR
            const uint8_t c = tc.bg[tile][ty * 8 + (mx & 7)];
            out.px[size_t(y) * out.w + x] = uint8_t((o.bgp >> (c * 2)) & 3);
            if (debugTint) {
                const uint8_t a = f.attrAt(brow, bcol);
                (*debugTint)[size_t(y) * out.w + x] =
                    (a & 0xc0) == 0xc0 ? 3 : f.passable(brow, bcol) ? 1 : 2;
            }
        }
    }
    auto screenPos = [&](const Actor& m, int& sx, int& sy) {
        int dy = m.row * 16 + m.subY() - camY, dx = m.col * 16 + m.subX() - camX;
        dy = wrap(dy + mapH / 2, mapH) - mapH / 2;   // nearest wrapped copy
        dx = wrap(dx + mapW / 2, mapW) - mapW / 2;
        sx = dx; sy = dy;
    };
    for (int i = 1; i < Field::kSlots; i++) {
        const Actor& n = f.actors()[size_t(i)];
        if (!n.drawn()) continue;
        int sx, sy, tile; bool flip;
        screenPos(n, sx, sy);
        frameFor(n, n.spriteTile, tile, flip);
        drawSprite(tc, o.obp, out, sx, sy, tile, flip);
    }
    int sx, sy, tile; bool flip;
    screenPos(p, sx, sy);
    frameFor(p, 0, tile, flip);
    drawSprite(tc, o.obp, out, sx, sy, tile, flip);
}

static void drawBgTile(const TileCache& tc, uint8_t bgp, Frame& out, int sx, int sy, uint8_t tile) {
    const auto& t = tc.bg[tile];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const int X = sx + x, Y = sy + y;
            if (X < 0 || Y < 0 || X >= out.w || Y >= out.h) continue;
            out.px[size_t(Y) * out.w + X] = uint8_t((bgp >> (t[y * 8 + x] * 2)) & 3);
        }
}

static void drawFrame(const TileCache& tc, uint8_t bgp, Frame& out, int x0, int y0, int w, int h) {
    for (int ty = 0; ty < h; ty++)
        for (int tx = 0; tx < w; tx++) {
            uint8_t t = 0xff;
            const bool top = ty == 0, bot = ty == h - 1, left = tx == 0, right = tx == w - 1;
            if (top) t = left ? 0xf7 : right ? 0xf9 : 0xfc;
            else if (bot) t = left ? 0xf8 : right ? 0xfa : 0xfc;
            else if (left || right) t = 0xfb;
            drawBgTile(tc, bgp, out, x0 + tx * 8, y0 + ty * 8, t);
        }
}

void renderTextBox(const TextBox& tb, const TileCache& tc, const RenderOpts& o, Frame& out) {
    if (!tb.active()) return;
    const int x0 = (out.w - 160) / 2, y0 = out.h - 64;
    drawFrame(tc, o.bgp, out, x0, y0, 20, 8);
    for (int r = 0; r < TextBox::kRows; r++)
        for (int c = 0; c < TextBox::kCols; c++)
            drawBgTile(tc, o.bgp, out, x0 + 8 + c * 8, y0 + 16 + r * 16, tb.lines()[size_t(r)][size_t(c)]);
    if (tb.choosing()) {
        // Yes / No box above the window's right side, hand cursor (OBJ $7E) on the choice.
        const int bx = x0 + 160 - 7 * 8, by = y0 - 6 * 8;
        drawFrame(tc, o.bgp, out, bx, by, 7, 6);
        const uint8_t yes[] = {0xd2, 0xd8, 0xe6}, no[] = {0xc7, 0xe2};
        for (int i = 0; i < 3; i++) drawBgTile(tc, o.bgp, out, bx + 24 + i * 8, by + 16, yes[i]);
        for (int i = 0; i < 2; i++) drawBgTile(tc, o.bgp, out, bx + 24 + i * 8, by + 32, no[i]);
        const auto& t = tc.obj[0x7e];
        const int cy = by + 16 + tb.choice() * 16;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (t[y * 8 + x]) out.px[size_t(cy + y) * out.w + bx + 8 + x] = uint8_t((o.obp >> (t[y * 8 + x] * 2)) & 3);
    }
}

} // namespace ffl3
