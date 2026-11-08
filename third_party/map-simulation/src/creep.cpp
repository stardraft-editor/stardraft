// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <mapeditor/simulation/creep.h>
#include <mapeditor/simulation/local_route.h>
#include <stdexcept>
namespace mapeditor::simulation {
Creep::Creep(const Terrain &terrain, std::span<const std::uint8_t> initial)
    : width_(terrain.width()), height_(terrain.height()) {
  if (!initial.empty()) {
    if (initial.size() != std::size_t(width_) * height_ ||
        std::any_of(initial.begin(), initial.end(),
                    [](auto v) { return v > 1; }))
      throw std::invalid_argument("Invalid initial creep mask");
    cells_.assign(initial.begin(), initial.end());
    return;
  }
  cells_.reserve(std::size_t(width_) * height_);
  for (int y = 0; y < height_; ++y)
    for (int x = 0; x < width_; ++x)
      cells_.push_back(terrain.tile(x, y).creep);
}
bool Creep::at(int x, int y) const {
  return x >= 0 && y >= 0 && x < width_ && y < height_ &&
         cells_[std::size_t(y) * width_ + x];
}
void Creep::establish(const Terrain &terrain, Point center, Point footprint,
                      bool rounded, std::span<const std::uint8_t> occupied) {
  if (terrain.width() != width_ || terrain.height() != height_ ||
      occupied.size() != cells_.size())
    throw std::invalid_argument("Creep ground cover dimensions differ");
  if (footprint.x < 32 || footprint.y < 32 || footprint.x % 32 ||
      footprint.y % 32)
    throw std::invalid_argument("Invalid creep ground cover footprint");
  int left = center.x / 32 - footprint.x / 64;
  int top = center.y / 32 - footprint.y / 64;
  PixelBox body{left, top, left + footprint.x / 32 - 1,
                top + footprint.y / 32 - 1};
  PixelBox wide{body.left - 2, body.top, body.right + 2, body.bottom};
  PixelBox tall{body.left - 1, body.top - 1, body.right + 1, body.bottom + 1};
  for (std::size_t id = 0; id < cells_.size(); ++id) {
    int x = int(id % width_), y = int(id / width_);
    Point tile{x, y};
    bool covered = rounded ? wide.contains(tile) || tall.contains(tile)
                           : body.contains(tile);
    if (!covered || occupied[id])
      continue;
    const auto &ground = terrain.tile(x, y);
    if (!ground.unbuildable && !ground.partial &&
        (y == height_ - 1 || !terrain.tile(x, y + 1).unbuildable))
      cells_[id] = 1;
  }
}
void Creep::spread(const Terrain &terrain, Point center, Point footprint,
                   bool provider, std::span<const std::uint8_t> occupied,
                   Random &random) {
  if (terrain.width() != width_ || terrain.height() != height_ ||
      occupied.size() != cells_.size())
    throw std::invalid_argument("Creep terrain/occupancy dimensions differ");
  if (footprint.x < 32 || footprint.y < 32 || footprint.x % 32 ||
      footprint.y % 32)
    throw std::invalid_argument("Invalid creep footprint");
  int x0 = center.x / 32 - footprint.x / 64,
      y0 = center.y / 32 - footprint.y / 64;
  int x1 = x0 + footprint.x / 32, y1 = y0 + footprint.y / 32;
  if (x0 < 0 || y0 < 0 || x1 > width_ || y1 > height_)
    throw std::out_of_range("Creep provider outside map");
  auto eligible = [&](int x, int y) {
    const auto &tile = terrain.tile(x, y);
    return !tile.unbuildable && !tile.partial &&
           (y == height_ - 1 || !terrain.tile(x, y + 1).unbuildable);
  };
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x)
      if (eligible(x, y))
        cells_[std::size_t(y) * width_ + x] = 1;
  while (grow(terrain, center, footprint, provider, occupied, random).grew) {
  }
}
Creep::Growth Creep::grow(const Terrain &terrain, Point center, Point footprint,
                          bool provider, std::span<const std::uint8_t> occupied,
                          Random &random) {
  if (terrain.width() != width_ || terrain.height() != height_ ||
      occupied.size() != cells_.size())
    throw std::invalid_argument("Creep terrain/occupancy dimensions differ");
  if (footprint.x < 32 || footprint.y < 32 || footprint.x % 32 ||
      footprint.y % 32)
    throw std::invalid_argument("Invalid creep footprint");
  int x0 = center.x / 32 - footprint.x / 64,
      y0 = center.y / 32 - footprint.y / 64;
  int x1 = x0 + footprint.x / 32, y1 = y0 + footprint.y / 32;
  if (x0 < 0 || y0 < 0 || x1 > width_ || y1 > height_)
    throw std::out_of_range("Creep provider outside map");
  auto eligible = [&](int x, int y) {
    const auto &tile = terrain.tile(x, y);
    return !tile.unbuildable && !tile.partial &&
           (y == height_ - 1 || !terrain.tile(x, y + 1).unbuildable);
  };
  int left = provider ? std::max(0, (center.x - 320) / 32) : x0;
  int top = provider ? std::max(0, (center.y - 200) / 32) : y0;
  int right = provider ? std::min(width_ - 1, (center.x + 320) / 32) : x1 - 1;
  int bottom = provider ? std::min(height_ - 1, (center.y + 200) / 32) : y1 - 1;
  Growth result;
  std::array<std::vector<std::size_t>, 8> choices;
  for (int y = top; y <= bottom; ++y)
    for (int x = left; x <= right; ++x) {
      auto id = std::size_t(y) * width_ + x;
      if (cells_[id] || !eligible(x, y))
        continue;
      if (provider) {
        auto dx = std::int64_t(x * 32 - center.x + 16),
             dy = std::int64_t(y * 32 - center.y + 16);
        if (dx * dx * 25 + dy * dy * 64 > 320 * 320 * 25)
          continue;
      }
      int neighbors = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (dx || dy)
            neighbors += at(x + dx, y + dy);
      if (neighbors) {
        if (occupied[id])
          result.occupied = true;
        else
          choices[neighbors - 1].push_back(id);
      }
    }
  auto bucket = std::find_if(choices.rbegin(), choices.rend(),
                             [](const auto &v) { return !v.empty(); });
  if (bucket == choices.rend())
    return result;
  cells_[(*bucket)[(random.next() >> 4) % bucket->size()]] = 1;
  result.grew = true;
  return result;
}
} // namespace mapeditor::simulation
