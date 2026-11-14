#include <algorithm>
#include "render.h"
#include "fieldanim.h"
#include "console.h"
#include "opening.h"
#include "uitext.h"

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
    for (int i = 0; i < 128; i++) std::copy(f.objTiles()[i].begin(), f.objTiles()[i].end(), vram.begin() + i * 16);
    for (int i = 0; i < 256; i++)                 // BG tile numbers are signed: 0-127 at $9000
        std::copy(bgt[i].begin(), bgt[i].end(), vram.begin() + (i < 128 ? 0x1000 + i * 16 : 0x800 + (i - 128) * 16));
    decodeAll();
    map = f.mapId();
}

void TileCache::decodeAll() {
    for (int i = 0; i < 256; i++) decode(&vram[size_t(i < 128 ? 0x1000 + i * 16 : 0x800 + (i - 128) * 16)], bg[size_t(i)]);
    for (int i = 0; i < 128; i++) decode(&vram[size_t(i * 16)], obj[size_t(i)]);
}

void TileCache::animate(int hook, uint8_t frame) {
    if (!hook || hook == 0x0c) return;
    runMapHook(hook, frame, vram.data());
    decodeAll();
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
    static std::vector<uint8_t> bgIdx;                 // BG colour index per pixel (OBJ priority)
    bgIdx.assign(out.px.size(), 0);

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
            bgIdx[size_t(y) * out.w + x] = c;
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
    // 00:3A7F: objects with wC360 bit 2 (bit 6 of their column byte) are drawn from the counted
    // layouts: list 01:6ABE/6AFE/6B3E/6B7E[facing][move type] -> frame -> count, then
    // (dy, dx, t): tile = base + (t & $0F) ($6F / $8F = tile $6F), t's top bits = OAM flags.
    auto drawLayout = [&](const Actor& n, int sx, int sy) {
        const Rom& rom = f.rom();
        static const uint16_t tables[4] = {0x6abe, 0x6afe, 0x6b7e, 0x6b3e};   // down up left right
        const uint16_t list = rom.w(1, uint16_t(tables[int(n.facing) & 3] + (n.obj.move & 0x1f) * 2));
        uint16_t p = rom.w(1, list);
        const int cnt = rom.b(1, p++);
        for (int k = 0; k < cnt; k++, p = uint16_t(p + 3)) {
            const int dy = int8_t(rom.b(1, p)), dx = int8_t(rom.b(1, uint16_t(p + 1)));
            const uint8_t t = rom.b(1, uint16_t(p + 2));
            const int tile = (t == 0x6f || t == 0x8f) ? 0x6f : (n.spriteTile + (t & 0x0f)) & 0x7f;
            const uint8_t fl = (t == 0x6f) ? 0 : t;
            const auto& tl = tc.obj[size_t(tile)];
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    const uint8_t c = tl[size_t(((fl & 0x40) ? 7 - y : y) * 8 + ((fl & 0x20) ? 7 - x : x))];
                    if (!c) continue;
                    const int X = sx + dx + x, Y = sy + dy + y;
                    if (X < 0 || Y < 0 || X >= out.w || Y >= out.h) continue;
                    const size_t at = size_t(Y) * out.w + X;
                    if ((fl & 0x80) && bgIdx[at]) continue;
                    out.px[at] = uint8_t((o.obp >> (c * 2)) & 3);
                }
        }
    };
    for (int i = 1; i < Field::kSlots; i++) {
        const Actor& n = f.actors()[size_t(i)];
        if (!n.drawn()) continue;
        int sx, sy, tile; bool flip;
        screenPos(n, sx, sy);
        if (n.obj.flagsX & 0x40) { drawLayout(n, sx, sy); continue; }
        frameFor(n, n.spriteTile, tile, flip);
        drawSprite(tc, o.obp, out, sx, sy, tile, flip);
    }
    if (!p.id) return;                                // hidden (event $12 $00, actor command 4x)
    int sx, sy, tile; bool flip;
    screenPos(p, sx, sy);
    if (const int lift = f.leaderLift()) {            // events $12 $08: OAM Y wraps at 256
        const int top = out.h / 2 - 72;               // where the GB screen's row 0 sits
        sy = (((sy - lift - top + 16) & 255) - 16) + top;
    }
    frameFor(p, p.spriteTile, tile, flip);
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

