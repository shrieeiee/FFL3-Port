#include "console.h"
#include "battledata.h"
#include <algorithm>

namespace ffl3 {

namespace {
using Screen = std::array<uint8_t, 360>;
const uint8_t kNl = 0x06;
const uint8_t kLow[] = {0x01, 0x02, 0x03, 0x04, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x40,
                        0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x0e, 0x0f};
void box(Screen& s, int c0, int r0, int w, int h) {   // border around an inner w x h area at (c0, r0)
    for (int r = r0 - 1; r <= r0 + h; r++)
        for (int c = c0 - 1; c <= c0 + w; c++) {
            if (r < 0 || r >= 18 || c < 0 || c >= 20) continue;
            uint8_t t = 0xff;
            const bool top = r == r0 - 1, bot = r == r0 + h, l = c == c0 - 1, rt = c == c0 + w;
            if (top) t = l ? 0xf7 : rt ? 0xf9 : 0xfc;
            else if (bot) t = l ? 0xf8 : rt ? 0xfa : 0xfc;
            else if (l || rt) t = 0xfb;
            s[size_t(r * 20 + c)] = t;
        }
}
// Lay text into a window: lines wrap at spaces to the window width, `step` rows apart.
void put(Screen& s, int c0, int r0, int w, int h, const Glyphs& g, int step) {
    std::vector<Glyphs> lines(1);
    Glyphs word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        if (!lines.back().empty() && int(lines.back().size() + word.size()) > w) {
            lines.emplace_back();
        }
        lines.back().insert(lines.back().end(), word.begin(), word.end());
        word.clear();
    };
    for (uint8_t c : g) {
        if (c == kNl) { flushWord(); lines.emplace_back(); continue; }
        if (c == 0xff && !word.empty() && word.back() != 0xff) flushWord();
        word.push_back(c);
    }
    flushWord();
    for (size_t i = 0; i < lines.size(); i++) {
        const int r = r0 + int(i) * step;
        if (r >= r0 + h) break;
        for (size_t k = 0; k < lines[i].size() && int(k) < w; k++) s[size_t(r * 20 + c0 + int(k))] = lines[i][k];
    }
}
}

TalonConsole::TalonConsole(const Rom& rom, BattleRam& ram, GameFlags& flags) : rom_(rom), ram_(ram), flags_(flags) {
    // 01:7C1B: attach every part in the bag. Bits: $D8 = wC6F8 bit 7 ... $E0 = wC6F9 bit 7.
    for (int item = 0xd8; item <= 0xe4; item++) {
        bool inBag = false;
        for (int i = 0; i < 24; i++) {                       // 0B:706C: take one from the bag
            const size_t h = size_t(0xc6ff + i * 2);
            if (ram_[h] != item || !ram_[h + 1]) continue;
            if (--ram_[h + 1] == 0) { ram_[h] = 0xff; ram_[h + 1] = 0xff; }
            inBag = true;
            break;
        }
        if (!inBag) continue;
        const int k = item - 0xd8;
        ram_[size_t(0xc6f8 + k / 8)] |= uint8_t(0x80 >> (k % 8));
        if (item < 0xe1) flags_.event.set(rom_.b(1, uint16_t(0x7b98 + item)));   // 01:7C49 (01:7C70)
        attached_.push_back(uint8_t(item));
    }
    if (!attached_.empty()) state_ = State::Attach;
}

bool TalonConsole::owned(uint8_t item) const {
    const int k = item - 0xd8;
    return ram_[size_t(0xc6f8 + k / 8)] & (0x80 >> (k % 8));
}

Glyphs TalonConsole::text(uint16_t addr, int ce94) const {
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
    Glyphs out;
    std::function<void(uint8_t)> code;
    uint16_t p = addr;
    code = [&](uint8_t c) {
        if (c >= 0x88) { out.push_back(c); return; }
        if (c >= 0x4e) { for (uint8_t x : dict[size_t(c - 0x4e)]) code(x); return; }
        for (size_t i = 0; i < sizeof kLow; i++)
            if (kLow[i] == c) { for (uint8_t x : dict[58 + i]) code(x); return; }
        if (c == 0x06) out.push_back(kNl);
        else if (c == 0x09) { const int n = rom_.b(9, p++); for (int i = 0; i < n; i++) out.push_back(0xff); }
        else if (c == 0x1e) {                                 // 00:280F: $40-$4F = part n + $98 if attached,
            const uint8_t n = rom_.b(9, p++);                  // $3F = the item in wCE94
            const int item = n == 0x3f ? ce94 : n >= 0x40 && n < 0x50 ? n + 0x98 : -1;
            if (item < 0 || (n != 0x3f && !owned(uint8_t(item)))) { for (int i = 0; i < 8; i++) out.push_back(0xff); return; }
            Glyphs g = itemName(rom_, item);
            while (!g.empty() && g.back() == 0xff) g.pop_back();
            out.insert(out.end(), g.begin(), g.end());
        }
    };
    for (int guard = 0; guard < 400; guard++) {
        const uint8_t c = rom_.b(9, p++);
        if (c == 0x00) break;
        code(c);
    }
    return out;
}

