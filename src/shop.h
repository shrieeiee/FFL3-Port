// Shops and inns (01:6FD4). Data is the original's: the 6 wares of shop n come from the
// pointer table 01:7804 (n >> 4) + (n & 15) * 6, prices from 01:7E4F (word per item, items
// below $D2), selling pays half, gold is wC6E0 (3 bytes, max 999999). Shops $50+ are inns:
// the cost is the HP + MP the party is missing (01:7731), paid from gold (01:7778).
// The menus are the port's own layout (the original's shop screens aren't traced yet).
#pragma once
#include "battle.h"

namespace ffl3 {

struct ShopInput { bool a = false, b = false, up = false, down = false; };

class Shop {
public:
    Shop(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, int id);
    void tick(const ShopInput& in);
    bool done() const { return state_ == State::Done; }
    // 20 x 18 window of tiles ($00 = transparent), drawn over the field
    std::array<uint8_t, 20 * 18> screen() const;
    int cursorRow() const;                     // hand cursor (tile row), -1 = none
    int cursorCol() const { return 1; }
    static int price(const Rom& rom, uint8_t item);
    int gold() const;
    std::function<void(int id)> playSfx;    // $35 bought (01:703C), $3B error (00:1E83)

private:
    enum class State { Menu, Buy, Sell, Inn, Message, Done };
    void say(const char* s, State after);
    void setGold(int g);
    void sfx(int id) { if (playSfx) playSfx(id); }
    int innCost() const;
    const Rom& rom_;
    BattleRam& ram_;
    std::vector<CharRecord>& party_;
    int id_ = 0;
    std::vector<uint8_t> wares_;
    State state_ = State::Menu, after_ = State::Menu;
    int cursor_ = 0, first_ = 0;
    std::string message_;
};

} // namespace ffl3
