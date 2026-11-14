#include "text.h"
#include <cstdio>
#include <cstdlib>
#include <cstdio>

namespace ffl3 {

static const int kCharDelay = 10;   // wCED9, set to $0A on the title screen (01:7C79)

namespace {
// Codes below $4E that are dictionary words (00:30d1), entries $3A+i.
const uint8_t kDictLow[] = {0x01, 0x02, 0x03, 0x04, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x40,
                            0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x0e, 0x0f};
// Argument bytes per control code (from the handlers; validated on all NPC texts).
int argCount(uint8_t c) {
    switch (c) {
    case 0x00: case 0x05: case 0x06: case 0x07: case 0x08: case 0x0b: case 0x10: case 0x14:
    case 0x1c: case 0x27: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x34: case 0x4b: case 0x4c:
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
}

TextBox::TextBox(const Rom& rom) : rom_(rom) {
    uint32_t o = Rom::off(3, 0x71c5);
    const uint8_t* d = rom.raw(0, 0);   // ROM base
    for (auto& e : dict_) {
        const uint8_t n = d[o];
        e.assign(d + o + 1, d + o + 1 + n);
        o += 1u + n;
    }
    // Default party names = the first four character records (07:7A88, $60 bytes each).
    for (int m = 0; m < 4; m++) {
        std::string n;
        for (int i = 0; i < 6; i++) {
            const uint8_t t = rom.b(7, uint16_t(0x7a88 + m * 0x60 + i));
            if (t >= 0xba && t <= 0xd3) n += char('A' + t - 0xba);
            else if (t >= 0xd4 && t <= 0xed) n += char('a' + t - 0xd4);
        }
        partyNames[size_t(m)] = n;
    }
    clear();
}

void TextBox::clear() {
    for (auto& l : lines_) l.fill(0xff);
    row_ = col_ = 0;
}

void TextBox::open(int bank, uint16_t addr) {
    stack_.clear();
    Src s;
    s.bank = bank; s.addr = addr;
    stack_.push_back(s);
    clear();
    active_ = true;
    state_ = State::Run;
    delay_ = 0;
    pendingGlyphs_.clear();
}

void TextBox::call(int bank, uint16_t addr) {
    if (!active_) { open(bank, addr); return; }
    Src s;
    s.bank = bank; s.addr = addr; s.isCall = true;
    stack_.push_back(s);
}

int TextBox::next() {
    while (!stack_.empty()) {
        Src& s = stack_.back();
        if (s.isBuf) {
            if (s.pos < s.buf.size()) return s.buf[s.pos++];
            stack_.pop_back();           // dictionary word finished
            continue;
        }
        return rom_.b(s.bank, s.addr++);
    }
    return -1;
}

void TextBox::putGlyph(uint8_t t) {
    if (col_ >= kCols) newline();
    if (state_ == State::WaitScroll) { pendingGlyphs_.push_back(t); return; }
    lines_[row_][col_++] = t;
}

void TextBox::newline() {
    history.push_back(lines_[size_t(row_)]);
    col_ = 0;
    if (row_ < kRows - 1) { row_++; return; }
    // Full window: the game scrolls up a line by itself (no key wait). Waits come from
    // explicit codes ($1C before each new speaker, $0B, $08, $10).
    for (int r = 0; r < kRows - 1; r++) lines_[size_t(r)] = lines_[size_t(r) + 1];
    lines_[kRows - 1].fill(0xff);
    if (waitBeforeNextLine_) { waitBeforeNextLine_ = false; }
}

void TextBox::finish() {
    if (col_ > 0) history.push_back(lines_[size_t(row_)]);
    // Pop the ROM text we were running (and any dictionary buffers above it).
    while (!stack_.empty() && stack_.back().isBuf) stack_.pop_back();
    if (!stack_.empty()) {
        const bool wasCall = stack_.back().isCall;
        stack_.pop_back();
        if (wasCall && !stack_.empty()) return;   // back to the calling text
    }
    active_ = false;
    state_ = State::Idle;
    clear();
}

void TextBox::runRef(uint8_t lo, uint8_t hi) {
    if (hi < 0x40) {                     // 00:2ef6 -> 00:1236 event command
        if (onEvent) onEvent(hi, lo);
        if (mustWait && mustWait()) waitEvent_ = true;   // the game runs the battle etc. inside the event
        return;
    }
    Src s;
    s.isCall = true;
    if (hi < 0x80) { s.bank = 9; s.addr = uint16_t(hi << 8 | lo); }
    else if (hi < 0xc0) { s.bank = 0xa; s.addr = uint16_t(((hi & 0x3f) | 0x40) << 8 | lo); }
    else { s.bank = 3; s.addr = uint16_t((hi & 0x7f) << 8 | lo); }
    stack_.push_back(s);
}

void TextBox::step() {
    // Execute bytes until a glyph is shown (typewriter), or we block on input.
    for (int guard = 0; guard < 512 && state_ == State::Run; guard++) {
        if (waitEvent_) return;
        if (isBusy && isBusy()) return;          // an event started a move/effect: the game runs it to the end first
        const int v = next();
        if (v < 0) { finish(); return; }
        const uint8_t b = uint8_t(v);
        if (b >= 0x88) { putGlyph(b); return; }
        // Dictionary word?
        int di = -1;
        if (b >= 0x4e) di = b - 0x4e;
        else for (int i = 0; i < 22; i++) if (kDictLow[i] == b) { di = 0x3a + i; break; }
        if (di >= 0) {
            Src s; s.isBuf = true; s.buf = dict_[di];
            stack_.push_back(s);
            continue;
        }
        uint8_t a[8] = {};
        const int n = argCount(b);
        for (int i = 0; i < n; i++) a[i] = uint8_t(next());
        switch (b) {
        case 0x00: finish(); return;
        case 0x10: state_ = State::WaitEnd; return;
        case 0x06: newline(); if (state_ != State::Run) return; break;
        case 0x0b: state_ = State::WaitA; return;
        case 0x08: state_ = State::WaitA; clearOnResume_ = true; return;   // 274d: wait, then 27ab
        case 0x07: case 0x14: clear(); break;
        case 0x1c: break;                // 27c5: wCD34 bit 1 = trim the blanks of the next name (names are trimmed anyway)
        case 0x05: case 0x4c: break;
        case 0x0c: delay_ = 2 * a[0]; return;   // 2766: pause 2 x n frames (A cuts it short)
        case 0x11: break;
        case 0x27: break;                        // 2B02: wCDC1 = 1
        case 0x1f: {                     // 2955 (index via 20de): party member or story character
            int m = a[0];
            if (m == 0x80 || m == 0xff) m = 0;                 // wCD4C / wCD36 (current actor)
            else if (m >= 0x80) m -= 0x7b;
            else if (m >= 5 && m < 9) m -= 5;
            std::vector<uint8_t> name;
            if (m < 5 && memberName) {
                name = memberName(m);
            } else if (m >= 0x20 && classLabel) {
                name = classLabel();
            } else if (m < 4) {
                for (char c : partyNames[size_t(m)]) {
                    uint8_t t = 0xff;
                    if (c >= 'A' && c <= 'Z') t = uint8_t(0xba + c - 'A');
                    else if (c >= 'a' && c <= 'z') t = uint8_t(0xd4 + c - 'a');
                    else if (c >= '0' && c <= '9') t = uint8_t(0xb0 + c - '0');
                    name.push_back(t);
                }
            } else if (m >= 0x10 && m < 0x20) {                // 07:7A88 + (m-$0C) * $60, 6 chars
                for (int i = 0; i < 6; i++) name.push_back(rom_.b(7, uint16_t(0x7a88 + (m - 0x0c) * 0x60 + i)));
                while (!name.empty() && name.back() == 0xff) name.pop_back();
            }
            for (uint8_t t : name) putGlyph(t);
            break;
        }
        case 0x36: if (flags) flags->event.set(a[0]); break;
        case 0x37: if (flags) flags->event.reset(a[0]); break;
        case 0x39: {
            const bool f = flags && flags->test(a[0]);
            if (f) runRef(a[1], a[2]); else runRef(a[3], a[4]);
            break;
        }
        case 0x3a: {
            const bool f = flags && flags->test(a[0]), g = flags && flags->test(a[1]);
            if (f) runRef(a[2], a[3]); else if (g) runRef(a[4], a[5]); else runRef(a[6], a[7]);
            break;
        }
        case 0x3c:
            choiceA_[0] = a[0]; choiceA_[1] = a[1]; choiceB_[0] = a[2]; choiceB_[1] = a[3];
            choice_ = 0;
            state_ = State::Choice;
            return;
        case 0x3e: runRef(a[0], a[1]); break;
        // Shorthand event calls (0B:6C35, table 0B:6C4C): code -> event command, arg = param.
        case 0x32: runRef(a[0], 0x26); break;
        case 0x3d: runRef(a[0], 0x28); break;
        case 0x3b: runRef(a[0], 0x27); break;
        case 0x13: runRef(a[0], 0x1d); break;
        case 0x12: runRef(a[0], 0x1c); break;
        case 0x3f: runRef(a[0], 0x1b); break;
        case 0x0a: runRef(a[0], 0x10); break;
        case 0x0d: runRef(a[0], 0x0e); break;
        case 0x38: runRef(a[0], 0x12); break;
        case 0x2a: {                     // 2c9c / 1e8f: spell (< $30, 0E:6D9B x6) or item (0E:6EBB x8) name
            if (a[0] == 0xff) break;
            const bool spell = a[0] < 0x30;
            const uint16_t p = spell ? uint16_t(0x6d9b + a[0] * 6) : uint16_t(0x6ebb + (a[0] - 0x30) * 8);
            for (int i = 0; i < (spell ? 6 : 8); i++) putGlyph(rom_.b(0xe, uint16_t(p + i)));
            break;
        }
        case 0x1e: {                     // name of the item in wCE94 (chests, shops), trailing blanks cut
            if (itemArg < 0 || itemArg == 0xff) break;
            const bool spell = itemArg < 0x30;
            const uint16_t p = spell ? uint16_t(0x6d9b + itemArg * 6) : uint16_t(0x6ebb + (itemArg - 0x30) * 8);
            int n = spell ? 6 : 8;
            while (n > 0 && rom_.b(0xe, uint16_t(p + n - 1)) == 0xff) n--;
            for (int i = 0; i < n; i++) putGlyph(rom_.b(0xe, uint16_t(p + i)));
            break;
        }
        default:
            if (log) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "text code %02X (arg %02X) not implemented", b, a[0]);
                log(buf);
            }
            break;
        }
    }
}

void TextBox::tick(bool aPressed, bool aHeld, bool upPressed, bool downPressed) {
    if (!active_) return;
    blink_++;
    switch (state_) {
    case State::WaitA:
        if (aPressed) {
            state_ = State::Run;
            if (clearOnResume_) { clear(); clearOnResume_ = false; }
        }
        return;
    case State::WaitScroll:
        if (aPressed) {
            for (int r = 0; r < kRows - 1; r++) lines_[size_t(r)] = lines_[size_t(r) + 1];
            lines_[kRows - 1].fill(0xff);
            row_ = kRows - 1;
            col_ = 0;
            state_ = State::Run;
            for (uint8_t t : pendingGlyphs_) putGlyph(t);
            pendingGlyphs_.clear();
        }
        return;
    case State::WaitEnd:
        if (aPressed) {
            finish();                    // close, or return to the calling text
            if (active_) state_ = State::Run;
        }
        return;
    case State::Choice:
        if (upPressed || downPressed) choice_ ^= 1;
        if (aPressed) {
            state_ = State::Run;
            if (choice_ == 0) runRef(choiceA_[0], choiceA_[1]);
            else runRef(choiceB_[0], choiceB_[1]);
        }
        return;
    case State::Run:
        if (waitEvent_ && mustWait && mustWait()) return;
        waitEvent_ = false;
        if (isBusy && isBusy()) return;
        if (delay_ > 0 && !aHeld) { delay_--; return; }
        step();
        if (delay_ <= 0) delay_ = kCharDelay;   // 00:3593: 1 + wCED9 (10) frames a character; A held skips the wait
        return;
    case State::Idle:
        return;
    }
}

} // namespace ffl3
