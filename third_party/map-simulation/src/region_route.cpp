// SPDX-License-Identifier: MIT
#include <algorithm>
#include <limits>
#include <mapeditor/simulation/region_route.h>
#include <stdexcept>
namespace mapeditor::simulation {
namespace {
struct Label {
  std::int64_t cost = std::numeric_limits<std::int64_t>::max(), remaining = 0;
  int parent = -1, heap = -1;
};
// Indexed heap keeps frontier capacity independent of the number of relaxed
// edges. Equal insertions rise; extraction retains the left child on equal
// cost.
class Frontier {
public:
  explicit Frontier(std::vector<Label> &labels) : labels_(labels) {}
  std::size_t size() const { return ids_.size(); }
  bool empty() const { return ids_.empty(); }
  int first() const { return ids_.front(); }
  void offer(int id) {
    int at = labels_[id].heap;
    if (at < 0) {
      at = int(ids_.size());
      ids_.push_back(id);
      labels_[id].heap = at;
    }
    while (at > 0) {
      int parent = (at - 1) / 2;
      if (score(ids_[parent]) < score(id))
        break;
      exchange(at, parent);
      at = parent;
    }
  }
  int pop() {
    int result = ids_.front();
    exchange(0, int(ids_.size()) - 1);
    ids_.pop_back();
    labels_[result].heap = -1;
    int at = 0;
    while (at * 2 + 1 < int(ids_.size())) {
      int child = at * 2 + 1;
      if (child + 1 < int(ids_.size()) &&
          score(ids_[child + 1]) < score(ids_[child]))
        ++child;
      if (score(ids_[at]) <= score(ids_[child]))
        break;
      exchange(at, child);
      at = child;
    }
    return result;
  }

private:
  std::vector<Label> &labels_;
  std::vector<int> ids_;
  std::int64_t score(int id) const {
    return labels_[id].cost + labels_[id].remaining;
  }
  void exchange(int a, int b) {
    std::swap(ids_[a], ids_[b]);
    labels_[ids_[a]].heap = a;
    labels_[ids_[b]].heap = b;
  }
};
} // namespace
RegionRoute regionRoute(const Regions &graph, Point source, Point destination) {
  int start = graph.regionAt(source), end = graph.regionAt(destination);
  if (start < 0 || end < 0)
    throw std::out_of_range("Region route outside map");
  if (start == end)
    return {{start}, 1, 0, 0};
  const auto &regions = graph.entries();
  auto position = [&](int id) {
    return id == start ? source : id == end ? destination : regions[id].center;
  };
  auto length = [](Point a, Point b) {
    return distance({(a.x - b.x) * 256, (a.y - b.y) * 256});
  };
  RegionRoute result;
  std::vector<Label> labels;
  std::vector<int> discovered;
  auto search = [&](int from, int to) {
    labels.assign(regions.size(), {});
    discovered = {from};
    Frontier frontier(labels);
    labels[from].cost = 0;
    labels[from].remaining = 16384 * 256;
    frontier.offer(from);
    while (!frontier.empty()) {
      int current = frontier.pop();
      if (current == to)
        return current;
      auto relax = [&](int next) {
        if (frontier.size() >= 125 || discovered.size() >= 350)
          return;
        auto edge = std::int64_t(length(position(current), position(next)));
        if (regions[next].group != regions[start].group)
          edge *= 2;
        auto cost = labels[current].cost + edge;
        if (cost >= labels[next].cost || labels[current].parent == next)
          return;
        if (labels[next].cost == std::numeric_limits<std::int64_t>::max()) {
          discovered.push_back(next);
          labels[next].remaining = length(position(next), position(to));
        }
        labels[next].cost = cost;
        labels[next].parent = current;
        frontier.offer(next);
      };
      for (int next : regions[current].walking)
        relax(next);
      if (regions[current].group != regions[start].group)
        for (int next : regions[current].blocked)
          relax(next);
      result.peakFrontier = std::max(result.peakFrontier, int(frontier.size()));
      if (frontier.size() >= 125 || discovered.size() >= 350)
        break;
    }
    if (!frontier.empty())
      return frontier.first();
    int closest = from;
    auto remaining = length(position(to), regions[from].center);
    for (int id : discovered) {
      auto value = id == from ? remaining : labels[id].remaining;
      if (value < remaining) {
        remaining = value;
        closest = id;
      }
    }
    return closest;
  };
  bool reverse = regions[start].group != regions[end].group;
  int goal = reverse ? search(end, start) : search(start, end);
  if (reverse && goal != start) {
    goal = search(start, goal);
    reverse = false;
  }
  result.nodes = int(discovered.size());
  for (int id = goal; id >= 0; id = labels[id].parent) {
    if (reverse && regions[id].group != regions[goal].group)
      break;
    result.regions.push_back(id);
  }
  if (!reverse)
    std::reverse(result.regions.begin(), result.regions.end());
  result.fullSize = int(result.regions.size());
  if (result.regions.size() > 50)
    result.regions.resize(50);
  return result;
}
} // namespace mapeditor::simulation
