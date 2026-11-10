#include "battlevm.h"

namespace ffl3 {

// VM register addresses (wCCxx)
enum : uint16_t {
    kDst = 0xcc01, kDstSize = 0xcc03, kCmp = 0xcc04, kA = 0xcc05, kB = 0xcc08, kSign = 0xcc0b,
    kLeft = 0xcc0c, kSp = 0xcc0f, kOp = 0xff9f,
};

BattleVM::BattleVM(const Rom& rom) : rom_(rom) {}

uint8_t BattleVM::rd(uint16_t a) const {
    if (a < 0x4000) return rom_.b(0, a);
    if (a < 0x8000) return rom_.b(0x0c, a);
    if (written && uninit && a >= 0xc000 && !written[a]) uninit[a]++;
    return mem[a];
}

void BattleVM::start(uint16_t pc) {
    wr(kSp, 0x11);                       // 0C:4000
    wr(kSp + 1, 0xcc);
    de_ = pc;
    steps_ = 0;
}

// 0C:43D6 - load c+1 bytes (c 0-2), or via pointer (c 3/4 -> 1/2 bytes), into wCC08.
void BattleVM::loadSized(int c, uint16_t hl) {
    if (c >= 3) { c -= 3; hl = rd16(hl); }
    wr(kB + 2, 0); wr(kB + 1, 0);
    if (c >= 2) { wr(kB, rd(hl)); hl++; wr(kB + 1, rd(hl)); hl++; wr(kB + 2, rd(hl)); }
    else if (c == 1) { wr(kB, rd(hl)); hl++; wr(kB + 1, rd(hl)); }
    else wr(kB, rd(hl));
}

// 0C:43C0 - operand byte at de: mode = bits 5-7.
void BattleVM::operand() {
    const int c = rd(de_++) >> 5;
    if (c == 5) {                          // random 0..n-1
        const uint8_t n = rd(de_++);
        const uint8_t r = random ? random(n) : 0;
        wr(kB, r); wr(kB + 1, 0); wr(kB + 2, 0);
    } else if (c == 6) {
        wr(kB, rd(de_++)); wr(kB + 1, 0); wr(kB + 2, 0);
    } else if (c == 7) {
        wr(kB, rd(de_)); wr(kB + 1, rd(uint16_t(de_ + 1))); wr(kB + 2, 0);
        de_ += 2;
    } else {
        const uint16_t hl = uint16_t(0xc900 | rd(de_++));
        loadSized(c, hl);
    }
}

// 0C:4017 - write the accumulator to the destination set up by the statement.
void BattleVM::store() {
    int c = rd(kDstSize);
    if (c >= 3) { if (c >= 5) c -= 2; c -= 3; }
    uint16_t hl = rd16(kDst);
    if (c >= 2) { wr(hl++, rd(kA)); wr(hl++, rd(kA + 1)); wr(hl, rd(kA + 2)); }
    else if (c == 1) { wr(hl++, rd(kA)); wr(hl, rd(kA + 1)); }
    else wr(hl, rd(kA));
}

// 0C:441F - two's-complement negate the 24-bit value whose top byte is at `top`, if negative.
void BattleVM::neg24(uint16_t top) {
    if (!(rd(top) & 0x80)) return;
    wr(top, uint8_t(~rd(top)));
    wr(uint16_t(top - 1), uint8_t(~rd(uint16_t(top - 1))));
    const uint8_t lo = uint8_t(~rd(uint16_t(top - 2)) + 1);
    wr(uint16_t(top - 2), lo);
    if (lo) return;
    wr(uint16_t(top - 1), uint8_t(rd(uint16_t(top - 1)) + 1));
    if (rd(uint16_t(top - 1))) return;
    wr(top, uint8_t(rd(top) + 1));
}

// 0C:444F - a comparison inside an "if": remember the op and the left-hand value.
void BattleVM::compareSave() {
    wr(kCmp, rd(de_) & 0x1f);
    wr(kLeft, rd(kA)); wr(kLeft + 1, rd(kA + 1)); wr(kLeft + 2, rd(kA + 2));
}

// 0C:40F1 - push the return address on the VM stack, continue at target.
void BattleVM::call(uint16_t target) {
    uint16_t sp = rd16(kSp);
    wr(sp++, uint8_t(de_)); wr(sp++, uint8_t(de_ >> 8));
    wr(kSp, uint8_t(sp)); wr(kSp + 1, uint8_t(sp >> 8));
    de_ = target;
}

static uint32_t a24(const BattleVM& vm, uint16_t at) { return uint32_t(vm.rd(at) | vm.rd(uint16_t(at + 1)) << 8 | vm.rd(uint16_t(at + 2)) << 16); }

// Expression items until the $1F terminator (0C:4186). Returns false when the statement ended.
bool BattleVM::expression() {
    for (;;) {
        const uint8_t k = rd(de_) & 0x1f;
        switch (k) {
        case 0x01: case 0x02: {            // mul / div, signed 16-bit (0C:4242 / 0C:428A)
            operand();
            wr(kSign, rd(kA + 2));
            neg24(kA + 2);
            wr(kSign, uint8_t(rd(kB + 2) ^ rd(kSign)));
            neg24(kB + 2);
            const uint16_t m = uint16_t(rd(kB) | rd(kB + 1) << 8);
            const uint16_t x = uint16_t(rd(kA) | rd(kA + 1) << 8);
            uint8_t e, d, l;
            if (k == 0x01) {               // 00:1C7E: 16x16 -> 32, low 24 bits kept
                const uint32_t p = uint32_t(x) * m;
                e = uint8_t(p); d = uint8_t(p >> 8); l = uint8_t(p >> 16);
                if (rd(kSign) & 0x80) {    // the original's negation drops the low byte: e = $80
                    e = 0x80; d = uint8_t(~d); l = uint8_t(~l);
                }
            } else {                       // 00:1C9C: restoring division, x/0 = 0
                uint16_t bc = x, hl = 0;
                const uint16_t sp = uint16_t(-m);
                for (int i = 0; i < 16; i++) {
                    const int bit = bc >> 15;
                    bc = uint16_t(bc << 1);
                    hl = uint16_t(hl << 1 | bit);
                    const uint32_t s = uint32_t(hl) + sp;
                    hl = uint16_t(s);
                    if (!(s >> 16)) { hl = uint16_t(hl + m); bc = uint16_t(bc | 1); }
                }
                const uint16_t q = uint16_t(~bc);
                e = uint8_t(q); d = uint8_t(q >> 8); l = 0;
                if (rd(kSign) & 0x80) {
                    l = 0xff;
                    const uint16_t n = uint16_t(-q);
                    e = uint8_t(n); d = uint8_t(n >> 8);
                }
            }
            wr(kA, e); wr(kA + 1, d); wr(kA + 2, l);
            break;
        }
        case 0x03: case 0x04: case 0x11: { // add / sub / sub clamped at 0 (24-bit)
            operand();
            const uint32_t a = a24(*this, kA), b = a24(*this, kB);
            uint32_t r = k == 0x03 ? a + b : a - b;
            if (k == 0x11 && b > a) r = 0;
            wr(kA, uint8_t(r)); wr(kA + 1, uint8_t(r >> 8)); wr(kA + 2, uint8_t(r >> 16));
            break;
        }
        case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: {   // and / or / nand / xor / not
            operand();
            for (int i = 0; i < 3; i++) {
                const uint8_t a = rd(uint16_t(kA + i)), b = rd(uint16_t(kB + i));
                uint8_t r = 0;
                switch (k) {
                case 0x05: r = a & b; break;
                case 0x06: r = a | b; break;
                case 0x07: r = uint8_t(~(a & b)); break;
                case 0x08: r = a ^ b; break;
                default: r = uint8_t(~b); break;
                }
                wr(uint16_t(kA + i), r);
            }
            break;
        }
        case 0x0a: case 0x0b: {            // shr / shl by the operand's low byte (24-bit)
            operand();
            uint32_t a = a24(*this, kA);
            for (int n = rd(kB); n > 0; n--) a = k == 0x0a ? a >> 1 : (a << 1) & 0xffffff;
            wr(kA, uint8_t(a)); wr(kA + 1, uint8_t(a >> 8)); wr(kA + 2, uint8_t(a >> 16));
            break;
        }
        case 0x0c: case 0x0d: case 0x0e: case 0x0f: case 0x10:     // compare ops (0C:42E1)
            if (rd(kCmp)) compareSave();
            operand();
            wr(kA, rd(kB)); wr(kA + 1, rd(kB + 1)); wr(kA + 2, rd(kB + 2));
            break;
        case 0x12: {                       // A = random(0..[c9xx]-1) (0C:42ED)
            de_++;
            const uint8_t n = rd(uint16_t(0xc900 | rd(de_++)));
            wr(kA, random ? random(n) : 0); wr(kA + 1, 0); wr(kA + 2, 0);
            break;
        }
        case 0x13: {                       // A = [absolute] (0C:4304)
            if (rd(kCmp)) compareSave();
            const int c = rd(de_++) >> 5;
            const uint16_t hl = rd16(de_);
            de_ += 2;
            loadSized(c, hl);
            wr(kA, rd(kB)); wr(kA + 1, rd(kB + 1)); wr(kA + 2, rd(kB + 2));
            break;
        }
        case 0x1f: {                       // end of statement (0C:4043)
            de_++;
            const uint8_t cmp = rd(kCmp);
            if (!cmp) { store(); return false; }
            const uint32_t l = a24(*this, kLeft), r = a24(*this, kA);
            const bool eq = l == r, lt = l < r;
            bool ok;
            if (eq) ok = cmp == 0x0c || cmp == 0x13 || cmp >= 0x0f;
            else if (lt) ok = cmp == 0x0d || cmp == 0x0f;
            else ok = cmp == 0x0e || cmp == 0x10;
            if (!ok) {                     // skip the next statement (0C:407C, table 0C:437C)
                uint8_t op = rd(de_) & 0x1f;
                if (op == 0x1f) op = 6;
                // 0C:437C: skip lengths per statement op (0 and 1 walk their items)
                static const int8_t kSkip[14] = {-1, -2, 3, 3, 2, 3, 1, 2, 2, 2, 2, 1, 1, 1};
                const int sk = op < 14 ? kSkip[op] : 1;
                if (sk > 0) { de_ += uint16_t(sk); }
                else {
                    if (sk == -1) de_ += ((rd(de_) >> 5) & 7) >= 5 ? 3 : 2;   // 4398
                    for (;;) {             // 43A5 loop (an "if" starts on its own opcode)
                        const uint8_t x = rd(de_);
                        const uint8_t o = x & 0x1f;
                        if (o == 0x1f) { de_++; break; }
                        if (o == 0x13 || (x >> 5) == 7) de_ += 3;
                        else de_ += 2;
                    }
                }
            }
            return false;
        }
        default:                           // 00, 14-1E: A = operand
            operand();
            wr(kA, rd(kB)); wr(kA + 1, rd(kB + 1)); wr(kA + 2, rd(kB + 2));
            break;
        }
    }
}

BattleVM::Stop BattleVM::run(long maxSteps) {
    for (long n = 0; n < maxSteps; n++) {
        if (onStep) onStep(de_);
        steps_++;
        const uint8_t x = rd(de_);
        const uint8_t op = x & 0x1f;
        if (op == 0x1f) return Stop::End;
        wr(kOp, op);
        const int mode = x >> 5;
        switch (op) {
        case 0x00: case 0x0b: {           // assignment (0C:414B)
            de_++;
            uint16_t hl = uint16_t(0xc900 | rd(de_++));
            if (mode >= 3) {
                if (mode < 5) hl = rd16(hl);
                else hl = uint16_t((hl & 0xff) | rd(de_++) << 8);
            }
            wr(kDst, uint8_t(hl)); wr(kDst + 1, uint8_t(hl >> 8)); wr(kDstSize, uint8_t(mode)); wr(kCmp, 0);
            if (op == 0x0b) {
                wr(kA, rd(de_++)); wr(kA + 1, 0); wr(kA + 2, 0);
                store();
            } else {
                expression();
            }
            break;
        }
        case 0x01:                         // if (0C:42C8)
            wr(kCmp, 1);
            operand();
            wr(kA, rd(kB)); wr(kA + 1, rd(kB + 1)); wr(kA + 2, rd(kB + 2));
            expression();
            break;
        case 0x02: de_ = rd16(uint16_t(de_ + 1)); break;
        case 0x03: {                       // native call
            const uint16_t target = rd16(uint16_t(de_ + 1));
            de_ += 3;
            if (native && !native(target)) return Stop::Yield;
            break;
        }
        case 0x04: de_ += 2; break;
        case 0x05: { const uint16_t t = rd16(uint16_t(de_ + 1)); de_ += 3; call(t); break; }
        case 0x06: {                       // return
            uint16_t sp = rd16(kSp);
            sp--; const uint8_t d = rd(sp); sp--; const uint8_t e = rd(sp);
            wr(kSp, uint8_t(sp)); wr(kSp + 1, uint8_t(sp >> 8));
            de_ = uint16_t(e | d << 8);
            break;
        }
        case 0x07: case 0x08: {            // inc / dec [c9xx], mode+1 bytes, with carry
            uint16_t hl = uint16_t(0xc900 | rd(uint16_t(de_ + 1)));
            de_ += 2;
            for (int c = mode + 1; c > 0; c--) {
                const uint8_t v = uint8_t(rd(hl) + (op == 0x07 ? 1 : -1));
                wr(hl++, v);
                if (op == 0x07 ? v != 0 : v != 0xff) break;
            }
            break;
        }
        case 0x09: {                       // call [[c9xx]]
            const uint16_t t = rd16(uint16_t(0xc900 | rd(uint16_t(de_ + 1))));
            de_ += 2;
            call(t);
            break;
        }
        case 0x0a: de_ = rd16(uint16_t(0xc900 | rd(uint16_t(de_ + 1)))); break;
        case 0x0c: { const uint16_t t = rd16(uint16_t(0x40ce + mode * 2)); de_++; call(t); break; }
        case 0x0d: de_ = rd16(uint16_t(0x40de + mode * 2)); break;
        default:
            return Stop::Bad;              // expression ops at statement level: never used
        }
    }
    return Stop::Limit;
}

} // namespace ffl3
