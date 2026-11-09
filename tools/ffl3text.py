"""FFL3 text/event bytecode (00:3211 loop, handler table 00:3175, dictionary 03:71C5)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ffl3data import Rom

DICT_LOW = [0x01,0x02,0x03,0x04,0x16,0x17,0x18,0x19,0x1a,0x1b,0x40,0x41,0x42,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x0e,0x0f]
# argument bytes per control code (from the handlers); 'ref' = 2-byte event/text reference
ARGS = {0x00:0, 0x05:0, 0x06:0, 0x07:0, 0x08:0, 0x09:1, 0x0a:1, 0x0b:0, 0x0c:1, 0x0d:1, 0x10:0,
        0x11:1, 0x12:1, 0x13:1, 0x14:0, 0x15:1, 0x1c:0, 0x1d:1, 0x1e:1, 0x1f:1, 0x20:1, 0x21:1,
        0x22:1, 0x23:1, 0x24:1, 0x25:1, 0x26:1, 0x27:1, 0x28:1, 0x29:1, 0x2a:1, 0x2b:0, 0x2c:0,
        0x2d:0, 0x2e:0, 0x2f:1, 0x30:1, 0x31:1, 0x32:1, 0x33:1, 0x34:0, 0x35:1, 0x36:1, 0x37:1,
        0x38:1, 0x39:5, 0x3a:8, 0x3b:1, 0x3c:4, 0x3d:1, 0x3e:2, 0x3f:1, 0x43:3, 0x4b:0, 0x4c:0, 0x4d:0}
GLYPH = {0xac:',', 0xad:"'", 0xae:'…', 0xaf:'.', 0xee:'"', 0xef:'"', 0xf0:':', 0xf1:'/', 0xf2:'-',
         0xf3:'!', 0xf4:'?', 0xfe:'.', 0xff:' '}

class Text:
    def __init__(self, rom):
        self.r = rom; d = rom.d; o = rom.off(3, 0x71c5); self.dic = []
        for _ in range(80):
            n = d[o]; self.dic.append(d[o+1:o+1+n]); o += 1 + n
    def dict_entry(self, b):
        if 0x4e <= b < 0x88: return self.dic[b - 0x4e]
        if b in DICT_LOW: return self.dic[0x3a + DICT_LOW.index(b)]
        return None
    @staticmethod
    def glyph(b):
        if 0xba <= b <= 0xd3: return chr(65 + b - 0xba)
        if 0xd4 <= b <= 0xed: return chr(97 + b - 0xd4)
        if 0xb0 <= b <= 0xb9: return chr(48 + b - 0xb0)
        return GLYPH.get(b, '<%02x>' % b)
    @staticmethod
    def ref_target(lo, hi):
        """2-byte reference (00:2ef6): event command, or (bank, addr) of another text."""
        if hi < 0x40: return ('event', hi, lo)
        if hi < 0x80: return ('text', 9, hi << 8 | lo)
        if hi < 0xc0: return ('text', 0xa, (hi | 0x40) << 8 & 0x7fff | lo)
        return ('text', 3, (hi & 0x7f) << 8 | lo)
    def parse(self, bank, addr, limit=2000):
        """Yield (kind, value, args) ops until a top-level end (00 or 10). Returns end address."""
        p = addr; ops = []
        for _ in range(limit):
            b = self.r.b(bank, p); p += 1
            if b >= 0x88: ops.append(('ch', b, ())); continue
            e = self.dict_entry(b)
            if e is not None: ops.append(('word', b, tuple(e))); continue
            n = ARGS.get(b)
            if n is None: ops.append(('bad', b, ())); return ops, p
            args = tuple(self.r.b(bank, p + i) for i in range(n)); p += n
            ops.append(('code', b, args))
            if b in (0x00, 0x10): return ops, p
        return ops, p
    def render(self, ops):
        s = ''
        for k, b, a in ops:
            if k == 'ch': s += self.glyph(b)
            elif k == 'word': s += ''.join(self.glyph(x) if x >= 0x88 else {6: '\n', 0x1c: '', 0x1f: '<name>'}.get(x, '{%02x}' % x) for x in a)
            elif b == 0x06: s += '\n'
            elif b in (0x1f,): s += '<name%d>' % a[0]
            elif b in (0x39, 0x3a, 0x3c, 0x3e): s += '{%02x %s}' % (b, ' '.join('%02x' % x for x in a))
            elif b in (0x00, 0x10): s += '{end}' if b == 0 else '{wait,end}'
            else: s += '{%02x%s}' % (b, (' ' + ' '.join('%02x' % x for x in a)) if a else '')
        return s

if __name__ == '__main__':
    r = Rom(sys.argv[1]); T = Text(r)
    first = r.w(0xa, 0x4004); n = (first - 0x4004) // 2
    ptrs = [r.ptr(0xa, 0x4004, i) for i in range(n)]
    srt = sorted(set(ptrs)); ok = bad = 0; fails = []
    for i, p in enumerate(ptrs):
        ops, end = T.parse(0xa, p)
        nxt = next((q for q in srt if q > p), None)
        if nxt is None or end == nxt: ok += 1
        else: bad += 1; fails.append((i + 1, p, end, nxt, ops[-1]))
    print(f'{n} NPC texts: {ok} end exactly at the next text, {bad} do not')
    for f in fails[:12]: print('  id %02x 0a:%04x ends %04x next %04x last op %s' % f)
