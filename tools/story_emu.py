#!/usr/bin/env python3
"""Play a story route (tests/story/route.txt) on the real game in PyBoy, logging checkpoints.

usage: story_emu.py saga3.gb route.txt out.log [--shots DIR] [--state-every N]

The same step rules as the port's --story player (src/story.cpp), so the two logs can be
diffed with tests/story_compare.py. Needs PyBoy (pip install pyboy).

"Free" = the field main loop (00:0265) ran this frame, the leader stands on the block grid and
no step is running (wC6EF = 0, 01:43F9) - the step event check (00:08F3) has had its turn.
While not free (text, battles, menus, map fades) the player mashes A, like the port's.
"""
import sys, os, argparse
from pyboy import PyBoy

ap = argparse.ArgumentParser()
ap.add_argument('rom'); ap.add_argument('route'); ap.add_argument('log')
ap.add_argument('--shots')
a = ap.parse_args()

pb = PyBoy(a.rom, window='null', sound_emulated=False)
pb.set_emulation_speed(0)
mem = pb.memory
st = {'loop': False}
pb.hook_register(0, 0x0265, lambda ctx: st.__setitem__('loop', True), None)
if os.environ.get('STORY_TXT'):                           # debug: log every text the game opens (00:3211)
    pb.hook_register(0, 0x3211, lambda ctx: print(f'TXT frame {frame} bank {pb.memory[0x2000] if False else "?"}'), None)

frame = 0
held = set()
log = open(a.log, 'w')

last_pos = {'row': 0, 'col': 0, 'map': 0}
def state():
    """Map and block of the leader. The block only updates when the leader is lined up on one
    (mid-step the scroll would round to the next block early going up or left)."""
    sy = mem[0xc6e4] | mem[0xc6e5] << 8
    sx = mem[0xc6e6] | mem[0xc6e7] << 8
    aligned = sy % 16 == 0 and sx % 16 == 0 and mem[0xc6ef] == 0   # wC6EF: a step still running (01:43F9)
    if sy % 16 == 0 and sx % 16 == 0 or mem[0xc6e8] != last_pos['map']:
        last_pos.update(map=mem[0xc6e8], row=((sy + 0x50) >> 4) & 63, col=((sx + 0x50) >> 4) & 63)
    return {'map': mem[0xc6e8], 'row': last_pos['row'], 'col': last_pos['col'], 'aligned': aligned}

free_run = 0
def tick(keys=()):
    """One frame with these buttons held; tracks how many frames in a row we've been free."""
    global frame, free_run
    for k in list(held):
        if k not in keys: pb.button_release(k); held.discard(k)
    for k in keys:
        if k not in held: pb.button_press(k); held.add(k)
    st['loop'] = False
    pb.tick(1, bool(a.shots))
    frame += 1
    s = state()
    free_run = free_run + 1 if (st['loop'] and s['aligned']) else 0
    return s

def free(): return free_run >= 2

def flags():
    return bytes(mem[0xc760 + i] for i in range(32)).hex()

def party():
    out = []
    for i in range(4):
        b = 0xc500 + 0x60 * i
        out.append('%d/%d' % (mem[b + 0x10] | mem[b + 0x11] << 8, mem[b + 0x12] | mem[b + 0x13] << 8))
    return ' '.join(out)