template <class T>
static void renderOverlay(const T& w, const TileCache& tc, const RenderOpts& o, Frame& out) {
    const int x0 = (out.w - 160) / 2;
    const auto s = w.screen();
    for (int r = 0; r < 18; r++)
        for (int c = 0; c < 20; c++)
            if (s[size_t(r * 20 + c)]) drawBgTile(tc, o.bgp, out, x0 + c * 8, r * 8, s[size_t(r * 20 + c)]);
    for (const UiSprite& sp : w.sprites())
        for (int k = 0; k < 4; k++) {
            const auto& t = sp.tiles[size_t(k)];
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    const int X = x0 + sp.x + (k >> 1) * 8 + x, Y = sp.y + (k & 1) * 8 + y;
                    if (t[size_t(y * 8 + x)] && X >= 0 && X < out.w && Y >= 0 && Y < out.h)
                        out.px[size_t(Y) * size_t(out.w) + size_t(X)] = uint8_t((o.obp >> (t[size_t(y * 8 + x)] * 2)) & 3);
                }
        }
    const int cr = w.cursorRow();
    if (cr < 0) return;
    const auto& t = tc.obj[0x7e];
    const int cx = x0 + 8 * w.cursorCol() - 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (t[size_t(y * 8 + x)]) out.px[size_t(cr * 8 + y) * size_t(out.w) + size_t(cx + x)] = uint8_t((o.obp >> (t[size_t(y * 8 + x)] * 2)) & 3);
}
void renderShop(const Shop& m, const TileCache& tc, const RenderOpts& o, Frame& out) { renderOverlay(m, tc, o, out); }
void renderMenu(const FieldMenu& m, const TileCache& tc, const RenderOpts& o, Frame& out) { renderOverlay(m, tc, o, out); }
void renderConsole(const TalonConsole& m, const TileCache& tc, const RenderOpts& o, Frame& out) { renderOverlay(m, tc, o, out); }
void renderNaming(const NamingScreen& m, const TileCache& tc, const RenderOpts& o, Frame& out) {
    std::fill(out.px.begin(), out.px.end(), uint8_t(o.bgp & 3));
    renderOverlay(m, tc, o, out);
}
void renderTitle(const TitleScreen& t, const TileCache& tc, const RenderOpts& o, Frame& out) {
    std::fill(out.px.begin(), out.px.end(), uint8_t(o.bgp & 3));
    const int x0 = (out.w - 160) / 2;
    const auto s = t.screen();
    for (int r = 0; r < 18; r++)
        for (int c = 0; c < 20; c++) {
            const uint8_t id = s[size_t(r * 20 + c)];
            if (id >= 0x80) { drawBgTile(tc, o.bgp, out, x0 + c * 8, r * 8, id); continue; }
            const Tile& tl = t.logoTiles()[id];
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    out.px[size_t(r * 8 + y) * size_t(out.w) + size_t(x0 + c * 8 + x)] = uint8_t((o.bgp >> (tl[size_t(y * 8 + x)] * 2)) & 3);
        }
    const Tile& reg = t.registered();                 // OAM y $53 x $52 -> (74, 67)
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (reg[size_t(y * 8 + x)]) out.px[size_t(67 + y) * size_t(out.w) + size_t(x0 + 74 + x)] = uint8_t((o.obp >> (reg[size_t(y * 8 + x)] * 2)) & 3);
    const auto& cur = tc.obj[0x7e];
    const int cx = x0 + 8 * t.cursorCol() - 8, cy = t.cursorRow() * 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (cur[size_t(y * 8 + x)]) out.px[size_t(cy + y) * size_t(out.w) + size_t(cx + x)] = uint8_t((o.obp >> (cur[size_t(y * 8 + x)] * 2)) & 3);
}

// ---- battle --------------------------------------------------------------------------------

