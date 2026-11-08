// SPDX-License-Identifier: MIT
#pragma once
#include "geometry.h"
#include <cstdint>
#include <span>
#include <vector>
namespace mapeditor::simulation {
struct TerrainTile {
  int category = 0; // 0 blocked; 1 low; 2 middle; 3 high
  bool partial = false, buildable = false;
  bool unbuildable = false, creep = false;
  std::uint16_t blocked = 65535;
  std::array<std::uint8_t, 16> heights{};
  int sightHeight = 0;
};
class Terrain {
public:
  Terrain(int width, int height, std::span<const std::uint16_t> tiles,
          std::span<const std::uint8_t> cv5, std::span<const std::uint8_t> vf4);
  int width() const { return width_; }
  int height() const { return height_; }
  bool walkable(int miniX, int miniY) const;
  // Conservative fast query: every covered build tile must be fully walkable.
  bool fullyWalkableTiles(Point first, Point last) const {
    if (first.x < 0 || first.y < 0 || last.x < first.x || last.y < first.y ||
        last.x >= width_ * 32 || last.y >= height_ * 32)
      return false;
    for (int y = first.y / 32; y <= last.y / 32; ++y)
      for (int x = first.x / 32; x <= last.x / 32; ++x)
        if (tiles_[std::size_t(y) * width_ + x].blocked)
          return false;
    return true;
  }
  const TerrainTile &tile(int x, int y) const;
  int groundHeight(Point pixel) const {
    const auto &value = tile(pixel.x / 32, pixel.y / 32);
    return value.heights[(pixel.y % 32) / 8 * 4 + (pixel.x % 32) / 8];
  }

private:
  int width_, height_;
  std::vector<TerrainTile> tiles_;
};
struct RegionBounds {
  int left = 0, top = 0, right = 0, bottom = 0;
};
struct Region {
  int category = 0, group = 0, tileCount = 0;
  bool reserved = false;
  Point center;
  RegionBounds bounds;
  std::vector<int> walking, blocked;
  bool walkable() const { return category > 0; }
};
class Regions {
public:
  explicit Regions(const Terrain &terrain);
  const std::vector<Region> &entries() const { return regions_; }
  const std::vector<int> &tileLabels() const { return tiles_; }
  const std::vector<int> &cells() const { return cells_; }
  int regionAt(Point pixel) const;
  int regionAtPreferWalkable(Point pixel) const;
  int tileWidth() const { return width_; }
  int tileHeight() const { return height_; }

private:
  int width_, height_;
  std::vector<Region> regions_;
  std::vector<int> tiles_, cells_, preferred_;
};
} // namespace mapeditor::simulation
