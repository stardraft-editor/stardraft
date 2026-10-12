#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;

enum class Era : uint16_t { Badlands, Platform, Install, Ashworld, Jungle, Desert, Ice, Twilight };

// CASC base name of a tileset ("badlands", "platform", ...). StarCraft masks ERA to 3 bits.
const char* tilesetName(uint16_t era);

// One CV5 tile group (52 bytes). MTXM/TILE values are (group << 4) | subtile.
struct TileGroup {
    uint16_t type = 0;  // terrain type / ISOM link
    uint8_t buildability = 0;
    uint8_t groundHeight = 0;
    std::array<uint16_t, 4> edges{};  // left, top, right, bottom
    std::array<uint16_t, 4> extra{};  // remaining editor fields (stack connections etc.)
    std::array<uint16_t, 16> megatiles{};
};

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

// Classic (SD) tileset graphics: tile groups -> megatiles (32x32) -> minitiles (8x8, palette indexed).
struct Tileset {
    std::vector<TileGroup> groups;                     // .cv5
    std::vector<std::array<uint32_t, 16>> megatiles;   // .vx4ex: (minitile << 1) | hflip, 4x4 per megatile
    std::vector<std::array<uint8_t, 64>> minitiles;    // .vr4: 8x8 palette indices
    std::array<Rgb, 256> palette{};                    // .wpe
    std::vector<std::array<uint16_t, 16>> minitileFlags;  // .vf4: per megatile, 4x4 minitile flags

    // CV5 group flags (low byte of the field after the type).
    static constexpr uint8_t kUnbuildable = 0x80;
    // VF4 minitile flags.
    static constexpr uint16_t kWalkable = 0x1;

    bool load(const GameData& data, uint16_t era, std::string& error);
    uint16_t era = 0;  // set by load

    // Megatile index for an MTXM/TILE value, or -1 if out of range.
    int megatileFor(uint16_t tileValue) const;
    // Group for an MTXM/TILE value, or nullptr if out of range.
    const TileGroup* groupFor(uint16_t tileValue) const;
};

}  // namespace stardraft
