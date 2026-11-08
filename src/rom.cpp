#include "rom.h"
#include <cstdio>
#include <cstring>

namespace ffl3 {

bool Rom::load(const std::string& path, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "Can't open ROM: " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::rewind(f);
    data_.resize(size > 0 ? size_t(size) : 0);
    size_t got = std::fread(data_.data(), 1, data_.size(), f);
    std::fclose(f);
    if (size != 0x40000 || got != data_.size()) {
        error = "ROM must be 256 KB (Final Fantasy Legend III (U))";
        return false;
    }
    // Header: title "SAGA3" and global checksum of the US release.
    if (std::memcmp(&data_[0x134], "SAGA3", 5) != 0) {
        error = "That isn't a SaGa 3 / Final Fantasy Legend III ROM";
        return false;
    }
    uint16_t sum = 0;
    for (size_t i = 0; i < data_.size(); i++)
        if (i != 0x14e && i != 0x14f) sum += data_[i];
    uint16_t hdr = (data_[0x14e] << 8) | data_[0x14f];
    if (sum != hdr) {
        error = "ROM checksum mismatch (bad dump or patched ROM)";
        return false;
    }
    return true;
}

uint8_t Rom::b(int bank, uint16_t addr) const {
    uint32_t o = off(bank, addr);
    return o < data_.size() ? data_[o] : 0xff;
}

std::vector<uint8_t> Rom::decompress(int bank, uint16_t start, uint16_t end) const {
    std::vector<uint8_t> out;
    uint16_t p = start;
    const int n = 8 << b(bank, p++);           // block size: 8, 16, 32 or 64 bytes
    while (p < end) {
        const uint8_t fill = b(bank, p++);
        const uint16_t mask = p;
        p = uint16_t(p + n / 8);
        for (int i = 0; i < n / 8; i++) {
            const uint8_t m = b(bank, uint16_t(mask + i));
            for (int bit = 7; bit >= 0; bit--)
                out.push_back((m >> bit) & 1 ? b(bank, p++) : fill);
        }
    }
    return out;
}

} // namespace ffl3
