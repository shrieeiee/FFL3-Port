#include "battlenatives.h"

namespace ffl3 {

void gbCopy(BattleVM& vm, uint16_t& hl, uint16_t& de, int n) {
    for (int i = 0; i < n; i++) vm.wr(de++, vm.rd(hl++));
}

void gbFill(BattleVM& vm, uint16_t hl, uint8_t a, int n) {
    for (int i = 0; i < n; i++) vm.wr(hl++, a);
}

// 0C:4BE8: hl = wD000 + [wC9D6] * $80 + de (field of the current combatant)
static uint16_t combatant(const BattleVM& vm, uint16_t field) {
    return uint16_t(0xd000 + vm.rd(0xc9d6) * 0x80 + field);
}

bool runLogicNative(BattleVM& vm, const Rom& rom, uint16_t addr) {
    switch (addr) {
    case 0x445f:                          // count the alive bits of wCC5E-60 -> wD87B-7D
        for (int i = 0; i < 3; i++) {
            uint8_t v = vm.rd(uint16_t(0xcc5e + i)), n = 0;
            for (int b = 0; b < 8; b++) n += (v >> b) & 1;
            vm.wr(uint16_t(0xd87b + i), n);
        }
        return true;
    case 0x4490: { uint16_t hl = vm.rd16(0xc9c2), de = 0xc90a; gbCopy(vm, hl, de, 6); return true; }
    case 0x44ac: { uint16_t hl = vm.rd16(0xc9c2), de = 0xd83e; gbCopy(vm, hl, de, 8); return true; }
    case 0x4865: { uint16_t de = vm.rd16(0xc9c2), hl = 0xd83e; gbCopy(vm, hl, de, 8); return true; }
    case 0x49db: {                        // list the known spells/talents in range -> wD846
        uint16_t de = combatant(vm, 0x6a), hl = 0xd846;
        vm.wr(0xd86a, 0);
        for (int c = 0;; c++) {
            const uint8_t byte = vm.rd(uint16_t(de + c / 8));
            if (byte & (0x80 >> (c & 7)) && vm.rd(0xce92) <= c) {
                vm.wr(hl++, rom.b(0x0c, uint16_t(0x7f33 + c)));
                vm.wr(0xd86a, uint8_t(vm.rd(0xd86a) + 1));
            }
            if (vm.rd(0xce93) < c + 1) return true;
        }
    }
    case 0x4b9f: {                        // battle records -> party records (wC500)
        const int n = vm.rd(0xd203) == 0xff ? 4 : 5;
        for (int i = 0; i < n; i++) {
            uint16_t hl = uint16_t(0xd000 + i * 0x80), de = uint16_t(0xc500 + i * 0x60);
            gbCopy(vm, hl, de, 0x59);
        }
        return true;
    }
    case 0x4bf6: {                        // which of 8 special items the combatant holds -> wC997
        uint16_t hl = combatant(vm, 3);
        if (vm.rd(hl) == 0xff) return true;
        hl = uint16_t(hl + 0x45);
        uint8_t c = 0;
        for (int s = 0; s < 8; s++) {
            hl++;
            for (int j = 0; j < 8; j++) {
                if (rom.b(0x0c, uint16_t(0x4c23 + j)) == vm.rd(hl)) c |= 1;
                c = uint8_t(c >> 1 | c << 7);
            }
        }
        vm.wr(0xc997, c);
        return true;
    }
    case 0x4d41: {                        // wCE00 table -> record +$62-64 of the 5 party slots
        uint16_t hl = 0xce00;
        for (int i = 0; i < 5; i++) {
            uint16_t de = uint16_t(0xd062 + i * 0x80);
            vm.wr(de++, vm.rd(hl++));
            hl++;
            gbCopy(vm, hl, de, 2);
        }
        return true;
    }
    case 0x5db6: {                        // wC9A5 (7 bytes) -> table 0C:5DC7[wC9D6]
        uint16_t de = vm.rd16(uint16_t(0x5dc7 + vm.rd(0xc9d6) * 2)), hl = 0xc9a5;
        gbCopy(vm, hl, de, 7);
        return true;
    }
    case 0x5f9d: {                        // message id wC9BF -> text pointer (09:78FE table)
        const uint16_t p = uint16_t(0x78fe + vm.rd(0xc9bf) * 2);
        vm.wr(0xce8d, rom.b(9, p));
        vm.wr(0xce8e, rom.b(9, uint16_t(p + 1)));
        return true;
    }
    case 0x5fcc: gbFill(vm, 0xd83e, 0xff, 8); return true;
    case 0x5fd3: gbFill(vm, 0xd86b, 0x19, 8); return true;
    case 0x5fdb: {                        // name from the ROM character records (bank 7)
        const uint16_t src = uint16_t(0x7ada + (vm.rd(0xc9d6) + 1) * 0x60);
        for (int i = 0; i < 7; i++) vm.wr(uint16_t(0xc9a5 + i), rom.b(7, uint16_t(src + i)));
        return true;
    }
    case 0x5ff1:                          // clear the damage tables
        gbFill(vm, 0xd822, 0x80, 13);
        gbFill(vm, 0xd82f, 0x80, 13);
        return true;
    case 0x602b: {                        // first combatant that isn't out ($80/$40 of +1)
        uint16_t hl = 0xd001;
        uint8_t a = 0;
        while (vm.rd(hl) & 0xc0) { hl = uint16_t(hl + 0x80); a++; }
        vm.wr(0xc9d8, a);
        return true;
    }
    case 0x6042: {                        // how many of the wDDFF combatants are fine
        uint8_t c = 0;
        uint16_t hl = 0xd001;
        int b = vm.rd(0xddff);
        do { if (!(vm.rd(hl) & 0xc0)) c++; hl = uint16_t(hl + 0x80); } while (--b & 0xff);
        vm.wr(0xc97e, c);
        return true;
    }
    case 0x606d: {
        uint16_t hl = combatant(vm, 0x3d);
        if ((vm.rd(0xc919) & 7) != 4) return true;
        uint16_t de = 0xd6bd;
        gbCopy(vm, hl, de, 3);
        return true;
    }
    case 0x6083:                          // cap four stats at 99
        for (int i = 0; i < 4; i++) if (vm.rd(uint16_t(0xc940 + i)) >= 100) vm.wr(uint16_t(0xc940 + i), 99);
        return true;
    case 0x6851: {
        uint16_t hl = uint16_t(0x681f + vm.rd(0xc9fd) * 5), de = 0xc97b;
        gbCopy(vm, hl, de, 2);
        vm.wr(0xc9ff, vm.rd(hl));
        de = 0xce91;
        gbCopy(vm, hl, de, 3);
        return true;
    }
    default:
        return false;
    }
}

} // namespace ffl3
