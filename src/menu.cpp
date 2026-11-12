#include "menu.h"
#include "battlenatives.h"
#include <algorithm>
#include <cstdio>

namespace ffl3 {

namespace {
void putStr(UiScreen& s, int col, int row, const char* t) { uiPut(s, col, row, asciiGlyphs(t)); }
void frac(UiScreen& s, int col, int row, int a, int b) {       // "%3d/%3d" at col..col+6
    uiNum(s, col + 2, row, a);
    s[size_t(row * 20 + col + 3)] = 0xf1;
    uiNum(s, col + 6, row, b);
}
const char* kTop[7] = {"Item", "Magic", "Talent", "Status", "Equip", "Auto", "Save"};
}

FieldMenu::FieldMenu(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party,
                     std::function<bool(int)> save, std::function<bool(int, CharRecord&)> peek)
    : rom_(rom), ram_(ram), party_(party), save_(std::move(save)), peek_(std::move(peek)) {}

int FieldMenu::members() const { return party_.size() > 4 && party_[4].raw[3] != 0xff ? 5 : int(std::min<size_t>(party_.size(), 4)); }

// Equipment slot k takes: 0 weapons, 1 shields ... by the item's icon glyph ($A1 shield, $A2
// helm, $A3 armour, $A4 gloves, $A5 shoes, $A6 others; anything else below $C0 is a weapon).
bool FieldMenu::fits(int slot, uint8_t item) const {
    if (item < 0x30 || item >= 0xc0) return false;
    const uint8_t icon = itemName(rom_, item)[0];
    static const uint8_t want[8] = {0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa6};
    if (slot == 0) return icon < 0xa1 || icon > 0xa6;
    return icon == want[slot];
}

std::vector<int> FieldMenu::bagIndices() const {
    std::vector<int> v;
    for (int i = 0; i < 24; i++) if (ram_[size_t(0xc6ff + i * 2)] != 0xff) v.push_back(i);
    return v;
}

// Swap bag entry `bagIndex` (or nothing, -1) with equipment slot `slot` of member_.
void FieldMenu::equip(int slot, int bagIndex) {
    CharRecord& r = party_[size_t(member_)];
    const uint8_t old = r.raw[size_t(0x40 + slot)];
    if (bagIndex >= 0) {
        const size_t h = size_t(0xc6ff + bagIndex * 2);
        r.raw[size_t(0x40 + slot)] = ram_[h];
        if (--ram_[h + 1] == 0) { ram_[h] = 0xff; ram_[h + 1] = 0xff; }
    } else {
        r.raw[size_t(0x40 + slot)] = 0xff;
    }
    if (old != 0xff && inventoryAdd(ram_, old) < 0) {          // bag full: undo
        r.raw[size_t(0x40 + slot)] = old;
        say("Your bag is full!", "", State::Slots);
        return;
    }
    // Recompute with the game's own routines on a RAM image holding the party records.
    recomputeMember(vm(), rom_, member_);
    syncBack();
}

static BattleVM& vmStorage(const Rom& rom) { static BattleVM s(rom); return s; }

BattleVM& FieldMenu::vm() {
    BattleVM& storage = vmStorage(rom_);
    storage.mem = ram_;
    for (size_t i = 0; i < party_.size() && i < 5; i++)
        std::copy(party_[i].raw.begin(), party_[i].raw.end(), storage.mem.begin() + 0xc500 + long(i) * 0x60);
    return storage;
}

void FieldMenu::syncBack() {
    BattleVM& v = vmStorage(rom_);
    for (size_t i = 0; i < party_.size() && i < 5; i++)
        std::copy(v.mem.begin() + 0xc500 + long(i) * 0x60, v.mem.begin() + 0xc560 + long(i) * 0x60, party_[i].raw.begin());
}

// Spells a member knows: bits at record +$51-56 (09:51EA), with their MP cost (09:5D87).
std::vector<FieldMenu::Spell> FieldMenu::spells(int member) {
    std::vector<Spell> v;
    const CharRecord& r = party_[size_t(member)];
    BattleVM& m = vm();
    for (int k = 0; k < 6; k++)
        for (int bit = 0; bit < 8; bit++)
            if (r.raw[size_t(0x51 + k)] & (0x80 >> bit)) {
                const uint8_t id = uint8_t((k ^ 1) * 8 + 7 - bit);
                m.mem[0xce26] = id; m.mem[0xce25] = 0; m.mem[0xce27] = 0; m.mem[0xcd37] = 0;
                m.mem[0xcd36] = uint8_t(member);
                runLogicNative(m, rom_, 0x6b64);
                v.push_back({id, m.mem[0xce29]});
            }
    return v;
}

void FieldMenu::choose(uint8_t id, bool spell) {
    BattleVM& m = vm();
    m.mem[0xce26] = id; m.mem[0xce25] = 0; m.mem[0xce27] = spell ? 0 : 1; m.mem[0xcd37] = 0;
    m.mem[0xcd36] = uint8_t(member_);
    runLogicNative(m, rom_, 0x6b64);                         // 09:5D87: use kind wCE28
    const uint8_t kind = m.mem[0xce28];
    useId_ = id; useSpell_ = spell; back_ = spell ? State::Spells : State::Items;
    if (spell && party_[size_t(member_)].mp() < m.mem[0xce29]) { sfx(0x3b); say("Not enough MP.", "", back_); return; }
    if (!kind || (kind & 0x80) || !(kind & 0x18)) { sfx(0x3b); say("Can't use that here.", "", back_); return; }
    if (kind & 0x10) { use(0x80); return; }
    target_ = 0;
    state_ = State::Target;
}

void FieldMenu::use(int target) {
    BattleVM& m = vm();
    m.mem[0xce26] = useId_; m.mem[0xce25] = 0; m.mem[0xce27] = 0; m.mem[0xcd37] = 0;
    m.mem[0xcd36] = uint8_t(target);
    m.mem[0xce1c] = uint8_t(member_);
    auto rand8 = [this]() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return uint8_t(rng_ >> 8); };
    const bool ok = fieldUse(m, rom_, rand8, [this](int id) { sfx(id); });
    syncBack();
    if (!ok) { say("Nothing happened.", "", back_); return; }
    if (!useSpell_ && useId_ >= 0xc0) {                       // used up
        for (int i = 0; i < 24; i++) {
            const size_t h = size_t(0xc6ff + i * 2);
            if (ram_[h] != useId_) continue;
            if (--ram_[h + 1] == 0) { ram_[h] = 0xff; ram_[h + 1] = 0xff; }
            break;
        }
    }
    state_ = back_;
}

