// FFL3 / SaGa 3 PC port - milestone 5: walk, talk, events and battles.
//
//   ffl3 [rom.gb] [--map HEX] [--pos ROW,COL] [--walk UDLR...] [--shot out.bmp]
//                 [--wide] [--debug] [--dump-blocks out.bin] [--scale N]
//
// Keys: arrows/WASD walk, Z/Space/Enter = A, X/Backspace = B, F1 collision overlay, F2 no-clip,
//       F3 random battles on/off, F4 start a random battle here, Tab wide/GB view,
//       PgUp/PgDn ([ / ]) previous/next map, P palette, F12 screenshot, Esc quit.
#include "render.h"
#include <SDL.h>
#include <algorithm>
#include <memory>
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
void talk(const Rom& rom, Field& field, TextBox& tb) {
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
    default:                                         // 1 = chest (00:09d4), 2 = shop (0A:6FD4)
        std::snprintf(buf, sizeof buf, "%s %02X (not implemented)", o.kind == 1 ? "chest" : "shop", o.id);
        field.setLastEvent(buf);
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
    std::string romPath = "saga3.gb", shot, dumpTiles, walk;
    int textId = -1, runTextId = -1, battleArg = -1, shotAt = -1;
    std::string keys, dumpView;
    int mapId = -1, scale = 5, startRow = -1, startCol = -1;
    bool wide = false, debug = false, showSlots = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--map" && i + 1 < argc) mapId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--pos" && i + 1 < argc) std::sscanf(argv[++i], "%d,%d", &startRow, &startCol);
        else if (a == "--walk" && i + 1 < argc) walk = argv[++i];
        else if (a == "--text" && i + 1 < argc) textId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--run-text" && i + 1 < argc) runTextId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--battle" && i + 1 < argc) battleArg = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--keys" && i + 1 < argc) keys = argv[++i];
        else if (a == "--dump-view" && i + 1 < argc) dumpView = argv[++i];
        else if (a == "--shot-at" && i + 1 < argc) shotAt = std::atoi(argv[++i]);
        else if (a == "--dump-blocks" && i + 1 < argc) dumpTiles = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--wide") wide = true;
        else if (a == "--debug") debug = true;
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
    std::unique_ptr<Battle> battle;
    BattleGfx bgfx;
    uint32_t battleSeed = 1;
    field.startBattle = [&](int hffca) {
        battle = std::make_unique<Battle>(rom, party, hffca, battleSeed++);
        bgfx.build(rom, *battle);
        char b[64]; std::snprintf(b, sizeof b, "battle: formation %02X%s", hffca & 0x7f, hffca & 0x80 ? " (fixed)" : "");
        field.setLastEvent(b);
    };
    // After a battle: back to the field, or (all down) back to the start like a game over.
    auto finishBattle = [&] {
        const Battle::Result r = battle->result();
        battle.reset();
        if (r == Battle::Result::Lost) {
            newParty();
            text.close();
            field.enterMap(0x0b, 26, 7, Dir::Up);
            field.setLastEvent("party defeated - back to the start");
        } else {
            field.setLastEvent(r == Battle::Result::Won ? "battle won" : "ran away");
        }
    };
    field.giveItem = [&](int item) {
        char b[64]; std::snprintf(b, sizeof b, "got item %02X", item);
        field.setLastEvent(b);
    };
    text.log = [&](const std::string& m) { field.setLastEvent(m); };
    if (mapId < 0) {                                  // new game
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
        for (const std::string& l : B.log) {
            std::string a;
            for (unsigned char t : l) {
                if (t >= 0xba && t <= 0xd3) a += char('A' + t - 0xba);
                else if (t >= 0xd4 && t <= 0xed) a += char('a' + t - 0xd4);
                else if (t >= 0xb0 && t <= 0xb9) a += char('0' + t - 0xb0);
                else if (t == 0xff) a += ' ';
                else if (t == 0xaf) a += '.';
                else if (t == 0xf3) a += '!';
                else if (t == 0xf2) a += '-';
                else if (t == 0xad) a += '\'';
                else a += '#';
            }
            std::printf("| %s\n", a.c_str());
        }
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
    if (!dumpTiles.empty() || !shot.empty() || !walk.empty()) {
        if (!dumpTiles.empty()) {
            FILE* f = std::fopen(dumpTiles.c_str(), "wb");
            if (!f) return 1;
            std::fwrite(field.layout().blocks.data(), 1, field.layout().blocks.size(), f);
            std::fclose(f);
        }
        // Script: U/D/L/R step, A = press A (talk / advance text) then 40 frames, W = 60 frames.
        for (char c : walk) {
            if (c == 'A' || c == 'a') {
                if (text.active()) text.tick(true, true, false, false);
                else talk(rom, field, text);
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
        if (!shot.empty()) {
            renderField(field, tiles, opts, frame, debug ? &tint : nullptr);
            renderTextBox(text, tiles, opts, frame);
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
            if (battle) {
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
                if (aPressed && !field.player().moveFrames) talk(rom, field, text);
                else field.tick(d);
            }
            aPrev = aHeld; upPrev = up; downPrev = down; bPrev = bHeld; leftPrev = left; rightPrev = right;
        }
        refreshTiles();
        if (battle) {
            renderBattle(*battle, bgfx, tiles, opts, frame);
            toArgb(frame, kPalettes[pal], nullptr, argb);
        } else {
            renderField(field, tiles, opts, frame, debug ? &tint : nullptr);
            renderTextBox(text, tiles, opts, frame);
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
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
