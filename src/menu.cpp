#include "menu.h"
#include "battlenatives.h"
#include <algorithm>
#include <cstdio>

namespace ffl3 {

namespace {
using Screen = std::array<uint8_t, 360>;
void put(Screen& s, int col, int row, const Glyphs& g) {
    for (size_t i = 0; i < g.size() && col + int(i) < 20; i++) s[size_t(row * 20 + col + int(i))] = g[i];
}
void putStr(Screen& s, int col, int row, const char* t) { put(s, col, row, asciiGlyphs(t)); }
void box(Screen& s, int c0, int r0, int w, int h) {
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
const char* kTop[5] = {"Equip", "Items", "Magic", "Status", "Save"};
}

FieldMenu::FieldMenu(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, std::function<bool()> save)
    : rom_(rom), ram_(ram), party_(party), save_(std::move(save)) {}

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
        message_ = "The bag is full.";
        next_ = State::Slots; state_ = State::Message;
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
    if (spell && party_[size_t(member_)].mp() < m.mem[0xce29]) { sfx(0x3b); say("Not enough MP.", back_); return; }
    if (!kind || (kind & 0x80) || !(kind & 0x18)) { sfx(0x3b); say("Can't use that here.", back_); return; }
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
    if (!ok) { say("Nothing happened.", back_); return; }
    if (!useSpell_ && useId_ >= 0xc0) {                       // used up
        for (int i = 0; i < 24; i++) {
            const size_t h = size_t(0xc6ff + i * 2);
            if (ram_[h] != useId_) continue;
            if (--ram_[h + 1] == 0) { ram_[h] = 0xff; ram_[h + 1] = 0xff; }
            break;
        }
    }
    char buf[32];
    if (target < 5) {
        const CharRecord& r = party_[size_t(target)];
        std::snprintf(buf, sizeof buf, "HP %d/%d MP %d/%d", r.hp(), r.maxHp(), r.mp(), r.maxMp());
    } else {
        std::snprintf(buf, sizeof buf, "Done.");
    }
    say(buf, back_);
}

void FieldMenu::tick(const MenuInput& in) {
    const int members = int(std::min<size_t>(party_.size(), 4));
    switch (state_) {
    case State::Top:
        if (in.up) top_ = (top_ + 4) % 5;
        if (in.down) top_ = (top_ + 1) % 5;
        if (in.b) { state_ = State::Done; return; }
        if (!in.a) return;
        if (top_ == 1) { state_ = State::Items; first_ = 0; bag_ = 0; }
        else if (top_ == 4) { message_ = save_ && save_() ? "Saved." : "Couldn't save."; next_ = State::Top; state_ = State::Message; }
        else { state_ = State::Member; }
        return;
    case State::Member:
        if (in.up) member_ = (member_ + members - 1) % members;
        if (in.down) member_ = (member_ + 1) % members;
        if (in.b) { state_ = State::Top; return; }
        if (in.a) {
            state_ = top_ == 0 ? State::Slots : top_ == 2 ? State::Spells : State::Status;
            slot_ = 0; bag_ = 0; first_ = 0;
        }
        return;
    case State::Slots:
        if (in.up) slot_ = (slot_ + 7) % 8;
        if (in.down) slot_ = (slot_ + 1) % 8;
        if (in.b) { state_ = State::Member; return; }
        if (in.a) { state_ = State::Bag; bag_ = 0; first_ = 0; }
        return;
    case State::Bag: {
        const auto bag = bagIndices();
        const int n = int(bag.size()) + 1;                     // last entry = "(remove)"
        if (in.up) bag_ = (bag_ + n - 1) % n;
        if (in.down) bag_ = (bag_ + 1) % n;
        first_ = std::max(0, std::min(bag_ - 5, n - 6));
        if (in.b) { state_ = State::Slots; return; }
        if (in.a) { state_ = State::Slots; equip(slot_, bag_ < int(bag.size()) ? bag[size_t(bag_)] : -1); }
        return;
    }
    case State::Items: {
        const auto bag = bagIndices();
        const int n = std::max<int>(1, int(bag.size()));
        if (bag_ >= n) bag_ = n - 1;
        if (in.up) bag_ = (bag_ + n - 1) % n;
        if (in.down) bag_ = (bag_ + 1) % n;
        first_ = std::max(0, std::min(bag_ - 5, n - 6));
        if (in.b) { state_ = State::Top; return; }
        if (in.a && !bag.empty()) { member_ = 0; choose(ram_[size_t(0xc6ff + bag[size_t(bag_)] * 2)], false); }
        return;
    }
    case State::Spells: {
        const auto list = spells(member_);
        const int n = std::max<int>(1, int(list.size()));
        if (in.up) bag_ = (bag_ + n - 1) % n;
        if (in.down) bag_ = (bag_ + 1) % n;
        first_ = std::max(0, std::min(bag_ - 5, n - 6));
        if (in.b) { state_ = State::Member; return; }
        if (in.a && !list.empty()) choose(list[size_t(bag_)].id, true);
        return;
    }
    case State::Target:
        if (in.up) target_ = (target_ + members - 1) % members;
        if (in.down) target_ = (target_ + 1) % members;
        if (in.b) { state_ = back_; return; }
        if (in.a) use(target_);
        return;
    case State::Status:
        if (in.up) member_ = (member_ + members - 1) % members;
        if (in.down) member_ = (member_ + 1) % members;
        if (in.a || in.b) state_ = State::Member;
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
    case State::Member: return 2 + member_ * 2;
    case State::Slots: return 2 + slot_ * 2;
    case State::Bag: case State::Items: case State::Spells: return 2 + (bag_ - first_) * 2;
    case State::Target: return 2 + target_ * 2;
    default: return -1;
    }
}

int FieldMenu::cursorCol() const {
    switch (state_) {
    case State::Top: return 1;
    case State::Member: return 9;
    default: return 1;
    }
}

std::array<uint8_t, 360> FieldMenu::screen() const {
    Screen s{};
    char buf[32];
    auto name = [&](int i) { return party_[size_t(i)].name(); };
    auto list = [&](bool withRemove) {
        box(s, 0, 0, 20, 14);
        const auto bag = bagIndices();
        for (int i = 0; i < 6; i++) {
            const int k = first_ + i;
            if (k < int(bag.size())) {
                const size_t h = size_t(0xc6ff + bag[size_t(k)] * 2);
                put(s, 2, 2 + i * 2, itemName(rom_, ram_[h]));
                std::snprintf(buf, sizeof buf, "%2d", ram_[h + 1]);
                putStr(s, 16, 2 + i * 2, buf);
            } else if (withRemove && k == int(bag.size())) {
                putStr(s, 2, 2 + i * 2, "- nothing -");
            }
        }
        if (bag.empty() && !withRemove) putStr(s, 2, 2, "(empty)");
    };
    switch (state_) {
    case State::Top: case State::Member: {
        box(s, 0, 0, 8, 12);
        for (int i = 0; i < 5; i++) putStr(s, 2, 2 + i * 2, kTop[i]);
        if (state_ == State::Member) {
            box(s, 8, 0, 12, 12);
            for (int i = 0; i < int(std::min<size_t>(party_.size(), 4)); i++) put(s, 10, 2 + i * 2, name(i));
        }
        box(s, 0, 15, 20, 3);
        const int g = ram_[0xc6e0] | ram_[0xc6e1] << 8 | ram_[0xc6e2] << 16;
        std::snprintf(buf, sizeof buf, "%dGP", g);
        putStr(s, 1, 16, buf);
        break;
    }
    case State::Slots: {
        box(s, 0, 0, 20, 18);
        const CharRecord& r = party_[size_t(member_)];
        for (int i = 0; i < 8; i++) {
            const uint8_t it = r.raw[size_t(0x40 + i)];
            if (it != 0xff) put(s, 2, 2 + i * 2, itemName(rom_, it));
            else putStr(s, 2, 2 + i * 2, "-");
        }
        break;
    }
    case State::Bag: list(true); break;
    case State::Items: list(false); break;
    case State::Spells: {
        box(s, 0, 0, 20, 14);
        const auto sp = const_cast<FieldMenu*>(this)->spells(member_);
        for (int i = 0; i < 6 && first_ + i < int(sp.size()); i++) {
            put(s, 2, 2 + i * 2, itemName(rom_, sp[size_t(first_ + i)].id));
            std::snprintf(buf, sizeof buf, "%2d", sp[size_t(first_ + i)].cost);
            putStr(s, 16, 2 + i * 2, buf);
        }
        if (sp.empty()) putStr(s, 2, 2, "(none)");
        break;
    }
    case State::Target: {
        box(s, 0, 0, 20, 12);
        for (int i = 0; i < int(std::min<size_t>(party_.size(), 4)); i++) {
            const CharRecord& r = party_[size_t(i)];
            put(s, 2, 2 + i * 2, name(i));
            std::snprintf(buf, sizeof buf, "%3d/%3d", r.hp(), r.maxHp());
            putStr(s, 12, 2 + i * 2, buf);
        }
        break;
    }
    case State::Status: {
        box(s, 0, 0, 20, 18);
        const CharRecord& r = party_[size_t(member_)];
        put(s, 2, 1, r.name());
        std::snprintf(buf, sizeof buf, "HP %3d/%3d", r.hp(), r.maxHp()); putStr(s, 2, 3, buf);
        std::snprintf(buf, sizeof buf, "MP %3d/%3d", r.mp(), r.maxMp()); putStr(s, 2, 5, buf);
        std::snprintf(buf, sizeof buf, "Str %3d", r.str()); putStr(s, 2, 8, buf);
        std::snprintf(buf, sizeof buf, "Def %3d", r.def()); putStr(s, 2, 10, buf);
        std::snprintf(buf, sizeof buf, "Mag %3d", r.mag()); putStr(s, 2, 12, buf);
        std::snprintf(buf, sizeof buf, "Agi %3d", r.agi()); putStr(s, 2, 14, buf);
        std::snprintf(buf, sizeof buf, "Lv %2d", r.raw[3]); putStr(s, 12, 3, buf);
        break;
    }
    case State::Message:
        box(s, 0, 12, 20, 6);
        putStr(s, 1, 14, message_.c_str());
        break;
    case State::Done: break;
    }
    return s;
}

} // namespace ffl3
