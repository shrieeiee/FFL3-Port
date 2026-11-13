// Video capture: AVI files with RLE8 video (the 4 shades as a palette, so a minute of play is a
// few MB) and 16-bit stereo PCM sound. Plays in VLC, Windows Media Player, ffmpeg...
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace ffl3 {

// One frame (w*h colour indices 0-3, top row first) as a BI_RLE8 bitmap. With prev, a delta
// frame: unchanged pixels are skipped (the player keeps the last picture there).
std::vector<uint8_t> encodeRle8(const uint8_t* px, int w, int h, const uint8_t* prev = nullptr);

class AviWriter {
public:
    ~AviWriter() { close(); }
    // palette: 4 colours as 0xRRGGBB.
    bool open(const std::string& path, int w, int h, int fps, const std::array<uint32_t, 4>& palette, int audioRate);
    void frame(const std::vector<uint8_t>& rle, bool key = true);   // an encoded frame
    void audio(const int16_t* lr, int frames);        // interleaved stereo
    void close();
    bool isOpen() const { return f_ != nullptr; }
    uint64_t bytes() const { return size_; }

private:
    void chunk(const char* id, const void* data, uint32_t n, bool key);
    FILE* f_ = nullptr;
    int w_ = 0, h_ = 0, fps_ = 60, rate_ = 0;
    uint32_t frames_ = 0, audioFrames_ = 0, maxChunk_ = 0;
    long moviStart_ = 0;
    uint64_t size_ = 0;
    struct Idx { char id[4]; uint32_t flags, off, len; };
    std::vector<Idx> idx_;
};

// Live recording: keyframe every 5 s, delta frames between.
class Recorder {
public:
    bool open(const std::string& path, int w, int h, const std::array<uint32_t, 4>& pal, int rate);
    void frame(const std::vector<uint8_t>& px);
    void audio(const std::vector<int16_t>& lr) { avi_.audio(lr.data(), int(lr.size() / 2)); }
    void close() { avi_.close(); prev_.clear(); }
    bool isOpen() const { return avi_.isOpen(); }
    uint64_t bytes() const { return avi_.bytes(); }
private:
    AviWriter avi_;
    std::vector<uint8_t> prev_;
    int w_ = 0, h_ = 0, n_ = 0;
};

// Sound from the audio thread, collected for the recorder and the clip.
class AudioTap {
public:
    void push(const float* lr, int frames);           // audio thread
    std::vector<int16_t> take();                      // main thread: everything since last time
private:
    std::mutex m_;
    std::vector<int16_t> buf_;
};

// The last N seconds of video + sound, for bug reports.
class ClipBuffer {
public:
    explicit ClipBuffer(int seconds = 30) : max_(size_t(seconds) * 60) {}
    void add(std::vector<uint8_t> px, std::vector<int16_t> sound);   // raw frame (w*h indices)
    bool write(const std::string& path, int w, int h, const std::array<uint32_t, 4>& pal, int rate) const;
    void clear() { frames_.clear(); }
private:
    size_t max_;
    std::deque<std::pair<std::vector<uint8_t>, std::vector<int16_t>>> frames_;
};

} // namespace ffl3
