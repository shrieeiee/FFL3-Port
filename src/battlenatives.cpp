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

// 09:5466 (VM native 0C:6B49): unpack the data of the action in wC99B (type << 4) / wC99D (id)
// - weapon, spell, item, talent... - into wC9AD-C9BE. Tables in bank 9, lookups in bank 0B.
static void actionData(BattleVM& vm, const Rom& rom) {
    const int type = (vm.rd(0xc99b) >> 4) & 0x0f;
    const uint8_t c = vm.rd(0xc99d);
    uint16_t hl;
    auto r = [&]() { return rom.b(9, hl); };
    auto f551e = [&]() {
        const uint8_t a = r();
        vm.wr(0xc9ad, rom.b(0x0b, uint16_t(0x7104 + ((a >> 4) & 0x0f))));
        const uint8_t lo = a & 0x0f;
        hl++;
        vm.wr(0xc9af, lo); vm.wr(0xc9b2, lo);
        vm.wr(0xc9ae, rom.b(0x0b, uint16_t(0x7114 + lo)));
        vm.wr(0xc9af, 0);
        if (uint8_t(lo - 1) < 2) vm.wr(0xc9af, vm.rd(0xc9ae));
    };
    auto f5557 = [&]() { vm.wr(0xc9b6, r()); hl++; };
    auto f555c = [&]() {
        const uint8_t a = r(); hl++;
        vm.wr(0xc9b3, a >> 6);
        vm.wr(0xc9bc, a & 0x3f);
        if ((a & 0x3f) >= 0x0a && (a & 0x3f) < 0x0d) vm.wr(0xc9af, 0);
    };
    auto f5575 = [&]() { vm.wr(0xc9bd, r()); hl++; };
    auto f557a = [&]() { const uint8_t a = r(); hl++; vm.wr(0xc9bb, a >> 6); vm.wr(0xc9be, a & 0x3f); };
    auto f558d = [&]() { vm.wr(0xc9ba, r()); hl++; };
    auto f5592 = [&]() { const uint8_t a = r(); hl++; vm.wr(0xc9b0, a >> 4); vm.wr(0xc9b1, a & 0x0f); };
    auto f55a1 = [&]() { vm.wr(0xc9b9, r()); hl++; };
    auto f55a6 = [&]() { const uint8_t a = r(); hl++; vm.wr(0xc9b4, a >> 4); vm.wr(0xc9b5, a & 0x0f); };
    auto f55b5 = [&]() { const uint8_t a = r(); hl++; vm.wr(0xc9b7, a >> 4); vm.wr(0xc9b8, a & 0x0f); };
    switch (type) {
    case 0: {                             // 5488: 7-byte records at 09:55C4 (index wraps like the GB)
        const uint8_t a1 = uint8_t(0xd0 + c), a2 = uint8_t(a1 * 2);
        hl = uint16_t(0x55c4 + a1 + a2 * 3);
        f551e(); f5557(); f555c(); f5575(); f557a(); f558d(); f5592();
        break;
    }
    case 1: {                             // 5501: 6-byte records at 09:57BC
        const uint8_t a = uint8_t(c * 2);
        hl = uint16_t(0x57bc + a * 3);
        f551e(); f5557(); f555c(); f5575(); f557a(); f55a1();
        break;
    }
    case 2: {                             // 54C7: 6-byte records at 09:5A50
        const uint8_t a = uint8_t(c * 2);
        hl = uint16_t(0x5a50 + a * 3);
        f551e(); f5557(); f555c(); f5575(); f557a(); f55a1();
        break;
    }
    case 3: {                             // 54E4: 5-byte records at 09:5B70
        const uint8_t a1 = uint8_t(0x40 + c), a = uint8_t(a1 * 5);
        hl = uint16_t(0x5b70 + a);
        f551e(); f5557(); f555c(); f5575(); f557a();
        break;
    }
    case 4: {                             // 54AB: 5-byte records at 09:5BFC
        const uint8_t a1 = uint8_t(0x88 + c), a2 = uint8_t(a1 * 2);
        hl = uint16_t(0x5bfc + a1 + a2 * 2);
        f551e(); f55b5(); f55a6(); f558d(); f5592();
        break;
    }
    default: break;                       // the table has 5 entries
    }
}

// Read through a bank mapping: $0000-3FFF bank 0, $4000-7FFF `bank`, RAM above.
static uint8_t rdb(const BattleVM& vm, const Rom& rom, int bank, uint16_t a) {
    if (a < 0x4000) return rom.b(0, a);
    if (a < 0x8000) return rom.b(bank, a);
    return vm.rd(a);
}

