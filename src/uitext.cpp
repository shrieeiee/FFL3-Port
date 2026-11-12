#include "uitext.h"
#include "rom.h"

namespace ffl3 {

void uiBox(UiScreen& s, int x, int y, int w, int h) {
    for (int r = y - 1; r <= y + h; r++)
        for (int c = x - 1; c <= x + w; c++) {
            if (r < 0 || r >= 18 || c < 0 || c >= 20) continue;
            uint8_t t = 0xff;
            const bool top = r == y - 1, bot = r == y + h, l = c == x - 1, rt = c == x + w;
            if (top) t = l ? 0xf7 : rt ? 0xf9 : 0xfc;
            else if (bot) t = l ? 0xf8 : rt ? 0xfa : 0xfc;
            else if (l || rt) t = 0xfb;
            s[size_t(r * 20 + c)] = t;
        }
}

void uiPut(UiScreen& s, int col, int row, const Glyphs& g) {
    for (size_t i = 0; i < g.size(); i++) {
        const int c = col + int(i);
        if (c >= 0 && c < 20 && row >= 0 && row < 18) s[size_t(row * 20 + c)] = g[i];
    }
}

Glyphs uiNumber(int n) {
    Glyphs g;
    if (n < 0) n = 0;
    do { g.insert(g.begin(), uint8_t(0xb0 + n % 10)); n /= 10; } while (n);
    return g;
}

void uiNum(UiScreen& s, int endCol, int row, int n) {
    const Glyphs g = uiNumber(n);
    uiPut(s, endCol - int(g.size()) + 1, row, g);
}

Glyphs uiTrim(Glyphs g) {
    while (!g.empty() && g.back() == 0xff) g.pop_back();
    return g;
}

Glyphs uiText(const Rom& rom, uint16_t addr,
              const std::function<void(uint8_t, uint8_t, Glyphs&)>& special) {
    static const uint8_t kLow[] = {0x01, 0x02, 0x03, 0x04, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x40,
                                   0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x0e, 0x0f};
    static std::vector<Glyphs> dict;
    if (dict.empty()) {                                       // 03:71C5: 80 length-prefixed words
        uint16_t a = 0x71c5;
        for (int i = 0; i < 80; i++) {
            const int n = rom.b(3, a++);
            Glyphs e;
            for (int k = 0; k < n; k++) e.push_back(rom.b(3, a++));
            dict.push_back(e);
        }
    }
    Glyphs out;
    uint16_t p = addr;
    std::function<void(uint8_t)> code = [&](uint8_t c) {
        if (c >= 0x88) { out.push_back(c); return; }
        if (c >= 0x4e) { for (uint8_t x : dict[size_t(c - 0x4e)]) code(x); return; }
        for (size_t i = 0; i < sizeof kLow; i++)
            if (kLow[i] == c) { for (uint8_t x : dict[58 + i]) code(x); return; }
        switch (c) {
        case 0x06: out.push_back(0x06); break;
        case 0x09: { const int n = rom.b(9, p++); for (int i = 0; i < n; i++) out.push_back(0xff); break; }
        case 0x05: case 0x1c: case 0x4c: if (special) special(c, 0, out); break;
        case 0x43: p = uint16_t(p + 3); break;
        default: { const uint8_t a = rom.b(9, p++); if (special) special(c, a, out); break; }
        }
    };
    for (int guard = 0; guard < 600; guard++) {
        const uint8_t c = rom.b(9, p++);
        if (c == 0x00) break;
        code(c);
    }
    return out;
}

int spritePage(const Rom& rom, const CharRecord& r) {
    const uint8_t d = r.raw[0x0d];
    const uint8_t id = (d & 7) == 2 ? uint8_t(((d >> 3) & 0x0f) + 0xf0) : r.raw[0x0e];
    return 0x40 + (rom.b(0x0b, uint16_t(0x748f + id)) & 0x3f);
}

UiSprite memberSprite(const Rom& rom, const CharRecord& r, int x, int y) {
    UiSprite s{x, y, {}};
    const int page = spritePage(rom, r);
    for (int k = 0; k < 4; k++) decodeTile(rom.raw(4, uint16_t(page << 8 | k * 16)), s.tiles[size_t(k)]);
    return s;
}

} // namespace ffl3
