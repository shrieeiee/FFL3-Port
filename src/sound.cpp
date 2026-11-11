// FFL3 / SaGa 3 PC port - sound driver (translation of bank 0F) and Game Boy APU synth.
#include "sound.h"
#include "rom.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ffl3 {

// APU register offsets (FF10 + n)
enum : uint8_t {
    NR10 = 0x10, NR11, NR12, NR13, NR14, NR21 = 0x16, NR22, NR23, NR24,
    NR30 = 0x1a, NR31, NR32, NR33, NR34, NR41 = 0x20, NR42, NR43, NR44, NR50, NR51, NR52,
};

// ---------------------------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------------------------

SoundDriver::SoundDriver(const Rom& rom) : rom_(rom) {}

uint8_t SoundDriver::rd(uint16_t a) const {
    if (a >= 0xcb00 && a < 0xcb68) return wram[a - 0xcb00];
    return a < 0x8000 ? rom_.b(0x0f, a) : 0xff;
}

// 0F:4006
void SoundDriver::update() {
    if ((m(0xcb1c) | m(0xcb4c)) == 0 && h(0xba) != h(0xbb)) {
        uint8_t a = h(0xba);
        h(0xbb) = a;                                   // 402D
        if (a) startSong(a); else init();
    }
    if (h(0xbc)) startSfx();
    tick();
    sfxUpdate();
}

// 0F:4031
void SoundDriver::init() {
    io(NR52, 0xff);
    io(NR12, 0x08); io(NR22, 0x08); io(NR32, 0x08);
    silence();
    io(NR50, 0x77);
    io(NR51, 0xff);
    m(0xcb05) = m(0xcb1d) = m(0xcb35) = m(0xcb4d) = 0xff;
    m(0xcb1c) = 0; m(0xcb01) = 0;
    std::memset(hram, 0, sizeof hram);
}

// 0F:4065
void SoundDriver::startSong(uint8_t id) {
    uint8_t a = uint8_t(id - 1);
    if (a >= 0x14) return;
    uint16_t p = uint16_t(0x482f + a * 8);
    static const uint16_t dst[4] = {0xcb09, 0xcb21, 0xcb39, 0xcb51};
    for (int i = 0; i < 4; i++) set16(dst[i], rd16(uint16_t(p + 2 * i)));
    m(0xcb00) = 0xff; m(0xcb01) = 0x3c;
    for (uint16_t b : {0xcb02, 0xcb1a, 0xcb32, 0xcb4a}) {   // 40B1
        for (int i = 0; i < 4; i++) m(uint16_t(b + i)) = 0;
        m(uint16_t(b + 4)) = 1;
    }
}

// 0F:40B9
void SoundDriver::silence() {
    m(0xcb66) = m(0xcb67) = 0;
    io(NR10, 0);
    io(NR13, 0xff); io(NR23, 0xff); io(NR31, 0xff); io(NR33, 0xff);
    io(NR14, 0x07); io(NR24, 0x07); io(NR34, 0x07);
    io(NR42, 0x00); io(NR44, 0x80);
}

// 0F:40DD
void SoundDriver::restoreCh1() {
    io(NR10, 0); io(NR11, m(0xcb30)); io(NR12, 0); io(NR13, 0xff); io(NR14, 0x87);
    io(NR51, uint8_t((ioRead(NR51) & 0xee) | m(0xcb31)));
}

// 0F:40FD
void SoundDriver::restoreNoise() {
    io(NR42, 0x08); io(NR43, 0); io(NR44, 0x80);
    io(NR51, uint8_t((ioRead(NR51) & 0x77) | m(0xcb61)));
}

// 0F:41CE
void SoundDriver::tick() {
    unsigned s = m(0xcb00) + m(0xcb01);
    m(0xcb00) = uint8_t(s);
    if (s > 0xff) { notes(); notes(); }
    uint8_t c = uint8_t(h(0xbd) + 1);
    if (c == 3) c = 0;
    h(0xbd) = c;
    if (c == 0) fxCh2();
    if (h(0xbd) == 1) fxCh1();
    if (h(0xbd) == 2) fxWaveNoise();
}