// 0B:6EB6: lowest / highest set bit of [hl] (+5 = combatant slot) -> wCE8F / wCE90
static void bitRange(BattleVM& vm, uint16_t hl) {
    const uint8_t c = vm.rd(hl);
    int lo = 0, hi = 7;
    if (c) {
        while (!(c & (1 << lo))) lo++;
        while (!(c & (1 << hi))) hi--;
    }
    vm.wr(0xce8f, uint8_t(lo + 5));
    vm.wr(0xce90, uint8_t(hi + 5));
}

// Inventory: 24 (id, count) pairs at wC6FF.
static void inventoryTake(BattleVM& vm) {          // 0B:706C: use up item wCE91
    for (int i = 0; i < 24; i++) {
        const uint16_t hl = uint16_t(0xc6ff + i * 2);
        if (vm.rd(hl) != vm.rd(0xce91)) continue;
        if (!vm.rd(uint16_t(hl + 1))) break;
        vm.wr(uint16_t(hl + 1), uint8_t(vm.rd(uint16_t(hl + 1)) - 1));
        return;
    }
    vm.wr(0xce91, 0xff);
}

static void inventoryAdd(BattleVM& vm) {           // 0B:7037: add item wCE94 (max 9 each)
    const uint8_t id = vm.rd(0xce94);
    for (int i = 0; i < 24; i++) {
        const uint16_t hl = uint16_t(0xc6ff + i * 2);
        if (vm.rd(hl) != id) continue;
        if (vm.rd(uint16_t(hl + 1)) >= 9) { vm.wr(0xce94, 0xff); return; }
        vm.wr(uint16_t(hl + 1), uint8_t(vm.rd(uint16_t(hl + 1)) + 1));
        return;
    }
    for (int i = 0; i < 24; i++) {
        const uint16_t hl = uint16_t(0xc6ff + i * 2);
        if (vm.rd(hl) == 0xff) { vm.wr(hl, id); vm.wr(uint16_t(hl + 1), 1); return; }
    }
    vm.wr(0xce94, 0xff);
}

// 00:1FC4: offset of the record of member wCD36 (party records, or battle records if wCD37)
static uint16_t memberOffset(const BattleVM& vm, const Rom& rom) {
    uint8_t a = vm.rd(0xcd36);
    if (a >= 0x80) a = uint8_t(a - 0x7b);
    const uint16_t t = uint16_t((vm.rd(0xcd37) ? 0x1fed : 0x1fe3) + uint8_t(a * 2));
    return uint16_t(rom.b(0, t) | rom.b(0, uint16_t(t + 1)) << 8);
}

// 09:5D7F / 09:5D87: what the item / spell / talent wCE26 is, for menus and the VM:
// copies its 7 data bytes to wCEDB and sets wCE28 (use kind) / wCE29.
static void copyRecord(BattleVM& vm, const Rom& rom, uint16_t src) {
    for (int i = 0; i < 7; i++) vm.wr(uint16_t(0xcedb + i), rom.b(9, uint16_t(src + i)));
}
static void itemInfo(BattleVM& vm, const Rom& rom);
static void itemInfo5d87(BattleVM& vm, const Rom& rom) {
    vm.wr(0xce28, 0); vm.wr(0xce29, 0);
    const uint8_t a = vm.rd(0xce26);
    if (a == 0xff) return;
    if (vm.rd(0xce25)) {                                       // 5DC4
        if (a >= 0x6e) return;
        copyRecord(vm, rom, uint16_t(0x57bc + a * 6));
        vm.wr(0xce28, vm.rd(0xcee0));
        return;
    }
    if (a < 0x30) {                                            // 5E3D: spells
        copyRecord(vm, rom, uint16_t(0x5a50 + a * 6));
        if (vm.rd(0xce27)) return;
        vm.wr(0xce28, vm.rd(0xcee0));
        uint8_t v = uint8_t((((a >> 1) | (a << 7)) & 7) * 2 + 4);
        if (a & 0x20) v = uint8_t(v * 2 - 8);
        vm.wr(0xce29, uint8_t(v * 2));
        return;
    }
    if (a < 0x78) {                                            // 5DD4: items
        copyRecord(vm, rom, uint16_t(0x55c4 + (a - 0x30) * 7));
        if (vm.rd(0xce27)) {                                   // 5E04
            vm.wr(0xce26, vm.rd(0xcee0));
            vm.wr(0xce27, 0);
            itemInfo(vm, rom);
            vm.wr(0xce29, 0);
            vm.wr(0xce26, a);
            copyRecord(vm, rom, uint16_t(0x55c4 + (a - 0x30) * 7));
            return;
        }
        uint8_t k;
        if (a < 0x5e) k = 0x81;
        else if (a < 0x62) k = 0x83;
        else if (a < 0x6e) k = 0x81;
        else if (a < 0x71) k = 0x84;
        else k = 0x83;
        vm.wr(0xce28, k);
        return;
    }
    if (a < 0xc0) {                                            // 5E1F
        copyRecord(vm, rom, uint16_t(0x5bfc + (a - 0x78) * 5));
        vm.wr(0xce26, vm.rd(0xcede));
        vm.wr(0xce27, 0);
        itemInfo(vm, rom);
        vm.wr(0xce29, 0);
        vm.wr(0xce26, a);
        copyRecord(vm, rom, uint16_t(0x5bfc + (a - 0x78) * 5));
        return;
    }
    const bool ok = vm.rd(0xcd37) ? (a == 0xd1 || a < 0xcb) : a < 0xd8;
    if (!ok) return;
    copyRecord(vm, rom, uint16_t(0x5b70 + (a - 0xc0) * 5));     // 5E61
    vm.wr(0xce28, 0x28);
}
static void itemInfo(BattleVM& vm, const Rom& rom) { itemInfo5d87(vm, rom); vm.wr(0xce27, 0); }

