#include "mapdata.h"
#include <cstring>

namespace ffl3 {

MapHeader loadMapHeader(const Rom& rom, int map) {
    const uint16_t h = rom.ptr(6, 0x6c58, map);
    uint8_t r[6];
    for (int i = 0; i < 6; i++) r[i] = rom.b(6, uint16_t(h + i));
    MapHeader m;
    m.layout = r[0] & 0x7f;
    m.flag80 = (r[0] & 0x80) != 0;
    m.gfxset = r[1];
    m.unk1e = r[2];
    m.hook = r[3];
    m.music = r[4] & 0x1f;
    m.music_hi = r[4] >> 5;
    m.unk2d = r[5];
    return m;
}

GfxSet loadGfxSet(const Rom& rom, int idx) {
    const uint16_t p = rom.ptr(3, 0x731c, idx);
    uint8_t r[7];
    for (int i = 0; i < 7; i++) r[i] = rom.b(3, uint16_t(p + i));
    GfxSet g;
    g.hmask = uint8_t((r[0] & 0x0f) - 1);
    g.wmask = uint8_t((r[0] >> 4) - 1);
    for (int i = 0; i < 4; i++) g.pages[i] = r[1 + i];
    g.tileattr = r[5];
    g.unk = r[6];
    return g;
}

Layout loadLayout(const Rom& rom, int L) {
    Layout out;
    if (L <= 0 || L >= 0x20) return out;
    const int bank = L >= 0x10 ? 8 : 7;
    const int idx = bank == 8 ? L - 0x10 : L - 1;
    const uint16_t start = rom.ptr(bank, 0x4000, idx);
    const uint16_t end = rom.ptr(bank, 0x4000, idx + 1);
    if (start < 0x4000 || end <= start || end >= 0x8000) return out;

    const std::vector<uint8_t> cells = rom.decompress(bank, start, end);
    const int stride = rom.b(7, uint16_t(0x7ea0 + L));        // tilemap row bytes
    const int count = rom.w(7, uint16_t(0x7ec0 + L * 2)) >> 2;   // metatiles
    const uint16_t mt = rom.ptr(bank, 0x4022, idx);
    if (stride < 2 || count <= 0 || int(cells.size()) < count) return out;

    out.bank = bank;
    out.w = stride / 2;
    out.h = count / out.w;
    out.cells.assign(cells.begin(), cells.begin() + count);
    const int tw = out.w * 2;
    out.tiles.assign(size_t(tw) * out.h * 2, 0);
    // 00:0f95 loop: metatile bytes are TL, BL, TR, BR
    for (int i = 0; i < out.w * out.h; i++) {
        const uint16_t o = uint16_t(mt + out.cells[i] * 4);
        const int x = (i % out.w) * 2, y = (i / out.w) * 2;
        out.tiles[y * tw + x] = rom.b(bank, o);
        out.tiles[(y + 1) * tw + x] = rom.b(bank, uint16_t(o + 1));
        out.tiles[y * tw + x + 1] = rom.b(bank, uint16_t(o + 2));
        out.tiles[(y + 1) * tw + x + 1] = rom.b(bank, uint16_t(o + 3));
    }
    out.valid = true;
    return out;
}

std::array<std::array<uint8_t, 16>, 256> loadBgTiles(const Rom& rom, const GfxSet& g) {
    std::array<std::array<uint8_t, 16>, 256> t{};
    // 07:7f4f (rst $18 $31 -> bank 3): 03:4000, $800 bytes -> $8800 = tiles $80-$FF
    for (int i = 0; i < 128; i++)
        std::memcpy(t[0x80 + i].data(), rom.raw(3, uint16_t(0x4000 + i * 16)), 16);
    // 07:7f5b: four $200-byte pages from bank 5 -> $9000 = tiles $00-$7F
    for (int p = 0; p < 4; p++)
        for (int i = 0; i < 32; i++)
            std::memcpy(t[p * 32 + i].data(), rom.raw(5, uint16_t((g.pages[p] << 8) + i * 16)), 16);
    return t;
}

} // namespace ffl3