// 0F:439B - counter = 1, current list pointer = list start
void SoundDriver::restart(uint16_t at) {
    m(at) = 1;
    m(uint16_t(at + 3)) = m(uint16_t(at + 1));
    m(uint16_t(at + 4)) = m(uint16_t(at + 2));
}

// 0F:474B - next (count, data) entry of an effect list; a zero count links to another list.
uint16_t SoundDriver::list(uint16_t counter, uint8_t& count) {
    uint16_t p = m16(uint16_t(counter + 3));
    count = rd(p++);
    if (count) return p;
    p = rd16(p);
    count = rd(p++);
    return p;
}

// 0F:43EA
void SoundDriver::octave(uint16_t at, uint8_t a) {
    static const uint8_t add[8] = {0x18, 0x30, 0x48, 0x60, 0xe8, 0xd0, 0xb8, 0xa0};  // 0F:4420
    if (!(a & 8)) m(at) = uint8_t((a & 7) * 24);
    else m(at) = uint8_t(m(at) + add[a & 7]);
}

// 0F:4416
uint16_t SoundDriver::freqAddr(uint8_t note, uint16_t oct) {
    return uint16_t(0x4114 + uint8_t(note * 2 + m(oct)));
}

// F0-FF command (E0-EF too on the noise channel). Returns false if the driver would hang.
bool SoundDriver::command(int ch, uint8_t a, uint16_t& de) {
    static const uint16_t tables[4] = {0x4428, 0x4448, 0x4468, 0x4488};
    uint16_t addr = rd16(uint16_t(tables[ch] + (a & 15) * 2));
    if (addr == 0x4616) return false;   // "jr 4616" - never used by real data
    handler(addr, de);
    return true;
}

void SoundDriver::handler(uint16_t addr, uint16_t& de) {
    auto loop = [&](uint16_t counter) {                     // 4529
        if (--m(counter)) de = rd16(de); else de = uint16_t(de + 2);
    };
    auto ifCount = [&](uint16_t counter) {                  // 4543
        uint8_t v = rd(de++);
        if (v == m(counter)) de = rd16(de); else de = uint16_t(de + 2);
    };
    auto ptr = [&](uint16_t at) { m(at) = rd(de++); m(uint16_t(at + 1)) = rd(de++); };  // 44CC
    switch (addr) {
    case 0x44a8: { uint8_t v = rd(de++); m(0xcb3b) = v; io(NR32, v); break; }
    case 0x44b0: ptr(0xcb14); break;
    case 0x44b5: ptr(0xcb2c); break;
    case 0x44ba: ptr(0xcb5c); break;
    case 0x44bf: ptr(0xcb0f); break;
    case 0x44c4: ptr(0xcb27); break;
    case 0x44c9: ptr(0xcb3f); break;
    case 0x44d3: m(0xcb02) = rd(de++); break;
    case 0x44d9: m(0xcb1a) = rd(de++); break;
    case 0x44df: m(0xcb32) = rd(de++); break;
    case 0x44e5: m(0xcb4a) = rd(de++); break;
    case 0x44eb: m(0xcb03) = rd(de++); break;
    case 0x44f1: m(0xcb1b) = rd(de++); break;
    case 0x44f7: m(0xcb33) = rd(de++); break;
    case 0x44fd: m(0xcb4b) = rd(de++); break;
    case 0x4503: loop(0xcb02); break;
    case 0x4508: loop(0xcb1a); break;
    case 0x450d: loop(0xcb32); break;
    case 0x4512: loop(0xcb4a); break;
    case 0x4517: loop(0xcb03); break;
    case 0x451c: loop(0xcb1b); break;
    case 0x4521: loop(0xcb33); break;
    case 0x4526: loop(0xcb4b); break;
    case 0x452f: ifCount(0xcb02); break;
    case 0x4534: ifCount(0xcb1a); break;
    case 0x4539: ifCount(0xcb32); break;
    case 0x453e: ifCount(0xcb4a); break;
    case 0x4548: de = rd16(de); break;
    case 0x454e: { uint8_t v = rd(de++); io(NR21, v); m(0xcb18) = v; break; }
    case 0x4556: { uint8_t v = rd(de++); m(0xcb30) = v; if (!m(0xcb1c)) io(NR11, v); break; }
    case 0x4565: m(0xcb01) = rd(de++); break;
    case 0x456b: {                                           // load wave RAM
        uint8_t lo = rd(de++), hi = rd(de++);
        h(0xc2) = lo; h(0xc3) = hi;
        uint16_t p = uint16_t(lo | hi << 8);
        io(NR30, 0);
        for (int i = 0; i < 16; i++) io(uint8_t(0x30 + i), rd(uint16_t(p + i)));
        io(NR30, 0x80); io(NR32, 0); io(NR34, 0x87);
        break;
    }
    case 0x458f: handler(0x4565, de); handler(0x44bf, de); handler(0x44b0, de); handler(0x454e, de);
                 handler(0x459b, de); break;
    case 0x459b: { uint8_t v = rd(uint16_t(0x4603 + rd(de++))); io(NR51, uint8_t((ioRead(NR51) & 0xdd) | v)); break; }
    case 0x45ac: handler(0x44c4, de); handler(0x44b5, de); handler(0x4556, de); handler(0x45b5, de); break;
    case 0x45b5: {
        uint8_t v = rd(uint16_t(0x4607 + rd(de++)));
        m(0xcb31) = v;
        if (!m(0xcb1c)) io(NR51, uint8_t((ioRead(NR51) & 0xee) | v));
        break;
    }
    case 0x45cf: handler(0x44c9, de); handler(0x44a8, de); handler(0x456b, de); handler(0x45d8, de); break;
    case 0x45d8: { uint8_t v = rd(uint16_t(0x460b + rd(de++))); io(NR51, uint8_t((ioRead(NR51) & 0xbb) | v)); break; }
    case 0x45e9: {
        uint8_t v = rd(uint16_t(0x460f + rd(de++)));
        m(0xcb61) = v;
        if (!m(0xcb4c)) io(NR51, uint8_t((ioRead(NR51) & 0x77) | v));
        break;
    }
    case 0x4613: de++; break;
    default: break;
    }
}

