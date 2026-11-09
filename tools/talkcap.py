"""Make the real game show NPC dialogue `id` by rewriting the Talon-console object, and
capture each page (screenshot + RAM/VRAM dump)."""
import sys, subprocess
BASE = open('/root/ffl3/run/probe.txt').read()
def capture(tid, pages=8, outdir='/root/ffl3/talk', wait=150):
    toks = ['w30', 'hold:up:3', 'w20', 'poke:C20A:%02X' % tid, 'poke:C35A:00', 'a']
    for i in range(pages):
        toks += [f'w{wait}', f'shot:t{tid:02x}_{i}', f'dump:t{tid:02x}_{i}', 'a']
    open('/root/ffl3/run/tc.txt', 'w').write(BASE + ' '.join(toks) + '\n')
    subprocess.run(['/root/ffl3/tools/cov', '/root/ffl3/rom/saga3.gb', outdir, '/root/ffl3/run/tc.txt'], capture_output=True)
if __name__ == '__main__':
    for t in sys.argv[1:]: capture(int(t, 16))
