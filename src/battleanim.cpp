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
    writes_.clear();
    based_ = false;
    if (!busy()) return;
    if (wait_ > 0 && --wait_ > 0) return;
    cur_.resume();
}

void BattleAnim::atLine(int line, uint16_t r, uint8_t v) {
    if (!based_) {
        for (int i = 0; i < 12; i++) base_[size_t(i)] = mem_[uint16_t(0xff40 + i)];
        based_ = true;
    }
    writes_.push_back({line, r, v});
    mem_[r] = v;
}

uint8_t BattleAnim::reg(int line, uint16_t r) const {
    if (!based_) return mem_[r];
    uint8_t v = base_[size_t(r - 0xff40)];
    for (const LineWrite& w : writes_)
        if (w.r == r && w.line <= line) v = w.v;
    return v;
}

void BattleAnim::copy(uint16_t src, uint16_t dst, int n) {
    for (int i = 0; i < n; i++) mem_[uint16_t(dst + i)] = mem_[uint16_t(src + i)];
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
    mem_[kBGP] = mem_[kOBP0] = pal;
    atLine(64, kBGP, 0xd2);
    atLine(64, kOBP0, 0xd2);
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
        mem_[kBGP] = rb(uint16_t(0x4bc9 + ((e & 0x0c) >> 2)));
        atLine(64, kBGP, 0xd2);
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
    int style = mem_[0xcc5c] & 3;
    if (const char* ds = std::getenv("FFL3_DEATHSTYLE")) style = std::atoi(ds) & 3;   // debug
    if (style == 1) { co_await death1(); co_return; }
    if (style == 3) { co_await death3(); co_return; }
    if (style == 2) { co_await death2(); co_return; }
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
    clearRect();
    clearOam();
}

void BattleAnim::clearRect() {                         // 02:52C1 (02:52DD: $9800 + col + row*32)
    const int w = mem_[0xde17], h = mem_[0xde18];
    uint16_t hl = uint16_t(0x9800 + mem_[0xde19] + mem_[0xde1a] * 32);
    for (int r = 0; r < h; r++, hl = uint16_t(hl + 32))
        for (int c = 0; c < w; c++) mem_[uint16_t(hl + c)] = 0xff;
    if (eraseEnemy) eraseEnemy(mem_[0xde01]);
}

// 02:447F: wait a frame, then register r on lines 0..lines-1 from wDE40, back to 0 after.
void BattleAnim::rasterTable(uint16_t r, int lines) {
    for (int l = 0; l < lines; l++) atLine(l, r, mem_[uint16_t(0xde40 + l)]);
    atLine(lines, r, 0);
}

// 02:5055 (death style 1): the picture drops out in horizontal lines, SCY $40 per line.
AnimTask BattleAnim::death1() {
    for (int i = 0; i < 0x40; i++) mem_[uint16_t(0xde40 + i)] = 0;
    auto mirror = [&] {                                // 02:50C4
        copy(0xde40, 0xde48, 8); copy(0xde40, 0xde60, 16);
        copy(0xde50, 0xde58, 8); copy(0xde50, 0xde70, 16);
    };
    for (int k = 8; k > 0; k--) {
        co_await wait(1);                              // 02:5077
        rasterTable(kSCY, 65);
        uint8_t c = rb(uint16_t(0x5098 + k - 1));
        for (int b = 0; b < 8; b++, c >>= 1) {
            const uint8_t a = (c & 1) ? 0x40 : 0;
            mem_[uint16_t(0xde40 + b)] = a;
            mem_[uint16_t(0xde57 - b)] = a;
        }
        mirror();
        for (int t = 0; t < 3; t++) {                  // 02:50A6, three times
            co_await wait(1);
            rasterTable(kSCY, 65);
            const uint8_t a48 = mem_[0xde48], a5f = mem_[0xde5f];
            copy(0xde49, 0xde40, 7);
            mem_[0xde47] = a48;
            mem_[0xde50] = a5f;
            copy(0xde58, 0xde51, 7);
            mirror();
        }
    }
    for (int i = 0; i < 0x100; i++) mem_[uint16_t(0x9800 + i)] = 0xff;   // 02:51B6
    if (eraseEnemy) eraseEnemy(-1);
    co_await wait(1);
    mem_[kSCX] = mem_[kSCY] = 0;
}