// 0F:41F9 - advance each channel's note counter; read new notes/commands when it hits 0.
void SoundDriver::notes() {
    // --- pulse 2 (music lead) ---
    if (m(0xcb05) != 0xff) {
        uint8_t c = --m(0xcb06);
        h(0xbe) = c;
        if (!c) {
            uint16_t de = m16(0xcb09);
            for (;;) {
                uint8_t a = rd(de++);
                if (a < 0xe0) {
                    set16(0xcb09, de);
                    static const uint8_t lens[16] = {0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18, 0x12,
                                                     0x10, 0x0c, 0x09, 0x08, 0x06, 0x04, 0x03, 0x02};
                    m(0xcb06) = lens[a & 15];
                    uint8_t note = a >> 4;
                    m(0xcb07) = note;
                    if (note == 0x0c) break;
                    if (note > 0x0c) { io(NR23, 0xff); io(NR24, 0x07); break; }
                    uint16_t t = freqAddr(note, 0xcb08);
                    io(NR22, rd(uint16_t(m16(0xcb14) + 1)));
                    uint8_t lo = rd(t), hi = rd(uint16_t(t + 1));
                    io(NR23, lo); m(0xcb0c) = lo;
                    io(NR24, hi); m(0xcb0d) = hi;
                    restart(0xcb0e); restart(0xcb13);
                    break;
                }
                if (a == 0xff) { m(0xcb05) = 0xff; io(NR23, 0xff); io(NR24, 0x07); break; }
                if (a >= 0xf0) { if (!command(0, a, de)) { m(0xcb05) = 0xff; break; } }
                else octave(0xcb08, a);
            }
        }
    }
    // --- pulse 1 (shared with sound effects) ---
    if (m(0xcb1d) != 0xff) {
        uint8_t c = --m(0xcb1e);
        h(0xbf) = c;
        if (!c) {
            uint16_t de = m16(0xcb21);
            for (;;) {
                uint8_t a = rd(de++);
                if (a < 0xe0) {
                    set16(0xcb21, de);
                    static const uint8_t lens[16] = {0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18, 0x12,
                                                     0x10, 0x0c, 0x09, 0x08, 0x06, 0x04, 0x03, 0x02};
                    m(0xcb1e) = lens[a & 15];
                    uint8_t note = a >> 4;
                    m(0xcb1f) = note;
                    if (m(0xcb1c) || note == 0x0c) break;
                    if (note > 0x0c) { io(NR13, 0xff); io(NR14, 0x07); break; }
                    uint16_t t = freqAddr(note, 0xcb20);
                    io(NR12, rd(uint16_t(m16(0xcb2c) + 1)));
                    uint8_t lo = rd(t), hi = rd(uint16_t(t + 1));
                    io(NR13, lo); m(0xcb24) = lo;
                    io(NR14, hi); m(0xcb25) = hi;
                    restart(0xcb26); restart(0xcb2b);
                    m(0xcb66) = 0;
                    break;
                }
                if (a == 0xff) { m(0xcb1d) = 0xff; io(NR13, 0xff); io(NR14, 0x07); break; }
                if (a >= 0xf0) { if (!command(1, a, de)) { m(0xcb1d) = 0xff; break; } }
                else octave(0xcb20, a);
            }
        }
    }
    // --- wave ---
    if (m(0xcb35) != 0xff) {
        uint8_t c = --m(0xcb36);
        h(0xc0) = c;
        if (!c) {
            uint16_t de = m16(0xcb39);
            for (;;) {
                uint8_t a = rd(de++);
                if (a < 0xe0) {
                    set16(0xcb39, de);
                    static const uint8_t lens[16] = {0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18, 0x12,
                                                     0x10, 0x0c, 0x09, 0x08, 0x06, 0x04, 0x03, 0x02};
                    m(0xcb36) = lens[a & 15];
                    uint8_t note = a >> 4;
                    m(0xcb37) = note;
                    if (note == 0x0c) break;
                    if (note > 0x0c) { io(NR32, 0); break; }
                    uint16_t t = freqAddr(note, 0xcb38);
                    uint8_t lo = rd(t), hi = rd(uint16_t(t + 1));
                    io(NR32, m(0xcb3b));
                    io(NR33, lo); m(0xcb3c) = lo;
                    io(NR34, hi & 7); m(0xcb3d) = hi & 7;
                    restart(0xcb3e);
                    break;
                }
                if (a == 0xff) { m(0xcb35) = 0xff; io(NR33, 0xff); io(NR34, 0x07); break; }
                if (a >= 0xf0) { if (!command(2, a, de)) { m(0xcb35) = 0xff; break; } }
                else octave(0xcb38, a);
            }
        }
    }
    // --- noise ---
    if (m(0xcb4d) != 0xff) {
        uint8_t c = --m(0xcb4e);
        h(0xc1) = c;
        if (!c) {
            uint16_t de = m16(0xcb51);
            for (;;) {
                uint8_t a = rd(de++);
                if (a < 0xe0) {
                    set16(0xcb51, de);
                    static const uint8_t lens[16] = {0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18, 0x12,
                                                     0x10, 0x0c, 0x09, 0x08, 0x06, 0x04, 0x03, 0x02};
                    m(0xcb4e) = lens[a & 15];
                    uint8_t note = a >> 4;
                    m(0xcb4f) = note;
                    if (m(0xcb4c) || note == 0x0c) break;
                    if (note > 0x0c) { io(NR42, 0); io(NR43, 0); io(NR44, 0x80); break; }
                    uint8_t poly = rd(uint16_t(0x4619 + note));
                    io(NR42, rd(uint16_t(m16(0xcb5c) + 1)));
                    io(NR43, poly); m(0xcb54) = poly;
                    io(NR44, 0x80);
                    m(0xcb67) = 0;
                    restart(0xcb5b);
                    break;
                }
                if (a == 0xff) { m(0xcb4d) = 0xff; io(NR42, 0); io(NR43, 0); io(NR44, 0x80); break; }
                if (!command(3, a, de)) { m(0xcb4d) = 0xff; break; }
            }
        }
    }
}

