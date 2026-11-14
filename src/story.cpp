#include "story.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ffl3 {

bool StoryPlayer::load(const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "can't open " + path; return false; }
    std::string ln;
    for (int n = 1; std::getline(f, ln); n++) {
        if (size_t h = ln.find('#'); h != std::string::npos) ln.resize(h);
        std::istringstream is(ln);
        Step s{n, {}};
        for (std::string w; is >> w;) s.w.push_back(w);
        if (!s.w.empty()) steps_.push_back(std::move(s));
    }
    return true;
}

void StoryPlayer::emit(const char* kind, const std::string& label) {
    char head[96];
    std::snprintf(head, sizeof head, "%s %s frame %llu map %02X pos %d,%d", kind, label.c_str(),
                  (unsigned long long)frame_, v_.map, v_.row, v_.col);
    const std::string line = std::string(head) + " flags " + v_.flags + " party " + v_.party;
    if (onLine) onLine(line, label);
}

uint8_t StoryPlayer::frame(const StoryView& v) {
    v_ = v;
    freeRun_ = v.free ? freeRun_ + 1 : 0;
    if (done_) return 0;
    if (!started_) { started_ = true; root_ = run(); cur_ = root_.h; }
    frame_++;
    out_ = 0;
    auto h = cur_;
    h.resume();
    if (root_.h.done()) done_ = true;
    return out_;
}

AnimTask StoryPlayer::press(uint8_t key, int n) {
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 3; k++) co_await tick(key);
        for (int k = 0; k < 17; k++) co_await tick();
    }
}

AnimTask StoryPlayer::mashUntilFree(int limit, bool start, bool* ok) {
    *ok = true;
    for (int n = 0; !isFree(); n++) {
        if (n >= limit) { *ok = false; co_return; }
        uint8_t k = 0;
        if (n % 16 < 2) k = start && (n / 16) % 4 == 3 ? uint8_t(A | Start) : uint8_t(A);
        co_await tick(k);
    }
}

static uint8_t dirKey(char c) {
    switch (c) {
    case 'U': return StoryPlayer::Up;
    case 'D': return StoryPlayer::Down;
    case 'L': return StoryPlayer::Left;
    case 'R': return StoryPlayer::Right;
    default: return 0;
    }
}

