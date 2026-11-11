// The battle script VM (bank 0C, interpreter at 0C:4008). The whole battle - setup, turn
// order, AI, damage, messages - is bytecode in bank 0C; the game enters it once at 0C:4A2D.
// This runs that bytecode exactly as the GB does, on a 64K memory image; "native" calls
// (VM op 03) go to a handler so the port can supply the UI / helper routines.
//
// VM registers live in RAM like the original: wCC01/02 store address, wCC03 store size,
// wCC04 compare op, wCC05-07 accumulator, wCC08-0A operand, wCC0B sign, wCC0C-0E compare
// left side, wCC0F/10 VM stack pointer (stack from wCC11). Variables: wC900-C9FF.
// See tools/battlevm.py for the instruction set.
#pragma once
#include "rom.h"
#include <array>
#include <cstdint>
#include <functional>

namespace ffl3 {

class BattleVM {
public:
    enum class Stop { Yield, End, Limit, Bad };

    explicit BattleVM(const Rom& rom);

    std::array<uint8_t, 0x10000> mem{};      // RAM image; $0000-$7FFF reads come from the ROM

    uint8_t rd(uint16_t a) const;
    uint16_t rd16(uint16_t a) const { return uint16_t(rd(a) | rd(uint16_t(a + 1)) << 8); }
    void wr(uint16_t a, uint8_t v) { if (a == 0xffbc && onSfx) onSfx(v); if (a >= 0x8000) { mem[a] = v; if (written) written[a] = 1; } }
    // Debug: when set, reads of RAM not yet written since start() are counted in uninit[].
    uint8_t* written = nullptr;
    uint32_t* uninit = nullptr;

    void start(uint16_t pc);                  // 0C:4000: VM stack at wCC11, pc = de
    // Run until a native asks to yield, the program ends, or `maxSteps` statements ran.
    Stop run(long maxSteps = 1000000);
    uint16_t pc() const { return de_; }
    long steps() const { return steps_; }

    // Native call (op 03): addr in bank 0C (or bank 0 below $4000). Return true to keep
    // running, false to yield (the VM resumes after the call on the next run()).
    std::function<bool(uint16_t addr)> native;
    // 00:1D2E: random number 0..n-1 (0 when n = 0). Used by operand mode 5 and expr op 12.
    std::function<uint8_t(uint8_t n)> random;
    // Called before each statement (tests compare the address trail with the real game).
    std::function<void(uint16_t de)> onStep;
    // the bytecode requests sound effects by writing hFFBC (the sound driver's SFX slot)
    std::function<void(uint8_t id)> onSfx;

private:
    void operand();                           // 0C:43C0 -> wCC08
    void loadSized(int c, uint16_t hl);       // 0C:43D6
    void store();                             // 0C:4017
    bool expression();                        // 0C:4186 loop; false = statement finished
    void compareSave();                       // 0C:444F
    void neg24(uint16_t top);                 // 0C:441F
    void call(uint16_t target);               // 0C:40F1

    const Rom& rom_;
    uint16_t de_ = 0;
    long steps_ = 0;
};

} // namespace ffl3
