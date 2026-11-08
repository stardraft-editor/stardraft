// SPDX-License-Identifier: MIT
#pragma once
#include "dat.h"
#include "regions.h"
#include <optional>
#include <span>
namespace mapeditor::simulation {
struct CollisionBody {
  std::uint32_t key = 0;
  Point position;
  dat::Extents bounds;
  bool flying = false, collides = true;
};
struct PlacementRequest {
  Point origin;
  dat::Extents bounds;
  bool flying = false;
  std::uint32_t ignoreKey = ~0U;
};
class Placement {
public:
  Placement(const Terrain &terrain, const Regions &regions,
            std::span<const CollisionBody> bodies)
      : terrain_(terrain), regions_(regions), bodies_(bodies) {}
  std::optional<Point> find(const PlacementRequest &,
                            bool terrainDisplaces = false) const;
  bool terrainFits(Point, const dat::Extents &) const;

private:
  const Terrain &terrain_;
  const Regions &regions_;
  std::span<const CollisionBody> bodies_;
};
} // namespace mapeditor::simulation
