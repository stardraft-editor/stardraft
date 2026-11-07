// SPDX-License-Identifier: MIT
#pragma once
#include "geometry.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mapeditor::simulation {
// Value-state ordering of footprint boundaries. Moving boundaries keep their
// relative order when they meet; newly inserted boundaries precede equal ones.
class SpatialBounds {
public:
  using Key = std::uint32_t;
  struct Entry {
    int coordinate;
    Key key;
    bool maximum;
  };
  void insert(Key key, Point first, Point last) {
    if (!keys_.insert(key).second)
      throw std::logic_error("Duplicate spatial bounds");
    for (int axis = 0; axis < 2; ++axis) {
      auto &events = axes_[axis];
      for (bool maximum : {false, true}) {
        auto p = maximum ? last : first;
        int coordinate = axis ? p.y : p.x;
        auto at = std::lower_bound(events.begin(), events.end(), coordinate,
                                   [](const auto &entry, int value) {
                                     return entry.coordinate < value;
                                   });
        events.insert(at, {coordinate, key, maximum});
      }
    }
  }
  void move(Key key, Point first, Point last) {
    if (!keys_.contains(key))
      throw std::logic_error("Missing spatial bounds");
    for (int axis = 0; axis < 2; ++axis) {
      auto &events = axes_[axis];
      bool changed = false;
      for (auto &entry : events)
        if (entry.key == key) {
          auto p = entry.maximum ? last : first;
          int coordinate = axis ? p.y : p.x;
          changed |= coordinate != entry.coordinate;
          entry.coordinate = coordinate;
        }
      if (changed) {
        // Only this body's two boundaries changed. Restore the nearly sorted
        // sweep without allocating a merge buffer for every movement frame.
        // Strict comparison retains the same equal-coordinate order as a
        // stable sort, including when the two boundaries cross other bodies.
        for (std::size_t i = 1; i < events.size(); ++i) {
          const auto entry = events[i];
          std::size_t j = i;
          while (j && events[j - 1].coordinate > entry.coordinate) {
            events[j] = events[j - 1];
            --j;
          }
          events[j] = entry;
        }
      }
    }
  }
  void remove(Key key) {
    if (!keys_.erase(key))
      return;
    for (auto &events : axes_)
      std::erase_if(events,
                    [&](const auto &entry) { return entry.key == key; });
  }
  std::unordered_map<Key, int> encounters(Point origin) const {
    std::unordered_map<Key, int> ranks;
    ranks.reserve(keys_.size());
    for (int axis = 0; axis < 2; ++axis) {
      const auto &events = axes_[axis];
      int coordinate = axis ? origin.y : origin.x;
      int pivot = int(std::lower_bound(events.begin(), events.end(), coordinate,
                                       [](const auto &entry, int value) {
                                         return entry.coordinate < value;
                                       }) -
                      events.begin());
      for (int i = 0; i < int(events.size()); ++i) {
        int rank = i < pivot ? (pivot - i - 1) * 4 + axis * 2
                             : (i - pivot) * 4 + axis * 2 + 1;
        auto [at, inserted] = ranks.try_emplace(events[i].key, rank);
        if (!inserted)
          at->second = std::min(at->second, rank);
      }
    }
    return ranks;
  }
  // A visible unit anchors the two sweeps at its own boundary entries.
  // Overlapping footprints can therefore be encountered on either side.
  std::unordered_map<Key, int> encountersFrom(Key key) const {
    if (!keys_.contains(key))
      throw std::invalid_argument("Missing spatial search anchor");
    std::unordered_map<Key, int> ranks;
    ranks.reserve(keys_.size());
    for (int axis = 0; axis < 2; ++axis) {
      const auto &events = axes_[axis];
      int minimum = -1, maximum = -1;
      for (int i = 0; i < int(events.size()); ++i)
        if (events[i].key == key)
          (events[i].maximum ? maximum : minimum) = i;
      if (minimum < 0 || maximum < 0)
        throw std::logic_error("Missing spatial anchor boundary");
      for (int i = 0; i < int(events.size()); ++i) {
        int rank = std::numeric_limits<int>::max();
        if (i < maximum)
          rank = (maximum - i - 1) * 4 + axis * 2;
        if (i > minimum)
          rank = std::min(rank, (i - minimum - 1) * 4 + axis * 2 + 1);
        if (rank == std::numeric_limits<int>::max())
          continue;
        auto [at, inserted] = ranks.try_emplace(events[i].key, rank);
        if (!inserted)
          at->second = std::min(at->second, rank);
      }
    }
    return ranks;
  }
  const std::array<std::vector<Entry>, 2> &axes() const { return axes_; }

private:
  std::array<std::vector<Entry>, 2> axes_;
  std::unordered_set<Key> keys_;
};
} // namespace mapeditor::simulation