// 02:51DE: 8 frames of BGP = table[wDE11 - 1] on lines 0-89.
AnimTask BattleAnim::flashLines(uint16_t table, int idx) {
    const uint8_t b = rb(uint16_t(table + idx - 1));
    for (int c = 0; c < 8; c++) {
        co_await wait(1);
        mem_[kBGP] = b;
        atLine(0x5a, kBGP, 0xd2);
    }
}

// 02:5167 (death style 3, the bosses): the picture bounces down, flashes, then melts tile
// by tile (each frame every tile slides down a row through a noise mask), fades and goes.
AnimTask BattleAnim::death3() {
    uint8_t b = 0, last = 0;
    do {
        sound(0x30);
        for (int c = 5; c > 0; c--) {
            co_await wait(2);
            last = uint8_t(rb(uint16_t(0x51ca + c)) + b);
            mem_[kSCY] = last;
        }
        b--;
    } while (last != 0xf8);
    for (int k = 0x0c; k > 0; k--) co_await flashLines(0x51d1, k);
    for (int f = 0x20; f > 0; f--) {                   // 02:51FD
        mem_[0xde0f] = uint8_t(f);
        if (f >= 6 && !(f & 1)) sound(0x36);
        co_await wait(1);
        for (int t = 0; t < 0x5a; t++) {
            const uint16_t tile = uint16_t(0x9000 + t * 16);
            copy(tile, 0xde80, 16);
            mem_[0xde90] = (f >= 0x2e || !(f & 1)) ? 0 : rnd(0xff);
            mem_[0xde91] = rnd(0xff);
            copy(0xde80, 0xde92, 14);
            for (int r = 0; r < 8; r++)
                mem_[uint16_t(0xdea0 + r)] = uint8_t(~mem_[uint16_t(0xde80 + r * 2)] | mem_[uint16_t(0xde81 + r * 2)]);
            for (int r = 0; r < 8; r++) {
                const uint8_t c = mem_[uint16_t(0xdea0 + r)];
                uint8_t& lo = mem_[uint16_t(0xde90 + r * 2)];
                uint8_t& hi = mem_[uint16_t(0xde91 + r * 2)];
                lo = uint8_t(~(~lo & c));
                hi = uint8_t(hi & c);
            }
            copy(0xde90, tile, 16);
        }
        mem_[kSCY] = mem_[kSCY] == 0xf8 ? 0xf7 : 0xf8;  // the LY-timed jitter, once a frame
    }
    for (int k = 4; k > 0; k--) co_await flashLines(0x4433, k);
    for (int i = 0; i < 0x100; i++) mem_[uint16_t(0x9800 + i)] = 0xff;
    if (eraseEnemy) eraseEnemy(-1);
    co_await wait(1);
    mem_[kSCX] = mem_[kSCY] = 0;
}

