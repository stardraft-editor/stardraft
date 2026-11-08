// SPDX-License-Identifier: MIT
#pragma once
#include "dat.h"
#include "geometry.h"
#include "random.h"
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

namespace mapeditor::simulation {
// Air separation has its own orientation and cadence, independent of velocity.
// The zero profile is represented directly, without an encoded sentinel.
struct AirState {
  int cell = -1, heading = 0;
  unsigned profile = 0, wait = 0;
  bool initialized = false;
};

class AirField {
public:
  AirField() = default;
  AirField(int width, int height) : width_(width), height_(height) {
    if (width < 1 || height < 1 || width > 8192 || height > 8192)
      throw std::invalid_argument("Invalid air-field dimensions");
    columns_ = (width + 47) / 48;
    density_.resize(std::size_t(columns_) * ((height + 47) / 48));
  }
  void insert(AirState &unit, Point position, Random &random) {
    if (unit.cell >= 0)
      throw std::logic_error("Air unit is already registered");
    int cell = cellAt(position);
    if (!unit.initialized) {
      unsigned sample = random.next();
      unit.heading = signedHeading(int(sample));
      unit.profile = sample >> 12;
      unit.initialized = true;
    }
    unit.wait = 0;
    unit.cell = cell;
    if (density_[cell] < 255)
      ++density_[cell];
  }
  void erase(AirState &unit) {
    if (unit.cell < 0)
      return;
    auto &value = density_.at(std::size_t(unit.cell));
    if (value)
      --value;
    unit.cell = -1;
  }
  void move(AirState &unit, Point position, Random &random) {
    if (cellAt(position) != unit.cell) {
      erase(unit);
      insert(unit, position, random);
    }
  }
  int strength(const AirState &unit) const {
    return unit.cell < 0 ? 0 : density_.at(std::size_t(unit.cell));
  }
  // Original arithmetic fitted against displacement observations. Profiles
  // select signs through a Gray-code bit and an alternating bit; no engine
  // adjustment table is embedded here.
  static Point vector(int direction, unsigned profile, int density,
                      bool resting) {
    if (profile > 7 || density < 2 || density > 255)
      throw std::invalid_argument("Invalid air separation input");
    auto result = directionVector(direction);
    result.x /= 2;
    result.y /= 2;
    if (resting) {
      result.x = result.x * (density / 4 + 1) / 2;
      result.y = result.y * (density / 4 + 1) / 2;
    }
    auto adjust = [](int value, bool positive) {
      if (!positive)
        return value + (value <= -20 ? -5 : 5);
      int sign = int(value > 0) - int(value < 0);
      return value + sign * (std::abs(value) <= 20 ? 5 : -5);
    };
    result.x = adjust(result.x, ((profile >> 1) ^ (profile >> 2)) & 1);
    result.y = adjust(result.y, profile & 1);
    return result;
  }
  std::optional<Point> push(AirState &unit, Point position, Point target,
                            dat::Extents bounds) {
    int density = strength(unit);
    if (density < 2) {
      unit.wait = 0;
      return {};
    }
    if (unit.wait)
      --unit.wait;
    else {
      unit.wait = 7;
      if (position.x != target.x || position.y != target.y) {
        int bearing = heading({target.x - position.x, target.y - position.y});
        // Keep the 32-heading phase inside the lateral band on this side of
        // the bearing, rather than steering the separation toward the goal.
        unsigned relative = unsigned(unit.heading - bearing) & 255U;
        unit.heading = signedHeading(bearing + 32 + int(relative & 31U) -
                                     96 * int(relative >> 7));
      }
      int horizontal = position.x - bounds.left < 32                 ? 1
                       : position.x + bounds.right + 1 > width_ - 32 ? -1
                                                                     : 0;
      int vertical = position.y - bounds.top < 32                    ? 1
                     : position.y + bounds.bottom + 1 > height_ - 32 ? -1
                                                                     : 0;
      if (horizontal || vertical) {
        unit.profile = 0;
        unit.initialized = false;
      }
      auto direction = directionVector(unit.heading);
      if (direction.x * horizontal < 0)
        unit.heading = signedHeading(-unit.heading);
      if (direction.y * vertical < 0)
        unit.heading = signedHeading(128 - unit.heading);
    }
    return vector(unit.heading, unit.profile, density,
                  position.x == target.x && position.y == target.y);
  }

private:
  int width_ = 0, height_ = 0, columns_ = 0;
  std::vector<std::uint8_t> density_;
  int cellAt(Point position) const {
    if (position.x < 0 || position.y < 0 || position.x >= width_ ||
        position.y >= height_)
      throw std::out_of_range("Air unit outside field bounds");
    return position.y / 48 * columns_ + position.x / 48;
  }
};
} // namespace mapeditor::simulation