// 01:79B3 (Depart is allowed) and 01:63E6 (which mode the Talon flies in).
int TalonConsole::takeOffMode() const {
    const uint8_t eng = ram_[0xc6fa];
    if (flags_.test(0x3a)) return eng == 0xe0 ? 8 : eng == 0xdf ? 7 : 6;
    if (flags_.test(0x39)) return eng == 0xdf ? 5 : 4;
    return 3;
}

void TalonConsole::openSub(int option) {
    option_ = option;
    sub_ = 0;
    const uint8_t f8 = ram_[0xc6f8], f9 = ram_[0xc6f9];
    const bool ok = option == 1 ? ((f8 & 3) || (f9 & 0x80)) : option == 2 ? (f9 & 0x70) != 0
                  : option == 3 ? true : (f8 & 0x0c) != 0;
    if (!ok) {                                                // 7BB4: "No Proper Units"
        messageGlyphs_ = text(0x77e2);
        state_ = State::Message;
        return;
    }
    state_ = State::Sub;
}

void TalonConsole::tick(const ConsoleInput& in) {
    switch (state_) {
    case State::Attach:
        if (in.a || in.b) { attached_.erase(attached_.begin()); if (attached_.empty()) state_ = State::Menu; }
        return;
    case State::Message:
        if (in.a || in.b) state_ = State::Menu;
        return;
    case State::Menu:
        if (in.up) option_ = (option_ + 4) % 5;               // 7A92 / 7AA1: wraps
        if (in.down) option_ = (option_ + 1) % 5;
        if (in.b) { result_ = 0; state_ = State::Done; return; }
        if (!in.a) return;
        if (option_ == 0) {                                   // 79B3
            const bool go = flags_.test(5) ||
                            (!flags_.test(0x0e) && flags_.test(0x0f) && flags_.test(0xe1));
            if (!go) { sfx(0x3b); return; }
            result_ = 1;
            state_ = State::Done;
            return;
        }
        openSub(option_);
        return;
    case State::Sub: {
        if (in.up) sub_ = (sub_ + 4) % 5;
        if (in.down) sub_ = (sub_ + 1) % 5;
        if (in.b || (option_ == 4 && in.a)) { state_ = State::Menu; return; }
        if (!in.a) return;
        static const uint8_t base[4] = {0xde, 0xe1, 0xd8, 0};
        const int limit = option_ == 3 ? 5 : 3;
        uint8_t item = uint8_t(base[option_ - 1] + sub_);
        if (option_ == 3 && sub_ == 4) item = 0xe4;
        if (sub_ >= limit || !owned(item)) { sfx(0x3b); return; }
        if (option_ == 1) ram_[0xc6fa] = item;                 // 7AE2: engine
        else if (option_ == 3) ram_[0xc6fb] = item;            // 7B3C: weapon
        else {                                                // 7B12: warp - not in the port yet
            messageGlyphs_ = asciiGlyphs("Warp: not yet");
            state_ = State::Message;
            return;
        }
        messageGlyphs_ = text(0x782c);                        // 7B5B: "Set completed!"
        state_ = State::Message;
        return;
    }
    case State::Done:
        return;
    }
}

int TalonConsole::cursorRow() const {
    if (state_ == State::Menu) return 8 + option_ * 2;
    if (state_ == State::Sub && option_ != 4) return 8 + sub_ * 2;
    return -1;
}

int TalonConsole::cursorCol() const { return state_ == State::Sub ? 10 : 1; }

std::array<uint8_t, 360> TalonConsole::screen() const {
    Screen s{};
    // 01:7BCC: windows from 01:7C04 / 7BF0[option]
    static const uint16_t top[5] = {0x7798, 0x77d9, 0x780c, 0x783b, 0x7866};
    static const uint16_t right[5] = {0, 0x7802, 0x7822, 0x7856, 0x7882};
    static const uint16_t prompt[5] = {0, 0x77f4, 0x7813, 0x7844, 0x786f};
    const int opt = (state_ == State::Sub || state_ == State::Message) ? option_ : 0;
    box(s, 1, 1, 18, 4);
    put(s, 1, 2, 18, 3, text(top[opt]), 2);
    if (state_ == State::Sub && prompt[opt]) {
        for (int c = 1; c < 19; c++) s[size_t(4 * 20 + c)] = 0xff;
        put(s, 1, 4, 18, 1, text(prompt[opt]), 2);
    }
    box(s, 1, 7, 7, 10);
    put(s, 1, 8, 7, 9, text(0x77b6), 2);
    box(s, 10, 7, 9, 10);
    if (right[opt]) put(s, 10, 8, 9, 9, text(right[opt]), 2);
    if (state_ == State::Message) {
        box(s, 3, 10, 14, 2);
        for (int r = 10; r < 12; r++) for (int c = 3; c < 17; c++) s[size_t(r * 20 + c)] = 0xff;
        put(s, 3, 10, 14, 2, messageGlyphs_, 1);
    }
    if (state_ == State::Attach) {                           // 01:7C5B: "Attached X to Talon."
        box(s, 1, 1, 18, 6);
        for (int r = 1; r < 7; r++) for (int c = 1; c < 19; c++) s[size_t(r * 20 + c)] = 0xff;
        put(s, 2, 2, 17, 5, text(0x7889, attached_.front()), 2);
    }
    return s;
}

} // namespace ffl3
