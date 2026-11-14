#include "fieldanim.h"
#include <cstring>

namespace ffl3 {

namespace {
struct V {
    uint8_t* m;                         // $8000-$97FF
    uint8_t& operator[](int a) const { return m[a - 0x8000]; }
};

void copy(V v, int src, int dst, int n) { std::memmove(&v[dst], &v[src], size_t(n)); }
void swap(V v, int a, int b, int n) {   // via wC800 (01:60F5 / 6101)
    uint8_t t[32];
    std::memcpy(t, &v[a], size_t(n));
    copy(v, b, a, n);
    std::memcpy(&v[b], t, size_t(n));
}

// 01:5730: the 16x8 strip at hl (two tiles side by side) scrolls right a pixel, on frame 6
// (top strip) and 14 (the strip below, hl + $100) of every 16. 01:5723: frame 6 only.
void flow(V v, uint8_t t, int hl, bool bottom = true) {
    if ((t & 15) == 14 && bottom) hl += 0x100;
    else if ((t & 15) != 6) return;
    for (int i = 0; i < 16; i++) {
        uint8_t& b = v[hl + i];
        uint8_t& c = v[hl + i + 16];
        const uint8_t nb = uint8_t((c & 1) << 7 | b >> 1);
        const uint8_t nc = uint8_t((b & 1) << 7 | c >> 1);
        b = nb;
        c = nc;
    }
}

void down(V v, int hl) {                // 01:57A9: a tile scrolls down a row (wraps)
    uint8_t m[16];
    std::memcpy(m, &v[hl], 16);
    for (int i = 0; i < 16; i++) v[hl + i] = m[(i + 14) & 15];
}

void up(V v, int hl) {                  // 01:57CE: ... up a row
    uint8_t m[16];
    std::memcpy(m, &v[hl], 16);
    for (int i = 0; i < 16; i++) v[hl + i] = m[(i + 2) & 15];
}

void rrc(V v, int hl) {                 // 01:5847: every byte rotated right (pixels shift right)
    for (int i = 0; i < 16; i++) { uint8_t& b = v[hl + i]; b = uint8_t(b >> 1 | b << 7); }
}

void downEvery4(V v, uint8_t t, int hl) { if ((t & 3) == 0) down(v, hl); }   // 01:57A4
void upOn2(V v, uint8_t t, int hl) { if ((t & 3) == 2) up(v, hl); }          // 01:57C7

void h5836(V v, uint8_t t) { if ((t & 3) == 0) { down(v, 0x9790); down(v, 0x97a0); } }
void h5828(V v, uint8_t t) { if ((t & 3) == 2) down(v, 0x9190); h5836(v, t); }
void h5798(V v, uint8_t t) { upOn2(v, t, 0x9220); downEvery4(v, t, 0x93b0); }
void h58a1(V v, uint8_t t) {
    if (t & 15) return;
    swap(v, 0x97b0, 0x9690, 32);
    swap(v, 0x96d0, 0x9790, 32);
}
void h5601(V v, uint8_t t) {            // four frames of a 16x16 picture, one every 16 frames
    if (t & 15) return;
    static const int src[4] = {0x9460, 0x94a0, 0x94c0, 0x94e0};
    const int s = src[(t >> 4) & 3];
    copy(v, s, 0x9060, 32);
    copy(v, s + 0x100, 0x9160, 32);
}
void h565f(V v, uint8_t t) {
    if (!(t & 15)) {
        static const int src[4] = {0x90a0, 0x90c0, 0x90e0, 0x9440};
        const int s = src[(t >> 4) & 3];
        copy(v, s, 0x96d0, 32);
        copy(v, s + 0x100, 0x97d0, 32);
    }
    h5836(v, t);
}
void h5964(V v, uint8_t t) {
    if (t & 15) return;
    swap(v, 0x9220, 0x9250, 32);
    swap(v, 0x9320, 0x9350, 32);
}
void h594a(V v, uint8_t t) { if (!(t & 15)) swap(v, 0x90c0, 0x92a0, 16); }
void h5701(V v, uint8_t t) { flow(v, t, 0x9600); flow(v, t, 0x9060); }
} // namespace

// 01:54A0: the map's hook (map header byte 3 -> wCA18), once a frame; t = hFF93.
void runMapHook(int hook, uint8_t t, uint8_t* vram8000) {
    const V v{vram8000};
    switch (hook) {
    case 0x01: flow(v, t, 0x9080); break;                          // 56E4
    case 0x02: flow(v, t, 0x9600); break;                          // 571E
    case 0x03: flow(v, t, 0x9680, false); break;                   // 5723
    case 0x04: flow(v, t, 0x9200); break;                          // 56DF
    case 0x05: flow(v, t, 0x9680); break;                          // 5719
    case 0x06: flow(v, t, 0x9000); break;                          // 5714
    case 0x07: upOn2(v, t, 0x9020); downEvery4(v, t, 0x91b0); break;   // 578C
    case 0x08:                                                     // 59C2
        flow(v, t, 0x96c0);
        if ((t & 0x3f) == 0x20) swap(v, 0x97e0, 0x96e0, 32);
        else if ((t & 0x3f) == 0) swap(v, 0x97c0, 0x96c0, 32);
        break;
    case 0x09: flow(v, t, 0x9400); break;                          // 56C8
    case 0x0a: flow(v, t, 0x9080); flow(v, t, 0x9400); break;      // 56C5
    case 0x0b: if (!(t & 0x3f)) swap(v, 0x97e0, 0x96e0, 32); break;   // 59FE
    case 0x0d:                                                     // 57F2
        if ((t & 15) == 8) up(v, 0x9380);
        else if (!(t & 15)) rrc(v, 0x9390);
        break;
    case 0x0e:                                                     // 56E9
        if (t & 15) { flow(v, t, 0x9000); break; }
        rrc(v, 0x9190);
        h5701(v, t);
        break;
    case 0x0f: case 0x15: case 0x1e: h5601(v, t); break;
    case 0x10: h5836(v, t); break;
    case 0x11: case 0x1d: h5828(v, t); break;
    case 0x12: case 0x13: h565f(v, t); break;
    case 0x14:                                                     // 586A
        if (t & 0x1f) break;
        swap(v, 0x9440, 0x9460, 32);
        swap(v, 0x9540, 0x9560, 32);
        break;
    case 0x16: h5798(v, t); h58a1(v, t); break;                    // 589E
    case 0x17: flow(v, t, 0x9000); h58a1(v, t); break;             // 5899
    case 0x18: h5798(v, t); break;
    case 0x19: h594a(v, t); h5964(v, t); break;                    // 58D0
    case 0x1a:                                                     // 58D6
        if (!(t & 15)) swap(v, 0x9500, 0x9510, 16);
        if (t & 0x1f) break;
        swap(v, 0x92e0, 0x96c0, 16);
        swap(v, 0x93e0, 0x96d0, 16);
        swap(v, 0x93b0, 0x9640, 16);
        swap(v, 0x93c0, 0x9750, 16);
        break;
    case 0x1b: h5964(v, t); break;
    case 0x1c: downEvery4(v, t, 0x8600); if (!(t & 3)) down(v, 0x8610); flow(v, t, 0x9200); break;   // 56CD
    case 0x1f: h5701(v, t); break;
    case 0x20: flow(v, t, 0x9260); flow(v, t, 0x9400); break;      // 5709
    case 0x21: if (!(t & 15)) swap(v, 0x9660, 0x9760, 32); break;   // 5850
    case 0x22:                                                     // 5993
        if (t & 0x1f) break;
        swap(v, 0x9620, 0x9660, 32);
        swap(v, 0x9720, 0x9760, 32);
        break;
    case 0x23: flow(v, t, 0x9080); if (!(t & 3)) { down(v, 0x9600); down(v, 0x9610); } break;   // 5814
    default: break;                                                // 0, $0C: nothing
    }
}

} // namespace ffl3

