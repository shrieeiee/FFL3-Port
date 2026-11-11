#include "battlenatives.h"
#include <functional>

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

// ---- member records (0B:7349 / 0B:728B family) ---------------------------------------
// 0B:73F4: address of field `off` in the record of member wCD36.
static uint16_t recField(const BattleVM& vm, const Rom& rom, int off) {
    return uint16_t(0xc500 + memberOffset(vm, rom) + off);
}
static void itemInfoWith(BattleVM& vm, const Rom& rom, uint8_t ce25) {   // 0B:73A2 / 73A7
    const uint8_t keep = vm.rd(0xce25);
    vm.wr(0xce25, ce25);
    itemInfo(vm, rom);
    vm.wr(0xce25, keep);
}
static void info73b1(BattleVM& vm, const Rom& rom) {
    const uint8_t a = vm.rd(0xce26);
    if (a >= 0x70) vm.wr(0xcedb, rom.b(0x0b, uint16_t(0x73c9 + (a - 0x70))));
    else itemInfoWith(vm, rom, 1);
}
// 0B:7337 + 7403: OR the kind bits of the item in wCEDB into field `at` (3 bytes).
static void orKind(BattleVM& vm, const Rom& rom, uint16_t at) {
    vm.wr(at, uint8_t(vm.rd(at) | rom.b(0x0b, uint16_t(0x7104 + (vm.rd(0xcedb) >> 4)))));
    const uint16_t hl = uint16_t(at + 1);
    const uint8_t lo = vm.rd(0xcedb) & 0x0f, t = rom.b(0x0b, uint16_t(0x7114 + lo));
    if (lo >= 0x0a) { vm.wr(hl, t); vm.wr(uint16_t(hl + 1), uint8_t(vm.rd(uint16_t(hl + 1)) | 0xe0)); }
    else if (lo < 7) vm.wr(hl, uint8_t(vm.rd(hl) | t));
    else vm.wr(uint16_t(hl + 1), uint8_t(vm.rd(uint16_t(hl + 1)) | t));
}
static void rec7349(BattleVM& vm, const Rom& rom) {
    const uint16_t r58 = recField(vm, rom, 0x58);
    vm.wr(r58, 0);
    for (int i = 0; i < 8; i++) {
        const uint8_t a = vm.rd(recField(vm, rom, 0x48 + i));
        if (a < 0x86 || a >= 0x90) continue;
        vm.wr(0xce26, a);
        info73b1(vm, rom);
        vm.wr(r58, uint8_t(vm.rd(r58) | rom.b(0x0b, uint16_t(0x7104 + (vm.rd(0xcedb) >> 4)))));
    }
}
static void rec72e1(BattleVM& vm, const Rom& rom) {
    const uint16_t f = recField(vm, rom, 0x36);
    gbFill(vm, f, 0, 3);
    for (int i = 0; i < 7; i++) {
        const uint8_t a = vm.rd(uint16_t(f + 0x0b + i));
        if (a == 0xff) continue;
        vm.wr(0xce26, a);
        itemInfoWith(vm, rom, 0);
        orKind(vm, rom, f);
    }
}
static void rec7305(BattleVM& vm, const Rom& rom) {
    const uint16_t f = recField(vm, rom, 0x39);
    gbFill(vm, f, 0, 3);
    for (int i = 0; i < 8; i++) {
        const uint8_t a = vm.rd(uint16_t(f + 0x0f + i));
        if (a < 0x70 || a >= 0x86) continue;
        vm.wr(0xce26, a);
        info73b1(vm, rom);
        orKind(vm, rom, f);
    }
}
static void rec7422(BattleVM& vm, const Rom& rom) {
    const uint16_t f = recField(vm, rom, 0x3d);
    gbFill(vm, f, 0, 3);
    const uint8_t keep = vm.rd(0xce26);
    for (int i = 0; i < 7; i++) {
        const uint8_t a = vm.rd(recField(vm, rom, 0x41 + i));
        if (a == 0xff) continue;
        vm.wr(0xce26, a);
        itemInfoWith(vm, rom, 0);
        vm.wr(f, uint8_t(vm.rd(f) + (vm.rd(0xcedd) >> 4)));
        vm.wr(uint16_t(f + 1), uint8_t(vm.rd(uint16_t(f + 1)) + (vm.rd(0xcedc) & 0x0f)));
        vm.wr(uint16_t(f + 2), uint8_t(vm.rd(uint16_t(f + 2)) + (vm.rd(0xcedd) & 0x0f)));
    }
    vm.wr(0xce26, keep);
    itemInfoWith(vm, rom, 0);
}
static void rec728b(BattleVM& vm, const Rom& rom) {
    rec72e1(vm, rom);
    rec7305(vm, rom);
    if ((vm.rd(recField(vm, rom, 0x0d)) & 7) != 4) {
        rec7422(vm, rom);
        for (int i = 0; i < 3; i++) {                                  // 72C0
            const uint16_t a = recField(vm, rom, 0x39 + i);
            vm.wr(a, uint8_t(vm.rd(a) | vm.rd(recField(vm, rom, 0x36 + i))));
        }
    }
    const uint16_t r1 = recField(vm, rom, 0x01);
    vm.wr(r1, uint8_t(vm.rd(r1) & ~vm.rd(recField(vm, rom, 0x3a))));
    const uint16_t r58 = recField(vm, rom, 0x58);
    vm.wr(r58, uint8_t(vm.rd(r58) & (~vm.rd(recField(vm, rom, 0x39)) | 0x0f)));
}

