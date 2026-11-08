import sys; sys.path.insert(0, '/root/ffl3/tools')
from ffl3data import Rom, tile_pixels
from PIL import Image
r = Rom(sys.argv[1]); mid = int(sys.argv[2], 16); out = sys.argv[3]; scale = int(sys.argv[4]) if len(sys.argv) > 4 else 2
h = r.map_header(mid); g = r.gfxset(h['gfxset']); L = r.layout(h['layout'])
tiles = [tile_pixels(t) for t in r.tileset_vram(g['pages'])]
shade = [255, 170, 85, 0]   # BGP $E4
W, H = len(L['tiles'][0]) * 8, len(L['tiles']) * 8
img = Image.new('L', (W, H))
px = img.load()
for ty, row in enumerate(L['tiles']):
    for tx, t in enumerate(row):
        p = tiles[t]
        for y in range(8):
            for x in range(8):
                px[tx*8+x, ty*8+y] = shade[p[y][x]]
img.resize((W*scale, H*scale), Image.NEAREST).save(out)
print(f"map {mid:02x} layout {h['layout']:02x} {L['w']}x{L['h']} metatiles -> {out}")
