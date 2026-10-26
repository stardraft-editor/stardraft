#pragma once

// Pictures for palettes: what a terrain or a doodad looks like, drawn on the CPU (classic SD art, or
// the HD2 art when given). RGBA Sprites with straight alpha.

#include <cstdint>
#include <string>

#include "stardraft/sprites.h"

namespace stardraft {

class GameData;
struct DoodadType;
struct HdTiles;
struct Tileset;

// A patch of a terrain: 4x2 tiles of its solid tile group, with varied tile variants. Empty (width 0)
// if the tileset's rules have no solid group for it.
Sprite terrainPicture(const Tileset& tileset, uint16_t terrain, const HdTiles* hd = nullptr);

// The tiles of a map (MTXM values, row-major, `mapWidth` wide) in the rectangle (x, y, w, h) of tiles: SD, or HD2
// (64 pixels per tile) when `hd` is given. Tiles outside the map are transparent.
Sprite mapTilesPicture(const Tileset& tileset, const std::vector<uint16_t>& tiles, int mapWidth, int x, int y, int w, int h,
                       const HdTiles* hd = nullptr);

// A doodad: its tiles (transparent where it has none) and its overlay sprite on top.
Sprite doodadPicture(const DoodadType& doodad, const Tileset& tileset, UnitSprites& sprites, const HdTiles* hd = nullptr);
// Tiles as a picture: `tiles` row by row, w x h (0xFFFF: no tile there, transparent).
Sprite tilesSprite(const Tileset& ts, int w, int h, const std::vector<uint16_t>& tiles, const HdTiles* hd = nullptr);

}  // namespace stardraft
