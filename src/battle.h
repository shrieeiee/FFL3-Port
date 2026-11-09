// Battles (bank 0B). Pure logic - no SDL - so tests can drive it headless.
//
// The screen is modelled the way the GB draws it: monster pictures on BG rows 0-7, and a
// window (WY, 20 tiles wide) holding the menus / status panel / messages, plus a few OBJs
// (hand cursor, target brackets, party portraits). Battle::view() returns exactly that, so the
// renderer is dumb and the layout can be checked against VRAM dumps from the real game.
//
// What's faithful: formations, monster counts and placement, stats, names, pictures, party
// records, menus and status panel layout, message wording, encounter tables.
// What's PROVISIONAL (until the battle effect VM at 0C:4008 is decoded): hit/damage/critical
// formulas, enemy AI (random talent at a random member), turn order details, escape chance.
#pragma once
#include "battledata.h"
#include <deque>
#include <random>
#include <string>

namespace ffl3 {

struct BattleInput { bool a = false, b = false, up = false, down = false, left = false, right = false, aHeld = false; };

struct BattleObj {
    int x, y;            // screen pixels (top-left)
    int tile;            // kCursor / kBracket, or a party portrait / field sprite
    bool flipX = false, flipY = false;
    int member = -1;     // >= 0: tile is relative to this member's sprite page
    bool tall = false;   // field sprite (front frame, 16x16)
};

struct BattleView {
    int wy = 144;                            // window top (144 = hidden)
    std::array<uint8_t, 20 * 10> win{};      // window tiles, 20 x 10
    std::vector<BattleObj> objs;
    std::vector<int> visibleSlots;           // enemy slots to draw (alive)
    int shakeX = 0;                          // hit flash: x offset of the struck group
    int flashSlot = -1;                      // enemy slot blinking (hit)
};

class Battle {
public:
    enum class Result { None, Won, Lost, Ran };
    static constexpr int kCursor = 0x20;     // OBJ tiles in battle VRAM (03:5430 / 03:57B0)
    static constexpr int kBracket = 0x24;

    // party: the 4 members + guest (index 4), updated in place (HP/MP) when the battle ends.
    // hffca = formation index, | $80 for fixed monster counts.
    Battle(const Rom& rom, std::vector<CharRecord>& party, int hffca, uint32_t seed);

    void tick(const BattleInput& in);
    BattleView view() const;
    Result result() const { return result_; }
    bool finished() const { return state_ == State::Done; }

    const Formation& formation() const { return form_; }
    const EnemyLayout& layout() const { return layout_; }
    const std::vector<Monster>& monsters() const { return groups_; }     // per group
    int enemyHp(int slot) const { return enemies_[size_t(slot)].hp; }
    int memberSprite(int m) const;                                       // field sprite page (04:xx00)
    std::vector<std::string> log;                                        // every message line (tests)
    int gpWon = 0;

private:
    enum class State { Intro, FightMenu, Command, Target, Running, Done };
    struct Fighter {
        bool enemy = false;
        int index = 0;            // party member / enemy slot
        int group = 0;
        Glyphs name;
        int hp = 0, maxHp = 0, mp = 0, maxMp = 0, str = 0, def = 0, mag = 0, agi = 0, eva = 0;
        bool alive() const { return hp > 0; }
    };
    struct Command { int kind = 0; int item = 0xff; int targetGroup = 0; bool set = false; };
    struct Step { std::vector<Glyphs> lines; std::function<void()> effect; int pause = 0; };

    int rnd(int n) { return n <= 0 ? 0 : int(rng_() % uint32_t(n)); }
    int aliveInGroup(int g) const;
    std::vector<int> aliveGroups() const;
    int firstWeapon(int member) const;
    void beginRound();
    void act(Fighter& who, int targetGroup, int item, bool weapon);
    void say(std::vector<Glyphs> lines, std::function<void()> effect = nullptr, int pause = 0);
    void endBattle(Result r);
    void putText(std::array<uint8_t, 200>& w, int col, int row, const Glyphs& g) const;
    void putNum(std::array<uint8_t, 200>& w, int col, int row, int v, int width) const;
    void box(std::array<uint8_t, 200>& w, int c0, int r0, int cw, int rh) const;
    void statusPanel(BattleView& v, int hideCol) const;

    const Rom& rom_;
    std::vector<CharRecord>& party_;
    Formation form_;
    EnemyLayout layout_;
    std::vector<Monster> groups_;
    std::vector<Fighter> members_, enemies_;
    std::mt19937 rng_;
    State state_ = State::Intro;
    Result result_ = Result::None;
    int timer_ = 0, cursor_ = 0, member_ = 0, targetCursor_ = 0;
    std::array<Command, 4> cmds_{};
    // message window: queued steps, typed lines
    std::deque<Step> steps_;
    std::vector<Glyphs> shown_;               // finished lines (last two visible)
    Glyphs typing_;
    size_t typed_ = 0;
    int typeDelay_ = 0, wait_ = 0, flashSlot_ = -1, flashTimer_ = 0;
    bool stepStarted_ = false;
};

} // namespace ffl3
