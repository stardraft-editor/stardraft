#include "stardraft/area_measurement.h"
#include <algorithm>
#undef NDEBUG
#include <cassert>
#include <cstdio>
using namespace stardraft;
int main() {
    Tileset ts;
    ts.groups.resize(2);
    ts.groups[1].megatiles.fill(1);
    ts.megatiles.resize(2);
    ts.minitileFlags.resize(2);
    ts.minitileFlags[1].fill(Tileset::kWalkable);
    TerrainLayer map{32, 32, std::vector<uint16_t>(32 * 32, 16)};
    std::vector<std::pair<int, int>> p{{2, 2}, {6, 2}, {6, 5}, {2, 5}};
    auto a = measureArea(map, ts, p);
    assert(a.totalNexus() == 1 && a.walkableNexus() == 1 && a.buildableNexus() == 1);
    std::reverse(p.begin(), p.end());
    assert(measureArea(map, ts, p).walkCells == a.walkCells);
    ts.minitileFlags[1][0] = 0;
    a = measureArea(map, ts, p);
    assert(a.walkCells == 180 && a.buildTiles == 0);
    ts.minitileFlags[1].fill(1);
    a = measureArea(map, ts, {{0, 31}, {32, 31}, {32, 32}, {0, 32}});
    assert(a.tiles.size() == 32 && a.buildTiles == 0);
    a = measureArea(map, ts, {{-9, -9}, {4, -9}, {4, 3}, {-9, 3}});
    assert(a.tiles.size() == 12);
    a = measureArea(map, ts, {{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}});
    assert(a.tiles.size() == 7);
    assert(measureArea(map, ts, {}).tiles.empty());
    // Painted masks can include separate patches and holes; they use the same terrain metrics.
    std::vector<uint8_t> mask(size_t(map.width) * map.height, 0);
    for (int y = 2; y < 5; ++y)
        for (int x = 2; x < 6; ++x) mask[size_t(y) * map.width + x] = 1;
    auto painted = measureAreaMask(map, ts, mask);
    assert(painted.totalNexus() == 1 && painted.buildableNexus() == 1);
    mask[size_t(3) * map.width + 3] = 0;   // a hole
    mask[size_t(14) * map.width + 19] = 1; // a disconnected tile
    painted = measureAreaMask(map, ts, mask);
    assert(painted.tiles.size() == 12 && painted.walkCells == 192 && painted.buildTiles == 12);
    assert(std::none_of(painted.tiles.begin(), painted.tiles.end(), [](const auto& t) { return t.x == 3 && t.y == 3; }));
    assert(std::any_of(painted.tiles.begin(), painted.tiles.end(), [](const auto& t) { return t.x == 19 && t.y == 14; }));
    mask[size_t(31) * map.width + 10] = 1;
    painted = measureAreaMask(map, ts, mask);
    assert(painted.tiles.size() == 13 && painted.buildTiles == 12); // reserved bottom edge
    mask.pop_back();
    assert(measureAreaMask(map, ts, mask).tiles.empty());
    std::puts("Area measurement: polygons, clipping, winding, masks, holes, walkability and buildability passed");
}
