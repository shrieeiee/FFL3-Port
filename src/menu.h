// Field menu (the port's own layout): Equip, Items, Magic, Status, Save. Items and spells are
// used with the game's own field code (09:521A, see fieldUse). Equipment changes swap an
// item between the bag (wC6FF) and the member's 8 equipment slots (record +$40-47), then
// re-run the game's own recompute (0B:7349 / 0B:728B) on the record.
#pragma once
#include "battle.h"
#include "uitext.h"
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
    std::vector<UiSprite> sprites() const { return {}; }
    std::function<void(int id)> playSfx;

private:
    enum class State { Top, Member, Slots, Bag, Items, Spells, Target, Status, Message, Done };
    struct Spell { uint8_t id, cost; };
    std::vector<int> bagIndices() const;
    void equip(int slot, int bagIndex);
    BattleVM& vm();                         // RAM image with the party records at wC500
    void syncBack();
    std::vector<Spell> spells(int member);
    void choose(uint8_t id, bool spell);    // check the use kind, then pick a target or use
    void use(int target);                   // 0-4, or $80 = everyone
    void say(const std::string& m, State next) { message_ = m; next_ = next; state_ = State::Message; }
    void sfx(int id) { if (playSfx) playSfx(id); }
    const Rom& rom_;
    BattleRam& ram_;
    std::vector<CharRecord>& party_;
    std::function<bool()> save_;
    State state_ = State::Top, next_ = State::Top;
    int top_ = 0, member_ = 0, slot_ = 0, bag_ = 0, first_ = 0, target_ = 0;
    uint8_t useId_ = 0;
    bool useSpell_ = false;
    State back_ = State::Items;
    uint32_t rng_ = 0x1234567;
    std::string message_;
};

} // namespace ffl3
