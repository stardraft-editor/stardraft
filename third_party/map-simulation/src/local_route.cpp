// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mapeditor/simulation/local_route.h>
#include <mapeditor/simulation/region_route.h>
#include <stdexcept>
#include <tuple>
namespace mapeditor::simulation {
namespace {
PixelBox between(Point a, Point b) {
  return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x),
          std::max(a.y, b.y)};
}
bool overlaps(PixelBox a, PixelBox b) {
  return a.left <= b.right && a.right >= b.left && a.top <= b.bottom &&
         a.bottom >= b.top;
}
bool valid(PixelBox b) { return b.left <= b.right && b.top <= b.bottom; }
constexpr std::array<Point, 8> neighborSteps{
    {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};
template <class Predicate>
std::optional<Point> squareRings(Point center, int step, int rings,
                                 Predicate eligible) {
  if (eligible(center))
    return center;
  for (int radius = step; radius <= rings * step; radius += step) {
    for (int offset = -radius; offset <= radius; offset += step) {
      Point p{center.x + offset, center.y - radius};
      if (eligible(p))
        return p;
    }
    for (int offset = -radius + step; offset <= radius; offset += step) {
      Point p{center.x + radius, center.y + offset};
      if (eligible(p))
        return p;
    }
    for (int offset = radius - step; offset >= -radius; offset -= step) {
      Point p{center.x + offset, center.y + radius};
      if (eligible(p))
        return p;
    }
    for (int offset = radius - step; offset > -radius; offset -= step) {
      Point p{center.x - radius, center.y + offset};
      if (eligible(p))
        return p;
    }
  }
  return {};
}
Point destinationInRegion(const Regions &regions, int id, Point desired) {
  const auto &region = regions.entries()[id];
  Point tile{
      std::clamp(desired.x, region.bounds.left * 32, region.bounds.right * 32) /
          32,
      std::clamp(desired.y, region.bounds.top * 32, region.bounds.bottom * 32) /
          32};
  auto result = squareRings(tile, 1, 7, [&](Point p) {
    return p.x >= 0 && p.y >= 0 && p.x < regions.tileWidth() &&
           p.y < regions.tileHeight() &&
           regions.regionAtPreferWalkable({p.x * 32, p.y * 32}) == id;
  });
  return result ? Point{result->x * 32 + 16, result->y * 32 + 16}
                : region.center;
}
} // namespace
bool PixelBox::contains(Point p) const {
  return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
}
unsigned unexploredLocalDirections(Point origin,
                                   std::span<const PixelBox> visited) {
  unsigned result = 0;
  for (unsigned i = 0; i < neighborSteps.size(); ++i) {
    Point p{origin.x + neighborSteps[i].x, origin.y + neighborSteps[i].y};
    // Adjacent sweeps may cover the source row and its neighbor in different
    // rectangles of the same column. A covered row one pixel away alone
    // does not close the continuation.
    bool sourceCovered = false, neighborCovered = false;
    for (auto b : visited) {
      sourceCovered |= b.contains({p.x, origin.y});
      neighborCovered |= b.contains(p);
      if (sourceCovered && neighborCovered)
        break;
    }
    if (!sourceCovered || !neighborCovered)
      result |= 1U << i;
  }
  return result;
}
LocalSpace::LocalSpace(PixelBox domain, std::vector<PixelBox> obstacles,
                       std::optional<std::vector<NavigationWall>> navigation,
                       std::optional<std::size_t> terrainObstacleCount)
    : domain_(domain), obstacles_(std::move(obstacles)),
      terrainObstacleCount_(terrainObstacleCount.value_or(obstacles_.size())),
      navigation_(navigation ? std::move(*navigation)
                             : std::vector<NavigationWall>{}) {
  if (!navigation)
    for (auto box : obstacles_) {
      navigation_.push_back({0, box.bottom, box.left, box.right});
      navigation_.push_back({1, box.left, box.top, box.bottom});
      navigation_.push_back({2, box.top, box.left, box.right});
      navigation_.push_back({3, box.right, box.top, box.bottom});
    }
  if (terrainObstacleCount_ > obstacles_.size() || !valid(domain_) ||
      domain_.left < 0 || domain_.top < 0 || domain_.right > 8191 ||
      domain_.bottom > 8191 ||
      std::any_of(obstacles_.begin(), obstacles_.end(),
                  [](auto b) { return !valid(b); }) ||
      std::any_of(navigation_.begin(), navigation_.end(), [](auto b) {
        return b.normal < 0 || b.normal > 3 || b.first > b.last;
      }))
    throw std::invalid_argument("Invalid local navigation geometry");
  // Footprint expansion can join terrain edges that were separate in the
  // minitile raster. Only the endpoints of their union are navigation ports.
  std::sort(navigation_.begin(), navigation_.end(), [](auto a, auto b) {
    return std::tie(a.normal, a.line, a.first) <
           std::tie(b.normal, b.line, b.first);
  });
  std::vector<NavigationWall> merged;
  for (auto wall : navigation_) {
    if (!merged.empty() && merged.back().normal == wall.normal &&
        merged.back().line == wall.line &&
        wall.first <= merged.back().last + 1) {
      if (wall.last > merged.back().last) {
        merged.back().last = wall.last;
        merged.back().lastExtension = wall.lastExtension;
      }
      if (wall.boundarySource)
        merged.back().boundarySource = wall.boundarySource;
    } else
      merged.push_back(wall);
  }
  navigation_ = std::move(merged);
}
bool LocalSpace::clear(PixelBox area) const {
  return valid(area) && domain_.contains({area.left, area.top}) &&
         domain_.contains({area.right, area.bottom}) &&
         std::none_of(obstacles_.begin(), obstacles_.end(),
                      [&](auto b) { return overlaps(area, b); });
}
LocalExpansion LocalSpace::expand(Point origin, int radius,
                                  unsigned allowed) const {
  if (radius < 1 || radius > 8192 || !clear(between(origin, origin)))
    throw std::invalid_argument("Invalid local expansion source");
  PixelBox window{std::max(domain_.left, origin.x - radius),
                  std::max(domain_.top, origin.y - radius),
                  std::min(domain_.right, origin.x + radius),
                  std::min(domain_.bottom, origin.y + radius)};
  const auto horizon = window;
  LocalExpansion result;
  // Clip the window to faces with unexplored space. Expanding a quadrant may
  // still expose a port along a cardinal boundary already visited at its root.
  auto has = [&](int bit) { return bool(allowed & (1U << bit)); };
  if (!has(0) && !has(4) && !has(7))
    window.top = origin.y;
  if (!has(1) && !has(4) && !has(5))
    window.right = origin.x;
  if (!has(2) && !has(5) && !has(6))
    window.bottom = origin.y;
  if (!has(3) && !has(6) && !has(7))
    window.left = origin.x;
  std::vector<NavigationWall> activeWalls;
  std::vector<PixelBox> barriers;
  for (const auto &wall : navigation_) {
    bool horizontal = wall.normal % 2 == 0;
    int axis = horizontal ? origin.y : origin.x;
    bool facesOrigin = wall.normal == 0 || wall.normal == 3 ? wall.line < axis
                                                            : wall.line > axis;
    if (!facesOrigin)
      continue;
    if (wall.line < (horizontal ? window.top : window.left) - 1 ||
        wall.line > (horizontal ? window.bottom : window.right) + 1 ||
        wall.first > (horizontal ? window.right : window.bottom) ||
        wall.last < (horizontal ? window.left : window.top))
      continue;
    activeWalls.push_back(wall);
    barriers.push_back(
        horizontal ? PixelBox{wall.first, wall.line, wall.last, wall.line}
                   : PixelBox{wall.line, wall.first, wall.line, wall.last});
  }
  auto navigationClear = [&](PixelBox area) {
    return valid(area) && domain_.contains({area.left, area.top}) &&
           domain_.contains({area.right, area.bottom}) &&
           std::none_of(barriers.begin(), barriers.end(),
                        [&](auto box) { return overlaps(area, box); });
  };
  auto cardinalOpen = [&](int face) {
    const bool extent = face == 0 ? window.top < origin.y
                        : face == 1 ? window.right > origin.x
                        : face == 2 ? window.bottom > origin.y
                                    : window.left < origin.x;
    return has(face) && extent;
  };
  auto quadrantEnabled = [&](int index) {
    return has(4 + index) || cardinalOpen(index) || cardinalOpen((index + 1) % 4);
  };
  auto add = [&](Point p, std::optional<PixelBox> seen = {},
                 unsigned role = 255, bool boundaryPort = false,
                 unsigned retreat = 0, int axialNormal = -1,
                 bool openBeyondWindow = false, int continuationAxis = -1) {
    if (p.x == origin.x && p.y == origin.y)
      return;
    auto area = seen.value_or(between(origin, p));
    int forward = p.y > origin.y ? 1 : -1;
    bool backFaceJunction =
        openBeyondWindow &&
        std::any_of(navigation_.begin(), navigation_.end(),
                    [&](const auto &wall) {
                      if (wall.normal != (forward > 0 ? 0 : 2))
                        return false;
                      if (p.y != wall.line - neighborSteps[wall.normal].y)
                        return false;
                      for (auto [endpoint, extension] :
                           {std::pair{wall.first, wall.firstExtension},
                            std::pair{wall.last, wall.lastExtension}})
                        if (p.x == endpoint + extension &&
                            !clear({endpoint, p.y, endpoint, p.y}))
                          return true;
                      return false;
                    });
    unsigned mask = 0;
    for (unsigned i = 0; i < neighborSteps.size(); ++i) {
      Point q{p.x + neighborSteps[i].x, p.y + neighborSteps[i].y};
      // A junction on the back face of overlapping footprints opens the
      // next corridor segment. Its reverse half-plane remains closed, and
      // nearby footprints still constrain the forward exterior corner.
      if (backFaceJunction &&
          ((q.y - p.y) * forward < 0 || !clear(between(p, q))))
        continue;
      bool beyond = openBeyondWindow && !window.contains(q);
      bool outsideHorizon = !horizon.contains(q);
      auto visibleBoundary = [&](std::size_t at) {
        const auto &wall = activeWalls[at];
        int low = wall.normal % 2 ? horizon.left : horizon.top;
        int high = wall.normal % 2 ? horizon.right : horizon.bottom;
        return !outsideHorizon || (wall.line >= low && wall.line <= high);
      };
      bool narrowExit =
          p.y != origin.y && (q.y - p.y) * forward > 0 &&
          std::any_of(
              activeWalls.begin(), activeWalls.end(), [&](const auto &wall) {
                if (wall.normal % 2 == 0 ||
                    wall.line - neighborSteps[wall.normal].x != p.x ||
                    neighborSteps[wall.normal].x != q.x - p.x ||
                    wall.last != p.y - 1)
                  return false;
                return std::any_of(activeWalls.begin(), activeWalls.end(),
                                   [&](const auto &next) {
                                     return next.normal == wall.normal &&
                                            next.line == wall.line &&
                                            next.first == p.y + 1;
                                   });
              });
      // A forward diagonal can leave through a one-pixel gap between collinear
      // walls. Other corners retain swept boundary constraints.
      auto crosses = [&](PixelBox barrier) {
        if (!narrowExit)
          return overlaps(between(p, q), barrier);
        return overlaps(between(p, {q.x, p.y}), barrier) ||
               overlaps(between(p, {p.x, q.y}), barrier);
      };
      bool opensJunction = beyond && backFaceJunction;
      bool free = opensJunction ? domain_.contains(q)
                                : domain_.contains(q) &&
                                      std::none_of(barriers.begin(),
                                                   barriers.end(), crosses);
      if (outsideHorizon && !opensJunction) {
        free = domain_.contains(q);
        for (std::size_t wall = 0; free && wall < activeWalls.size(); ++wall)
          if (visibleBoundary(wall) && crosses(barriers[wall]))
            free = false;
      }
      if (axialNormal >= 0 && !beyond) {
        free = domain_.contains(q);
        for (std::size_t wall = 0; free && wall < activeWalls.size(); ++wall)
          if (activeWalls[wall].normal == axialNormal &&
              crosses(barriers[wall]))
            free = false;
      }
      bool xHorizon = p.x == horizon.left || p.x == horizon.right;
      bool yHorizon = p.y == horizon.top || p.y == horizon.bottom;
      // Internal shadow corners retain known terrain constraints. An outer
      // clipped-window corner can expose a prospective exit past the horizon.
      bool constrainedExit = axialNormal < 0 && (xHorizon != yHorizon);
      bool windowCorner = (p.x == window.left || p.x == window.right) &&
                          (p.y == window.top || p.y == window.bottom);
      bool knownContact = std::any_of(activeWalls.begin(), activeWalls.end(),
          [&](const auto &wall) {
            bool horizontal = wall.normal % 2 == 0;
            int lineLow = horizontal ? horizon.top : horizon.left;
            int lineHigh = horizontal ? horizon.bottom : horizon.right;
            if (wall.line < lineLow || wall.line > lineHigh)
              return false;
            int along = horizontal ? p.x : p.y;
            int across = horizontal ? p.y : p.x;
            int step = horizontal ? neighborSteps[wall.normal].y
                                  : neighborSteps[wall.normal].x;
            return across == wall.line - step &&
                   along >= wall.first && along <= wall.last;
          });
      bool clearExit =
          domain_.contains(q) &&
          (!constrainedExit ||
           (outsideHorizon && windowCorner && !knownContact) ||
           std::none_of(obstacles_.begin(),
                        obstacles_.begin() + terrainObstacleCount_,
                        [&](auto box) { return box.contains(q); }));
      if (!area.contains(q) &&
          (boundaryPort || (retreat & (1U << i)) || (free && clearExit)))
        mask |= 1U << i;
    }
    // A roof-clipped shadow corner continues its roof sweep. The locally
    // clear side is not the ownership direction of this search port.
    if (continuationAxis >= 0)
      mask = (mask & ~15U) | (1U << continuationAxis);
    auto found = std::find_if(result.frontier.begin(), result.frontier.end(),
                              [&](auto q) { return p.x == q.x && p.y == q.y; });
    if (found == result.frontier.end()) {
      result.frontier.push_back(p);
      result.allowed.push_back(mask & role);
    } else
      result.allowed[std::size_t(found - result.frontier.begin())] &=
          mask & role;
  };
  constexpr std::array<Point, 4> directions{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
  for (auto d : directions) {
    int index = d.y < 0 ? 0 : d.x > 0 ? 1 : d.y > 0 ? 2 : 3;
    Point end{d.x < 0   ? window.left
              : d.x > 0 ? window.right
                        : origin.x,
              d.y < 0   ? window.top
              : d.y > 0 ? window.bottom
                        : origin.y};
    for (auto obstacle : barriers) {
      if (d.x && origin.y >= obstacle.top && origin.y <= obstacle.bottom) {
        if (d.x > 0 && obstacle.left > origin.x)
          end.x = std::min(end.x, obstacle.left - 1);
        if (d.x < 0 && obstacle.right < origin.x)
          end.x = std::max(end.x, obstacle.right + 1);
      }
      if (d.y && origin.x >= obstacle.left && origin.x <= obstacle.right) {
        if (d.y > 0 && obstacle.top > origin.y)
          end.y = std::min(end.y, obstacle.top - 1);
        if (d.y < 0 && obstacle.bottom < origin.y)
          end.y = std::max(end.y, obstacle.bottom + 1);
      }
    }
    if (d.y < 0)
      window.top = end.y;
    else if (d.x > 0)
      window.right = end.x;
    else if (d.y > 0)
      window.bottom = end.y;
    else
      window.left = end.x;
    if (has(index))
      add(end, {}, 255, false, 0, index);
  }
  constexpr std::array<Point, 4> quadrants{
      {{1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};
  auto freeLine = [&](const NavigationWall &wall) {
    auto normal = neighborSteps[wall.normal];
    return wall.line - (wall.normal % 2 == 0 ? normal.y : normal.x);
  };
  auto component = [&](int normal, int boundary,
                       int along) -> std::optional<std::pair<int, int>> {
    std::optional<std::pair<int, int>> span;
    for (const auto &wall : activeWalls)
      if (wall.normal == normal && freeLine(wall) == boundary &&
          wall.first <= along && wall.last >= along)
        span = span ? std::pair{std::min(span->first, wall.first),
                                std::max(span->second, wall.last)}
                    : std::pair{wall.first, wall.last};
    if (!span)
      return {};
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &wall : activeWalls)
        if (wall.normal == normal && freeLine(wall) == boundary &&
            wall.first <= span->second + 1 && wall.last >= span->first - 1) {
          auto expanded = std::pair{std::min(span->first, wall.first),
                                    std::max(span->second, wall.last)};
          if (expanded != *span) {
            span = expanded;
            changed = true;
          }
        }
    }
    return span;
  };
  auto endpoints = [&](std::vector<int> walls, int normal, int low, int high,
                       Point quadrant) {
    auto direction = neighborSteps[normal];
    bool horizontal = normal % 2 == 0;
    bool reverse = direction.x < 0 || direction.y < 0;
    std::sort(walls.begin(), walls.end(), [&](int a, int b) {
      return reverse ? activeWalls[a].first > activeWalls[b].first
                     : activeWalls[a].first < activeWalls[b].first;
    });
    for (int index : walls) {
      const auto &wall = activeWalls[index];
      // The initial sweep from a touching body face reaches the search
      // horizon before opening that face's corner ports. Later sweeps can
      // discover those ports from their own origins.
      if (wall.boundarySource && wall.boundarySource->x == origin.x &&
          wall.boundarySource->y == origin.y)
        continue;
      for (int ordinal = 0; ordinal < 2; ++ordinal) {
        int endpoint = reverse ? 1 - ordinal : ordinal;
        int along = endpoint ? wall.last + wall.lastExtension
                             : wall.first + wall.firstExtension;
        if (along < low || along > high)
          continue;
        int alongDirection = horizontal ? quadrant.x : quadrant.y;
        int far = alongDirection > 0 ? high : low;
        if (along == far && endpoint == int(alongDirection > 0))
          continue;
        int near = alongDirection > 0 ? low : high;
        if (ordinal == 0 && along == near)
          continue;
        Point point = horizontal ? Point{along, freeLine(wall)}
                                 : Point{freeLine(wall), along};
        if (!window.contains(point) || (point.x - origin.x) * quadrant.x < 0 ||
            (point.y - origin.y) * quadrant.y < 0 ||
            !navigationClear(between(origin, point)))
          continue;
        int tangent = endpoint ? 1 : -1;
        Point diagonal = horizontal ? Point{direction.x + tangent, direction.y}
                                    : Point{direction.x, direction.y + tangent};
        unsigned role = 0;
        for (unsigned bit = 0; bit < neighborSteps.size(); ++bit)
          if ((neighborSteps[bit].x == direction.x &&
               neighborSteps[bit].y == direction.y) ||
              (neighborSteps[bit].x == diagonal.x &&
               neighborSteps[bit].y == diagonal.y))
            role |= 1U << bit;
        add(point, {}, role, true);
      }
    }
  };
  for (int quadrant = 0; quadrant < 4; ++quadrant) {
    if (!quadrantEnabled(quadrant))
      continue;
    auto direction = quadrants[quadrant];
    unsigned cornerRole = 0;
    for (unsigned bit = 0; bit < neighborSteps.size(); ++bit)
      if (neighborSteps[bit].x * direction.x > 0 ||
          neighborSteps[bit].y * direction.y > 0)
        cornerRole |= 1U << bit;
    auto retreatMask = [&](bool horizontal) {
      unsigned mask = 0;
      for (unsigned bit = 0; bit < neighborSteps.size(); ++bit) {
        auto step = neighborSteps[bit];
        if ((step.x == -direction.x && step.y == -direction.y) ||
            (horizontal ? step.x == 0 && step.y == -direction.y
                        : step.x == -direction.x && step.y == 0))
          mask |= 1U << bit;
      }
      return mask;
    };
    int normalX = direction.x > 0 ? 1 : 3, normalY = direction.y < 0 ? 0 : 2;
    int farX = direction.x > 0 ? window.right : window.left;
    int farY = direction.y > 0 ? window.bottom : window.top;
    int width = direction.x * (farX - origin.x),
        height = direction.y * (farY - origin.y);
    int firstHeight = 0, shadowWidth = width;
    if (width > 0) {
      firstHeight = height;
      if (auto edge = component(normalX, farX, origin.y)) {
        int end = direction.y > 0 ? edge->second + 1 : edge->first - 1;
        firstHeight =
            std::min(firstHeight, std::max(0, direction.y * (end - origin.y)));
      }
      std::vector<int> walls;
      for (int index = 0; index < int(activeWalls.size()); ++index) {
        const auto &wall = activeWalls[index];
        if (wall.normal != normalY || wall.first > std::max(origin.x, farX) ||
            wall.last < std::min(origin.x, farX))
          continue;
        int reach = direction.y * (freeLine(wall) - origin.y);
        if (reach < 0 || reach > firstHeight)
          continue;
        if (reach < firstHeight) {
          firstHeight = reach;
          walls.clear();
        }
        walls.push_back(index);
      }
      Point corner{farX, origin.y + direction.y * firstHeight};
      auto area = between(origin, corner);
      // A northwest sweep discovers its return along a north wall before the
      // outer endpoint ports. Discovery order breaks equal fallback scores.
      bool northReturn =
          direction.x < 0 && direction.y < 0 &&
          std::any_of(
              walls.begin(), walls.end(),
              [&](int index) {
                const auto &wall = activeWalls[index];
                return wall.first + wall.firstExtension <= origin.x &&
                       wall.last + wall.lastExtension >= origin.x;
              });
      Point axialReturn{origin.x, corner.y};
      if (northReturn && std::none_of(result.frontier.begin(),
                                      result.frontier.end(), [&](Point point) {
                                        return point.x == axialReturn.x &&
                                               point.y == axialReturn.y;
                                      }))
        add(axialReturn,
            PixelBox{axialReturn.x, axialReturn.y, axialReturn.x,
                     axialReturn.y},
            retreatMask(false), true);
      if (quadrant % 2)
        endpoints(walls, normalY, std::min(origin.x, farX),
                  std::max(origin.x, farX), direction);
      if (firstHeight > 0)
        add({farX, origin.y}, area, 255, true);
      unsigned retreat = firstHeight == 0 && height > 0 ? retreatMask(true) : 0;
      add(corner, area, height == 0 ? 255U : cornerRole | retreat,
          firstHeight == 0 && walls.empty(), retreat);
      if (firstHeight == height) {
        // A diagonal sweep can retain a forward port where an unrequested
        // axis was clipped, including an exterior continuation at a junction.
        bool clippedAxis =
            !has(normalY) &&
            farY != (direction.y > 0 ? horizon.bottom : horizon.top);
        bool existingAxial = std::any_of(
            result.frontier.begin(), result.frontier.end(),
            [&](Point p) { return p.x == origin.x && p.y == farY; });
        unsigned axialRole = 255;
        if (clippedAxis) {
          axialRole = 0;
          for (unsigned bit = 0; bit < neighborSteps.size(); ++bit)
            if (neighborSteps[bit].y == direction.y)
              axialRole |= 1U << bit;
        }
        // Preserve a physical return already exposed by the preceding sweep.
        if (!clippedAxis || !existingAxial)
          add({origin.x, farY}, area, axialRole, clippedAxis, 0, normalY);
      }
      if (!(quadrant % 2))
        endpoints(walls, normalY, std::min(origin.x, farX),
                  std::max(origin.x, farX), direction);
      for (int index : walls) {
        const auto &wall = activeWalls[index];
        int near = direction.x > 0 ? wall.first + wall.firstExtension
                                   : wall.last + wall.lastExtension;
        shadowWidth =
            std::min(shadowWidth, std::max(0, direction.x * (near - origin.x)));
      }
      result.areas.push_back(area);
    }
    if (firstHeight >= height)
      continue;
    Point start{origin.x, origin.y + direction.y * firstHeight};
    int secondWidth = shadowWidth;
    if (auto edge = component(normalY, farY, origin.x)) {
      int end = direction.x > 0 ? edge->second + 1 : edge->first - 1;
      secondWidth =
          std::min(secondWidth, std::max(0, direction.x * (end - origin.x)));
    }
    std::vector<int> walls;
    for (int index = 0; index < int(activeWalls.size()); ++index) {
      const auto &wall = activeWalls[index];
      if (wall.normal != normalX || wall.first > std::max(start.y, farY) ||
          wall.last < std::min(start.y, farY))
        continue;
      int reach = direction.x * (freeLine(wall) - origin.x);
      if (reach < 0 || reach > secondWidth)
        continue;
      if (reach < secondWidth) {
        secondWidth = reach;
        walls.clear();
      }
      walls.push_back(index);
    }
    Point corner{origin.x + direction.x * secondWidth, farY};
    auto area = between(start, corner);
    bool horizonRoof = farY == (direction.y > 0 ? horizon.bottom : horizon.top);
    // Southeast returns need an actual side wall. Northwest strips also
    // expose returns at the horizon, but keep their primary corner under
    // a clipped roof alone.
    // Southwest also retains roof-only returns at zero primary depth.
    bool primaryReturn = firstHeight > 0 || !walls.empty() ||
                         (direction.y < 0 &&
                          (direction.x > 0 || horizonRoof || !has(normalY))) ||
                         (direction.y > 0 && direction.x < 0);
    if (direction.x == direction.y && walls.empty() &&
        (!horizonRoof || direction.y > 0))
      primaryReturn = false;
    bool narrowedReturn = secondWidth < shadowWidth && primaryReturn;
    bool earlyEndpoints = quadrant % 2;
    if (narrowedReturn && earlyEndpoints)
      add({corner.x, start.y}, {}, 1U << (quadrant + 4), true);
    if (earlyEndpoints)
      endpoints(walls, normalX, std::min(start.y, farY),
                std::max(start.y, farY), direction);
    bool clippedAxis = !has(normalY) &&
                       farY != (direction.y > 0 ? horizon.bottom : horizon.top);
    if (secondWidth > 0) {
      // A diagonal-only strip can expose a prospective continuation at its
      // clipped axial end even when that cardinal face was already explored.
      add({origin.x, farY}, area, 255, clippedAxis, 0, normalY);
    }
    unsigned retreat = secondWidth == 0 && (shadowWidth > 0 || firstHeight > 0)
                           ? retreatMask(false)
                           : 0;
    bool existingCorner = std::any_of(
        result.frontier.begin(), result.frontier.end(),
        [&](Point p) { return p.x == corner.x && p.y == corner.y; });
    if (secondWidth == 0 && existingCorner && has(normalY) && !horizonRoof) {
      // An empty collapsed quadrant must not close a cardinal wall-end exit.
      // Its physical side wall constrains only the forward escape half-plane.
      if (!walls.empty()) {
        bool crossesCorner =
            std::any_of(walls.begin(), walls.end(), [&](int i) {
              return activeWalls[i].first <= farY &&
                     activeWalls[i].last >= farY;
            });
        // A northwest endpoint beyond the side wall retains its forward
        // continuation. The other collapsed sweeps keep their escape side.
        if (crossesCorner || direction.x > 0 || direction.y > 0) {
          unsigned escapeRole = 0;
          for (unsigned bit = 0; bit < neighborSteps.size(); ++bit) {
            auto step = neighborSteps[bit];
            if (step.x == -direction.x &&
                (step.y == 0 || step.y == direction.y))
              escapeRole |= 1U << bit;
          }
          // A collapsed shadow retains its previously discovered return.
          add(corner, area, escapeRole | retreat, false, retreat);
        } else
          add(corner, area, cornerRole | retreat, false, retreat);
      }
    } else if (secondWidth == 0 && !existingCorner) {
      unsigned escapeRole = 255;
      bool prospectiveCorner = clippedAxis && direction.x < 0;
      if (prospectiveCorner) {
        escapeRole = 0;
        for (unsigned bit = 0; bit < neighborSteps.size(); ++bit) {
          auto step = neighborSteps[bit];
          if (step.x == -direction.x && (step.y == 0 || step.y == direction.y))
            escapeRole |= 1U << bit;
        }
      }
      add(corner, area, escapeRole, prospectiveCorner, retreat, -1, true);
    } else
      add(corner, area, cornerRole | retreat, secondWidth == 0 && walls.empty(),
          retreat, -1, false,
          firstHeight > 0 && walls.empty() && !horizonRoof &&
                  secondWidth == width &&
                  farX != (direction.x > 0 ? horizon.right : horizon.left)
              ? normalY : -1);
    // Northeast/southwest discover the secondary corner before its return;
    // the other sweeps expose the return before their wall endpoints.
    if (narrowedReturn && !earlyEndpoints)
      add({corner.x, start.y}, {}, 1U << (quadrant + 4), true);
    if (secondWidth >= shadowWidth && direction.y < 0 && secondWidth < width) {
      // A turn-back port belongs to an actual wall endpoint. A sweep strip
      // shortened by the search window alone does not expose that corner.
      bool wallEndsHere =
          std::any_of(walls.begin(), walls.end(), [&](int index) {
            const auto &wall = activeWalls[index];
            return freeLine(wall) == corner.x &&
                   wall.last + wall.lastExtension == start.y;
          });
      bool existingReturn = std::any_of(
          result.frontier.begin(), result.frontier.end(), [&](Point point) {
            return point.x == corner.x && point.y == start.y;
          });
      if (wallEndsHere && !existingReturn)
        add({corner.x, start.y}, PixelBox{corner.x, start.y, corner.x, start.y},
            retreatMask(false), true);
      result.terminal.push_back({corner.x, start.y});
    }
    if (!earlyEndpoints)
      endpoints(walls, normalX, std::min(start.y, farY),
                std::max(start.y, farY), direction);
    result.areas.push_back(area);
  }
  return result;
}
GroundGeometry groundGeometry(const Terrain &terrain, const Contours &contours,
                              const dat::Extents &shape,
                              std::span<const std::uint8_t> creep) {
  if ((!creep.empty() &&
       creep.size() != std::size_t(terrain.width()) * terrain.height()) ||
      std::min({shape.left, shape.top, shape.right, shape.bottom}) < 0 ||
      std::max({shape.left, shape.top, shape.right, shape.bottom}) > 65535)
    throw std::invalid_argument("Invalid ground-space footprint or creep mask");
  std::vector<PixelBox> obstacles;
  std::vector<NavigationWall> navigation;
  // A blocked center and an intersecting wall are independent rejection
  // conditions. Keep them separate so dynamic creep changes only the former.
  std::map<std::pair<int, int>, int> previous;
  for (int y = 0; y < terrain.height() * 4; ++y) {
    std::map<std::pair<int, int>, int> current;
    auto blocked = [&](int x) {
      bool covered = creep.empty()
                         ? terrain.tile(x / 4, y / 4).creep
                         : creep[(y / 4) * terrain.width() + x / 4] != 0;
      return !covered && !terrain.walkable(x, y);
    };
    for (int x = 0; x < terrain.width() * 4;) {
      if (!blocked(x)) {
        ++x;
        continue;
      }
      int begin = x++;
      while (x < terrain.width() * 4 && blocked(x))
        ++x;
      auto key = std::pair{begin, x};
      auto found = previous.find(key);
      int index;
      if (found == previous.end()) {
        index = int(obstacles.size());
        obstacles.push_back({begin * 8, y * 8, x * 8 - 1, y * 8 + 7});
      } else {
        index = found->second;
        obstacles[index].bottom = y * 8 + 7;
      }
      current.emplace(key, index);
    }
    previous = std::move(current);
  }
  for (int normal = 0; normal < 4; ++normal)
    for (auto edge : contours.edges()[normal]) {
      PixelBox wall{};
      switch (normal) {
      case 0:
        wall = {edge.first - shape.right, edge.line + 1, edge.last + shape.left,
                edge.line + shape.top};
        break;
      case 1:
        wall = {edge.line - shape.right, edge.first - shape.bottom,
                edge.line - 1, edge.last + shape.top};
        break;
      case 2:
        wall = {edge.first - shape.right, edge.line - shape.bottom,
                edge.last + shape.left, edge.line - 1};
        break;
      case 3:
        wall = {edge.line + 1, edge.first - shape.bottom,
                edge.line + shape.left, edge.last + shape.top};
        break;
      }
      if (valid(wall))
        obstacles.push_back(wall);
      const std::array<int, 4> offset{
          {shape.top, -shape.right, -shape.bottom, shape.left}};
      int line = edge.line + offset[normal];
      int first = edge.first + offset[edge.corners & 3],
          last = edge.last + offset[(edge.corners >> 2) & 3];
      if (first <= last)
        navigation.push_back(
            {normal, line, first, last,
             (edge.corners & 3) == 0 || (edge.corners & 3) == 3 ? 1 : -1,
             ((edge.corners >> 2) & 3) == 0 || ((edge.corners >> 2) & 3) == 3
                 ? 1
                 : -1});
    }
  return {{shape.left, shape.top, terrain.width() * 32 - shape.right - 1,
           terrain.height() * 32 - shape.bottom - 33},
          shape,
          std::move(obstacles),
          std::move(navigation)};
}
LocalSpace
GroundGeometry::withBodies(std::span<const CollisionBody> bodies,
                           std::optional<Point> departure) const {
  auto obstacles = this->obstacles;
  auto navigation = this->navigation;
  for (const auto &body : bodies)
    if (body.collides && !body.flying) {
      PixelBox obstacle{body.position.x - body.bounds.left - shape.right,
                        body.position.y - body.bounds.top - shape.bottom,
                        body.position.x + body.bounds.right + shape.left,
                        body.position.y + body.bounds.bottom + shape.top};
      const auto originalBody = obstacle;
      if (departure && obstacle.contains(*departure)) {
        // Preserve tangent travel from a touched face without opening the
        // rectangle's interior. A strictly interior source stays blocked.
        if (departure->x == obstacle.left)
          ++obstacle.left;
        else if (departure->x == obstacle.right)
          --obstacle.right;
        if (departure->y == obstacle.top)
          ++obstacle.top;
        else if (departure->y == obstacle.bottom)
          --obstacle.bottom;
        if (!valid(obstacle))
          continue;
      }
      obstacles.push_back(obstacle);
      auto faceSource = [&](int normal) -> std::optional<Point> {
        if (!departure || !originalBody.contains(*departure))
          return {};
        const bool touches = normal == 0 ? departure->y == originalBody.bottom
                             : normal == 1 ? departure->x == originalBody.left
                             : normal == 2 ? departure->y == originalBody.top
                                           : departure->x == originalBody.right;
        return touches ? departure : std::nullopt;
      };
      navigation.push_back({0, obstacle.bottom, obstacle.left, obstacle.right,
                            -1, 1, faceSource(0)});
      navigation.push_back({1, obstacle.left, obstacle.top, obstacle.bottom,
                            -1, 1, faceSource(1)});
      navigation.push_back({2, obstacle.top, obstacle.left, obstacle.right,
                            -1, 1, faceSource(2)});
      navigation.push_back({3, obstacle.right, obstacle.top, obstacle.bottom,
                            -1, 1, faceSource(3)});
    }
  return LocalSpace(domain, std::move(obstacles), std::move(navigation),
                    this->obstacles.size());
}
LocalSpace groundSpace(const Terrain &terrain, const Contours &contours,
                       const dat::Extents &shape,
                       std::span<const CollisionBody> bodies,
                       std::span<const std::uint8_t> creep) {
  return groundGeometry(terrain, contours, shape, creep).withBodies(bodies);
}
std::optional<Point> adjustGroundTarget(const Terrain &terrain,
                                        const Regions &regions,
                                        const dat::Extents &shape,
                                        Point target) {
  if (target.x < 0 || target.y < 0 || target.x >= terrain.width() * 32 ||
      target.y >= terrain.height() * 32 ||
      std::min({shape.left, shape.top, shape.right, shape.bottom}) < 0 ||
      std::max({shape.left, shape.top, shape.right, shape.bottom}) > 65535)
    throw std::invalid_argument("Invalid ground target or footprint");
  auto eligible = [&](Point center) {
    PixelBox box{center.x - shape.left, center.y - shape.top,
                 center.x + shape.right, center.y + shape.bottom};
    if (box.left < 0 || box.top < 0 || box.right >= terrain.width() * 32 ||
        box.bottom >= terrain.height() * 32)
      return false;
    for (int y = box.top / 32; y <= box.bottom / 32; ++y)
      for (int x = box.left / 32; x <= box.right / 32; ++x)
        if (!terrain.tile(x, y).partial &&
            !regions.entries()[regions.tileLabels()[y * terrain.width() + x]]
                 .walkable())
          return false;
    return true;
  };
  // Clockwise square borders, beginning at each northwest corner. Repeated
  // corners have no side effects, so each candidate is tested only once.
  return squareRings(target, 8, 4, eligible);
}
namespace {
bool orderPointFits(const Terrain &terrain, const Contours &contours,
                    const dat::Extents &shape, Point p) {
  return p.x >= shape.left && p.y >= shape.top &&
         p.x + shape.right < terrain.width() * 32 &&
         p.y + shape.bottom < terrain.height() * 32 &&
         terrain.walkable(p.x / 8, p.y / 8) && contours.permits(p, shape);
}
std::optional<Point> groundOrderAnchor(const Terrain &terrain,
                                      const Regions &regions,
                                      const dat::Extents &shape, Point target,
                                      Point source, bool keepProbeEndpoint = false) {
  auto fallback = adjustGroundTarget(terrain, regions, shape, target);
  int targetRegion = regions.regionAtPreferWalkable(target);
  int sourceRegion = regions.regionAtPreferWalkable(source);
  if (sourceRegion < 0)
    throw std::invalid_argument("Ground order source outside map");
  if (!terrain.walkable(target.x / 8, target.y / 8) ||
      regions.entries()[sourceRegion].group !=
          regions.entries()[targetRegion].group) {
    auto route = regionRoute(regions, source, target);
    if (!route.regions.empty())
      targetRegion = route.regions.back();
  }
  const Point regional = destinationInRegion(regions, targetRegion, target);
  auto anchor = adjustGroundTarget(terrain, regions, shape, regional);
  if (!anchor && keepProbeEndpoint) {
    // Command formation keeps the final coarse-search coordinate even when
    // its aggregate footprint cannot be placed. The enumerated border search
    // ends one eight-pixel cell northwest of its four-ring envelope.
    return Point{std::clamp(regional.x - 40, 0, terrain.width() * 32 - 1),
                 std::clamp(regional.y - 40, 0, terrain.height() * 32 - 1)};
  }
  return anchor ? anchor : fallback;
}
std::optional<Point> projectToOrderAnchor(
    const Terrain &terrain, const Contours &contours, const Regions &regions,
    const dat::Extents &shape, Point target, Point anchor) {
  const int anchorRegion = regions.regionAt(anchor);
  const auto &neighbors = regions.entries()[anchorRegion].walking;
  auto eligible = [&](Point p) {
    if (!orderPointFits(terrain, contours, shape, p))
      return false;
    const int region = regions.regionAt(p);
    return region == anchorRegion ||
           std::find(neighbors.begin(), neighbors.end(), region) != neighbors.end();
  };
  if (eligible(target))
    return target;
  Point delta{anchor.x - target.x, anchor.y - target.y};
  const int length = std::max(1, distance(delta));
  int spacing = std::min(16, length / 4);
  if (spacing < 2)
    spacing = length;
  // Build ordered candidates independently of clearance testing. Integer
  // interpolation preserves the requested pixel's rounding origin.
  std::vector<Point> candidates;
  candidates.reserve(length / spacing + 1);
  for (int sample = 1; sample <= length / spacing; ++sample) {
    auto offset = std::int64_t(sample) * spacing;
    candidates.push_back(
        {target.x + int(std::int64_t(delta.x) * offset / length),
         target.y + int(std::int64_t(delta.y) * offset / length)});
  }
  auto accepted = std::find_if(candidates.begin(), candidates.end(), eligible);
  return accepted != candidates.end() ? std::optional(*accepted) : anchor;
}
} // namespace
std::optional<Point> projectGroundOrderTarget(
    const Terrain &terrain, const Contours &contours, const Regions &regions,
    const dat::Extents &shape, Point target, Point source) {
  // Retain validation even for an already clear request.
  adjustGroundTarget(terrain, regions, shape, target);
  if (orderPointFits(terrain, contours, shape, target))
    return target;
  auto anchor = groundOrderAnchor(terrain, regions, shape, target, source);
  return anchor ? projectToOrderAnchor(terrain, contours, regions, shape, target,
                                      *anchor)
                : std::nullopt;
}
std::optional<OrderGroupProjection> projectGroupOrderTargets(
    const Terrain &terrain, const Contours &contours, const Regions &regions,
    std::span<const OrderGroupMember> members, Point requested) {
  if (requested.x < 0 || requested.y < 0 ||
      requested.x >= terrain.width() * 32 ||
      requested.y >= terrain.height() * 32)
    throw std::invalid_argument("Group order outside map");
  OrderGroupProjection result{requested};
  if (members.empty())
    return result;
  PixelBox selection{members.front().position.x, members.front().position.y,
                     members.front().position.x, members.front().position.y};
  std::int64_t sumX = 0, sumY = 0;
  std::uint64_t area = 0;
  bool ground = false;
  for (const auto &member : members) {
    const auto &b = member.bounds;
    if (std::min({b.left, b.top, b.right, b.bottom}) < 0 ||
        std::max({b.left, b.top, b.right, b.bottom}) > 65535)
      throw std::invalid_argument("Invalid group footprint");
    selection.left = std::min(selection.left, member.position.x);
    selection.right = std::max(selection.right, member.position.x);
    selection.top = std::min(selection.top, member.position.y);
    selection.bottom = std::max(selection.bottom, member.position.y);
    sumX += member.position.x;
    sumY += member.position.y;
    area += std::uint64_t(b.left + b.right + 1) * (b.top + b.bottom + 1);
    ground |= member.ground;
  }
  const Point center{int(sumX / std::int64_t(members.size())),
                     int(sumY / std::int64_t(members.size()))};
  if (ground) {
    dat::Extents anchorShape = members.front().bounds;
    dat::Extents fitShape = anchorShape;
    if (members.size() > 1) {
      const int side = int(std::sqrt(double(area * 2)));
      // Coarse terrain probes include both ends of the aggregate square;
      // contour clearance below uses its smaller interior span.
      const int near = side / 2, far = side - near;
      anchorShape = {near, near, far, far};
      const int reach = (side - 1) / 2;
      fitShape = {reach, reach, reach, reach};
    }
    if (!orderPointFits(terrain, contours, fitShape, requested)) {
      auto anchor = groundOrderAnchor(terrain, regions, anchorShape, requested,
                                      center, true);
      if (!anchor)
        return std::nullopt;
      result.groundAnchor = *anchor;
    }
  }
  if (members.size() > 1) {
    result.insideSelection = selection.contains(requested);
    const int cohesion = (ground ? 6 : 8) * 32;
    result.keepsOffsets = !result.insideSelection &&
                          selection.right - selection.left <= cohesion &&
                          selection.bottom - selection.top <= cohesion;
  }
  for (const auto &member : members) {
    const Point anchor = member.ground ? result.groundAnchor : requested;
    Point destination = requested;
    if (result.insideSelection)
      destination = {std::clamp(member.position.x, anchor.x - 32, anchor.x + 32),
                     std::clamp(member.position.y, anchor.y - 32, anchor.y + 32)};
    else if (result.keepsOffsets)
      destination = {requested.x + member.position.x - center.x,
                     requested.y + member.position.y - center.y};
    destination.x = std::clamp(destination.x, member.bounds.left,
                               terrain.width() * 32 - member.bounds.right - 1);
    destination.y = std::clamp(destination.y, member.bounds.top,
                               terrain.height() * 32 - member.bounds.bottom - 33);
    if (member.ground) {
      auto projected = projectToOrderAnchor(terrain, contours, regions,
                                            member.bounds, destination, anchor);
      if (!projected)
        return std::nullopt;
      destination = *projected;
    }
    result.destinations.push_back(destination);
  }
  return result;
}
void limitRouteStorage(LocalRouteResult &route) {
  constexpr std::size_t wordBudget = 48;
  bool clipped = route.points.size() >= wordBudget / 2;
  if (clipped) {
    route.points.resize(wordBudget / 2 - 1);
    route.reached = false;
  }
  auto capacity = wordBudget - route.points.size() * 2;
  // A coordinate-heavy leg keeps just its regional anchor.
  if (clipped)
    capacity = 1;
  auto &memory = route.retained;
  if (memory.regions.size() > capacity)
    memory.regions.resize(capacity);
  memory.cursor = std::min(memory.cursor, memory.regions.size());
  route.itinerary.assign(memory.regions.begin() + memory.cursor,
                         memory.regions.end());
  route.itineraryTruncated = memory.fullSize > int(memory.regions.size());
}
LocalRouteResult planGroundRoute(const LocalSpace &space,
                                 const Regions &regions, Point source,
                                 Point target, std::optional<PixelBox> contact,
                                 std::optional<Point> adjustedTarget,
                                 LocalRouteDiagnostics *diagnostics,
                                 const LocalRouteResult *previous) {
  if (diagnostics)
    *diagnostics = {};
  if (regions.regionAt(source) < 0 || regions.regionAt(target) < 0)
    throw std::out_of_range("Local route outside region map");
  if (!space.clear(between(source, source)))
    throw std::invalid_argument("Local route source intersects an obstacle");
  const auto startRegion = regions.regionAt(source);
  Point destination = target;
  RegionRoute itinerary;
  LocalRouteResult::RegionalMemory memory;
  if (previous) {
    auto prior = previous->retained;
    if (prior.regions.empty()) {
      prior.regions = previous->itinerary;
      prior.fullSize =
          int(prior.regions.size()) + int(previous->itineraryTruncated);
    }
    if (prior.cursor < prior.regions.size()) {
      auto begin = std::find(prior.regions.begin() + prior.cursor + 1,
                             prior.regions.end(), startRegion);
      if (begin != prior.regions.end() &&
          !(prior.fullSize > int(prior.regions.size()) &&
            prior.regions.end() - begin <= 3)) {
        memory = prior;
        memory.cursor = std::size_t(begin - prior.regions.begin());
        itinerary.regions.assign(begin, prior.regions.end());
        itinerary.fullSize = prior.fullSize;
      }
    }
  }
  if (itinerary.regions.empty()) {
    itinerary = regionRoute(regions, source, target);
    memory.regions = itinerary.regions;
    memory.fullSize = itinerary.fullSize;
  }
  bool initiallyStuck =
      itinerary.regions.size() == 1 && startRegion != regions.regionAt(target);
  if (initiallyStuck)
    destination = destinationInRegion(regions, startRegion, target);
  int preferredRegion =
      itinerary.regions.size() > 1 ? itinerary.regions[1] : -1;
  bool penalizeOutside =
      preferredRegion >= 0 && regions.entries()[preferredRegion].walkable();
  if (itinerary.regions.size() > 3) {
    target = regions.entries()[itinerary.regions[2]].center;
    contact.reset();
  } else if (!contact && adjustedTarget &&
             (adjustedTarget->x != target.x || adjustedTarget->y != target.y)) {
    destination = target = *adjustedTarget;
  }
  bool targetIsDestination =
      target.x == destination.x && target.y == destination.y;
  int destinationRegion = regions.regionAt(destination);
  if (destinationRegion < 0)
    throw std::out_of_range("Adjusted destination outside region map");
  const auto &neighborRegions =
      regions
          .entries()[preferredRegion >= 0 ? preferredRegion : destinationRegion]
          .walking;
  auto isNeighbor = [&](Point point) {
    int region = regions.regionAt(point);
    return region != startRegion &&
           std::find(neighborRegions.begin(), neighborRegions.end(), region) !=
               neighborRegions.end();
  };
  auto length = [](Point a, Point b) {
    return distance({(a.x - b.x) * 256, (a.y - b.y) * 256});
  };
  auto goal = [&](Point p) {
    if (p.x == target.x && p.y == target.y)
      return true;
    return contact && contact->contains(p) &&
           (p.x == contact->left || p.x == contact->right ||
            p.y == contact->top || p.y == contact->bottom);
  };
  struct Node {
    Point p;
    int parent;
    std::int64_t cost, remaining;
    int heap = -1;
    bool visited = false;
    unsigned directions = 255;
  };
  std::vector<Node> nodes{{source, -1, 0, 16384 * 256}};
  std::map<std::pair<int, int>, int> coordinates{{{source.x, source.y}, 0}};
  std::vector<int> frontier;
  int peakFrontier = 0;
  auto score = [&](int id) { return nodes[id].cost + nodes[id].remaining; };
  auto exchange = [&](int a, int b) {
    std::swap(frontier[a], frontier[b]);
    nodes[frontier[a]].heap = a;
    nodes[frontier[b]].heap = b;
  };
  auto offer = [&](int id) {
    int at = nodes[id].heap;
    if (at < 0) {
      if (frontier.size() >= 150)
        return;
      at = int(frontier.size());
      frontier.push_back(id);
      peakFrontier = std::max(peakFrontier, int(frontier.size()));
      nodes[id].heap = at;
    }
    while (at > 0) {
      int parent = (at - 1) / 2;
      if (score(frontier[parent]) < score(id))
        break;
      exchange(at, parent);
      at = parent;
    }
  };
  auto pop = [&] {
    int id = frontier.front();
    exchange(0, int(frontier.size()) - 1);
    frontier.pop_back();
    nodes[id].heap = -1;
    int at = 0;
    while (at * 2 + 1 < int(frontier.size())) {
      int child = at * 2 + 1;
      if (child + 1 < int(frontier.size()) &&
          score(frontier[child + 1]) < score(frontier[child]))
        ++child;
      if (score(frontier[at]) <= score(frontier[child]))
        break;
      exchange(at, child);
      at = child;
    }
    return id;
  };
  offer(0);
  std::vector<PixelBox> visited;
  auto outward = [&](Point origin) {
    return unexploredLocalDirections(origin, visited);
  };
  bool goalDiscovered = false;
  bool tired = false;
  auto finish = [&](int id, bool reached, bool stuck) {
    LocalRouteResult result;
    result.retained = memory;
    result.source = source;
    result.destination = destination;
    result.reached = reached;
    result.stuck = stuck;
    result.nodes = int(nodes.size());
    result.peakFrontier = peakFrontier;
    if (diagnostics) {
      diagnostics->searchTarget = target;
      diagnostics->destination = destination;
      diagnostics->destinationRegion = destinationRegion;
      diagnostics->selected = id;
      diagnostics->candidates.clear();
      for (const auto &node : nodes)
        diagnostics->candidates.push_back(
            {node.p, node.parent, regions.regionAt(node.p), node.cost,
             node.remaining, node.visited, node.directions});
    }
    for (int n = id; nodes[n].parent >= 0; n = nodes[n].parent)
      result.points.push_back(nodes[n].p);
    std::reverse(result.points.begin(), result.points.end());
    if (reached)
      result.destination = nodes[id].p;
    limitRouteStorage(result);
    return result;
  };
  while (!frontier.empty()) {
    int id = pop();
    auto node = nodes[id];
    if (diagnostics)
      diagnostics->dequeued.push_back(node.p);
    if (goal(node.p)) {
      auto result = finish(id, itinerary.regions.size() <= 3, initiallyStuck);
      if (result.points.empty())
        result.points.push_back(source);
      return result;
    }
    int openPreferred =
        int(std::count_if(frontier.begin(), frontier.end(),
                          [&](int n) {
                            return regions.regionAt(nodes[n].p) ==
                                   preferredRegion;
                          })) +
        int(regions.regionAt(node.p) == preferredRegion);
    bool openNeighbor =
        isNeighbor(node.p) ||
        std::any_of(frontier.begin(), frontier.end(),
                    [&](int n) { return isNeighbor(nodes[n].p); });
    bool exhausted = tired;
    if (openPreferred > 0 && !goalDiscovered &&
        !(targetIsDestination && nodes.size() < 150))
      tired = exhausted = true;
    if (openNeighbor && !goalDiscovered && nodes.size() >= 200)
      tired = exhausted = true;
    if (regions.regionAt(node.p) == preferredRegion && exhausted)
      return finish(id, false, initiallyStuck);
    nodes[id].visited = true;
    if (isNeighbor(node.p) && exhausted)
      break;
    // Once a destination port is exposed, settle the existing frontier rather
    // than discovering further detours. This is an observable search policy.
    if (goalDiscovered || exhausted)
      continue;
    auto mask = nodes[id].directions & outward(node.p);
    nodes[id].directions = mask;
    if (!mask)
      continue;
    auto expansion = space.expand(node.p, 64, mask);
    if (diagnostics)
      diagnostics->expansions.push_back({node.p, mask, expansion});
    for (auto point : expansion.terminal)
      if (goal(point)) {
        expansion.frontier.push_back(point);
        expansion.allowed.push_back(255);
      }
    for (auto area : expansion.areas) {
      if (area.contains(target)) {
        expansion.frontier.push_back(target);
        expansion.allowed.push_back(255);
      }
    }
    visited.insert(visited.end(), expansion.areas.begin(),
                   expansion.areas.end());
    for (std::size_t port = 0; port < expansion.frontier.size(); ++port) {
      auto point = expansion.frontier[port];
      auto directions = goal(point) ? 255U : expansion.allowed[port];
      if (!goal(point) && directions) {
        directions &= outward(point);
        if (!directions)
          continue;
      }
      if (goal(point))
        goalDiscovered = true;
      auto cost = std::int64_t(length(node.p, point));
      int region = regions.regionAt(point);
      if (penalizeOutside && region != startRegion && region != preferredRegion)
        cost *= 2;
      cost += node.cost;
      auto key = std::pair{point.x, point.y};
      auto found = coordinates.find(key);
      int next;
      if (found == coordinates.end()) {
        if (nodes.size() >= 250)
          break;
        next = int(nodes.size());
        coordinates.emplace(key, next);
        nodes.push_back({point, id, cost, length(point, target)});
        nodes.back().directions = directions;
        nodes.back().visited = !directions && !goal(point);
      } else {
        next = found->second;
        // A reverse edge must not close its predecessor's remaining exits.
        if (next == node.parent)
          continue;
        if (nodes[next].cost <= cost) {
          if (!nodes[next].visited)
            nodes[next].directions &= directions;
          continue;
        }
        nodes[next].cost = cost;
        nodes[next].parent = id;
        if (nodes[next].visited)
          nodes[next].directions = directions;
        nodes[next].visited = false;
      }
      if (!nodes[next].visited)
        offer(next);
      if (frontier.size() >= 150 || nodes.size() >= 250)
        break;
    }
    if ((frontier.size() >= 150 || nodes.size() >= 250) && !goalDiscovered &&
        std::none_of(frontier.begin(), frontier.end(), [&](int candidate) {
          return regions.regionAt(nodes[candidate].p) == preferredRegion;
        }))
      break;
  }
  std::vector<int> discovered(regions.entries().size()),
      unvisited(regions.entries().size());
  int active = 0;
  for (int id = 1; id < int(nodes.size()); ++id) {
    auto &node = nodes[id];
    int region = regions.regionAt(node.p);
    ++discovered[region];
    if (!node.visited) {
      node.directions &= outward(node.p);
      if (!node.directions)
        node.visited = true;
    }
    if (!node.visited) {
      ++active;
      ++unvisited[region];
    }
  }
  int remaining = active;
  if (targetIsDestination) {
    remaining = unvisited[destinationRegion];
    for (int region : neighborRegions) {
      if (region == startRegion)
        continue;
      if (!discovered[region]) {
        ++remaining;
        break;
      }
      remaining = std::max(0, remaining - (discovered[region] + 1) / 2);
    }
  }
  bool unexploredNeighbor =
      remaining > 0 || (preferredRegion >= 0 && unvisited[preferredRegion] > 0);
  int best = 0;
  std::int64_t bestRank =
      unexploredNeighbor ? 16384 * 256 : length(source, target);
  for (int id = 1; id < int(nodes.size()); ++id) {
    const auto &node = nodes[id];
    if (!unexploredNeighbor && !targetIsDestination &&
        regions.regionAt(node.p) != startRegion)
      continue;
    std::int64_t rank = node.remaining;
    if (unexploredNeighbor) {
      int region = regions.regionAt(node.p);
      if (region == destinationRegion || region == preferredRegion)
        rank += node.cost / 2;
      else if (isNeighbor(node.p))
        rank = rank * 3 / 2;
      else
        rank *= region == startRegion ? 2 : 4;
      if (node.visited)
        rank *= 32;
    }
    if (rank < bestRank) {
      best = id;
      bestRank = rank;
    }
  }
  bool stuck = initiallyStuck || !unexploredNeighbor || preferredRegion < 0;
  if (diagnostics) {
    diagnostics->remainingCandidates = remaining;
    diagnostics->weightedFallback = unexploredNeighbor;
  }
  return finish(best, stuck, stuck);
}
std::vector<Point> localGroundRoute(const LocalSpace &space,
                                    const Regions &regions, Point source,
                                    Point target,
                                    std::optional<PixelBox> contact,
                                    std::optional<Point> adjustedTarget) {
  return planGroundRoute(space, regions, source, target, contact,
                         adjustedTarget)
      .points;
}
} // namespace mapeditor::simulation
