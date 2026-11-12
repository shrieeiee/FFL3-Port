#include "battleanim.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace ffl3 {

namespace {
constexpr uint16_t kOam = 0xc000;
constexpr uint16_t kRow = 0xdc80;      // party row: counter, status mask, count, then 8 bytes / member
constexpr uint16_t kRowOam = 0xdc83;
constexpr uint16_t kSCY = 0xff42, kSCX = 0xff43, kLCDC = 0xff40, kBGP = 0xff47, kOBP0 = 0xff48;
}

BattleAnim::BattleAnim(const Rom& rom, Mem& mem) : rom_(rom), mem_(mem) {}

uint8_t BattleAnim::rb(uint16_t a) const { return a < 0x4000 ? rom_.b(0, a) : rom_.b(2, a); }

void BattleAnim::copyRom(int bank, uint16_t src, uint16_t dst, int n) {
    for (int i = 0; i < n; i++) mem_[uint16_t(dst + i)] = rom_.b(bank, uint16_t(src + i));
}

void BattleAnim::start(uint16_t entry) {
    root_ = entry == 0x4000 ? attack() : effect(mem_[0xde00]);
    cur_ = root_.h;
    wait_ = 0;
    cur_.resume();                     // runs up to its first wait, in the frame it was called
}

void BattleAnim::frame() {
    for (int i = 0; i < 160; i++) oam_[size_t(i)] = mem_[uint16_t(kOam + i)];   // OAM DMA
    vblank();
    blank = blankNext_;
    blankNext_ = false;
    topBgp = mem_[kBGP];
    topObp = mem_[kOBP0];
    if (!busy()) return;
    if (wait_ > 0 && --wait_ > 0) return;
    cur_.resume();
}

// 00:3E26 (vblank): every 16 frames each member who can act swaps the halves of his sprite and
// mirrors them (the walking-on-the-spot animation); then the row goes to the OAM shadow at wC078.
void BattleAnim::vblank() {
    if (!mem_[0xcbff]) return;
    mem_[kRow] = uint8_t((mem_[kRow] + 1) & 15);
    const int n = mem_[kRow + 2];
    if (mem_[kRow] == 0) {
        uint8_t c = mem_[kRow + 1];
        for (int i = 0; i < n; i++, c >>= 1) {
            if (c & 1) continue;
            const uint16_t p = uint16_t(0xdc85 + i * 8);
            std::swap(mem_[p], mem_[uint16_t(p + 4)]);
            const uint8_t a = mem_[uint16_t(p + 5)] ^ 0x20;
            mem_[uint16_t(p + 5)] = a;
            mem_[uint16_t(p + 1)] = a;
        }
    }
    for (int i = 0; i < n * 8; i++) mem_[uint16_t(0xc078 + i)] = mem_[uint16_t(kRowOam + i)];
}

void BattleAnim::loadMemberTiles(const std::function<int(int)>& spritePage) {
    const int n = (mem_[0xd203] & 0x80) ? 4 : 5;
    for (int i = 0; i < n; i++) {
        const uint16_t rec = uint16_t(0xd000 + i * 0x80);
        const bool status = (mem_[uint16_t(rec + 1)] & 0xfc) | (mem_[uint16_t(rec + 2)] & 0xe0);
        const uint16_t dst = uint16_t(0x86c0 + i * 0x40);
        copyRom(4, uint16_t(spritePage(i) << 8 | 0x40), dst, 0x40);
        if (status)                                    // 0B:7E4F: only the high bit plane is kept
            for (int k = 0; k < 0x40; k += 2) mem_[uint16_t(dst + k)] = 0;
    }
}

void BattleAnim::clearOam() {                          // 02:4FC5: the 30 effect sprites
    for (int i = 0; i < 0x78; i++) mem_[uint16_t(kOam + i)] = 0;
}

// ---- party row ----

