// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/contours.h>
#include <tuple>
namespace mapeditor::simulation {
Contours::Contours(const Terrain &terrain) {
  const int width = terrain.width() * 4, height = terrain.height() * 4;
  constexpr std::array<Point, 4> normals{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
  // Extract runs independently on each scan line. This avoids contour tracing,
  // mutation of the terrain raster and special handling of connected loops.
  for (int normal = 0; normal < 4; ++normal) {
    bool horizontal = normal % 2 == 0;
    int lines = horizontal ? height : width;
    int length = horizontal ? width : height;
    auto coordinate = [&](int line, int along) {
      return horizontal ? Point{along, line} : Point{line, along};
    };
    auto walkable = [&](int line, int along) {
      auto p = coordinate(line, along);
      return terrain.walkable(p.x, p.y);
    };
    auto boundary = [&](int line, int along) {
      auto p = coordinate(line, along), n = normals[normal];
      return terrain.walkable(p.x, p.y) &&
             !terrain.walkable(p.x + n.x, p.y + n.y);
    };
    for (int line = 0; line < lines; ++line)
      for (int along = 0; along < length;) {
        if (!boundary(line, along)) {
          ++along;
          continue;
        }
        int begin = along++;
        while (along < length && boundary(line, along))
          ++along;
        bool firstConvex = !walkable(line, begin - 1);
        bool lastConvex = !walkable(line, along);
        int firstNormal = horizontal ? 3 : 0;
        int lastNormal = horizontal ? 1 : 2;
        if (!firstConvex)
          firstNormal = (firstNormal + 2) % 4;
        if (!lastConvex)
          lastNormal = (lastNormal + 2) % 4;
        edges_[normal].push_back(
            {line * 8 + (normal == 0 || normal == 3 ? -1 : 8),
             begin * 8 - int(firstConvex), along * 8 - 1 + int(lastConvex),
             firstNormal | (lastNormal << 2) | (firstConvex ? 0 : 16) |
                 (lastConvex ? 0 : 32)});
      }
    std::sort(edges_[normal].begin(), edges_[normal].end(), [](auto a, auto b) {
      return std::tie(a.line, a.first) < std::tie(b.line, b.first);
    });
  }
}
bool Contours::permits(Point center, const dat::Extents &shape) const {
  const std::array<int, 4> near{{center.y, center.x, center.y, center.x}};
  const std::array<int, 4> reach{
      {shape.top, shape.right, shape.bottom, shape.left}};
  for (int normal = 0; normal < 4; ++normal) {
    auto along = normal % 2 == 0 ? center.x : center.y;
    auto low = normal % 2 == 0 ? shape.right : shape.bottom;
    auto high = normal % 2 == 0 ? shape.left : shape.top;
    int begin = normal == 0 || normal == 3 ? near[normal] - reach[normal]
                                           : near[normal] + 1;
    int end = normal == 0 || normal == 3 ? near[normal] - 1
                                         : near[normal] + reach[normal];
    const auto &edges = edges_[normal];
    auto first = std::lower_bound(
        edges.begin(), edges.end(), begin,
        [](const auto &edge, int value) { return edge.line < value; });
    for (; first != edges.end() && first->line <= end; ++first)
      if (along >= first->first - low && along <= first->last + high)
        return false;
  }
  return true;
}
bool Contours::permitsMovement(Point from, Point to,
                               const dat::Extents &shape) const {
  dat::Extents sweep{shape.left + std::max(0, from.x - to.x),
                     shape.top + std::max(0, from.y - to.y),
                     shape.right + std::max(0, to.x - from.x),
                     shape.bottom + std::max(0, to.y - from.y)};
  return permits(from, sweep);
}

} // namespace mapeditor::simulation
