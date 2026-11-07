// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/region_route.h>
#include <mapeditor/simulation/regions.h>
using namespace mapeditor::simulation;
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
void word(std::vector<std::uint8_t> &b, int offset, int value) {
  b.at(offset) = value & 255;
  b.at(offset + 1) = (value >> 8) & 255;
}
int main() {
  try {
    std::vector<std::uint8_t> cv(104), vf(96);
    // Hand-authored tables: mega 0 open, mega 1 has three blocked corners,
    // mega 2 blocked. No proprietary data is embedded in the fixture.
    for (int i = 0; i < 16; ++i) {
      word(vf, i * 2, 1);
      word(vf, 32 + i * 2, i < 13 ? 1 : 0);
    }
    for (int i = 0; i < 16; ++i)
      word(cv, 20 + i * 2, 2);
    word(cv, 52 + 20 + 2, 1);
    std::vector<std::uint16_t> tiles(64 * 64, 16);
    Terrain terrain(64, 64, tiles, cv, vf);
    Regions graph(terrain);
    check(graph.entries().size() == 67, "Flat region count");
    check(graph.regionAt({1024, 1024}) == 37 &&
              graph.regionAt({1280, 768}) == 30,
          "Flat region identifiers");
    check(graph.entries()[37].center.x == 1136 &&
              graph.entries()[37].center.y == 1136,
          "Region seed center");
    check(graph.entries()[37].group == 1 && graph.entries()[37].tileCount == 64,
          "Region topology");
    auto neighbor = graph.entries()[37].walking.front();
    auto route = regionRoute(graph, graph.entries()[37].center,
                             graph.entries()[neighbor].center);
    check(route.regions == std::vector<int>({37, neighbor}) &&
              route.fullSize == 2,
          "Adjacent region route");
    auto same = regionRoute(graph, {1024, 1024}, {1025, 1025});
    check(same.regions == std::vector<int>({37}) && same.nodes == 0,
          "Same-region path");
    auto across = regionRoute(graph, {300, 300}, {1800, 1800});
    check(across.regions.front() == graph.regionAt({300, 300}) &&
              across.regions.back() == graph.regionAt({1800, 1800}) &&
              across.nodes > 0,
          "Multi-region path");
    check(graph.tileLabels()[56 * 64 + 7] == 57 &&
              graph.tileLabels()[56 * 64] == 64,
          "Bottom fringe region ordering");
    check(!terrain.walkable(0, 248) && terrain.walkable(20, 248) &&
              !terrain.walkable(100, 252),
          "Reserved UI map footprint");
    check(graph.regionAt({-1, 0}) == -1 && !terrain.walkable(-1, 0),
          "Map bounds");
    int count = 0;
    for (const auto &r : graph.entries())
      count += r.tileCount;
    check(count == 4096, "Region tile coverage");
    tiles[10 * 64 + 10] = 17;
    Terrain partial(64, 64, tiles, cv, vf);
    Regions partialGraph(partial);
    check(!partial.walkable(43, 43), "Partial minitile collision mask");
    check(
        partialGraph.entries()[partialGraph.regionAt({43 * 8 + 4, 43 * 8 + 4})]
            .walkable(),
        "Isolated partial-region fallback differs from the compatibility "
        "target");
    tiles[9 * 64 + 9] = 0;
    Terrain blocked(64, 64, tiles, cv, vf);
    Regions blockedGraph(blocked);
    check(!blockedGraph
               .entries()[blockedGraph.regionAt({9 * 32 + 16, 9 * 32 + 16})]
               .walkable(),
          "Blocked terrain region");
    bool rejected = false;
    try {
      Terrain bad(64, 64, {}, cv, vf);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "Malformed terrain size accepted");
    std::cout << "PASS: native terrain regions and partial-cell behavior\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
