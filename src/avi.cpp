#include "avi.h"
#include <algorithm>
#include <cstring>

namespace ffl3 {

namespace {
// Pixels [x0, x1) of a row: runs (n, v) where 3+ repeat, absolute literals (0, n, bytes..., pad)
// otherwise.
void encodeSpan(const uint8_t* row, int x0, int x1, std::vector<uint8_t>& out) {
    int x = x0;
    while (x < x1) {
        int r = 1;
        while (x + r < x1 && r < 255 && row[x + r] == row[x]) r++;
        if (r >= 3 || x1 - x < 3) { out.push_back(uint8_t(r)); out.push_back(row[x]); x += r; continue; }
        int n = 0;                                      // literal until a run of 3 starts
        while (x + n < x1 && n < 255) {
            if (x + n + 2 < x1 && row[x + n] == row[x + n + 1] && row[x + n] == row[x + n + 2]) break;
            n++;
        }
        if (n < 3) { for (int i = 0; i < n; i++) { out.push_back(1); out.push_back(row[x + i]); } x += n; continue; }
        out.push_back(0); out.push_back(uint8_t(n));
        out.insert(out.end(), row + x, row + x + n);
        if (n & 1) out.push_back(0);
        x += n;
    }
}
} // namespace

std::vector<uint8_t> encodeRle8(const uint8_t* px, int w, int h, const uint8_t* prev) {
    std::vector<uint8_t> out;
    out.reserve(size_t(w) * 4);
    for (int y = h - 1; y >= 0; y--) {                 // DIBs are bottom-up
        const uint8_t* row = px + size_t(y) * size_t(w);
        const uint8_t* old = prev ? prev + size_t(y) * size_t(w) : nullptr;
        int x = 0;
        while (x < w) {
            if (old) {                                  // skip what hasn't changed
                int k = 0;
                while (x + k < w && row[x + k] == old[x + k]) k++;
                if (x + k == w) break;                  // rest of the row unchanged: end of line
                if (k >= 4 || (k && x == 0)) {
                    while (k > 0) { const int d = std::min(k, 255); out.push_back(0); out.push_back(2); out.push_back(uint8_t(d)); out.push_back(0); k -= d; x += d; }
                    continue;
                }
                int e = x;                              // changed span: up to 4 unchanged in a row
                for (int same = 0; e < w; e++) {
                    if (row[e] == old[e]) { if (++same >= 4) { e -= 3; break; } } else same = 0;
                }
                if (e > w) e = w;
                encodeSpan(row, x, e, out);
                x = e;
            } else {
                encodeSpan(row, 0, w, out);
                x = w;
            }
        }
        out.push_back(0);
        out.push_back(y ? 0 : 1);                      // end of line / end of bitmap
    }
    return out;
}

namespace {
void put32(FILE* f, uint32_t v) { uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); }
void put16(FILE* f, uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); }
void fourcc(FILE* f, const char* s) { std::fwrite(s, 1, 4, f); }
void patch32(FILE* f, long at, uint32_t v) { const long here = std::ftell(f); std::fseek(f, at, SEEK_SET); put32(f, v); std::fseek(f, here, SEEK_SET); }

// Header layout (byte offsets patched on close).
constexpr long kRiffSize = 4, kAvihFrames = 48, kAvihMaxBytes = 36, kVidLength = 140, kVidSugg = 144,
               kAudLength = 1288, kMoviSizeAt = 0;   // (movi size: found at open)
} // namespace

bool AviWriter::open(const std::string& path, int w, int h, int fps, const std::array<uint32_t, 4>& pal, int rate) {
    close();
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) return false;
    w_ = w; h_ = h; fps_ = fps; rate_ = rate;
    frames_ = audioFrames_ = maxChunk_ = 0;
    idx_.clear();
    fourcc(f_, "RIFF"); put32(f_, 0); fourcc(f_, "AVI ");
    fourcc(f_, "LIST"); put32(f_, 4 + 64 + 12 + 64 + 8 + 40 + 1024 + 12 + 64 + 8 + 18); fourcc(f_, "hdrl");
    fourcc(f_, "avih"); put32(f_, 56);
    put32(f_, uint32_t(1000000 / fps)); put32(f_, 0); put32(f_, 0); put32(f_, 0x10);   // AVIF_HASINDEX
    put32(f_, 0); put32(f_, 0); put32(f_, 2); put32(f_, 0);                             // frames, init, streams, sugg
    put32(f_, uint32_t(w)); put32(f_, uint32_t(h)); put32(f_, 0); put32(f_, 0); put32(f_, 0); put32(f_, 0);
    // video stream
    fourcc(f_, "LIST"); put32(f_, 4 + 64 + 8 + 40 + 1024); fourcc(f_, "strl");
    fourcc(f_, "strh"); put32(f_, 56);
    fourcc(f_, "vids"); fourcc(f_, "mrle"); put32(f_, 0); put16(f_, 0); put16(f_, 0); put32(f_, 0);
    put32(f_, 1); put32(f_, uint32_t(fps)); put32(f_, 0); put32(f_, 0);                 // scale, rate, start, length
    put32(f_, 0); put32(f_, 0xffffffff); put32(f_, 0);                                   // sugg, quality, sample size
    put16(f_, 0); put16(f_, 0); put16(f_, uint16_t(w)); put16(f_, uint16_t(h));
    fourcc(f_, "strf"); put32(f_, 40 + 1024);
    put32(f_, 40); put32(f_, uint32_t(w)); put32(f_, uint32_t(h)); put16(f_, 1); put16(f_, 8);
    put32(f_, 1); put32(f_, 0); put32(f_, 0); put32(f_, 0); put32(f_, 256); put32(f_, 4);   // BI_RLE8
    for (int i = 0; i < 256; i++) {
        const uint32_t c = i < 4 ? pal[size_t(i)] : 0;
        const uint8_t q[4] = {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16), 0};       // B G R 0
        std::fwrite(q, 1, 4, f_);
    }
    // audio stream
    fourcc(f_, "LIST"); put32(f_, 4 + 64 + 8 + 18); fourcc(f_, "strl");
    fourcc(f_, "strh"); put32(f_, 56);
    fourcc(f_, "auds"); put32(f_, 0); put32(f_, 0); put16(f_, 0); put16(f_, 0); put32(f_, 0);
    put32(f_, 1); put32(f_, uint32_t(rate)); put32(f_, 0); put32(f_, 0);
    put32(f_, uint32_t(rate) * 4 / 10); put32(f_, 0xffffffff); put32(f_, 4);
    put16(f_, 0); put16(f_, 0); put16(f_, 0); put16(f_, 0);
    fourcc(f_, "strf"); put32(f_, 18);
    put16(f_, 1); put16(f_, 2); put32(f_, uint32_t(rate)); put32(f_, uint32_t(rate) * 4); put16(f_, 4); put16(f_, 16); put16(f_, 0);
    fourcc(f_, "LIST"); put32(f_, 0); moviStart_ = std::ftell(f_); fourcc(f_, "movi");
    size_ = uint64_t(std::ftell(f_));
    return true;
}

