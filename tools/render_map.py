"""Render a whole map (all blocks, outside layer) with the game's field palette."""
import sys; sys.path.insert(0, __import__('os').path.dirname(__file__))
from ffl3data import Rom, tile_pixels, block_tiles
from PIL import Image
def render(r, mid, bgp=0xd2):
    h = r.map_header(mid); g = r.gfxset(h['gfxset']); L = r.layout(h['layout'])
    tiles = [tile_pixels(t) for t in r.tileset_vram(g['pages'])]
    B = L['tiles']; bh, bw = len(B), len(B[0])
    shade = [255, 170, 85, 0]
    img = Image.new('L', (bw*16, bh*16)); px = img.load()
    cache = {}
    for by in range(bh):
        for bx in range(bw):
            b = B[by][bx]
            if b not in cache: cache[b] = block_tiles(r, g['blockset'] if 'blockset' in g else g['tileattr'], b)
            t4 = cache[b]
            for k, (dx, dy) in enumerate([(0,0),(0,8),(8,0),(8,8)]):
                p = tiles[t4[k]]
                for y in range(8):
                    for x in range(8):
                        px[bx*16+dx+x, by*16+dy+y] = shade[(bgp >> (p[y][x]*2)) & 3]
    return img, h, L
if __name__ == '__main__':
    r = Rom(sys.argv[1]); img, h, L = render(r, int(sys.argv[2], 16)); img.save(sys.argv[3])
