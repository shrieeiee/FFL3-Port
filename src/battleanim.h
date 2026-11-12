// Battle animations: the bank 2 routines (02:4000-52FF), hand-translated.
//
// They work on the battle's 64K memory image exactly as on the GB: the OAM shadow at wC000
// (30 effect sprites + the party row at wC078), OBJ tile data at $8000-$8FFF, the party-row
// tables at wDC80-DCAA and the scroll / palette registers at $FF42 / $FF43 / $FF47 / $FF48.
// Each routine is a C++20 coroutine; a wait of n frames (00:3A6B / 3A6D) suspends it and
// frame() resumes it once the frames have passed, so the battle simply waits until busy()
// is false - the same thing the GB's battle code does when it calls into bank 2.
#pragma once
#include "rom.h"
#include <array>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace ffl3 {

// A coroutine that can co_await other AnimTasks (nested calls, like the GB's call / ret).
struct AnimTask {
    struct promise_type {
        std::coroutine_handle<> cont;
        AnimTask get_return_object() { return AnimTask{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct Final {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto c = h.promise().cont;
                return c ? c : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        Final final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { throw; }
    };
    std::coroutine_handle<promise_type> h;
    AnimTask() = default;
    explicit AnimTask(std::coroutine_handle<promise_type> x) : h(x) {}
    AnimTask(AnimTask&& o) noexcept : h(std::exchange(o.h, {})) {}
    AnimTask& operator=(AnimTask&& o) noexcept {
        if (this != &o) { if (h) h.destroy(); h = std::exchange(o.h, {}); }
        return *this;
    }
    AnimTask(const AnimTask&) = delete;
    ~AnimTask() { if (h) h.destroy(); }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept { h.promise().cont = c; return h; }
    void await_resume() const noexcept {}
};

class BattleAnim {
public:
    using Mem = std::array<uint8_t, 0x10000>;
    BattleAnim(const Rom& rom, Mem& mem);

    void start(uint16_t entry);       // 0x4000: attack (wC9BD), 0x4394: effect wDE00
    bool busy() const { return root_.h && !root_.h.done(); }
    // One frame: the vblank work (party row 00:3E26, OAM DMA), then the routine runs on
    // if its wait is over. Afterwards oam() / topBgp / topObp / blank describe the frame.
    void frame();

    const std::array<uint8_t, 160>& oam() const { return oam_; }
    uint8_t topBgp = 0xd2, topObp = 0xd0;    // palettes on lines 0-63 (02:441A raster split)
    bool blank = false;                      // LCD off this frame (02:43C8)

    // 0B:7DCA with a=2 (via 0C:6001): the members' battle sprites into OBJ tiles $6C+.
    void loadMemberTiles(const std::function<int(int)>& spritePage);
    std::function<void(int slot)> eraseEnemy;   // 02:52C1 cleared the slot's picture from the BG
    std::function<uint8_t(uint8_t)> random;    // 00:1D2E
    std::vector<int> unknownIds;               // effect ids not translated yet (logged)

private:
    struct Wait {
        BattleAnim* a; int n;
        bool await_ready() const noexcept { return n <= 0; }
        void await_suspend(std::coroutine_handle<> h) noexcept { a->cur_ = h; a->wait_ = n; }
        void await_resume() const noexcept {}
    };
    Wait wait(int n) { return Wait{this, n}; }   // 00:3A6D (n = 1: 00:3A6B)

    uint8_t& m(uint16_t a) { return mem_[a]; }
    uint8_t rb(uint16_t a) const;                 // bank 2 / bank 0 data
    void copyRom(int bank, uint16_t src, uint16_t dst, int n);

    AnimTask attack();                            // 02:4000
    AnimTask effect(int id);                      // 02:4397
    AnimTask bob(uint16_t table);                 // 02:4023
    AnimTask partySwap();                         // 02:40A7
    AnimTask partyRowInit();                      // 02:4110
    AnimTask partyRowClear();                     // 02:4142
    void statusMask();                            // 02:4167
    uint8_t memberMask(uint8_t e, uint8_t d);     // 02:4171
    void setMemberY(int i, uint8_t y);            // 02:40F0
    void setMemberX(int i, uint8_t x);            // 02:40DB
    AnimTask script();                            // 02:41B1
    void drawGrid(uint8_t code, uint8_t grid);    // 02:4210
    int position(uint8_t& x, uint8_t& y);         // 02:42C0 (returns a)
    void clearOam();                              // 02:4FC5
    AnimTask shake();                             // 02:439F
    AnimTask lcdBlink();                          // 02:43C8
    AnimTask fade(uint16_t table);                // 02:4408 / 440B
    AnimTask flashTop(uint8_t pal);               // 02:441A
    AnimTask vanish();                            // 02:43EA
    AnimTask vanishReturn();                      // 02:43F8
    AnimTask bgFlash();                           // 02:4BA9
    AnimTask damage(bool miss);                   // 02:4D45 / 4D65
    bool spawnNumber();                           // 02:4DAE
    void particlesInit(uint8_t speed);            // 02:4E7C
    void particleAdd(uint16_t motion);            // 02:4E8C
    void particlesStep();                         // 02:4ED2
    AnimTask particlesFrame(bool& more);          // 02:4EB9
    AnimTask death();                             // 02:4F20 (style 0: 4FA4)
    void vblank();                                // 00:3E26

    const Rom& rom_;
    Mem& mem_;
    AnimTask root_;
    std::coroutine_handle<> cur_;
    int wait_ = 0;
    bool blankNext_ = false;
    std::array<uint8_t, 160> oam_{};
};

} // namespace ffl3