void BattleAnim::setMemberY(int i, uint8_t y) {        // 02:40F0
    const uint16_t p = uint16_t(kRowOam + (i * 8 & 0xff));
    mem_[p] = y;
    mem_[uint16_t(p + 4)] = y;
}

void BattleAnim::setMemberX(int i, uint8_t x) {        // 02:40DB
    const uint16_t p = uint16_t(kRowOam + (i * 8 & 0xff));
    mem_[uint16_t(p + 1)] = x;
    mem_[uint16_t(p + 5)] = uint8_t(x + 8);
}

// 02:4171: bit i set = member i has one of the status bits e (record +1) or d (record +2).
uint8_t BattleAnim::memberMask(uint8_t e, uint8_t d) {
    uint8_t c = 0;
    for (int i = 0; i < 5; i++) {
        const uint16_t r = uint16_t(0xd001 + i * 0x80);
        const bool f = (mem_[r] & e) || (mem_[uint16_t(r + 1)] & d);
        c = uint8_t(c >> 1 | (f ? 0x80 : 0));
    }
    return uint8_t(c >> 3);
}

void BattleAnim::statusMask() { mem_[kRow + 1] = memberMask(0xc0, 0xc0); }   // 02:4167

AnimTask BattleAnim::partyRowInit() {                  // 02:4110
    const uint8_t b = mem_[0xddff];
    uint16_t de = uint16_t(0x4139 + (b & 1) * 4);
    mem_[kRow] = 0;
    mem_[kRow + 1] = 0;
    mem_[kRow + 2] = b;
    uint16_t hl = kRowOam;
    uint8_t c = 0x36;
    auto entry = [&](uint8_t x) {
        mem_[hl++] = 0x58;
        mem_[hl++] = x;
        mem_[hl++] = uint8_t(c++ * 2);
        mem_[hl++] = 0;
    };
    for (int i = 0; i < b; i++, de++) { entry(rb(de)); entry(uint8_t(rb(de) + 8)); }
    statusMask();
    mem_[0xcbff] = 1;
    co_await wait(1);
}

AnimTask BattleAnim::partyRowClear() {                 // 02:4142
    for (int i = 0; i < 40; i++) mem_[uint16_t(kRowOam + i)] = 0;
    co_await wait(1);
    mem_[0xcbff] = 0;
}

// 02:4023: the target member steps with the table while a monster attacks him.
AnimTask BattleAnim::bob(uint16_t table) {
    if (mem_[0xc9e6] == 0xff) co_return;
    for (int k = 0; k < 4; k++) {
        const uint8_t t = mem_[0xc9d6];
        if (t >= 5) co_return;
        setMemberY(t, rb(uint16_t(table + k)));
        co_await wait(2);
    }
}

// 02:40A7: a member using something on another: the target steps over next to him and back.
AnimTask BattleAnim::partySwap() {
    uint8_t c = 0x10;
    const uint8_t e = mem_[0xde10], d = mem_[0xde01];
    if (d < e) c = 0xf0;
    const uint8_t b = mem_[uint16_t(kRowOam + (d * 8 & 0xff) + 1)];
    setMemberX(e, uint8_t(b - c));
    co_await wait(3);
    setMemberX(d, uint8_t(c + b));
    setMemberX(e, b);
    co_await wait(3);
    co_await wait(3);
}

