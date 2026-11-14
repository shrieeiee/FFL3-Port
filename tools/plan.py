#!/usr/bin/env python3
"""Plan a walk for a story route: shortest path on one map, printed as a `go` step.

usage: plan.py saga3.gb MAP ROW,COL (ROW,COL | exit | out | event N[,N] | attr XX | map XX) [--show] [--grid FILE]

Exits that warp within the same map are followed (multi-floor maps like towers); pitfalls are
only used when they're the way to the goal. Exit blocks in a region with no exit entry do
nothing (00:13F1 finds none), so they're walked over.
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
    A = []; Bk = []
    for l in lines[1:]:
        if l.startswith('obj '): objs.add(tuple(map(int, l[4:].split(','))))
        elif l.strip():
            A.append([int(t.split('/')[1], 16) for t in l.split()])
            Bk.append([int(t.split('/')[0], 16) for t in l.split()])
    goal = [g_ for g_ in goal if g_ not in ('--grid', '--show')][:2]
    goal = [x for x in goal if not x.endswith('.txt')]
else:
    h = r.map_header(m); g = r.gfxset(h['gfxset']); L = r.layout(h['layout'])
    A = [[block_attr(r, g['tileattr'], b & 0x7f) for b in row] for row in L['tiles']]
    Bk = [[b for b in row] for row in L['tiles']]
H, W = len(A), len(A[0])

# Exit table 06:6308[map] (00:13F1): an exit block warps by the 64 px region it's in; pitfalls
# ($08/$09, 00:14B2) too, after a fall (01:661C). Dest = (b & $7F) + 5.
EXITS = {}
p_, e_ = r.ptr(6, 0x6308, m), r.ptr(6, 0x6308, m + 1)
while p_ + 4 <= e_ and e_ - p_ <= 0x200:
    b = [r.b(6, p_ + i) for i in range(4)]
    EXITS[(b[0] >> 4, b[0] & 15)] = (b[1], (b[2] & 0x7f) + 5, (b[3] & 0x7f) + 5)
    p_ += 4
def warp(y, x):
    """Where an event block sends you: (map, row, col) or None."""
    a = A[y][x]
    if (a & 0xc0) != 0xc0: return None
    c = a & 0x3f
    if c in (0, 1, 2, 3, 0x0a): return EXITS.get(((y >> 2) & 15, (x >> 2) & 15))
    if c in (8, 9): return EXITS.get(((y >> 2) & 15, (x >> 2) & 15))        # pitfall: its own region
    return None
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
    if goal[0] == 'map':                                  # an exit to another map
        w = warp(y, x); return bool(w) and w[0] == int(goal[1], 16)
    gy, gx = map(int, goal[0].split(',')); return (y, x) == (gy, gx)

D = [('U', -1, 0), ('D', 1, 0), ('L', 0, -1), ('R', 0, 1)]

EXCL = (0xeb, 0xc1, 0xc3, 0xc5, 0xcc, 0xe4)
def jcheck(y, x, f, land):
    """01:4B5E (land=False: a wall to jump on the spot) / 4BA7 (land=True: can land)."""
    y %= H; x %= W
    a = A[y][x]; cls = a & 0xc0
    xx = cls if cls in (0xc0, 0x80) else a
    g = layer(f, a)
    if (xx & 8) and (g & (0x0b if land else 0xcb)) == 9: return False
    if (Bk[y][x] & 0x40) != (Bk[cur[0]][cur[1]] & 0x40): return False      # wCA12, the bridge layer
    if a in EXCL or (a & 0x70) == 0x30: return False
    if not land: return (xx & 3) == 3
    if g & 7 & xx: return False
    return (y, x) not in objs
def jump(y, x, f, d):
    """01:4A7F: where a jump facing d lands, or None (on the spot / can't)."""
    if m < 0x0c or (A[y][x] & 0xf0) == 0x30 or (f & 0x8b) == 9: return None
    dy, dx = {'U': (-1, 0), 'D': (1, 0), 'L': (0, -1), 'R': (0, 1)}[d]
    cur[:] = [y, x]
    if jcheck(y + dy, x + dx, f, False): return None
    if jcheck(y + 2 * dy, x + 2 * dx, f, True): return ((y + 2 * dy) % H, (x + 2 * dx) % W)
    if jcheck(y + dy, x + dx, f, True): return ((y + dy) % H, (x + dx) % W)
    return None
cur = [0, 0]
start = (sr, sc, flags0, '')
import heapq
prev = {start: None}
cost = {start: 0}
q = [(0, 0, start)]
tie = 0
def push(st, frm, how, c):
    global tie
    if st in cost and cost[st] <= c: return
    cost[st] = c; prev[st] = (frm, how); tie += 1; heapq.heappush(q, (c, tie, st))
end = None
while q:
    c0, _, cur_st = heapq.heappop(q)
    if c0 > cost.get(cur_st, 1 << 30): continue
    y, x, f, face = cur_st
    if (y, x) != (sr, sc) and is_goal(y, x): end = (y, x, f, face); break
    plain_exit = (A[y][x] & 0xc0) == 0xc0 and (A[y][x] & 0x3f) in (0, 1) and warp(y, x) is None
    if (y, x) != (sr, sc) and (A[y][x] & 0xc0) == 0xc0 and (A[y][x] & 0x3f) != 0x25 and not plain_exit:
        w = warp(y, x)                                    # same-map warp: carry on from where it lands
        if w and w[0] == m:
            push((w[1] % H, w[2] % W, 0x47, ''), cur_st, '|', c0 + 1)
        continue                                          # other events: stop
    for d, dy, dx in D:
        ny, nx = (y + dy) % H, (x + dx) % W
        ok, g = step(f, A[ny][nx])
        if not ok or (ny, nx) in objs: continue
        push((ny, nx, g, d), cur_st, d, c0 + 1)
    if face:                                              # B: jump the way we face (01:47B2); costs more
        j = jump(y, x, f, face)
        if j: push((j[0], j[1], f, face), cur_st, 'J', c0 + 4)
if not end:
    print('no path', file=sys.stderr); sys.exit(1)
path = []
p = end
while prev[p]: p, d = prev[p][0], prev[p][1]; path.append(d)
path.reverse()
end = end[:2]
toks = []
for d in path:
    if d == '|': toks.append(['|', 0]); continue          # a warp: the next leg starts afresh
    if d == 'J': toks.append(['J', 0]); continue          # a jump
    if toks and toks[-1][0] == d: toks[-1][1] += 1
    else: toks.append([d, 1])
out = []
for d, n in toks:
    if d == '|': out.append('|')
    elif d == 'J': out.append('#J')
    elif out and out[-1] not in ('|', '#J'): out[-1] += f' {d}{n}'
    else: out.append(f'go {d}{n}')
lines = [('jump' if o == '#J' else o) for o in out if o != '|']
print('\n'.join(lines) + f'   # -> {end[0]},{end[1]} attr {A[end[0]][end[1]]:02X}')
if '--show' in sys.argv:
    for y in range(H):
        print(''.join('S' if (y, x) == (sr, sc) else 'G' if (y, x) == end else
                      '#' if not step(flags0, A[y][x])[0] else 'E' if (A[y][x] & 0xc0) == 0xc0 else '.' for x in range(W)))
