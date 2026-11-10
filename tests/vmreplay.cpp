// Replays a battle recorded in the real game (tools/trace, REPLAY=...) through the C++ battle VM:
// natives are replaced by the RAM changes they made in the real game, random numbers by the
// recorded ones. Every VM statement address must match the real game's, in order.
// usage: vmreplay rom.gb enter.mem replay.txt
#include "../src/battlevm.h"
#include "../src/battlenatives.h"
#include <map>
#include <set>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>
using namespace ffl3;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: vmreplay rom enter.mem replay.txt\n"); return 2; }
    Rom rom; std::string err;
    if (!rom.load(argv[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    BattleVM vm(rom);
    FILE* f = std::fopen(argv[2], "rb");
    if (!f || std::fread(vm.mem.data(), 1, 0x10000, f) != 0x10000) { std::fprintf(stderr, "bad mem\n"); return 2; }
    std::fclose(f);
    std::deque<uint16_t> steps, randoms;
    struct Nat { uint16_t addr; std::vector<std::pair<uint16_t, uint8_t>> diff; };
    std::deque<Nat> nats;
    f = std::fopen(argv[3], "r");
    static char line[1 << 20];
    while (std::fgets(line, sizeof line, f)) {
        unsigned a, v;
        if (line[0] == 'S' && std::sscanf(line + 2, "%x", &a) == 1) steps.push_back(uint16_t(a));
        else if (line[0] == 'R' && std::sscanf(line + 2, "%x", &a) == 1) randoms.push_back(uint16_t(a));
        else if (line[0] == 'N' && std::sscanf(line + 2, "%x", &a) == 1) nats.push_back({uint16_t(a), {}});
        else if (line[0] == 'D' && !nats.empty()) {
            for (char* p = std::strtok(line + 1, " \n"); p; p = std::strtok(nullptr, " \n"))
                if (std::sscanf(p, "%x:%x", &a, &v) == 2) nats.back().diff.push_back({uint16_t(a), uint8_t(v)});
        }
    }
    std::fclose(f);
    const size_t total = steps.size();
    vm.start(steps.empty() ? 0x4a2d : steps.front());
    bool bad = false;
    std::vector<uint16_t> recent;
    vm.onStep = [&](uint16_t de) {
        recent.push_back(de);
        if (const char* w = std::getenv("WATCH"))
            if (de == std::strtol(w, nullptr, 16)) {
                std::printf("at %04x:", de);
                for (int i = 0; i < 0x100; i++) std::printf("%s%02x", i % 32 ? " " : "\n  ", vm.mem[0xc900 + i]);
                std::printf("\n");
            }
        if (bad) return;
        if (steps.empty()) { std::printf("extra step %04x\n", de); bad = true; return; }
        if (steps.front() != de) {
            std::printf("MISMATCH after %zu steps: game %04x, port %04x. last:", total - steps.size(), steps.front(), de);
            for (size_t i = recent.size() > 8 ? recent.size() - 8 : 0; i < recent.size(); i++) std::printf(" %04x", recent[i]);
            std::printf("\n");
            bad = true;
            return;
        }
        steps.pop_front();
    };
    size_t natCount = 0;
    int nativeFails = 0;
    std::map<uint16_t, int> nativeOk;
    vm.native = [&](uint16_t addr) {
        if (nats.empty() || nats.front().addr != addr) {
            std::printf("native mismatch: port %04x, game %04x\n", addr, nats.empty() ? 0 : nats.front().addr);
            bad = true; return false;
        }
        // Logic natives: run ours and compare with what the game did (outside stack/OAM/gfx).
        auto relevant = [](uint16_t a) {   // not stack, OAM buffer, sprite/sound work areas (interrupts)
            return !(a >= 0xcf80 && a <= 0xcfff) && !(a >= 0xc000 && a < 0xc100) && !(a >= 0xcb00 && a < 0xcc00) &&
                   !(a >= 0xdc80 && a < 0xdd00) && a < 0xff80; };
        {
            BattleVM copy = vm;
            if (runLogicNative(copy, rom, addr)) {
                std::map<uint16_t, uint8_t> mine, game;
                for (int a = 0x8000; a < 0x10000; a++) if (copy.mem[size_t(a)] != vm.mem[size_t(a)] && relevant(uint16_t(a))) mine[uint16_t(a)] = copy.mem[size_t(a)];
                for (auto& d : nats.front().diff) if (relevant(d.first) && vm.mem[d.first] != d.second) game[d.first] = d.second;
                if (mine != game) {
                    std::printf("native %04x differs:", addr);
                    for (auto& m : mine) if (!game.count(m.first) || game[m.first] != m.second) std::printf(" port %04x=%02x", m.first, m.second);
                    for (auto& g : game) if (!mine.count(g.first) || mine[g.first] != g.second) std::printf(" game %04x=%02x", g.first, g.second);
                    std::printf("\n");
                    nativeFails++;
                } else nativeOk[addr]++;
            }
        }
        for (auto& d : nats.front().diff) vm.mem[d.first] = d.second;
        nats.pop_front();
        natCount++;
        return true;
    };
    vm.random = [&](uint8_t) { if (randoms.empty()) { std::printf("out of randoms\n"); bad = true; return uint8_t(0); } uint8_t r = uint8_t(randoms.front()); randoms.pop_front(); return r; };
    while (!bad && !steps.empty()) {
        const auto st = vm.run(1);
        if (st == BattleVM::Stop::End || st == BattleVM::Stop::Bad) {
            if (!steps.empty()) std::printf("VM stopped (%d) with %zu steps left\n", int(st), steps.size());
            break;
        }
        if (nats.empty() && steps.size() > 0 && bad) break;
    }
    std::printf("logic natives checked:");
    for (auto& n : nativeOk) std::printf(" %04x x%d", n.first, n.second);
    std::printf(", %d differ\n", nativeFails);
    if (nativeFails) bad = true;
    std::printf("%s: %zu/%zu statements, %zu natives replayed\n", bad || !steps.empty() ? "FAIL" : "PASS",
                total - steps.size(), total, natCount);
    return bad || !steps.empty() ? 1 : 0;
}
