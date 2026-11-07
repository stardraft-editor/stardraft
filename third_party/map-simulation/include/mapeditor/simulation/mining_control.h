// SPDX-License-Identifier: MIT
#pragma once
#include "world.h"
#include <algorithm>

namespace mapeditor::simulation {
// Map Editor's first-party cargo/cycle policies, expressed against independent
// value state and command APIs. No reference-engine objects enter this state.
struct MiningControl {
  UnitHandle worker, field, neighbor;
  Point spot{}, previous{}, issuedAt{}, departure{}, firstAt{}, secondAt{};
  bool enabled = false, cycle = false, loaded = false, issued = false;
  bool failed = false, cargoSawEmpty = false;
  int commands = 0, delay = 0, detour = 0;
  int delivered = -1, stage = -1, redirected = -1;
  int cargoDelay = -1, cargoLoadedFrame = -1;

  static int gap(const World &world, const UnitState &a, const UnitState &b) {
    const auto &x = world.definitions().units[a.type].bounds;
    const auto &y = world.definitions().units[b.type].bounds;
    int dx = std::max({0, b.position.x - y.left - a.position.x - x.right - 1,
                       a.position.x - x.left - b.position.x - y.right - 1});
    int dy = std::max({0, b.position.y - y.top - a.position.y - x.bottom - 1,
                       a.position.y - x.top - b.position.y - y.bottom - 1});
    return distance({dx, dy});
  }
  bool apply(World &world) {
    auto *unit = world.get(worker);
    const auto *mineral = world.get(field);
    if (!unit || unit->dead || !unit->visible || !unit->completed ||
        !unit->motion || !(world.definitions().units[unit->type].flags & 8))
      return false;
    auto validMineral = [](const UnitState *u) {
      return u && !u->dead && u->visible && u->type >= 176 && u->type <= 178;
    };
    if (cycle) {
      const auto *other = world.get(neighbor);
      if (!validMineral(mineral) || !validMineral(other))
        return false;
      bool carrying = (unit->cargo & 3) == 2;
      if (loaded && !carrying) {
        delivered = world.frame();
        stage = 0;
      }
      loaded = carrying;
      if (carrying)
        stage = -1;
      if (!enabled || carrying || (unit->cargo & 3) || stage < 0 || stage == 2)
        return false;
      if (stage == 0) {
        if (world.frame() - delivered < delay ||
            unit->resourceTarget != field || gap(world, *unit, *mineral) <= 10)
          return false;
        firstAt = departure = unit->position;
        redirected = world.frame();
        world.harvest(worker, neighbor);
        stage = 1;
        ++commands;
        return true;
      }
      bool contact = gap(world, *unit, *other) <= 10 || unit->order == 86 ||
                     unit->order == 87;
      if (world.frame() - redirected < detour && !contact)
        return false;
      if (contact || samePoint(unit->position, departure) ||
          unit->resourceTarget != neighbor)
        failed = true;
      secondAt = unit->position;
      world.harvest(worker, field);
      stage = 2;
      ++commands;
      return true;
    }
    bool carrying = (unit->cargo & 3) == 2;
    if (!carrying || !loaded)
      issued = false;
    if (!carrying) {
      cargoLoadedFrame = -1;
      cargoSawEmpty = true;
    } else if (!loaded)
      cargoLoadedFrame = world.frame();
    Point from = loaded ? previous : unit->position;
    previous = unit->position;
    loaded = carrying;
    if (!enabled || !carrying || issued || !validMineral(mineral) ||
        unit->resourceTarget != field)
      return false;
    if (cargoDelay >= 0) {
      if (!cargoSawEmpty || cargoLoadedFrame < 0 ||
          world.frame() - cargoLoadedFrame < cargoDelay)
        return false;
    } else {
      if (unit->order != 90 || unit->orderState != 1 ||
          unit->motion->speed() <= 0 || samePoint(from, unit->position))
        return false;
      double dx = unit->position.x - from.x, dy = unit->position.y - from.y;
      double along =
          std::clamp(((spot.x - from.x) * dx + (spot.y - from.y) * dy) /
                         (dx * dx + dy * dy),
                     0.0, 1.0);
      double ex = from.x + along * dx - spot.x;
      double ey = from.y + along * dy - spot.y;
      if (ex * ex + ey * ey > 4.0)
        return false;
    }
    issuedAt = unit->position;
    if (!world.returnCargo(worker))
      return false;
    issued = true;
    ++commands;
    return true;
  }
};
} // namespace mapeditor::simulation
