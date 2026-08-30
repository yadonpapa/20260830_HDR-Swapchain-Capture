// 流用元: 20260611_rawdecklink-signal-player/core/src/crc32.cpp （逐語コピー、改変なし）
#include "crc32.h"
#include <cstring>

namespace {
struct Tables {
    uint32_t t[8][256];
    Tables() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i)
            for (int j = 1; j < 8; ++j)
                t[j][i] = (t[j - 1][i] >> 8) ^ t[0][t[j - 1][i] & 0xFF];
    }
};
const Tables g;
}  // namespace

uint32_t rdl_crc32(uint32_t crc, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (len >= 8) {  // slice-by-8 (little-endian x64)
        uint32_t lo, hi;
        std::memcpy(&lo, p, 4);
        std::memcpy(&hi, p + 4, 4);
        lo ^= crc;
        crc = g.t[7][lo & 0xFF] ^ g.t[6][(lo >> 8) & 0xFF] ^
              g.t[5][(lo >> 16) & 0xFF] ^ g.t[4][lo >> 24] ^
              g.t[3][hi & 0xFF] ^ g.t[2][(hi >> 8) & 0xFF] ^
              g.t[1][(hi >> 16) & 0xFF] ^ g.t[0][hi >> 24];
        p += 8;
        len -= 8;
    }
    while (len--) crc = g.t[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
