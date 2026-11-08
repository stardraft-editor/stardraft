// SPDX-License-Identifier: MIT
// Deterministic geometry reconstructed from mathematical definitions and
// black-box observations. No engine lookup tables are copied into this file.
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mapeditor::simulation {
struct Point { int x = 0, y = 0; };
// Clip a ray from the middle half of a target's bounds toward the shooter.
// Vertical clipping precedes horizontal clipping; each step retains integer
// truncation so corner intersections stay deterministic.
inline Point projectileImpactPoint(Point shooter, Point boxMin, Point boxMax) {
  if (boxMax.x < boxMin.x || boxMax.y < boxMin.y)
    throw std::invalid_argument("Invalid projectile target bounds");
  const int insetX = (boxMax.x - boxMin.x) / 4;
  const int insetY = (boxMax.y - boxMin.y) / 4;
  const Point low{boxMin.x + insetX, boxMin.y + insetY};
  const Point high{boxMax.x - insetX, boxMax.y - insetY};
  const Point center{low.x + (high.x - low.x) / 2,
                     low.y + (high.y - low.y) / 2};
  const int y = shooter.y < low.y ? low.y : shooter.y > high.y ? high.y : shooter.y;
  if (y != shooter.y) {
    shooter.x = center.x + int(std::int64_t(shooter.x - center.x) *
                               (y - center.y) / (shooter.y - center.y));
    shooter.y = y;
  }
  const int x = shooter.x < low.x ? low.x : shooter.x > high.x ? high.x : shooter.x;
  if (x != shooter.x) {
    shooter.y = center.y + int(std::int64_t(shooter.y - center.y) *
                               (x - center.x) / (shooter.x - center.x));
    shooter.x = x;
  }
  return shooter;
}
inline int signedHeading(int value) {
  auto byte = unsigned(value) & 255U;
  return byte >= 128 ? int(byte) - 256 : int(byte);
}
namespace geometry_detail {
constexpr long double pi = 3.141592653589793238462643383279502884L;
constexpr long double sine(long double x) {
  while (x > pi) x -= 2 * pi;
  while (x < -pi) x += 2 * pi;
  long double sum = x, term = x;
  for (int n = 1; n < 18; ++n) {
    term *= -x * x / ((2 * n) * (2 * n + 1));
    sum += term;
  }
  return sum;
}
constexpr int nearest(long double v) { return int(v < 0 ? v - .5L : v + .5L); }
constexpr std::array<Point, 256> vectors() {
  std::array<Point, 256> result{};
  for (int i = 0; i < 256; ++i) {
    auto angle = i * pi / 128;
    result[i] = {nearest(256 * sine(angle)),
                 nearest(-256 * sine(angle + pi / 2))};
  }
  return result;
}
constexpr std::array<std::uint32_t, 64> slopes() {
  std::array<std::uint32_t, 64> result{};
  for (int i = 0; i < 63; ++i) {
    auto angle = (i + 1) * pi / 128;
    auto value = 256 * sine(angle) / sine(pi / 2 - angle);
    auto integer = std::uint32_t(value);
    result[i] = i == 31 ? 256 : integer + (value > integer ? 1 : 0);
  }
  result[63] = std::numeric_limits<std::uint32_t>::max();
  return result;
}
constexpr auto directions = vectors();
constexpr auto tangentBounds = slopes();
constexpr std::uint64_t magnitude(int value) {
  return value < 0 ? std::uint64_t(-std::int64_t(value)) : std::uint64_t(value);
}
} // namespace geometry_detail

// Brood War's integer distance metric. This deliberately preserves its
// non-Euclidean diagonal distances and rounding at the quarter-slope boundary.
inline int distance(Point delta) {
  auto a = geometry_detail::magnitude(delta.x), b = geometry_detail::magnitude(delta.y);
  if (a < b) { auto t = a; a = b; b = t; }
  auto extra = b * 3 / 8;
  auto result = b <= a / 4 ? a : a - a / 16 - a / 64 + extra + extra / 32;
  if (result > std::uint64_t(std::numeric_limits<int>::max()))
    throw std::out_of_range("Distance exceeds supported coordinates");
  return int(result);
}
// Signed 8-bit heading: 0 north, 64 east, -128 south, -64 west.
// Quantization is asymmetric at diagonal thresholds; atan2 rounding differs.
inline int heading(Point delta) {
  if (!delta.x) return delta.y > 0 ? -128 : 0;
  auto slope = geometry_detail::magnitude(delta.y) * 256 /
               geometry_detail::magnitude(delta.x);
  int low = 0, high = 63;
  while (low < high) {
    int middle = (low + high) / 2;
    if (geometry_detail::tangentBounds[middle] < slope) low = middle + 1;
    else high = middle;
  }
  int result = delta.y < 0 ? 64 - low : 64 + low;
  return delta.x < 0 ? -result : result;
}
constexpr Point directionVector(int index) {
  return geometry_detail::directions[std::uint8_t(index)];
}
// Flat-terrain sight footprints, generated with midpoint-circle rasterization.
// Reference measurements retain a 3x3 center and expose a nine-cell shortfall
// on the final perimeter of airborne footprints. No sight tables are imported.
inline const std::vector<Point> &sightFootprint(int range, bool airborne) {
  if (range < 0 || range > 11)
    throw std::out_of_range("Unsupported sight range");
  static const auto footprints = [] {
    std::array<std::vector<Point>, 24> result;
    for (int air = 0; air < 2; ++air)
      for (int sight = 0; sight < 12; ++sight) {
        std::array<std::array<bool, 25>, 25> cells{};
        auto span = [&](int row, int extent) {
          for (int x = -extent; x <= extent; ++x)
            cells[row + 12][x + 12] = true;
        };
        for (int row = -1; row <= 1; ++row)
          span(row, 1);
        int radius = sight + 1;
        int x = 0, y = radius;
        while (x <= y) {
          span(x, y);
          span(-x, y);
          span(y, x);
          span(-y, x);
          ++x;
          if (4 * x * x + (2 * y - 1) * (2 * y - 1) > 4 * radius * radius)
            --y;
        }
        if (air) {
          int remaining = 9;
          auto omit = [&](int col, int row) {
            auto &cell = cells[row + 12][col + 12];
            if (remaining && cell) {
              cell = false;
              --remaining;
            }
          };
          for (int row = -radius; row <= radius; ++row)
            omit(-radius, row);
          for (int col = -radius; col <= radius; ++col)
            omit(col, radius);
          for (int row = -radius; row <= radius; ++row)
            omit(radius, row);
          for (int row = -radius; row <= radius && remaining; ++row)
            for (int col = -radius; col <= radius && remaining; ++col)
              omit(col, row);
        }
        auto &footprint = result[air * 12 + sight];
        for (int row = -radius; row <= radius; ++row)
          for (int col = -radius; col <= radius; ++col)
            if (cells[row + 12][col + 12])
              footprint.push_back({col, row});
      }
    return result;
  }();
  return footprints[int(airborne) * 12 + range];
}
} // namespace mapeditor::simulation
