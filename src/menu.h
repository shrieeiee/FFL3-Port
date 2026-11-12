// Field menu with the game's own screens (layouts taken from the real game's window tiles):
// Item, Magic, Talent, Status, Equip, Auto, Save. Items and spells are
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
    // save(slot) writes save file 0-2; peek(slot, leader) reads a file's leader for the slot list.
    FieldMenu(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party,
              std::function<bool(int)> save, std::function<bool(int, CharRecord&)> peek = {});
    void tick(const MenuInput& in);
    bool done() const { return state_ == State::Done; }
    UiScreen screen() const;
    int cursorRow() const;
    int cursorCol() const;
    std::vector<UiSprite> sprites() const;
    std::function<void(int id)> playSfx;

private:
    enum class State { Top, Member, Slots, Bag, Items, Spells, Target, Talents, Status, Auto, Save, Message, Done };
    struct Spell { uint8_t id, cost; };
    std::vector<int> bagIndices() const;
    void equip(int slot, int bagIndex);
    bool fits(int slot, uint8_t item) const;
    BattleVM& vm();                         // RAM image with the party records at wC500
    void syncBack();
    std::vector<Spell> spells(int member);
    void choose(uint8_t id, bool spell);    // check the use kind, then pick a target or use
    void use(int target);                   // 0-4, or $80 = everyone
    void say(const std::string& l1, const std::string& l2, State next) { message_ = l1; message2_ = l2; next_ = next; state_ = State::Message; }
    void sfx(int id) { if (playSfx) playSfx(id); }
    int members() const;                    // 4, or 5 with a guest
    void drawMain(UiScreen& s) const;
    void drawParty(UiScreen& s, int top, int firstMember) const;
    void drawList(UiScreen& s) const;
    void drawEquip(UiScreen& s) const;
    void drawStatus(UiScreen& s) const;
    void drawTalents(UiScreen& s) const;
    void drawSave(UiScreen& s) const;
    void memberLine(UiScreen& s, const CharRecord& r, int row) const;
    Glyphs statusName(const CharRecord& r) const;
    const Rom& rom_;
    BattleRam& ram_;
    std::vector<CharRecord>& party_;
    std::function<bool(int)> save_;
    std::function<bool(int, CharRecord&)> peek_;
    State state_ = State::Top, next_ = State::Top;
    int top_ = 0, member_ = 0, slot_ = 0, bag_ = 0, first_ = 0, target_ = 0, mark_ = -1, file_ = 0;
    uint8_t useId_ = 0;
    bool useSpell_ = false;
    State back_ = State::Items;
    uint32_t rng_ = 0x1234567;
    std::string message_, message2_;
};

} // namespace ffl3
