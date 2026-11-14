#!/usr/bin/env python3
"""Plan a walk for a story route: shortest path on one map, printed as a `go` step.

usage: plan.py saga3.gb MAP ROW,COL (ROW,COL | exit | out | event N[,N] | attr XX) [--show] [--grid FILE]
       plan.py saga3.gb - - TARGET --grid build/story/grid.txt   (map and start from the grid:
       the port writes it where a --story run ends)

--grid: the live map the port dumps (FFL3_GRID=FILE ffl3 rom --map MM --pos R,C --walk W),
which has the runtime block patches and the visible objects; otherwise the ROM layout.

Walkability is the leader's rule (00:1024, vehicle 0): event blocks (attr & $C0 == $C0) are
walked onto, $80 blocks are open, otherwise blocked when attr & $45. Objects aren't taken
into account (the story player reports a block if one is in the way). Exits = event blocks
with command 0/1 (the exit table) or 2/3 (warp with return).
"""
import sys, collections
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from ffl3data import Rom, block_attr

r = Rom(sys.argv[1])
if sys.argv[2] == '-':
    hw = open(sys.argv[sys.argv.index('--grid') + 1]).readline().split()
    m = int(hw[1], 16); sr, sc = map(int, hw[hw.index('pos') + 1].split(','))
else:
    m = int(sys.argv[2], 16)
    sr, sc = map(int, sys.argv[3].split(','))
goal = sys.argv[4:]
objs = set()
mask, flags0 = 0xff, 0x47
if '--grid' in sys.argv:
    lines = open(sys.argv[sys.argv.index('--grid') + 1]).read().split('\n')
    hw = lines[0].split()
    if 'mask' in hw: mask = int(hw[hw.index('mask') + 1], 16); flags0 = int(hw[hw.index('flags') + 1], 16)
    A = []
    for l in lines[1:]:
        if l.startswith('obj '): objs.add(tuple(map(int, l[4:].split(','))))
        elif l.strip(): A.append([int(t.split('/')[1], 16) for t in l.split()])
    goal = [g_ for g_ in goal if g_ not in ('--grid', '--show')][:2]
    goal = [x for x in goal if not x.endswith('.txt')]
else:
    h = r.map_header(m); g = r.gfxset(h['gfxset']); L = r.layout(h['layout'])
    A = [[block_attr(r, g['tileattr'], b & 0x7f) for b in row] for row in L['tiles']]
H, W = len(A), len(A[0])
goal = [x for x in goal if x not in ('--show',)]

def layer(f, a):                                # 01:4EE1
    if not (a & 0x4b): f |= 3
    if (f & 3) == 3 and (a & 3) and (a & 3) != 3: f = (f & 0xfc) | ((a & 3) ^ 3)
    return (f & 0xf7) | (a & 8)

def step(f, a):
    """(can walk onto a block with attr a, leader flags after) - 00:1024 + 01:4E48."""
    if a == 0xeb: return False, f
    if (a & 0xc0) in (0xc0, 0x80): return True, 0x47
    g = layer(f, a)
    return (a & 0x47 & g & mask) == 0, g

def is_goal(y, x):
    if goal[0] == 'exit': return (A[y][x] & 0xc0) == 0xc0 and (A[y][x] & 0x3f) <= 3
    if goal[0] == 'attr': return A[y][x] == int(goal[1], 16)
    if goal[0] == 'event': return (A[y][x] & 0xc0) == 0xc0 and (A[y][x] & 0x3f) in [int(v, 16) for v in goal[1].split(',')]
    if goal[0] == 'out': return (A[y][x] & 0xc0) == 0xc0 and (A[y][x] & 0x3f) in (4, 5)
    gy, gx = map(int, goal[0].split(',')); return (y, x) == (gy, gx)

D = [('U', -1, 0), ('D', 1, 0), ('L', 0, -1), ('R', 0, 1)]
start = (sr, sc, flags0)
prev = {start: None}
q = collections.deque([start])
end = None
while q:
    y, x, f = q.popleft()
    if (y, x) != (sr, sc) and is_goal(y, x): end = (y, x, f); break
    if (y, x) != (sr, sc) and (A[y][x] & 0xc0) == 0xc0 and (A[y][x] & 0x3f) != 0x25: continue   # stop at events (not the lift)
    for d, dy, dx in D:
        ny, nx = (y + dy) % H, (x + dx) % W
        ok, g = step(f, A[ny][nx])
        if not ok or (ny, nx) in objs or (ny, nx, g) in prev: continue
        prev[(ny, nx, g)] = ((y, x, f), d); q.append((ny, nx, g))
if not end:
    print('no path', file=sys.stderr); sys.exit(1)
path = []
p = end
while prev[p]: p, d = prev[p][0], prev[p][1]; path.append(d)
path.reverse()
end = end[:2]
toks = []
for d in path:
    if toks and toks[-1][0] == d: toks[-1][1] += 1
    else: toks.append([d, 1])
print('go ' + ' '.join(f'{d}{n}' for d, n in toks) + f'   # -> {end[0]},{end[1]} attr {A[end[0]][end[1]]:02X}')
if '--show' in sys.argv:
    for y in range(H):
        print(''.join('S' if (y, x) == (sr, sc) else 'G' if (y, x) == end else
                      '#' if not step(flags0, A[y][x])[0] else 'E' if (A[y][x] & 0xc0) == 0xc0 else '.' for x in range(W)))
