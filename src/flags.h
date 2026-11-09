// Event / chest flags (wC760 / wC780).
#pragma once
#include <bitset>
#include <cstdint>

namespace ffl3 {

struct GameFlags {
    std::bitset<256> event;            // wC760, flag n = bit 7-(n&7) of byte n>>3 (00:3cfa)
    std::bitset<256> chest;            // wC780
    bool test(int f) const { return event.test(size_t(f & 0xff)); }
    void loadEventBytes(const uint8_t* b) {   // 32 bytes as laid out in wC760
        for (int n = 0; n < 256; n++) event[size_t(n)] = (b[n >> 3] >> (7 - (n & 7))) & 1;
    }
};

} // namespace ffl3