void BattleGfx::build(const Rom& rom, const Battle& b) {
    for (int g = 0; g < 3; g++) monster[size_t(g)] = b.monsters()[size_t(g)].tiles;
    decodeTile(rom.raw(3, 0x5430), cursor);
    decodeTile(rom.raw(3, 0x57b0), bracket);
    member.clear();
    for (int m = 0; m < 5; m++) {
        std::array<Tile, 4> t;
        const int page = b.memberSprite(m);
        for (int k = 0; k < 4; k++) decodeTile(rom.raw(4, uint16_t(page << 8 | k * 16)), t[size_t(k)]);
        member.push_back(t);
    }
}

static void putTile(Frame& out, const Tile& t, uint8_t pal, int sx, int sy, bool obj, bool fx = false, bool fy = false) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const uint8_t c = t[size_t((fy ? 7 - y : y) * 8 + (fx ? 7 - x : x))];
            if (obj && !c) continue;
            const int X = sx + x, Y = sy + y;
            if (X < 0 || Y < 0 || X >= out.w || Y >= out.h) continue;
            out.px[size_t(Y) * out.w + X] = uint8_t((pal >> (c * 2)) & 3);
        }
}

void renderBattle(const Battle& b, const BattleGfx& g, const TileCache& tc, const RenderOpts& o, Frame& out) {
    (void)o;
    const int x0 = (out.w - 160) / 2;
    const BattleView v = b.view();
    if (v.blank) { std::fill(out.px.begin(), out.px.end(), uint8_t(0)); return; }
    const int wy = std::min(v.wy, 144);
    auto put = [&](int X, int Y, uint8_t pal, uint8_t c) {
        if (X < 0 || Y < 0 || X >= out.w || Y >= out.h) return;
        out.px[size_t(Y) * out.w + X] = uint8_t((pal >> (c * 2)) & 3);
    };
    // BG from the map at $9800, tiles 0-127 from $9000 (the pictures), 128+ the font.
    auto bgPixel = [&](uint8_t t, int px, int py) -> uint8_t {
        if (t >= 128 || !v.vram) return tc.bg[t][size_t(py * 8 + px)];
        const uint8_t* d = v.vram + 0x1000 + t * 16 + py * 2;
        return uint8_t(((d[0] >> (7 - px)) & 1) | (((d[1] >> (7 - px)) & 1) << 1));
    };
    for (int y = 0; y < (v.rawWindow ? out.h : std::min(wy, out.h)); y++) {
        const int by = (y + v.scy[size_t(y)]) & 255;
        const uint8_t* row = v.vram ? v.vram + 0x1800 + (by >> 3) * 32 : nullptr;
        for (int X = 0; X < out.w; X++) {
            const int bx = (X - x0 + v.scx[size_t(y)]) & 255;
            const uint8_t t = row ? row[bx >> 3] : 0xff;
            put(X, y, v.bgp[size_t(y)], bgPixel(t, bx & 7, by & 7));
        }
    }
    // the window: the GB's rules when an animation drives it, else the battle's own
    if (v.rawWindow) {
        int line = 0;
        bool on = false;
        for (int y = 0; y < out.h; y++) {
            if (!(v.lcdc[size_t(y)] & 0x20)) continue;
            if (!on && y >= v.wyReg[size_t(y)]) on = true;
            if (!on || v.wx[size_t(y)] > 166) continue;
            const uint8_t* row = v.vram + 0x1c00 + ((line >> 3) & 31) * 32;
            for (int X = x0 + v.wx[size_t(y)] - 7; X < x0 + 160; X++) {
                const int wxp = X - (x0 + v.wx[size_t(y)] - 7);
                if (X < x0) continue;
                put(X, y, v.bgp[size_t(y)], bgPixel(row[(wxp >> 3) & 31], wxp & 7, line & 7));
            }
            line++;
        }
    } else
    for (int y = wy; y < out.h; y += 8)
        for (int c = 0; c < 20; c++) {
            const int row = (y - wy) / 8;
            if (row >= 10) break;
            putTile(out, tc.bg[v.win[size_t(row * 20 + c)]], v.bgp[size_t(y)], x0 + c * 8, y, false);
        }
    // OBJs from the OAM shadow (8x16; lower entries in front)
    if (v.vram)
        for (int i = 39; i >= 0; i--) {
            const uint8_t* e = &v.oam[size_t(i * 4)];
            const int sy = e[0] - 16, sx = e[1] - 8;
            if (e[0] == 0 || e[0] >= 160 || e[1] == 0 || e[1] >= 168) continue;
            const uint8_t attr = e[3];
            const uint8_t* t = v.vram + (e[2] & 0xfe) * 16;
            for (int y = 0; y < 16; y++) {
                const int ty = (attr & 0x40) ? 15 - y : y;
                const uint8_t lo = t[ty * 2], hi = t[ty * 2 + 1];
                const int Y = sy + y;
                if (Y < 0 || Y >= 144) continue;
                const uint8_t pal = (attr & 0x10) ? v.obp1[size_t(Y)] : v.obp0[size_t(Y)];
                for (int x = 0; x < 8; x++) {
                    const int bit = (attr & 0x20) ? x : 7 - x;
                    const uint8_t c = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
                    if (c) put(x0 + sx + x, Y, pal, c);
                }
            }
        }
    // the menus' own sprites (cursor, brackets, status-panel portraits)
    for (const BattleObj& ob : v.objs) {
        if (ob.member >= 0) {
            const auto& t = g.member[size_t(ob.member)];
            for (int k = 0; k < 4; k++) putTile(out, t[size_t(k)], v.obp0[0], x0 + ob.x + (k >> 1) * 8, ob.y + (k & 1) * 8, true);
        } else {
            putTile(out, ob.tile == Battle::kCursor ? g.cursor : g.bracket, v.obp0[0], x0 + ob.x, ob.y, true, ob.flipX, ob.flipY);
        }
    }
}

