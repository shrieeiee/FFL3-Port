// Text / event bytecode interpreter (original: 00:3211 loop, handlers at 00:3175).
//
// Byte stream:
//   $00        end            $10  wait for A, end      $06  newline
//   $0B        wait for A     $07/$14  clear window     $1C  wait for A (before each new speaker)
//   $1F n      party member n's name (other $1D-$27 name/number codes take 1 byte)
//   $36/$37 f  set / clear event flag f
//   $39 f A B  if flag f: run ref A else ref B
//   $3A f g A B C  if f: A  elif g: B  else C
//   $3C A B    Yes/No prompt: Yes -> ref A, No -> ref B
//   $3E A      run ref A
//   $0A $0D $12 $13 $32 $38 $3B $3D $3F p   shorthand event calls (table 0B:6C4C), param p
//   $2A n      spell (n < $30, 0E:6D9B) or item (0E:6EBB) name
//   $4E-$87 and 22 low codes (00:30d1): dictionary words (03:71C5), which may contain codes
//   $88-$FF    glyphs (= tile numbers in the shared font, 03:4000 -> $8800)
// A "ref" is 2 bytes (lo, hi): hi < $40 = event command hi with parameter lo (00:1236);
// otherwise another text: hi $40-7F -> bank 9, $80-BF -> bank $A, $C0-FF -> bank 3.
// Every NPC text (239 of them) parses with these lengths and ends exactly where the next one
// begins - see tools/ffl3text.py.
#pragma once
#include "flags.h"
#include "rom.h"
#include <array>
#include <bitset>
#include <functional>
#include <string>
#include <vector>

namespace ffl3 {

class TextBox {
public:
    static constexpr int kCols = 18, kRows = 3;

    explicit TextBox(const Rom& rom);

    // Start a text at bank:addr. Opens the window.
    void open(int bank, uint16_t addr);
    void call(int bank, uint16_t addr);   // run inside the current text if one is active, else open
    bool active() const { return active_; }
    // Abandon the running text (game over mid-script).
    void close() { stack_.clear(); active_ = false; state_ = State::Idle; }
    // One 60 Hz frame. a/b/up/down = button states this frame (pressed = edge).
    void tick(bool aPressed, bool aHeld, bool upPressed, bool downPressed);

    // Window contents for the renderer: kRows lines of tile numbers ($FF = blank).
    const std::array<std::array<uint8_t, kCols>, kRows>& lines() const { return lines_; }
    bool waitingForA() const { return state_ == State::WaitA || state_ == State::WaitScroll || state_ == State::WaitEnd; }
    bool choosing() const { return state_ == State::Choice; }
    int choice() const { return choice_; }
    int blink() const { return blink_; }

    GameFlags* flags = nullptr;
    // Names of members 0-4 (record +7, 6 glyphs) and the class label of the record being named
    // (00:29C0); when set they are used instead of partyNames.
    std::function<std::vector<uint8_t>(int member)> memberName;
    std::function<std::vector<uint8_t>()> classLabel;
    std::array<std::string, 4> partyNames{{"Arthur", "Curtis", "Sharon", "Gloria"}};
    int itemArg = -1;                                   // wCE94 for the $1E item-name code
    std::function<void(int cmd, int param)> onEvent;   // event command refs (hi < $40)
    std::function<void(const std::string&)> log;
    std::function<bool()> isBusy;                      // scripts wait for actor moves
    std::function<bool()> mustWait;                    // a battle / shop / cockpit an event opened: stop until it closes

private:
    enum class State { Idle, Run, WaitA, WaitScroll, WaitEnd, Choice };
    struct Src {                      // a byte source: ROM text, or an in-memory buffer
        int bank = 0; uint16_t addr = 0;
        std::vector<uint8_t> buf; size_t pos = 0;
        bool isBuf = false, isCall = false;
    };
    int next();                       // next byte from the source stack, -1 at end of buffers
    void runRef(uint8_t lo, uint8_t hi);
    void putGlyph(uint8_t t);
    void newline();
    void clear();
    void finish();                    // end of the current text (return from a call or close)
    void step();                      // execute until something visible happens or we block

    const Rom& rom_;
    std::vector<uint8_t> dict_[80];
    std::vector<Src> stack_;
    std::array<std::array<uint8_t, kCols>, kRows> lines_{};
    int row_ = 0, col_ = 0;
    bool active_ = false;
    State state_ = State::Idle;
    int delay_ = 0, blink_ = 0, choice_ = 0;
    uint8_t choiceA_[2]{}, choiceB_[2]{};
    std::vector<uint8_t> pendingGlyphs_;
    bool waitEvent_ = false;
    bool clearOnResume_ = false;
    bool waitBeforeNextLine_ = false;
public:
    std::vector<std::array<uint8_t, kCols>> history;   // every finished line (tests)
};

} // namespace ffl3
