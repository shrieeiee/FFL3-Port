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
#include "console.h"
#include "opening.h"
#include "bugreport.h"
#include "avi.h"
#include <filesystem>
#include <ctime>
#include <csignal>
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

ffl3::AudioTap* g_tap = nullptr;                        // sound for the video recorder (audio thread)

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
    int runScript = -1;
    int textId = -1, runTextId = -1, battleArg = -1, shotAt = -1;
    std::string keys, dumpView, ramPokes;
    int mapId = -1, scale = 5, startRow = -1, startCol = -1;
    bool wide = false, debug = false, showSlots = false, mute = false;
    std::string wavKind;
    int partyHp = -1;
    bool wanderOpt = false, noTitle = false, openingTest = false, titleShots = false;
    int shopOpt = -1;
    std::vector<int> setFlags;
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
        else if (a == "--run-script" && i + 1 < argc) runScript = int(std::strtol(argv[++i], nullptr, 16));   // BAAAA: bank B, address AAAA
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
        else if (a == "--wander") wanderOpt = true;
        else if (a == "--no-title") noTitle = true;
        else if (a == "--opening") openingTest = true;
        else if (a == "--title-shots") titleShots = true;   // test: title.bmp, naming0.bmp   // test: play the opening headless (auto A, default names)
        else if (a == "--shop" && i + 1 < argc) shopOpt = int(std::strtol(argv[++i], nullptr, 16));   // test: open shop
        else if (a == "--flag" && i + 1 < argc) setFlags.push_back(int(std::strtol(argv[++i], nullptr, 16)));   // test: set event flag
        else if (a == "--wav" && i + 3 < argc) {      // --wav music|sfx ID SECONDS (writes ffl3.wav)
            wavKind = argv[++i]; wavId = int(std::strtol(argv[++i], nullptr, 16)); wavSecs = std::atof(argv[++i]);
        }
        else if (a == "--slots") showSlots = true;
        else if (a.size() > 2 && a[0] == '-' && a[1] == '-') { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        else romPath = a;
    }

    std::string exeDir = ".";
    if (char* bp = SDL_GetBasePath()) { exeDir = bp; SDL_free(bp); }
    {                                                   // double-clicked: look next to the exe
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(romPath, ec)) {
            if (fs::exists(fs::path(exeDir) / romPath, ec)) romPath = (fs::path(exeDir) / romPath).string();
            else
                for (const auto& de : fs::directory_iterator(exeDir, ec))
                    if (de.path().extension() == ".gb" || de.path().extension() == ".GB") { romPath = de.path().string(); break; }
        }
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
        if (const char* a = std::getenv("FFL3_AREA")) ctx.area = std::atoi(a);   // debug
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
    bool gameOver = false;                              // "Fight again?" No: back to the title (00:0200)
    auto finishBattle = [&] {
        const Battle::Result r = battle->result();
        battle.reset();
        if (r == Battle::Result::GameOver) { gameOver = true; field.setLastEvent("game over"); return; }
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
        text.call(9, n >= 0 ? 0x724e : 0x731a);
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
        const Field::Spot sp = field.savedSpot();               // wC6E9-C6ED: where the Talon is
        const int sx = (sp.row - 5) * 16, sy = (sp.col - 5) * 16;
        battleRam[0xc6e9] = uint8_t(sx); battleRam[0xc6ea] = uint8_t(sx >> 8);
        battleRam[0xc6eb] = uint8_t(sy); battleRam[0xc6ec] = uint8_t(sy >> 8);
        battleRam[0xc6ed] = uint8_t(sp.map);
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
        field.setSavedSpot({battleRam[0xc6ed], (battleRam[0xc6e9] | battleRam[0xc6ea] << 8) / 16 + 5,
                            (battleRam[0xc6eb] | battleRam[0xc6ec] << 8) / 16 + 5});
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
    field.healParty = [&] {                    // 01:7701: living members back to full HP / MP
        const int n = party[4].raw[3] == 0xff ? 4 : 5;
        for (int i = 0; i < n; i++)
            if (!(party[size_t(i)].raw[1] & 0x40)) { party[size_t(i)].setMp(party[size_t(i)].maxMp()); party[size_t(i)].setHp(party[size_t(i)].maxHp()); }
    };
    field.hurtParty = [&] {
        for (int i = 0; i < 5; i++) {
            const int hp = party[size_t(i)].hp() - 2;
            party[size_t(i)].setHp(hp <= 0 ? 1 : hp);
        }
    };
    field.cureParty = [&] { for (int i = 0; i < 5; i++) party[size_t(i)].raw[1] = 0; };
    field.leaveParty = [&](int slot) { party[size_t(slot)].raw[3] = 0xff; };   // 0B:6E33: +3 = $FF
    field.openShop = openShop;
    std::unique_ptr<FieldMenu> menu;
    std::unique_ptr<TalonConsole> console;
    text.mustWait = [&] { return bool(battle) || bool(shop) || bool(console); };
    field.talonConsole = [&] {
        console = std::make_unique<TalonConsole>(rom, battleRam, flags);
        console->playSfx = [&](int id) { sndSfx(id); };
    };
    auto consoleDone = [&] {
        if (console->result() == 1) { text.close(); field.takeOff(console->takeOffMode()); }
        console.reset();
    };
    auto openMenu = [&] {
        static const char* files[3] = {"ffl3.sav", "ffl3-2.sav", "ffl3-3.sav"};
        menu = std::make_unique<FieldMenu>(rom, battleRam, party,
            [&](int slot) { return saveGame(files[slot]); },
            [](int slot, CharRecord& leader) {               // the file's first party record
                FILE* f = std::fopen(files[slot], "rb");
                if (!f) return false;
                const bool ok = !std::fseek(f, 8 + 5 + 64, SEEK_SET) && std::fread(leader.raw.data(), 1, 0x60, f) == 0x60;
                std::fclose(f);
                return ok;
            });
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
    text.onEvent = [&](int cmd, int param) {
        field.runEventCommand(cmd, param);
        if (std::getenv("FFL3_PARTYLOG") && (cmd == 0x10 || cmd == 0x1b || cmd == 0x22))
            for (size_t i = 0; i < party.size(); i++) std::fprintf(stderr, "  ev %02X %02X slot %zu: %02x %02x b3=%02x hp=%d\n", cmd, param, i, party[i].raw[0], party[i].raw[0x0d], party[i].raw[3], party[i].hp());
    };

    text.memberName = [&](int m) {                     // 00:2955: record +7, 6 glyphs ($4C = blank)
        std::vector<uint8_t> n;
        if (size_t(m) >= party.size()) return n;
        for (int i = 0; i < 6; i++) { const uint8_t g = party[size_t(m)].raw[size_t(7 + i)]; n.push_back(g == 0x4c ? 0xff : g); }
        while (!n.empty() && n.back() == 0xff) n.pop_back();
        return n;
    };
    text.classLabel = [&] { return classLabel(rom, party[4]); };
    // ---- the opening (01:7C79 / 7D40) --------------------------------------------------
    // 0 = playing, 1 = title, 2 = story crawl, 3 = naming
    int opening = 0, namingMember = 0;
    bool prologue = false;                                // 09:6015 running (00:023F)
    std::unique_ptr<TitleScreen> title;
    std::unique_ptr<NamingScreen> naming;
    auto startNewGame = [&] {
        party.clear();                                    // 7D40: records 07:7A81 x 5
        for (int i = 0; i < 5; i++) party.push_back(loadCharacter(rom, i));
        initBattleRam(battleRam);
        for (int i = 0; i < 0x60; i++) battleRam[size_t(0xc6e0 + i)] = rom.b(7, uint16_t(0x7e41 + i));   // wC6E0 block
        uint8_t fl[32];
        for (int i = 0; i < 32; i++) fl[i] = rom.b(1, uint16_t(0x7ddd + i));                            // wC760 flags
        flags.loadEventBytes(fl);
        flags.chest.reset();
        field.setSavedSpot({1, 21, 21});
        field.setVehicle(0);
        text.open(9, 0x5ead);                             // the story crawl (warps to map 5 itself)
        opening = 2;
    };
    auto startNaming = [&](int m) {
        namingMember = m;
        naming = std::make_unique<NamingScreen>(rom, m);
        naming->playSfx = [&](int id) { sndSfx(id); };
        opening = 3;
    };
    auto namingDone = [&] {                               // 7D97: members 0-3, B goes back one
        if (naming->back()) { startNaming(std::max(0, namingMember - 1)); return; }
        for (int i = 0; i < 6; i++) party[size_t(namingMember)].raw[size_t(7 + i)] = naming->name()[size_t(i)];
        if (namingMember < 3) { startNaming(namingMember + 1); return; }
        naming.reset();
        party[4] = party[3];                              // 7DB3: wC620 -> wC680
        field.joinParty(3, 2);                            // 0B:6DE4 (a = 3, b = 2)
        field.leaveParty(4);                              // 0B:6E33 (a = 4)
        text.open(9, 0x6015);                             // the prologue (00:0250)
        prologue = true;
        opening = 0;
    };
    // 00:0255: once the prologue text is over, flag $31 = prologue done (random battles on, and
    // a reset skips the opening). Control starts on the Talon's map at (26,7) facing up - the
    // spot the real game hands over at.
    auto finishPrologue = [&] {
        prologue = false;
        flags.event.set(0x31);
        if (field.mapId() == 0x0b) field.enterMap(0x0b, 26, 7, Dir::Up);
    };

    // New game: the Talon is parked on world map 1 at scroll ($100, $100) (wC6E9-C6ED from RAM);
    // wC6F8-C6FB: part $D8 attached, no engine chosen.
    field.setSavedSpot({1, 21, 21});
    for (int f : setFlags) flags.event.set(size_t(f & 0xff));
    battleRam[0xc6f8] = 0x80; battleRam[0xc6f9] = 0; battleRam[0xc6fa] = 0xff;
    if (!loadPath.empty()) {
        if (!loadGame(loadPath.c_str())) { std::fprintf(stderr, "can't load %s\n", loadPath.c_str()); return 1; }
    } else if (mapId < 0) {                                  // new game
        field.enterMap(0x0b, startRow >= 0 ? startRow : 26, startCol >= 0 ? startCol : 7, Dir::Up);
        if (!noTitle && startRow < 0) { title = std::make_unique<TitleScreen>(rom); opening = 1; }
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
    int tilesVersion = -1;
    uint8_t hff93 = 0;                                       // frame counter (00:11CE)
    auto refreshTiles = [&] {
        if (tiles.map != field.mapId() || tilesVersion != field.blocksVersion()) {
            tiles.build(rom, field);
            tilesVersion = field.blocksVersion();
        }
    };

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
            if (const char* d = std::getenv("FFL3_OAMLOG")) {   // debug / tests: each frame's OAM
                static FILE* lf = std::fopen(d, "wb");
                if (lf) { const BattleView v = battle->view(); std::fwrite(v.oam.data(), 1, 160, lf); std::fflush(lf); }
            }
            if (const char* d = std::getenv("FFL3_FRAMES")) {   // debug: every frame to DIR/fNNNNN.bmp
                const std::string keep = shot;
                char nm[64]; std::snprintf(nm, sizeof nm, "/f%05d.bmp", frameNo);
                shot = std::string(d) + nm; snap(); shot = keep; shotDone = false;
            }
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
            std::fwrite(v.oam.data(), 1, v.oam.size(), f);   // then the OAM and OBJ tiles $8000-$8FFF
            std::fwrite(v.vram, 1, 0x1000, f);
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

    if (titleShots) {
        TileCache tc;
        tc.build(rom, field);
        RenderOpts o;
        Frame fr;
        fr.resize(160, 144);
        std::vector<uint32_t> px;
        TitleScreen t(rom);
        renderTitle(t, tc, o, fr);
        toArgb(fr, kPalettes[0], nullptr, px);
        saveBmp(px, 160, 144, "title.bmp");
        for (int m = 0; m < 4; m++) {
            NamingScreen n(rom, m);
            if (m == 0) for (int i = 0; i < 6; i++) { OpeningInput in; in.a = true; n.tick(in); }
            renderNaming(n, tc, o, fr);
            toArgb(fr, kPalettes[0], nullptr, px);
            saveBmp(px, 160, 144, ("naming" + std::to_string(m) + ".bmp").c_str());
        }
        return 0;
    }

    // ---- headless: the whole opening, auto A, default names; prints the state at the end ----
    if (openingTest) {
        startNewGame();
        auto run = [&](int maxFrames) {
            for (int f = 0; f < maxFrames && (text.active() || field.busy() || battle); f++) {
                if (battle) {
                    BattleInput in;
                    in.a = f % 6 == 0; in.aHeld = true;
                    battle->tick(in);
                    if (battle->finished()) { std::printf("battle: %s\n", battle->result() == Battle::Result::Won ? "won" : "not won"); finishBattle(); }
                    continue;
                }
                field.tick(-1);
                text.tick(f % 20 == 0, false, false, false);
            }
        };
        if (!shot.empty()) {                              // --shot FILE --shot-at N: the crawl at frame N
            for (int f = 0; f < shotAt && text.active(); f++) { field.tick(-1); text.tick(false, false, false, false); }
            TileCache tc; tc.build(rom, field);
            RenderOpts o; Frame fr; fr.resize(160, 144);
            std::vector<uint32_t> px;
            renderField(field, tc, o, fr, nullptr);
            renderTextBox(text, tc, o, fr);
            toArgb(fr, kPalettes[0], nullptr, px);
            saveBmp(px, 160, 144, shot.c_str());
            return 0;
        }
        run(60 * 600);
        std::printf("crawl done: map %02X\n", field.mapId());
        startNaming(0);
        for (int m = 0; m < 4 && naming; m++) { OpeningInput in; in.start = true; naming->tick(in); namingDone(); }
        run(60 * 3000);
        if (prologue && !text.active()) finishPrologue();
        const Actor& p = field.player();
        std::printf("end: map %02X at %d,%d facing %d vehicle %d text %d\n", field.mapId(), p.row, p.col, int(p.facing), field.vehicle(), int(text.active()));
        std::printf("gold %d\nbag:", battleRam[0xc6e0] | battleRam[0xc6e1] << 8 | battleRam[0xc6e2] << 16);
        for (int i = 0; i < 24; i++) if (battleRam[size_t(0xc6ff + i * 2)] != 0xff) std::printf(" %02X x%d", battleRam[size_t(0xc6ff + i * 2)], battleRam[size_t(0xc700 + i * 2)]);
        std::printf("\nflags:");
        for (int i = 0; i < 32; i++) {
            uint8_t b = 0;
            for (int k = 0; k < 8; k++) if (flags.event.test(size_t(i * 8 + k))) b |= uint8_t(0x80 >> k);
            std::printf(" %02x", b);
        }
        std::printf("\n");
        for (size_t i = 0; i < party.size(); i++) {
            std::printf("record %zu:", i);
            for (int k = 0; k < 0x60; k++) std::printf("%02x", party[i].raw[size_t(k)]);
            std::printf("\n");
        }
        return 0;
    }

    // ---- headless: run NPC text `runTextId` in the field (events + actor moves), auto-A ----
    if (runTextId >= 0 || runScript >= 0) {
        if (runScript >= 0) text.open(runScript >> 16, uint16_t(runScript));
        else text.open(0xa, rom.ptr(0xa, 0x4004, runTextId - 1));
        for (int f = 0; f < 60 * 600 && (text.active() || field.busy() || battle); f++) {
            if (battle) { std::printf("(battle %s skipped)\n", field.lastEvent().c_str()); battle.reset(); }
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
        field.wander = wanderOpt;
        if (shopOpt >= 0) openShop(shopOpt);
        if (!dumpTiles.empty()) {
            FILE* f = std::fopen(dumpTiles.c_str(), "wb");
            if (!f) return 1;
            std::fwrite(field.layout().blocks.data(), 1, field.layout().blocks.size(), f);
            std::fclose(f);
        }
        // Script: U/D/L/R step, A = press A (talk / advance text) then 40 frames, W = 60 frames.
        for (char c : walk) {
            if (c == 'M' && !menu) { openMenu(); continue; }
            if (console) {                              // A / B / U / D drive the Talon cockpit
                ConsoleInput in;
                in.a = c == 'A'; in.b = c == 'B'; in.up = c == 'U'; in.down = c == 'D';
                console->tick(in);
                if (console->done()) consoleDone();
                continue;
            }
            if (c == 'B' && !text.active() && field.land()) continue;   // land the Talon
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
                in.left = c == 'L'; in.right = c == 'R';
                shop->tick(in);
                if (shop->done()) shop.reset();
                continue;
            }
            if (c == 'A' || c == 'a') {
                if (text.active()) text.tick(true, true, false, false);
                else if (!field.boardTalon()) talk(rom, field, text, chest, openShop);
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
        if (const char* vd = std::getenv("FFL3_VRAMDUMP")) {     // tests: VRAM $8000-$97FF after N hook frames
            refreshTiles();
            const int n = std::getenv("FFL3_HOOKFRAMES") ? std::atoi(std::getenv("FFL3_HOOKFRAMES")) : 0;
            for (int i = 0; i < n; i++) tiles.animate(field.header().hook, uint8_t(i));
            if (FILE* f = std::fopen(vd, "wb")) { std::fwrite(tiles.vram.data(), 1, tiles.vram.size(), f); std::fclose(f); }
        }
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
            if (console) renderConsole(*console, tiles, opts, frame);
            toArgb(frame, kPalettes[pal], debug ? &tint : nullptr, argb);
            if (!saveBmp(argb, frame.w, frame.h, shot.c_str())) return 1;
        }
        if (showSlots) {
            std::printf("slots:");
            for (int i = 0; i < Field::kSlots; i++) {
                const Actor& a = field.actors()[size_t(i)];
                if (a.used && wanderOpt) std::printf(" %d:%02X@%d,%d", i, a.savedId, a.row, a.col);
                else if (a.used) std::printf(" %d:%02X%s%s", i, a.savedId, a.id ? "" : "(hidden)", a.proxy ? "(proxy)" : "");
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
    field.wander = true;
    // Audio: the sound driver + APU run in the SDL audio callback, one driver frame per GB frame.
    static std::unique_ptr<SoundSystem> sound;
    SDL_AudioDeviceID adev = 0;
    if (!mute && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        sound = std::make_unique<SoundSystem>(rom, 48000);
        SDL_AudioSpec want{}, have{};
        want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
        want.callback = [](void* u, Uint8* buf, int len) {
            static_cast<SoundSystem*>(u)->render(reinterpret_cast<float*>(buf), len / 8);
            if (g_tap) g_tap->push(reinterpret_cast<const float*>(buf), len / 8);
        };
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
#ifndef FFL3_VERSION
#define FFL3_VERSION "dev"
#endif
    // ---- tester support: input / event log, F12 bug reports, crash reports, F10 commands ----
    BugLog buglog;
    // Video: F8 records to captures/; the last 30 s always go into F12 reports as clip.avi.
    AudioTap tap;
    g_tap = &tap;
    const int audioRate = 48000;
    ClipBuffer clip(30);
    Recorder rec;
    std::string recPath;
    int clipW = 0;
    std::string toast;                                  // notice across the top of the screen
    int toastFrames = 0;
    auto notify = [&](const std::string& msg, int frames = 300) { toast = msg; toastFrames = frames; buglog.event("NOTICE " + msg); };
    field.onUnknownEvent = [&](const char* what) {
        notify(std::string("Not done yet: ") + what + " - press F12 to report");
    };
    auto infoText = [&] {
        std::string s = "FFL3 port " FFL3_VERSION "\n";
        char b[256];
        const std::time_t t = std::time(nullptr);
        std::strftime(b, sizeof b, "time %Y-%m-%d %H:%M:%S\n", std::localtime(&t)); s += b;
        std::snprintf(b, sizeof b, "frame %llu\nmap %02X row %d col %d facing %d vehicle %d\n",
                      static_cast<unsigned long long>(buglog.frameNo()), field.mapId(), field.player().row,
                      field.player().col, int(field.player().facing), field.vehicle());
        s += b;
        s += std::string("screen: ") + (opening ? "opening" : battle ? "battle" : shop ? "shop" : menu ? "menu" : console ? "cockpit" : text.active() ? "text" : "field") + "\n";
        if (battle) { std::snprintf(b, sizeof b, "battle formation %02X\n", battle->formation().index); s += b; }
        s += "party:";
        for (auto& r : party) { std::snprintf(b, sizeof b, " %d/%d", r.hp(), r.maxHp()); s += b; }
        s += "\nlast event: " + field.lastEvent() + "\n";
        return s;
    };
    auto palRgb = [&] { std::array<uint32_t, 4> p{}; for (int i = 0; i < 4; i++) p[size_t(i)] = kPalettes[pal].c[i] & 0xffffff; return p; };
    auto writeReport = [&](bool crash) {
        const std::string dir = newReportDir(exeDir);
        if (clipW) clip.write((std::filesystem::path(dir) / "clip.avi").string(), clipW, frame.h, palRgb(), audioRate);
        buglog.write(dir, infoText() + (crash ? "CRASH\n" : ""));
        saveBmp(argb, frame.w, frame.h, (std::filesystem::path(dir) / "screen.bmp").string().c_str());
        if (!crash) saveGame((std::filesystem::path(dir) / "game.sav").string().c_str());
        return dir;
    };
    installCrashHandler([&] { writeReport(true); });
    bool prompt = false;                                // F10: tester command line
    std::string promptLine;
    std::string lastLogged;
    auto runCommand = [&](const std::string& line) {
        char cmd[32] = {};
        unsigned a = 0, b = 0, c = 0;
        const int n = std::sscanf(line.c_str(), "%31s %x %u %u", cmd, &a, &b, &c);
        const std::string k = cmd;
        buglog.event("COMMAND " + line);
        if (k == "battle" && n >= 2 && !battle) { field.startBattle(int(a)); return std::string("battle ") + line.substr(7); }
        if (k == "map" && n >= 2) {
            field.setVehicle(a < 6 ? 2 : 0);
            if (n >= 4) field.enterMap(int(a), int(b), int(c)); else field.enterMap(int(a));
            return std::string("map ") + line.substr(4);
        }
        if (k == "fx") { if (n >= 2) SDL_setenv("FFL3_FORCEFX", line.substr(3).c_str(), 1); else SDL_setenv("FFL3_FORCEFX", "", 1); return std::string("next attack plays sprite script ") + line.substr(2); }
        if (k == "effect" && n >= 2) { SDL_setenv("FFL3_FORCEEFFECT", line.substr(7).c_str(), 1); return std::string("next attack plays effect ") + line.substr(7); }
        if (k == "death" && n >= 2) { SDL_setenv("FFL3_DEATHSTYLE", line.substr(6).c_str(), 1); return std::string("monster deaths use style ") + line.substr(6); }
        if (k == "normal") {
            for (const char* v : {"FFL3_FORCEFX", "FFL3_FORCEEFFECT", "FFL3_DEATHSTYLE", "FFL3_AREA", "FFL3_POKE"}) SDL_setenv(v, "", 1);
            return std::string("test settings back to normal");
        }
        if (k == "heal") {
            for (auto& r : party) { r.raw[0x10] = r.raw[0x12]; r.raw[0x11] = r.raw[0x13]; r.raw[0x14] = r.raw[0x16]; r.raw[0x15] = r.raw[0x17]; r.raw[1] &= 0x40; }
            return std::string("party healed");
        }
        if (k == "flag" && n >= 3) { flags.event.set(a & 0xff, b != 0); return std::string("flag set"); }
        if (k == "area") { SDL_setenv("FFL3_AREA", n >= 2 ? line.substr(5).c_str() : "", 1); return std::string("battles count as encounter area ") + line.substr(4); }
        if (k == "hp1") { for (auto& r : party) { r.raw[0x10] = 1; r.raw[0x11] = 0; } return std::string("party at 1 HP"); }
        if (k == "music" && n >= 2) { sndMusic(int(a)); return std::string("music ") + line.substr(6); }
        if (k == "sfx" && n >= 2) { sndSfx(int(a)); return std::string("sound effect ") + line.substr(4); }
        if (k == "event" && n >= 3) {                       // event XX YY (hex): a field event command
            unsigned p2 = 0;
            std::sscanf(line.c_str(), "%*s %x %x", &a, &p2);
            field.runEventCommand(int(a), int(p2));
            return std::string("event ") + line.substr(6);
        }
        if (k == "poke" && n >= 2) { SDL_setenv("FFL3_POKE", line.substr(5).c_str(), 1); return std::string("battle pokes ") + line.substr(5); }
        return std::string("commands: battle XX | map XX [row col] | fx XX | effect XX | death N | area N | hp1 | poke A=V | normal | heal | flag XX 0/1 | music XX | sfx XX | event XX YY");
    };
    // ---- F7: test mode - every bit that needs checking, set up with one key ----
    struct TestItem { const char* name; const char* setup; const char* check; bool resetAfterBattle; };
    static const TestItem kTests[] = {
        {"Opening", "TITLE", "Title, story crawl (water shimmer), naming, simulator battle, cutscenes", false},
        {"Water anim", "map 10 22 12", "Water flows, waterfall moves, like the real game", false},
        {"Talon deck", "map 0b 26 7", "Walking, doors, people, cockpit console", false},
        {"Flash event", "event 12 03", "Screen flashes white 4 times with a sound", false},
        {"Normal fight", "battle 00", "Party row walks; weapon sprites; numbers bounce; death dissolve", false},
        {"Auto Set", "battle 00", "Fight menu > Auto: toggle a member; they show Auto and skip choosing", false},
        {"Fight again?", "area 1;hp1;battle 30", "Lose: Fight again? Yes retries, No goes to the title", true},
        {"Boss fight", "battle da", "Boss picture, music, its death", false},
        {"Spell fire", "fx 6c;battle 00", "First attack plays the fire effect", true},
        {"Spell ripple", "fx 6b;battle 00", "Monsters ripple side to side", true},
        {"Spell gather", "fx 6a;battle 00", "Sparks pulled into the monsters, they vanish and return", true},
        {"Spell bubbles", "fx 6d;battle 00", "Rising bubbles", true},
        {"Spell rocks", "fx 6e;battle 00", "Rocks fall diagonally", true},
        {"Spell orbs", "fx 68;battle 00", "Orbs scatter", true},
        {"Spell clouds", "fx 27;battle 00", "Clouds drift", true},
        {"Spell shimmer", "fx 44;battle 00", "Monsters shimmer and flicker, sparks rise", true},
        {"Spell vortex", "fx 28;battle 00", "Whirling disc, screen goes pale, box comes back", true},
        {"Death dissolve", "death 0;battle 00", "Killed monster dissolves in a dotted mask", true},
        {"Death lines", "death 1;battle 00", "Picture drops out in horizontal lines", true},
        {"Death 2nd form", "death 2;battle 00", "Boss lifts off, new form shudders in, flashes", true},
        {"Death melt", "death 3;battle 00", "Bounce, flashes, melts down tile by tile", true},
        {"Picture swap", "effect 13;poke c985=5,c9d6=7;battle 00", "A monster takes another's picture", true},
        {"Thrown object", "effect 14;poke c91b=10,c9d6=5;battle 00", "Object scrolls in over the monsters", true},
        {"Sound test", "music 01", "F10 'music XX' / 'sfx XX' for the others", false},
        {"Saving", "", "Save in the menu, quit, Continue from the title", false},
    };
    constexpr int kNumTests = int(sizeof(kTests) / sizeof(kTests[0]));
    std::vector<char> testMark(size_t(kNumTests), ' ');      // ' ', 'P' passed, 'B' bug
    {                                                         // results from earlier sessions
        if (FILE* f = std::fopen((std::filesystem::path(exeDir) / "test_results.txt").string().c_str(), "r")) {
            char ln[256];
            while (std::fgets(ln, sizeof ln, f))
                for (int i = 0; i < kNumTests; i++) {
                    const std::string l = ln;
                    if (l.find(std::string("| ") + kTests[i].name + "\n") != std::string::npos ||
                        l.find(std::string("| ") + kTests[i].name + "\r") != std::string::npos)
                        testMark[size_t(i)] = l.find("PASS") != std::string::npos ? 'P' : 'B';
                }
            std::fclose(f);
        }
    }
    bool testMenu = false, resetAfterBattle = false, reportNextFrame = false;
    int testCursor = 0;
    auto markTest = [&](int i, bool pass) {
        testMark[size_t(i)] = pass ? 'P' : 'B';
        if (FILE* f = std::fopen((std::filesystem::path(exeDir) / "test_results.txt").string().c_str(), "a")) {
            char t[32];
            const std::time_t now = std::time(nullptr);
            std::strftime(t, sizeof t, "%Y-%m-%d %H:%M", std::localtime(&now));
            std::fprintf(f, "%s %s %s | %s\n", t, FFL3_VERSION, pass ? "PASS" : "BUG ", kTests[i].name);
            std::fclose(f);
        }
        buglog.event(std::string(pass ? "TEST PASS " : "TEST BUG ") + kTests[i].name);
    };
    std::function<void(const std::string&)> runSetup = [&](const std::string& setup) {
        runCommand("normal");
        size_t at = 0;
        while (at <= setup.size() && !setup.empty()) {
            const size_t semi = setup.find(';', at);
            const std::string c = setup.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
            if (c == "TITLE") {
                battle.reset(); shop.reset(); menu.reset(); console.reset(); text.close();
                title = std::make_unique<TitleScreen>(rom);
                opening = 1;
                sndMusic(6);
            } else if (!c.empty()) {
                if (opening) { opening = 0; title.reset(); naming.reset(); }
                runCommand(c);
            }
            if (semi == std::string::npos) break;
            at = semi + 1;
        }
    };
    std::string lastTitle;
    bool running = true;
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    bool aPrev = true, upPrev = true, downPrev = true, bPrev = true, leftPrev = true, rightPrev = true;
    bool startPrev = true, aKeyPrev = true;

    while (running) {
        SDL_Event e;
        if (const char* inj = std::getenv("FFL3_INJECT")) {      // tests: "frame:Key frame:Key ..." key presses
            static std::vector<std::pair<uint64_t, SDL_Keycode>> evs;
            static bool parsed = false;
            if (!parsed) {
                parsed = true;
                std::string all = inj;
                for (size_t at = 0; at < all.size();) {
                    size_t sp = all.find(' ', at);
                    const std::string tok = all.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
                    const size_t c = tok.find(':');
                    if (c != std::string::npos) evs.push_back({std::stoull(tok.substr(0, c)), SDL_GetKeyFromName(tok.substr(c + 1).c_str())});
                    if (sp == std::string::npos) break;
                    at = sp + 1;
                }
            }
            for (auto& [fr, key] : evs)
                if (fr == buglog.frameNo() && key != SDLK_UNKNOWN) {
                    SDL_Event ev{};
                    ev.type = SDL_KEYDOWN;
                    ev.key.keysym.sym = key;
                    SDL_PushEvent(&ev);
                    key = SDLK_UNKNOWN;                          // once
                }
        }
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (prompt) {                                        // typing a tester command
                if (e.type == SDL_TEXTINPUT) promptLine += e.text.text;
                if (e.type != SDL_KEYDOWN) continue;
                if (e.key.keysym.sym == SDLK_BACKSPACE && !promptLine.empty()) promptLine.pop_back();
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    prompt = false;
                    SDL_StopTextInput();
                    if (e.key.keysym.sym != SDLK_ESCAPE && !promptLine.empty()) notify(runCommand(promptLine));
                    else toastFrames = 0;
                }
                continue;
            }
            if (e.type != SDL_KEYDOWN) continue;
            if (testMenu) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: case SDLK_w: testCursor = (testCursor + kNumTests - 1) % kNumTests; break;
                case SDLK_DOWN: case SDLK_s: testCursor = (testCursor + 1) % kNumTests; break;
                case SDLK_PAGEUP: testCursor = std::max(0, testCursor - 10); break;
                case SDLK_PAGEDOWN: testCursor = std::min(kNumTests - 1, testCursor + 10); break;
                case SDLK_RIGHT: case SDLK_d: markTest(testCursor, true); break;
                case SDLK_LEFT: case SDLK_a:                     // bug: report from the game screen
                    markTest(testCursor, false);
                    testMenu = false;
                    reportNextFrame = true;
                    break;
                case SDLK_z: case SDLK_SPACE: case SDLK_RETURN:
                    testMenu = false;
                    if (runSetup) runSetup(kTests[testCursor].setup);
                    resetAfterBattle = kTests[testCursor].resetAfterBattle;
                    notify(std::string("Check: ") + kTests[testCursor].check, 600);
                    break;
                case SDLK_F7: case SDLK_x: case SDLK_ESCAPE: testMenu = false; break;
                default: break;
                }
                continue;
            }
            switch (e.key.keysym.sym) {
            case SDLK_F7: testMenu = true; continue;
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
                if (!opening && !battle && !shop && !menu && !console && !text.active()) openMenu();
                break;
            case SDLK_F5: field.setLastEvent(saveGame("ffl3.sav") ? "saved to ffl3.sav" : "save failed"); break;
            case SDLK_F9: field.setLastEvent(loadGame("ffl3.sav") ? "loaded ffl3.sav" : "no save (ffl3.sav)"); break;
            case SDLK_F3: field.encounters = !field.encounters;
                field.setLastEvent(field.encounters ? "random battles on" : "random battles off"); break;
            case SDLK_F4: if (!battle && !opening && !text.active()) field.startBattle(randomFormation(rom, field.encounterArea(), field.vehicle(), field.random8())); break;
            case SDLK_TAB: wide = !wide; frame.resize(wide ? 256 : 160, 144); break;
            case SDLK_p: pal = (pal + 1) % kNumPalettes; break;
            case SDLK_F11: {                                     // plain screenshot
                char name[64];
                std::snprintf(name, sizeof name, "ffl3_%02X_%d.bmp", field.mapId(), shotN++);
                saveBmp(argb, frame.w, frame.h, (std::filesystem::path(exeDir) / name).string().c_str());
                notify(std::string("screenshot ") + name, 120);
                break;
            }
            case SDLK_F12: {                                     // bug report
                const std::string dir = writeReport(false);
                notify("Bug report saved: " + std::filesystem::path(dir).filename().string() + " - note what went wrong", 240);
                break;
            }
            case SDLK_F10:
                prompt = true; promptLine.clear(); SDL_StartTextInput();
                break;
            case SDLK_F8:                                        // video recording on / off
                if (rec.isOpen()) {
                    rec.close();
                    notify("Recording saved: " + std::filesystem::path(recPath).filename().string(), 180);
                } else {
                    namespace fs = std::filesystem;
                    std::error_code ec;
                    fs::create_directories(fs::path(exeDir) / "captures", ec);
                    char nm[64];
                    const std::time_t t = std::time(nullptr);
                    std::strftime(nm, sizeof nm, "ffl3-%Y%m%d-%H%M%S.avi", std::localtime(&t));
                    recPath = (fs::path(exeDir) / "captures" / nm).string();
                    if (rec.open(recPath, frame.w, frame.h, palRgb(), audioRate)) notify("Recording (F8 to stop)", 120);
                    else notify("Could not start recording", 180);
                }
                break;
            default: break;
            }
        }
        // Fixed 60 Hz game logic, like the GB.
        const Uint64 now = SDL_GetPerformanceCounter();
        acc += double(now - last) / double(SDL_GetPerformanceFrequency());
        last = now;
        if (acc > 0.25) acc = 0.25;
        int steps = 0;                                           // logic frames this time round (for capture)
        while (acc >= 1.0 / 60.0) {
            acc -= 1.0 / 60.0;
            steps++;
            static const Uint8 kNoKeys[SDL_NUM_SCANCODES] = {};
            const Uint8* k = prompt || testMenu ? kNoKeys : SDL_GetKeyboardState(nullptr);   // typing / test menu: no game input
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
            const bool startHeld = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
            const bool startPressed = startHeld && !startPrev;
            startPrev = startHeld;
            buglog.frame(uint16_t((aHeld ? BugLog::A : 0) | (bHeld ? BugLog::B : 0) | (startHeld ? BugLog::Start : 0) |
                                  (up ? BugLog::Up : 0) | (down ? BugLog::Down : 0) | (d == int(Dir::Left) ? BugLog::Left : 0) |
                                  (d == int(Dir::Right) ? BugLog::Right : 0) | (k[SDL_SCANCODE_M] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] ? BugLog::Menu : 0)));
            if (field.lastEvent() != lastLogged) { lastLogged = field.lastEvent(); if (!lastLogged.empty()) buglog.event(lastLogged); }
            if (toastFrames > 0 && --toastFrames == 0) toast.clear();
            if (const char* tr = std::getenv("FFL3_SELFTEST"); tr && buglog.frameNo() == uint64_t(std::getenv("FFL3_SELFTEST_AT") ? std::atoi(std::getenv("FFL3_SELFTEST_AT")) : 120)) {   // packaging check
                std::printf("report: %s\n", writeReport(false).c_str());
                if (std::string(tr) == "crash") std::raise(SIGSEGV);
                running = false;
            }
            if (opening == 1 || opening == 3) {
                OpeningInput in;
                const bool aKey = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE];   // Enter is Start here
                in.a = aKey && !aKeyPrev; in.b = bHeld && !bPrev; in.start = startPressed;
                in.up = up && !upPrev; in.down = down && !downPrev; in.left = left && !leftPrev; in.right = right && !rightPrev;
                aKeyPrev = aKey;
                if (opening == 1) {
                    title->tick(in);
                    if (title->choice() == 2 && loadGame("ffl3.sav")) { title.reset(); opening = 0; }
                    else if (title->choice()) { title.reset(); startNewGame(); }
                } else {
                    naming->tick(in);
                    if (naming->done()) namingDone();
                }
            } else if (menu) {
                MenuInput in;
                in.a = aPressed; in.b = bHeld && !bPrev; in.up = up && !upPrev; in.down = down && !downPrev;
                in.left = left && !leftPrev; in.right = right && !rightPrev;
                menu->tick(in);
                if (menu->done()) menu.reset();
            } else if (shop) {
                ShopInput in;
                in.a = aPressed; in.b = bHeld && !bPrev; in.up = up && !upPrev; in.down = down && !downPrev;
                in.left = left && !leftPrev; in.right = right && !rightPrev;
                shop->tick(in);
                if (shop->done()) shop.reset();
            } else if (battle) {
                BattleInput in;
                in.a = aPressed; in.aHeld = aHeld; in.b = bHeld && !bPrev;
                in.up = up && !upPrev; in.down = down && !downPrev;
                in.left = left && !leftPrev; in.right = right && !rightPrev;
                battle->tick(in);
                if (battle->finished()) { finishBattle(); if (resetAfterBattle) { runCommand("normal"); resetAfterBattle = false; } }
                if (gameOver) { gameOver = false; title = std::make_unique<TitleScreen>(rom); opening = 1; }
            } else if (console) {
                ConsoleInput in;
                in.a = aPressed; in.b = bHeld && !bPrev; in.up = up && !upPrev; in.down = down && !downPrev;
                console->tick(in);
                if (console->done()) consoleDone();
            } else if (text.active()) {
                field.wander = false;                // NPCs stand still while someone talks
                field.tick(-1);                      // scripted actor moves animate under text
                field.wander = true;
                text.tick(aPressed, aHeld, up && !upPrev, down && !downPrev);
            } else {
                if (aPressed && !field.player().moveFrames && field.boardTalon()) {}
                else if (aPressed && !field.player().moveFrames) talk(rom, field, text, chest, openShop);
                else if (bHeld && !bPrev && field.land()) {}            // B in the air: land (01:4250)
                else field.tick(d);
                refreshTiles();
                tiles.animate(field.header().hook, hff93);              // 01:54A0 in the field main loop
            }
            hff93++;
            if (opening == 2 && !text.active()) startNaming(0);          // the crawl is over
            if (prologue && !text.active() && !battle) finishPrologue();
            const int wantMusic = (opening == 1 || opening == 3) ? 6 : battle ? battle->formation().music() : field.music;   // 01:7C79 / 0B:4000 / map header
            if (wantMusic != curMusic) { curMusic = wantMusic; sndMusic(wantMusic); }
            aPrev = aHeld; upPrev = up; downPrev = down; bPrev = bHeld; leftPrev = left; rightPrev = right;
        }
        refreshTiles();
        if (opening == 1 || opening == 3) {
            if (opening == 1) renderTitle(*title, tiles, opts, frame);
            else renderNaming(*naming, tiles, opts, frame);
            renderToast(tiles, prompt ? "> " + promptLine + "_" : toast, frame);
            toArgb(frame, kPalettes[pal], nullptr, argb);
        } else if (battle) {
            renderBattle(*battle, bgfx, tiles, opts, frame);
            renderToast(tiles, prompt ? "> " + promptLine + "_" : toast, frame);
            toArgb(frame, kPalettes[pal], nullptr, argb);
        } else {
            RenderOpts fo = opts;
            if (field.flashWhite()) fo.bgp = 0;                 // event $12 $03 (BGP = 0)
            renderField(field, tiles, fo, frame, debug ? &tint : nullptr);
            renderTextBox(text, tiles, opts, frame);
            if (shop) renderShop(*shop, tiles, opts, frame);
            if (menu) renderMenu(*menu, tiles, opts, frame);
            if (console) renderConsole(*console, tiles, opts, frame);
            renderToast(tiles, prompt ? "> " + promptLine + "_" : toast, frame);
            toArgb(frame, kPalettes[pal], debug ? &tint : nullptr, argb);
        }
        if (testMenu) {                                          // F7 test mode list
            std::vector<std::string> l = {"TEST MODE", "A set up, Right ok", "Left bug, B close", ""};
            std::vector<std::string> wrap;                       // what to check, wrapped to the width
            {
                const size_t cols = size_t(frame.w / 8);
                std::string cur;
                std::string word;
                const std::string txt = std::string(kTests[testCursor].check) + " ";
                for (char ch : txt) {
                    if (ch != ' ') { word += ch; continue; }
                    if (!cur.empty() && cur.size() + 1 + word.size() > cols) { wrap.push_back(cur); cur.clear(); }
                    cur += (cur.empty() ? "" : " ") + word;
                    word.clear();
                }
                if (!cur.empty()) wrap.push_back(cur);
            }
            const int rows = frame.h / 8 - 5 - int(wrap.size());
            const int first = std::clamp(testCursor - rows / 2, 0, std::max(0, kNumTests - rows));
            for (int i = first; i < kNumTests && i < first + rows; i++)
                l.push_back(std::string(i == testCursor ? "\x01" : "") + (testMark[size_t(i)] == 'P' ? "ok " : testMark[size_t(i)] == 'B' ? "BUG" : "   ") + kTests[i].name);
            l.push_back("");
            l.insert(l.end(), wrap.begin(), wrap.end());
            renderLines(tiles, l, frame);
            toArgb(frame, kPalettes[pal], nullptr, argb);
        }
        if (reportNextFrame && !testMenu && steps) {
            reportNextFrame = false;
            const std::string dir = writeReport(false);
            notify("Bug report saved: " + std::filesystem::path(dir).filename().string() + " - add a note", 240);
        }
        if (steps) {                                             // capture: one video frame per logic frame
            if (frame.w != clipW) {
                clip.clear();
                clipW = frame.w;
                if (rec.isOpen()) { rec.close(); notify("Recording stopped (screen width changed)", 180); }
            }
            const std::vector<uint8_t> rle(frame.px.begin(), frame.px.end());   // raw; encoded when written
            std::vector<int16_t> snd = tap.take();
            if (rec.isOpen()) {
                for (int i = 0; i < steps; i++) rec.frame(rle);
                rec.audio(snd);
                if (rec.bytes() > 1800ull << 20) { rec.close(); notify("Recording stopped at 1.8 GB - press F8 for a new file", 240); }
            }
            clip.add(rle, std::move(snd));
            for (int i = 1; i < steps; i++) clip.add(rle, {});
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
    g_tap = nullptr;
    if (adev) SDL_CloseAudioDevice(adev);
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
