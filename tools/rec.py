"""Record a battle for the VM replay test: rec.py NAME FORM 'tokens...'"""
import sys, subprocess, os, shutil
OFF = int(open('/root/ffl3/run/param_off.txt').read())
BASE = open('/root/ffl3/run/probe.txt').read()
name, form, toks = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
d = bytearray(open('/root/ffl3/rom/saga3.gb', 'rb').read()); d[OFF] = form
rom = f'/tmp/saga3_f{form:02x}.gb'; open(rom, 'wb').write(d)
out = '/root/ffl3/rec'; os.makedirs(out, exist_ok=True)
script = BASE + ' '.join(['trace:/tmp/rec_tr.txt', 'w30', 'hold:up:3', 'w20', 'poke:C20A:03', 'poke:C35A:00', 'a', toks, f'dump:{name}_end', f'shot:{name}_end']) + '\n'
open('/tmp/rec_script.txt', 'w').write(script)
env = dict(os.environ, REPLAY=f'{out}/{name}.txt')
os.makedirs('/tmp/trout', exist_ok=True)
r = subprocess.run(['/root/ffl3/tools/trace', rom, out, '/tmp/rec_script.txt'], env=env, capture_output=True, text=True)
shutil.copy('/tmp/trout/enter.mem', f'{out}/{name}_enter.mem')
print(r.stderr.strip()[-100:])
