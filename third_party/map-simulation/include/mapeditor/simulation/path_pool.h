// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace mapeditor::simulation {
// Value-owned path history. Returning a slot retains its regional history;
// the next lease receives the most recently returned slot.
class PathPool {
public:
  struct Record {
    bool active = false;
    int created = 0;
    std::vector<int> regions;
    bool singleStep = false;
    std::optional<int> conversionRegion;
  };
  int acquire() {
    int slot;
    if (free_.empty()) {
      slot = int(records_.size());
      records_.emplace_back();
    } else {
      slot = free_.back();
      free_.pop_back();
    }
    records_[slot].active = true;
    return slot;
  }
  void release(int slot) {
    auto &record = live(slot);
    record.active = false;
    free_.push_back(slot);
  }
  void route(int slot, std::span<const int> regions, int created,
             std::optional<int> conversionRegion = {},
             bool retainedWaypoint = false) {
    if (created < 0 || (conversionRegion && *conversionRegion < 0) ||
        std::any_of(
            regions.begin(), regions.end(), [](int r) { return r < 0; }))
      throw std::invalid_argument("Invalid path history");
    auto &record = live(slot);
    record.created = created;
    record.regions.assign(regions.begin(), regions.end());
    record.singleStep = false;
    record.conversionRegion = conversionRegion;
    if (!retainedWaypoint)
      record.conversionRegion = regions.empty()
                                    ? std::optional<int>{}
                                    : std::optional<int>{regions.front()};
  }
  void escape(int slot) {
    auto &record = live(slot);
    // Single-step conversion uses the retained identity of the previous
    // route, whether that route is still active or its slot was returned.
    if (!record.singleStep) {
      record.regions.clear();
      if (record.conversionRegion)
        record.regions.push_back(*record.conversionRegion);
    } else if (record.regions.size() > 1)
      record.regions.resize(1);
    record.singleStep = true;
  }
  void shiftFrames(int delta) {
    for (auto &record : records_)
      record.created = std::max(0, record.created + delta);
  }
  const Record &record(int slot) const { return records_.at(slot); }
  const std::vector<Record> &records() const { return records_; }
  const std::vector<int> &freeOrder() const { return free_; }
  std::optional<int> firstRegion(int slot) const {
    const auto &regions = record(slot).regions;
    return regions.empty() ? std::nullopt : std::optional<int>(regions.front());
  }

private:
  Record &live(int slot) {
    auto &record = records_.at(slot);
    if (!record.active)
      throw std::logic_error("Path slot is not leased");
    return record;
  }
  std::vector<Record> records_;
  std::vector<int> free_;
};
} // namespace mapeditor::simulation