// 02:50ED (death style 2, a boss changing form): the picture is lifted into 24 OBJs
// (03:7E74, recoloured, OBP1 $D8), the BG gets the new form (0E:7D63) which shudders in under
// it for 127 frames, then the sprites go and the screen flashes.
AnimTask BattleAnim::death2() {
    copy(0x9800, 0xdf00, 0x100);
    uint16_t de = 0x8000;
    for (int c = 0; c < 0x100; c += 0x40)
        for (int b = 7; b < 13; b++)
            for (int half = 0; half < 2; half++) {
                const uint8_t t = mem_[uint16_t(0xdf00 + b + c + half * 0x20)];
                for (int k = 0; k < 16; k++)
                    mem_[uint16_t(de + k)] = t == 0xff ? 0 : mem_[uint16_t(0x9000 + t * 16 + k)];
                de = uint16_t(de + 16);
            }
    uint16_t o = kOam;
    uint8_t e = 0;
    for (int y = 0x10; y <= 0x40; y += 0x10)
        for (int x = 0x40; x < 0x70; x += 8) {
            mem_[o++] = uint8_t(y); mem_[o++] = uint8_t(x); mem_[o++] = uint8_t(e++ * 2); mem_[o++] = 0x10;
        }
    for (int i = 0; i < 0x300; i += 2)                 // lo = ~lo ^ hi
        mem_[uint16_t(0x8000 + i)] = uint8_t(~mem_[uint16_t(0x8000 + i)] ^ mem_[uint16_t(0x8001 + i)]);
    co_await wait(1);
    mem_[kSCY] = 0x40;
    mem_[0xff49] = 0xd8;
    for (int i = 0; i < 0x5a0; i++) mem_[uint16_t(0x9000 + i)] = rom_.b(0x0e, uint16_t(0x6735 + i));
    maskMap(0x0e, 0x7d9a, 16);
    for (int d = 0x7f; d > 0; d--) {
        co_await wait(1);
        const uint8_t a = rb(uint16_t(0x51ca + ((d >> 1) & 3)));
        if (!a) sound(0x1c);
        mem_[kSCY] = a;
        mem_[kSCX] = (d & 1) ? uint8_t(-d) : uint8_t(d);
        mem_[kBGP] = rb(uint16_t(0x4430 + ((d >> 5) & 3)));
        atLine(0x48, kSCX, 0);
        atLine(0x48, kSCY, 0);
        atLine(0x48, kBGP, 0xd2);
    }
    sound(0x1c);
    clearOam();
    for (int k = 0x30; k > 0; k--) {                   // 02:51E0 with c = 1
        co_await wait(1);
        mem_[kBGP] = rb(uint16_t(0x51ca + k - 1));
        atLine(0x5a, kBGP, 0xd2);
    }
}

// 0E:7D7E: the map from a bit mask, 16 cells a row from column 2, cells numbered in order.
void BattleAnim::maskMap(int bank, uint16_t mask, int bytes) {
    for (int i = 0; i < 0x100; i++) mem_[uint16_t(0xdf00 + i)] = 0xff;
    uint8_t c = 0;
    uint16_t hl = 0xdf02;
    for (int i = 0; i < bytes; i++) {
        uint8_t a = rom_.b(bank, uint16_t(mask + i));
        for (int b = 0; b < 8; b++, hl++, a = uint8_t(a << 1))
            if (a & 0x80) mem_[hl] = c++;
        if (i & 1) hl = uint16_t(hl + 0x10);
    }
    copy(0xdf00, 0x9800, 0x100);
}

// 02:498C: from line `start` (plus a 0-3 wobble), n lines: the window's left edge from the
// table, the BG showing row 31 (filled with tile $FC) scrolled by the same table - a disc.
// Above it BGP = bgp and the window (blanked) covers the screen.
void BattleAnim::vortexLines(uint16_t table, int start, int n, uint8_t bgp) {
    mem_[0xde10]++;
    int d = start + (mem_[0xde10] & 3);
    mem_[0xff4a] = 0;
    mem_[kSCY] = 0;
    mem_[kBGP] = bgp;
    mem_[kOBP0] = mem_[0xde12];
    for (int i = 0; i < n; i++, d++, table++) {
        const uint8_t v = rb(table);
        atLine(d + 1, 0xff4b, uint8_t(v + 0xa5));
        atLine(d + 1, kSCY, uint8_t(0xf9 - d));
        atLine(d + 1, kSCX, uint8_t(v - 1));
    }
    atLine(d + 1, 0xff4b, 7);
    atLine(d + 1, kSCY, 0);
    atLine(d + 1, kBGP, 0xd2);
    atLine(d + 1, kOBP0, 0xd2);
}

