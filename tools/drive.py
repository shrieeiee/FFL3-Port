"""Closed-loop driver: replays the game in cov, one planned step at a time."""
import sys, subprocess
sys.path.insert(0, '/root/ffl3/tools')
from route import route
BASE = open('/root/ffl3/run/probe.txt').read()
def run(tokens, tag='d'):
    open('/root/ffl3/run/drive.txt', 'w').write(BASE + ' '.join(tokens) + f' w2 dump:{tag}\n')
    subprocess.run(['/root/ffl3/tools/cov', '/root/ffl3/rom/saga3.gb', '/root/ffl3/probe2', '/root/ffl3/run/drive.txt'],
                   capture_output=True)
    m = open(f'/root/ffl3/probe2/{tag}.mem', 'rb').read()
    return m[0xc6e8], ((m[0xc6e4] | m[0xc6e5] << 8) + 0x50) // 16 % 64, ((m[0xc6e6] | m[0xc6e7] << 8) + 0x50) // 16 % 64, m
def goto(tokens, mid, goal, maxsteps=80):
    for _ in range(maxsteps):
        cm, y, x, m = run(tokens)
        if cm != mid: return tokens, cm
        p = route(mid, (y, x), goal)
        if not p: print('no route from', (y, x)); return tokens, cm
        tokens.append(f'hold:{p[0]}:16 w6')
    return tokens, None
if __name__ == '__main__':
    toks, newmap = goto([], 0x10, (23, 12))
    cm, y, x, m = run(toks + ['w60'])
    print('now map %02x at %d,%d after %d steps' % (cm, y, x, len(toks)))
    open('/root/ffl3/run/drive_tokens.txt', 'w').write(' '.join(toks))
