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
    g.blockset = r[5];
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
    out.w = stride / 2;               // 07:7EA0 is the wD000 row stride in blocks
    out.h = count / out.w;
    out.cells.assign(cells.begin(), cells.begin() + count);
    out.bw = out.w * 2;
    out.bh = out.h * 2;
    out.blocks.assign(size_t(out.bw) * out.bh, 0);
    // 00:0f95 loop: super-block bytes are TL, BL, TR, BR
    for (int i = 0; i < out.w * out.h; i++) {
        const uint16_t o = uint16_t(mt + out.cells[i] * 4);
        const int x = (i % out.w) * 2, y = (i / out.w) * 2;
        out.blocks[size_t(y) * out.bw + x] = rom.b(bank, o);
        out.blocks[size_t(y + 1) * out.bw + x] = rom.b(bank, uint16_t(o + 1));
        out.blocks[size_t(y) * out.bw + x + 1] = rom.b(bank, uint16_t(o + 2));
        out.blocks[size_t(y + 1) * out.bw + x + 1] = rom.b(bank, uint16_t(o + 3));
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

namespace ffl3 {

std::vector<MapObject> loadMapObjects(const Rom& rom, int map) {
    std::vector<MapObject> v;
    uint16_t p = uint16_t(rom.ptr(6, 0x6c58, map) + 6);
    for (int n = 0; n < 16 && rom.b(6, p) != 0xff; n++, p = uint16_t(p + 6)) {
        uint8_t r[6];
        for (int i = 0; i < 6; i++) r[i] = rom.b(6, uint16_t(p + i));
        MapObject o;
        o.flag = r[0];
        o.id = r[1];
        o.row = r[2] & 0x3f; o.flagsY = r[2] & 0xc0;
        o.col = r[3] & 0x3f; o.flagsX = r[3] & 0xc0;
        o.move = r[4] & 0x1f; o.kind = r[4] >> 6;
        o.sprite = r[5];
        v.push_back(o);
    }
    return v;
}

std::vector<MapExit> loadMapExits(const Rom& rom, int map) {
    std::vector<MapExit> v;
    uint16_t p = rom.ptr(6, 0x6308, map);
    const uint16_t end = rom.ptr(6, 0x6308, map + 1);
    if (map + 1 >= kMapCount || end < p || end - p > 0x200) return v;
    for (; p + 4 <= end; p = uint16_t(p + 4)) {
        uint8_t r[4];
        for (int i = 0; i < 4; i++) r[i] = rom.b(6, uint16_t(p + i));
        MapExit e;
        e.regionRow = r[0] >> 4;
        e.regionCol = r[0] & 0x0f;
        e.dest = r[1];
        e.row = r[2] & 0x7f;
        e.col = r[3] & 0x7f;
        e.facing = uint8_t(((r[2] >> 7) << 1) | (r[3] >> 7));
        v.push_back(e);
    }
    return v;
}

std::array<std::array<uint8_t, 4>, 64> loadBlockDefs(const Rom& rom, const GfxSet& g) {
    std::array<std::array<uint8_t, 4>, 64> d{};
    const uint16_t p = rom.ptr(6, 0x4000, g.blockset);
    for (int i = 0; i < 64; i++)
        for (int k = 0; k < 4; k++) d[i][k] = rom.b(6, uint16_t(p + i * 4 + k));
    return d;
}

std::array<uint8_t, 64> loadBlockAttrs(const Rom& rom, const GfxSet& g) {
    std::array<uint8_t, 64> a{};
    const uint16_t p = rom.ptr(6, 0x5acc, g.blockset);
    for (int i = 0; i < 64; i++) a[i] = rom.b(6, uint16_t(p + i));
    return a;
}

std::array<std::array<uint8_t, 16>, 128> loadObjTiles(const Rom& rom, int spriteset, int playerPage) {
    uint8_t v[0x800] = {};
    auto cp = [&](int bank, uint16_t src, uint16_t dst, int n) {
        std::memcpy(v + (dst - 0x8000), rom.raw(bank, src), size_t(n));
    };
    // 07:7f00 - fixed sprite tiles
    struct C { int bank; uint16_t src; int n; };
    const C fixed[] = {{4, 0x7d00, 0x100}, {4, 0x7c00, 0x100}, {4, 0x7e00, 0x200}, {3, 0x5100, 0x100},
                       {3, 0x5800, 0x100}, {3, 0x4e00, 0x40}, {3, 0x5380, 0x80}, {3, 0x5440, 0x40}};
    uint16_t dst = 0x8100;
    for (const C& c : fixed) { cp(c.bank, c.src, dst, c.n); dst = uint16_t(dst + c.n); }
    cp(3, 0x5430, 0x87e0, 0x10);
    // 00:0438 - sprite set 03:751C[spriteset]: page mask, page list, then single frames
    uint16_t p = rom.ptr(3, 0x751c, spriteset);
    const uint8_t mask = rom.b(3, p++);
    for (int i = 0; i < 8; i++) {
        if (!(mask & (0x80 >> i))) continue;
        const uint8_t idx = rom.b(3, p++);
        if (idx < 0x40) cp(4, uint16_t((0x40 + idx) << 8), uint16_t(0x8000 + i * 0x100), 0x100);
        else cp(3, uint16_t((0x49 + idx - 0x40) << 8), uint16_t(0x8000 + i * 0x100), 0x100);
    }
    dst = 0x8600;
    for (int n = 0; n < 8 && rom.b(3, p) != 0xff; n++) {
        cp(3, uint16_t(0x4900 + (rom.b(3, p++) & 0x7f) * 0x40), dst, 0x40);
        dst = uint16_t(dst + 0x40);
    }
    // 00:04b1 - party leader -> $8000
    cp(4, uint16_t(playerPage << 8), 0x8000, 0x100);
    std::array<std::array<uint8_t, 16>, 128> t{};
    for (int i = 0; i < 128; i++) std::memcpy(t[i].data(), v + i * 16, 16);
    return t;
}

} // namespace ffl3
