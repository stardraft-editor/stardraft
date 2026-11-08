// SPDX-License-Identifier: MIT
#pragma once
#include "geometry.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
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
    moveImpl(key, first, last, {});
  }
  void move(Key key, Point first, Point last, Point oldFirst, Point oldLast) {
    moveImpl(key, first, last, std::array<Point, 2>{oldFirst, oldLast});
  }

private:
  void moveImpl(Key key, Point first, Point last,
                const std::optional<std::array<Point, 2>> &previous) {
    if (!keys_.contains(key))
      throw std::logic_error("Missing spatial bounds");
    // Hints are validated against the current array before use. They remain
    // safe after copies, insertions, removals and address reuse; no world or
    // unit state is retained here. Large external keys use the normal search.
    thread_local std::array<std::array<std::size_t, 4>, 1700> hints{};
    for (int axis = 0; axis < 2; ++axis) {
      auto &events = axes_[axis];
      std::array<std::size_t, 2> at{events.size(), events.size()};
      if (key < hints.size())
        for (int maximum = 0; maximum < 2; ++maximum) {
          auto hint = hints[key][axis * 2 + maximum];
          if (hint < events.size() && events[hint].key == key &&
              events[hint].maximum == bool(maximum))
            at[maximum] = hint;
        }
      if (previous && (at[0] == events.size() || at[1] == events.size()))
        for (int maximum = 0; maximum < 2; ++maximum) {
          const auto p = (*previous)[maximum];
          const int coordinate = axis ? p.y : p.x;
          auto begin = std::lower_bound(
              events.begin(), events.end(), coordinate,
              [](const auto &e, int value) { return e.coordinate < value; });
          for (; begin != events.end() && begin->coordinate == coordinate;
               ++begin)
            if (begin->key == key && begin->maximum == bool(maximum)) {
              at[maximum] = std::size_t(begin - events.begin());
              break;
            }
        }
      if (at[0] == events.size() || at[1] == events.size())
        for (std::size_t i = 0; i < events.size(); ++i)
          if (events[i].key == key)
            at[events[i].maximum] = i;
      if (at[0] == events.size() || at[1] == events.size())
        throw std::logic_error("Missing spatial boundary");
      if (key < hints.size())
        for (int maximum = 0; maximum < 2; ++maximum)
          hints[key][axis * 2 + maximum] = at[maximum];
      const int low = axis ? first.y : first.x, high = axis ? last.y : last.x;
      if (events[at[0]].coordinate == low && events[at[1]].coordinate == high)
        continue;
      const auto lower = std::lower_bound(
          events.begin(), events.end(), std::min(low, high),
          [](const auto &e, int value) { return e.coordinate < value; });
      const auto upper = std::upper_bound(
          events.begin(), events.end(), std::max(low, high),
          [](int value, const auto &e) { return value < e.coordinate; });
      const auto begin =
          std::min({std::size_t(lower - events.begin()), at[0], at[1]});
      const auto end =
          std::max({std::size_t(upper - events.begin()), at[0] + 1, at[1] + 1});
      events[at[0]].coordinate = low;
      events[at[1]].coordinate = high;
      // The unchanged prefix/suffix remain sorted. Include equal endpoints
      // in the repair interval and retain their original stable tie order.
      for (std::size_t i = begin + 1; i < end; ++i) {
        const auto entry = events[i];
        std::size_t j = i;
        while (j > begin && events[j - 1].coordinate > entry.coordinate) {
          events[j] = events[j - 1];
          --j;
        }
        events[j] = entry;
      }
      for (std::size_t i = begin; i < end; ++i)
        if (events[i].key < hints.size())
          hints[events[i].key][axis * 2 + int(events[i].maximum)] = i;
    }
  }

public:
  void remove(Key key) {
    if (!keys_.erase(key))
      return;
    for (auto &events : axes_)
      std::erase_if(events,
                    [&](const auto &entry) { return entry.key == key; });
  }
  // Crossed faces are ordered by X before Y, then travel distance, then
  // stable sweep order in the direction of travel. The callback checks the
  // body's eligibility and overlap on the perpendicular axis.
  template <class Eligible>
  std::optional<Key>
  firstCrossed(const std::array<int, 2> &low, const std::array<int, 2> &high,
               const std::array<int, 2> &delta, Eligible eligible) const {
    for (int axis = 0; axis < 2; ++axis) {
      if (!delta[axis])
        continue;
      const auto &events = axes_[axis];
      const std::int64_t leading = delta[axis] > 0 ? high[axis] : low[axis];
      const auto ending = leading + delta[axis];
      auto first = std::lower_bound(events.begin(), events.end(),
                                    std::min(leading, ending),
                                    [](const auto &entry, auto coordinate) {
                                      return entry.coordinate < coordinate;
                                    });
      auto last =
          std::upper_bound(first, events.end(), std::max(leading, ending),
                           [](auto coordinate, const auto &entry) {
                             return coordinate < entry.coordinate;
                           });
      if (delta[axis] > 0) {
        for (auto at = first; at != last; ++at)
          if (eligible(at->key, 1 - axis))
            return at->key;
      } else {
        while (last != first) {
          --last;
          if (eligible(last->key, 1 - axis))
            return last->key;
        }
      }
    }
    return {};
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
