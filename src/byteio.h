#pragma once

// Little-endian helpers, independent of host byte order.

#include <cstdint>
#include <span>
#include <vector>

namespace stardraft {

inline uint16_t readU16(std::span<const uint8_t> b, size_t off) {
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

inline uint32_t readU32(std::span<const uint8_t> b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

inline void appendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

inline void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

}  // namespace stardraft