// 02:4000: an action's animation. wC9BD bit 7 clear: just the effect wC9BD.
AnimTask BattleAnim::attack() {
    const uint8_t fx = mem_[0xc9bd];
    if (!(fx & 0x80)) { co_await effect(fx); co_return; }
    mem_[0xde00] = fx & 0x7f;
    const uint8_t actor = mem_[0xc9d8];
    mem_[0xde01] = actor;
    if (actor >= 5) {                                  // a monster: sprite script over it
        co_await bob(0x4101);
        co_await script();
        co_await bob(0x4105);
        co_return;
    }
    mem_[0xde10] = actor;                              // 02:403D: a member steps forward
    if (!mem_[0xcbff] || !mem_[0xde00]) co_return;
    const uint8_t b = mem_[0xc99c];
    if (b & 0x80) {                                    // everybody who can act
        const uint8_t mask = memberMask(0xc0, 0x00);
        const int n = mem_[kRow + 2];
        for (int k = 0; k < 7; k++) {
            uint8_t c = mask;
            for (int i = 0; i < n; i++, c >>= 1)
                if (!(c & 1)) setMemberY(i, rb(uint16_t(0x40fa + k)));
            co_await wait(3);
        }
        co_return;
    }
    const uint8_t target = mem_[0xc9f7];
    const bool partyTarget = target < 5;
    if (partyTarget && !(b & 0x40)) {
        mem_[0xde10] = target;
        co_await partySwap();
    }
    for (int k = 0; k < 7; k++) {
        setMemberY(mem_[0xde01], rb(uint16_t(0x40fa + k)));
        co_await wait(3);
    }
    if (partyTarget) co_await partyRowInit();
}

// ---- sprite scripts ----

// 02:42C0: where an effect is drawn, in tiles (x, y): over a monster's picture, or a member.
int BattleAnim::position(uint8_t& x, uint8_t& y) {
    const uint8_t a = mem_[0xde01];
    if (a >= 5) {
        const int s = a - 5;
        const uint8_t v = mem_[uint16_t(0xcc51 + s)];
        y = uint8_t(uint8_t((v & 15) - 1) >> 1);
        x = uint8_t(uint8_t(uint8_t((v >> 4) - 1) >> 1) - 1);
        x = uint8_t(x + mem_[uint16_t(0xcc41 + s * 2)]);
        y = uint8_t(y + mem_[uint16_t(0xcc42 + s * 2)]);
        return 0;
    }
    const uint8_t f = uint8_t(((mem_[0xddff] - 1) & 4) ^ 4);
    const uint8_t step = uint8_t((f >> 2) + 4);
    x = uint8_t(step * a);
    y = 7;
    return f;
}

// 02:4210: one frame of a script: a 6 x 5 grid of 8x16 sprites.
void BattleAnim::drawGrid(uint8_t code, uint8_t grid) {
    mem_[0xde03] = uint8_t(((code & 7) + 1) * 4);
    mem_[0xde04] = uint8_t((code << 1) & 0x70);
    uint8_t x, y;
    position(x, y);
    mem_[0xde05] = uint8_t(x * 8);
    mem_[0xde06] = uint8_t(y * 8);
    uint16_t hl = uint16_t(rb(uint16_t(0x5a95 + grid * 2)) | rb(uint16_t(0x5a96 + grid * 2)) << 8);
    uint16_t de = 0xde40;
    int b = 30;
    while (b > 0) {                                    // RLE: $80|n = n x next byte, $C0|n = n blanks
        uint8_t a = rb(hl++);
        if (!(a & 0x80)) { mem_[de++] = a; b--; continue; }
        int c = a & 0x1f;
        const uint8_t v = (a & 0x40) ? 0x1f : rb(hl++);
        do { mem_[de++] = v; b--; } while (b > 0 && --c > 0);
    }
    const uint8_t f = mem_[0xde04];
    if (f & 0x40) {                                    // 02:430B: upside down
        for (int r = 0; r < 2; r++)
            for (int c = 0; c < 6; c++) {
                uint8_t& p = mem_[uint16_t(0xde40 + r * 6 + c)];
                uint8_t& q = mem_[uint16_t(0xde40 + (4 - r) * 6 + c)];
                const uint8_t t = p;
                p = q ^ 0x40;
                q = t ^ 0x40;
            }
        for (int c = 0; c < 6; c++) mem_[uint16_t(0xde4c + c)] ^= 0x40;
    }
    if (f & 0x20) {                                    // 02:433B: mirrored
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++) {
                uint8_t& p = mem_[uint16_t(0xde40 + r * 6 + c)];
                uint8_t& q = mem_[uint16_t(0xde40 + r * 6 + 5 - c)];
                const uint8_t t = p;
                p = q ^ 0x20;
                q = t ^ 0x20;
            }
    }
    clearOam();
    uint16_t o = kOam;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 6; c++) {
            const uint8_t cell = mem_[uint16_t(0xde40 + r * 6 + c)];
            if ((cell & 0x1f) == 0x1f) continue;
            mem_[o++] = uint8_t(mem_[0xde06] + r * 8);
            mem_[o++] = uint8_t(mem_[0xde05] + c * 8);
            mem_[o++] = uint8_t(mem_[0xde02] + (cell & 0x1f));
            mem_[o++] = uint8_t((cell & 0x60) | (f & 0x10));
        }
}

