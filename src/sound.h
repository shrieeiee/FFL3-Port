// FFL3 / SaGa 3 PC port - sound.
// SoundDriver is a C++ translation of the game's bank-0F sound driver (0F:4000-4830): music
// sequencer, effects (vibrato / envelope lists) and sound effects. It writes Game Boy APU
// registers; Apu turns those register writes into samples. All music and SFX data are read
// from the user's ROM.
#pragma once
#include <cstdint>
#include <functional>

namespace ffl3 {

class Rom;

// Game Boy APU (2 pulse, wave, noise) synthesised at an arbitrary sample rate.
class Apu {
public:
    explicit Apu(int sampleRate = 48000);
    void write(uint8_t reg, uint8_t v);   // reg = low byte of FF10-FF3F
    uint8_t read(uint8_t reg) const;      // raw register value (enough for the driver's NR51 reads)
    // Render n stereo frames (interleaved L,R) of float samples, running the APU clock.
    void render(float* out, int n);
    int sampleRate() const { return rate_; }

private:
    struct Square { bool on = false; int duty = 0, step = 0, timer = 0, freq = 0, len = 0, vol = 0, envTimer = 0;
                    bool lenOn = false; int sweepTimer = 0, shadow = 0; bool sweepOn = false; };
    struct Wave { bool on = false; int pos = 0, timer = 0, freq = 0, len = 0; bool lenOn = false; uint8_t sample = 0; };
    struct Noise { bool on = false; int timer = 0, len = 0, vol = 0, envTimer = 0; bool lenOn = false; uint16_t lfsr = 0x7fff; };
    void trigger(int ch);
    void frameStep();
    int sweepCalc();
    int squareOut(const Square& s, int ch) const;
    int period(int ch) const;
    uint8_t r_[0x30] = {};
    Square sq_[2];
    Wave wave_;
    Noise noise_;
    int rate_;
    double cycAcc_ = 0;
    int seqTimer_ = 8192, seqStep_ = 0;
    float capL_ = 0, capR_ = 0, charge_;
};

class SoundDriver {
public:
    explicit SoundDriver(const Rom& rom);
    std::function<void(uint8_t reg, uint8_t v)> out;   // APU register write
    std::function<uint8_t(uint8_t reg)> in;            // APU register read
    void init();            // 0F:4003 / 4031
    void update();          // 0F:4000 / 4006, once per frame (vblank)
    void music(int id) { h(0xba) = uint8_t(id); }     // hFFBA - song request (0 = stop)
    void sfx(int id) { h(0xbc) = uint8_t(id); }       // hFFBC - sound effect request
    int currentMusic() { return h(0xbb); }

    uint8_t wram[0x68] = {};   // wCB00-CB67
    uint8_t hram[0x10] = {};   // hFFBA-FFC9

private:
    const Rom& rom_;
    uint8_t& m(uint16_t a) { return wram[a - 0xcb00]; }
    uint8_t& h(uint8_t a) { return hram[a - 0xba]; }
    uint16_t m16(uint16_t a) { return uint16_t(m(a) | m(a + 1) << 8); }
    void set16(uint16_t a, uint16_t v) { m(a) = uint8_t(v); m(a + 1) = uint8_t(v >> 8); }
    uint8_t rd(uint16_t a) const;
    uint16_t rd16(uint16_t a) const { return uint16_t(rd(a) | rd(a + 1) << 8); }
    void io(uint8_t reg, uint8_t v) { if (out) out(reg, v); }
    uint8_t ioRead(uint8_t reg) { return in ? in(reg) : 0; }

    void startSong(uint8_t id);
    void silence();          // 40B9
    void restoreCh1();       // 40DD
    void restoreNoise();     // 40FD
    void tick();             // 41CE
    void notes();            // 41F9
    void fxCh2();            // 4625
    void fxCh1();            // 467D
    void fxWaveNoise();      // 46DB
    void startSfx();         // 4759
    void sfxUpdate();        // 478E
    void restart(uint16_t at);                        // 439B
    uint16_t list(uint16_t counter, uint8_t& count);  // 474B
    void octave(uint16_t at, uint8_t a);              // 43EA
    uint16_t freqAddr(uint8_t note, uint16_t octave); // 4416
    bool command(int ch, uint8_t a, uint16_t& de);    // F0-FF dispatch
    void handler(uint16_t addr, uint16_t& de);
};

// Driver + APU, run one driver frame per 70224 GB cycles of audio. Requests are applied one per
// driver frame, in order (the game never issues two in the same frame).
class SoundSystem {
public:
    SoundSystem(const Rom& rom, int sampleRate);
    void music(int id) { queue(0, id); }
    void sfx(int id) { queue(1, id); }
    void render(float* out, int frames);   // interleaved stereo
    SoundDriver& driver() { return drv_; }
    Apu& apu() { return apu_; }

private:
    void queue(int kind, int id);
    Apu apu_;
    SoundDriver drv_;
    int req_[32][2] = {};
    int reqHead_ = 0, reqCount_ = 0;
    double toFrame_ = 0;
};

} // namespace ffl3
