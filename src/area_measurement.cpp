#include "stardraft/area_measurement.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace stardraft {
namespace {
void appendMeasuredTile(MeasuredArea& result, const TerrainLayer& map, const Tileset& ts, int x, int y) {
    const auto value = map.tiles[size_t(y) * map.width + x];
    const auto* group = ts.groupFor(value);
    const int mega = ts.megatileFor(value);
    uint16_t walk = 0;
    if (mega >= 0 && size_t(mega) < ts.minitileFlags.size())
        for (int k = 0; k < 16; ++k)
            if (ts.minitileFlags[size_t(mega)][size_t(k)] & Tileset::kWalkable) walk |= uint16_t(1u << k);
    const bool edge = y == map.height - 1 || (y == map.height - 2 && (x < 5 || x >= map.width - 5));
    const bool build = group && !(group->buildability & Tileset::kUnbuildable) && walk == 0xffff && !edge;
    result.tiles.push_back({x, y, walk, build});
    result.walkCells += std::popcount(walk);
    result.buildTiles += build;
}
} // namespace

MeasuredArea measureArea(const TerrainLayer& map, const Tileset& ts, const std::vector<std::pair<int, int>>& points) {
    MeasuredArea result;
    if (points.size() < 3 || map.tiles.size() != size_t(map.width) * map.height) return result;
    int top = map.height, bottom = 0;
    for (auto [x, y] : points) top = std::min(top, y), bottom = std::max(bottom, y);
    top = std::clamp(top, 0, int(map.height)), bottom = std::clamp(bottom, 0, int(map.height));
    std::vector<double> crossings;
    for (int y = top; y < bottom; ++y) {
        crossings.clear();
        const double scan = y + 0.5;
        for (size_t i = 0; i < points.size(); ++i) {
            auto [ax, ay] = points[i];
            auto [bx, by] = points[(i + 1) % points.size()];
            if ((ay > scan) != (by > scan)) crossings.push_back(ax + (scan - ay) * (double(bx) - ax) / (double(by) - ay));
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            // Clamp doubles before conversion: arbitrary outlines need not be inside the map.
            const int left = int(std::clamp(std::ceil(crossings[i] - 0.5), 0.0, double(map.width)));
            const int right = int(std::clamp(std::ceil(crossings[i + 1] - 0.5), 0.0, double(map.width)));
            for (int x = left; x < right; ++x) {
                appendMeasuredTile(result, map, ts, x, y);
            }
        }
    }
    return result;
}

MeasuredArea measureAreaMask(const TerrainLayer& map, const Tileset& ts, const std::vector<uint8_t>& mask) {
    MeasuredArea result;
    if (map.tiles.size() != size_t(map.width) * map.height || mask.size() != map.tiles.size()) return result;
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x) {
            const size_t index = size_t(y) * map.width + x;
            if (!mask[index]) continue;
            appendMeasuredTile(result, map, ts, x, y);
        }
    return result;
}

} // namespace stardraft