// 02:41B1: load the script's OBJ tiles and play it.
AnimTask BattleAnim::script() {
    const uint8_t id = mem_[0xde00];
    copyRom(2, uint16_t(0x658c + rb(uint16_t(0x651c + id)) * 0x20), 0x8000, 0x200);
    copyRom(3, 0x79d4, 0x8200, 0x4a0);                 // 03:79C8: impact / common tiles
    mem_[0xde02] = 0;
    uint16_t p = uint16_t(rb(uint16_t(0x53ee + (id * 2 & 0xff))) | rb(uint16_t(0x53ef + (id * 2 & 0xff))) << 8);
    for (;;) {
        const uint8_t a = rb(p++);
        if (a == 0xff) break;
        if (a & 0x80) { mem_[0xde02] = uint8_t(a << 1); continue; }
        if (a & 0x40) { co_await effect(a); continue; }
        drawGrid(a, rb(p++));
        co_await wait(mem_[0xde03]);
    }
    clearOam();                                        // 02:45AE
    co_await wait(1);
}

// ---- screen effects ----

AnimTask BattleAnim::shake() {                         // 02:439F: SCX then SCY through 02:43C0
    for (int b = 0; b < 2; b++)
        for (uint16_t reg : {kSCX, kSCY})
            for (int i = 0; i < 8; i++) {
                co_await wait(1);
                mem_[reg] = rb(uint16_t(0x43c0 + i));
            }
    mem_[kSCX] = 0;
    mem_[kSCY] = 0;
}

AnimTask BattleAnim::lcdBlink() {                      // 02:43C8: LCD off / on in vblank, twice
    for (int b = 0; b < 2; b++) {
        blankNext_ = true;
        co_await wait(5);
    }
}

// 02:441A: next frame, lines 0-63 (the monsters) in palette `pal`, BG and OBJ 0.
AnimTask BattleAnim::flashTop(uint8_t pal) {
    co_await wait(1);
    topBgp = topObp = pal;
    mem_[kBGP] = mem_[kOBP0] = 0xd2;
}

AnimTask BattleAnim::fade(uint16_t table) {            // 02:440B: 3 palettes, 7 frames each
    for (int d = 0; d < 3; d++) {
        const uint8_t pal = rb(uint16_t(table + 1 + d));
        for (int e = 0; e < 7; e++) co_await flashTop(pal);
    }
}

AnimTask BattleAnim::vanish() {                        // 02:43EA: the monsters fade out
    co_await fade(0x4430);
    clearOam();
    co_await wait(1);
    mem_[kSCY] = 0x40;
}

AnimTask BattleAnim::vanishReturn() {                  // 02:43F8: ... and back
    co_await vanish();
    co_await wait(15);
    mem_[kSCY] = 0;
    co_await fade(0x4433);
}

AnimTask BattleAnim::bgFlash() {                       // 02:4BA9: BG palette cycle on the monsters
    for (int e = 0x30; e > 0; e--) {
        co_await wait(1);
        topBgp = rb(uint16_t(0x4bc9 + ((e & 0x0c) >> 2)));
    }
}

// ---- damage numbers (particles) ----