// 0B:6E74: member wCD36 learns spell bit wCE94 (16-bit words at record +$51)
static void learnBit(BattleVM& vm, const Rom& rom) {
    const uint8_t n = vm.rd(0xce94);
    const uint16_t hl = uint16_t(memberOffset(vm, rom) + 0xc551 + 2 * (n / 16));
    uint16_t w = uint16_t(vm.rd(hl) << 8 | vm.rd(uint16_t(hl + 1)));    // d:e
    const int b = n % 16;
    auto rot = [&](int k) { for (int i = 0; i < k; i++) w = uint16_t(w >> 1 | (w & 1) << 15); };
    rot(b);
    w |= 1;
    rot(16 - b);
    vm.wr(uint16_t(hl + 1), uint8_t(w));
    vm.wr(hl, uint8_t(w >> 8));
}

// ---- monster record at wD680 (0B:451C / 4526 / 453A / 45CB / 466B) --------------------
static void scaleHp(BattleVM& vm, uint16_t de) {                    // 0B:45AB (de -> hi byte)
    uint16_t hl = uint16_t(vm.rd(uint16_t(de - 1)) | vm.rd(de) << 8);
    if (vm.rd(0xd68e) >= 0xbb) {
        hl = uint16_t(hl << 4);
        vm.wr(uint16_t(de - 1), uint8_t(hl)); vm.wr(de, uint8_t(hl >> 8));
    }
    vm.wr(uint16_t(de + 1), uint8_t(hl)); vm.wr(uint16_t(de + 2), uint8_t(hl >> 8));
}
static void monsterStats(BattleVM& vm, const Rom& rom) {              // 0B:453A
    const uint8_t id = vm.rd(0xcc70);
    vm.wr(0xd68e, id);
    uint16_t hl = uint16_t(0x4a19 + id * 12);
    auto r = [&]() { return rom.b(0x0b, hl++); };
    vm.wr(0xd68d, r() & 7);
    vm.wr(0xd690, r());
    uint8_t a = r(); vm.wr(0xcc73, a); vm.wr(0xd691, a & 0x0f); scaleHp(vm, 0xd691);
    vm.wr(0xd694, r());
    a = r(); vm.wr(0xcc74, a); vm.wr(0xd695, a & 0x0f); scaleHp(vm, 0xd695);
    for (int i = 0; i < 4; i++) { const uint8_t v = rom.b(0x0b, uint16_t(hl + i)); vm.wr(uint16_t(0xd698 + i), v); vm.wr(uint16_t(0xd6f0 + i), v); }
    hl = uint16_t(hl + 4);
    for (int i = 0; i < 3; i++) vm.wr(uint16_t(0xd6bd + i), r());
    const uint16_t t1 = uint16_t(0x469d + ((vm.rd(0xcc73) & 0xf0) >> 2));
    for (int i = 0; i < 4; i++) vm.wr(uint16_t(0xd6d9 + i), rom.b(0x0b, uint16_t(t1 + i)));
    const uint16_t t2 = uint16_t(0x46dd + ((vm.rd(0xcc74) >> 4) & 0x0f) * 3);
    for (int i = 0; i < 3; i++) vm.wr(uint16_t(0xd6dd + i), rom.b(0x0b, uint16_t(t2 + i)));
}
static void monsterRest(BattleVM& vm, const Rom& rom) {               // 0B:45CB
    vm.wr(0xd680, 0x0f); vm.wr(0xd681, 0); vm.wr(0xd682, 0);
    gbFill(vm, 0xd6d1, 0, 6);
    gbFill(vm, 0xd6ea, 0, 6);
    const uint8_t id = vm.rd(0xcc70);
    uint16_t hl = uint16_t(0x533d + id * 9), de = 0xd6c8;
    uint8_t b = rom.b(0x0b, hl++);
    for (int c = 8; c > 0; c--) {
        const uint8_t a = rom.b(0x0b, hl);
        const bool bit = b & 0x80;
        b = uint8_t(b << 1);
        if (a == 0xff) continue;
        if (bit) { vm.wr(de++, a); hl++; continue; }
        hl++;
        if (a >= 0x30) vm.wr(0xd6c0, a);
        else { vm.wr(0xce94, a); vm.wr(0xcd36, 0x0d); learnBit(vm, rom); }
    }
    vm.wr(0xcd36, 0x0d);
    rec7349(vm, rom);
    rec728b(vm, rom);
    hl = uint16_t(0x470d + id * 4);
    const uint8_t d0 = rom.b(0x0b, hl++);
    vm.wr(0xd683, d0 & 0x3f);
    vm.wr(0xd68f, d0 >> 6); vm.wr(0xd6e8, d0 >> 6);
    vm.wr(0xd6d7, uint8_t((uint8_t(vm.rd(0xd69a) + (d0 & 0x3f)) >> 1) + 0x4b));
    for (int i = 0; i < 3; i++) vm.wr(uint16_t(0xd6e5 + i), rom.b(0x0b, uint16_t(hl + i)));
}
static void monsterNew(BattleVM& vm, const Rom& rom) {                // 0B:4526
    gbFill(vm, 0xd690, 0xff, 0x70);
    const uint8_t id = vm.rd(0xcc70);
    for (int i = 0; i < 8; i++) vm.wr(uint16_t(0xd685 + i), rom.b(0x0e, uint16_t(0x7533 + id * 8 + i)));
    monsterStats(vm, rom);
}
static void monster466b(BattleVM& vm) {
    uint16_t hl = 0xd6c8;
    for (int b = 8; b > 0; b--) {
        uint16_t de = hl;
        const uint8_t a = vm.rd(hl++);
        if (a != 0x6a) continue;
        uint16_t src = hl;
        gbCopy(vm, src, de, b - 1);
        vm.wr(de, 0xff);
    }
}
// 0B:708B: spell bits from items (record +$51-56 -> +$6A..) for combatants 0..11
static void spellBits(BattleVM& vm, const Rom& rom) {
    for (int m = 0; m <= 12; m++) {
        vm.wr(0xcd36, uint8_t(m));
        const uint16_t base = uint16_t(0xc500 + rom.b(0, uint16_t((vm.rd(0xcd37) ? 0x1fed : 0x1fe3))) +
                                       (rom.b(0, uint16_t((vm.rd(0xcd37) ? 0x1fed : 0x1fe3) + 1)) << 8) + m * 0x80 + 0x51);
        int c = 0;
        static const int order[6] = {1, 0, 3, 2, 5, 4};
        for (int k : order) {
            const uint16_t at = uint16_t(base + k);
            for (int bit = 0; bit < 8; bit++, c++) {
                const uint8_t v = vm.rd(at);
                vm.wr(at, uint8_t(v >> 1 | v << 7));                    // rrc [hl]
                if (!(v & 1)) continue;
                const uint8_t t = rom.b(0x0b, uint16_t(0x7543 + c));
                const uint16_t h = uint16_t(memberOffset(vm, rom) + 0xc56a + (t >> 3));
                vm.wr(h, uint8_t(vm.rd(h) | (1 << (7 - (t & 7)))));
                if (!(t >> 3)) vm.wr(h, vm.rd(h) & 0x1f);
            }
        }
    }
    vm.wr(0xcd36, 0x0c);
}

