#include "shop.h"
#include <algorithm>
#include <cstdio>

namespace ffl3 {

namespace {
void put(std::array<uint8_t, 360>& s, int col, int row, const Glyphs& g) {
    for (size_t i = 0; i < g.size() && col + int(i) < 20; i++) s[size_t(row * 20 + col + int(i))] = g[i];
}
void putStr(std::array<uint8_t, 360>& s, int col, int row, const char* t) { put(s, col, row, asciiGlyphs(t)); }
void box(std::array<uint8_t, 360>& s, int c0, int r0, int w, int h) {
    for (int r = r0; r < r0 + h; r++)
        for (int c = c0; c < c0 + w; c++) {
            uint8_t t = 0xff;
            const bool top = r == r0, bot = r == r0 + h - 1, l = c == c0, rt = c == c0 + w - 1;
            if (top) t = l ? 0xf7 : rt ? 0xf9 : 0xfc;
            else if (bot) t = l ? 0xf8 : rt ? 0xfa : 0xfc;
            else if (l || rt) t = 0xfb;
            s[size_t(r * 20 + c)] = t;
        }
}
}

Shop::Shop(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, int id)
    : rom_(rom), ram_(ram), party_(party), id_(id & 0x7f) {
    if (id_ < 0x40) {
        const uint16_t t = uint16_t(0x7804 + (id_ >> 4) * 2);
        const uint16_t p = uint16_t(rom.b(1, t) | rom.b(1, uint16_t(t + 1)) << 8);
        for (int i = 0; i < 6; i++) {
            const uint8_t it = rom.b(1, uint16_t(p + (id_ & 15) * 6 + i));
            if (it != 0xff) wares_.push_back(it);
        }
    } else if (id_ >= 0x50) {
        state_ = State::Inn;
    } else {
        say("(This shop isn't in", State::Done);     // $40-$4F: special shops
        message_ += "\n the port yet.)";
    }
}

int Shop::price(const Rom& rom, uint8_t item) {                // 00:2E54
    if (item >= 0xd2) return 0;
    const uint16_t a = uint16_t(0x7e4f + item * 2);
    return rom.b(1, a) | rom.b(1, uint16_t(a + 1)) << 8;
}

int Shop::gold() const { return ram_[0xc6e0] | ram_[0xc6e1] << 8 | ram_[0xc6e2] << 16; }

void Shop::setGold(int g) {
    g = std::min(std::max(g, 0), 999999);                       // 01:77CA cap
    ram_[0xc6e0] = uint8_t(g); ram_[0xc6e1] = uint8_t(g >> 8); ram_[0xc6e2] = uint8_t(g >> 16);
}

int Shop::innCost() const {                                    // 01:7731
    int c = 0;
    for (const CharRecord& r : party_) {
        if (r.raw[3] == 0xff || (r.raw[1] & 0x40)) continue;
        c += (r.maxHp() - r.hp()) + (r.maxMp() - r.mp());
    }
    return c;
}

void Shop::say(const char* s, State after) { message_ = s; after_ = after; state_ = State::Message; }

void Shop::tick(const ShopInput& in) {
    switch (state_) {
    case State::Menu:
        if (in.up) cursor_ = (cursor_ + 2) % 3;
        if (in.down) cursor_ = (cursor_ + 1) % 3;
        if (in.b) { state_ = State::Done; return; }
        if (!in.a) return;
        if (cursor_ == 2) { state_ = State::Done; return; }
        state_ = cursor_ == 0 ? State::Buy : State::Sell;
        cursor_ = first_ = 0;
        return;
    case State::Buy: case State::Sell: {
        std::vector<uint8_t> items;
        if (state_ == State::Buy) items = wares_;
        else for (int i = 0; i < 24; i++) if (ram_[size_t(0xc6ff + i * 2)] != 0xff) items.push_back(ram_[size_t(0xc6ff + i * 2)]);
        if (in.b || items.empty()) { if (in.b || in.a) { state_ = State::Menu; cursor_ = 0; } return; }
        const int n = int(items.size());
        if (in.up) cursor_ = (cursor_ + n - 1) % n;
        if (in.down) cursor_ = (cursor_ + 1) % n;
        cursor_ = std::min(cursor_, n - 1);
        first_ = std::max(0, std::min(cursor_ - 5, n - 6));
        if (!in.a) return;
        const uint8_t it = items[size_t(cursor_)];
        const int p = price(rom_, it);
        if (state_ == State::Buy) {
            if (gold() < p) { sfx(0x3b); say("Not enough GP.", State::Buy); return; }
            if (inventoryAdd(ram_, it) < 0) { sfx(0x3b); say("You can't carry", State::Buy); message_ += "\nany more."; return; }
            setGold(gold() - p);
            sfx(0x35);
            say("Thank you!", State::Buy);
        } else {
            for (int i = 0; i < 24; i++) {
                const size_t h = size_t(0xc6ff + i * 2);
                if (ram_[h] != it) continue;
                if (--ram_[h + 1] == 0) {                    // remove the entry
                    ram_[h] = 0xff;
                    ram_[h + 1] = 0xff;
                }
                break;
            }
            setGold(gold() + p / 2);
            say("Thank you!", State::Sell);
        }
        return;
    }
    case State::Inn: {
        if (in.up || in.down) cursor_ ^= 1;
        if (in.b) { state_ = State::Done; return; }
        if (!in.a) return;
        if (cursor_ == 1) { state_ = State::Done; return; }
        const int c = innCost();
        if (gold() < c) { sfx(0x3b); say("Not enough GP.", State::Done); return; }
        setGold(gold() - c);
        for (CharRecord& r : party_) {
            if (r.raw[3] == 0xff || (r.raw[1] & 0x40)) continue;
            r.setHp(r.maxHp()); r.setMp(r.maxMp());
        }
        say("Rest well!", State::Done);
        return;
    }
    case State::Message:
        if (in.a || in.b) { state_ = after_; }
        return;
    case State::Done:
        return;
    }
}

int Shop::cursorRow() const {
    switch (state_) {
    case State::Menu: return 11 + cursor_ * 2;
    case State::Buy: case State::Sell: return 3 + (cursor_ - first_) * 2;
    case State::Inn: return 13 + cursor_ * 2;
    default: return -1;
    }
}

std::array<uint8_t, 360> Shop::screen() const {
    std::array<uint8_t, 360> s{};
    char buf[32];
    // gold box (top right)
    box(s, 9, 0, 11, 3);
    std::snprintf(buf, sizeof buf, "%6dGP", gold());
    putStr(s, 10, 1, buf);
    switch (state_) {
    case State::Menu:
        box(s, 0, 9, 9, 9);
        putStr(s, 2, 11, "Buy");
        putStr(s, 2, 13, "Sell");
        putStr(s, 2, 15, "Exit");
        break;
    case State::Buy: case State::Sell: {
        std::vector<uint8_t> items;
        std::vector<int> counts;
        if (state_ == State::Buy) for (uint8_t w : wares_) { items.push_back(w); counts.push_back(-1); }
        else for (int i = 0; i < 24; i++)
            if (ram_[size_t(0xc6ff + i * 2)] != 0xff) { items.push_back(ram_[size_t(0xc6ff + i * 2)]); counts.push_back(ram_[size_t(0xc700 + i * 2)]); }
        box(s, 0, 2, 20, 14);
        for (int i = 0; i < 6 && first_ + i < int(items.size()); i++) {
            const uint8_t it = items[size_t(first_ + i)];
            put(s, 2, 3 + i * 2, itemName(rom_, it));
            const int p = state_ == State::Buy ? price(rom_, it) : price(rom_, it) / 2;
            std::snprintf(buf, sizeof buf, "%5d", p);
            putStr(s, 13, 3 + i * 2, buf);
            if (counts[size_t(first_ + i)] >= 0) { std::snprintf(buf, sizeof buf, "%d", counts[size_t(first_ + i)]); putStr(s, 11, 3 + i * 2, buf); }
        }
        if (items.empty()) putStr(s, 2, 3, state_ == State::Sell ? "Nothing to sell." : "Sold out.");
        break;
    }
    case State::Inn:
        box(s, 0, 9, 20, 9);
        std::snprintf(buf, sizeof buf, "Rest for %d GP?", innCost());
        putStr(s, 1, 11, buf);
        putStr(s, 2, 13, "Yes");
        putStr(s, 2, 15, "No");
        break;
    case State::Message: {
        box(s, 0, 12, 20, 6);
        int row = 14, col = 1;
        for (char c : message_) {
            if (c == '\n') { row += 2; col = 1; continue; }
            char t[2] = {c, 0};
            putStr(s, col++, row, t);
        }
        break;
    }
    case State::Done: break;
    }
    return s;
}

} // namespace ffl3