bool runLogicNative(BattleVM& vm, const Rom& rom, uint16_t addr) {
    switch (addr) {
    case 0x6b64: itemInfo(vm, rom); return true;
    case 0x6b61: {                        // 09:5D64: the item in the member's first slot
        vm.wr(0xce25, 0); vm.wr(0xce27, 0);
        const uint8_t a = vm.rd(uint16_t(memberOffset(vm, rom) + 0xc540));
        vm.wr(0xce26, a);
        if (a == 0xff) { vm.wr(0xce28, 0x81); return true; }
        itemInfo(vm, rom);
        return true;
    }
    case 0x6b4c: bitRange(vm, uint16_t(0xcc5e + uint8_t(vm.rd(0xce84) - 1))); return true;
    case 0x605d:
        vm.wr(0xce84, vm.rd(0xcc5e) | vm.rd(0xcc5f) | vm.rd(0xcc60));
        bitRange(vm, 0xce84);
        return true;
    case 0x6b2b:
        if (vm.rd(0xc9d6) >= 5) vm.wr(0xce91, 0xff);
        else inventoryTake(vm);
        return true;
    case 0x6b3b: inventoryAdd(vm); return true;
    case 0x6b52: {                        // 0B:7E89
        const uint8_t a = uint8_t(uint8_t(((vm.rd(0xc96e) - 1) & 0xfe) * 2) + vm.rd(0xc995));
        vm.wr(0xd888, rdb(vm, rom, 0x0b, uint16_t(vm.rd16(0xc97b) + a)));
        return true;
    }
    case 0x6b58: {                        // 0B:6947: which enemy group holds combatant wD87A
        const uint8_t n = uint8_t(vm.rd(0xd87a) - 5);
        const uint8_t m = n < 8 ? uint8_t(1 << n) : 0;
        uint8_t c = 0;
        while (c < 3 && !(m & vm.rd(uint16_t(0xcc5e + c)))) c++;
        vm.wr(0xd87a, c);
        return true;
    }
    case 0x6b41: {                        // 0B:701A: name of item/spell wCE8C -> wCE3C, 0-terminated
        const uint8_t id = vm.rd(0xce8c);
        if (id == 0xff) return true;
        uint16_t src; int n;
        if (id < 0x30) { src = uint16_t(0x6d9b + id * 6); n = 6; }
        else { src = uint16_t(0x6ebb + (id - 0x30) * 8); n = 8; }
        for (int i = 0; i < n; i++) vm.wr(uint16_t(0xce3c + i), rdb(vm, rom, 0x0e, uint16_t(src + i)));
        uint16_t hl = uint16_t(0xce3c + n);
        vm.wr(hl, 0xff);
        while (vm.rd(hl) == 0xff) hl--;
        vm.wr(uint16_t(hl + 1), 0);
        return true;
    }
    case 0x6b49: actionData(vm, rom); return true;
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