// 02:48C6 (effect B): the window is blanked and the monsters turn to silhouettes while a
// whirling disc with particles opens over them; then it fades and the window comes back.
AnimTask BattleAnim::vortex() {
    ownsWindow = true;
    for (int i = 0; i < 20; i++) mem_[uint16_t(0x9be0 + i)] = 0xfc;
    particlesInit(0);
    mem_[0xdeff] = 1;
    mem_[0xde12] = 0xd2;
    copy(0x9c00, 0x9d60, 0x140);                       // 02:48AE
    for (int i = 0; i < 0x140; i++) mem_[uint16_t(0x9c00 + i)] = 0xff;
    for (int k = 0x40; k > 0; k--) {
        mem_[0xde0f] = uint8_t(k);
        const uint16_t g = uint16_t(0x49fd + (((0x40 - k) >> 3) & 7) * 2);   // 02:49DD
        spawnGroup(0x4ba6, uint16_t(rb(g) | rb(uint16_t(g + 1)) << 8), 0x08, 0x0c);
        co_await wait(1);                              // 02:497F
        vortexLines(0x4b47, k, 0x41 - k, 3);
        particlesStep();
        co_await wait(1);
        vortexLines(0x4b47, k, 0x41 - k, 3);
    }
    for (int c = 0x1f; c > 0; c--) {                   // 02:493D
        const int b = (c >> 3) & 3;
        mem_[0xde12] = rb(uint16_t(0x4433 + b));
        co_await wait(1);
        vortexLines(uint16_t(0x4b47 + 0x20 - c), 1, 0x40, uint8_t(b));
        co_await wait(1);
        mem_[0xff4a] = 0; mem_[kSCY] = 0; mem_[kSCX] = 0;
        mem_[0xff4b] = 0xa5;
        mem_[kBGP] = rb(uint16_t(0x4430 + b));
        atLine(0x45, 0xff4b, 7);
        atLine(0x45, kSCY, 0);
        atLine(0x45, kBGP, 0xd2);
        atLine(0x45, kOBP0, 0xd2);
    }
    clearOam();
    co_await wait(1);
    mem_[kLCDC] = 0xc7;
    copy(0x9d60, 0x9c00, 0x140);
    co_await wait(1);
    mem_[kLCDC] = 0xe7;
    mem_[0xff4a] = 0x58; mem_[0xff4b] = 7;
    mem_[kSCY] = mem_[kSCX] = 0;
    for (int i = 0; i < 0x140; i++) mem_[uint16_t(0x9d60 + i)] = 0xff;
    for (int i = 0; i < 20; i++) mem_[uint16_t(0x9be0 + i)] = 0xff;
    ownsWindow = false;
}

// 02:4F3E (effect 13): monster wC985 takes the picture of monster wC9D6 behind a dissolving mask.
AnimTask BattleAnim::swapPicture() {
    co_await wait(1);                                  // 02:500C with hl = 52FE + $3E
    for (int i = 0; i < 16; i++) mem_[uint16_t(0xde40 + i)] = mem_[uint16_t(0xde50 + i)] = rb(uint16_t(0x533c + i));
    copy(0xde40, 0x8000, 0x20);
    auto rect = [&](uint8_t slot) {                    // 02:529F
        const uint8_t v = mem_[uint16_t(0xcc51 + slot)];
        mem_[0xde17] = uint8_t((v >> 4) + 1);
        mem_[0xde18] = uint8_t((v & 15) + 1);
        mem_[0xde19] = mem_[uint16_t(0xcc41 + slot * 2)];
        mem_[0xde1a] = mem_[uint16_t(0xcc42 + slot * 2)];
    };
    rect(uint8_t(mem_[0xc985] - 5));
    {                                                  // 02:4FD0
        const int b = mem_[0xde17], rows = int8_t(mem_[0xde18]) >> 1;
        uint8_t y = uint8_t((mem_[0xde1a] + 2) * 8);
        uint16_t o = kOam;
        for (int c = rows; c > 0; c--, y = uint8_t(y + 16)) {
            uint8_t x = uint8_t((mem_[0xde19] + 1) * 8);
            for (int k = 0; k < b; k++, x = uint8_t(x + 8)) {
                mem_[o++] = y; mem_[o++] = x; mem_[o++] = 0; mem_[o++] = (c & 1) ? 0 : 0x40;
            }
        }
    }
    copy(0xde17, 0xde1b, 4);
    rect(uint8_t(mem_[0xc9d6] - 5));
    {                                                  // 02:4F7B: target's map rect -> source's
        const int w = mem_[0xde1b], h = mem_[0xde1c];
        uint16_t src = uint16_t(0x9800 + mem_[0xde1d] + mem_[0xde1e] * 32);
        uint16_t dst = uint16_t(0x9800 + mem_[0xde19] + mem_[0xde1a] * 32);
        for (int r = 0; r < h; r++, src = uint16_t(src + 32), dst = uint16_t(dst + 32))
            copy(dst, src, w);
    }
    for (int k = 7; k > 0; k--) {
        co_await wait(1);
        const uint16_t p = uint16_t(0x537e + ((k - 1) & 7) * 16);
        for (int i = 0; i < 16; i++) mem_[uint16_t(0xde40 + i)] = mem_[uint16_t(0xde50 + i)] = rb(uint16_t(p + i));
        copy(0xde40, 0x8000, 0x20);
        co_await wait(3);
    }
    clearOam();
}