void BattleAnim::particlesInit(uint8_t speed) {        // 02:4E7C
    mem_[0xdefc] = speed;
    mem_[0xdefd] = mem_[0xdefe] = mem_[0xdeff] = 0;
    for (int i = 0; i < 0xf0; i++) mem_[uint16_t(0xdf00 + i)] = 0xff;
}

// 02:4E8C: a particle (age, lifetime, motion pointer, y, x, tile, attr) from wDE13-DE16.
void BattleAnim::particleAdd(uint16_t motion) {
    uint16_t hl = 0xdf00;
    while (mem_[hl] != 0xff) hl = uint16_t(hl + 8);
    mem_[hl++] = 0;
    mem_[hl++] = rb(motion++);
    mem_[hl++] = uint8_t(motion);
    mem_[hl++] = uint8_t(motion >> 8);
    for (int i = 0; i < 4; i++) mem_[hl++] = mem_[uint16_t(0xde13 + i)];
}

// 02:4ED2 (after its 1-frame wait): move every particle one step and draw them.
void BattleAnim::particlesStep() {
    clearOam();
    uint16_t de = kOam;
    int b = 30;
    for (int i = 0; i < 30; i++) {
        const uint16_t e = uint16_t(0xdf00 + i * 8);
        if (mem_[e] == 0xff) { b--; continue; }
        uint8_t a = uint8_t(mem_[e] + 1);
        if (a >= mem_[uint16_t(e + 1)]) { a = 0xff; b--; }
        mem_[e] = a;
        uint16_t bc = uint16_t(mem_[uint16_t(e + 2)] | mem_[uint16_t(e + 3)] << 8);
        mem_[uint16_t(e + 4)] = uint8_t(mem_[uint16_t(e + 4)] + rb(bc++));
        mem_[uint16_t(e + 5)] = uint8_t(mem_[uint16_t(e + 5)] + rb(bc++));
        if (mem_[0xdeff]) bc = uint16_t(bc - 2);
        mem_[uint16_t(e + 2)] = uint8_t(bc);
        mem_[uint16_t(e + 3)] = uint8_t(bc >> 8);
        for (int k = 4; k < 8; k++) mem_[de++] = mem_[uint16_t(e + k)];
    }
    mem_[0xdefd] = uint8_t(b);
}

// 02:4EB9: one particle frame; `more` false once they are all gone (and two frames after).
AnimTask BattleAnim::particlesFrame(bool& more) {
    co_await wait(1);
    particlesStep();
    co_await wait(mem_[0xdefc]);
    if (mem_[0xdefd]) { mem_[0xdefe] = 0xfe; more = true; co_return; }
    const uint8_t a = uint8_t(mem_[0xdefe] + 1);
    if (!a) { more = false; co_return; }
    mem_[0xdefe] = a;
    more = true;
}