def mash_until_free(limit, start=False):
    n = 0
    while not free():
        if n >= limit: return False
        k = ()
        if n % 16 < 2: k = ('a', 'start') if start and (n // 16) % 4 == 3 else ('a',)
        tick(k); n += 1
    return True

DIRS = {'U': 'up', 'D': 'down', 'L': 'left', 'R': 'right'}
KEYS = {'A': 'a', 'B': 'b', 'S': 'start', 'E': 'select', **DIRS}

def emit(kind, label, extra=''):
    s = state()
    line = f'{kind} {label} frame {frame} map {s["map"]:02X} pos {s["row"]},{s["col"]} flags {flags()} party {party()}{extra}'
    print(line); log.write(line + '\n'); log.flush()
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
        pb.screen.image.save(os.path.join(a.shots, f'{label}.png'))

def fail(why):
    emit('FAIL', why.replace(' ', '_'))
    sys.exit(1)

steps = [l.split('#')[0].strip() for l in open(a.route)]
section = 'start'
for ln, line in enumerate(steps, 1):
    if not line: continue
    w = line.split()
    op = w[0]
    if op == '==':
        section = '_'.join(w[1:]); continue
    if op == 'new':                                       # title, naming, opening: until free
        def press(k, n=1):
            for _ in range(n):
                for _ in range(3): tick((k,))
                for _ in range(17): tick()
        for _ in range(600): tick()                       # the same presses as run/probe.txt
        press('start'); [tick() for _ in range(60)]; press('a'); [tick() for _ in range(60)]; press('a', 400)
        for _ in range(4):
            press('start'); [tick() for _ in range(60)]; press('a', 10)
        press('start'); [tick() for _ in range(60)]
        if not mash_until_free(60000): fail(f'line {ln} never got control')
    elif op == 'go':                                      # go D3 L2: end up 3 blocks down, ...
        for tok in w[1:]:
            d, n = DIRS[tok[0]], int(tok[1:] or 1)
            vert = tok[0] in 'UD'; sign = 1 if tok[0] in 'DR' else -1
            if not mash_until_free(20000): fail(f'line {ln} stuck before {tok}')
            map0 = state()['map']; prev = state()['row' if vert else 'col']; moved = 0; guard = 0
            while moved < n and state()['map'] == map0:
                if not mash_until_free(20000): fail(f'line {ln} stuck in {tok}')
                if state()['map'] != map0: break
                s0 = state(); t = 0; idle = 0
                while True:
                    s = tick((d,)); t += 1
                    idle = 0 if st['loop'] else idle + 1      # the overworld loop skips frames: only
                    if (s['map'], s['row'], s['col']) != (s0['map'], s0['row'], s0['col']) or idle >= 8: break   # 8 in a row = text / battle
                    if t > 120: fail(f'line {ln} blocked going {tok} at {s0["map"]:02X} {s0["row"]},{s0["col"]}')
                now = state()['row' if vert else 'col']
                if os.environ.get("STORY_DEBUG"):
                    print('try', guard, tok, 'from', s0['row'], s0['col'], 'to', state()['row'], state()['col'], 't', t, 'loop', st['loop'], 'c6e3', mem[0xc6e3], 'c2e0', hex(mem[0xc2e0]), 'c6ef', mem[0xc6ef])
                if now != prev:
                    dd = (now - prev) * sign
                    if dd < -32: dd += 64
                    if dd > 32: dd -= 64
                    moved += dd; prev = now
                guard += 1
                if guard > 400: fail(f'line {ln} wandering in {tok}')
                tick()
    elif op == 'a':                                       # press A (talk / examine), then until free
        if not mash_until_free(20000): fail(f'line {ln} stuck before A')
        tick(('a',)); tick(('a',)); tick(); tick()
        if not mash_until_free(20000): fail(f'line {ln} stuck after A')
    elif op == 'wait':
        for _ in range(int(w[1])): tick()
    elif op == 'keys':                                    # raw presses: A B S U D L R, wN = wait
        for tok in w[1:]:
            if tok[0] == 'w': [tick() for _ in range(int(tok[1:]))]; continue
            for _ in range(3): tick((KEYS[tok],))
            for _ in range(12): tick()
    elif op == 'settle':
        if not mash_until_free(20000): fail(f'line {ln} never settled')
    elif op == 'peek':
        emit('CHECK', w[1] if len(w) > 1 else f'{section}_{ln}')
    elif op == 'check':
        mash_until_free(20000); emit('CHECK', w[1] if len(w) > 1 else f'{section}_{ln}')
    elif op == 'expect':                                  # expect map MM [row col]
        mash_until_free(20000)
        s = state()
        want = (int(w[2], 16),) + tuple(int(x) for x in w[3:5])
        got = (s['map'], s['row'], s['col'])[:len(want)]
        if got != want: fail(f'line {ln} expected {w[2:]} got {s["map"]:02X} {s["row"]},{s["col"]}')
        emit('CHECK', f'{section}_{ln}')
    else:
        fail(f'line {ln} unknown step {op}')
emit('DONE', 'end')
