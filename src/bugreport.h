// Tester support: a rolling record of the last minutes of input and game events, written
// out with a screenshot and a save when the tester presses F12 (or when the port crashes).
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace ffl3 {

class BugLog {
public:
    static constexpr size_t kMaxFrames = 60 * 60 * 10;   // ten minutes of input
    static constexpr size_t kMaxLines = 400;

    void frame(uint16_t keys);                       // once per 60 Hz logic frame
    void event(const std::string& line);             // stamped with the frame number
    uint64_t frameNo() const { return frame_; }

    // Writes DIR/inputs.txt, log.txt, info.txt; the caller adds screen.bmp / game.sav.
    bool write(const std::string& dir, const std::string& info) const;

    // Keys bits for inputs.txt.
    enum : uint16_t { A = 1, B = 2, Start = 4, Up = 8, Down = 16, Left = 32, Right = 64, Menu = 128 };

private:
    uint64_t frame_ = 0;
    std::deque<std::pair<uint64_t, uint16_t>> keys_;   // runs: (first frame, keys)
    std::deque<std::string> lines_;
};

// A fresh folder bug_reports/YYYYMMDD-HHMMSS[-n] next to the exe (created).
std::string newReportDir(const std::string& base);

// Crash hooks: on a crash the handler is called once (best effort) before the process dies.
void installCrashHandler(std::function<void()> onCrash);

} // namespace ffl3
