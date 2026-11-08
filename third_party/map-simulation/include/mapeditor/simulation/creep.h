// SPDX-License-Identifier: MIT
#pragma once
#include "random.h"
#include "regions.h"
namespace mapeditor::simulation {
class Creep {
public:
  explicit Creep(const Terrain &terrain,
                 std::span<const std::uint8_t> initial = {});
  bool at(int x, int y) const;
  const std::vector<std::uint8_t> &cells() const { return cells_; }
  // Establish a completed building's ground cover without a growth draw.
  // The caller supplies occupancy from other buildings only.
  void establish(const Terrain &, Point center, Point footprint, bool rounded,
                 std::span<const std::uint8_t> occupied);
  // Complete initial spread, including deterministic growth choices. Occupancy
  // is caller-owned tile data, so copies never retain mutable pointers to
  // units.
  void spread(const Terrain &, Point center, Point footprint, bool provider,
              std::span<const std::uint8_t> occupied, Random &);
  struct Growth {
    bool grew = false, occupied = false;
  };
  Growth grow(const Terrain &, Point center, Point footprint, bool provider,
              std::span<const std::uint8_t> occupied, Random &);

private:
  int width_, height_;
  std::vector<std::uint8_t> cells_;
};
} // namespace mapeditor::simulation
