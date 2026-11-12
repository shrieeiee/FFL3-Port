#include "opening.h"
#include "rom.h"

namespace ffl3 {

std::vector<uint8_t> unpackMasked(const Rom& rom, int bank, uint16_t src, uint16_t end) {
    std::vector<uint8_t> out;
    uint16_t p = src;
    const int n = rom.b(bank, p++);
    const int size = ((4 << (n + 1)) & 0xff) ? ((4 << (n + 1)) & 0xff) : 256;
    while (p < end) {
        const uint8_t fill = rom.b(bank, p++);
        const uint16_t masks = p;
        p = uint16_t(p + size / 8);
        for (int k = 0; k < size / 8; k++) {
            const uint8_t mb = rom.b(bank, uint16_t(masks + k));
            for (int bit = 0; bit < 8; bit++)
                out.push_back((mb & (0x80 >> bit)) ? rom.b(bank, p++) : fill);
        }
    }
    return out;
}

static uint16_t labelAddr(const CharRecord& r) {
    static const uint8_t off[10] = {0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x07, 0x15, 0x00, 0x00};
    const int ci = (r.raw[0x0d] >> 3) & 0x0f;
    return uint16_t(0x78d7 + (ci < 10 ? off[ci] : 0));
}

std::vector<uint8_t> classLabel(const Rom& rom, const CharRecord& r) {
    std::vector<uint8_t> g;
    for (int i = 0; i < 7; i++) g.push_back(rom.b(9, uint16_t(labelAddr(r) + i)));
    while (!g.empty() && g.back() == 0xff) g.pop_back();
    return g;
}

// ---- title -------------------------------------------------------------------------------

TitleScreen::TitleScreen(const Rom& rom) : rom_(rom) {
    const std::vector<uint8_t> raw = unpackMasked(rom, 3, 0x5c00, 0x6287);   // 00:1D7F
    for (int t = 0; t < 128; t++) {
        Tile tile{};
        for (int y = 0; y < 8; y++) {
            const uint8_t lo = raw[size_t(t * 16 + y * 2)], hi = raw[size_t(t * 16 + y * 2 + 1)];
            for (int x = 0; x < 8; x++)
                tile[size_t(y * 8 + x)] = uint8_t(((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1));
        }
        logo_.push_back(tile);
    }
    decodeTile(rom.raw(3, 0x71b5), reg_);
}

void TitleScreen::tick(const OpeningInput& in) {
    if (in.up || in.down) newGame_ = !newGame_;        // 7D21: OAM y ^= $10
    if (in.a || in.start) choice_ = newGame_ ? 1 : 2;  // 7D3B: y = $68 -> new game
}

std::array<uint8_t, 360> TitleScreen::screen() const {
    UiScreen s;
    s.fill(0xff);
    for (int r = 0; r < 8; r++)                       // window map $9C22: 8 rows of 16 logo tiles
        for (int c = 0; c < 16; c++) s[size_t((1 + r) * 20 + 2 + c)] = uint8_t(r * 16 + c);
    // 09:7215 at (6, 10) in a 8x4 area, 09:7227 at (0, 14) in 20x4 (01:7CE0)
    const Glyphs a = uiText(rom_, 0x7215), b = uiText(rom_, 0x7227);
    int row = 11, col = 6;
    for (uint8_t g : a) { if (g == 0x06) { row += 2; col = 6; continue; } s[size_t(row * 20 + col++)] = g; }
    row = 15; col = 0;
    for (uint8_t g : b) { if (g == 0x06) { row += 2; col = 0; continue; } if (col < 20) s[size_t(row * 20 + col++)] = g; }
    return s;
}

// ---- naming ------------------------------------------------------------------------------

NamingScreen::NamingScreen(const Rom& rom, int member) : rom_(rom), rec_(loadCharacter(rom, member)) {
    for (int i = 0; i < 6; i++) name_[size_t(i)] = rec_.raw[size_t(7 + i)];
}

void NamingScreen::tick(const OpeningInput& in) {
    auto snd = [&](int id) { if (playSfx) playSfx(id); };
    if (in.right) col_ = (col_ + 1) % 9;                // 7AE8: x $10-$90, wraps
    if (in.left) col_ = (col_ + 8) % 9;
    if (in.up) row_ = (row_ + 5) % 6;                   // 7B04: y $40-$90, wraps
    if (in.down) row_ = (row_ + 1) % 6;
    if (in.a) {                                         // 7A6E
        if (len_ == 0) name_.fill(0xff);
        else if (len_ == 6) { snd(0x3b); return; }
        uint8_t g = rom_.b(9, uint16_t(0x78a0 + row_ * 9 + col_));   // 7B4B -> 00:1EB4 (bank 9)
        if (g == 0xff) g = 0x4c;                        // a space is stored as $4C
        name_[size_t(len_++)] = g;
    }
    if (in.b) {                                         // 7AAB
        if (len_ == 0) { back_ = true; done_ = true; return; }
        name_[size_t(--len_)] = 0xff;
    }
    if (in.start) {                                     // 7AC5
        if (len_ == 0 && name_[0] == 0xff) { snd(0x3b); return; }
        for (auto& g : name_) if (g == 0x4c) g = 0xff;  // 00:2990: $4C prints as a blank
        done_ = true;
    }
}

std::array<uint8_t, 360> NamingScreen::screen() const {
    UiScreen s;
    s.fill(0xff);
    uiBox(s, 1, 1, 2, 2);                               // face (0F:7B90 window list)
    uiBox(s, 5, 1, 14, 2);
    uiBox(s, 1, 5, 18, 12);
    // label (00:29C0: class bits of record +$0D -> 09:78D7 + 00:29D7[i]) and the name
    const uint16_t lab = labelAddr(rec_);
    for (int i = 0; i < 7; i++) s[size_t(2 * 20 + 5 + i)] = rom_.b(9, uint16_t(lab + i));
    for (int i = 0; i < 6; i++) {
        const uint8_t g = name_[size_t(i)];
        s[size_t(2 * 20 + 13 + i)] = g == 0x4c ? 0xff : g;
    }
    for (int r = 0; r < 6; r++)                         // the letter grid, every other column / row
        for (int c = 0; c < 9; c++) {
            const uint8_t g = rom_.b(9, uint16_t(0x78a0 + r * 9 + c));
            s[size_t((6 + r * 2) * 20 + 2 + c * 2)] = g;
        }
    return s;
}

std::vector<UiSprite> NamingScreen::sprites() const { return {memberSprite(rom_, rec_, 9, 10)}; }

} // namespace ffl3
