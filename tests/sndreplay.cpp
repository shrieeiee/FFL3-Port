// Checks the C++ sound driver against a register log recorded from the real game.
// usage: sndreplay rom.gb sound.log
// The log (tools/sndtrace) has one "C frame <wCB00-CB67> <hFFBA-FFC5>" line at each entry to
// 0F:4006 followed by the APU writes made during that call. The driver is fed the same music /
// SFX requests (hFFBA / hFFBC) and must make identical writes and reach identical state.
#include "rom.h"
#include "sound.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ffl3;

struct Call { long frame; uint8_t wram[0x68]; uint8_t hram[12]; std::vector<std::pair<int, int>> writes; };

static bool hramDiff(const uint8_t* a, const uint8_t* b) {
    for (int k = 1; k < 12; k++) if (k != 2 && a[k] != b[k]) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: sndreplay rom.gb sound.log\n"); return 2; }
    Rom rom; std::string err;
    if (!rom.load(argv[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    std::ifstream in(argv[2]);
    std::vector<Call> calls; std::string line;
    std::vector<std::pair<int, int>> pre;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        if (line[0] == 'C') {
            Call c; char t; ss >> t >> c.frame;
            for (auto& b : c.wram) { unsigned v; ss >> std::hex >> v; b = uint8_t(v); }
            for (auto& b : c.hram) { unsigned v; ss >> std::hex >> v; b = uint8_t(v); }
            calls.push_back(c);
        } else {
            long f; unsigned reg, val; std::string pc;
            ss >> std::dec >> f >> std::hex >> reg >> val >> pc;
            if (pc.rfind("0f:", 0) != 0) continue;
            (calls.empty() ? pre : calls.back().writes).push_back({int(reg), int(val)});
        }
    }
    uint8_t apu[0x30] = {};
    apu[0x26 - 0x10] = 0xf1;
    for (auto& w : pre) apu[w.first - 0x10] = uint8_t(w.second);
    SoundDriver drv(rom);
    std::vector<std::pair<int, int>> got;
    drv.out = [&](uint8_t r, uint8_t v) { apu[r - 0x10] = v; got.push_back({r, v}); };
    drv.in = [&](uint8_t r) { return apu[r - 0x10]; };
    std::memcpy(drv.wram, calls[0].wram, 0x68);
    std::memcpy(drv.hram, calls[0].hram, 12);
    int bad = 0;
    for (size_t i = 0; i < calls.size(); i++) {
        const Call& c = calls[i];
        if (i) {   // compare state at entry
            if (std::memcmp(drv.wram, c.wram, 0x68) || hramDiff(drv.hram, c.hram)) {
                std::printf("state mismatch at frame %ld:\n", c.frame);
                for (int k = 0; k < 0x68; k++) if (drv.wram[k] != c.wram[k]) std::printf("  cb%02x got %02x want %02x\n", k, drv.wram[k], c.wram[k]);
                for (int k = 1; k < 12; k++) if (k != 2 && drv.hram[k] != c.hram[k]) std::printf("  ff%02x got %02x want %02x\n", 0xba + k, drv.hram[k], c.hram[k]);
                if (++bad > 5) return 1;
            }
        }
        drv.hram[0] = c.hram[0];   // requests made by the game since the last call
        drv.hram[2] = c.hram[2];
        got.clear();
        drv.update();
        if (got != c.writes) {
            std::printf("write mismatch at frame %ld:\n  got ", c.frame);
            for (auto& w : got) std::printf(" %02x=%02x", w.first, w.second);
            std::printf("\n  want");
            for (auto& w : c.writes) std::printf(" %02x=%02x", w.first, w.second);
            std::printf("\n");
            if (++bad > 5) return 1;
        }
    }
    std::printf("%zu driver calls, %s\n", calls.size(), bad ? "MISMATCH" : "all match");
    return bad ? 1 : 0;
}