void AviWriter::chunk(const char* id, const void* data, uint32_t n, bool key) {
    Idx e;
    std::memcpy(e.id, id, 4);
    e.flags = key ? 0x10 : 0;
    e.off = uint32_t(std::ftell(f_) - moviStart_);
    e.len = n;
    idx_.push_back(e);
    fourcc(f_, id); put32(f_, n);
    std::fwrite(data, 1, n, f_);
    if (n & 1) std::fputc(0, f_);
    maxChunk_ = std::max(maxChunk_, n);
    size_ += 8 + n + (n & 1);
}

void AviWriter::frame(const std::vector<uint8_t>& rle, bool key) {
    if (!f_) return;
    chunk("00dc", rle.data(), uint32_t(rle.size()), key);
    frames_++;
}

void AviWriter::audio(const int16_t* lr, int frames) {
    if (!f_ || frames <= 0) return;
    chunk("01wb", lr, uint32_t(frames) * 4, true);
    audioFrames_ += uint32_t(frames);
}

void AviWriter::close() {
    if (!f_) return;
    const long moviEnd = std::ftell(f_);
    fourcc(f_, "idx1"); put32(f_, uint32_t(idx_.size() * 16));
    for (const Idx& e : idx_) { std::fwrite(e.id, 1, 4, f_); put32(f_, e.flags); put32(f_, e.off); put32(f_, e.len); }
    const long end = std::ftell(f_);
    patch32(f_, kRiffSize, uint32_t(end - 8));
    patch32(f_, moviStart_ - 4, uint32_t(moviEnd - moviStart_));
    patch32(f_, kAvihFrames, frames_);
    patch32(f_, kAvihMaxBytes, maxChunk_);
    patch32(f_, kVidLength, frames_);
    patch32(f_, kVidSugg, maxChunk_);
    patch32(f_, kAudLength, audioFrames_);
    (void)kMoviSizeAt;
    std::fclose(f_);
    f_ = nullptr;
}

void AudioTap::push(const float* lr, int frames) {
    std::lock_guard<std::mutex> g(m_);
    if (buf_.size() > 48000 * 2 * 4) buf_.clear();     // nobody is taking it: don't grow forever
    for (int i = 0; i < frames * 2; i++) {
        const float v = std::clamp(lr[i], -1.0f, 1.0f);
        buf_.push_back(int16_t(v * 32767.0f));
    }
}

std::vector<int16_t> AudioTap::take() {
    std::lock_guard<std::mutex> g(m_);
    std::vector<int16_t> out;
    out.swap(buf_);
    return out;
}

bool Recorder::open(const std::string& path, int w, int h, const std::array<uint32_t, 4>& pal, int rate) {
    w_ = w; h_ = h; n_ = 0; prev_.clear();
    return avi_.open(path, w, h, 60, pal, rate);
}

void Recorder::frame(const std::vector<uint8_t>& px) {
    const bool key = prev_.empty() || n_ % 300 == 0;
    avi_.frame(encodeRle8(px.data(), w_, h_, key ? nullptr : prev_.data()), key);
    prev_ = px;
    n_++;
}

void ClipBuffer::add(std::vector<uint8_t> px, std::vector<int16_t> sound) {
    frames_.emplace_back(std::move(px), std::move(sound));
    while (frames_.size() > max_) frames_.pop_front();
}

bool ClipBuffer::write(const std::string& path, int w, int h, const std::array<uint32_t, 4>& pal, int rate) const {
    AviWriter a;
    if (!a.open(path, w, h, 60, pal, rate)) return false;
    const std::vector<uint8_t>* prev = nullptr;
    int n = 0;
    for (const auto& [v, s] : frames_) {
        const bool key = !prev || n % 300 == 0;
        a.frame(encodeRle8(v.data(), w, h, key ? nullptr : prev->data()), key);
        a.audio(s.data(), int(s.size() / 2));
        prev = &v;
        n++;
    }
    a.close();
    return true;
}

} // namespace ffl3