// 0E:7DBF (effect 14): something thrown onto the monsters - a 16x16 object (0E:6CCF) bouncing
// in from the right, or for a big monster a larger one (0E:7E86) dropping with the window
// shaking. Both draw over the pictures' tiles; the battle redraws them afterwards.
AnimTask BattleAnim::thrown() {
    const uint8_t id = mem_[0xc91b];
    bool big = false;
    if (id < 0x78) {
        const uint8_t v = mem_[uint16_t(0xcc51 + uint8_t(mem_[0xc9d6] - 5))];
        big = (v & 0x57) >= 0x57;
    }
    ownsWindow = true;
    if (!big) {
        co_await wait(1);
        mem_[kSCY] = 0xc0;
        mem_[kSCX] = 0x28;
        const uint16_t src = uint16_t(0x6ccf + (id >= 0x77 ? 0 : 0x40));
        for (int i = 0; i < 0x40; i++) mem_[uint16_t(0x9000 + i)] = rom_.b(0x0e, uint16_t(src + i));
        co_await wait(1);
        mem_[0x9889] = 0; mem_[0x988a] = 2; mem_[0x98a9] = 1; mem_[0x98aa] = 3;
        uint16_t t = 0x7e1f;
        do {
            co_await wait(3);
            mem_[kSCY] = rom_.b(0x0e, t++);
            mem_[kSCX] = uint8_t(mem_[kSCX] - 4);
        } while (mem_[kSCX]);
    } else {
        mem_[kSCY] = 0x40;
        for (int i = 0; i < 0xe0; i++) mem_[uint16_t(0x9000 + i)] = rom_.b(0x0e, uint16_t(0x7e86 + i));
        maskMap(0x0e, 0x7e56, 16);
        uint16_t t = 0x7e66;
        for (int b = 0x10; b > 0; b--) {
            co_await wait(3);
            mem_[kSCY] = rom_.b(0x0e, t++);
            mem_[0xff4a] = rom_.b(0x0e, t++);
            if (b == 6) sound(0x10);
        }
    }
    ownsWindow = false;
    restoreBg = true;
}

// ---- spells ----

// 02:4757: a group of particles: n, then per particle (row<<5 | col) and (attr | tile/2),
// placed at (yb + row*8, xb + col*8) - only if there are enough free slots.
bool BattleAnim::spawnGroup(uint16_t motion, uint16_t group, uint8_t xb, uint8_t yb) {
    const int n = rb(group++);
    int free = 0;
    for (int i = 0; i < 30; i++) if (mem_[uint16_t(0xdf00 + i * 8)] == 0xff) free++;
    if (n >= free + 1) return false;
    for (int i = 0; i < n; i++) {
        const uint8_t p = rb(group++), t = rb(group++);
        mem_[0xde13] = uint8_t(((p >> 5) & 7) * 8 + yb);
        mem_[0xde14] = uint8_t((p & 0x1f) * 8 + xb);
        mem_[0xde15] = uint8_t((t & 0x0f) * 2);
        mem_[0xde16] = t & 0xf0;
        particleAdd(motion);
    }
    return true;
}