// 02:4DAE: start the next number in wDE40 (one word per combatant, $80xx = none, $88xx = miss).
bool BattleAnim::spawnNumber() {
    uint8_t c = mem_[0xde09];
    const uint8_t b = mem_[0xde0a];
    if (c >= b) return false;
    uint16_t hl = uint16_t(0xde40 + c * 2);
    uint16_t v;
    for (;;) {
        c++;
        v = uint16_t(mem_[hl] | mem_[uint16_t(hl + 1)] << 8);
        hl = uint16_t(hl + 2);
        if ((v >> 8) != 0x80) break;
        if (c >= b) return false;
    }
    mem_[0xde09] = c;
    mem_[0xde0b]++;
    if ((v >> 8) == 0x88) {                            // "Miss"
        mem_[0xcd31] = 0x0c; mem_[0xcd30] = 0x0b; mem_[0xcd2f] = 0x0a; mem_[0xcd2e] = 0xff;
    } else {
        if (v & 0x8000) v = uint16_t(-v);
        mem_[0xde0c] = uint8_t(v); mem_[0xde0d] = uint8_t(v >> 8);
        // 00:2009 (a = 2): five digits ($B0+) at wCD2D, leading zeros blank, wCD2B/2C blank
        mem_[0xcd2b] = mem_[0xcd2c] = 0xb0;
        int n = v;
        const int div[5] = {10000, 1000, 100, 10, 1};
        for (int i = 0; i < 5; i++) { mem_[uint16_t(0xcd2d + i)] = uint8_t(0xb0 + n / div[i]); n %= div[i]; }
        for (uint16_t a = 0xcd2b; a < 0xcd31 && mem_[a] == 0xb0; a++) mem_[a] = 0xff;
    }
    // 02:4E20: where the number goes
    mem_[0xde01] = uint8_t(c - 1);
    uint8_t x, y;
    const int a = position(x, y);
    x = uint8_t((x + 4) * 8 + a);
    y = uint8_t((y + 2) * 8);
    for (uint16_t p = 0xcd2f; p < 0xcd32 && mem_[p] == 0xff; p++) x = uint8_t(x - 4);
    for (uint16_t p = 0xcd31; mem_[p] != 0xff; p--) {   // 02:4E01: digits right to left
        mem_[0xde13] = y;
        mem_[0xde14] = x;
        x = uint8_t(x - 8);
        mem_[0xde15] = uint8_t((mem_[p] & 0x0f) * 2);
        mem_[0xde16] = 0;
        particleAdd(0x4e4b);
    }
    return true;
}

// 02:4D45 (miss) / 4D65: the damage numbers of the last action, monsters / party in turn.
AnimTask BattleAnim::damage(bool miss) {
    if (miss) {
        for (int i = 0; i < 26; i++) mem_[uint16_t(0xde40 + i)] = 0x80;
        uint8_t t = mem_[0xc9f7];
        if (t == 0xff) t = mem_[0xc9d8];
        mem_[uint16_t(0xde40 + (t * 2 & 0xff))] = 0x88;
        mem_[uint16_t(0xde41 + (t * 2 & 0xff))] = 0x88;
    } else {
        for (int i = 0; i < 26; i++) mem_[uint16_t(0xde40 + i)] = mem_[uint16_t(0xd822 + i)];
    }
    // 03:78D6: 13 digit glyphs as 8x16 OBJs (blank top half) at $8000
    for (int g = 0; g < 13; g++)
        for (int k = 0; k < 32; k++)
            mem_[uint16_t(0x8000 + g * 32 + k)] = k < 16 ? 0 : rom_.b(3, uint16_t(0x78f8 + g * 16 + k - 16));
    particlesInit(1);
    const bool enemiesFirst = mem_[0xd873] >= 5;
    for (int pass = 0; pass < 2; pass++) {
        const bool enemies = (pass == 0) == enemiesFirst;
        mem_[0xde09] = enemies ? 5 : 0;
        mem_[0xde0a] = enemies ? 13 : 5;
        mem_[0xde0b] = 0;
        for (;;) {
            spawnNumber();
            if (!mem_[0xde0b]) break;
            bool more = true;
            for (int k = 0; k < 16; k++) co_await particlesFrame(more);
            if (!more) break;
        }
    }
}

// ---- death ----

