// SPDX-License-Identifier: MIT
#pragma once
#include "contours.h"
#include "placement.h"
#include "regions.h"
#include <optional>
#include <span>
namespace mapeditor::simulation {
// Inclusive configuration-space rectangle. Obstacles have already been
// expanded by the moving unit's footprint; domain contains legal centers.
struct PixelBox {
  int left, top, right, bottom;
  bool contains(Point p) const;
  bool operator==(const PixelBox &) const = default;
};
// N/E/S/W/NE/SE/SW/NW continuation mask. A column closes an exit when the
// explored union covers both the source row and its neighboring pixel.
unsigned unexploredLocalDirections(Point, std::span<const PixelBox>);
struct NavigationWall {
  int normal = 0, line = 0, first = 0, last = 0;
  int firstExtension = -1, lastExtension = 1;
  std::optional<Point> boundarySource;
};
struct LocalExpansion {
  std::vector<Point> frontier;
  std::vector<unsigned> allowed;
  std::vector<PixelBox> areas;
  // Closed sweep junctions can terminate contact paths without opening a new
  // search direction.
  std::vector<Point> terminal;
};
class LocalSpace {
public:
  // Immutable terrain precedes dynamic bodies in the obstacle list. Terrain
  // constrains exits even where a sweep retains virtual unit junction ports.
  LocalSpace(PixelBox domain, std::vector<PixelBox> obstacles,
             std::optional<std::vector<NavigationWall>> navigation = {},
             std::optional<std::size_t> terrainObstacleCount = {});
  bool clear(PixelBox) const;
  // Bits 0..3 select N/E/S/W faces, bits 4..7 select NE/SE/SW/NW.
  LocalExpansion expand(Point, int radius = 64,
                        unsigned directions = 255) const;

private:
  PixelBox domain_;
  std::vector<PixelBox> obstacles_;
  std::size_t terrainObstacleCount_;
  std::vector<NavigationWall> navigation_;
};
LocalSpace groundSpace(const Terrain &, const Contours &, const dat::Extents &,
                       std::span<const CollisionBody> bodies = {},
                       std::span<const std::uint8_t> creep = {});
// Immutable terrain contribution, before terrain/unit wall unions are formed.
// Each request appends current units and performs the complete union afresh.
struct GroundGeometry {
  PixelBox domain;
  dat::Extents shape;
  std::vector<PixelBox> obstacles;
  std::vector<NavigationWall> navigation;
  // A departing center can follow a body face it already touches. Opening
  // that face leaves the body's interior and terrain obstacles blocked.
  LocalSpace withBodies(std::span<const CollisionBody>,
                        std::optional<Point> departure = {}) const;
};
GroundGeometry groundGeometry(const Terrain &, const Contours &,
                              const dat::Extents &,
                              std::span<const std::uint8_t> creep = {});
std::optional<Point> adjustGroundTarget(const Terrain &, const Regions &,
                                        const dat::Extents &, Point target);
// Order destinations require full footprint clearance. Regional path targets
// have a separate, deliberately coarser adjustment contract.
std::optional<Point> projectGroundOrderTarget(const Terrain &, const Contours &,
                                             const Regions &,
                                             const dat::Extents &, Point target,
                                             Point source);
struct OrderGroupMember {
  Point position;
  dat::Extents bounds;
  bool ground = true;
};
struct OrderGroupProjection {
  Point groundAnchor;
  bool insideSelection = false, keepsOffsets = false;
  std::vector<Point> destinations;
};
// Command admission geometry, independent of subsequent unit route planning.
std::optional<OrderGroupProjection>
projectGroupOrderTargets(const Terrain &, const Contours &, const Regions &,
                         std::span<const OrderGroupMember>, Point requested);
struct LocalRouteResult {
  struct RegionalMemory {
    std::vector<int> regions;
    std::size_t cursor = 0;
    int fullSize = 0;
  } retained;
  std::vector<Point> points;
  std::vector<int> itinerary;
  bool itineraryTruncated = false;
  Point source, destination;
  bool reached = false, stuck = false;
  int nodes = 0, peakFrontier = 0;
};
// Local coordinates consume two words and regional IDs one word in the shared
// continuation budget. Retain consumed regional history and its end sentinel.
void limitRouteStorage(LocalRouteResult &);
struct LocalRouteCandidate {
  Point position;
  int parent = -1, region = -1;
  std::int64_t cost = 0, remaining = 0;
  bool visited = false;
  unsigned directions = 0;
};
struct LocalRouteDiagnostics {
  struct Expansion {
    Point source;
    unsigned directions = 0;
    LocalExpansion result;
  };
  std::vector<Expansion> expansions;
  std::vector<Point> dequeued;
  std::vector<LocalRouteCandidate> candidates;
  Point searchTarget, destination;
  int destinationRegion = -1;
  int remainingCandidates = 0, selected = -1;
  bool weightedFallback = false;
};
LocalRouteResult planGroundRoute(const LocalSpace &, const Regions &,
                                 Point source, Point target,
                                 std::optional<PixelBox> contact = {},
                                 std::optional<Point> adjustedTarget = {},
                                 LocalRouteDiagnostics *diagnostics = nullptr,
                                 const LocalRouteResult *previous = nullptr);
// Experimental local planner. Arbitrary budget-limited fallback, continued
// paths and dynamic collision response still need full compatibility checks.
// Synthetic first-path checks do not establish complete pathfinding parity;
// production world routing must wait for the full compatibility gate.
std::vector<Point> localGroundRoute(const LocalSpace &, const Regions &,
                                    Point source, Point target,
                                    std::optional<PixelBox> contact = {},
                                    std::optional<Point> adjustedTarget = {});
} // namespace mapeditor::simulation