// 02:44B8 (effect 5): sparks appear at random and are pulled into the middle of the monsters.
AnimTask BattleAnim::converge() {
    particlesInit(3);
    mem_[0xdeff] = 0x0e;
    bool more;
    for (int k = 0x0e; k > 0; k--) {
        bool spawn = true;                             // 02:4568: make room at wDF00
        if (mem_[0xdf00] != 0xff) {
            spawn = false;
            for (int i = 1; i < 30; i++) {
                const uint16_t e = uint16_t(0xdf00 + i * 8);
                if (mem_[e] == 0xff) { copy(0xdf00, e, 8); spawn = true; break; }
            }
        }
        if (spawn) {
            const uint8_t e[8] = {0, 0x0a, 0x3c, 0x46, uint8_t(rnd(0x0a) * 8), uint8_t(rnd(0x16) * 8), 0, 0};
            for (int i = 0; i < 8; i++) mem_[uint16_t(0xdf00 + i)] = e[i];
        }
        co_await particlesFrame(more);
        for (int i = 0; i < 30; i++) {                 // 02:4500
            const uint16_t e = uint16_t(0xdf00 + i * 8);
            if (mem_[e] == 0xff) continue;
            auto pull = [&](uint16_t a, uint8_t target) {
                const uint8_t v = mem_[a];
                const uint8_t d = uint8_t((v < target ? target - v : v - target) >> 2);
                mem_[a] = v < target ? uint8_t(v + d) : uint8_t(v - d);
                return d;
            };
            const uint8_t d = pull(uint16_t(e + 4), 0x28);
            const uint8_t bb = pull(uint16_t(e + 5), 0x54);
            const int a = uint8_t(bb + d);
            if (a < 4) { mem_[e] = 0xff; continue; }
            mem_[uint16_t(e + 6)] = a < 8 ? 2 : 0;
            mem_[uint16_t(e + 7)] = 0;
        }
        co_await particlesFrame(more);
    }
}

// 02:4437 (effect 6): the monsters ripple - SCX per line from a rotating table.
AnimTask BattleAnim::wave() {
    uint16_t hl = 0xde40;                              // 02:4498
    for (int c = 0; c < 4; c++)
        for (int b = 0; b < 8; b++) {
            const uint8_t a = rb(uint16_t(0x44b0 + b));
            mem_[hl++] = a;
            mem_[hl++] = uint8_t(-a);
        }
    for (int phase = 0; phase < 2; phase++)
        for (int n = phase ? 0x60 : 0x40; n > 0; n--) {
            const uint8_t c = mem_[0xde40];
            copy(0xde41, 0xde40, 0x3f);
            mem_[0xde7f] = phase ? uint8_t(c << 1) : c;
            co_await wait(1);
            rasterTable(kSCX, 65);
        }
}

// The particle spells: patterns of sprites from the script's tiles falling / scattering.
AnimTask BattleAnim::rain(int id) {
    bool more = true;
    switch (id) {
    case 7:                                            // 02:463E
        particlesInit(2);
        for (int k = 0x10; k > 0; k--) {
            const uint8_t a = uint8_t(k - 1);
            spawnGroup(0x463b, 0x466d, rb(uint16_t(0x465d + (a & 15))), 0x08);
            spawnGroup(0x463b, 0x4682, rb(uint16_t(0x465d + ((a + 8) & 15))), 0x28);
            co_await particlesFrame(more);
            spawnGroup(0x463b, 0x4682, rb(uint16_t(0x465d + (a & 15))), 0x08);
            spawnGroup(0x463b, 0x466d, rb(uint16_t(0x465d + ((a + 8) & 15))), 0x28);
            co_await particlesFrame(more);
        }
        break;
    case 8:                                            // 02:4697
        particlesInit(1);
        for (;;) {
            spawnGroup(0x46c0, 0x46bb, 0x50, 0x28);
            do co_await particlesFrame(more); while (more);
            const uint8_t s = uint8_t(--mem_[0xdefc]);
            if (uint8_t(s + 1) == 0) break;
        }
        break;
    case 9:                                            // 02:458A
        particlesInit(1);
        mem_[0xdeff] = 0x10;
        for (int k = 0x10; k > 0; k--) {
            spawnGroup(0x45b4, 0x45b7, rb(uint16_t(0x47b6 + (k & 15))), 0);
            for (int f = 0; f < 3; f++) co_await particlesFrame(more);
        }
        do co_await particlesFrame(more); while (more);
        break;
    case 0x0c: case 0x0d: {                            // 02:45C9 / 45C0
        int n = 8;
        if (id == 0x0c) { copy(0x82e0, 0x8000, 0x100); particlesInit(3); n = 0x18; }
        else particlesInit(6);
        for (int k = n; k > 0; k--) {                  // 02:45DC, 4737: bases from 02:45F9
            const uint16_t t = uint16_t(0x45f9 + (k & 15) * 2);
            spawnGroup(0x463b, 0x4619, rb(uint16_t(t + 1)), rb(t));
            co_await particlesFrame(more);
            spawnGroup(0x463b, 0x462a, rb(uint16_t(t + 1)), rb(t));
        }
        co_await particlesFrame(more);
        break;
    }
    }
    clearOam();                                        // 02:45AE
    co_await wait(1);
}

