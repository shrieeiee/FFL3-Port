"""Plan a walk on a non-overworld map with the verified rules, emit cov tokens."""
import sys, collections
sys.path.insert(0, '/root/ffl3-port/tools')
from ffl3data import Rom, block_attr
r = Rom('/root/ffl3/rom/saga3.gb')
def grid(mid):
    h = r.map_header(mid); g = r.gfxset(h['gfxset']); L = r.layout(h['layout'])
    B = L['tiles']; return [[block_attr(r, g['blockset'], b) for b in row] for row in B]
def walkable(a):
    if (a & 0xc0) == 0xc0: return False
    if (a & 0xc0) == 0x80: return True
    return (a & 0x45) == 0
def route(mid, start, goal):
    A = grid(mid); H, W = len(A), len(A[0])
    D = {'up': (-1, 0), 'down': (1, 0), 'left': (0, -1), 'right': (0, 1)}
    prev = {start: None}; q = collections.deque([start])
    while q:
        p = q.popleft()
        for d, (dy, dx) in D.items():
            n = ((p[0] + dy) % H, (p[1] + dx) % W)
            if n == goal:
                path = [d]; c = p
                while prev[c]: path.append(prev[c][1]); c = prev[c][0]
                return path[::-1]
            if n in prev or not walkable(A[n[0]][n[1]]): continue
            prev[n] = (p, d); q.append(n)
    return None
if __name__ == '__main__':
    mid = int(sys.argv[1], 16); s = tuple(map(int, sys.argv[2].split(','))); g = tuple(map(int, sys.argv[3].split(',')))
    p = route(mid, s, g); print(' '.join(f'hold:{d}:16 w8' for d in p) if p else 'NO ROUTE')
