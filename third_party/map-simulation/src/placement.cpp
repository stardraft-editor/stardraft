// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/placement.h>
#include <stdexcept>
namespace mapeditor::simulation {
namespace {
int down8(int n) { return (n >= 0 ? n / 8 : (n - 7) / 8) * 8; }
int up8(int n) { return (n >= 0 ? (n + 7) / 8 : n / 8) * 8; }
bool overlaps(Point a, const dat::Extents &x, Point b, const dat::Extents &y) {
  return a.x - x.left <= b.x + y.right && a.x + x.right >= b.x - y.left &&
         a.y - x.top <= b.y + y.bottom && a.y + x.bottom >= b.y - y.top;
}
} // namespace
bool Placement::terrainFits(Point p, const dat::Extents &e) const {
  if (p.x - e.left < 0 || p.y - e.top < 0 ||
      p.x + e.right >= terrain_.width() * 32 ||
      p.y + e.bottom >= terrain_.height() * 32 - 32)
    return false;
  for (int y = (p.y - e.top) / 8; y <= (p.y + e.bottom) / 8; ++y)
    for (int x = (p.x - e.left) / 8; x <= (p.x + e.right) / 8; ++x)
      if (!terrain_.walkable(x, y))
        return false;
  return true;
}
std::optional<Point> Placement::find(const PlacementRequest &request,
                                     bool terrainDisplaces) const {
  auto shape = request.bounds;
  auto origin = request.origin;
  if (origin.x < 0 || origin.y < 0 || origin.x >= terrain_.width() * 32 ||
      origin.y >= terrain_.height() * 32)
    throw std::out_of_range("Placement origin outside map");
  for (int extent : {shape.left, shape.top, shape.right, shape.bottom})
    if (extent < 0 || extent > 65535)
      throw std::out_of_range("Invalid unit collision extent");
  auto inBounds = [&](Point p) {
    return p.x >= shape.left && p.y >= shape.top &&
           p.x + shape.right < terrain_.width() * 32 &&
           p.y + shape.bottom < terrain_.height() * 32 - 32;
  };
  auto collision = [&](Point p) -> const CollisionBody * {
    for (const auto &body : bodies_)
      if (body.collides && body.key != request.ignoreKey &&
          body.flying == request.flying &&
          overlaps(p, shape, body.position, body.bounds))
        return &body;
    return nullptr;
  };
  auto *initial = collision(origin);
  if (!initial && inBounds(origin)) {
    if (request.flying || terrainFits(origin, shape))
      return origin;
    if (!terrainDisplaces)
      return std::nullopt;
  }
  int source = regions_.regionAt(origin);
  auto reachable = [&](Point p) {
    if (request.flying)
      return true;
    int destination = regions_.regionAt(p);
    return source >= 0 && destination >= 0 &&
           regions_.entries()[source].group ==
               regions_.entries()[destination].group;
  };
  int left = std::max(0, origin.x - 128), top = std::max(0, origin.y - 128),
      right = std::min(terrain_.width() * 32 - 1, origin.x + 127),
      bottom = std::min(terrain_.height() * 32 - 33, origin.y + 127);
  int radiusX =
      initial ? std::max(8, shape.left + initial->bounds.right + 2) : 8;
  int radiusY =
      initial ? std::max(8, shape.top + initial->bounds.bottom + 2) : 8;
  auto scan = [&](bool horizontal, int fixed, int begin, int end,
                  int direction) -> std::optional<Point> {
    for (int value = begin; direction > 0 ? value <= end : value >= end;) {
      Point p = horizontal ? Point{value, fixed} : Point{fixed, value};
      if (inBounds(p)) {
        if (auto *blocker = collision(p)) {
          int center = horizontal ? blocker->position.x : blocker->position.y;
          int before = horizontal ? blocker->bounds.left : blocker->bounds.top;
          int after =
              horizontal ? blocker->bounds.right : blocker->bounds.bottom;
          int extent = horizontal ? (direction > 0 ? shape.left : shape.right)
                                  : (direction > 0 ? shape.top : shape.bottom);
          int edge = center + (direction > 0 ? after + 1 + extent
                                             : -before - 1 - extent);
          // Reverse scans retain the reference's reflected eight-pixel
          // rounding.
          int jump = direction > 0 ? up8(edge) : 2 * edge - up8(edge);
          if (direction > 0 ? jump <= value : jump >= value)
            throw std::runtime_error("Placement scan failed to advance");
          value = jump;
          continue;
        }
        if (reachable(p) && (request.flying || terrainFits(p, shape)))
          return p;
      }
      value += direction * 8;
    }
    return std::nullopt;
  };
  for (; !(origin.x - radiusX < left && origin.y - radiusY < top &&
           origin.x + radiusX > right && origin.y + radiusY > bottom);
       radiusX += 16, radiusY += 16) {
    int x0 = std::max(left, down8(origin.x - radiusX)),
        x1 = std::min(right, up8(origin.x + radiusX));
    int y0 = std::max(top, down8(origin.y - radiusY)),
        y1 = std::min(bottom, up8(origin.y + radiusY));
    int width = shape.left + shape.right + 1;
    int bottomBegin = x0 + (radiusX * 2 > width * 2 ? up8(width) : 0);
    if (auto result = scan(true, y1, bottomBegin, x1, 1))
      return result;
    if (auto result = scan(false, x1, y1, y0, -1))
      return result;
    if (auto result = scan(true, y0, x1, x0, -1))
      return result;
    if (auto result = scan(false, x0, y0, y1, 1))
      return result;
  }
  return std::nullopt;
}
} // namespace mapeditor::simulation
