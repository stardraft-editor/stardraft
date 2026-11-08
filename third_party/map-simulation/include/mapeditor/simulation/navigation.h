// SPDX-License-Identifier: MIT
#pragma once
#include "geometry.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <tuple>
#include <vector>

namespace mapeditor::simulation {
// Synthetic empty-terrain navigation. Real terrain must provide its actual
// region/obstacle graph; this helper is not a substitute for region generation.
inline std::vector<Point> openGroundRoute(Point from, Point target) {
  for (auto p : {from, target})
    if (p.x < 0 || p.y < 0 || p.x > 8191 || p.y > 8191)
      throw std::out_of_range("Route outside supported map");
  auto same = [](Point a, Point b) { return a.x == b.x && a.y == b.y; };
  if (same(from, target))
    return {target};
  auto region = [](Point p) { return std::pair{p.x / 256, p.y / 256}; };
  const auto sourceRegion = region(from), targetRegion = region(target);
  auto length = [](Point a, Point b) {
    return distance({(a.x - b.x) * 256, (a.y - b.y) * 256});
  };
  struct Node {
    Point p;
    int parent;
    std::int64_t cost;
  };
  std::vector<Node> nodes{{from, -1, 0}};
  std::map<std::pair<int, int>, int> coordinates{{{from.x, from.y}, 0}};
  using Entry = std::tuple<std::int64_t, std::uint64_t, int, std::int64_t>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
  std::uint64_t serial = 0;
  open.emplace(length(from, target), serial++, 0, 0);
  static constexpr std::array<Point, 8> directions = {
      {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}};
  while (!open.empty()) {
    auto [score, sequence, id, queuedCost] = open.top();
    open.pop();
    auto node = nodes[id];
    if (queuedCost != node.cost)
      continue;
    if (same(node.p, target)) {
      std::vector<Point> result;
      for (int n = id; nodes[n].parent >= 0; n = nodes[n].parent)
        result.push_back(nodes[n].p);
      std::reverse(result.begin(), result.end());
      return result;
    }
    std::vector<Point> candidates;
    for (auto direction : directions) {
      Point p{node.p.x + direction.x * 64, node.p.y + direction.y * 64};
      if (p.x >= 0 && p.y >= 0 && p.x <= 8191 && p.y <= 8191)
        candidates.push_back(p);
    }
    if (std::max(std::abs(target.x - node.p.x),
                 std::abs(target.y - node.p.y)) <= 64)
      candidates.push_back(target);
    for (auto point : candidates) {
      auto cost = std::int64_t(length(node.p, point));
      auto zone = region(point);
      if (zone != sourceRegion && zone != targetRegion)
        cost *= 2;
      cost += node.cost;
      auto key = std::pair{point.x, point.y};
      auto found = coordinates.find(key);
      int next;
      if (found == coordinates.end()) {
        if (nodes.size() >= 250)
          continue;
        next = int(nodes.size());
        coordinates.emplace(key, next);
        nodes.push_back({point, id, cost});
      } else {
        next = found->second;
        if (nodes[next].cost <= cost)
          continue;
        nodes[next].cost = cost;
        nodes[next].parent = id;
      }
      open.emplace(cost + length(point, target), serial++, next, cost);
    }
  }
  throw std::runtime_error("Empty-terrain navigation budget exhausted");
}
} // namespace mapeditor::simulation
