// FFL3 / SaGa 3 PC port - ROM access.
// The port reads all game data from the user's own ROM at runtime; no game assets are
// stored in this repository.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ffl3 {

class Rom {
public:
    bool load(const std::string& path, std::string& error);

    // Offset of bank:addr in the ROM image (bank is ignored for 0000-3FFF, like MBC1 bank 0).
    static uint32_t off(int bank, uint16_t addr) {
        return addr < 0x4000 ? addr : uint32_t(bank) * 0x4000 + (addr - 0x4000);
    }
    uint8_t b(int bank, uint16_t addr) const;
    uint16_t w(int bank, uint16_t addr) const { return b(bank, addr) | (b(bank, addr + 1) << 8); }
    // 00:3741 - 16-bit pointer table lookup
    uint16_t ptr(int bank, uint16_t table, int idx) const { return w(bank, uint16_t(table + idx * 2)); }
    const uint8_t* raw(int bank, uint16_t addr) const { return data_.data() + off(bank, addr); }

    // 00:3d76 - bitmask decompressor. Reads bank:[start,end).
    std::vector<uint8_t> decompress(int bank, uint16_t start, uint16_t end) const;

private:
    std::vector<uint8_t> data_;
};

} // namespace ffl3