// 02:47C6 (effect A): sparks rising out of the monsters while they shimmer and flicker.
AnimTask BattleAnim::sparks() {
    particlesInit(0);
    auto spark = [&] {                                 // 02:4812
        const uint8_t y = uint8_t(rnd(0x0c) * 4 + 0x1c);
        const uint8_t x = uint8_t(rnd(0x15) + 0x20);
        spawnGroup(0x4830, 0x482d, x, y);
    };
    for (int k = 0x18; k > 0; k--) {
        co_await wait(1);
        spark();
        spark();
        // 02:4869: the SCX table (02:4872, from 02:488E), then two writes a line (02:47E9)
        uint16_t de = 0xdebf;
        uint8_t c = uint8_t(k);
        for (int b = 0; b < 0x40; b++, c++) {
            const uint16_t t = uint16_t(0x488e + (c & 15) * 2);
            mem_[de--] = rb(t);
            mem_[de--] = rb(uint16_t(t + 1));
        }
        co_await wait(1);
        for (int l = 0; l < 0x41; l++) atLine(l, kSCX, mem_[uint16_t(0xde41 + l * 2)]);
        atLine(0x41, kSCX, 0);
        particlesStep();
        spark();
        co_await wait(1);                              // 02:4859: monsters gone for a frame
        mem_[kSCY] = 0x40;
        atLine(0x40, kSCY, 0);
    }
}

// 02:4BCD (effect F): ten pairs of BG tiles replaced from 02:4C05.
AnimTask BattleAnim::tilePatch() {
    uint16_t src = 0x4c05;
    for (int i = 0; i < 10; i++) {
        const uint16_t dst = uint16_t(rb(uint16_t(0x4bf1 + i * 2)) | rb(uint16_t(0x4bf2 + i * 2)) << 8);
        for (int k = 0; k < 0x20; k++) mem_[uint16_t(dst + k)] = rb(src++);
    }
    co_return;
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
    case 0x05: co_await converge(); co_return;
    case 0x06: co_await wave(); co_return;
    case 0x07: case 0x08: case 0x09: case 0x0c: case 0x0d: co_await rain(id); co_return;
    case 0x0a: co_await sparks(); co_return;
    case 0x0e: co_await bgFlash(); co_return;
    case 0x0b: co_await vortex(); co_return;
    case 0x0f: co_await tilePatch(); co_return;
    case 0x13: co_await swapPicture(); co_return;
    case 0x14: co_await thrown(); co_return;
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
        // Ids past the table (02:4364 has $18 entries) - never used by the game.
        if (std::find(unknownIds.begin(), unknownIds.end(), id) == unknownIds.end()) unknownIds.push_back(id);
        if (std::getenv("FFL3_ANIMLOG")) std::fprintf(stderr, "ANIM effect %02X not translated\n", id);
        for (int e = 16; e > 0; e--) {
            co_await wait(1);
            mem_[kBGP] = rb(uint16_t(0x4bc9 + ((e & 0x0c) >> 2)));
            atLine(64, kBGP, 0xd2);
        }
        co_return;
    }
}

} // namespace ffl3
