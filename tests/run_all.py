#!/usr/bin/env python3
"""Run every test suite and print a summary.

usage: tests/run_all.py saga3.gb [--build build] [--battle-dumps DIR] [--rec-dumps DIR]
                        [--vm-recs DIR] [--skip name,name] [--only name,name]

Suites that need dumps/recordings from the real game are skipped when the folder isn't given
(they hold game data, so they're not in the repo). The in-game --autotest runs with SDL's
dummy drivers and leaves its report in BUILD/autotest/results.html.
Exits 1 if anything failed."""
import argparse, os, subprocess, sys, time

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)

ap = argparse.ArgumentParser()
ap.add_argument('rom')
ap.add_argument('--build', default=os.path.join(root, 'build'))
ap.add_argument('--battle-dumps')
ap.add_argument('--rec-dumps')
ap.add_argument('--vm-recs')
ap.add_argument('--skip', default='')
ap.add_argument('--only', default='')
ap.add_argument('-v', '--verbose', action='store_true')
a = ap.parse_args()

exe = os.path.join(a.build, 'ffl3')
vm = os.path.join(a.build, 'vmreplay')
rom = os.path.abspath(a.rom)
headless = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')

suites = [
    ('maps',     ['sh', f'{here}/test_maps.sh', rom, exe], None),
    ('field',    ['sh', f'{here}/test_field.sh', rom, exe], None),
    ('battle',   [sys.executable, f'{here}/test_battle.py', exe, rom, a.battle_dumps or ''], None),
    ('tileanim', [sys.executable, f'{here}/test_tileanim.py', exe, rom, a.rec_dumps or ''],
                 None if a.rec_dumps else 'needs --rec-dumps'),
    ('vm',       ['sh', f'{here}/test_vm.sh', rom, vm, a.vm_recs or ''],
                 None if a.vm_recs else 'needs --vm-recs'),
    ('tester',   [sys.executable, f'{here}/test_tester.py', exe, rom], None),
    ('autotest', [exe, rom, '--autotest'], None),
]
skip = set(filter(None, a.skip.split(',')))
only = set(filter(None, a.only.split(',')))

def count(out):
    lines = out.splitlines()
    ok = sum(1 for l in lines if l.startswith(('ok ', 'PASS')) or ' PASS' in l)
    bad = sum(1 for l in lines if l.startswith('FAIL') or ' FAIL' in l)
    for l in lines:   # suites that print one summary line ("all 192 maps match")
        if l.startswith('all ') and ok == 0:
            ok = int(l.split()[1])
    return ok, bad

results = []
for name, cmd, why in suites:
    if (only and name not in only) or name in skip:
        continue
    if why:
        results.append((name, 'SKIP', why, 0)); print(f'{name:9} SKIP  ({why})'); continue
    tty = sys.stdout.isatty()
    if tty:
        print(f'{name:9} ...', end='', flush=True)
    t = time.time()
    try:
        p = subprocess.run(cmd, cwd=a.build if name == 'autotest' else root, env=headless,
                           capture_output=True, text=True, timeout=1800)
        out, code = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired:
        out, code = 'timed out', 1
    dt = time.time() - t
    ok, bad = count(out)
    status = 'PASS' if code == 0 and bad == 0 else 'FAIL'
    detail = f'{ok} ok' + (f', {bad} failed' if bad else '') + (f', exit {code}' if code else '')
    print(('\r' if tty else '') + f'{name:9} {status}  {detail}  ({dt:.1f}s)')
    if status == 'FAIL' or a.verbose:
        for l in out.splitlines():
            if a.verbose or 'FAIL' in l or 'rror' in l:
                print('    ' + l)
    results.append((name, status, detail, dt))

failed = [r for r in results if r[1] == 'FAIL']
print()
print(f'{len(results) - len(failed)}/{len(results)} suites ok'
      + (f'; failed: {", ".join(r[0] for r in failed)}' if failed else ''))
if any(r[0] == 'autotest' for r in results):
    print('autotest report: ' + os.path.join(a.build, 'autotest', 'results.html'))
sys.exit(1 if failed else 0)
