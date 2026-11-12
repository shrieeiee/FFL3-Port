#include "shop.h"
#include <algorithm>

namespace ffl3 {

namespace {
const uint8_t kSp = 0xff, kQ = 0xf4, kBang = 0xf3, kG = 0xc0, kSlash = 0xf1;
Glyphs operator+(Glyphs a, const Glyphs& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
Glyphs one(uint8_t c) { return Glyphs{c}; }
}

Shop::Shop(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, int id)
    : rom_(rom), ram_(ram), party_(party), id_(id & 0x7f), mix_(id & 0x80) {
    if (id_ < 0x40) {
        const uint16_t p = rom.w(1, uint16_t(0x7804 + (id_ >> 4) * 2));
        for (int i = 0; i < 6; i++) wares_[i] = rom.b(1, uint16_t(p + (id_ & 15) * 6 + i));
        for (int i = 0; i < 4; i++)                              // 01:742A
            for (int k = 0; k < 2; k++) recipe_[i][k] = rom.b(1, uint16_t((id_ < 0x10 ? 0x77f4 : 0x77fc) + i * 2 + k));
    } else if (id_ < 0x50) {
        state_ = State::Inn;
        cursor_ = 0;
    } else {
        state_ = State::Done;                                    // $50+: 01:76C7, a text - unused
    }
}

int Shop::price(const Rom& rom, uint8_t item) {                // 00:2E54
    if (item >= 0xd2) return 0;
    return rom.w(1, uint16_t(0x7e4f + item * 2));
}

int Shop::gold() const { return ram_[0xc6e0] | ram_[0xc6e1] << 8 | ram_[0xc6e2] << 16; }

void Shop::setGold(int g) {
    g = std::min(std::max(g, 0), 999999);                       // 01:77CA cap
    ram_[0xc6e0] = uint8_t(g); ram_[0xc6e1] = uint8_t(g >> 8); ram_[0xc6e2] = uint8_t(g >> 16);
}

int Shop::members() const { return party_.size() > 4 && party_[4].raw[3] != 0xff ? 5 : 4; }

int Shop::innCost() const {                                    // 01:7731
    int c = 0;
    for (int i = 0; i < members(); i++) {
        const CharRecord& r = party_[size_t(i)];
        if (r.raw[1] & 0x40) continue;
        c += (r.maxHp() - r.hp()) + (r.maxMp() - r.mp());
    }
    return c;
}

int Shop::bagCount(uint8_t item) const {
    for (int i = 0; i < 24; i++)
        if (ram_[size_t(0xc6ff + i * 2)] == item) return ram_[size_t(0xc700 + i * 2)];
    return 0;
}

void Shop::bagTake(uint8_t item, int n) {
    for (int i = 0; i < 24; i++) {
        const size_t h = size_t(0xc6ff + i * 2);
        if (ram_[h] != item) continue;
        if (ram_[h + 1] <= n) { ram_[h] = 0xff; ram_[h + 1] = 0xff; }
        else ram_[h + 1] = uint8_t(ram_[h + 1] - n);
        return;
    }
}

void Shop::say(Glyphs l1, Glyphs l2, State after) {
    line1_ = std::move(l1); line2_ = std::move(l2);
    after_ = after;
    state_ = State::Message;
}

void Shop::confirm(Ask what) { ask_ = what; asked_ = state_; yes_ = 0; state_ = State::Confirm; }

// 01:75E1 answered. Texts: 01:795C[n] -> 09:7579 ...
void Shop::answer(bool yes) {
    const Glyphs thanks = msg(0x752f), full = msg(0x759c);
    switch (ask_) {
    case Ask::Buy:
        if (!yes) { state_ = State::Buy; return; }
        if (inventoryAdd(ram_, item_) < 0) { error(); say(full, {}, State::Buy); return; }
        setGold(gold() - price(rom_, item_));
        sfx(0x35);
        say(thanks, {}, State::Buy);
        return;
    case Ask::Sell:
        if (!yes) { state_ = State::Sell; return; }
        bagTake(item_, 1);
        setGold(gold() + price(rom_, item_) / 2);
        say(thanks, {}, State::Sell);
        return;
    case Ask::Mix: {                                             // 01:7373
        if (!yes) { state_ = State::MixList; return; }
        const int light = recipe_[cursor_][1] >> 4, dark = recipe_[cursor_][1] & 15;
        if (bagCount(0xd7) < light || bagCount(0xd6) < dark) { say(msg(0x7625), {}, State::MixList); return; }
        bagTake(0xd7, light); bagTake(0xd6, dark);
        if (inventoryAdd(ram_, item_) < 0) {                     // 73B8: stones back
            for (int i = 0; i < light; i++) inventoryAdd(ram_, 0xd7);
            for (int i = 0; i < dark; i++) inventoryAdd(ram_, 0xd6);
            error();
            say(full, {}, State::MixList);
            return;
        }
        sfx(0x35);
        say(thanks, {}, State::MixList);
        return;
    }
    case Ask::Element:                                           // 01:74E6
        picks_ = 0;
        if (yes && inventoryAdd(ram_, result_) >= 0) {
            sfx(0x35);
            pick_[0] = pick_[1] = 0xff; result_ = 0;
            say(thanks, {}, State::Elements);
            return;
        }
        inventoryAdd(ram_, pick_[0]); inventoryAdd(ram_, pick_[1]);   // 7573: both back
        pick_[0] = pick_[1] = 0xff; result_ = 0;
        if (yes) { error(); say(full, {}, State::Elements); }
        else state_ = State::Elements;
        return;
    }
}

void Shop::tick(const ShopInput& in) {
    switch (state_) {
    case State::Menu: {                                          // 01:704A
        const int n = mix_ ? 4 : 3;
        if (in.right) option_ = (option_ + 1) % n;
        if (in.left) option_ = (option_ + n - 1) % n;
        if (in.b) { state_ = State::Done; return; }
        if (!in.a) return;
        cursor_ = first_ = 0;
        if (option_ == 0) state_ = State::Done;
        else if (option_ == 1) state_ = State::Buy;
        else if (option_ == 2) state_ = State::Sell;
        else if (id_ < 0x20) state_ = State::MixList;
        else { state_ = State::Elements; picks_ = 0; pick_[0] = pick_[1] = 0xff; result_ = 0; }
        return;
    }
    case State::Buy: {                                           // 01:7187
        const int last = wares_[4] != 0xff ? 5 : 3;
        if (in.up) {
            if (cursor_ > 0) { if (--cursor_ < first_) first_ = cursor_; }
            else if (last == 3) cursor_ = 3;
            else error();
        }
        if (in.down) {
            if (cursor_ < last) { if (++cursor_ > first_ + 3) first_ = cursor_ - 3; }
            else if (last == 3) cursor_ = 0;
            else error();
        }
        if (in.b) { state_ = State::Menu; return; }
        if (!in.a) return;
        item_ = wares_[cursor_];
        if (item_ == 0xff) { error(); return; }
        if (gold() < price(rom_, item_)) { say(msg(0x753c), {}, State::Buy); return; }
        confirm(Ask::Buy);
        return;
    }
    case State::Sell: {                                          // 01:725D
        if (in.up) { if (cursor_ > 0) { if (--cursor_ < first_) first_ = cursor_; } else error(); }
        if (in.down) { if (cursor_ < 23) { if (++cursor_ > first_ + 3) first_ = cursor_ - 3; } else error(); }
        if (in.b) { state_ = State::Menu; return; }
        if (!in.a) return;
        item_ = ram_[size_t(0xc6ff + cursor_ * 2)];
        if (item_ == 0xff) { error(); return; }
        if (price(rom_, item_) / 2 == 0 && price(rom_, item_) == 0) { say(msg(0x75ce), {}, State::Sell); return; }
        confirm(Ask::Sell);
        return;
    }
    case State::MixList:                                         // 01:733C
        if (in.up) cursor_ = (cursor_ + 3) % 4;
        if (in.down) cursor_ = (cursor_ + 1) % 4;
        if (in.b) { state_ = State::Menu; return; }
        if (!in.a) return;
        item_ = recipe_[cursor_][0];
        confirm(Ask::Mix);
        return;
    case State::Elements: {                                      // 01:745B
        if (in.up) cursor_ = (cursor_ + 3) % 4;
        if (in.down) cursor_ = (cursor_ + 1) % 4;
        if (in.b) {
            if (picks_) { inventoryAdd(ram_, pick_[0]); pick_[0] = 0xff; picks_ = 0; }   // 7539
            else state_ = State::Menu;
            return;
        }
        if (!in.a) return;
        const uint8_t el = uint8_t(0xd2 + cursor_);
        if (!bagCount(el)) { error(); return; }
        bagTake(el, 1);
        pick_[picks_++] = el;
        if (picks_ < 2) return;
        result_ = rom_.b(1, uint16_t(0x77e4 + (pick_[1] - 0xd2) * 4 + (pick_[0] - 0xd2)));
        if (!result_) {                                          // "That combination is no good."
            inventoryAdd(ram_, pick_[0]); inventoryAdd(ram_, pick_[1]);
            say(msg(0x7654), msg(0x7666), State::Elements);
            return;
        }
        confirm(Ask::Element);
        return;
    }
    case State::Confirm:
        if (in.left || in.right) yes_ ^= 1;
        if (in.b) answer(false);
        else if (in.a) answer(yes_ == 0);
        return;
    case State::Inn: {                                           // 01:7621
        if (in.left || in.right) cursor_ ^= 1;
        if (in.b) { state_ = State::Done; return; }
        if (!in.a) return;
        if (cursor_ == 1) { state_ = State::Done; return; }
        const int c = innCost();
        if (gold() < c) { say({}, msg(0x753c), State::Inn); return; }
        setGold(gold() - c);
        for (int i = 0; i < members(); i++) {
            CharRecord& r = party_[size_t(i)];
            if (r.raw[1] & 0x40) continue;
            r.setHp(r.maxHp()); r.setMp(r.maxMp());
        }
        say({}, msg(0x752f), State::Done);
        return;
    }
    case State::Message:
        if (!(in.a || in.b)) return;
        if (after_ == State::Elements) { pick_[0] = pick_[1] = 0xff; picks_ = 0; result_ = 0; }
        state_ = after_;
        return;
    case State::Done:
        return;
    }
}

int Shop::cursorRow() const {
    switch (state_) {
    case State::Menu: return 4;
    case State::Buy: case State::Sell: return 7 + (cursor_ - first_) * 2;
    case State::MixList: case State::Elements: return 7 + cursor_ * 2;
    case State::Confirm: return 10;
    case State::Inn: return 7;
    default: return -1;
    }
}

int Shop::cursorCol() const {                                   // (the renderer draws one column left)
    return cursorTile() + 1;
}

int Shop::cursorTile() const {
    switch (state_) {
    case State::Menu: { static const int c[4] = {1, 6, 10, 15}; return c[option_]; }   // 01:70AD
    case State::Elements: return 9;
    case State::Confirm: return yes_ ? 10 : 6;
    case State::Inn: return cursor_ ? 14 : 9;
    default: return 2;
    }
}

std::vector<UiSprite> Shop::sprites() const {
    std::vector<UiSprite> v;
    const State st = state_ == State::Message ? after_ : state_ == State::Confirm ? asked_ : state_;
    if (st == State::Elements || state_ == State::Done) return v;
    const bool inn = id_ >= 0x40;
    if (inn) {                                                   // 09:7685: two columns
        for (int i = 0; i < members(); i++)
            v.push_back(memberSprite(rom_, party_[size_t(i)], 9 + (i & 1) * 72, 72 + (i / 2) * 24));
    } else {                                                     // 09:7738: a row along the bottom
        for (int i = 0; i < members(); i++) v.push_back(memberSprite(rom_, party_[size_t(i)], 9 + i * 16, 120));
    }
    return v;
}

// The shop's windows (01:711A): party, gold, wares / bag, message.
void Shop::drawShop(UiScreen& s, bool listOnTop) const {
    const State st = state_ == State::Message ? after_ : state_ == State::Confirm ? asked_ : state_;
    uiBox(s, 1, 15, 10, 2);
    uiBox(s, 12, 15, 7, 2);
    uiNum(s, 17, 16, gold());
    s[16 * 20 + 18] = kG;
    if (!listOnTop) uiBox(s, 1, 6, 18, 8);
    uiBox(s, 1, 1, 18, 4);
    if (listOnTop) uiBox(s, 1, 6, 18, 8);
    for (int i = 0; i < 4; i++) {
        const int row = 7 + i * 2;
        if (st == State::Sell) {                                 // 09:76FD: the bag, with counts
            const size_t h = size_t(0xc6ff + (first_ + i) * 2);
            if (ram_[h] == 0xff) continue;
            uiPut(s, 4, row, itemName(rom_, ram_[h]));
            uiNum(s, 13, row, ram_[h + 1]);
        } else if (st == State::MixList) {                       // 09:774D
            uiPut(s, 4, row, itemName(rom_, recipe_[i][0]));
        } else {                                                 // wares and prices
            const int k = (st == State::Buy ? first_ : 0) + i;
            if (wares_[k] == 0xff) continue;
            uiPut(s, 3, row, itemName(rom_, wares_[k]));
            uiNum(s, 16, row, price(rom_, wares_[k]));
            s[size_t(row * 20 + 17)] = kG;
        }
    }
}

// 01:75B4: First / Second on the left, the four elements and how many are held on the right.
void Shop::drawElements(UiScreen& s) const {
    uiBox(s, 1, 15, 18, 2);
    uiPut(s, 1, 16, msg(0x778c));
    if (result_) uiPut(s, 13, 16, itemName(rom_, result_));
    uiBox(s, 1, 6, 7, 8);
    uiBox(s, 9, 6, 10, 8);
    uiBox(s, 1, 1, 18, 4);
    s[5 * 20 + 8] = 0xf7; s[5 * 20 + 0] = 0xf8; s[5 * 20 + 19] = 0xfa;
    const Glyphs fs = msg(0x7761);                               // "First", "", "Second"
    int row = 7; Glyphs line;
    for (size_t i = 0; i <= fs.size(); i++) {
        if (i == fs.size() || fs[i] == 0x06) { uiPut(s, 1, row, line); line.clear(); row += 2; continue; }
        line.push_back(fs[i]);
    }
    for (int k = 0; k < 2; k++)
        if (pick_[k] != 0xff) uiPut(s, 2, 9 + k * 4, name(pick_[k]));
    for (int i = 0; i < 4; i++) {
        uiPut(s, 10, 7 + i * 2, name(uint8_t(0xd2 + i)));
        uiNum(s, 17, 7 + i * 2, bagCount(uint8_t(0xd2 + i)));
    }
}

// 01:7697: greeting and price, Stay / Exit, everyone's HP and MP, gold.
void Shop::drawInn(UiScreen& s) const {
    uiBox(s, 1, 1, 18, 6);
    uiBox(s, 1, 9, 18, 8);
    for (int i = 0; i < members(); i++) {
        const CharRecord& r = party_[size_t(i)];
        const int col = 3 + (i & 1) * 9, row = 9 + (i / 2) * 3;
        uiNum(s, col + 2, row, r.hp()); s[size_t(row * 20 + col + 3)] = kSlash; uiNum(s, col + 6, row, r.maxHp());
        uiNum(s, col + 2, row + 1, r.mp()); s[size_t((row + 1) * 20 + col + 3)] = kSlash; uiNum(s, col + 6, row + 1, r.maxMp());
    }
    uiBox(s, 11, 15, 8, 2);
    uiNum(s, 17, 16, gold());
    s[16 * 20 + 18] = kG;
    if (state_ == State::Inn) {
        uiBox(s, 9, 6, 10, 2);
        uiPut(s, 9, 7, msg(0x754c));                             // " Stay Exit"
    }
}

UiScreen Shop::screen() const {
    UiScreen s{};
    if (state_ == State::Done) return s;
    const State st = state_ == State::Message ? after_ : state_ == State::Confirm ? asked_ : state_;
    Glyphs l1, l2;
    if (id_ >= 0x40) {
        drawInn(s);
        if (state_ == State::Inn) {                              // "Howdy!" / "nG/night."
            l1 = msg(0x751c);
            l2 = one(kSp) + uiNumber(innCost()) + one(kG) + uiTrim(asciiGlyphs("/night."));
        }
    } else if (st == State::Elements) {
        drawElements(s);
        l1 = msg(0x7635); l2 = msg(0x7643);                      // "Which do you" "want to mix?"
    } else {
        drawShop(s, state_ != State::Menu);
        switch (state_) {
        case State::Menu: l1 = msg(0x7579); l2 = msg(mix_ ? 0x7566 : 0x7557); break;
        case State::Buy: l1 = msg(0x758a); break;
        case State::Sell: l1 = msg(0x75af); break;
        case State::MixList: l1 = msg(0x75e7); l2 = msg(0x75f9); break;
        default: break;
        }
    }
    if (state_ == State::Message) { l1 = line1_; l2 = line2_; }
    if (state_ == State::Confirm) {
        switch (ask_) {
        case Ask::Buy: l1 = one(kSp) + name(item_) + one(kQ); break;
        case Ask::Element: l1 = one(kSp) + name(result_) + one(kQ); break;
        case Ask::Sell:                                          // "X will be" "nG!"
            l1 = one(kSp) + name(item_) + uiTrim(asciiGlyphs(" will be"));
            l2 = one(kSp) + uiNumber(price(rom_, item_) / 2) + one(kG) + one(kBang);
            break;
        case Ask::Mix:                                           // "n Light and" "n Dark. Okay?"
            l1 = uiNumber(recipe_[cursor_][1] >> 4) + one(kSp) + name(0xd7) + uiTrim(asciiGlyphs(" and"));
            l2 = uiNumber(recipe_[cursor_][1] & 15) + one(kSp) + name(0xd6) + uiTrim(asciiGlyphs(". Okay?"));
            break;
        }
        uiBox(s, 6, 9, 8, 2);
        uiPut(s, 6, 10, msg(0x75c6));                            // " Yes No"
    }
    uiPut(s, 1, 2, l1);
    uiPut(s, 1, 4, l2);
    return s;
}

} // namespace ffl3
