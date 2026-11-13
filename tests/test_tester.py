"""The tester layer, driven by injected key presses (needs SDL's dummy drivers):
F7 test mode marks a pass and a bug, A sets an item up, F12-style report with a clip.avi.
usage: test_tester.py build/ffl3 rom.gb"""
import glob, os, shutil, subprocess, sys
exe, rom = sys.argv[1:3]
d = os.path.dirname(os.path.abspath(exe))
for p in [os.path.join(d, 'bug_reports'), os.path.join(d, 'test_results.txt')]:
    shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else (os.path.exists(p) and os.remove(p))
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', FFL3_SELFTEST='1', FFL3_SELFTEST_AT='400',
           FFL3_INJECT='30:F7 40:Down 50:Right 60:Down 70:Left 120:F7 130:Down 140:Down 150:Down 160:Return')
subprocess.run([exe, rom, '--no-title'], env=env, capture_output=True, timeout=60)
fails = 0
def check(name, ok):
    global fails
    print(('PASS' if ok else 'FAIL'), name)
    fails += not ok
res = open(os.path.join(d, 'test_results.txt')).read() if os.path.exists(os.path.join(d, 'test_results.txt')) else ''
check('test mode: pass recorded', 'PASS | Water anim' in res)
check('test mode: bug recorded', 'BUG  | Talon deck' in res)
reps = sorted(glob.glob(os.path.join(d, 'bug_reports', '*')))
check('bug report from the test menu + the self-test report', len(reps) == 2)
last = reps[-1] if reps else ''
for f in ['clip.avi', 'game.sav', 'inputs.txt', 'log.txt', 'info.txt', 'screen.bmp']:
    check('report has ' + f, os.path.exists(os.path.join(last, f)))
log = open(os.path.join(last, 'log.txt')).read() if last else ''
check('item set up from the menu (battle 00)', 'battle: formation 00' in log)
if shutil.which('ffprobe') and last:
    r = subprocess.run(['ffprobe', '-v', 'error', '-count_frames', '-show_entries', 'stream=codec_name,nb_read_frames', '-of', 'csv=p=0',
                        os.path.join(last, 'clip.avi')], capture_output=True, text=True)
    check('clip.avi decodes (msrle + pcm, ~400 frames): ' + r.stdout.replace('\n', ' '), 'msrle' in r.stdout and 'pcm_s16le' in r.stdout)
sys.exit(1 if fails else 0)
