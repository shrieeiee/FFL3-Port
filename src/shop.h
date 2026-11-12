// Shops and inns (01:6FD4), with the game's own screens and texts (bank 9).
// Shop n < $40: 6 wares from the pointer table 01:7804 (n >> 4) + (n & 15) * 6, prices from
// 01:7E4F (word per item, items below $D2), selling pays half, gold is wC6E0 (3 bytes, max
// 999999). Bit 7 of the id adds "Mix": shops below $20 trade Light / Dark stones for an item
// (01:77F4 / 77FC), the others combine two elements into a spell (01:77E4).
// Shops $40-$4F are inns: the price is the HP + MP the party is missing (01:7731).
#pragma once
#include "battle.h"
#include "uitext.h"

namespace ffl3 {

struct ShopInput { bool a = false, b = false, up = false, down = false, left = false, right = false; };

class Shop {
public:
    Shop(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, int id);
    void tick(const ShopInput& in);
    bool done() const { return state_ == State::Done; }
    UiScreen screen() const;                   // 20 x 18 tiles ($00 = transparent)
    int cursorRow() const;                     // hand cursor (tile row), -1 = none
    int cursorCol() const;
    int cursorTile() const;                    // the column the hand is in
    std::vector<UiSprite> sprites() const;     // the party
    static int price(const Rom& rom, uint8_t item);
    int gold() const;
    std::function<void(int id)> playSfx;       // $35 bought (01:703C), $3B error (00:1E83)

private:
    enum class State { Menu, Buy, Sell, MixList, Elements, Confirm, Inn, Message, Done };
    enum class Ask { Buy, Sell, Mix, Element };
    void say(Glyphs l1, Glyphs l2, State after);
    void confirm(Ask what);
    void answer(bool yes);
    void setGold(int g);
    void sfx(int id) { if (playSfx) playSfx(id); }
    void error() { sfx(0x3b); }
    int innCost() const;
    int members() const;
    int bagCount(uint8_t item) const;
    void bagTake(uint8_t item, int n);
    Glyphs msg(uint16_t addr) const { return uiText(rom_, addr); }
    Glyphs name(uint8_t item) const { return uiTrim(itemName(rom_, item)); }
    void drawShop(UiScreen& s, bool listOnTop) const;
    void drawElements(UiScreen& s) const;
    void drawInn(UiScreen& s) const;
    const Rom& rom_;
    BattleRam& ram_;
    std::vector<CharRecord>& party_;
    int id_ = 0;
    bool mix_ = false;
    uint8_t wares_[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};   // wCE2B
    uint8_t recipe_[4][2] = {};                // wCE31: item, Light << 4 | Dark
    State state_ = State::Menu, after_ = State::Menu, asked_ = State::Menu;
    Ask ask_ = Ask::Buy;
    int option_ = 1, cursor_ = 0, first_ = 0, yes_ = 0;
    uint8_t item_ = 0xff;                      // wCE94
    int picks_ = 0;
    uint8_t pick_[2] = {0xff, 0xff};           // wCE40 / wCE41
    uint8_t result_ = 0;                       // wCE3C
    Glyphs line1_, line2_;
};

} // namespace ffl3