// 0F:4625 - pulse 2 vibrato + envelope lists
void SoundDriver::fxCh2() {
    if (m(0xcb05) || !h(0xbe) || m(0xcb07) == 0x0d) return;
    uint8_t n;
    if (!--m(0xcb0e)) {
        uint16_t p = list(0xcb0e, n);
        m(0xcb0e) = n;
        int8_t off = int8_t(rd(p++));
        set16(0xcb11, p);
        uint16_t f = uint16_t(m16(0xcb0c) + off);
        io(NR23, uint8_t(f)); io(NR24, uint8_t((f >> 8) & 7));
    }
    if (m(0xcb13) == 0xff || --m(0xcb13)) return;
    uint16_t p = list(0xcb13, n);
    m(0xcb13) = n;
    io(NR22, rd(p++));
    io(NR24, m(0xcb0d));
    set16(0xcb16, p);
}

// 0F:467D - pulse 1 vibrato + envelope lists
void SoundDriver::fxCh1() {
    if (m(0xcb1c) | m(0xcb1d)) return;
    if (!h(0xbf) || m(0xcb1f) == 0x0d || m(0xcb66)) return;
    uint8_t n;
    if (!--m(0xcb26)) {
        uint16_t p = list(0xcb26, n);
        m(0xcb26) = n;
        int8_t off = int8_t(rd(p++));
        set16(0xcb29, p);
        uint16_t f = uint16_t(m16(0xcb24) + off);
        io(NR13, uint8_t(f)); io(NR14, uint8_t((f >> 8) & 7));
    }
    if (m(0xcb2b) == 0xff || --m(0xcb2b)) return;
    uint16_t p = list(0xcb2b, n);
    m(0xcb2b) = n;
    io(NR12, rd(p++));
    io(NR14, m(0xcb25));
    set16(0xcb2e, p);
}