void FieldMenu::tick(const MenuInput& in) {
    const int n4 = members();
    auto step = [&](int& v, int n) { if (in.up) v = (v + n - 1) % n; if (in.down) v = (v + 1) % n; };
    switch (state_) {
    case State::Top:
        step(top_, 7);
        if (in.b) { state_ = State::Done; return; }
        if (!in.a) return;
        mark_ = -1;
        if (top_ == 0) { state_ = State::Items; first_ = 0; bag_ = 0; }
        else if (top_ == 5) { state_ = State::Auto; member_ = 1; }
        else if (top_ == 6) { state_ = State::Save; file_ = 0; }
        else { state_ = State::Member; }
        return;
    case State::Member:
        step(member_, n4);
        if (in.b) { state_ = State::Top; return; }
        if (in.a) {
            state_ = top_ == 4 ? State::Slots : top_ == 1 ? State::Spells : top_ == 2 ? State::Talents : State::Status;
            slot_ = 0; bag_ = 0; first_ = 0;
        }
        return;
    case State::Slots:                                         // 2 columns x 4 rows
        if (in.up) slot_ = (slot_ + 6) % 8;
        if (in.down) slot_ = (slot_ + 2) % 8;
        if (in.left || in.right) slot_ ^= 1;
        if (in.b) { state_ = State::Member; return; }
        if (in.a) { state_ = State::Bag; bag_ = 0; first_ = 0; }
        return;
    case State::Bag: {
        const auto bag = bagIndices();
        const int n = int(bag.size()) + 1;                     // one empty line past the end = take off
        step(bag_, n);
        first_ = std::max(0, std::min(bag_ - 3, n - 4));
        if (in.b) { state_ = State::Slots; return; }
        if (!in.a) return;
        if (bag_ < int(bag.size()) && !fits(slot_, ram_[size_t(0xc6ff + bag[size_t(bag_)] * 2)])) { sfx(0x3b); return; }
        state_ = State::Slots;
        equip(slot_, bag_ < int(bag.size()) ? bag[size_t(bag_)] : -1);
        return;
    }
    case State::Items: {
        const auto bag = bagIndices();
        const int n = std::max<int>(1, int(bag.size()));
        if (bag_ >= n) bag_ = n - 1;
        step(bag_, n);
        first_ = std::max(0, std::min(bag_ - 7, n - 8));
        if (in.b) { if (mark_ >= 0) mark_ = -1; else state_ = State::Top; return; }
        if (!in.a || bag.empty()) return;
        if (mark_ < 0) { mark_ = bag_; return; }               // first A marks the item
        if (mark_ != bag_) {                                   // A on another one: swap places
            const size_t x = size_t(0xc6ff + bag[size_t(mark_)] * 2), y = size_t(0xc6ff + bag[size_t(bag_)] * 2);
            std::swap(ram_[x], ram_[y]); std::swap(ram_[x + 1], ram_[y + 1]);
            mark_ = -1;
            return;
        }
        mark_ = -1;                                            // A on the same one: use it
        member_ = 0;
        choose(ram_[size_t(0xc6ff + bag[size_t(bag_)] * 2)], false);
        return;
    }
    case State::Spells: {
        const auto list = spells(member_);
        const int n = std::max<int>(1, int(list.size()));
        step(bag_, n);
        first_ = std::max(0, std::min(bag_ - 7, n - 8));
        if (in.b) { state_ = State::Member; return; }
        if (in.a && !list.empty()) choose(list[size_t(bag_)].id, true);
        return;
    }
    case State::Target:
        step(target_, n4);
        if (in.b) { state_ = back_; return; }
        if (in.a) use(target_);
        return;
    case State::Talents: case State::Status:
        if (in.a || in.b) state_ = State::Member;
        return;
    case State::Auto: {                                        // record +0 bit 7; never the leader
        const int n = n4 - 1;
        if (n < 1) { state_ = State::Top; return; }
        int k = member_ - 1;
        step(k, n);
        member_ = k + 1;
        if (in.b) { state_ = State::Top; member_ = 0; return; }
        if (in.a) party_[size_t(member_)].raw[0] ^= 0x80;
        return;
    }
    case State::Save:
        step(file_, 3);
        if (in.b) { state_ = State::Top; return; }
        if (in.a) { if (!(save_ && save_(file_))) sfx(0x3b); else sfx(0x35); }
        return;
    case State::Message:
        if (in.a || in.b) state_ = next_;
        return;
    case State::Done:
        return;
    }
}