AnimTask StoryPlayer::run() {
    std::string section = "start";
    bool ok = true;
    auto fail = [&](const std::string& why) {
        std::string l = why;
        for (char& c : l) if (c == ' ') c = '_';
        emit("FAIL", l);
        failed_ = true;
    };
    for (const Step& s : steps_) {
        const std::string& op = s.w[0];
        const std::string at = "line " + std::to_string(s.line);
        if (op == "==") {
            section.clear();
            for (size_t i = 1; i < s.w.size(); i++) section += (i > 1 ? "_" : "") + s.w[i];
            continue;
        }
        if (op == "new") {
            co_await mashUntilFree(60000, true, &ok);
            if (!ok) { fail(at + " never got control"); co_return; }
        } else if (op == "go") {                                       // go D3 L2: end up 3 blocks down, ...
            for (size_t i = 1; i < s.w.size(); i++) {
                const std::string& t = s.w[i];
                const uint8_t d = dirKey(t[0]);
                const int n = t.size() > 1 ? std::atoi(t.c_str() + 1) : 1;
                const bool vert = d == Up || d == Down;
                const int sign = (d == Down || d == Right) ? 1 : -1;
                co_await mashUntilFree(20000, false, &ok);
                if (!ok) { fail(at + " stuck before " + t); co_return; }
                const int map0 = v_.map, from = vert ? v_.row : v_.col;
                int moved = 0, prev = from, guard = 0;
                while (moved < n && v_.map == map0) {
                    co_await mashUntilFree(20000, false, &ok);
                    if (!ok) { fail(at + " stuck in " + t); co_return; }
                    if (v_.map != map0) break;
                    const StoryView s0 = v_;
                    for (int f = 0;; f++) {
                        co_await tick(d);
                        if (v_.map != s0.map || v_.row != s0.row || v_.col != s0.col || !v_.loop) break;
                        if (f > 120) {
                            char b[96];
                            std::snprintf(b, sizeof b, "%s blocked going %s at %02X %d,%d", at.c_str(), t.c_str(), s0.map, s0.row, s0.col);
                            fail(b);
                            co_return;
                        }
                    }
                    const int now = vert ? v_.row : v_.col;
                    if (now != prev) {
                        int dd = (now - prev) * sign; if (dd < -32) dd += 64; if (dd > 32) dd -= 64;
                        moved += dd; prev = now;
                    }
                    if (++guard > 400) { fail(at + " wandering in " + t); co_return; }
                    co_await tick();
                }
            }
        } else if (op == "a") {
            co_await mashUntilFree(20000, false, &ok);
            if (!ok) { fail(at + " stuck before A"); co_return; }
            co_await tick(A); co_await tick(A); co_await tick(); co_await tick();
            co_await mashUntilFree(20000, false, &ok);
            if (!ok) { fail(at + " stuck after A"); co_return; }
        } else if (op == "jump") {                                      // B: jump the way the leader faces
            co_await mashUntilFree(20000, false, &ok);
            if (!ok) { fail(at + " stuck before jump"); co_return; }
            co_await tick(B); co_await tick(B); co_await tick(); co_await tick();
            co_await mashUntilFree(20000, false, &ok);
            if (!ok) { fail(at + " stuck after jump"); co_return; }
        } else if (op == "face") {                                      // face D: turn without moving (a tap)
            co_await mashUntilFree(20000, false, &ok);
            co_await tick(dirKey(s.w.size() > 1 ? s.w[1][0] : 'D'));
            co_await tick();
            co_await mashUntilFree(20000, false, &ok);
        } else if (op == "battles") {                                   // random encounters on / off
            if (onCommand) onCommand("battles " + (s.w.size() > 1 ? s.w[1] : std::string("on")));
        } else if (op == "wait") {
            const int n = s.w.size() > 1 ? std::atoi(s.w[1].c_str()) : 1;
            for (int i = 0; i < n; i++) co_await tick();
        } else if (op == "keys") {
            for (size_t i = 1; i < s.w.size(); i++) {
                const std::string& t = s.w[i];
                if (t[0] == 'w') { const int n = std::atoi(t.c_str() + 1); for (int f = 0; f < n; f++) co_await tick(); continue; }
                uint8_t k = dirKey(t[0]);
                if (t[0] == 'A') k = A; else if (t[0] == 'B') k = B; else if (t[0] == 'S') k = Start;
                for (int f = 0; f < 3; f++) co_await tick(k);
                for (int f = 0; f < 12; f++) co_await tick();
            }
        } else if (op == "settle") {
            co_await mashUntilFree(20000, false, &ok);
            if (!ok) { fail(at + " never settled"); co_return; }
        } else if (op == "peek") {                                    // log now, free or not
            emit("CHECK", s.w.size() > 1 ? s.w[1] : section + "_" + std::to_string(s.line));
        } else if (op == "check") {
            co_await mashUntilFree(20000, false, &ok);
            emit("CHECK", s.w.size() > 1 ? s.w[1] : section + "_" + std::to_string(s.line));
        } else if (op == "expect") {
            co_await mashUntilFree(20000, false, &ok);
            const int m = s.w.size() > 2 ? int(std::strtol(s.w[2].c_str(), nullptr, 16)) : -1;
            bool good = s.w.size() > 2 && s.w[1] == "map" && v_.map == m;
            if (good && s.w.size() > 4) good = v_.row == std::atoi(s.w[3].c_str()) && v_.col == std::atoi(s.w[4].c_str());
            if (!good) {
                char b[128];
                std::snprintf(b, sizeof b, "%s expected %s got %02X %d,%d", at.c_str(),
                              s.w.size() > 2 ? s.w[2].c_str() : "?", v_.map, v_.row, v_.col);
                fail(b);
                co_return;
            }
            emit("CHECK", section + "_" + std::to_string(s.line));
        } else {
            fail(at + " unknown step " + op);
            co_return;
        }
    }
    emit("DONE", "end");
}

} // namespace ffl3
