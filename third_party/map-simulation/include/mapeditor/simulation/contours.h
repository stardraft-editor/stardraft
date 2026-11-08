// SPDX-License-Identifier: MIT
#pragma once
#include "dat.h"
#include "regions.h"
#include <array>
namespace mapeditor::simulation {
// Directed boundaries between walkable and blocked minitiles. The normal
// points into blocked terrain (north, east, south, west). Endpoints describe
// inclusive pixel coordinates, with a one-pixel extension at convex corners.
struct TerrainEdge {
  int line = 0, first = 0, last = 0;
  int corners = 0;
  bool operator==(const TerrainEdge &) const = default;
};
class Contours {
public:
  explicit Contours(const Terrain &);
  const std::array<std::vector<TerrainEdge>, 4> &edges() const {
    return edges_;
  }
  // Wall clearance only. Callers check map bounds and center walkability,
  // including any dynamic creep, separately.
  bool permits(Point center, const dat::Extents &) const;
  // Ground movement reserves the complete axis-aligned footprint sweep.
  // Both endpoint placements may be clear while this sweep touches a corner.
  bool permitsMovement(Point from, Point to, const dat::Extents &) const;

private:
  std::array<std::vector<TerrainEdge>, 4> edges_;
};
} // namespace mapeditor::simulation
