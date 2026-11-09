#include "battledata.h"

namespace ffl3 {

void decodeTile(const uint8_t* t16, Tile& out) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            out[size_t(y * 8 + x)] = uint8_t(((t16[y * 2] >> (7 - x)) & 1) | (((t16[y * 2 + 1] >> (7 - x)) & 1) << 1));
}

Glyphs asciiGlyphs(const char* s) {
    Glyphs g;
    for (; *s; s++) {
        const char c = *s;
        if (c >= 'A' && c <= 'Z') g.push_back(uint8_t(0xba + c - 'A'));
        else if (c >= 'a' && c <= 'z') g.push_back(uint8_t(0xd4 + c - 'a'));
        else if (c >= '0' && c <= '9') g.push_back(uint8_t(0xb0 + c - '0'));
        else switch (c) {
            case '.': g.push_back(0xaf); break;
            case ',': g.push_back(0xac); break;
            case '\'': g.push_back(0xad); break;
            case '!': g.push_back(0xf3); break;
            case '?': g.push_back(0xf4); break;
            case '-': g.push_back(0xf2); break;
            case '/': g.push_back(0xf1); break;
            case ':': g.push_back(0xf0); break;
            default: g.push_back(0xff); break;
        }
    }
    return g;
}

static Glyphs trimmed(const uint8_t* p, int n) {
    Glyphs g(p, p + n);
    while (!g.empty() && g.back() == 0xff) g.pop_back();
    return g;
}

Glyphs CharRecord::name() const { return trimmed(raw.data() + 7, 6); }

Glyphs itemName(const Rom& rom, int id) {
    if (id < 0x30) return trimmed(rom.raw(0xe, uint16_t(0x6d9b + id * 6)), 6);
    return trimmed(rom.raw(0xe, uint16_t(0x6ebb + (id - 0x30) * 8)), 8);
}

Glyphs talentName(const Rom& rom, int id) { return trimmed(rom.raw(9, uint16_t(0x67f7 + id * 8)), 8); }

// 0B:5BD8[class + 1]: picture size in tiles, (w-1) << 4 | (h-1).
static void picSize(const Rom& rom, int cls, int& w, int& h) {
    const uint8_t s = rom.b(0xb, uint16_t(0x5bd8 + cls + 1));
    w = (s >> 4) + 1;
    h = (s & 15) + 1;
}

Monster loadMonster(const Rom& rom, int id) {
    Monster m;
    m.id = id;
    m.name = trimmed(rom.raw(0xe, uint16_t(0x7533 + id * 8)), 8);
    const uint8_t* s = rom.raw(0xb, uint16_t(0x4a19 + id * 12));
    m.genus = s[0];
    m.hp = s[1] | (s[2] & 0x0f) << 8;
    m.mp = s[3] | (s[4] & 0x0f) << 8;
    m.atk = s[5]; m.def = s[6]; m.mag = s[7]; m.agi = s[8]; m.eva = s[9]; m.mdef = s[10]; m.mev = s[11];
    const uint8_t* d = rom.raw(0xb, uint16_t(0x470d + id * 4));
    m.element = d[0] >> 4; m.level = d[0] & 15; m.xp = d[1]; m.gp = d[2]; m.drop = d[3];
    const uint8_t* t = rom.raw(0xb, uint16_t(0x533d + id * 9));
    for (int i = 0; i < 8; i++)
        if (t[0] & (0x80 >> i) && t[1 + i] != 0xff) m.talents.push_back(t[1 + i]);

    // Picture: sprite byte 0B:4301[m] = meta << 3 | variant. Meta 0B:43CC = bank << 4 | class.
    const uint8_t spr = rom.b(0xb, uint16_t(0x4301 + id));
    const int meta = spr >> 3, variant = spr & 7;
    const uint8_t mt = rom.b(0xb, uint16_t(0x43cc + meta));
    m.sizeClass = mt & 15;
    const int bank = mt >> 4;
    picSize(rom, m.sizeClass, m.picW, m.picH);
    const int count = rom.b(0xb, uint16_t(0x43c4 + m.sizeClass));
    const uint16_t p0 = rom.w(0xb, uint16_t(0x43e4 + meta * 4)), p1 = rom.w(0xb, uint16_t(0x43e6 + meta * 4));
    const uint16_t p2 = rom.w(0xb, uint16_t(0x43e8 + meta * 4));
    const std::vector<uint8_t> a = rom.decompress(bank, p0, p1), b = rom.decompress(bank, p1, p2);
    m.tiles.resize(size_t(count));
    for (int i = 0; i < count; i++) {
        uint8_t raw[16] = {};
        for (int k = 0; k < 8; k++) {
            const size_t o = size_t((variant * count + i) * 8 + k);
            raw[k * 2] = o < a.size() ? a[o] : 0;
            raw[k * 2 + 1] = o < b.size() ? b[o] : 0;
        }
        decodeTile(raw, m.tiles[size_t(i)]);
    }
    // Tiles fill the picture row by row; the big classes leave corners empty
    // (class 4: the two top corners, class 5: all four). Seen in VRAM.
    m.cell.assign(size_t(m.picW * m.picH), -1);
    int next = 0;
    for (int r = 0; r < m.picH; r++)
        for (int c = 0; c < m.picW; c++) {
            const bool edgeC = c == 0 || c == m.picW - 1;
            if (m.sizeClass == 4 && r == 0 && edgeC) continue;
            if (m.sizeClass == 5 && (r == 0 || r == m.picH - 1) && edgeC) continue;
            if (next < count) m.cell[size_t(r * m.picW + c)] = next++;
        }
    return m;
}

