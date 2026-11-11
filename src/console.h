// FFL3 / SaGa 3 PC port - the Talon cockpit (event $23 -> 01:7972).
// Same windows, texts (bank 9) and rules as the game: Depart / Engine / Warp / Weapon / Option.
// Parts the party carries (items $D8-$E4) are "attached" when the cockpit opens (01:7C1B):
// taken from the bag, remembered in wC6F8/wC6F9, with story flags 01:7C70 for $D8-$E0.
#pragma once
#include "battle.h"
#include "flags.h"
#include <functional>
#include <string>

namespace ffl3 {

struct ConsoleInput { bool a = false, b = false, up = false, down = false; };

class TalonConsole {
public:
    TalonConsole(const Rom& rom, BattleRam& ram, GameFlags& flags);
    void tick(const ConsoleInput& in);
    bool done() const { return state_ == State::Done; }
    int result() const { return result_; }      // wCA57: 1 = take off, 0 = closed
    int takeOffMode() const;                      // 01:63E6: vehicle mode 3-8
    std::array<uint8_t, 20 * 18> screen() const;
    int cursorRow() const;
    int cursorCol() const;
    std::function<void(int id)> playSfx;

private:
    enum class State { Attach, Menu, Sub, Message, Done };
    Glyphs text(uint16_t addr, int ce94 = -1) const;   // bank 9 text -> glyphs (6 = newline)
    void openSub(int option);
    void sfx(int id) { if (playSfx) playSfx(id); }
    bool owned(uint8_t item) const;               // 00:2901: bit in wC6F8/wC6F9
    const Rom& rom_;
    BattleRam& ram_;
    GameFlags& flags_;
    State state_ = State::Menu;
    int option_ = 0, sub_ = 0, result_ = 0;
    std::vector<uint8_t> attached_;               // parts attached on opening (messages)
    std::string message_;
    Glyphs messageGlyphs_;
};

} // namespace ffl3
