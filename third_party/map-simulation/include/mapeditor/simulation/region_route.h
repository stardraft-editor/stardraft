// SPDX-License-Identifier: MIT
#pragma once
#include "regions.h"
namespace mapeditor::simulation {
struct RegionRoute {
  std::vector<int> regions;
  int fullSize = 0, nodes = 0, peakFrontier = 0;
};
// Bounded region-level route planning. This does not generate a unit-clearance
// short path; callers must still plan around terrain edges and unit occupancy.
RegionRoute regionRoute(const Regions &, Point source, Point destination);
} // namespace mapeditor::simulation