Formation loadFormation(const Rom& rom, int n) {
    Formation f;
    f.index = n;
    const uint8_t* p = rom.raw(0xb, uint16_t(0x758b + n * 5));
    for (int i = 0; i < 3; i++) f.ids[i] = p[i];
    f.flags = p[3];
    f.counts = p[4];
    return f;
}

CharRecord loadCharacter(const Rom& rom, int i) {
    CharRecord c;
    const uint8_t* p = rom.raw(7, uint16_t(0x7a81 + i * 0x60));
    std::copy(p, p + 0x60, c.raw.begin());
    return c;
}

EnemyLayout layoutEnemies(const Rom& rom, const Formation& f, bool fixed, const std::function<int(int)>& rnd) {
    EnemyLayout L;
    int cls1[3], w[3], h[3];                         // wCC30 (class + 1), wCC3B (w, h)
    for (int g = 0; g < 3; g++) {
        cls1[g] = f.ids[g] == 0xff ? 0 : (rom.b(0xb, uint16_t(0x43cc + (rom.b(0xb, uint16_t(0x4301 + f.ids[g])) >> 3))) & 15) + 1;
        const uint8_t s = rom.b(0xb, uint16_t(0x5bd8 + cls1[g]));
        w[g] = (s >> 4) + 1;
        h[g] = (s & 15) + 1;
    }
    if (fixed) {                                     // 0B:5a7e
        uint8_t b = f.counts & 0x3f;
        for (int g = 0; g < 3; g++) {
            L.units[g] = f.ids[g] == 0xff ? 0 : ((b >> 4) & 3) + 1;
            b = uint8_t(b << 2);
        }
    } else {                                         // 0B:5a9a
        L.units[0] = rnd(((f.counts & 0xc0) >> 6) + 1) + 1;
        int room = 20 - w[0] * L.units[0];
        if ((cls1[0] == 1 || cls1[0] == 3) && (cls1[1] == 1 || cls1[1] == 3)) room -= 2;
        const int fit = room > 0 ? room / w[1] : 0;  // wCC3A
        if (f.ids[1] != 0xff && fit > 0) {
            const int total = rnd(fit + 1);
            if (total > 0) {
                L.units[1] = rnd(total) + 1;
                if (f.ids[2] != 0xff) L.units[2] = total - L.units[1];
            }
        }
    }
    int width = 0;
    for (int g = 0; g < 3; g++) width += w[g] * L.units[g];
    int col = (20 - width) / 2;                      // 0B:5b33
    for (int g = 0; g < 3; g++) {
        const bool stacked = cls1[g] && cls1[g] < 3;  // 4-tall pictures go two to a column
        L.count[g] = L.units[g] * (stacked ? 2 : 1);
        for (int u = 0; u < L.units[g]; u++) {
            L.slots.push_back({g, col, 0});
            if (stacked) L.slots.push_back({g, col, 4});
            col += w[g];
        }
        (void)h;
    }
    return L;
}

int randomFormation(const Rom& rom, int area, int vehicle, int r) {
    int pack = area;                                 // 00:1186
    if (vehicle == 1 || vehicle >= 4) pack += 1;
    else if (vehicle == 2) pack += 2;
    int c = 0;
    while (c < 6 && r < rom.b(3, uint16_t(0x76bc + c))) c++;   // thresholds 03:76BC
    return rom.b(3, uint16_t(0x76c2 + pack * 7 + c));
}

int encounterRate(const Rom& rom, int musicHi) { return rom.b(0, uint16_t(0x119a + (musicHi & 7))); }

} // namespace ffl3
