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
    // if its wait is over. Afterwards oam() / reg() / blank describe the frame.
    void frame();

    const std::array<uint8_t, 160>& oam() const { return oam_; }
    // A video register ($FF40-$FF4B) as it was on screen line `line` this frame: the routines
    // change SCX / SCY / BGP part-way down the screen by waiting for LY.
    uint8_t reg(int line, uint16_t r) const;
    bool blank = false;                      // LCD off this frame (02:43C8)
    bool ownsWindow = false;                 // the routine drives the window (map $9C00, WX/WY)
    bool restoreBg = false;                  // set after effect $14 (it overwrote the pictures)

    // 0B:7DCA with a=2 (via 0C:6001): the members' battle sprites into OBJ tiles $6C+.
    void loadMemberTiles(const std::function<int(int)>& spritePage);
    std::function<void(int slot)> eraseEnemy;   // a picture was cleared from the BG (-1: all)
    std::function<uint8_t(uint8_t)> random;    // 00:1D2E
    std::function<void(int)> sfx;              // a write to hFFBC
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
    void atLine(int line, uint16_t r, uint8_t v); // wait for LY = line, then write r
    void copy(uint16_t src, uint16_t dst, int n);  // RAM to RAM
    uint8_t rnd(uint8_t n) { return random ? random(n) : 0; }
    void sound(int id) { if (sfx) sfx(id); }
    void clearRect();                             // 02:52C1: picture rect wDE17-DE1A -> $FF
    bool spawnGroup(uint16_t motion, uint16_t group, uint8_t xb, uint8_t yb);   // 02:4757
    AnimTask converge();                          // 02:44B8 (effect 5)
    AnimTask wave();                              // 02:4437 (effect 6)
    void rasterTable(uint16_t r, int lines);      // 02:447F: r on lines 0..lines-1 from wDE40
    AnimTask rain(int id);                        // 02:463E / 4697 / 458A / 45C0 / 45C9 (7, 8, 9, D, C)
    AnimTask sparks();                            // 02:47C6 (effect A)
    AnimTask tilePatch();                         // 02:4BCD (effect F)
    AnimTask death1();                            // 02:5055
    AnimTask death3();                            // 02:5167
    AnimTask flashLines(uint16_t table, int idx); // 02:51DE
    AnimTask death2();                            // 02:50ED
    AnimTask vortex();                            // 02:48C6 (effect B)
    void vortexLines(uint16_t table, int start, int n, uint8_t bgp);   // 02:498C
    AnimTask swapPicture();                       // 02:4F3E (effect 13)
    AnimTask thrown();                            // 0E:7DBF (effect 14)
    void maskMap(int bank, uint16_t mask, int bytes);   // 0E:7D7E

    const Rom& rom_;
    Mem& mem_;
    AnimTask root_;
    std::coroutine_handle<> cur_;
    int wait_ = 0;
    bool blankNext_ = false;
    struct LineWrite { int line; uint16_t r; uint8_t v; };
    std::vector<LineWrite> writes_;
    std::array<uint8_t, 12> base_{};
    bool based_ = false;
    std::array<uint8_t, 160> oam_{};
};

} // namespace ffl3
