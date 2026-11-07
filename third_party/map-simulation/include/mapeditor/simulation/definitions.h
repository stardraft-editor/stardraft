// SPDX-License-Identifier: MIT
#pragma once
#include "sprite.h"
#include <memory>
#include <string>

namespace mapeditor::simulation {
class OffsetTable {
public:
  explicit OffsetTable(std::span<const std::uint8_t>);
  Point at(int frame, int index, bool flip = false) const;
  std::size_t frames() const { return values_.size(); }
  const std::vector<std::vector<Point>> &values() const { return values_; }

private:
  std::vector<std::vector<Point>> values_;
};
// Immutable after loading: worlds and their copies share definitions, but
// retain all mutable unit, sprite, random and order state in their own value
// storage.
class Definitions {
public:
  using Read = std::function<std::vector<std::uint8_t>(const std::string &)>;
  explicit Definitions(const Read &);
  std::array<dat::Unit, 228> units;
  std::array<dat::Weapon, 130> weapons;
  std::array<dat::Order, 189> orders;
  std::array<dat::Flingy, 209> flingies;
  std::array<dat::Sprite, 517> sprites;
  std::array<dat::Image, 999> images;
  iscript::Program program;
  SpriteResources resources() const;
  int acquisitionRange(int type) const;
  int frameCount(int image) const { return frames_.at(image); }
  Point offset(int image, int frame, int kind, int index, bool flip) const;
  const OffsetTable *offsetTable(int image, int kind) const {
    return offsets_.at(image).at(kind).get();
  }

private:
  std::array<int, 999> frames_{};
  std::array<std::array<std::shared_ptr<const OffsetTable>, 6>, 999> offsets_;
};
} // namespace mapeditor::simulation
