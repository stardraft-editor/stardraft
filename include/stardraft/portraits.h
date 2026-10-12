#pragma once
// Unit portraits: StarCraft: Remastered keeps them as WebM videos (VP9 for the SD and HD2 art, VP8 for the
// Carbot skin), 10 frames a second. Decoded to RGBA frames here. GPU-free.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "stardraft/hd_assets.h"

namespace stardraft {

class GameData;

struct PortraitVideo {
    int width = 0, height = 0;
    double fps = 10;
    std::vector<std::vector<uint8_t>> frames;  // RGBA, width x height each
    uint64_t serial = 0;  // a number no other decoded video has (a cache of its pictures keys on it, not on addresses)
};

// Decodes a WebM file's video track (VP8 or VP9; 4:2:0 YUV or 4:4:4 RGB).
bool decodeWebm(std::span<const uint8_t> file, PortraitVideo& out, std::string& error);

// A portrait from the game's files: `path` as portdata.tbl writes it ("tmarine\TMaFid0"), `index` its variant
// (tmafid00, tmafid01, ...). `quality`: SD ("SD\portrait\..."), HD2 ("HD2\portrait\...") or HD ("portrait\...");
// `carbot`: the Carbot skin's (falls back to the normal art).
bool loadPortrait(const GameData& data, const std::string& path, int index, GraphicsQuality quality, bool carbot, PortraitVideo& out,
                  std::string& error);

}  // namespace stardraft
