// FFL3 / SaGa 3 PC port: walk, talk, events, chests, battles, save/load.
//
//   ffl3 [rom.gb] [--map HEX] [--pos ROW,COL] [--walk UDLR...] [--shot out.bmp]
//                 [--wide] [--debug] [--dump-blocks out.bin] [--scale N] [--mute]
//                 [--wav music|sfx HEXID SECONDS]  (renders ffl3.wav and exits)
//
// Keys: arrows/WASD walk, Z/Space/Enter = A, X/Backspace = B, F1 collision overlay, F2 no-clip,
//       M / Shift field menu (Equip, Items, Status, Save), F3 random battles on/off, F4 start a random battle here, F5 save / F9 load (ffl3.sav),
//       Tab wide/GB view,
//       PgUp/PgDn ([ / ]) previous/next map, P palette, F12 screenshot, Esc quit.
#include "render.h"
#include "sound.h"
#include <functional>
#include <SDL.h>
#include <algorithm>
#include <memory>
#include <cstring>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ffl3;

namespace {

struct Palette { const char* name; uint32_t c[4]; };
const Palette kPalettes[] = {
    {"grey",   {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000}},
    {"dmg",    {0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F}},
    {"pocket", {0xFFC4CFA1, 0xFF8B956D, 0xFF4D533C, 0xFF1F1F1F}},
};
constexpr int kNumPalettes = int(sizeof(kPalettes) / sizeof(kPalettes[0]));

void toArgb(const Frame& f, const Palette& pal, const std::vector<uint8_t>* tint, std::vector<uint32_t>& out) {
    out.resize(f.px.size());
    static const uint32_t kTint[] = {0, 0x0000C000, 0x00E00000, 0x000040FF};
    for (size_t i = 0; i < f.px.size(); i++) {
        uint32_t c = pal.c[f.px[i]];
        if (tint && (*tint)[i]) {
            const uint32_t t = kTint[(*tint)[i]];
            const uint32_t r = ((c >> 16) & 255) / 2 + ((t >> 16) & 255) / 2;
            const uint32_t g = ((c >> 8) & 255) / 2 + ((t >> 8) & 255) / 2;
            const uint32_t b = (c & 255) / 2 + (t & 255) / 2;
            c = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
        out[i] = c;
    }
}

bool saveBmp(const std::vector<uint32_t>& px, int w, int h, const char* path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint32_t*>(px.data()), w, h, 32, w * 4,
                                                        SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

int nextValid(const Rom& rom, int from, int dir) {
    for (int i = 1; i <= kMapCount; i++) {
        const int id = ((from + dir * i) % kMapCount + kMapCount) % kMapCount;
        if (loadLayout(rom, loadMapHeader(rom, id).layout).valid) return id;
    }
    return from;
}

// What happens when A is pressed at an object (00:097f, by object kind wC350).
void talk(const Rom& rom, Field& field, TextBox& tb, const std::function<void(int slot, const MapObject&)>& chest,
          const std::function<void(int)>& shop) {
    const int i = field.faceObjectInFront();
    if (i < 0) return;
    const MapObject& o = field.actors()[size_t(i)].obj;
    char buf[96];
    switch (o.kind) {
    case 0:                                          // 00:0995 - dialogue, 0A:4004[id-1]
        if (o.id == 0 || o.id == 0xff) return;
        tb.open(0xa, rom.ptr(0xa, 0x4004, o.id - 1));
        break;
    case 3:                                          // 00:09b7 - special objects, 0A:791E[id-1]
        if (o.id == 0 || o.id >= 9) return;
        tb.open(0xa, rom.ptr(0xa, 0x791e, o.id - 1));
        break;
    case 1:                                          // 00:09D7 - chest
        chest(i, o);
        break;
    default:                                         // 2 = shop (00:0A31 -> 01:6FD4, wCA2F = id - 1)
        (void)buf;
        if (o.id) shop(o.id - 1);
        break;
    }
}

int dirFromChar(char c) {
    switch (c) {
    case 'U': case 'u': return int(Dir::Up);
    case 'R': case 'r': return int(Dir::Right);
    case 'D': case 'd': return int(Dir::Down);
    case 'L': case 'l': return int(Dir::Left);
    default: return -1;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string romPath = "saga3.gb", shot, dumpTiles, walk, savePath, loadPath;
    int textId = -1, runTextId = -1, battleArg = -1, shotAt = -1;
    std::string keys, dumpView, ramPokes;
    int mapId = -1, scale = 5, startRow = -1, startCol = -1;
    bool wide = false, debug = false, showSlots = false, mute = false;
    std::string wavKind;
    int partyHp = -1;
    int wavId = 0;
    double wavSecs = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--map" && i + 1 < argc) mapId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--pos" && i + 1 < argc) std::sscanf(argv[++i], "%d,%d", &startRow, &startCol);
        else if (a == "--walk" && i + 1 < argc) walk = argv[++i];
        else if (a == "--save" && i + 1 < argc) savePath = argv[++i];   // after --walk
        else if (a == "--load" && i + 1 < argc) loadPath = argv[++i];
        else if (a == "--text" && i + 1 < argc) textId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--run-text" && i + 1 < argc) runTextId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--battle" && i + 1 < argc) battleArg = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--keys" && i + 1 < argc) keys = argv[++i];
        else if (a == "--dump-view" && i + 1 < argc) dumpView = argv[++i];
        else if (a == "--party-hp" && i + 1 < argc) partyHp = std::atoi(argv[++i]);   // test: set HP, print party
        else if (a == "--ram" && i + 1 < argc) ramPokes = argv[++i];     // ADDR:VAL,... into battle RAM
        else if (a == "--shot-at" && i + 1 < argc) shotAt = std::atoi(argv[++i]);
        else if (a == "--dump-blocks" && i + 1 < argc) dumpTiles = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--wide") wide = true;
        else if (a == "--debug") debug = true;
        else if (a == "--mute") mute = true;
        else if (a == "--wav" && i + 3 < argc) {      // --wav music|sfx ID SECONDS (writes ffl3.wav)
            wavKind = argv[++i]; wavId = int(std::strtol(argv[++i], nullptr, 16)); wavSecs = std::atof(argv[++i]);
        }
        else if (a == "--slots") showSlots = true;
        else if (a.size() > 2 && a[0] == '-' && a[1] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        else romPath = a;
    }

    Rom rom;
    std::string err;
    if (!rom.load(romPath, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FFL3",
            (err + "\n\nPut your Final Fantasy Legend III (U) ROM next to the exe as saga3.gb,\n"
                   "or drag it onto the exe.").c_str(), nullptr);
        return 1;
    }

    // ---- headless: render a song or sound effect to ffl3.wav (16-bit stereo, 48 kHz) --------
    if (!wavKind.empty()) {
        SoundSystem snd(rom, 48000);
        if (wavKind == "music") snd.music(wavId); else snd.sfx(wavId);
        std::vector<float> buf(size_t(wavSecs * 48000) * 2);
        snd.render(buf.data(), int(buf.size() / 2));
        FILE* f = std::fopen("ffl3.wav", "wb");
        if (!f) return 1;
        auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        const uint32_t bytes = uint32_t(buf.size() * 2);
        std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
        std::fwrite("data", 1, 4, f); u32(bytes);
        for (float x : buf) u16(uint16_t(int16_t(std::max(-1.f, std::min(1.f, x)) * 32767)));
        std::fclose(f);
        return 0;
    }

    GameFlags flags;
    // New game: the state the real game is in when the player first gets control (aboard the
    // Talon after the opening). Event flags captured from wC760 in the emulator; until the
    // opening script runs in the port, this is the starting point.
    static const uint8_t kStartFlags[32] = {
        0x01, 0x02, 0x80, 0x78, 0x03, 0xc8, 0xc0, 0x00, 0x40, 0x00, 0x0c, 0x91, 0x1a, 0x00, 0x00, 0x01,
        0x81, 0x00, 0x02, 0xa2, 0xa0, 0x02, 0x0f, 0x40, 0x15, 0x20, 0x02, 0x10, 0x00, 0x00, 0x03, 0xff};
    flags.loadEventBytes(kStartFlags);
    Field field(rom, flags);
    TextBox text(rom);
    text.flags = &flags;
    text.onEvent = [&](int cmd, int param) { field.runEventCommand(cmd, param); };
    text.isBusy = [&] { return field.busy(); };
    field.openText = [&](int bank, uint16_t addr) { text.open(bank, addr); };
    // Party: records 0-3 + guest (record 4, Myron) as at the start of the game.
    std::vector<CharRecord> party;
    auto newParty = [&] { party.clear(); for (int i = 0; i < 5; i++) party.push_back(loadCharacter(rom, i)); };
    newParty();
    // Sound: set up for the interactive window (no-ops headless). Requests mirror the game's
    // hFFBA (music) / hFFBC (SFX) writes.
    std::function<void(int)> sndMusic = [](int) {}, sndSfx = [](int) {};
    int curMusic = -1;
    static BattleRam battleRam;                     // the GB's RAM as battles see it, kept across fights
    initBattleRam(battleRam);
    std::unique_ptr<Battle> battle;
    BattleGfx bgfx;
    uint32_t battleSeed = 1;
    field.startBattle = [&](int hffca) {
        BattleContext ctx;
        for (int i = 0; i < 32; i++) {
            uint8_t b = 0;
            for (int k = 0; k < 8; k++) if (flags.event.test(size_t(i * 8 + k))) b |= uint8_t(0x80 >> k);
            ctx.eventFlags[size_t(i)] = b;
        }
        ctx.vehicle = field.vehicle();
        ctx.area = field.encounterArea();
        battle = std::make_unique<Battle>(rom, battleRam, party, ctx, hffca, battleSeed++ * 2654435761u);
        battle->playSfx = [&](int id) { sndSfx(id); };
        sndMusic(0);                                    // 00:10D7 stop the field music,
        sndSfx(0x36);                                   // 00:10E1 encounter sound
        curMusic = 0;                                   // battle music follows (0B:4000)
        bgfx.build(rom, *battle);
        char b[64]; std::snprintf(b, sizeof b, "battle: formation %02X%s", hffca & 0x7f, hffca & 0x80 ? " (fixed)" : "");
        field.setLastEvent(b);
    };
    // After a battle: back to the field. (Random fights you lose are retried by the battle itself.)
    auto finishBattle = [&] {
        const Battle::Result r = battle->result();
        battle.reset();
        if (r == Battle::Result::Lost) {               // story fights you can lose: carry on at 1 HP
            field.setLastEvent("party defeated");
        } else {
            field.setLastEvent(r == Battle::Result::Won ? "battle won" : "ran away");
        }
    };
    // 0B:6D63: put an item in the bag and say so (09:724E "Received X.", 09:731A when full).
    auto receive = [&](int item) {
        text.itemArg = item;
        const int n = inventoryAdd(battleRam, uint8_t(item));
        text.open(9, n >= 0 ? 0x724e : 0x731a);
        sndSfx(n < 0 ? 0x3b : item >= 0xd8 && item < 0xea ? 0x3d : 0x35);   // 0B:6D7E
    };
    field.giveItem = [&](int item) { receive(item); };
    // Save file: "FFL3SAV1", map, row, col, facing, vehicle, event + chest flags (32 bytes
    // each, MSB first), 5 party records, then the whole battle RAM image (inventory etc.).
    auto saveGame = [&](const char* path) {
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        std::fwrite("FFL3SAV1", 1, 8, f);
        const Actor& p = field.player();
        const uint8_t hdr[5] = {uint8_t(field.mapId()), uint8_t(p.row), uint8_t(p.col), uint8_t(p.facing), uint8_t(field.vehicle())};
        std::fwrite(hdr, 1, 5, f);
        for (const auto* bits : {&flags.event, &flags.chest})
            for (int i = 0; i < 32; i++) {
                uint8_t b = 0;
                for (int k = 0; k < 8; k++) if (bits->test(size_t(i * 8 + k))) b |= uint8_t(0x80 >> k);
                std::fputc(b, f);
            }
        for (int i = 0; i < 5; i++) {
            if (size_t(i) < party.size()) std::fwrite(party[size_t(i)].raw.data(), 1, 0x60, f);
            else for (int k = 0; k < 0x60; k++) std::fputc(0xff, f);
        }
        std::fwrite(battleRam.data(), 1, battleRam.size(), f);
        std::fclose(f);
        return true;
    };
    auto loadGame = [&](const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        char magic[8];
        uint8_t hdr[5], fl[64];
        std::array<uint8_t, 5 * 0x60> recs{};
        static BattleRam tmp;
        const bool ok = std::fread(magic, 1, 8, f) == 8 && !std::memcmp(magic, "FFL3SAV1", 8) &&
                        std::fread(hdr, 1, 5, f) == 5 && std::fread(fl, 1, 64, f) == 64 &&
                        std::fread(recs.data(), 1, recs.size(), f) == recs.size() &&
                        std::fread(tmp.data(), 1, tmp.size(), f) == tmp.size();
        std::fclose(f);
        if (!ok) return false;
        flags.event.reset(); flags.chest.reset();
        for (int i = 0; i < 256; i++) {
            if (fl[i / 8] & (0x80 >> (i % 8))) flags.event.set(size_t(i));
            if (fl[32 + i / 8] & (0x80 >> (i % 8))) flags.chest.set(size_t(i));
        }
        party.clear();
        for (int i = 0; i < 5; i++) {
            CharRecord c;
            std::copy(recs.begin() + i * 0x60, recs.begin() + (i + 1) * 0x60, c.raw.begin());
            if (c.raw[3] != 0xff || i < 4) party.push_back(c);
        }
        battleRam = tmp;
        battle.reset();
        text.close();
        field.setVehicle(hdr[4]);
        field.enterMap(hdr[0], hdr[1], hdr[2], Dir(hdr[3] & 3));
        return true;
    };
    std::unique_ptr<Shop> shop;
    auto openShop = [&](int id) {
        shop = std::make_unique<Shop>(rom, battleRam, party, id);
        shop->playSfx = [&](int sid) { sndSfx(sid); };
    };
    field.playSfx = [&](int id) { sndSfx(id); };
    auto poke = [&](uint16_t a, uint8_t v) {
        battleRam[a] = v;
        if (a >= 0xc500 && a < 0xc6e0 && size_t(a - 0xc500) / 0x60 < party.size())   // party records
            party[size_t(a - 0xc500) / 0x60].raw[size_t(a - 0xc500) % 0x60] = v;
    };
    field.pokeRam = poke;
    // 0B:6DE4: character `who` (07:7A81 + 0B:7573[who] * $60) into party slot `slot`;
    // slot 3 with who 6 = the guest (record 4) stays for good.
    field.joinParty = [&](int slot, int who) {
        if (slot < 0 || slot > 4) return;
        if (slot == 3 && who == 6) {
            party[3] = party[4];
            party[3].raw[3] = rom.b(7, 0x7ba4);
        } else {
            party[size_t(slot)] = loadCharacter(rom, rom.b(0x0b, uint16_t(0x7573 + who)));
        }
        for (int i = 0; i < 4; i++) battleRam[size_t(0xce00 + slot * 4 + i)] = 0;
    };
    field.leaveParty = [&](int slot) { party[size_t(slot)].raw[3] = 0xff; };   // 0B:6E33: +3 = $FF
    field.openShop = openShop;
    std::unique_ptr<FieldMenu> menu;
    auto openMenu = [&] {
        menu = std::make_unique<FieldMenu>(rom, battleRam, party, [&] { return saveGame("ffl3.sav"); });
        menu->playSfx = [&](int id) { sndSfx(id); };
    };
    auto chest = [&](int slot, const MapObject& o) {           // 00:09D7
        const int k = o.id - 1;
        if (o.id == 0 || k >= 0x80 || flags.chest.test(size_t(k))) { text.open(1, 0x611d); return; }   // "Empty"
        receive(rom.b(3, uint16_t(0x762a + k)));
        flags.chest.set(size_t(k));
        field.openChest(slot);
    };
    text.log = [&](const std::string& m) { field.setLastEvent(m); };
    if (!loadPath.empty()) {
        if (!loadGame(loadPath.c_str())) { std::fprintf(stderr, "can't load %s\n", loadPath.c_str()); return 1; }
    } else if (mapId < 0) {                                  // new game
        field.enterMap(0x0b, startRow >= 0 ? startRow : 26, startCol >= 0 ? startCol : 7, Dir::Up);
    } else {
        if (mapId < 6) field.setVehicle(2);            // debug jump straight onto a world map
        if (!field.enterMap(mapId, startRow, startCol)) field.enterMap(nextValid(rom, mapId, 1));
    }
    TileCache tiles;
    tiles.build(rom, field);
    RenderOpts opts;
    Frame frame;
    frame.resize(wide ? 256 : 160, 144);
    int pal = 0;
    std::vector<uint32_t> argb;
    std::vector<uint8_t> tint;
    auto refreshTiles = [&] { if (tiles.map != field.mapId()) tiles.build(rom, field); };

    // --ram ADDR:VAL,...: poke battle RAM (inventory etc.) and party records, for tests
    for (size_t p = 0; p < ramPokes.size();) {
        unsigned a, v;
        if (std::sscanf(ramPokes.c_str() + p, "%x:%x", &a, &v) == 2) {
            battleRam[a & 0xffff] = uint8_t(v);
            if (a >= 0xc500 && a < 0xc6e0 && (a - 0xc500) / 0x60 < party.size())   // party records
                party[(a - 0xc500) / 0x60].raw[(a - 0xc500) % 0x60] = uint8_t(v);
        }
        const size_t c = ramPokes.find(',', p);
        if (c == std::string::npos) break;
        p = c + 1;
    }

    // ---- headless: a battle. --battle HH (hFFCA: formation | $80 fixed counts), --keys script:
    //   A B U D L R = press (then 4 frames), W = 30 frames, * = mash A until the battle ends.
    //   --shot out.bmp saves the screen when the script ends (or at frame --shot-at N).
    if (battleArg >= 0) {
        field.startBattle(battleArg);
        int frameNo = 0;
        bool shotDone = false;
        auto snap = [&] {
            renderBattle(*battle, bgfx, tiles, opts, frame);
            toArgb(frame, kPalettes[pal], nullptr, argb);
            saveBmp(argb, frame.w, frame.h, shot.c_str());
            shotDone = true;
        };
        auto run = [&](BattleInput in) {
            if (!battle || battle->finished()) return;
            battle->tick(in);
            if (++frameNo == shotAt && !shot.empty()) snap();
        };
        const Battle& B = *battle;
        std::printf("formation %02X: ", B.formation().index);
        for (int g = 0; g < 3; g++) {
            if (B.formation().ids[g] == 0xff) continue;
            const Monster& m = B.monsters()[size_t(g)];
            std::printf("%s%02X x%d (HP %d, class %d %dx%d)", g ? ", " : "", m.id, B.layout().count[g], m.hp,
                        m.sizeClass, m.picW, m.picH);
        }
        std::printf("\nslots:");
        for (const EnemySlot& e : B.layout().slots) std::printf(" g%d@%d,%d", e.group, e.col, e.row);
        std::printf("\n");
        for (char c : keys) {
            BattleInput in;
            switch (c) {
            case 'A': in.a = in.aHeld = true; break;
            case 'B': in.b = true; break;
            case 'U': in.up = true; break;
            case 'D': in.down = true; break;
            case 'L': in.left = true; break;
            case 'R': in.right = true; break;
            case 'W': for (int i = 0; i < 30; i++) run({}); continue;
            case '*':
                for (int i = 0; i < 60 * 600 && battle && !battle->finished(); i++) {
                    BattleInput m; m.a = m.aHeld = (i % 8) == 0; run(m);
                }
                continue;
            default: continue;
            }
            run(in);
            for (int i = 0; i < 4; i++) run({});
        }
        if (!shot.empty() && !shotDone) snap();
        if (!dumpView.empty()) {                  // WY, 20x10 window tiles, then each group's tiles
            FILE* f = std::fopen(dumpView.c_str(), "wb");
            if (!f) return 1;
            const BattleView v = B.view();
            std::fputc(v.wy, f);
            std::fwrite(v.win.data(), 1, v.win.size(), f);
            for (int g = 0; g < 3; g++) {
                const auto& t = B.monsters()[size_t(g)].tiles;
                std::fputc(int(t.size()), f);
                for (const Tile& x : t) std::fwrite(x.data(), 1, 64, f);
            }
            std::fclose(f);
        }
        for (uint16_t n : B.unknownNatives) std::printf("unhandled native %04X\n", n);
        for (const std::string& l : B.log) std::printf("| %s\n", l.c_str());
        const char* res[] = {"none", "won", "lost", "ran"};
        std::printf("result: %s, frames %d, party HP:", res[int(B.result())], frameNo);
        if (B.finished()) for (const CharRecord& c : party) std::printf(" %d", c.hp());
        std::printf("\n");
        return 0;
    }

    // ---- headless: play NPC text `textId`, print the window at every wait -----------------
    if (textId >= 0) {
        auto ascii = [](uint8_t t) -> char {
            if (t >= 0xba && t <= 0xd3) return char('A' + t - 0xba);
            if (t >= 0xd4 && t <= 0xed) return char('a' + t - 0xd4);
            if (t >= 0xb0 && t <= 0xb9) return char('0' + t - 0xb0);
            switch (t) { case 0xff: return ' '; case 0xaf: case 0xfe: return '.'; case 0xac: return ',';
                         case 0xee: case 0xef: return '"'; case 0xf0: return ':'; case 0xf4: return '?';
                         case 0xf3: return '!'; case 0xf2: return '-'; case 0xad: return '\''; default: return '#'; }
        };
        text.open(0xa, rom.ptr(0xa, 0x4004, textId - 1));
        for (int page = 0; page < 60 && text.active(); page++) {
            for (int f = 0; f < 4000 && text.active() && !text.waitingForA() && !text.choosing(); f++)
                text.tick(false, false, false, false);
            std::printf("--- page %d%s\n", page, text.choosing() ? " (choice)" : "");
            for (const auto& l : text.lines()) {
                std::string s;
                for (uint8_t t : l) s += ascii(t);
                std::printf("|%s|\n", s.c_str());
            }
            text.tick(true, true, false, false);
        }
        std::printf("=== history\n");
        for (const auto& l : text.history) {
            std::string s;
            for (uint8_t t : l) s += ascii(t);
            std::printf("#%s#\n", s.c_str());
        }
        std::printf("[%s]\n", field.lastEvent().c_str());
        return 0;
    }

    // ---- headless: run NPC text `runTextId` in the field (events + actor moves), auto-A ----
    if (runTextId >= 0) {
        text.open(0xa, rom.ptr(0xa, 0x4004, runTextId - 1));
        for (int f = 0; f < 60 * 120 && (text.active() || field.busy()); f++) {
            field.tick(-1);
            text.tick(f % 20 == 0, false, false, false);
        }
        std::printf("leader %d,%d facing %d\n", field.player().row, field.player().col, int(field.player().facing));
        for (int i = 1; i < Field::kSlots; i++) {
            const Actor& a = field.actors()[size_t(i)];
            if (a.used) std::printf("slot %d id %02X pos %d,%d facing %d\n", i, a.id, a.row, a.col, int(a.facing));
        }
        std::printf("[%s]\n", field.lastEvent().c_str());
        return 0;
    }

    // ---- headless: tests / tooling ----------------------------------------------------
    if (!dumpTiles.empty() || !shot.empty() || !walk.empty() || !savePath.empty()) {
        if (partyHp >= 0) for (auto& r : party) r.setHp(partyHp);
        if (!dumpTiles.empty()) {
            FILE* f = std::fopen(dumpTiles.c_str(), "wb");
            if (!f) return 1;
            std::fwrite(field.layout().blocks.data(), 1, field.layout().blocks.size(), f);
            std::fclose(f);
        }
        // Script: U/D/L/R step, A = press A (talk / advance text) then 40 frames, W = 60 frames.
        for (char c : walk) {
            if (c == 'M' && !menu) { openMenu(); continue; }
            if (menu) {                                 // A / B / U / D drive the menu
                MenuInput in;
                in.a = c == 'A'; in.b = c == 'B'; in.up = c == 'U'; in.down = c == 'D';
                menu->tick(in);
                if (menu->done()) menu.reset();
                continue;
            }
            if (shop) {                                 // A / B / U / D drive an open shop
                ShopInput in;
                in.a = c == 'A'; in.b = c == 'B'; in.up = c == 'U'; in.down = c == 'D';
                shop->tick(in);
                if (shop->done()) shop.reset();
                continue;
            }
            if (c == 'A' || c == 'a') {
                if (text.active()) text.tick(true, true, false, false);
                else talk(rom, field, text, chest, openShop);
                for (int i = 0; i < 40; i++) { field.tick(-1); text.tick(false, false, false, false); }
                continue;
            }
            if (c == 'W' || c == 'w') {
                for (int i = 0; i < 60; i++) { field.tick(-1); text.tick(false, false, false, false); }
                continue;
            }
            const int d = dirFromChar(c);
            if (d < 0 || text.active()) continue;
            field.tick(d);
            for (int i = 0; i < 16 && field.player().moveFrames; i++) field.tick(-1);
            refreshTiles();
            if (field.lastEvent().rfind("exit", 0) == 0 || field.lastEvent().rfind("event", 0) == 0 ||
                field.lastEvent().rfind("battle", 0) == 0)
                std::printf("  step %c: %s\n", c, field.lastEvent().c_str()), field.setLastEvent("");
        }
        if (!savePath.empty() && !saveGame(savePath.c_str())) return 1;
        if (shop) std::printf("shop open, gold %d\n", shop->gold());
        if (partyHp >= 0) {
            std::printf("party:");
            for (auto& r : party) std::printf(" %d/%d,%d/%d,s%02X", r.hp(), r.maxHp(), r.mp(), r.maxMp(), r.raw[1]);
            std::printf("\n");
        }
        if (!shot.empty()) {
            renderField(field, tiles, opts, frame, debug ? &tint : nullptr);
            renderTextBox(text, tiles, opts, frame);
            if (shop) renderShop(*shop, tiles, opts, frame);
            if (menu) renderMenu(*menu, tiles, opts, frame);
            toArgb(frame, kPalettes[pal], debug ? &tint : nullptr, argb);
            if (!saveBmp(argb, frame.w, frame.h, shot.c_str())) return 1;
        }
        if (showSlots) {
            std::printf("slots:");
            for (int i = 0; i < Field::kSlots; i++) {
                const Actor& a = field.actors()[size_t(i)];
                if (a.used) std::printf(" %d:%02X%s%s", i, a.savedId, a.id ? "" : "(hidden)", a.proxy ? "(proxy)" : "");
            }
            std::printf("\n");
        }
        std::printf("map %02X layout %02X gfx %02X sprites %02X  %dx%d  player %d,%d  npcs %zu  [%s]\n",
                    field.mapId(), field.header().layout, field.header().gfxset, field.header().unk1e,
                    field.layout().w, field.layout().h, field.player().row, field.player().col,
                    size_t(std::count_if(field.actors().begin() + 1, field.actors().end(),
                                         [](const Actor& a) { return a.drawn(); })),
                    field.lastEvent().c_str());
        return 0;
    }

    // ---- interactive ------------------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    // Audio: the sound driver + APU run in the SDL audio callback, one driver frame per GB frame.
    static std::unique_ptr<SoundSystem> sound;
    SDL_AudioDeviceID adev = 0;
    if (!mute && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        sound = std::make_unique<SoundSystem>(rom, 48000);
        SDL_AudioSpec want{}, have{};
        want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
        want.callback = [](void* u, Uint8* buf, int len) { static_cast<SoundSystem*>(u)->render(reinterpret_cast<float*>(buf), len / 8); };
        want.userdata = sound.get();
        adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (adev) {
            sndMusic = [&](int id) { SDL_LockAudioDevice(adev); sound->music(id); SDL_UnlockAudioDevice(adev); };
            sndSfx = [&](int id) { SDL_LockAudioDevice(adev); sound->sfx(id); SDL_UnlockAudioDevice(adev); };
            SDL_PauseAudioDevice(adev, 0);
        } else {
            std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* win = SDL_CreateWindow("FFL3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       frame.w * scale, frame.h * scale, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = nullptr;
    int texW = 0, shotN = 0;
    std::string lastTitle;
    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    bool aPrev = true, upPrev = true, downPrev = true, bPrev = true, leftPrev = true, rightPrev = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type != SDL_KEYDOWN) continue;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_PAGEDOWN: case SDLK_RIGHTBRACKET: case SDLK_PAGEUP: case SDLK_LEFTBRACKET: {
                const bool fwd = e.key.keysym.sym == SDLK_PAGEDOWN || e.key.keysym.sym == SDLK_RIGHTBRACKET;
                const int m = nextValid(rom, field.mapId(), fwd ? 1 : -1);
                field.setVehicle(m < 6 ? 2 : 0);
                field.enterMap(m);
                break;
            }
            case SDLK_F1: debug = !debug; break;
            case SDLK_F2: field.noClip = !field.noClip; break;
            case SDLK_m: case SDLK_RSHIFT: case SDLK_LSHIFT:
                if (!battle && !shop && !menu && !text.active()) openMenu();
                break;
            case SDLK_F5: field.setLastEvent(saveGame("ffl3.sav") ? "saved to ffl3.sav" : "save failed"); break;
            case SDLK_F9: field.setLastEvent(loadGame("ffl3.sav") ? "loaded ffl3.sav" : "no save (ffl3.sav)"); break;
            case SDLK_F3: field.encounters = !field.encounters;
                field.setLastEvent(field.encounters ? "random battles on" : "random battles off"); break;
            case SDLK_F4: if (!battle) field.startBattle(randomFormation(rom, field.encounterArea(), field.vehicle(), field.random8())); break;
            case SDLK_TAB: wide = !wide; frame.resize(wide ? 256 : 160, 144); break;
            case SDLK_p: pal = (pal + 1) % kNumPalettes; break;
            case SDLK_F12: {
                char name[64];
                std::snprintf(name, sizeof name, "ffl3_%02X_%d.bmp", field.mapId(), shotN++);
                saveBmp(argb, frame.w, frame.h, name);
                break;
            }
            default: break;
            }
        }
        // Fixed 60 Hz game logic, like the GB.
        const Uint64 now = SDL_GetPerformanceCounter();
        acc += double(now - last) / double(SDL_GetPerformanceFrequency());
        last = now;
        if (acc > 0.25) acc = 0.25;
        while (acc >= 1.0 / 60.0) {
            acc -= 1.0 / 60.0;
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            int d = -1;
            if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) d = int(Dir::Down);
            else if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) d = int(Dir::Up);
            else if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) d = int(Dir::Left);
            else if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d = int(Dir::Right);
            const bool aHeld = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN];
            const bool up = d == int(Dir::Up), down = d == int(Dir::Down);
            const bool aPressed = aHeld && !aPrev;
            const bool bHeld = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_BACKSPACE];
            const bool left = d == int(Dir::Left), right = d == int(Dir::Right);
            if (menu) {
                MenuInput in;
                in.a = aPressed; in.b = bHeld && !bPrev; in.up = up && !upPrev; in.down = down && !downPrev;
                in.left = left && !leftPrev; in.right = right && !rightPrev;
                menu->tick(in);
                if (menu->done()) menu.reset();
            } else if (shop) {
                ShopInput in;
                in.a = aPressed; in.b = bHeld && !bPrev; in.up = up && !upPrev; in.down = down && !downPrev;
                shop->tick(in);
                if (shop->done()) shop.reset();
            } else if (battle) {
                BattleInput in;
                in.a = aPressed; in.aHeld = aHeld; in.b = bHeld && !bPrev;
                in.up = up && !upPrev; in.down = down && !downPrev;
                in.left = left && !leftPrev; in.right = right && !rightPrev;
                battle->tick(in);
                if (battle->finished()) finishBattle();
            } else if (text.active()) {
                field.tick(-1);                      // scripted actor moves animate under text
                text.tick(aPressed, aHeld, up && !upPrev, down && !downPrev);
            } else {
                if (aPressed && !field.player().moveFrames) talk(rom, field, text, chest, openShop);
                else field.tick(d);
            }
            const int wantMusic = battle ? battle->formation().music() : field.music;   // 0B:4000 / map header
            if (wantMusic != curMusic) { curMusic = wantMusic; sndMusic(wantMusic); }
            aPrev = aHeld; upPrev = up; downPrev = down; bPrev = bHeld; leftPrev = left; rightPrev = right;
        }
        refreshTiles();
        if (battle) {
            renderBattle(*battle, bgfx, tiles, opts, frame);
            toArgb(frame, kPalettes[pal], nullptr, argb);
        } else {
            renderField(field, tiles, opts, frame, debug ? &tint : nullptr);
            renderTextBox(text, tiles, opts, frame);
            if (shop) renderShop(*shop, tiles, opts, frame);
            if (menu) renderMenu(*menu, tiles, opts, frame);
            toArgb(frame, kPalettes[pal], debug ? &tint : nullptr, argb);
        }
        if (!tex || texW != frame.w) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, frame.w, frame.h);
            texW = frame.w;
            SDL_RenderSetLogicalSize(ren, frame.w, frame.h);
            SDL_RenderSetIntegerScale(ren, SDL_TRUE);
        }
        SDL_UpdateTexture(tex, nullptr, argb.data(), frame.w * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        char title[200];
        std::snprintf(title, sizeof title, "FFL3 - map %02X (%d,%d)%s%s - %s", field.mapId(),
                      field.player().row, field.player().col, field.noClip ? " [noclip]" : "",
                      debug ? " [collision]" : "", field.lastEvent().c_str());
        if (lastTitle != title) { SDL_SetWindowTitle(win, title); lastTitle = title; }
    }
    if (adev) SDL_CloseAudioDevice(adev);
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
