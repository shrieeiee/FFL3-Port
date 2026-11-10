#!/usr/bin/env python3
"""Disassembler for the battle script VM in bank 0C (interpreter at 0C:4008).

Instruction byte = mode << 5 | op. Accumulator A = wCC05 (24-bit), operand B = wCC08.
Operand by mode: 0/1/2 = [c9xx] 1/2/3 bytes; 3/4 = [[c9xx]] 1/2 bytes (pointer);
5 = random 0..n-1 (n imm); 6 = imm8; 7 = imm16.
Destination by mode (assign): 0-2 = c9xx 1-3 bytes; 3/4 = via pointer at c9xx, 1/2 bytes;
5/6/7 = absolute 16-bit address, 1/2/3 bytes.

Statements (op): 00 dest = expr..1F   01 if expr..1F (then next statement)   02 jump
 03 native call   04 skip 1 byte   05 call   06 return   07 inc [c9xx]   08 dec [c9xx]
 09 call [c9xx]   0A jump [c9xx]   0B dest = imm8   0C call table40CE[mode]
 0D jump table40DE[mode]   1F end
Expression items: 00 mov 01 mul 02 div 03 add 04 sub 05 and 06 or 07 nand 08 xor 09 not
 0A shr 0B shl 0C == 0D < 0E > 0F <= 10 >= 11 subclamp 12 rnd([c9xx]) 13 mov abs 1F end
"""
import os, sys

EXPR = {0: 'mov', 1: 'mul', 2: 'div', 3: 'add', 4: 'sub', 5: 'and', 6: 'or', 7: 'nand', 8: 'xor',
        9: 'not', 0xa: 'shr', 0xb: 'shl', 0xc: '==', 0xd: '<', 0xe: '>', 0xf: '<=', 0x10: '>=',
        0x11: 'subc', 0x12: 'rnd', 0x13: 'movabs'}
TAB_CALL = [0x5f4a, 0x782a, 0x7c9c, 0x756a, 0x7d38, 0x46b0, 0x4711, 0x47bb]
TAB_JUMP = [0x5f4a, 0x7331, 0x7e1a, 0x76ca, 0x6ce0, 0x732d]
NAMES = {}


class VM:
    def __init__(self, rom):
        self.d = open(rom, 'rb').read()

    def b(self, a):
        return self.d[0xc * 0x4000 + a - 0x4000] if a >= 0x4000 else self.d[a]

    def w(self, a):
        return self.b(a) | self.b(a + 1) << 8

    def operand(self, a, mode):
        """-> (text, new address)"""
        x = self.b(a)
        if mode <= 2: return ('[%s]%s' % (var(0xc900 + x), ['', '.w', '.l'][mode]), a + 1)
        if mode <= 4: return ('[[%s]]%s' % (var(0xc900 + x), ['', '.w'][mode - 3]), a + 1)
        if mode == 5: return ('rnd(%d)' % x, a + 1)
        if mode == 6: return ('#%02x' % x, a + 1)
        return ('#%04x' % self.w(a), a + 2)

    def dest(self, a, mode):
        x = self.b(a)
        if mode <= 2: return ('[%s]%s' % (var(0xc900 + x), ['', '.w', '.l'][mode]), a + 1)
        if mode <= 4: return ('[[%s]]%s' % (var(0xc900 + x), ['', '.w'][mode - 3]), a + 1)
        return ('[%s]%s' % (var(self.w(a)), ['', '.w', '.l'][mode - 5]), a + 2)

    def expr(self, a, cond=False):
        parts = []
        while True:
            x = self.b(a); op = x & 0x1f; mode = x >> 5
            if op == 0x1f: return ' '.join(parts), a + 1
            if op == 0x12:
                parts.append('rnd([%s])' % var(0xc900 + self.b(a + 1))); a += 2; continue
            if op == 0x13:
                parts.append('movabs [%s]%s' % (var(self.w(a + 1)), ['', '.w', '.l', '', '', '', '', ''][mode])); a += 3; continue
            t, a = self.operand(a + 1, mode)
            nm = EXPR.get(op, 'op%02x' % op)
            if not cond and 0xc <= op <= 0x10: nm = 'mov'
            parts.append(t if nm == 'mov' and not parts else '%s %s' % (nm, t))
            if len(parts) > 40: return ' '.join(parts) + ' …', a

    def one(self, a):
        x = self.b(a); op = x & 0x1f; mode = x >> 5
        if op == 0x00:
            dst, n = self.dest(a + 1, mode); e, n = self.expr(n); return '%s = %s' % (dst, e), n, None
        if op == 0x0b:
            dst, n = self.dest(a + 1, mode); return '%s = #%02x' % (dst, self.b(n)), n + 1, None
        if op == 0x01:
            t, n = self.operand(a + 1, mode); e, n = self.expr(n, True); return 'if %s %s' % (t, e), n, None
        if op == 0x02: return 'jump %04x' % self.w(a + 1), a + 3, ('j', self.w(a + 1))
        if op == 0x03: return 'native %s' % name(self.w(a + 1)), a + 3, None
        if op == 0x04: return 'nop2 %02x' % self.b(a + 1), a + 2, None
        if op == 0x05: return 'call %s' % name(self.w(a + 1)), a + 3, ('c', self.w(a + 1))
        if op == 0x06: return 'return', a + 1, ('r', None)
        if op in (7, 8):
            return '%s [%s]%s' % ('inc' if op == 7 else 'dec', var(0xc900 + self.b(a + 1)), ['', '.w', '.l'][min(mode, 2)]), a + 2, None
        if op == 0x09: return 'call [[%s]]' % var(0xc900 + self.b(a + 1)), a + 2, None
        if op == 0x0a: return 'jump [[%s]]' % var(0xc900 + self.b(a + 1)), a + 2, ('r', None)
        if op == 0x0c: return 'call %s' % name(TAB_CALL[mode]), a + 1, ('c', TAB_CALL[mode])
        if op == 0x0d: return 'jump %s' % name(TAB_JUMP[mode] if mode < 6 else 0), a + 1, ('j', TAB_JUMP[mode] if mode < 6 else None)
        if op == 0x1f: return 'end', a + 1, ('r', None)
        return 'db %02x' % x, a + 1, None

    def listing(self, start, n=60, follow=False):
        a = start; out = []
        for _ in range(n):
            t, na, fl = self.one(a)
            out.append('%04x: %-18s %s' % (a, ' '.join('%02x' % self.b(i) for i in range(a, min(na, a + 6))), t))
            a = na
            if fl and fl[0] in 'jr' and not follow: break
        return out


VARS = {}
def var(a):
    if 0xd000 <= a < 0xd500:
        i, f = divmod(a - 0xd000, 0x80)
        return 'F%d+%02x' % (i, f)
    if 0xc900 <= a < 0xca00:
        return VARS.get(a, 'v%02x' % (a & 0xff))
    return VARS.get(a, '%04x' % a)

def name(a): return NAMES.get(a, '%04x' % a)


if __name__ == '__main__':
    vm = VM(sys.argv[1])
    for s in sys.argv[2:]:
        a = int(s, 16)
        print('\n'.join(vm.listing(a, int(os.environ.get('N', '80')), True)))
        print()
