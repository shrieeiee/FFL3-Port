"""FFL3 / SaGa 3 ROM data access: decompression, maps, tilesets, text.
Every routine here mirrors a specific routine in the original game (address noted)."""
import struct

class Rom:
    def __init__(self, path):
        self.d = open(path, 'rb').read()

    def off(self, bank, addr):
        return addr if addr < 0x4000 else bank * 0x4000 + (addr - 0x4000)

    def b(self, bank, addr):
        return self.d[self.off(bank, addr)]

    def w(self, bank, addr):
        return self.b(bank, addr) | self.b(bank, addr + 1) << 8

    def ptr(self, bank, table, idx):           # 00:3741 (hl += a*2; hl = [hl])
        return self.w(bank, table + idx * 2)

    # ---- 00:3d76 bitmask decompressor -------------------------------------
    def decompress(self, bank, start, end):
        out = bytearray()
        p = start
        n = 8 << self.b(bank, p); p += 1      # block size 8/16/32/64
        while p < end:
            fill = self.b(bank, p); p += 1
            mask_p = p; p += n // 8
            for i in range(n // 8):
                m = self.b(bank, mask_p + i)
                for bit in range(8):
                    if m & (0x80 >> bit):
                        out.append(self.b(bank, p)); p += 1
                    else:
                        out.append(fill)
        return bytes(out)

    # ---- maps ----------------------------------------------------------------
    def map_header(self, map_id):              # 00:027d
        h = self.ptr(6, 0x6c58, map_id)
        r = [self.b(6, h + i) for i in range(6)]
        return dict(layout=r[0] & 0x7f, flag80=r[0] & 0x80, gfxset=r[1], unk1e=r[2],
                    hook=r[3], music=r[4] & 0x1f, music_hi=r[4] >> 5, unk2d=r[5])

    def gfxset(self, idx):                     # 00:0306, table 03:731c
        p = self.ptr(3, 0x731c, idx)
        r = [self.b(3, p + i) for i in range(7)]
        return dict(hmask=(r[0] & 0x0f) - 1, wmask=(r[0] >> 4) - 1,
                    pages=r[1:5], blockset=r[5], tileattr=r[5], unk=r[6])

    def layout(self, L):                       # 00:0f42
        bank = 8 if L >= 0x10 else 7
        idx = L - 0x10 if bank == 8 else L - 1
        start = self.ptr(bank, 0x4000, idx)
        end = self.ptr(bank, 0x4000, idx + 1)
        cells = self.decompress(bank, start, end)
        stride = self.b(7, 0x7ea0 + L)                 # tilemap row stride in bytes
        count = self.w(7, 0x7ec0 + L * 2) >> 2         # word = tile count; /4 = metatiles
        mt_base = self.ptr(bank, 0x4022, idx)
        wmt = stride // 2                              # metatiles per row
        hmt = count // wmt
        # expand 32px super-blocks -> 16px blocks (b0=TL b1=BL b2=TR b3=BR); this is what
        # the game keeps at wD000. 'tiles' is the BLOCK grid (name kept for compatibility).
        tiles = [[0] * (wmt * 2) for _ in range(hmt * 2)]
        meta = []
        for i in range(count):
            m = cells[i]
            o = mt_base + m * 4
            tl, bl, tr, br = (self.b(bank, o + k) for k in range(4))
            y, x = divmod(i, wmt)
            tiles[y*2][x*2], tiles[y*2][x*2+1] = tl, tr
            tiles[y*2+1][x*2], tiles[y*2+1][x*2+1] = bl, br
            meta.append(m)
        return dict(bank=bank, w=wmt, h=hmt, cells=meta, tiles=tiles, raw=cells)

    def tileset_vram(self, pages):
        """Returns 256 tiles (16 bytes each) indexed by BG tile number (8800 addressing)."""
        vram = bytearray(0x1800)               # 8000-97ff
        # 07:7f4f via rst18 $31 -> bank 3: 03:4000 x 0x800 -> 8800
        vram[0x0800:0x1000] = self.d[self.off(3, 0x4000):self.off(3, 0x4000) + 0x800]
        # 07:7f5b: four 0x200 pages from bank 5 -> 9000
        for i, pg in enumerate(pages):
            o = self.off(5, pg << 8)
            vram[0x1000 + i*0x200:0x1000 + (i+1)*0x200] = self.d[o:o+0x200]
        tiles = []
        for t in range(256):
            a = 0x1000 + t*16 if t < 0x80 else 0x0800 + (t-0x80)*16
            tiles.append(bytes(vram[a:a+16]))
        return tiles

def tile_pixels(t16):
    px = []
    for y in range(8):
        lo, hi = t16[y*2], t16[y*2+1]
        px.append([((lo >> (7-x)) & 1) | (((hi >> (7-x)) & 1) << 1) for x in range(8)])
    return px

def obj_vram(rom, spriteset, player_page=0x70):
    """Rebuild OBJ VRAM $8000-$87FF as the field loader leaves it (07:7f00, 00:0438, 00:04b1)."""
    v = bytearray(0x800)
    def cp(bank, src, dst, n):
        o = rom.off(bank, src); v[dst-0x8000:dst-0x8000+n] = rom.d[o:o+n]
    # 07:7f00 - fixed sprite tiles
    dst = 0x8100
    for bank, src, n in [(4,0x7d00,0x100),(4,0x7c00,0x100),(4,0x7e00,0x200),(3,0x5100,0x100),
                         (3,0x5800,0x100),(3,0x4e00,0x40),(3,0x5380,0x80),(3,0x5440,0x40)]:
        cp(bank, src, dst, n); dst += n
    cp(3, 0x5430, 0x87e0, 0x10)
    # 00:0438 - sprite set 03:751c[wCA1E]
    p = rom.ptr(3, 0x751c, spriteset)
    mask = rom.b(3, p); p += 1
    for i in range(8):
        if mask & (0x80 >> i):
            idx = rom.b(3, p); p += 1
            if idx < 0x40: cp(4, (0x40 + idx) << 8, 0x8000 + i*0x100, 0x100)
            else: cp(3, (0x49 + idx - 0x40) << 8, 0x8000 + i*0x100, 0x100)
    dst = 0x8600
    while rom.b(3, p) != 0xff:
        idx = rom.b(3, p) & 0x7f; p += 1
        cp(3, 0x4900 + idx*0x40, dst, 0x40); dst += 0x40
    # 00:04b1 - party leader
    cp(4, player_page << 8, 0x8000, 0x100)
    return bytes(v)


def block_tiles(rom, tileattr, block, layer=0):
    """16x16 block -> 4 BG tile numbers (TL, BL, TR, BR - column-major, verified against VRAM)
    via 06:4000[set] (00:0a3a, 01:5ea6)."""
    b = block & 0x3f
    if (block & 0x40) and not (layer & 0x40): b = 1      # other-layer blocks drawn as block 1
    elif not (block & 0x40) and (layer & 0x40): b = 0
    p = rom.ptr(6, 0x4000, tileattr) + b * 4
    return [rom.b(6, p + i) for i in range(4)]

def block_attr(rom, tileattr, block):
    return rom.b(6, rom.ptr(6, 0x5acc, tileattr) + (block & 0x3f))