// 0F:46DB - wave vibrato, noise envelope list
void SoundDriver::fxWaveNoise() {
    uint8_t n;
    if (!m(0xcb35) && h(0xc0) && m(0xcb37) != 0x0d && !--m(0xcb3e)) {
        uint16_t p = list(0xcb3e, n);
        m(0xcb3e) = n;
        int8_t off = int8_t(rd(p++));
        set16(0xcb41, p);
        uint16_t f = uint16_t(m16(0xcb3c) + off);
        io(NR33, uint8_t(f)); io(NR34, uint8_t((f >> 8) & 7));
    }
    if (m(0xcb4c) | m(0xcb4d)) return;
    if (!h(0xc1) || m(0xcb4f) == 0x0d) return;
    if (m(0xcb5b) == 0xff || --m(0xcb5b)) return;
    if (m(0xcb67)) return;
    uint16_t p = list(0xcb5b, n);
    m(0xcb5b) = n;
    io(NR42, rd(p++));
    io(NR44, 0x80);
    set16(0xcb5e, p);
}

// 0F:4759
void SoundDriver::startSfx() {
    uint8_t i = uint8_t((h(0xbc) - 1) * 2);
    uint8_t lo = rd(uint16_t(0x66f2 + i)), hi = rd(uint16_t(0x66f3 + i));
    if (uint8_t(lo + hi)) { m(0xcb62) = lo; m(0xcb63) = hi; m(0xcb1c) = 1; }
    lo = rd(uint16_t(0x6774 + i)); hi = rd(uint16_t(0x6775 + i));
    if (uint8_t(lo + hi)) { m(0xcb64) = lo; m(0xcb65) = hi; m(0xcb4c) = 1; }
    h(0xbc) = 0;
}