namespace ffl3 {
namespace {
// 01:5A18 / 5A44: one 8x8 tile turned a quarter clockwise, a bitplane at a time:
// out row j, bit p = in row p, bit 7 - j.
void turnTile(const uint8_t* in, uint8_t* out) {
    for (int plane = 0; plane < 2; plane++)
        for (int j = 0; j < 8; j++) {
            uint8_t b = 0;
            for (int p = 7; p >= 0; p--) b = uint8_t(b << 1 | ((in[p * 2 + plane] >> (7 - j)) & 1));
            out[j * 2 + plane] = b;
        }
}
// 01:5552: the 16x16 picture at src (TL, BL = +$100, TR = +$10, BR = +$110) turned into dst.
void turnPicture(uint8_t* v, int src, int dst) {
    uint8_t t[4][16];
    const int from[4] = {src, src + 0x100, src + 0x10, src + 0x110};           // C810 C820 C830 C840
    for (int i = 0; i < 4; i++) for (int k = 0; k < 16; k++) t[i][k] = v[from[i] - 0x8000 + k];
    const int to[4] = {dst + 0x10, dst, dst + 0x110, dst + 0x100};              // TL->TR, BL->TL, TR->BR, BR->BL
    for (int i = 0; i < 4; i++) turnTile(t[i], &v[to[i] - 0x8000]);
}
} // namespace

void mapLoadTiles(int map, uint8_t* v) {
    if (map != 0x0a && map != 0x0b) return;
    turnPicture(v, 0x9060, 0x9460);
    turnPicture(v, 0x9460, 0x94a0);
    turnPicture(v, 0x94a0, 0x94c0);
    turnPicture(v, 0x94c0, 0x94e0);
}

} // namespace ffl3