// Tester notices: a white bar with a line of text across the top (wraps to two lines).
void renderToast(const TileCache& tc, const std::string& msg, Frame& out) {
    if (msg.empty()) return;
    const size_t cols = size_t(out.w / 8);
    std::vector<std::string> lines;                     // wrapped at spaces, at most 3 lines
    std::string cur, word;
    for (char ch : msg + " ") {
        if (ch != ' ') { word += ch; continue; }
        while (word.size() > cols) { if (!cur.empty()) { lines.push_back(cur); cur.clear(); } lines.push_back(word.substr(0, cols)); word.erase(0, cols); }
        if (!cur.empty() && cur.size() + 1 + word.size() > cols) { lines.push_back(cur); cur.clear(); }
        if (!word.empty()) cur += (cur.empty() ? "" : " ") + word;
        word.clear();
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() > 3) lines.resize(3);
    for (int y = 0; y < int(lines.size()) * 8 + 1 && y < out.h; y++)
        for (int x = 0; x < out.w; x++) out.px[size_t(y) * out.w + x] = 0;
    for (size_t l = 0; l < lines.size(); l++) {
        const Glyphs g = asciiGlyphs(lines[l].c_str());
        for (size_t i = 0; i < cols; i++) putTile(out, tc.bg[i < g.size() ? g[i] : 0xff], 0xd2, int(i) * 8, int(l) * 8, false);
    }
}

// Tester menus: a white screen of text lines (each up to the screen width). A line starting
// with \x01 is drawn inverted (the selected one).
void renderLines(const TileCache& tc, const std::vector<std::string>& lines, Frame& out) {
    std::fill(out.px.begin(), out.px.end(), uint8_t(0));
    const int cols = out.w / 8;
    for (size_t l = 0; l < lines.size() && int(l) * 8 < out.h; l++) {
        const bool inv = !lines[l].empty() && lines[l][0] == '\x01';
        const Glyphs g = asciiGlyphs(lines[l].substr(inv ? 1 : 0, size_t(cols)).c_str());
        for (size_t i = 0; i < size_t(cols); i++)
            putTile(out, tc.bg[i < g.size() ? g[i] : 0xff], inv ? 0x1f : 0xd2, int(i) * 8, int(l) * 8, false);
    }
}

} // namespace ffl3
