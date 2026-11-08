// SPDX-License-Identifier: MIT
#include <array>
#include <mapeditor/simulation/world.h>

namespace mapeditor::simulation {
void World::processLarva(UnitState &unit) {
  if (unit.type != 35 || !unit.motion)
    throw std::runtime_error("Invalid larva movement state");
  auto *base = get(unit.connected);
  auto move = [&](Point point, UnitHandle target = {}) {
    unit.motion->faceOnArrival(std::nullopt);
    unit.moveTargetPosition = point;
    unit.moveTargetUnit = target;
    unit.planRequested = true;
    unit.routePlanningFrames =
        !unit.motion->hasRoute() ||
                (unit.motion->arrived() && unit.motion->moving())
            ? 1
            : 0;
    unit.immovable = false;
  };
  if (base && !base->dead) {
    auto a = definitions_->units[unit.type].bounds;
    auto b = definitions_->units[base->type].bounds;
    Point gap{
        std::max({0, base->position.x - b.left - unit.position.x - a.right - 1,
                  unit.position.x - a.left - base->position.x - b.right - 1}),
        std::max({0, base->position.y - b.top - unit.position.y - a.bottom - 1,
                  unit.position.y - a.top - base->position.y - b.bottom - 1})};
    if (distance(gap) > 10) {
      // Legacy target retention applies both positive footprint offsets to x;
      // its south edge remains the building edge. Preserve that observable
      // asymmetry when deciding whether an existing target needs a refresh.
      PixelBox retained{base->position.x - b.left - a.right - 1,
                        base->position.y - b.top - a.bottom - 1,
                        base->position.x + b.right + a.left + a.top + 2,
                        base->position.y + b.bottom};
      auto p = unit.moveTargetPosition;
      bool border = retained.contains(p) &&
                    (p.x == retained.left || p.x == retained.right ||
                     p.y == retained.top || p.y == retained.bottom);
      bool keep = unit.moveTargetUnit == unit.connected &&
                  (samePoint(p, base->position) || border);
      if (keep)
        unit.motion->faceOnArrival(base->position);
      else
        move(base->position, unit.connected);
    }
  } else
    base = nullptr;
  bool atGoal = samePoint(unit.position, unit.moveTargetPosition);
  bool targetLostCreep =
      get(unit.moveTargetUnit) &&
      !samePoint(unit.moveTargetPosition, get(unit.moveTargetUnit)->position) &&
      !creep_.at(unit.moveTargetPosition.x / 32,
                 unit.moveTargetPosition.y / 32);
  if (!atGoal && !targetLostCreep)
    return;
  if ((!base || unit.moveTargetUnit != unit.connected) &&
      !creep_.at(unit.position.x / 32, unit.position.y / 32))
    throw std::runtime_error("Larva creep-loss death is not implemented yet");
  auto choice = random_.next();
  Point next{unit.position.x + ((choice & 8) ? 10 : -10),
             unit.position.y + ((choice & 128) ? 10 : -10)};
  if (next.x < 0 || next.y < 0 || next.x >= map_->terrain.width() * 32 ||
      next.y >= map_->terrain.height() * 32 ||
      !creep_.at(next.x / 32, next.y / 32))
    return;
  if (base) {
    if (unit.orderState < 0 || unit.orderState >= 4)
      throw std::runtime_error("Invalid connected larva side");
    constexpr std::array<Point, 4> normals{{{-1, 0}, {0, -1}, {1, 0}, {0, 1}}};
    const auto &bounds = definitions_->units[base->type].bounds;
    std::array<int, 4> extents{bounds.left, bounds.top, bounds.right,
                               bounds.bottom};
    auto normal = normals[unit.orderState];
    int extent = extents[unit.orderState];
    auto onSide = [&](Point p) {
      return normal.x * (p.x - base->position.x) +
                 normal.y * (p.y - base->position.y) >=
             extent;
    };
    if (!onSide(next)) {
      if (onSide(unit.position))
        return;
      next = {base->position.x + normal.x * (extent + 10),
              base->position.y + normal.y * (extent + 10)};
    }
  }
  move(next);
}
void World::processSecondary(UnitState &unit) {
  processProduction(unit);
  if (unit.spawnLarvae && !unit.orderTimer && unit.owner != 11) {
    int count = 0;
    for (int id : visible_) {
      const auto &candidate = *units_[id];
      if (candidate.type == 35 && candidate.connected == unit.handle &&
          std::abs(candidate.position.x - unit.position.x) <= 256 &&
          std::abs(candidate.position.y - unit.position.y) <= 256)
        ++count;
    }
    if (count < 3) {
      if (unit.larvaTimer)
        --unit.larvaTimer;
      else
        unit.larvaTimer = get(larva(unit.handle)) ? 37 : 3;
    }
  }
  if (unit.creepGrowth) {
    if (unit.creepTimer)
      --unit.creepTimer;
    else {
      unit.creepTimer = 15;
      const auto &definition = definitions_->units[unit.type];
      bool extended =
          unit.type == 132 || unit.type == 133 || unit.type == 144 ||
          unit.type == 146 ||
          (unit.completed && (unit.type == 131 || unit.type == 143));
      auto growth =
          creep_.grow(map_->terrain, unit.position,
                      {definition.placementWidth, definition.placementHeight},
                      extended, occupancy(), random_);
      if (!growth.grew && !growth.occupied)
        unit.creepGrowth = false;
    }
  }
}
} // namespace mapeditor::simulation