void recomputeMember(BattleVM& vm, const Rom& rom, int member) {
    vm.wr(0xcd37, 0);
    vm.wr(0xcd36, uint8_t(member));
    rec7349(vm, rom);
    rec728b(vm, rom);
}

void battleSetupCombatants(BattleVM& vm, const Rom& rom) {
    gbFill(vm, 0xd000, 0xff, 0x680);
    for (int i = 0; i < 5; i++) {
        if (vm.rd(uint16_t(0xc503 + i * 0x60)) == 0xff) continue;
        uint16_t hl = uint16_t(0xc500 + i * 0x60), de = uint16_t(0xd000 + i * 0x80);
        gbCopy(vm, hl, de, 0x60);
    }
    for (int i = 0; i < 5; i++) {
        if (vm.rd(uint16_t(0xc518 + i * 0x60)) == 0xff) continue;
        uint16_t hl = uint16_t(0xc518 + i * 0x60), de = uint16_t(0xd070 + i * 0x80);
        gbCopy(vm, hl, de, 4);
    }
    for (int i = 0; i < 5; i++) gbFill(vm, uint16_t(0xd06a + i * 0x80), 0, 6);
    uint16_t de = 0xd280;
    for (int g = 0; g < 3; g++) {
        const int n = vm.rd(uint16_t(0xcc36 + g));
        if (g && !n) continue;
        vm.wr(0xcc70, vm.rd(uint16_t(0xcc59 + g)));
        monsterNew(vm, rom);
        monsterRest(vm, rom);
        for (int k = 0; k < (n ? n : 256); k++) { uint16_t hl = 0xd680; gbCopy(vm, hl, de, 0x80); }
        vm.wr(0xcc71, uint8_t(de)); vm.wr(0xcc72, uint8_t(de >> 8));
    }
    spellBits(vm, rom);
}

