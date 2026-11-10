// Replays a battle recorded in the real game (tools/trace, REPLAY=...) through the C++ battle VM:
// natives are replaced by the RAM changes they made in the real game, random numbers by the
// recorded ones. Every VM statement address must match the real game's, in order.
// usage: vmreplay rom.gb enter.mem replay.txt
#include "../src/battlevm.h"
#include "../src/battlenatives.h"
#include "../src/battledata.h"
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
    std::deque<std::vector<std::pair<uint16_t, uint8_t>>> reentries;   // RAM changes between VM runs
    f = std::fopen(argv[3], "r");
    static char line[1 << 20];
    while (std::fgets(line, sizeof line, f)) {
        unsigned a, v;
        if (line[0] == 'S' && std::sscanf(line + 2, "%x", &a) == 1) steps.push_back(uint16_t(a));
        else if (line[0] == 'R' && std::sscanf(line + 2, "%x", &a) == 1) randoms.push_back(uint16_t(a));
        else if (line[0] == 'N' && std::sscanf(line + 2, "%x", &a) == 1) nats.push_back({uint16_t(a), {}});
        else if (line[0] == 'E') {
            reentries.push_back({});
            for (char* p = std::strtok(line + 1, " \n"); p; p = std::strtok(nullptr, " \n"))
                if (std::sscanf(p, "%x:%x", &a, &v) == 2) reentries.back().push_back({uint16_t(a), uint8_t(v)});
        }
        else if (line[0] == 'D' && !nats.empty()) {
            for (char* p = std::strtok(line + 1, " \n"); p; p = std::strtok(nullptr, " \n"))
                if (std::sscanf(p, "%x:%x", &a, &v) == 2) nats.back().diff.push_back({uint16_t(a), uint8_t(v)});
        }
    }
    std::fclose(f);
    if (std::getenv("BUILD")) {               // build the whole entry RAM from game state, compare
        BattleVM t(rom);
        auto cp = [&](int a, int n) { for (int i = 0; i < n; i++) t.mem[size_t(a + i)] = vm.mem[size_t(a + i)]; };
        cp(0xc500, 0x300);                    // game state: party, inventory, flags, position...
        const int form = vm.mem[0xcc5d - 4] == 0 ? 0 : 0;   // formation index unknown here: use ids
        (void)form;
        cp(0xcc59, 5);                        // formation bytes
        cp(0xcc36, 3);                        // counts (random draw)
        t.mem[0xcd37] = 1;
        const int ids[3] = {vm.mem[0xcc59], vm.mem[0xcc5a], vm.mem[0xcc5b]};
        t.mem[0xd684] = pictureResidue(rom, ids);
        battleSetupCombatants(t, rom);
        int diffs = 0;
        std::map<int, int> blocks;
        for (int a = 0xc000; a < 0x10000; a++) {
            if (a >= 0xe000 && a < 0xff80) continue;
            if (t.mem[size_t(a)] != vm.mem[size_t(a)]) { diffs++; blocks[a & 0xfff0]++; }
        }
        for (auto& b : blocks) {
            std::printf(" %04x(%d):", b.first, b.second);
            for (int a = b.first; a < b.first + 16; a++) if (t.mem[size_t(a)] != vm.mem[size_t(a)]) std::printf(" %02x=%02x", a & 0xff, vm.mem[size_t(a)]);
            std::printf("\n");
        }
        std::printf("build: %d bytes differ\n", diffs);
        return 0;
    }
    if (std::getenv("SETUP")) {               // rebuild the combatant table and compare
        BattleVM t = vm;
        for (int a = 0xd000; a < 0xd700; a++) t.mem[size_t(a)] = 0x5a;
        const int ids[3] = {vm.mem[0xcc59], vm.mem[0xcc5a], vm.mem[0xcc5b]};
        t.mem[0xd684] = pictureResidue(rom, ids);
        battleSetupCombatants(t, rom);
        int diffs = 0;
        for (int a = 0xc000; a < 0xe000; a++)
            if (t.mem[size_t(a)] != vm.mem[size_t(a)] && !(a >= 0xce25 && a <= 0xce29) && !(a >= 0xcedb && a <= 0xcee1)) {
                if (diffs++ < 40) std::printf(" %04x port %02x game %02x\n", a, t.mem[size_t(a)], vm.mem[size_t(a)]);
            }
        std::printf("setup: %d bytes differ\n", diffs);
        return diffs ? 1 : 0;
    }
    if (const char* z = std::getenv("ZERO")) {    // e.g. ZERO=c900-c9ff: wipe scratch RAM at entry
        unsigned a, b;
        if (std::sscanf(z, "%x-%x", &a, &b) == 2) for (unsigned i = a; i <= b; i++) vm.mem[i] = 0;
    }
    static uint8_t wmap[0x10000]; static uint32_t umap[0x10000];
    if (std::getenv("UNINIT")) { vm.written = wmap; vm.uninit = umap; }
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
        if (const char* w = std::getenv("NATPRE"))
            if (addr == std::strtol(w, nullptr, 16))
                std::printf("pre %04x: vbf=%02x vd6=%02x vde=%02x d873=%02x %02x %02x %02x %02x ce7c=%02x %02x %02x %02x cd36=%02x cd4c=%02x ce8c=%02x c9a5=%02x\n", addr,
                    vm.mem[0xc9bf], vm.mem[0xc9d6], vm.mem[0xc9de], vm.mem[0xd873], vm.mem[0xd874], vm.mem[0xd875], vm.mem[0xd876], vm.mem[0xd877],
                    vm.mem[0xce7c], vm.mem[0xce7d], vm.mem[0xce7e], vm.mem[0xce7f], vm.mem[0xcd36], vm.mem[0xcd4c], vm.mem[0xce8c], vm.mem[0xc9a5]);
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
        for (auto& d : nats.front().diff) { vm.mem[d.first] = d.second; wmap[d.first] = 1; }
        nats.pop_front();
        natCount++;
        if (const char* w = std::getenv("NATDUMP"))
            if (addr == std::strtol(w, nullptr, 16)) {
                std::printf("after %04x: ce00", addr);
                for (int i = 0; i < 0x14; i++) std::printf("%s%02x", i % 4 ? "" : " ", vm.mem[0xce00 + i]);
                std::printf(" d061=%02x ffbc=%02x\n", vm.mem[0xd061], vm.mem[0xffbc]);
            }
        return true;
    };
    vm.random = [&](uint8_t) { if (randoms.empty()) { std::printf("out of randoms\n"); bad = true; return uint8_t(0); } uint8_t r = uint8_t(randoms.front()); randoms.pop_front(); return r; };
    while (!bad && !steps.empty()) {
        const auto st = vm.run(1);
        if (st == BattleVM::Stop::End && !steps.empty() && steps.front() == 0x4a2d) {
            std::printf("battle restarted (retry after defeat)\n");
            if (!reentries.empty()) { for (auto& d : reentries.front()) vm.mem[d.first] = d.second; reentries.pop_front(); }
            vm.start(0x4a2d);
            continue;
        }
        if (st == BattleVM::Stop::End || st == BattleVM::Stop::Bad) {
            if (!steps.empty()) std::printf("VM stopped (%d) with %zu steps left\n", int(st), steps.size());
            break;
        }
        if (nats.empty() && steps.size() > 0 && bad) break;
    }
    if (vm.uninit) {
        std::printf("read before written:");
        for (int a = 0xc000; a < 0x10000; a++) if (umap[a]) std::printf(" %04x=%02x", a, vm.mem[size_t(a)]);
        std::printf("\n");
    }
    std::printf("logic natives checked:");
    for (auto& n : nativeOk) std::printf(" %04x x%d", n.first, n.second);
    std::printf(", %d differ\n", nativeFails);
    if (nativeFails) bad = true;
    std::printf("%s: %zu/%zu statements, %zu natives replayed\n", bad || !steps.empty() ? "FAIL" : "PASS",
                total - steps.size(), total, natCount);
    return bad || !steps.empty() ? 1 : 0;
}
