// Battles. The rules are the original's: the bank 0C battle bytecode runs on BattleVM over a
// persistent 64K RAM image (BattleRam), with the logic natives translated in battlenatives.cpp.
// This class supplies the parts the GB does in bank 0B: the setup before the VM starts, and
// the UI natives - the command phase (Fight/Run/Auto, member commands, targets), the battle
// messages, and the animation / sound waits.
//
// The screen is modelled the way the GB draws it: monster pictures on BG rows 0-7, and a
// window (WY, 20 tiles wide) with menus / status panel / messages, plus a few OBJs.
// Battle::view() returns exactly that, so the renderer is dumb and testable against VRAM.
#pragma once
#include "battledata.h"
#include "battlevm.h"
#include "battleanim.h"
#include <deque>
#include <string>

namespace ffl3 {

// The GB's work RAM as the battle code sees it. Kept across battles: the battle code reads
// values left over from the previous fight (last commands, message state).
using BattleRam = std::array<uint8_t, 0x10000>;
void initBattleRam(BattleRam& ram);   // power-on state: WRAM cleared + the few leftovers set
// 0B:7037: add one of item `id` to the inventory at wC6FF (24 pairs, max 9 each).
// Returns the new count, or -1 when it doesn't fit.
int inventoryAdd(BattleRam& ram, uint8_t id);

struct BattleContext {
    std::array<uint8_t, 32> eventFlags{};   // wC760, MSB-first
    int vehicle = 0;                        // wC6E3
    int area = 0;                           // wCA2D
};

struct BattleInput { bool a = false, b = false, up = false, down = false, left = false, right = false, aHeld = false; };

struct BattleObj {
    int x, y;            // screen pixels (top-left)
    int tile;            // kCursor / kBracket, or a party portrait / field sprite
    bool flipX = false, flipY = false;
    int member = -1;     // >= 0: tile is relative to this member's sprite page
    bool tall = false;
};

struct BattleView {
    int wy = 144;                            // window top (144 = hidden)
    std::array<uint8_t, 20 * 10> win{};      // window tiles, 20 x 10
    std::vector<BattleObj> objs;
    std::vector<int> visibleSlots;           // enemy slots whose picture is on the BG
    int flashSlot = -1;
    // The GB's OBJ layer and scroll / palette registers as the bank 2 animations left them.
    std::array<uint8_t, 160> oam{};          // 40 entries (y, x, tile, attr), 8x16
    const uint8_t* objTiles = nullptr;       // OBJ tile data ($8000-$8FFF)
    uint8_t scx = 0, scy = 0, bgp = 0xd2, obp0 = 0xd0, obp1 = 0x90, topBgp = 0xd2, topObp = 0xd0;
    bool blank = false;
};

class Battle {
public:
    enum class Result { None, Won, Lost, Ran };
    static constexpr int kCursor = 0x20;     // OBJ tiles in battle VRAM (03:5430 / 03:57B0)
    static constexpr int kBracket = 0x24;

    // party: the 4 members + guest (index 4); written back when the battle ends.
    // hffca = formation index | $80 for fixed monster counts.
    Battle(const Rom& rom, BattleRam& ram, std::vector<CharRecord>& party, const BattleContext& ctx, int hffca, uint32_t seed);

    void tick(const BattleInput& in);
    BattleView view() const;
    Result result() const { return result_; }
    bool finished() const { return state_ == State::Done; }

    const Formation& formation() const { return form_; }
    std::function<void(int id)> playSfx;   // sound effects the battle bytecode requests (hFFBC)
    const EnemyLayout& layout() const { return layout_; }
    const std::vector<Monster>& monsters() const { return groups_; }   // per group
    int enemyHp(int slot) const;
    int memberSprite(int m) const;
    std::vector<std::string> log;            // every message line, ASCII (tests)
    std::vector<uint16_t> unknownNatives;    // natives not handled yet (logged, skipped)

private:
    enum class State { Intro, Vm, FightMenu, Command, List, Target, Message, Wait, Done };

    uint8_t m(uint16_t a) const { return vm_.mem[a]; }
    int word(uint16_t a) const { return vm_.mem[a] | vm_.mem[uint16_t(a + 1)] << 8; }
    bool memberAlive(int i) const;
    bool enemyAlive(int slot) const;
    std::vector<int> aliveGroups() const;
    Glyphs combatantName(int idx) const;

    void setup(int hffca, const BattleContext& ctx, bool retry = false);
    BattleContext ctx_;
    int hffca_ = 0;
    void runVm();
    bool native(uint16_t addr);
    uint8_t rand8();
    uint8_t rnd(uint8_t n);                   // 00:1D2E
    void finishCommands(bool run);
    void showMessage();
    void endBattle();

    void putText(std::array<uint8_t, 200>& w, int col, int row, const Glyphs& g) const;
    void putNum(std::array<uint8_t, 200>& w, int col, int row, int v, int width) const;
    void box(std::array<uint8_t, 200>& w, int c0, int r0, int cw, int rh) const;
    void statusPanel(BattleView& v, int hideCol) const;

    const Rom& rom_;
    BattleRam& ram_;
    std::vector<CharRecord>& party_;
    BattleVM vm_;
    BattleAnim anim_;
    std::array<bool, 8> erased_{};           // the slot's picture has been cleared from the BG
    bool startAnim(uint16_t entry);          // true: finished at once
    void hidePartyRow();
    Formation form_;
    EnemyLayout layout_;
    std::vector<Monster> groups_;
    State state_ = State::Intro;
    Result result_ = Result::None;
    int timer_ = 0, cursor_ = 0, member_ = 0;
    bool cmdSet_[4] = {};
    int cmdKind_[4] = {};                    // command chosen per member (for the "Weap" label)
    // list + target selection (0B:6527 / 6557 / 6589, 0B:670D-6B00)
    struct Entry { uint8_t id; int count; int slot; };
    std::vector<Entry> list_;
    int listCursor_ = 0, command_ = 0;
    uint8_t chosenId_ = 0xff, kind_ = 0, target_ = 0xff;
    int errorTimer_ = 0;
    void openList(int command);
    bool beginTarget(uint8_t kind);          // false: can't be used
    void moveTarget(int dx, int dy);
    void confirmTarget();
    uint8_t itemKind(uint8_t id, uint8_t ce25, uint8_t ce27);
    std::vector<Glyphs> lines_;              // message window: finished lines
    std::deque<uint8_t> pending_;            // glyphs still to type ($FE = newline)
    Glyphs cur_;
    int typeDelay_ = 0, flashSlot_ = -1, flashTimer_ = 0;
    long stepCount_ = 0;
};

} // namespace ffl3