int FieldMenu::cursorRow() const {
    switch (state_) {
    case State::Top: return 2 + top_ * 2;
    case State::Member: return member_ < 4 ? 2 + member_ * 3 : 15;
    case State::Target: return target_ < 4 ? 2 + target_ * 3 : 15;
    case State::Auto: return member_ < 4 ? 2 + member_ * 3 : 15;
    case State::Slots: return 10 + (slot_ / 2) * 2;
    case State::Bag: return 10 + (bag_ - first_) * 2;
    case State::Items: case State::Spells: return 2 + (bag_ - first_) * 2;
    case State::Save: return 7 + file_ * 4;
    default: return -1;
    }
}

int FieldMenu::cursorCol() const {                 // (the renderer draws the hand one tile left)
    switch (state_) {
    case State::Top: return 13;
    case State::Slots: return slot_ & 1 ? 11 : 2;
    case State::Items: case State::Spells: return 3;
    default: return 2;
    }
}

Glyphs FieldMenu::statusName(const CharRecord& r) const {      // 0E:6D73, 4 letters each
    int k = 0;
    if (r.raw[0] & 0x80) return asciiGlyphs("Auto");
    static const uint8_t bit[7] = {0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    for (int i = 0; i < 7 && !k; i++) if (r.raw[1] & bit[i]) k = i + 1;
    const uint8_t* p = rom_.raw(0x0e, uint16_t(0x6d73 + k * 4));
    return Glyphs(p, p + 4);
}

// One party entry: name, status, level / HP, MP (two rows from `row`).
void FieldMenu::memberLine(UiScreen& s, const CharRecord& r, int row) const {
    uiPut(s, 4, row, r.name());
    uiPut(s, 11, row, statusName(r));
    s[size_t(row * 20 + 16)] = 0xc5;                           // "L"
    uiNum(s, 18, row, r.raw[3]);
    frac(s, 4, row + 1, r.hp(), r.maxHp());
    frac(s, 12, row + 1, r.mp(), r.maxMp());
}

void FieldMenu::drawParty(UiScreen& s, int top, int firstMember) const {
    uiBox(s, 1, 14, 18, 3);
    uiBox(s, 1, top + 1, 18, 12 - top);
    for (int i = firstMember; i < int(std::min<size_t>(party_.size(), 4)); i++) memberLine(s, party_[size_t(i)], 2 + i * 3);
    if (members() == 5) memberLine(s, party_[4], 15);
}

void FieldMenu::drawMain(UiScreen& s) const {
    uiBox(s, 1, 14, 10, 3);
    uiBox(s, 1, 1, 10, 12);
    uiBox(s, 12, 1, 7, 14);
    uiBox(s, 12, 16, 7, 1);
    auto line = [&](const CharRecord& r, int row) {
        uiPut(s, 4, row, r.name());
        frac(s, 4, row + 1, r.hp(), r.maxHp());
    };
    for (int i = 0; i < int(std::min<size_t>(party_.size(), 4)); i++) line(party_[size_t(i)], 2 + i * 3);
    if (members() == 5) line(party_[4], 15);
    for (int i = 0; i < 7; i++) putStr(s, 13, 2 + i * 2, kTop[i]);
    uiNum(s, 17, 16, ram_[0xc6e0] | ram_[0xc6e1] << 8 | ram_[0xc6e2] << 16);
    s[16 * 20 + 18] = 0xc0;                                    // "G"
}

// Items / spells: icon + name from column 4, count (or MP cost) at column 13.
void FieldMenu::drawList(UiScreen& s) const {
    uiBox(s, 1, 1, 18, 16);
    if (state_ == State::Spells) {
        const auto sp = const_cast<FieldMenu*>(this)->spells(member_);
        for (int i = 0; i < 8 && first_ + i < int(sp.size()); i++) {
            uiPut(s, 4, 2 + i * 2, itemName(rom_, sp[size_t(first_ + i)].id));
            uiNum(s, 13, 2 + i * 2, sp[size_t(first_ + i)].cost);
        }
        return;
    }
    const auto bag = bagIndices();
    for (int i = 0; i < 8 && first_ + i < int(bag.size()); i++) {
        const size_t h = size_t(0xc6ff + bag[size_t(first_ + i)] * 2);
        uiPut(s, 4, 2 + i * 2, itemName(rom_, ram_[h]));
        uiNum(s, 13, 2 + i * 2, ram_[h + 1]);
        if (mark_ == first_ + i) s[size_t((2 + i * 2) * 20 + 2)] = 0xf5;
    }
}

void FieldMenu::drawEquip(UiScreen& s) const {
    const CharRecord& r = party_[size_t(member_)];
    uiBox(s, 1, 1, 18, 6);
    uiBox(s, 1, 9, 18, 8);
    s[1 * 20 + 10] = 0xc1; s[1 * 20 + 11] = 0xae;              // "HP"
    frac(s, 12, 1, r.hp(), r.maxHp());
    uiPut(s, 3, 2, r.name());
    s[2 * 20 + 10] = 0xc6; s[2 * 20 + 11] = 0xae;              // "MP"
    frac(s, 12, 2, r.mp(), r.maxMp());
    putStr(s, 1, 4, "Attack");  uiNum(s, 9, 4, r.raw[0x18]);
    putStr(s, 10, 4, "Defence"); uiNum(s, 18, 4, r.raw[0x19]);
    putStr(s, 1, 6, "Agility"); uiNum(s, 9, 6, r.raw[0x1a]);
    putStr(s, 10, 6, "Magic");  uiNum(s, 18, 6, r.raw[0x1b]);
    if (state_ == State::Slots) {
        for (int k = 0; k < 8; k++) {                          // 00:291D: names of the empty slots
            const int col = k & 1 ? 11 : 2, row = 10 + (k / 2) * 2;
            s[size_t(row * 20 + col)] = 0xf5;
            const uint8_t it = r.raw[size_t(0x40 + k)];
            Glyphs g;
            if (it == 0xff) { const uint8_t* p = rom_.raw(0, uint16_t(0x291d + k * 7)); g.assign(p, p + 7); }
            else { g = itemName(rom_, it); g.erase(g.begin()); }
            uiPut(s, col + 1, row, g);
        }
        return;
    }
    const auto bag = bagIndices();                             // what the slot can take; the rest is blanked
    for (int i = 0; i < 4 && first_ + i < int(bag.size()); i++) {
        const size_t h = size_t(0xc6ff + bag[size_t(first_ + i)] * 2);
        const int row = 10 + i * 2;
        if (fits(slot_, ram_[h])) {
            uiPut(s, 4, row, itemName(rom_, ram_[h]));
            uiNum(s, 13, row, ram_[h + 1]);
        } else {
            for (int c = 4; c < 12; c++) s[size_t(row * 20 + c)] = 0xf2;
            s[size_t(row * 20 + 13)] = 0xf2;
        }
    }
}

void FieldMenu::drawStatus(UiScreen& s) const {
    const CharRecord& r = party_[size_t(member_)];
    uiBox(s, 1, 1, 18, 16);
    uiPut(s, 3, 2, r.name());
    uiPut(s, 10, 2, statusName(r));
    s[2 * 20 + 16] = 0xc5;
    uiNum(s, 18, 2, r.raw[3]);
    // 0E:6D4F: class names, 6 letters (Robot Cyborg Beast Monst. Human Mutant)
    const int cls = (r.raw[0x0d] & 7) != 2 ? 3 : r.raw[0] & 0x40 ? 4 : 5;
    const uint8_t* p = rom_.raw(0x0e, uint16_t(0x6d4f + cls * 6));
    uiPut(s, 1, 4, Glyphs(p, p + 6));
    putStr(s, 9, 4, "NEXT");
    putStr(s, 9, 6, "EXP");
    const int exp = r.raw[4] | r.raw[5] << 8 | r.raw[6] << 16;
    uiNum(s, 18, 6, exp);
    {                                                          // 00:2A90: wCD9D[level] - EXP
        BattleVM& m = const_cast<FieldMenu*>(this)->vm();
        runLogicNative(m, rom_, 0x1e46);
        const size_t t = size_t(0xcd9d + r.raw[3] * 3);
        uiNum(s, 18, 4, std::max(0, (m.mem[t] | m.mem[t + 1] << 8 | m.mem[t + 2] << 16) - exp));
    }
    s[8 * 20 + 1] = 0xc1; s[8 * 20 + 2] = 0xae;
    frac(s, 3, 8, r.hp(), r.maxHp());
    s[8 * 20 + 10] = 0xc6; s[8 * 20 + 11] = 0xae;
    frac(s, 12, 8, r.mp(), r.maxMp());
    putStr(s, 1, 10, "Attack");  uiNum(s, 9, 10, r.raw[0x18]);
    putStr(s, 1, 12, "Defence"); uiNum(s, 9, 12, r.raw[0x19]);
    putStr(s, 1, 14, "Agility"); uiNum(s, 9, 14, r.raw[0x1a]);
    putStr(s, 1, 16, "Magic");   uiNum(s, 9, 16, r.raw[0x1b]);
    putStr(s, 10, 10, "Hit");     uiNum(s, 18, 10, r.raw[0x57]);
    putStr(s, 10, 12, "Evade");   uiNum(s, 18, 12, r.raw[0x3d]);
    putStr(s, 10, 14, "M.Def.");  uiNum(s, 18, 14, r.raw[0x3e]);
    putStr(s, 10, 16, "M.Evade"); uiNum(s, 18, 16, r.raw[0x3f]);
}

void FieldMenu::drawTalents(UiScreen& s) const {
    const CharRecord& r = party_[size_t(member_)];
    uiBox(s, 1, 1, 18, 2);
    uiBox(s, 1, 4, 18, 4);
    uiBox(s, 1, 9, 18, 8);
    int n = 0;
    for (int k = 0; k < 8 && n < 2; k++)                       // record +$48-4F
        if (r.raw[size_t(0x48 + k)] != 0xff) uiPut(s, n++ ? 11 : 2, 2, talentName(rom_, r.raw[size_t(0x48 + k)]));
    static const char* elem[4] = {"Quake", "Fire", "Ice", "Tornado"};
    static const char* stat[8] = {"Mute", "Confuse", "Blind", "Sleep", "Poison", "Para", "Curse", "Stone"};
    auto cell = [&](int col, int row, const char* t) {
        Glyphs g = asciiGlyphs(t);
        while (g.size() < 7) g.push_back(0xae);
        uiPut(s, col, row, g);
    };
    for (int i = 0; i < 4; i++) cell(i & 1 ? 11 : 2, 5 + (i / 2) * 2, elem[i]);
    for (int i = 0; i < 8; i++) cell(i & 1 ? 11 : 2, 10 + (i / 2) * 2, stat[i]);
}

void FieldMenu::drawSave(UiScreen& s) const {
    uiBox(s, 1, 1, 18, 3);
    putStr(s, 1, 1, "Please select file");
    putStr(s, 2, 3, "to save the data.");
    for (int f = 0; f < 3; f++) {
        uiBox(s, 1, 6 + f * 4, 18, 3);
        CharRecord r;
        if (peek_ && peek_(f, r)) memberLine(s, r, 7 + f * 4);
    }
}

std::vector<UiSprite> FieldMenu::sprites() const {
    std::vector<UiSprite> v;
    auto party = [&](int first) {
        for (int i = first; i < int(std::min<size_t>(party_.size(), 4)); i++) v.push_back(memberSprite(rom_, party_[size_t(i)], 16, 16 + i * 24));
        if (members() == 5) v.push_back(memberSprite(rom_, party_[4], 16, 120));
    };
    switch (state_) {
    case State::Top: case State::Member: case State::Target: party(0); break;
    case State::Auto: party(1); break;
    case State::Status: case State::Slots: case State::Bag: v.push_back(memberSprite(rom_, party_[size_t(member_)], 8, 8)); break;
    case State::Save:
        for (int f = 0; f < 3; f++) { CharRecord r; if (peek_ && peek_(f, r)) v.push_back(memberSprite(rom_, r, 16, 56 + f * 32)); }
        break;
    default: break;
    }
    return v;
}

UiScreen FieldMenu::screen() const {
    UiScreen s{};
    switch (state_) {
    case State::Top: drawMain(s); break;
    case State::Member: case State::Target: drawParty(s, 0, 0); break;
    case State::Auto:
        uiBox(s, 1, 1, 18, 2);
        putStr(s, 4, 2, "Auto Battle");
        drawParty(s, 3, 1);
        break;
    case State::Slots: case State::Bag: drawEquip(s); break;
    case State::Items: case State::Spells: drawList(s); break;
    case State::Status: drawStatus(s); break;
    case State::Talents: drawTalents(s); break;
    case State::Save: drawSave(s); break;
    case State::Message:
        uiBox(s, 1, 1, 18, 16);
        uiBox(s, 1, 6, 18, 6);
        putStr(s, 1, 7, message_.c_str());
        putStr(s, 1, 9, message2_.c_str());
        break;
    case State::Done: break;
    }
    return s;
}

} // namespace ffl3
