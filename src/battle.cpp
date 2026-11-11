#include <cstdlib>
#include "battle.h"
#include "battlenatives.h"
#include <algorithm>
#include <cstdio>
#include <functional>

namespace ffl3 {

namespace {
const char* kFight[3] = {"Fight", "Run", "Auto"};
const char* kCmds[4] = {"Weapon", "Talent", "Magic", "Item"};
// Field sprite page (04:xx00) of the starting party's records; where the game takes it
// from is not traced yet.
const int kSpritePage[10] = {0x70, 0x74, 0x72, 0x71, 0x73, 0x70, 0x70, 0x70, 0x70, 0x70};
// Codes below $4E that are dictionary words (00:30D1), entries $3A+i.
const uint8_t kDictLow[] = {0x01, 0x02, 0x03, 0x04, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x40,
                            0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x0e, 0x0f};
int argCount(uint8_t c) {
    switch (c) {
    case 0x00: case 0x05: case 0x06: case 0x07: case 0x08: case 0x0b: case 0x10: case 0x14:
    case 0x1c: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x34: case 0x4b: case 0x4c:
    case 0x4d:
        return 0;
    case 0x39: return 5;
    case 0x3a: return 8;
    case 0x3c: return 4;
    case 0x3e: return 2;
    case 0x43: return 3;
    default: return 1;
    }
}
Glyphs G(const char* s) { return asciiGlyphs(s); }
constexpr uint8_t kNewline = 0xfe;
}

void initBattleRam(BattleRam& ram) {
    ram.fill(0);
    ram[0xc9f8] = ram[0xc9f9] = 0xff;            // message routine idle
    ram[0xce8c] = 0xff;
    for (int i = 0; i < 5; i++) { ram[size_t(0xce02 + i * 4)] = 0xff; ram[size_t(0xce03 + i * 4)] = 0xff; }
    for (int i = 0; i < 48; i++) ram[size_t(0xc6ff + i)] = 0xff;   // inventory: 24 (id, count)
    ram[0xc6ff] = 0xe6; ram[0xc700] = 0x01;      // what a new game carries (Radar)
    ram[0xc6e0] = 0xc0; ram[0xc6e1] = 0x03;      // 960 GP, the gold after the opening
}

int inventoryAdd(BattleRam& ram, uint8_t id) {
    for (int i = 0; i < 24; i++) {
        const size_t hl = size_t(0xc6ff + i * 2);
        if (ram[hl] != id) continue;
        if (ram[hl + 1] >= 9) return -1;
        return ++ram[hl + 1];
    }
    for (int i = 0; i < 24; i++) {
        const size_t hl = size_t(0xc6ff + i * 2);
        if (ram[hl] == 0xff) { ram[hl] = id; ram[hl + 1] = 1; return 1; }
    }
    return -1;
}

Battle::Battle(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, const BattleContext& ctx, int hffca, uint32_t seed)
    : rom_(rom), ram_(ram), party_(party), vm_(rom) {
    vm_.mem = ram_;
    vm_.mem[0xff90] = uint8_t(seed);
    vm_.mem[0xff93] = uint8_t(seed >> 8);
    ctx_ = ctx;
    hffca_ = hffca;
    setup(hffca, ctx);
    vm_.native = [this](uint16_t a) { return native(a); };
    vm_.random = [this](uint8_t n) { return rnd(n); };
    vm_.onSfx = [this](uint8_t id) { if (id && playSfx) playSfx(id); };
    // wFF93 moves on once a frame on the GB, and the VM is slow enough that long loops (an
    // enemy re-rolling its action) span frames. Approximate: one frame per 150 statements.
    vm_.onStep = [this](uint16_t) { if (++stepCount_ % 150 == 0) vm_.mem[0xff93]++; };
    timer_ = 30;
}

// 00:39D5 and 00:1D2E
uint8_t Battle::rand8() {
    uint8_t a = uint8_t(vm_.mem[0xff93] + vm_.mem[0xff90]);
    a ^= 1;
    a = uint8_t(a >> 1 | a << 7);
    a = uint8_t(a + vm_.mem[0xff90]);
    vm_.mem[0xff90] = a;
    return a;
}

uint8_t Battle::rnd(uint8_t n) {
    if (!n) return 0;
    uint8_t r = rand8();
    if (!r) return 0;
    if (n < 9) r &= 7;
    while (r >= n) r = uint8_t(r - n);
    return r;
}

// What 0B:4000 / 405A / 4444 set up before entering the VM at 0C:4A2D.
void Battle::setup(int hffca, const BattleContext& ctx, bool retry) {
    auto& M = vm_.mem;
    for (size_t i = 0; i < 5; i++) {
        const uint16_t base = uint16_t(0xc500 + i * 0x60);
        if (i < party_.size()) std::copy(party_[i].raw.begin(), party_[i].raw.end(), M.begin() + base);
        else std::fill(M.begin() + base, M.begin() + base + 0x60, 0xff);
    }
    std::copy(ctx.eventFlags.begin(), ctx.eventFlags.end(), M.begin() + 0xc760);
    M[0xc6e3] = uint8_t(ctx.vehicle);
    M[0xca2d] = uint8_t(ctx.area);

    if (!retry) {                                      // a retry keeps the same monsters (0B:5A18)
        form_ = loadFormation(rom_, hffca & 0x7f);
        layout_ = layoutEnemies(rom_, form_, (hffca & 0x80) != 0, [this](int n) { return rnd(uint8_t(n)); });
        for (int g = 0; g < 3; g++) groups_.push_back(form_.ids[g] == 0xff ? Monster{} : loadMonster(rom_, form_.ids[g]));
    }
    for (int i = 0; i < 3; i++) M[size_t(0xcc59 + i)] = uint8_t(form_.ids[i]);
    M[0xcc5c] = form_.flags;
    M[0xcc5d] = form_.counts;
    M[0xffca] = uint8_t(hffca);
    for (int g = 0; g < 3; g++) { M[size_t(0xcc33 + g)] = uint8_t(layout_.units[g]); M[size_t(0xcc36 + g)] = uint8_t(layout_.count[g]); }
    M[0xcc5e] = M[0xcc5f] = M[0xcc60] = 0;
    for (size_t k = 0; k < layout_.slots.size(); k++)
        M[size_t(0xcc5e + layout_.slots[k].group)] |= uint8_t(1 << k);
    M[0xffba] = uint8_t(((form_.flags >> 4) & 3) + 1);
    M[0xcd37] = 1;
    M[0xd684] = pictureResidue(rom_, form_.ids);
    battleSetupCombatants(vm_, rom_);
    M[0xddff] = M[0xd203] == 0xff ? 4 : 5;
    std::copy(M.begin() + 0xc6e0, M.begin() + 0xc740, M.begin() + 0xdd00);
    M[0xceda] = 0;
    vm_.start(0x4a2d);
}

bool Battle::memberAlive(int i) const {
    const uint16_t r = uint16_t(0xd000 + i * 0x80);
    return m(uint16_t(r + 3)) != 0xff && word(uint16_t(r + 0x10)) > 0;
}

bool Battle::enemyAlive(int slot) const {
    const uint16_t r = uint16_t(0xd000 + (5 + slot) * 0x80);
    return m(uint16_t(r + 3)) != 0xff && word(uint16_t(r + 0x10)) > 0;
}

int Battle::enemyHp(int slot) const { return word(uint16_t(0xd000 + (5 + slot) * 0x80 + 0x10)); }

std::vector<int> Battle::aliveGroups() const {
    std::vector<int> v;
    for (int g = 0; g < 3; g++)
        for (size_t s = 0; s < layout_.slots.size(); s++)
            if (layout_.slots[s].group == g && enemyAlive(int(s))) { v.push_back(g); break; }
    return v;
}

int Battle::memberSprite(int i) const { return kSpritePage[std::min(i, 9)]; }

// 00:20DE: party 0-4 (name at record +7, 6 chars) or combatant $80+k (monster, +5, 8 chars)
Glyphs Battle::combatantName(int a) const {
    if (a >= 0x80) a -= 0x7b;
    const uint16_t r = uint16_t(0xd000 + a * 0x80);
    Glyphs g;
    const int off = a < 5 ? 7 : 5, n = a < 5 ? 6 : 8;
    for (int i = 0; i < n; i++) g.push_back(m(uint16_t(r + off + i)));
    while (!g.empty() && g.back() == 0xff) g.pop_back();
    return g;
}

// ---- VM and natives --------------------------------------------------------------------

void Battle::runVm() {
    state_ = State::Vm;
    for (int guard = 0; guard < 20; guard++) {
        const BattleVM::Stop s = vm_.run(200000);
        if (s == BattleVM::Stop::Yield) return;
        if (s == BattleVM::Stop::Limit) continue;
        endBattle();                                    // End (or a bad opcode)
        return;
    }
    std::fprintf(stderr, "battle VM stuck at %04X\n", vm_.pc());
    endBattle();
}

bool Battle::native(uint16_t addr) {
    if (runLogicNative(vm_, rom_, addr)) return true;
    switch (addr) {
    case 0x1d69:                                       // command phase (0B:5BDF, wCE88 = 1)
        vm_.mem[0xce88] = 1;
        lines_.clear(); cur_.clear();
        for (bool& c : cmdSet_) c = false;
        state_ = State::FightMenu;
        cursor_ = 0;
        return false;
    case 0x1d77:                                       // after a defeat (0B:5CDF)
        vm_.mem[0xce88] = 2;
        if (vm_.mem[0xceda] != 0xff)                   // no retry: fallen members get up with 1 HP
            for (int i = 0; i < 5; i++) {
                const uint16_t r = uint16_t(0xc500 + i * 0x60);
                if ((vm_.mem[uint16_t(r + 1)] & 0x80) || !(vm_.mem[uint16_t(r + 0x10)] | vm_.mem[uint16_t(r + 0x11)])) {
                    vm_.mem[uint16_t(r + 0x10)] = 1; vm_.mem[uint16_t(r + 0x11)] = 0;
                }
                vm_.mem[uint16_t(r + 1)] &= 0x40;
            }
        return true;
    case 0x5fb7:                                       // print battle message wCE8D/8E
        showMessage();
        state_ = State::Message;
        return false;
    case 0x6001: case 0x6b68: case 0x6b6c: case 0x6b71: case 0x6b7f:   // animations / sound
        if (std::getenv("FFL3_ANIMLOG")) std::fprintf(stderr, "ANIM %04X de00=%02X c9de=%02X\n", addr, m(0xde00), m(0xc9de));
        timer_ = 16;
        if (m(0xc9de) >= 5 && m(0xc9de) < 13) { flashSlot_ = m(0xc9de) - 5; flashTimer_ = 16; }   // blink the target
        state_ = State::Wait;
        return false;
    default:
        if (std::find(unknownNatives.begin(), unknownNatives.end(), addr) == unknownNatives.end()) unknownNatives.push_back(addr);
        return true;
    }
}

// Decode the bank 9 battle text at wCE8D/8E into pending glyphs.
void Battle::showMessage() {
    // dictionary 03:71C5: 80 length-prefixed entries
    static std::vector<Glyphs> dict;
    if (dict.empty()) {
        uint16_t a = 0x71c5;
        for (int i = 0; i < 80; i++) {
            const int n = rom_.b(3, a++);
            Glyphs e;
            for (int k = 0; k < n; k++) e.push_back(rom_.b(3, a++));
            dict.push_back(e);
        }
    }
    auto put = [&](const Glyphs& g) { for (uint8_t c : g) pending_.push_back(c); };
    uint16_t p = uint16_t(m(0xce8d) | m(0xce8e) << 8);
    auto rb = [&]() { return rom_.b(9, p++); };
    std::function<void(uint8_t)> code = [&](uint8_t c) {
        if (c >= 0x88) { pending_.push_back(c); return; }
        if (c >= 0x4e) { for (uint8_t x : dict[size_t(c - 0x4e)]) code(x); return; }
        for (size_t i = 0; i < sizeof kDictLow; i++)
            if (kDictLow[i] == c) { for (uint8_t x : dict[58 + i]) code(x); return; }
        switch (c) {
        case 0x06: pending_.push_back(kNewline); return;
        case 0x1f: {
            uint8_t n = rb();
            if (n == 0xff) n = m(0xcd36);
            else if (n == 0x80) n = m(0xcd4c);
            put(combatantName(n));
            return;
        }
        case 0x1e: case 0x22: case 0x31: {
            rb();
            if (c == 0x1e && m(0xcd36) >= 0x80) { put(talentName(rom_, m(0xce8c))); return; }   // monster: talent
            for (uint16_t a = 0xce3c; a < 0xce46 && m(a); a++) pending_.push_back(m(a));
            return;
        }
        case 0x30: {
            rb();
            const int v = m(0xce7c) | m(0xce7d) << 8 | m(0xce7e) << 16;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d", v);
            put(G(buf));
            return;
        }
        default:
            for (int i = argCount(c); i > 0; i--) rb();
            return;
        }
    };
    for (int guard = 0; guard < 400; guard++) {
        const uint8_t c = rb();
        if (c == 0x00 || c == 0x10) break;
        code(c);
    }
    typeDelay_ = 1;
}

void Battle::finishCommands(bool run) {
    auto& M = vm_.mem;
    if (run) {
        M[0xd061] |= 0x80;
        for (int i = 0; i < 5; i++) { M[size_t(0xce00 + i * 4)] = 0; M[size_t(0xce01 + i * 4)] = 0; M[size_t(0xce02 + i * 4)] = 0xff; M[size_t(0xce03 + i * 4)] = 0xff; }
    }
    runVm();
}

void Battle::endBattle() {
    if (vm_.mem[0xceda] == 0xff) {                     // 00:1126: the party was beaten - fight again
        lines_.clear(); cur_.clear();
        setup(hffca_, ctx_, true);
        state_ = State::Intro;
        timer_ = 30;
        return;
    }
    state_ = State::Done;
    ram_ = vm_.mem;
    for (size_t i = 0; i < party_.size() && i < 5; i++)
        std::copy(ram_.begin() + 0xc500 + long(i) * 0x60, ram_.begin() + 0xc560 + long(i) * 0x60, party_[i].raw.begin());
    bool anyAlive = false;
    for (int i = 0; i < 4; i++) anyAlive |= memberAlive(i);
    if (!anyAlive) result_ = Result::Lost;
    else if (aliveGroups().empty()) result_ = Result::Won;
    else result_ = Result::Ran;
}

void Battle::tick(const BattleInput& in) {
    vm_.mem[0xff93]++;                                 // the counter 00:39D5 mixes in
    if (flashTimer_ > 0 && --flashTimer_ == 0) flashSlot_ = -1;
    auto setCmd = [&](int i, int target) {
        const uint16_t e = uint16_t(0xce00 + i * 4);
        vm_.mem[e] = 0; vm_.mem[uint16_t(e + 1)] = 0;
        vm_.mem[uint16_t(e + 2)] = uint8_t(target); vm_.mem[uint16_t(e + 3)] = 0xff;
    };
    auto firstSlot = [&](int g) {
        for (size_t s = 0; s < layout_.slots.size(); s++) if (layout_.slots[s].group == g && enemyAlive(int(s))) return int(5 + s);
        return 0xff;
    };
    switch (state_) {
    case State::Intro:
        if (--timer_ <= 0) runVm();
        return;
    case State::Vm:
        runVm();
        return;
    case State::Wait:
        if (--timer_ <= 0) runVm();
        return;
    case State::Message:
        if (--typeDelay_ > 0) return;
        typeDelay_ = in.aHeld ? 1 : 2;
        if (!pending_.empty()) {
            const uint8_t c = pending_.front();
            pending_.pop_front();
            if (c == kNewline) { lines_.push_back(cur_); cur_.clear(); }
            else cur_.push_back(c);
            if (c == kNewline || pending_.empty()) {
                std::string s;
                for (uint8_t t : (c == kNewline ? lines_.back() : cur_)) {
                    if (t >= 0xba && t <= 0xd3) s += char('A' + t - 0xba);
                    else if (t >= 0xd4 && t <= 0xed) s += char('a' + t - 0xd4);
                    else if (t >= 0xb0 && t <= 0xb9) s += char('0' + t - 0xb0);
                    else if (t == 0xff) s += ' ';
                    else if (t == 0xaf) s += '.';
                    else if (t == 0xf3) s += '!';
                    else if (t == 0xad) s += '\'';
                    else if (t == 0xf2) s += '-';
                    else s += '#';
                }
                if (c == kNewline) log.push_back(s);
            }
            if (pending_.empty()) timer_ = 20;
            return;
        }
        if (in.a || --timer_ <= 0) runVm();
        return;
    case State::FightMenu:
        if (in.up) cursor_ = (cursor_ + 2) % 3;
        if (in.down) cursor_ = (cursor_ + 1) % 3;
        if (!in.a) return;
        if (cursor_ == 0) {
            member_ = 0;
            while (member_ < 4 && !memberAlive(member_)) member_++;
            for (int i = 0; i < 5; i++) if (!memberAlive(i)) setCmd(i, 0xff);
            state_ = State::Command;
            cursor_ = 0;
        } else if (cursor_ == 1) {
            finishCommands(true);
        } else {                                       // Auto: everyone attacks the first group
            const auto g = aliveGroups();
            for (int i = 0; i < 5; i++) setCmd(i, memberAlive(i) && !g.empty() ? firstSlot(g[0]) : 0xff);
            finishCommands(false);
        }
        return;
    case State::Command:
        if (in.up) cursor_ = (cursor_ + 3) % 4;
        if (in.down) cursor_ = (cursor_ + 1) % 4;
        if (in.b) {
            int k = member_ - 1;
            while (k >= 0 && !memberAlive(k)) k--;
            if (k < 0) { state_ = State::FightMenu; cursor_ = 0; }
            else { member_ = k; cmdSet_[k] = false; cursor_ = 0; }
            return;
        }
        if (in.a) {
            command_ = cursor_;
            if (command_ == 0) {                       // Weapon: the slot-0 weapon, id $FF
                chosenId_ = 0xff;
                const uint8_t w = m(uint16_t(0xd040 + member_ * 0x80));
                const uint8_t k = w == 0xff ? 0x81 : itemKind(w, 0, 0);
                if (!beginTarget(k ? k : 0x81)) confirmTarget();
            } else {
                openList(command_);
            }
        }
        return;
    case State::List: {
        if (errorTimer_ > 0) { errorTimer_--; return; }
        if (list_.empty()) { if (in.a || in.b) state_ = State::Command; return; }
        if (in.up) listCursor_ = (listCursor_ + int(list_.size()) - 1) % int(list_.size());
        if (in.down) listCursor_ = (listCursor_ + 1) % int(list_.size());
        if (in.b) { state_ = State::Command; return; }
        if (!in.a) return;
        const Entry& e = list_[size_t(listCursor_)];
        chosenId_ = e.id;
        uint8_t k;
        if (command_ == 1) k = itemKind(e.id, 1, 0);                      // 0B:6557
        else if (command_ == 3) k = itemKind(e.id, 0, 1);                 // 0B:6527
        else {                                                            // 0B:6589
            k = itemKind(e.id, 0, 0);
            const uint16_t r = uint16_t(0xd000 + member_ * 0x80);
            if (!m(uint16_t(r + 0x15)) && m(uint16_t(r + 0x14)) < m(0xce29)) { errorTimer_ = 30; return; }   // not enough MP
        }
        if (!k) { errorTimer_ = 20; return; }                             // can't use it here (1E83)
        if (!beginTarget(k)) confirmTarget();
        return;
    }
    case State::Target: {
        if (in.b) { state_ = command_ ? State::List : State::Command; return; }
        if (in.left) moveTarget(-1, 0);
        if (in.right) moveTarget(1, 0);
        if (in.up) moveTarget(0, -1);
        if (in.down) moveTarget(0, 1);
        if (in.a) confirmTarget();
        return;
    }
    case State::Done:
        return;
    }
}

// What 09:5D87 says about an item/spell/talent: wCE28, the use kind (0 = can't be used).
uint8_t Battle::itemKind(uint8_t id, uint8_t ce25, uint8_t ce27) {
    vm_.mem[0xce26] = id; vm_.mem[0xce25] = ce25; vm_.mem[0xce27] = ce27;
    vm_.mem[0xcd36] = uint8_t(member_);
    runLogicNative(vm_, rom_, 0x6b64);
    return m(0xce28);
}

void Battle::openList(int command) {
    list_.clear();
    listCursor_ = 0;
    const uint16_t r = uint16_t(0xd000 + member_ * 0x80);
    if (command == 1) {                                   // talents: record +$48-4F
        for (int i = 0; i < 8; i++) { const uint8_t id = m(uint16_t(r + 0x48 + i)); if (id != 0xff) list_.push_back({id, -1, i}); }
    } else if (command == 2) {                            // spells: bits at +$51-56 (09:51EA)
        for (int k = 0; k < 6; k++)
            for (int bit = 0; bit < 8; bit++)
                if (m(uint16_t(r + 0x51 + k)) & (0x80 >> bit)) {
                    const uint8_t id = uint8_t((k ^ 1) * 8 + 7 - bit);
                    itemKind(id, 0, 0);
                    list_.push_back({id, m(0xce29), -1});      // MP cost (wCE29)
                }
    } else {                                              // inventory wC6FF
        for (int i = 0; i < 24; i++) {
            const uint8_t id = m(uint16_t(0xc6ff + i * 2));
            if (id != 0xff) list_.push_back({id, m(uint16_t(0xc700 + i * 2)), i});
        }
    }
    state_ = State::List;
}

// 0B:670D / 6718: where the cursor starts. Returns false when the action takes no target.
bool Battle::beginTarget(uint8_t kind) {
    kind_ = kind;
    if (kind & 0x80) {
        target_ = 0xff;
        for (size_t s = 0; s < layout_.slots.size(); s++) if (enemyAlive(int(s))) { target_ = uint8_t(5 + s); break; }
        if (kind & 1) {}
        else if (kind & 4) { const auto g = aliveGroups(); target_ = uint8_t(0x81 + (g.empty() ? 0 : g[0])); }
        else { target_ = 0xff; return false; }
    } else {
        target_ = 0;
        if (kind & 8) {}
        else if (kind & 0x10) target_ = 0x80;
        else { target_ = 0xff; return false; }
    }
    state_ = State::Target;
    return true;
}

void Battle::moveTarget(int dx, int dy) {
    if (target_ < 5) {                                    // a party member (0B:698D)
        int t = target_;
        if (dy > 0) {
            if (t >= 2) { if (kind_ & 0x10) target_ = 0x80; else errorTimer_ = 10; return; }
            t += 2;
        } else if (dy < 0) { t = t >= 2 && t < 4 ? t - 2 : t == 4 ? 2 : t; }
        else if (dx) t = t < 4 ? (t ^ 1) : t;
        if (memberAlive(t) || m(uint16_t(0xd003 + t * 0x80)) != 0xff) target_ = uint8_t(t);
    } else if (target_ == 0x80) {
        if (dy < 0) target_ = 0;
    } else if (target_ & 0x80) {                          // enemy group
        const auto g = aliveGroups();
        if (g.empty()) return;
        auto it = std::find(g.begin(), g.end(), int(target_ & 0x0f) - 1);
        size_t k = it == g.end() ? 0 : size_t(it - g.begin());
        k = (k + g.size() + (dx + dy > 0 ? 1 : -1)) % g.size();
        target_ = uint8_t(0x81 + g[k]);
    } else {                                              // one enemy
        std::vector<int> alive;
        for (size_t s = 0; s < layout_.slots.size(); s++) if (enemyAlive(int(s))) alive.push_back(int(5 + s));
        if (alive.empty()) return;
        auto it = std::find(alive.begin(), alive.end(), int(target_));
        size_t k = it == alive.end() ? 0 : size_t(it - alive.begin());
        k = (k + alive.size() + (dx + dy > 0 ? 1 : -1)) % alive.size();
        target_ = uint8_t(alive[k]);
    }
}

// Store the command (wCE00 + 4i: command << 4, 0, target, id) and go to the next member.
void Battle::confirmTarget() {
    const uint16_t e = uint16_t(0xce00 + member_ * 4);
    vm_.mem[e] = uint8_t(command_ << 4);
    vm_.mem[uint16_t(e + 1)] = 0;
    vm_.mem[uint16_t(e + 2)] = target_;
    vm_.mem[uint16_t(e + 3)] = chosenId_;
    if (command_ == 3 && chosenId_ >= 0xc0)               // 0B:6527: battle items are used up here
        for (int i = 0; i < 24; i++)
            if (m(uint16_t(0xc6ff + i * 2)) == chosenId_) { vm_.mem[uint16_t(0xc700 + i * 2)]--; break; }
    cmdSet_[member_] = true;
    cmdKind_[member_] = command_;
    int n = member_ + 1;
    while (n < 4 && !memberAlive(n)) n++;
    if (n < 4) { member_ = n; state_ = State::Command; cursor_ = 0; return; }
    if (memberAlive(4)) {                                 // the guest acts on its own
        for (size_t s = 0; s < layout_.slots.size(); s++)
            if (enemyAlive(int(s))) {
                const uint16_t g = 0xce10;
                vm_.mem[g] = 0; vm_.mem[g + 1] = 0; vm_.mem[g + 2] = uint8_t(5 + s); vm_.mem[g + 3] = 0xff;
                break;
            }
    }
    finishCommands(false);
}

// ---- view ------------------------------------------------------------------------------

void Battle::putText(std::array<uint8_t, 200>& w, int col, int row, const Glyphs& g) const {
    for (size_t i = 0; i < g.size() && col + int(i) < 20; i++) w[size_t(row * 20 + col + int(i))] = g[i];
}

void Battle::putNum(std::array<uint8_t, 200>& w, int col, int row, int v, int width) const {
    for (int i = width - 1; i >= 0; i--) {
        w[size_t(row * 20 + col + i)] = (v || i == width - 1) ? uint8_t(0xb0 + v % 10) : 0xff;
        v /= 10;
    }
}

void Battle::box(std::array<uint8_t, 200>& w, int c0, int r0, int cw, int rh) const {
    for (int r = r0; r < r0 + rh; r++)
        for (int c = c0; c < c0 + cw; c++) {
            uint8_t t = 0xff;
            const bool top = r == r0, bot = r == r0 + rh - 1, lft = c == c0, rgt = c == c0 + cw - 1;
            if (top) t = lft ? 0xf7 : rgt ? 0xf9 : 0xfc;
            else if (bot) t = lft ? 0xf8 : rgt ? 0xfa : 0xfc;
            else if (lft || rgt) t = 0xfb;
            w[size_t(r * 20 + c)] = t;
        }
}

void Battle::statusPanel(BattleView& v, int hideCol) const {
    box(v.win, 0, 6, 20, 4);
    box(v.win, 0, 0, 20, 7);
    for (int i = 0; i < 5; i++) {
        const uint16_t r = uint16_t(0xd000 + i * 0x80);
        if (m(uint16_t(r + 3)) == 0xff) continue;
        const int x0 = i == 4 ? 0 : (i & 1) ? 9 : 0, y0 = i == 4 ? 7 : 1 + (i >> 1) * 3;
        putNum(v.win, x0 + 3, y0, word(uint16_t(r + 0x10)), 3);
        putNum(v.win, x0 + 3, y0 + 1, word(uint16_t(r + 0x14)), 3);
        if (i < 4 && cmdSet_[i]) {
            static const char* kShort[4] = {"Weap", "Tale", "Magi", "Item"};
            putText(v.win, x0 + 6, y0 + 1, G(kShort[cmdKind_[i] & 3]));
        } else {
            v.win[size_t(y0 * 20 + x0 + 6)] = 0xf1;
            putNum(v.win, x0 + 7, y0, word(uint16_t(r + 0x12)), 3);
        }
        const int col = i == 4 ? 0 : (i & 1);
        if (col == hideCol) continue;
        const int px = i == 4 ? 8 : (i & 1) ? 80 : 8, py = 64 + 8 + (i == 4 ? 48 : (i >> 1) * 24);
        v.objs.push_back({px, py, 0, false, false, i, true});
    }
}

BattleView Battle::view() const {
    BattleView v;
    v.win.fill(0xff);
    for (size_t s = 0; s < layout_.slots.size(); s++)
        if (enemyAlive(int(s)) && !(int(s) == flashSlot_ && (flashTimer_ & 4))) v.visibleSlots.push_back(int(s));
    auto brackets = [&v](int x0, int y0, int x1, int y1) {
        v.objs.push_back({x0, y0, kBracket, false, false});
        v.objs.push_back({x1 - 8, y0, kBracket, true, false});
        v.objs.push_back({x0, y1 - 8, kBracket, false, true});
        v.objs.push_back({x1 - 8, y1 - 8, kBracket, true, true});
    };
    switch (state_) {
    case State::Intro:
    case State::Done:
        break;
    case State::FightMenu: {
        v.wy = 64;
        box(v.win, 0, 0, 10, 10);
        box(v.win, 10, 0, 10, 10);
        int row = 2;
        for (int g : aliveGroups()) { putText(v.win, 1, row, groups_[size_t(g)].name); row += 2; }
        for (int i = 0; i < 3; i++) putText(v.win, 12, 2 + i * 2, G(kFight[i]));
        v.objs.push_back({88, 64 + 16 + cursor_ * 16, kCursor});
        break;
    }
    case State::Command: {
        v.wy = 64;
        const bool left = (member_ & 1) == 0;
        statusPanel(v, left ? 1 : 0);
        const int bx = left ? 10 : 0;
        box(v.win, bx, 0, 10, 10);
        for (int i = 0; i < 4; i++) putText(v.win, bx + 2, 2 + i * 2, G(kCmds[i]));
        v.objs.push_back({bx * 8 + 8, 64 + 16 + cursor_ * 16, kCursor});
        const int px = (member_ & 1) ? 80 : 8, py = 64 + 8 + (member_ >> 1) * 24;
        brackets(px, py - 8, px + 16, py + 24);
        break;
    }
    case State::Target: {
        v.wy = 64;
        statusPanel(v, -1);
        int x0 = 160, x1 = 0, y0 = 144, y1 = 0;
        auto addRect = [&](int ax, int ay, int bx, int by) { x0 = std::min(x0, ax); y0 = std::min(y0, ay); x1 = std::max(x1, bx); y1 = std::max(y1, by); };
        auto addSlot = [&](size_t s) {
            const EnemySlot& e = layout_.slots[s];
            const Monster& mo = groups_[size_t(e.group)];
            addRect(e.col * 8, e.row * 8, (e.col + mo.picW) * 8, (e.row + mo.picH) * 8);
        };
        auto addMember = [&](int i) {
            const int px = i == 4 ? 8 : (i & 1) ? 80 : 8, py = 64 + 8 + (i == 4 ? 48 : (i >> 1) * 24);
            addRect(px, py - 8, px + 16, py + 24);
        };
        if (target_ < 5) addMember(target_);
        else if (target_ == 0x80) for (int i = 0; i < 5; i++) { if (memberAlive(i)) addMember(i); }
        else if (target_ & 0x80) { for (size_t s = 0; s < layout_.slots.size(); s++) if (layout_.slots[s].group == (target_ & 0x0f) - 1 && enemyAlive(int(s))) addSlot(s); }
        else if (target_ >= 5 && size_t(target_ - 5) < layout_.slots.size()) addSlot(size_t(target_ - 5));
        if (x1 > x0) brackets(x0, y0, x1, y1);
        break;
    }
    case State::List: {                                // 0B list window: name, count / MP
        v.wy = 64;
        box(v.win, 0, 0, 20, 10);
        const int first = std::max(0, std::min(listCursor_ - 3, int(list_.size()) - 4));
        for (int i = 0; i < 4 && first + i < int(list_.size()); i++) {
            const Entry& e = list_[size_t(first + i)];
            putText(v.win, 2, 2 + i * 2, itemName(rom_, e.id));
            if (e.count >= 0) putNum(v.win, 15, 2 + i * 2, e.count, 2);
        }
        if (!list_.empty()) v.objs.push_back({8, 64 + 16 + (listCursor_ - first) * 16, kCursor});
        break;
    }
    case State::Vm: case State::Message: case State::Wait: {
        v.wy = 88;
        box(v.win, 0, 0, 20, 7);
        std::vector<Glyphs> lines = lines_;
        if (!cur_.empty()) lines.push_back(cur_);
        const size_t first = lines.size() > 2 ? lines.size() - 2 : 0;
        for (size_t i = first; i < lines.size(); i++) putText(v.win, 1, 2 + int(i - first) * 2, lines[i]);
        for (int i = 0; i < 5; i++)
            if (memberAlive(i)) v.objs.push_back({8 + i * 32, 72, 0, false, false, i, true});
        break;
    }
    }
    v.flashSlot = flashSlot_;
    return v;
}

} // namespace ffl3