// 00:2499: wCD46-49 = (wCD48-49) * (old wCD46-47), 32-bit
static void mul2499(BattleVM& vm) {
    const uint32_t de = uint32_t(vm.rd(0xcd46) | vm.rd(0xcd47) << 8);
    const uint32_t hi = uint32_t(vm.rd(0xcd48) | vm.rd(0xcd49) << 8);
    const uint32_t r = hi * de;
    for (int i = 0; i < 4; i++) vm.wr(uint16_t(0xcd46 + i), uint8_t(r >> (8 * i)));
}
// 00:24C7: wCD46-49 / (old wCD4A-4B): quotient in CD46-49, remainder in CD4A-4B
static void div24c7(BattleVM& vm) {
    const uint16_t div = uint16_t(vm.rd(0xcd4a) | vm.rd(0xcd4b) << 8);
    uint64_t v = 0;
    for (int i = 0; i < 4; i++) v |= uint64_t(vm.rd(uint16_t(0xcd46 + i))) << (8 * i);
    for (int b = 0; b < 32; b++) {
        const bool top = (v >> 47) & 1;
        v = (v << 1) & 0xffffffffffffull;
        const uint16_t rem = uint16_t(v >> 32);
        if (top || rem >= div) {
            v |= 1;
            v = (v & 0xffffffffull) | uint64_t(uint16_t(rem - div)) << 32;
        }
    }
    for (int i = 0; i < 6; i++) vm.wr(uint16_t(0xcd46 + i), uint8_t(v >> (8 * i)));
}
// 0F:79D4 (native 1E46): EXP needed for the next level of members 0-3 -> wCD9D + 3i
static void expTable(BattleVM& vm, const Rom& rom) {
    for (int i = 0; i < 4; i++) {
        gbFill(vm, 0xcd46, 0, 3);
        const uint16_t t = uint16_t((vm.rd(0xcd37) ? 0x1fed : 0x1fe3) + uint8_t(i * 2));
        const uint16_t off = uint16_t(rom.b(0, t) | rom.b(0, uint16_t(t + 1)) << 8);
        const uint8_t lv = vm.rd(uint16_t(0xc503 + off));
        if (lv != 0x63) {
            vm.wr(0xcd46, uint8_t(lv + 4));
            const uint8_t a = uint8_t(lv + 1);
            const uint16_t sq = uint16_t(uint32_t(a) * a);           // 00:246B (16-bit)
            vm.wr(0xcd48, uint8_t(sq)); vm.wr(0xcd49, uint8_t(sq >> 8));
            mul2499(vm);
        }
        for (int k = 0; k < 3; k++) vm.wr(uint16_t(0xcd9d + i * 3 + k), vm.rd(uint16_t(0xcd46 + k)));
    }
}

