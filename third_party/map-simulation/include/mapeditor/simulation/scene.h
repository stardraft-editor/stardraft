// SPDX-License-Identifier: MIT
#pragma once
#include "geometry.h"
#include <algorithm>
#include <list>
#include <map>
#include <vector>

namespace mapeditor::simulation {
// Copyable spatial draw order. A row retains its oldest anchor; subsequent
// arrivals enter immediately after it. Moving within a row keeps that order.
class SpriteRows {
public:
  using Key = std::uint64_t;
  explicit SpriteRows(int height = 256) : height_(height) {
    if (height < 1)
      throw std::invalid_argument("Invalid sprite row count");
  }
  Key create(Point p) {
    Key key = next_++;
    enter(key, row(p));
    return key;
  }
  void remove(Key key) {
    auto found = positions_.find(key);
    if (found == positions_.end())
      return;
    auto line = lines_.find(found->second);
    line->second.remove(key);
    if (line->second.empty())
      lines_.erase(line);
    positions_.erase(found);
  }
  void move(Key key, Point p) {
    auto found = positions_.find(key);
    if (found == positions_.end())
      throw std::invalid_argument("Missing sprite row entry");
    int destination = row(p);
    if (found->second == destination)
      return;
    remove(key);
    enter(key, destination);
  }
  std::vector<Key> ordered() const {
    std::vector<Key> result;
    result.reserve(positions_.size());
    for (const auto &[row, line] : lines_)
      result.insert(result.end(), line.begin(), line.end());
    return result;
  }

private:
  int height_;
  Key next_ = 0;
  std::map<int, std::list<Key>> lines_;
  std::map<Key, int> positions_;
  int row(Point p) const { return std::clamp(p.y / 32, 0, height_ - 1); }
  void enter(Key key, int row) {
    auto &line = lines_[row];
    auto at = line.begin();
    if (at != line.end())
      ++at;
    line.insert(at, key);
    positions_.emplace(key, row);
  }
};
} // namespace mapeditor::simulation