// 02:4F20: a monster dies. Style 0 (02:4FA4): an 8x16 mask sprite over each 8x16 of the
// picture, its pattern dissolving over 32 frames; then the picture is cleared.
AnimTask BattleAnim::death() {
    const uint8_t t = mem_[0xc9d6];
    if (t < 5) co_return;
    const int s = t - 5;
    mem_[0xde01] = uint8_t(s);
    const uint8_t v = mem_[uint16_t(0xcc51 + s)];      // 02:529F
    mem_[0xde17] = uint8_t((v >> 4) + 1);
    mem_[0xde18] = uint8_t((v & 15) + 1);
    mem_[0xde19] = mem_[uint16_t(0xcc41 + s * 2)];
    mem_[0xde1a] = mem_[uint16_t(0xcc42 + s * 2)];
    const int style = mem_[0xcc5c] & 3;
    if (style != 0 && std::getenv("FFL3_ANIMLOG")) std::fprintf(stderr, "ANIM death style %d (drawn as style 0)\n", style);
    for (int i = 0; i < 32; i++) mem_[uint16_t(0x8000 + i)] = 0;
    // 02:4FD0: the mask sprites
    {
        const int b = mem_[0xde17], rows = int8_t(mem_[0xde18]) >> 1;
        uint8_t y = uint8_t((mem_[0xde1a] + 2) * 8);
        uint16_t o = kOam;
        for (int c = rows; c > 0; c--, y = uint8_t(y + 16)) {
            uint8_t x = uint8_t((mem_[0xde19] + 1) * 8);
            for (int k = 0; k < b; k++, x = uint8_t(x + 8)) {
                mem_[o++] = y;
                mem_[o++] = x;
                mem_[o++] = 0;
                mem_[o++] = (c & 1) ? 0 : 0x40;
            }
        }
    }
    auto upload = [&] { for (int i = 0; i < 32; i++) mem_[uint16_t(0x8000 + i)] = mem_[uint16_t(0xde40 + i)]; };
    for (int k = 8; k > 0; k--) {
        co_await wait(1);                              // 02:5009: next pattern
        const uint16_t src = uint16_t(0x52fe + ((k - 1) & 7) * 16);
        for (int i = 0; i < 16; i++) mem_[uint16_t(0xde40 + i)] = mem_[uint16_t(0xde50 + i)] = rb(uint16_t(src + i));
        upload();
        for (int r = 0; r < 3; r++) {                  // 02:503E: scroll it up a row
            co_await wait(1);
            const uint8_t a0 = mem_[0xde50], a1 = mem_[0xde51];
            for (int i = 0; i < 14; i++) mem_[uint16_t(0xde40 + i)] = mem_[uint16_t(0xde42 + i)];
            mem_[0xde4e] = a0;
            mem_[0xde4f] = a1;
            for (int i = 0; i < 16; i++) mem_[uint16_t(0xde50 + i)] = mem_[uint16_t(0xde40 + i)];
            upload();
        }
    }
    if (eraseEnemy) eraseEnemy(s);                     // 02:52C1
    clearOam();
}

// ---- dispatch ----

// 02:4397 through the table at 02:4364.
AnimTask BattleAnim::effect(int id) {
    id &= 0x3f;
    switch (id) {
    case 0x00: co_return;
    case 0x01: co_await vanish(); co_return;
    case 0x02: co_await vanishReturn(); co_return;
    case 0x03: co_await shake(); co_return;
    case 0x04: co_await lcdBlink(); co_return;
    case 0x0e: co_await bgFlash(); co_return;
    case 0x10: co_await damage(false); co_return;
    case 0x11: co_await damage(true); co_return;
    case 0x12: co_await death(); co_return;
    case 0x15:                                         // 02:4109
        co_await wait(1);
        mem_[kLCDC] = 0xe7;
        co_await partyRowInit();
        co_return;
    case 0x16: co_await partyRowClear(); co_return;
    case 0x17: statusMask(); co_return;
    default:
        // Not translated yet: the spell raster / particle effects (5-D), 4BCD (F), 4F3E (13),
        // 0E:7DBF (14). A short flash of the monsters stands in for them.
        if (std::find(unknownIds.begin(), unknownIds.end(), id) == unknownIds.end()) unknownIds.push_back(id);
        if (std::getenv("FFL3_ANIMLOG")) std::fprintf(stderr, "ANIM effect %02X not translated\n", id);
        for (int e = 16; e > 0; e--) {
            co_await wait(1);
            topBgp = rb(uint16_t(0x4bc9 + ((e & 0x0c) >> 2)));
        }
        co_return;
    }
}

} // namespace ffl3
