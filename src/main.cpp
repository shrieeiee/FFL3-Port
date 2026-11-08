// FFL3 / SaGa 3 PC port - milestone 1: native map viewer.
//
//   ffl3 [rom.gb] [--map HEX] [--scale N] [--shot out.bmp] [--dump-tiles out.bin]
//
// Keys: arrows/WASD scroll, PgUp/PgDn ([ / ]) previous/next map, +/- zoom,
//       P palette, G metatile grid, F12 screenshot, Esc quit.
#include "mapdata.h"
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

struct LoadedMap {
    int id = -1;
    MapHeader hdr{};
    GfxSet gfx{};
    Layout layout;
    std::vector<uint32_t> pixels;   // ARGB, (w*16) x (h*16)
    int pw = 0, ph = 0;
};

bool buildMap(const Rom& rom, int id, const Palette& pal, LoadedMap& m) {
    m = LoadedMap{};
    m.id = id;
    m.hdr = loadMapHeader(rom, id);
    m.gfx = loadGfxSet(rom, m.hdr.gfxset);
    m.layout = loadLayout(rom, m.hdr.layout);
    if (!m.layout.valid) return false;
    const auto tiles = loadBgTiles(rom, m.gfx);
    const int tw = m.layout.w * 2, th = m.layout.h * 2;
    m.pw = tw * 8; m.ph = th * 8;
    m.pixels.assign(size_t(m.pw) * m.ph, 0);
    for (int ty = 0; ty < th; ty++)
        for (int tx = 0; tx < tw; tx++) {
            const auto& t = tiles[m.layout.tiles[ty * tw + tx]];
            for (int y = 0; y < 8; y++) {
                const uint8_t lo = t[y * 2], hi = t[y * 2 + 1];
                for (int x = 0; x < 8; x++) {
                    const int v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1);
                    m.pixels[size_t(ty * 8 + y) * m.pw + tx * 8 + x] = pal.c[v];
                }
            }
        }
    return true;
}

int nextValid(const Rom& rom, int from, int dir) {
    for (int i = 1; i <= kMapCount; i++) {
        const int id = ((from + dir * i) % kMapCount + kMapCount) % kMapCount;
        if (loadLayout(rom, loadMapHeader(rom, id).layout).valid) return id;
    }
    return from;
}

bool saveBmp(const LoadedMap& m, const char* path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(m.pixels.data()), m.pw, m.ph, 32, m.pw * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::string romPath = "saga3.gb", shot, dumpTiles;
    int mapId = 0x0b, scale = 3;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--map" && i + 1 < argc) mapId = int(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--scale" && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--dump-tiles" && i + 1 < argc) dumpTiles = argv[++i];
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

    int pal = 0;
    LoadedMap map;
    if (!buildMap(rom, mapId, kPalettes[pal], map)) {
        mapId = nextValid(rom, mapId, 1);
        buildMap(rom, mapId, kPalettes[pal], map);
    }

    // Headless modes (used by tests / tooling).
    if (!dumpTiles.empty() || !shot.empty()) {
        if (!dumpTiles.empty()) {
            FILE* f = std::fopen(dumpTiles.c_str(), "wb");
            if (!f) return 1;
            std::fwrite(map.layout.tiles.data(), 1, map.layout.tiles.size(), f);
            std::fclose(f);
        }
        if (!shot.empty() && !saveBmp(map, shot.c_str())) return 1;
        std::printf("map %02x layout %02x gfx %02x  %dx%d metatiles\n", map.id, map.hdr.layout,
                    map.hdr.gfxset, map.layout.w, map.layout.h);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("FFL3", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       160 * 5, 144 * 5, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = nullptr;
    auto upload = [&] {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, map.pw, map.ph);
        SDL_UpdateTexture(tex, nullptr, map.pixels.data(), map.pw * 4);
        char title[160];
        std::snprintf(title, sizeof title,
                      "FFL3 map viewer - map %02X  layout %02X  gfx %02X  hook %02X  music %02X  %dx%d  [%s]",
                      map.id, map.hdr.layout, map.hdr.gfxset, map.hdr.hook, map.hdr.music,
                      map.layout.w, map.layout.h, kPalettes[pal].name);
        SDL_SetWindowTitle(win, title);
    };
    upload();

    float camX = 0, camY = 0;
    bool grid = false, running = true;
    int shotN = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_PAGEDOWN: case SDLK_RIGHTBRACKET:
                    buildMap(rom, nextValid(rom, map.id, 1), kPalettes[pal], map); camX = camY = 0; upload(); break;
                case SDLK_PAGEUP: case SDLK_LEFTBRACKET:
                    buildMap(rom, nextValid(rom, map.id, -1), kPalettes[pal], map); camX = camY = 0; upload(); break;
                case SDLK_EQUALS: case SDLK_KP_PLUS: if (scale < 8) scale++; break;
                case SDLK_MINUS: case SDLK_KP_MINUS: if (scale > 1) scale--; break;
                case SDLK_p: pal = (pal + 1) % kNumPalettes; buildMap(rom, map.id, kPalettes[pal], map); upload(); break;
                case SDLK_g: grid = !grid; break;
                case SDLK_F12: {
                    char name[64]; std::snprintf(name, sizeof name, "map_%02X_%d.bmp", map.id, shotN++);
                    saveBmp(map, name); break;
                }
                default: break;
                }
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = float(now - last) / float(SDL_GetPerformanceFrequency());
        last = now;
        const Uint8* k = SDL_GetKeyboardState(nullptr);
        const float speed = 160.0f * dt;
        if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) camX -= speed;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) camX += speed;
        if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) camY -= speed;
        if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) camY += speed;

        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        const float viewW = float(ww) / scale, viewH = float(wh) / scale;
        if (camX > map.pw - viewW) camX = map.pw - viewW;
        if (camY > map.ph - viewH) camY = map.ph - viewH;
        if (camX < 0) camX = 0;
        if (camY < 0) camY = 0;

        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderClear(ren);
        const int ix = int(camX), iy = int(camY);
        SDL_Rect src{ix, iy, std::min(map.pw - ix, int(viewW) + 1), std::min(map.ph - iy, int(viewH) + 1)};
        SDL_Rect dst{0, 0, src.w * scale, src.h * scale};
        SDL_RenderCopy(ren, tex, &src, &dst);
        if (grid) {
            SDL_SetRenderDrawColor(ren, 255, 0, 128, 90);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for (int x = (16 - ix % 16) % 16; x < src.w; x += 16)
                SDL_RenderDrawLine(ren, x * scale, 0, x * scale, dst.h);
            for (int y = (16 - iy % 16) % 16; y < src.h; y += 16)
                SDL_RenderDrawLine(ren, 0, y * scale, dst.w, y * scale);
        }
        SDL_RenderPresent(ren);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