bool runLogicNative(BattleVM& vm, const Rom& rom, uint16_t addr) {
    switch (addr) {
    case 0x2499: mul2499(vm); return true;
    case 0x24c7: div24c7(vm); return true;
    case 0x1e46: expTable(vm, rom); return true;
    case 0x6b25:                          // 0B:44F0: monster wC91B into wD680
        vm.wr(0xcc70, vm.rd(0xc91b));
        gbFill(vm, 0xd690, 0xff, 0x70);
        monsterStats(vm, rom);
        monsterRest(vm, rom);
        monster466b(vm);
        return true;
    case 0x6b28:
        vm.wr(0xcc70, vm.rd(0xc91b));
        gbFill(vm, 0xd690, 0xff, 0x70);
        monsterStats(vm, rom);
        return true;
    case 0x6b44: rec7349(vm, rom); rec728b(vm, rom); return true;
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

// ---- field item / magic use (bank 9) ---------------------------------------------------
namespace {
struct FieldUse {
    BattleVM& vm; const Rom& rom; const std::function<uint8_t()>& rand8; const std::function<void(int)>& sfx;
    uint16_t rec(int off) { return uint16_t(0xc500 + memberOffset(vm, rom) + off); }
    uint16_t w(uint16_t a) { return uint16_t(vm.rd(a) | vm.rd(uint16_t(a + 1)) << 8); }
    void setW(uint16_t a, uint16_t v) { vm.wr(a, uint8_t(v)); vm.wr(uint16_t(a + 1), uint8_t(v >> 8)); }
    bool dead() { return vm.rd(rec(1)) & 0x40; }                       // 09:52F9
    uint8_t amount() {                                                 // 09:536D
        sfx(0x17);
        uint8_t b = vm.rd(0xcedc) >> 1, a = rand8();
        if (!b) a = 0;
        while (a >= b && a) a >>= 1;
        return uint8_t(b + a);
    }
    void healTo(uint16_t heal) {                                       // 09:5420 / 545A
        const uint16_t hp = w(rec(0x10)), max = w(rec(0x12));
        const uint16_t sum = uint16_t(hp + heal);
        setW(rec(0x10), sum > max ? max : sum);
    }
    uint16_t mul(uint8_t a, uint16_t de) {                             // 00:246B
        uint16_t hl = 0;
        for (int i = 0; i < 8; i++) { if (a & 1) hl = uint16_t(hl + de); a = uint8_t(a >> 1 | a << 7); de = uint16_t(de << 1); }
        return hl;
    }
    void healPercent() { healTo(uint16_t(mul(vm.rd(0xcedc), w(rec(0x12))) / 10)); }   // 09:542C
    void healFixed() { healTo(mul(vm.rd(0xcedc), 10)); }                               // 09:53F4
    void cureAll() { sfx(0x18); vm.wr(rec(1), 0); }                                    // 09:53D1
    void statUp(int de) {                                              // 09:5303-5310
        const uint8_t c = vm.rd(rec(0x0d));
        const uint16_t at = rec(0x0d + de);
        uint8_t b = amount();
        uint8_t a = uint8_t(b + vm.rd(at));
        vm.wr(at, a);
        if (a >= 0x64) { vm.wr(at, 0x63); b = uint8_t(b - uint8_t(a - 0x63)); }
        if (c & 7) return;
        const uint16_t cur = uint16_t(at - 0x1a);
        vm.wr(cur, uint8_t(vm.rd(cur) + b));
    }
    void maxHpUp() {                                                   // 09:5338
        const uint16_t at = rec(0x30);
        uint8_t b = amount();
        const unsigned lo = vm.rd(at) + b;
        vm.wr(at, uint8_t(lo));
        const uint8_t hi = uint8_t(vm.rd(uint16_t(at + 1)) + (lo >> 8));
        vm.wr(uint16_t(at + 1), hi);
        if (hi > 3 || (hi == 3 && uint8_t(lo) >= 0xe8)) {               // 5360: cap at 999
            b = uint8_t(b + uint8_t(0xe7 - uint8_t(lo)));
            vm.wr(at, 0xe7); vm.wr(uint16_t(at + 1), 0x03);
        }
        if (vm.rd(rec(0x0d)) & 7) return;
        setW(rec(0x12), uint16_t(w(rec(0x12)) + b));                    // 00:1D5A -> 0C:7F9E
        setW(rec(0x10), uint16_t(w(rec(0x10)) + b));
    }
    bool effect() {                                                    // 09:52CF
        uint8_t e = uint8_t((vm.rd(0xcedd) & 0x3f) - 0x18);
        if (e >= 5) e = uint8_t(e - 5);
        switch (e) {
        case 0:                                                        // 5395
            if (vm.rd(0xce26) == 0xca) { setW(rec(0x14), w(rec(0x16))); cureAll(); healPercent(); return true; }
            if (dead()) return true;
            sfx(0x17); healPercent(); return true;
        case 1: {                                                      // 53B9: cure a status
            sfx(0x18);
            const uint8_t x = vm.rd(0xcedb) & 0x0f;
            const uint8_t v = rom.b(0x0b, uint16_t(0x7114 + x));      // 0B:7400
            const uint8_t mask = (x < 7 || x >= 0x0a) ? v : 0;
            vm.wr(rec(1), uint8_t(vm.rd(rec(1)) & ~mask));
            return true;
        }
        case 2: sfx(0x3b); return false;                               // 1E83
        case 3: cureAll(); return true;                                // 53D1: revive
        case 4:                                                        // 53DF
            if (dead()) return true;
            sfx(0x17);
            if (vm.rd(0xce25) || vm.rd(0xce26) >= 0x30) healFixed(); else healPercent();
            return true;
        case 5: statUp(0x25); return true;
        case 6: statUp(0x26); return true;
        case 7: statUp(0x27); return true;
        case 8: maxHpUp(); return true;
        case 9: sfx(0x17); setW(rec(0x14), w(rec(0x16))); return true; // 5383
        default: return false;
        }
    }
};
}

bool fieldUse(BattleVM& vm, const Rom& rom, const std::function<uint8_t()>& rand8,
              const std::function<void(int)>& sfx) {
    FieldUse u{vm, rom, rand8, sfx};
    vm.wr(0xcd37, 0);
    itemInfo(vm, rom);                                                 // the menu's 09:5D7F
    if (!vm.rd(0xce25)) {                                              // 09:521A
        const uint8_t a = vm.rd(0xce26);
        if (a < 0x30) {                                                // 52B7: pay the MP
            const uint8_t keep = vm.rd(0xcd36);
            vm.wr(0xcd36, vm.rd(0xce1c));
            const uint16_t mp = u.rec(0x14);
            u.setW(mp, uint16_t(u.w(mp) - vm.rd(0xce29)));
            vm.wr(0xcd36, keep);
        } else if (a < 0xc0) {
            vm.wr(0xce26, a < 0x78 ? vm.rd(0xcee0) : vm.rd(0xcede));
            vm.wr(0xce27, 0);
        }
    }
    itemInfo(vm, rom);                                                 // rst18 2A = 09:5D7F
    const uint8_t e = vm.rd(0xcedd) & 0x3f;
    bool party = false;
    if (e >= 0x27) { sfx(0x3b); return false; }
    if (e < 0x22) {
        if (e < 0x18 || e >= 0x1d) { sfx(0x3b); return false; }
        party = vm.rd(0xcd36) & 0x80;
    }
    if (!party) return u.effect();
    const int n = vm.rd(0xc683) == 0xff ? 4 : 5;                       // 0B:6D12: guest?
    bool ok = true;
    for (int i = 0; i < n; i++) { vm.wr(0xcd36, uint8_t(i)); ok = u.effect() && ok; }
    return ok;
}

} // namespace ffl3
