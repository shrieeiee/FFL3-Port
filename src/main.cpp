// FFL3 / SaGa 3 PC port - milestone 2: walk the field.
//
//   ffl3 [rom.gb] [--map HEX] [--pos ROW,COL] [--walk UDLR...] [--shot out.bmp]
//                 [--wide] [--debug] [--dump-blocks out.bin] [--scale N]
//
// Keys: arrows/WASD walk, F1 collision overlay, F2 no-clip, Tab wide/GB view,
//       PgUp/PgDn ([ / ]) previous/next map, P palette, F12 screenshot, Esc quit.
#include "render.h"
#include <SDL.h>
#include <algorithm>
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

int dirFromChar(char c) {
    switch (c) {
    case 'D': case 'd': return 0;
    case 'U': case 'u': return 1;
    case 'L': case 'l': return 2;
    case 'R': case 'r': return 3;
    default: return -1;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string romPath = "saga3.gb", shot, dumpTiles, walk;
    int mapId = 0x02, scale = 5, startRow = -1, startCol = -1;
    bool wide = false, debug = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--map" && i + 1 < argc) mapId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--pos" && i + 1 < argc) std::sscanf(argv[++i], "%d,%d", &startRow, &startCol);
        else if (a == "--walk" && i + 1 < argc) walk = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--dump-blocks" && i + 1 < argc) dumpTiles = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--wide") wide = true;
        else if (a == "--debug") debug = true;
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

    Field field(rom);
    if (!field.enterMap(mapId, startRow, startCol)) field.enterMap(nextValid(rom, mapId, 1));
    TileCache tiles;
    tiles.build(rom, field);
    RenderOpts opts;
    Frame frame;
    frame.resize(wide ? 256 : 160, 144);
    int pal = 0;
    std::vector<uint32_t> argb;
    std::vector<uint8_t> tint;
    auto refreshTiles = [&] { if (tiles.map != field.mapId()) tiles.build(rom, field); };

    // ---- headless: tests / tooling ----------------------------------------------------
    if (!dumpTiles.empty() || !shot.empty() || !walk.empty()) {
        if (!dumpTiles.empty()) {
            FILE* f = std::fopen(dumpTiles.c_str(), "wb");
            if (!f) return 1;
            std::fwrite(field.layout().blocks.data(), 1, field.layout().blocks.size(), f);
            std::fclose(f);
        }
        for (char c : walk) {
            const int d = dirFromChar(c);
            if (d < 0) continue;
            field.tick(d);
            for (int i = 0; i < 16 && field.player().moveFrames; i++) field.tick(-1);
            refreshTiles();
            if (field.lastEvent().rfind("exit", 0) == 0 || field.lastEvent().rfind("event", 0) == 0)
                std::printf("  step %c: %s\n", c, field.lastEvent().c_str());
        }
        if (!shot.empty()) {
            renderField(field, tiles, opts, frame, debug ? &tint : nullptr);
            toArgb(frame, kPalettes[pal], debug ? &tint : nullptr, argb);
            if (!saveBmp(argb, frame.w, frame.h, shot.c_str())) return 1;
        }
        std::printf("map %02X layout %02X gfx %02X sprites %02X  %dx%d  player %d,%d  npcs %zu  [%s]\n",
                    field.mapId(), field.header().layout, field.header().gfxset, field.header().unk1e,
                    field.layout().w, field.layout().h, field.player().row, field.player().col,
                    field.npcs().size(), field.lastEvent().c_str());
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

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type != SDL_KEYDOWN) continue;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_PAGEDOWN: case SDLK_RIGHTBRACKET: field.enterMap(nextValid(rom, field.mapId(), 1)); break;
            case SDLK_PAGEUP: case SDLK_LEFTBRACKET: field.enterMap(nextValid(rom, field.mapId(), -1)); break;
            case SDLK_F1: debug = !debug; break;
            case SDLK_F2: field.noClip = !field.noClip; break;
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
            if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) d = 0;
            else if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) d = 1;
            else if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) d = 2;
            else if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) d = 3;
            field.tick(d);
        }
        refreshTiles();
        renderField(field, tiles, opts, frame, debug ? &tint : nullptr);
        toArgb(frame, kPalettes[pal], debug ? &tint : nullptr, argb);
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