// 0F:478E
void SoundDriver::sfxUpdate() {
    if (m(0xcb1c)) {
        m(0xcb66) = m(0xcb1c);
        if (!--m(0xcb1c)) {
            uint16_t p = m16(0xcb62);
            for (int guard = 0; guard < 4096; guard++) {
                uint8_t a = rd(p++);
                m(0xcb1c) = a;
                if (!a) { restoreCh1(); break; }
                if (a == 0xef) {
                    uint16_t to = rd16(p); p = uint16_t(p + 2);
                    if (--h(0xc4)) p = to;
                    continue;
                }
                if (a >= 0xf0) { h(0xc4) = a & 15; continue; }
                for (int r = 0; r < 5; r++) io(uint8_t(NR10 + r), rd(p++));
                io(NR51, ioRead(NR51) | 0x11);
                set16(0xcb62, p);
                break;
            }
        }
    }
    if (!m(0xcb4c)) return;
    m(0xcb67) = m(0xcb4c);
    if (--m(0xcb4c)) return;
    uint16_t p = m16(0xcb64);
    for (int guard = 0; guard < 4096; guard++) {
        uint8_t a = rd(p++);
        m(0xcb4c) = a;
        if (!a) { restoreNoise(); return; }
        if (a == 0xef) {
            uint16_t to = rd16(p); p = uint16_t(p + 2);
            if (--h(0xc5)) p = to;
            continue;
        }
        if (a >= 0xf0) { h(0xc5) = a & 15; continue; }
        io(NR42, rd(p++)); io(NR43, rd(p++)); io(NR44, 0x80);
        io(NR51, ioRead(NR51) | 0x88);
        set16(0xcb64, p);
        return;
    }
}

// ---------------------------------------------------------------------------------------------
// APU
// ---------------------------------------------------------------------------------------------

static const int kCpuHz = 4194304;
static const uint8_t kDuty[4] = {0x01, 0x81, 0x87, 0x7e};   // bit per step, step 0 = bit 0

Apu::Apu(int sampleRate) : rate_(sampleRate) {
    charge_ = float(std::pow(0.999958, double(kCpuHz) / sampleRate));
    // post-boot register values
    static const uint8_t init[0x17] = {0x80, 0xbf, 0xf3, 0xff, 0xbf, 0x00, 0x3f, 0x00, 0xff, 0xbf,
                                       0x7f, 0xff, 0x9f, 0xff, 0xbf, 0x00, 0xff, 0x00, 0x00, 0xbf,
                                       0x77, 0xf3, 0xf1};
    std::memcpy(r_, init, sizeof init);
}

uint8_t Apu::read(uint8_t reg) const { return r_[(reg - 0x10) % 0x30]; }

