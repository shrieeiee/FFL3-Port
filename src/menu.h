// Field menu (the port's own layout): Equip, Items, Status, Save. Equipment changes swap an
// item between the bag (wC6FF) and the member's 8 equipment slots (record +$40-47), then
// re-run the game's own recompute (0B:7349 / 0B:728B) on the record.
#pragma once
#include "battle.h"
#include <functional>

namespace ffl3 {

struct MenuInput { bool a = false, b = false, up = false, down = false, left = false, right = false; };

class FieldMenu {
public:
    FieldMenu(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, std::function<bool()> save);
    void tick(const MenuInput& in);
    bool done() const { return state_ == State::Done; }
    std::array<uint8_t, 20 * 18> screen() const;
    int cursorRow() const;
    int cursorCol() const;

private:
    enum class State { Top, Member, Slots, Bag, Items, Status, Message, Done };
    std::vector<int> bagIndices() const;
    void equip(int slot, int bagIndex);
    const Rom& rom_;
    BattleRam& ram_;
    std::vector<CharRecord>& party_;
    std::function<bool()> save_;
    State state_ = State::Top, next_ = State::Top;
    int top_ = 0, member_ = 0, slot_ = 0, bag_ = 0, first_ = 0;
    std::string message_;
};

} // namespace ffl3
