#pragma once
// --story ROUTE: plays a story route (tests/story/route.txt) on the port, one frame at a time,
// and logs a checkpoint line per check / expect. tools/story_emu.py plays the same file on the
// real game (PyBoy) with the same rules; tests/story_compare.py diffs the two logs.
//
// Steps: new | go D3 L2 ... | a | jump | face D | wait N | keys A B S U D L R wN | settle | check LABEL |
//        expect map MM [row col] | == section name.   '#' starts a comment.
// While the game isn't free (text, battle, menus, fades) the player mashes A.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "battleanim.h"   // AnimTask

namespace ffl3 {

struct StoryView {
    bool loop = false;          // the field main loop runs (no text, battle, menu, fade)
    bool free = false;          // ... and the leader stands on the grid: it would take a step now
    int map = 0, row = 0, col = 0;
    std::string flags;          // wC760-C77F as hex
    std::string party;          // "hp/max ..." for members 1-4
};

class StoryPlayer {
public:
    enum Key : uint8_t { Up = 1, Down = 2, Left = 4, Right = 8, A = 16, B = 32, Start = 64 };
    bool load(const std::string& path, std::string& err);
    uint8_t frame(const StoryView& v);           // keys to hold this frame; call once a frame
    bool finished() const { return done_; }
    bool failed() const { return failed_; }
    std::function<void(const std::string& line, const std::string& label)> onLine;   // CHECK / FAIL / DONE
    uint64_t frames() const { return frame_; }

private:
    struct Step { int line; std::vector<std::string> w; };
    std::vector<Step> steps_;
    StoryView v_;
    uint64_t frame_ = 0;
    int freeRun_ = 0;
    uint8_t out_ = 0;
    bool started_ = false, done_ = false, failed_ = false;
    std::coroutine_handle<> cur_;
    AnimTask root_;

    struct Tick {
        StoryPlayer* p; uint8_t keys;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept { p->cur_ = h; p->out_ = keys; }
        void await_resume() const noexcept {}
    };
    Tick tick(uint8_t keys = 0) { return Tick{this, keys}; }
    bool isFree() const { return freeRun_ >= 2; }
    AnimTask run();
    AnimTask mashUntilFree(int limit, bool start, bool* ok);
    AnimTask press(uint8_t key, int n);
    void emit(const char* kind, const std::string& label);
};

} // namespace ffl3