int Apu::period(int ch) const {
    if (ch < 2) return (2048 - sq_[ch].freq) * 4;
    if (ch == 2) return (2048 - wave_.freq) * 2;
    uint8_t nr43 = r_[NR43 - 0x10];
    static const int div[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    return div[nr43 & 7] << (nr43 >> 4);
}

void Apu::write(uint8_t reg, uint8_t v) {
    if (reg < 0x10 || reg >= 0x40) return;
    if (reg == NR52) {
        if (!(v & 0x80)) {
            std::memset(r_, 0, 0x16);
            sq_[0].on = sq_[1].on = wave_.on = noise_.on = false;
        }
        r_[NR52 - 0x10] = uint8_t((v & 0x80) | 0x70);
        return;
    }
    if (!(r_[NR52 - 0x10] & 0x80) && reg < 0x30) return;
    r_[reg - 0x10] = v;
    switch (reg) {
    case NR11: case NR21: { Square& s = sq_[reg == NR21]; s.duty = v >> 6; s.len = 64 - (v & 63); break; }
    case NR12: case NR22: if (!(v & 0xf8)) sq_[reg == NR22].on = false; break;
    case NR13: case NR23: { Square& s = sq_[reg == NR23]; s.freq = (s.freq & 0x700) | v; break; }
    case NR14: case NR24: {
        Square& s = sq_[reg == NR24];
        s.freq = (s.freq & 0xff) | ((v & 7) << 8);
        s.lenOn = v & 0x40;
        if (v & 0x80) trigger(reg == NR24);
        break;
    }
    case NR30: if (!(v & 0x80)) wave_.on = false; break;
    case NR31: wave_.len = 256 - v; break;
    case NR33: wave_.freq = (wave_.freq & 0x700) | v; break;
    case NR34:
        wave_.freq = (wave_.freq & 0xff) | ((v & 7) << 8);
        wave_.lenOn = v & 0x40;
        if (v & 0x80) trigger(2);
        break;
    case NR41: noise_.len = 64 - (v & 63); break;
    case NR42: if (!(v & 0xf8)) noise_.on = false; break;
    case NR44: noise_.lenOn = v & 0x40; if (v & 0x80) trigger(3); break;
    default: break;
    }
}

void Apu::trigger(int ch) {
    if (ch < 2) {
        Square& s = sq_[ch];
        uint8_t env = r_[(ch ? NR22 : NR12) - 0x10];
        s.on = (env & 0xf8) != 0;
        if (!s.len) s.len = 64;
        s.timer = period(ch);
        s.vol = env >> 4;
        s.envTimer = env & 7;
        if (ch == 0) {
            uint8_t sw = r_[0];
            s.shadow = s.freq;
            s.sweepTimer = (sw >> 4) & 7 ? (sw >> 4) & 7 : 8;
            s.sweepOn = (sw & 0x70) || (sw & 7);
            if (sw & 7) sweepCalc();
        }
    } else if (ch == 2) {
        wave_.on = (r_[NR30 - 0x10] & 0x80) != 0;
        if (!wave_.len) wave_.len = 256;
        wave_.timer = period(2);
        wave_.pos = 0;
    } else {
        uint8_t env = r_[NR42 - 0x10];
        noise_.on = (env & 0xf8) != 0;
        if (!noise_.len) noise_.len = 64;
        noise_.timer = period(3);
        noise_.vol = env >> 4;
        noise_.envTimer = env & 7;
        noise_.lfsr = 0x7fff;
    }
}

int Apu::sweepCalc() {
    Square& s = sq_[0];
    uint8_t sw = r_[0];
    int d = s.shadow >> (sw & 7);
    int f = (sw & 8) ? s.shadow - d : s.shadow + d;
    if (f > 2047) s.on = false;
    return f;
}

void Apu::frameStep() {
    int st = seqStep_;
    seqStep_ = (seqStep_ + 1) & 7;
    if (!(st & 1)) {                                    // length
        for (Square& s : sq_) if (s.lenOn && s.len && !--s.len) s.on = false;
        if (wave_.lenOn && wave_.len && !--wave_.len) wave_.on = false;
        if (noise_.lenOn && noise_.len && !--noise_.len) noise_.on = false;
    }
    if (st == 2 || st == 6) {                           // sweep
        Square& s = sq_[0];
        uint8_t sw = r_[0];
        if (--s.sweepTimer <= 0) {
            s.sweepTimer = (sw >> 4) & 7 ? (sw >> 4) & 7 : 8;
            if (s.sweepOn && ((sw >> 4) & 7)) {
                int f = sweepCalc();
                if (f <= 2047 && (sw & 7)) {
                    s.shadow = s.freq = f;
                    r_[NR13 - 0x10] = uint8_t(f);
                    r_[NR14 - 0x10] = uint8_t((r_[NR14 - 0x10] & 0xf8) | (f >> 8));
                    sweepCalc();
                }
            }
        }
    }
    if (st == 7) {                                      // envelope
        auto env = [](int& vol, int& timer, uint8_t reg) {
            int per = reg & 7;
            if (!per) return;
            if (--timer <= 0) {
                timer = per;
                if ((reg & 8) && vol < 15) vol++;
                else if (!(reg & 8) && vol > 0) vol--;
            }
        };
        env(sq_[0].vol, sq_[0].envTimer, r_[NR12 - 0x10]);
        env(sq_[1].vol, sq_[1].envTimer, r_[NR22 - 0x10]);
        env(noise_.vol, noise_.envTimer, r_[NR42 - 0x10]);
    }
}

void Apu::render(float* out, int n) {
    const double cps = double(kCpuHz) / rate_;
    const bool power = r_[NR52 - 0x10] & 0x80;
    for (int i = 0; i < n; i++) {
        cycAcc_ += cps;
        int cyc = int(cycAcc_);
        cycAcc_ -= cyc;
        double acc[4] = {0, 0, 0, 0};
        int left = cyc;
        while (left > 0) {
            int step = std::min(left, seqTimer_);
            for (int c = 0; c < 2; c++) step = std::min(step, sq_[c].timer > 0 ? sq_[c].timer : step);
            if (wave_.timer > 0) step = std::min(step, wave_.timer);
            if (noise_.timer > 0) step = std::min(step, noise_.timer);
            if (step <= 0) step = 1;
            // current digital outputs (0-15)
            for (int c = 0; c < 2; c++) {
                const Square& s = sq_[c];
                if (s.on && ((kDuty[s.duty] >> s.step) & 1)) acc[c] += double(s.vol) * step;
            }
            if (wave_.on) {
                static const int shift[4] = {4, 0, 1, 2};
                acc[2] += double(wave_.sample >> shift[(r_[NR32 - 0x10] >> 5) & 3]) * step;
            }
            if (noise_.on && !(noise_.lfsr & 1)) acc[3] += double(noise_.vol) * step;
            // advance timers
            for (int c = 0; c < 2; c++) {
                Square& s = sq_[c];
                if ((s.timer -= step) <= 0) { s.timer += period(c); s.step = (s.step + 1) & 7; }
            }
            if ((wave_.timer -= step) <= 0) {
                wave_.timer += period(2);
                wave_.pos = (wave_.pos + 1) & 31;
                uint8_t b = r_[0x20 + (wave_.pos >> 1)];
                wave_.sample = (wave_.pos & 1) ? (b & 15) : (b >> 4);
            }
            if ((noise_.timer -= step) <= 0) {
                noise_.timer += period(3);
                uint16_t x = uint16_t((noise_.lfsr ^ (noise_.lfsr >> 1)) & 1);
                noise_.lfsr = uint16_t((noise_.lfsr >> 1) | (x << 14));
                if (r_[NR43 - 0x10] & 8) noise_.lfsr = uint16_t((noise_.lfsr & ~0x40) | (x << 6));
            }
            if ((seqTimer_ -= step) <= 0) { seqTimer_ += 8192; frameStep(); }
            left -= step;
        }
        // DACs: digital 0..15 -> analog -1..1 (DAC off -> 0)
        bool dac[4] = {(r_[NR12 - 0x10] & 0xf8) != 0, (r_[NR22 - 0x10] & 0xf8) != 0,
                       (r_[NR30 - 0x10] & 0x80) != 0, (r_[NR42 - 0x10] & 0xf8) != 0};
        float a[4];
        for (int c = 0; c < 4; c++) a[c] = dac[c] && power ? float(acc[c] / cyc / 7.5 - 1.0) : 0.f;
        uint8_t nr51 = r_[NR51 - 0x10], nr50 = r_[NR50 - 0x10];
        float l = 0, r = 0;
        for (int c = 0; c < 4; c++) {
            if (nr51 & (0x10 << c)) l += a[c];
            if (nr51 & (1 << c)) r += a[c];
        }
        l *= float(((nr50 >> 4) & 7) + 1) / 8.f;
        r *= float((nr50 & 7) + 1) / 8.f;
        // high-pass (the GB's output capacitor)
        float ol = l - capL_, orr = r - capR_;
        capL_ = l - ol * charge_;
        capR_ = r - orr * charge_;
        out[2 * i] = ol * 0.2f;
        out[2 * i + 1] = orr * 0.2f;
    }
}

// ---------------------------------------------------------------------------------------------

SoundSystem::SoundSystem(const Rom& rom, int sampleRate) : apu_(sampleRate), drv_(rom) {
    drv_.out = [this](uint8_t r, uint8_t v) { apu_.write(r, v); };
    drv_.in = [this](uint8_t r) { return apu_.read(r); };
    drv_.init();
}

void SoundSystem::queue(int kind, int id) {
    if (reqCount_ == 32) return;
    int* r = req_[(reqHead_ + reqCount_++) % 32];
    r[0] = kind; r[1] = id;
}

void SoundSystem::render(float* out, int frames) {
    const double perFrame = double(apu_.sampleRate()) * 70224.0 / 4194304.0;   // ~59.73 Hz
    while (frames > 0) {
        if (toFrame_ <= 0) {
            if (reqCount_) {
                const int* r = req_[reqHead_];
                reqHead_ = (reqHead_ + 1) % 32; reqCount_--;
                if (r[0] == 0) drv_.music(r[1]); else drv_.sfx(r[1]);
            }
            drv_.update();
            toFrame_ += perFrame;
        }
        int n = std::min(frames, int(std::ceil(toFrame_)));
        if (n < 1) n = 1;
        apu_.render(out, n);
        out += 2 * n; frames -= n; toFrame_ -= n;
    }
}

} // namespace ffl3
