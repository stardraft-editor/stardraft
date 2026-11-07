// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/psi.h>
#include <mapeditor/simulation/world.h>
namespace mapeditor::simulation {
bool World::constructionSiteFits(const UnitState &worker, int type,
                                 Point center) const {
  const auto &building = definitions_->units[type];
  int width = building.placementWidth / 32,
      height = building.placementHeight / 32;
  Point tile{(center.x - building.placementWidth / 2) / 32,
             (center.y - building.placementHeight / 2) / 32};
  if (width <= 0 || height <= 0 || tile.x < 0 || tile.y < 0 ||
      tile.x > map_->terrain.width() - width ||
      tile.y > map_->terrain.height() - height)
    return false;
  int gasSite = -1;
  if (type == 149) {
    for (int id : visible_) {
      const auto &resource = *units_[id];
      if (!resource.dead && resource.type == 188 &&
          samePoint(resource.position, center)) {
        gasSite = id;
        break;
      }
    }
    if (gasSite < 0)
      return false;
  }
  // A refinery reserves the existing gas site, while every other building
  // remains an obstruction. The resource itself survives admission.
  auto occupied = occupancy(gasSite);
  if (building.flags & 0x80000U) {
    bool powered = false;
    for (int id : visible_) {
      const auto &provider = *units_[id];
      if (provider.type == 156 && !provider.dead && provider.psiActive &&
          provider.owner == worker.owner &&
          withinPsiField({center.x - provider.position.x,
                          center.y - provider.position.y})) {
        powered = true;
        break;
      }
    }
    if (!powered)
      return false;
  }
  bool zerg = building.groupFlags & 1;
  bool requiresCreep = gasSite < 0 && (building.flags & 0x20000U);
  for (int y = tile.y; y < tile.y + height; ++y)
    for (int x = tile.x; x < tile.x + width; ++x)
      if (!(exploredAt({x, y}) & (1U << worker.owner)) ||
          (requiresCreep ? !creep_.at(x, y)
                         : gasSite < 0 && !map_->terrain.tile(x, y).buildable) ||
          occupied[std::size_t(y) * map_->terrain.width() + x] ||
          (!zerg && creep_.at(x, y)))
        return false;
  auto here = map_->regions.regionAtPreferWalkable(worker.position),
       there = map_->regions.regionAtPreferWalkable({tile.x * 32, tile.y * 32});
  if (map_->regions.entries()[here].group !=
      map_->regions.entries()[there].group)
    return false;
  bool depot = type == 106 || type == 131 || type == 154;
  PixelBox reserved{tile.x * 32 - 96, tile.y * 32 - 96,
                    (tile.x + width) * 32 + 95, (tile.y + height) * 32 + 95};
  for (int id : visible_) {
    const auto &body = *units_[id];
    const auto &definition = definitions_->units[body.type];
    if (body.dead || body.handle == worker.handle || (definition.flags & 4))
      continue;
    const auto &bounds = definition.bounds;
    if (depot && (definition.flags & 0x2000U) &&
        body.position.x + bounds.right >= reserved.left &&
        body.position.x - bounds.left <= reserved.right &&
        body.position.y + bounds.bottom >= reserved.top &&
        body.position.y - bounds.top <= reserved.bottom)
      return false;
    if (!definition.building() &&
        body.position.x + bounds.right >= center.x - building.bounds.left &&
        body.position.x - bounds.left <= center.x + building.bounds.right &&
        body.position.y + bounds.bottom >= center.y - building.bounds.top &&
        body.position.y - bounds.top <= center.y + building.bounds.bottom)
      return false;
  }
  return true;
}
bool World::build(UnitHandle handle, int type, Point tile, int order) {
  auto *worker = get(handle);
  if (!worker || worker->dead || !worker->visible || !worker->completed ||
      worker->hallucinated || !worker->motion || type < 0 || type >= 228)
    return false;
  const auto &actor = definitions_->units[worker->type];
  if (worker->order == 70 && worker->orderState == 2)
    return false;
  const auto &building = definitions_->units[type];
  int expected = worker->type == 41 ? 25 : worker->type == 7 ? 30 : 31;
  if (!(actor.flags & 8) || !building.building() || order != expected ||
      !(actor.groupFlags & building.groupFlags & 7))
    return false;
  int width = building.placementWidth / 32,
      height = building.placementHeight / 32;
  if (width <= 0 || height <= 0 || tile.x < 0 || tile.y < 0 ||
      tile.x > map_->terrain.width() - width ||
      tile.y > map_->terrain.height() - height)
    return false;
  const auto &balance = economy_.resources(worker->owner);
  if (balance.minerals < building.mineralCost || balance.gas < building.gasCost)
    return false;
  if (type == 162) {
    if (!population(worker->owner).completed[166])
      return false;
  } else if (type == 160 || type == 166) {
    if (worker->type != 64 || !population(worker->owner).completed[154])
      return false;
  } else if (type == 142) {
    auto available = population(worker->owner);
    int bases =
        available.completed[131] + available.units[132] + available.units[133];
    if (!bases)
      return false;
  } else if (type != 106 && type != 109 && type != 131 && type != 149 && type != 154 &&
             type != 156)
    throw std::runtime_error("Construction prerequisites are not implemented");
  Point center{tile.x * 32 + building.placementWidth / 2,
               tile.y * 32 + building.placementHeight / 2};
  if (!constructionSiteFits(*worker, type, center))
    return false;
  worker->queuedCommands.clear();
  worker->queuedUserOverride = false;
  worker->buildQueue = {type};
  if (interruptHarvest(*worker, {order, center, {}}))
    return true;
  beginConstruction(*worker, center, order);
  return true;
}
void World::beginConstruction(UnitState &worker, Point center, int order) {
  if (worker.buildQueue.size() != 1)
    throw std::logic_error("Construction intent has no unique building type");
  prepareOrderTransition(worker);
  worker.target = {};
  worker.targetPosition = center;
  worker.pendingRoute.clear();
  worker.failedRouteFrames = 0;
  worker.order = order;
  worker.orderState = worker.orderTimer = 0;
}
void World::processConstruction(UnitState &worker) {
  auto resources = definitions_->resources();
  auto host = [&](const auto &instruction, const auto &layer) {
    return effect(worker.handle, instruction, layer);
  };
  auto recoverLanding = [&] {
    worker.gasLanding = false;
    for (auto id : worker.sprite.order) {
      auto &layer = worker.sprite.images[id];
      if (layer.modifier == 10) {
        layer.offset.y = 7;
        layer.frozenWorldY.reset();
      }
    }
    worker.target = {};
    worker.targetPosition = {};
    worker.order = 150;
    worker.orderState = worker.orderTimer = 0;
    worker.collisionPending = true;
    worker.collisionWait = 1;
  };
  if (worker.order == 150) {
    if (worker.type != 41 || !worker.motion)
      throw std::runtime_error("Unsupported construction collision recovery");
    discardPath(worker);
    worker.collisionRouteReset = true;
    worker.order = definitions_->units[worker.type].returnIdle;
    worker.orderState = worker.orderTimer = 0;
    worker.collisionPending = true;
    worker.collisionWait = 2;
    notifyOverlap(worker);
    auto continuation = worker.landingContinuation;
    worker.landingContinuation.reset();
    if (continuation) {
      if (continuation->order == 6)
        move(worker.handle, continuation->position);
      else if (continuation->order == 2)
        guard(worker.handle, continuation->position);
      else if (continuation->order == 14)
        attackMove(worker.handle, continuation->position);
      else if (continuation->order == 10)
        attack(worker.handle, continuation->target);
      else if (continuation->order == 49)
        follow(worker.handle, continuation->target);
      else if (continuation->order == 79 || continuation->order == 81 || continuation->order == 85)
        harvest(worker.handle, continuation->target,
                continuation->order == 81 ? HarvestOrder::Gas
                : continuation->order == 85 ? HarvestOrder::Minerals
                                          : HarvestOrder::Gather);
      else
        throw std::runtime_error("Unsupported landing continuation");
    }
    return;
  }
  if (worker.order == 45) {
    const auto &definition = definitions_->units[worker.type];
    int duration = std::max(1, definition.buildTime);
    int stage = worker.orderState / 2;
    if (stage == 3) {
      if (!(worker.sprite.context.signal & 4))
        return;
      worker.sprite.context.signal &= ~4;
      replaceImages(
          worker,
          definitions_
              ->sprites[definitions_->flingies[definition.graphics].sprite]
              .image);
      completeUnit(worker.handle, false, false);
      auto blocked = occupancy();
      int left = worker.position.x / 32 - definition.placementWidth / 64;
      int top = worker.position.y / 32 - definition.placementHeight / 64;
      for (int y = top; y < top + definition.placementHeight / 32; ++y)
        for (int x = left; x < left + definition.placementWidth / 32; ++x)
          if (x >= 0 && y >= 0 && x < map_->terrain.width() &&
              y < map_->terrain.height())
            blocked[std::size_t(y) * map_->terrain.width() + x] = 0;
      creep_.establish(map_->terrain, worker.position,
                       {definition.placementWidth, definition.placementHeight},
                       bool(definition.flags & 0x80000000U), blocked);
      worker.sprite.animate(15, resources, random_, host);
      worker.orderState = worker.orderTimer = 0;
      return;
    }
    if (stage < 0 || stage > 2)
      throw std::runtime_error("Invalid Zerg construction stage");
    if (worker.orderState % 2) {
      if (worker.type == 149 && stage == 0) {
        const std::vector<std::size_t> layers(worker.sprite.order.begin(),
                                              worker.sprite.order.end());
        for (auto id : layers)
          if (worker.sprite.images[id].type == 345)
            worker.sprite.detach(id);
      }
      worker.sprite.animate(13 + stage, resources, random_, host);
      ++worker.orderState;
    } else {
      std::array<int, 3> thresholds{duration * 3 / 4, duration / 2, 0};
      bool ready = stage == 2 ? worker.remainingBuildTime == 0
                              : worker.remainingBuildTime < thresholds[stage];
      if (ready)
        ++worker.orderState;
    }
    worker.remainingBuildTime = std::max(0, worker.remainingBuildTime - 1);
    auto full = finishedUnitVitals(worker.type, definition).hitpoints;
    worker.vitals.hitpoints = std::int32_t(std::min<std::int64_t>(
        full, std::int64_t(worker.vitals.hitpoints) + worker.constructionGain));
    return;
  }
  if (worker.order == 26) {
    if (worker.buildQueue.size() != 1 || !worker.motion)
      throw std::runtime_error("Invalid Drone building commitment");
    int type = worker.buildQueue.front();
    const auto &definition = definitions_->units[type];
    if (!samePoint(worker.position, worker.targetPosition) ||
        !constructionSiteFits(worker, type, worker.position) ||
        !economy_.spend(worker.owner,
                        {definition.mineralCost, definition.gasCost})) {
      recoverLanding();
      return;
    }
    auto &graphics =
        definitions_
            ->sprites[definitions_->flingies[definition.graphics].sprite];
    UnitState *foundation = &worker;
    if (type == 149) {
      for (int id : visible_)
        if (units_[id]->type == 188 &&
            samePoint(units_[id]->position, worker.targetPosition)) {
          foundation = &*units_[id];
          break;
        }
      if (foundation == &worker)
        throw std::logic_error("Committed gas site disappeared");
      foundation->owner = worker.owner;
      foundation->completed = false;
    }
    auto &building = *foundation;
    const int previousHp = building.vitals.hitpoints;
    morphType(building, type, graphics.image);
    building.constructionPreviousHp = std::max(256, previousHp / 256 * 256);
    if (definition.constructionImage)
      replaceImages(building, definition.constructionImage);
    building.completed = false;
    building.buildQueue.clear();
    building.order = 45;
    building.orderState = building.orderTimer = 0;
    building.remainingBuildTime = definition.buildTime;
    auto full = finishedUnitVitals(type, definition).hitpoints;
    building.vitals.hitpoints = std::max(1, full / 10);
    building.constructionGain =
        int((std::int64_t(full) - building.vitals.hitpoints) /
            std::max(1, definition.buildTime));
    building.creepGrowth = false;
    building.creepTimer = 0;
    building.planRequested = building.walking = false;
    building.collisionPending = building.escapingOverlap = false;
    building.gasLanding = false;
    building.target = building.moveTargetUnit = {};
    building.targetPosition = {};
    building.moveTargetPosition = building.position;
    if (type == 149) {
      // Asset 345 is the construction plume observed on gas conversion;
      // its own Init script creates the shadow and selects the gas frame.
      building.sprite.underlay(345, resources, random_,
          [&](const auto &i, const auto &layer) {
            return effect(building.handle, i, layer);
          });
      worker.consumedByConstruction = true;
      worker.order = worker.orderState = worker.orderTimer = 0;
      worker.queuedCommands.clear();
    }
    return;
  }
  if (worker.order == 31 && worker.orderState == 3) {
    worker.target = {};
    worker.targetPosition = {};
    worker.order = 2;
    worker.orderState = worker.orderTimer = worker.mainTimer = 0;
    worker.sprite.animate(12, definitions_->resources(), random_,
                          [&](const auto &i, const auto &layer) {
                            return effect(worker.handle, i, layer);
                          });
    return;
  }
  if (worker.buildQueue.size() != 1 || !worker.motion)
    throw std::runtime_error("Invalid construction approach state");
  if (worker.order == 25) {
    worker.order = 70;
    worker.orderTimer = 1;
    return;
  }
  if (worker.orderState == 0) {
    auto destination = worker.targetPosition;
    if (worker.order == 70 &&
        definitions_->units[worker.type].movementFlags == 193)
      destination.y -= 7;
    bool changed = !samePoint(worker.moveTargetPosition, destination);
    if (changed)
      worker.motion->keepMomentumForQueuedMove(
          worker.queuedCommands.empty() ? std::nullopt
                                        : std::optional<Point>(destination));
    worker.moveTargetPosition = destination;
    worker.moveTargetUnit = {};
    if (changed || !worker.motion->hasRoute()) {
      worker.planRequested = true;
      worker.routePlanningFrames = 0;
      worker.immovable = false;
    }
    worker.orderState = 1;
    return;
  }
  if (worker.order == 70) {
    if (samePoint(worker.position, worker.moveTargetPosition)) {
      bool invalidSite =
          worker.orderState == 1 &&
          !constructionSiteFits(worker, worker.buildQueue.front(),
                                worker.targetPosition);
      if (worker.immovable || invalidSite) {
        if (worker.orderState == 2)
          recoverLanding();
        else {
          worker.target = {};
          worker.targetPosition = {};
          worker.order = definitions_->units[worker.type].returnIdle;
          worker.orderState = worker.orderTimer = 0;
        }
        return;
      }
      if (worker.orderState == 1) {
        worker.gasLanding = worker.buildQueue.front() == 149;
        for (auto id : worker.sprite.order) {
          auto &layer = worker.sprite.images[id];
          if (layer.modifier == 10 && !worker.gasLanding)
            layer.frozenWorldY = worker.position.y + layer.offset.y;
        }
        discardPath(worker);
        worker.moveTargetPosition = worker.targetPosition;
        worker.moveTargetUnit = {};
        worker.planRequested = true;
        worker.routePlanningFrames = worker.gasLanding ? 0
                                      : worker.motion->moving() ? 1 : 0;
        worker.orderState = 2;
      } else if (worker.orderState == 2) {
        if (worker.landingContinuation) {
          recoverLanding();
          return;
        }
        for (auto id : worker.sprite.order) {
          auto &layer = worker.sprite.images[id];
          if (layer.modifier == 10) {
            layer.offset.y = 0;
            layer.frozenWorldY.reset();
          }
        }
        worker.motion->alignToPixel();
        // Landing publishes a new ground anchor. Check its overlap after the
        // next movement update, even if a new order interrupts commitment.
        worker.collisionPending = true;
        worker.collisionWait = 2;
        worker.landingArrivalPending = worker.motion->moving();
        worker.order = 26;
        worker.orderState = worker.orderTimer = 0;
      }
    }
    return;
  }
  if (worker.order == 31 && worker.orderState == 1) {
    const auto raw = worker.motion->rawPosition();
    const auto endpoint = worker.motion->stopTarget();
    Point delta{worker.targetPosition.x * 256 - raw.x,
                worker.targetPosition.y * 256 - raw.y};
    if (distance(delta) / 256 <= 70) {
      worker.path.reset();
      worker.moveTargetPosition = endpoint;
      worker.motion->brakeTo(endpoint);
      worker.planRequested = false;
      worker.orderState = 2;
    }
  } else if (worker.order == 31 && worker.orderState == 2 &&
             !worker.motion->moving()) {
    int type = worker.buildQueue.front();
    const auto &definition = definitions_->units[type];
    if (constructionSiteFits(worker, type, worker.targetPosition) &&
        economy_.spend(worker.owner,
                       {definition.mineralCost, definition.gasCost})) {
      auto building =
          createConstruction(type, worker.targetPosition, worker.owner);
      worker.buildQueue.clear();
      if (get(building)) {
        worker.target = building;
        worker.orderState = 3;
        return;
      }
    }
    worker.target = {};
    worker.targetPosition = {};
    worker.order = 2;
    worker.orderState = worker.orderTimer = worker.mainTimer = 0;
  } else if (worker.order != 31 && worker.motion->arrived()) {
    throw std::runtime_error(
        "Construction building creation is not implemented");
  }
}
void World::processWarp(UnitState &unit) {
  if (unit.order == 164) {
    if (!unit.psiActive) {
      unit.psiActive = true;
      unit.psiSprite = createEffect(321, unit.position, 0, 4);
      if (unit.psiSprite >= 0)
        effects_[unit.psiSprite]->hidden = true;
    }
    unit.order = definitions_->units[unit.type].returnIdle;
    unit.orderState = unit.orderTimer = 0;
    return;
  }
  auto resources = definitions_->resources();
  auto host = [&](const auto &i, const auto &layer) {
    return effect(unit.handle, i, layer);
  };
  if (unit.orderState == 0) {
    if (unit.remainingBuildTime)
      --unit.remainingBuildTime;
    else {
      unit.sprite.animate(13, resources, random_, host);
      unit.orderState = 1;
    }
    const auto &definition = definitions_->units[unit.type];
    auto full = finishedUnitVitals(unit.type, definition);
    unit.vitals.hitpoints = std::int32_t(std::min<std::int64_t>(
        full.hitpoints,
        std::int64_t(unit.vitals.hitpoints) + unit.constructionGain));
    if (definition.shields) {
      int gain = (full.shields - full.shields / 10) /
                 std::max(1, definition.buildTime);
      unit.vitals.shields = std::int32_t(std::min<std::int64_t>(
          full.shields, std::int64_t(unit.vitals.shields) + gain + 7));
    }
    return;
  }
  if (!(unit.sprite.context.signal & 1))
    return;
  unit.sprite.context.signal &= ~1;
  if (unit.orderState == 1) {
    const auto &definition = definitions_->units[unit.type];
    replaceImages(
        unit, definitions_
                  ->sprites[definitions_->flingies[definition.graphics].sprite]
                  .image);
    unit.sprite.runMainEntry(193, 0, 12, resources, random_, host);
    unit.orderState = 2;
  } else if (unit.orderState == 2) {
    const auto &definition = definitions_->units[unit.type];
    replaceImages(
        unit, definitions_
                  ->sprites[definitions_->flingies[definition.graphics].sprite]
                  .image);
    unit.sprite.animate(21, resources, random_, host);
    unit.orderState = 3;
  } else if (unit.orderState == 3) {
    completeUnit(unit.handle, false, false);
    unit.orderState = unit.orderTimer = unit.mainTimer = 0;
  } else {
    throw std::runtime_error("Invalid building warp phase");
  }
}
bool World::canRally(UnitHandle handle) const {
  const auto *unit = get(handle);
  return unit && !unit->dead && unit->visible &&
         definitions_->units[unit->type].building() &&
         (definitions_->units[unit->type].groupFlags & 32);
}
bool World::rallyPoint(UnitHandle handle, Point point) {
  if (!canRally(handle) || point.x < 0 || point.y < 0 ||
      point.x >= map_->terrain.width() * 32 ||
      point.y >= map_->terrain.height() * 32)
    return false;
  auto &unit = *get(handle);
  unit.rallyPosition = point;
  unit.rallyTarget = {};
  return true;
}
bool World::rallyUnit(UnitHandle handle, UnitHandle target) {
  if (!canRally(handle))
    return false;
  auto &unit = *get(handle);
  const auto *destination = get(target);
  if (!destination || destination->dead)
    destination = &unit;
  unit.rallyPosition = destination->position;
  unit.rallyTarget = destination->handle;
  return true;
}
void World::applyRally(UnitState &unit, const UnitState &producer) {
  const auto *target = get(producer.rallyTarget);
  if (target && !target->dead) {
    if (target->handle == producer.handle)
      return;
    if (target->owner == unit.owner) {
      unit.target = target->handle;
      unit.targetPosition = target->position;
      unit.order = 49;
      unit.orderState = unit.orderTimer = unit.mainTimer = 0;
      return;
    }
    move(unit.handle, target->position);
  } else if (producer.rallyPosition.x || producer.rallyPosition.y) {
    move(unit.handle, producer.rallyPosition);
  }
}
bool World::productionSupply(int owner, int type) const {
  const auto &definition = definitions_->units[type];
  auto counts = population(owner);
  for (int race = 0; race < 3; ++race) {
    if (!(definition.groupFlags & (1 << race)))
      continue;
    auto required = counts.used[race] +
                    definition.supplyRequired * definition.productionCount();
    return required <= 400 && required <= counts.available[race];
  }
  return true;
}
bool World::train(UnitHandle handle, int type) {
  auto *producer = get(handle);
  if (!producer || producer->dead || !producer->completed || !producer->visible)
    return false;
  if (type < 0 || type >= 228)
    throw std::out_of_range("Invalid train type");
  if (!definitions_->units[producer->type].building())
    return false;
  if (!((producer->type == 154 && type == 64) ||
        (producer->type == 106 && type == 7)))
    throw std::runtime_error("Unimplemented native production pair");
  if (producer->buildQueue.size() >= 5)
    return false;
  const auto &definition = definitions_->units[type];
  if (producer->buildQueue.empty() && !productionSupply(producer->owner, type))
    return false;
  if (!economy_.spend(producer->owner,
                      {definition.mineralCost, definition.gasCost}))
    return false;
  producer->buildQueue.push_back(type);
  if (producer->secondaryOrder != 38) {
    producer->secondaryOrder = 38;
    producer->secondaryState = 0;
  }
  return true;
}
bool World::cancelConstruction(UnitHandle handle) {
  auto *building = get(handle);
  if (!building || building->dead || !building->visible || building->completed)
    return false;
  if (building->type != 149 || building->order != 45 ||
      building->constructionPreviousHp <= 0)
    throw std::runtime_error("Native cancellation of this construction is not implemented");
  const int owner = building->owner;
  const auto &cost = definitions_->units[building->type];
  auto replacement = createFinished(41, building->position, owner, false);
  if (auto *worker = get(replacement)) {
    worker->vitals.hitpoints =
        std::min(worker->vitals.hitpoints, building->constructionPreviousHp);
    if (worker->motion)
      worker->motion->inheritInertia({});
    worker->collisionPending = worker->overlapStopRecovery = true;
    worker->collisionWait = 2;
  }
  building->owner = 11;
  const auto &gas = definitions_->units[188];
  const auto image = definitions_->sprites[definitions_->flingies[gas.graphics].sprite].image;
  morphType(*building, 188, image);
  building->completed = true;
  building->order = gas.humanIdle;
  building->orderState = building->orderTimer = building->mainTimer = 0;
  building->remainingBuildTime = 1;
  building->constructionGain = building->constructionPreviousHp = 0;
  building->buildQueue.clear();
  building->buildUnit = building->target = building->moveTargetUnit = {};
  building->targetPosition = {};
  building->moveTargetPosition = building->position;
  building->planRequested = building->walking = false;
  building->collisionPending = building->escapingOverlap = false;
  building->creepGrowth = false;
  economy_.credit(owner, {cost.mineralCost * 3 / 4, cost.gasCost * 3 / 4});
  return true;
}
bool World::cancelQueue(UnitHandle handle, int slot) {
  auto *producer = get(handle);
  if (!producer || producer->dead || !producer->completed ||
      !producer->visible || producer->buildQueue.empty())
    return false;
  // Morphing structures and mobile morph actors do not expose this action.
  switch (producer->type) {
  case 35: case 38: case 43: case 131: case 132: case 141: case 143:
    return false;
  }
  if (slot == 254)
    slot = int(producer->buildQueue.size()) - 1;
  if (slot < 0 || std::size_t(slot) >= producer->buildQueue.size())
    return false;
  int type = producer->buildQueue[slot];
  if (!((producer->type == 106 && type == 7) ||
        (producer->type == 154 && type == 64)))
    throw std::runtime_error("Unimplemented native queue cancellation product");
  const auto &definition = definitions_->units[type];
  bool refund = true;
  if (slot == 0 && get(producer->buildUnit)) {
    auto *child = get(producer->buildUnit);
    refund = child->order != 0;
    child->order = 0;
    child->orderState = child->orderTimer = 0;
  }
  producer->buildQueue.erase(producer->buildQueue.begin() + slot);
  if (refund)
    economy_.credit(producer->owner,
                    {definition.mineralCost, definition.gasCost});
  return true;
}
void World::processProduction(UnitState &producer) {
  if (producer.secondaryOrder != 38)
    return;
  if (auto *child = get(producer.buildUnit); child && !child->dead) {
    if (child->order == 0)
      return;
    if (child->remainingBuildTime == 0) {
      auto completed = completeUnit(child->handle, true, false);
      auto *unit = get(completed);
      if (!unit || unit->dead)
        throw std::runtime_error(
            "Blocked native production exit is not implemented");
      producer.buildQueue.erase(producer.buildQueue.begin());
      producer.buildUnit = {};
      producer.secondaryState = 0;
      // Placement admits moving bodies at the exit. Ground products still
      // need their initial legality check before following the rally route.
      if (!(definitions_->units[unit->type].flags & 4)) {
        unit->collisionPending = unit->overlapStopRecovery = true;
        unit->collisionWait = 2;
      }
      applyRally(*unit, producer);
      return;
    }
    --child->remainingBuildTime;
    auto full =
        finishedUnitVitals(child->type, definitions_->units[child->type])
            .hitpoints;
    child->vitals.hitpoints = std::int32_t(std::min<std::int64_t>(
        full, std::int64_t(child->vitals.hitpoints) + child->constructionGain));
    return;
  }
  if (producer.secondaryState == 2) {
    producer.buildUnit = {};
    producer.secondaryState = 0;
    return;
  }
  if (producer.buildQueue.empty()) {
    producer.sprite.animate(20, definitions_->resources(), random_,
                            [&](const auto &i, const auto &layer) {
                              return effect(producer.handle, i, layer);
                            });
    producer.secondaryOrder = 23;
    producer.secondaryState = 0;
    return;
  }
  producer.sprite.animate(19, definitions_->resources(), random_,
                          [&](const auto &i, const auto &layer) {
                            return effect(producer.handle, i, layer);
                          });
  producer.secondaryState = 1;
  if (!productionSupply(producer.owner, producer.buildQueue.front()))
    return;
  producer.buildUnit =
      createUnit(producer.buildQueue.front(), producer.position, producer.owner,
                 false, false, false);
  if (producer.buildUnit.index >= 0)
    producer.secondaryState = 2;
}
bool World::morph(UnitHandle handle, int type) {
  auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->completed)
    return false;
  if (unit->type == 38 || unit->type == 43)
    throw std::runtime_error("Unimplemented native advanced unit morph");
  if (unit->type != 35)
    return false;
  if (type != 37 && type != 41 && type != 42)
    throw std::runtime_error("Unimplemented native larva morph product");
  if (type == 37 && !population(unit->owner).completed[142])
    return false;
  if (!unit->buildQueue.empty() || !productionSupply(unit->owner, type))
    return false;
  const auto &definition = definitions_->units[type];
  if (!economy_.spend(unit->owner,
                      {definition.mineralCost, definition.gasCost}))
    return false;
  unit->buildQueue.push_back(type);
  unit->order = 42;
  unit->orderState = unit->orderTimer = unit->mainTimer = 0;
  return true;
}
void World::morphType(UnitState &unit, int type, int image) {
  spatial_.remove(unit.handle.index);
  air_.erase(unit.air);
  unit.type = type;
  unit.vitals = finishedUnitVitals(type, definitions_->units[type]);
  if (unit.motion) {
    // Immediate animation entries may have issued a speed before the type
    // changes.
    if (unit.motion->scriptDriven())
      unit.motion->scriptSpeed(unit.sprite.context.speed);
    unit.inactiveInertia = unit.motion->inertia();
  }
  unit.motion.reset();
  discardPath(unit);
  unit.sprite.context = {};
  unit.sprite.context.tileset = map_->tileset;
  unit.sprite.context.resourceContainer =
      bool(definitions_->units[type].flags & 0x2000U);
  unit.sprite.context.resources = unit.resources;
  unit.sprite.context.topSpeed =
      int(definitions_->flingies[definitions_->units[type].graphics].speed);
  replaceImages(unit, image);
  unit.sprite.setHeading(0);
  unit.sprite.tick(definitions_->resources(), random_,
                   [&](const auto &i, const auto &layer) {
                     return effect(unit.handle, i, layer);
                   });
  moveSpatial(unit, true);
  unit.secondaryTimer = 0;
  if ((definitions_->units[type].flags & 4) &&
      (definitions_->units[type].flags & 0x8000000U)) {
    air_.insert(unit.air, unit.position, random_);
    unit.air.heading = signedHeading(int(random_.next()));
    unit.sprite.context.flying = true;
  }
  notifyOverlap(unit);
}
bool World::clearOverlap(const UnitState &unit, Point point) const {
  const auto &shape = definitions_->units[unit.type].bounds;
  if (!Placement(map_->terrain, map_->regions, {}).terrainFits(point, shape))
    return false;
  for (const auto &body : bodies(unit.handle.index)) {
    if (body.flying || units_[body.key]->type == 35 ||
        (unit.gathering &&
         !definitions_->units[units_[body.key]->type].building()))
      continue;
    if (point.x + shape.right >= body.position.x - body.bounds.left &&
        point.x - shape.left <= body.position.x + body.bounds.right &&
        point.y + shape.bottom >= body.position.y - body.bounds.top &&
        point.y - shape.top <= body.position.y + body.bounds.bottom)
      return false;
  }
  return true;
}
void World::notifyOverlap(UnitState &unit) {
  const auto &shape = definitions_->units[unit.type].bounds;
  for (int id : visible_) {
    auto &other = *units_[id];
    const auto &type = definitions_->units[other.type];
    if (other.handle == unit.handle || !other.motion || other.gathering ||
        (type.flags & 4) || other.gasLanding || type.building() || other.collisionPending ||
        other.escapingOverlap || other.movementPreparation > 0)
      continue;
    if (other.position.x + type.bounds.right >= unit.position.x - shape.left &&
        other.position.x - type.bounds.left <= unit.position.x + shape.right &&
        other.position.y + type.bounds.bottom >= unit.position.y - shape.top &&
        other.position.y - type.bounds.top <= unit.position.y + shape.bottom) {
      other.collisionPending = true;
      other.collisionWait =
          (other.motion->stopped() || other.motion->settling()) ? 2 : 1;
      if (definitions_->units[unit.type].building() &&
          (other.motion->stopped() || other.motion->settling()))
        other.overlapStopRecovery = true;
    }
  }
}
void World::processMorph(UnitState &unit) {
  auto resources = definitions_->resources();
  auto animate = [&](int animation) {
    unit.sprite.animate(animation, resources, random_,
                        [&](const auto &i, const auto &layer) {
                          return effect(unit.handle, i, layer);
                        });
  };
  if (unit.order == 42) {
    if (unit.buildQueue.size() != 1 || unit.orderState < 0 ||
        unit.orderState > 2)
      throw std::runtime_error("Invalid native larva morph state");
    if (unit.orderState == 0) {
      discardPath(unit);
      unit.pendingRoute.clear();
      unit.planRequested = unit.walking = unit.collisionPending = false;
      unit.moveTargetPosition = unit.position;
      unit.moveTargetUnit = {};
      auto type = unit.buildQueue.front();
      const auto &physics =
          definitions_->flingies[definitions_->units[36].graphics];
      morphType(unit, 36, definitions_->sprites[physics.sprite].image);
      unit.completed = false;
      unit.remainingBuildTime = definitions_->units[type].buildTime;
      unit.orderState = 1;
    } else if (unit.orderState == 1) {
      if (unit.remainingBuildTime)
        --unit.remainingBuildTime;
      else {
        animate(13);
        unit.orderState = 2;
      }
    } else if (unit.sprite.context.signal & 4) {
      auto type = unit.buildQueue.front();
      morphType(unit, type, definitions_->units[type].constructionImage);
      unit.remainingBuildTime = definitions_->units[type].buildTime;
      unit.buildQueue.clear();
      unit.order = 41;
      unit.orderState = unit.orderTimer = 0;
      animate(13);
    }
  } else if (unit.sprite.context.signal & 4) {
    const auto &definition = definitions_->units[unit.type];
    const auto &physics = definitions_->flingies[definition.graphics];
    UnitHandle sibling;
    if (definition.productionCount() == 2) {
      const auto &image = unit.sprite.images[unit.sprite.main];
      auto birthPoint = [&](int index) {
        auto delta = definitions_->offset(image.type, image.frame(), 3,
                                           index, image.flip);
        return Point{
            std::clamp(unit.position.x + delta.x, definition.bounds.left,
                       map_->terrain.width() * 32 - definition.bounds.right - 1),
            std::clamp(unit.position.y + delta.y, definition.bounds.top,
                       map_->terrain.height() * 32 - definition.bounds.bottom - 1)};
      };
      auto second = birthPoint(1);
      unit.position = birthPoint(0);
      moveSpatial(unit);
      spriteRows_.move(unit.sprite.sceneKey, unit.position);
      sibling = createUnit(unit.type, second, unit.owner, false, false, false);
      if (auto *child = get(sibling)) {
        child->sprite.setHeading(144);
        child->vitals = finishedUnitVitals(child->type, definition);
        child->remainingBuildTime = 0;
        completeUnit(sibling, false, false);
        child->collisionPending = !(definition.flags & 4);
        child->overlapStopRecovery = child->collisionPending;
        child->collisionWait = 2;
        notifyOverlap(*child);
      }
    }
    unit.sprite.context.signal = 0;
    replaceImages(unit, definitions_->sprites[physics.sprite].image);
    // Birth activation uses the unit's vertical movement profile. These
    // offsets match observed hovering-worker and flying-unit transitions.
    int elevation = definition.productionCount() == 2 ? 0 :
        definition.movementFlags == 193 ? 7 : ((definition.flags & 4) ? 42 : 0);
    unit.position.y =
        std::max(definition.bounds.top, unit.position.y - elevation);
    spatial_.remove(unit.handle.index);
    completeUnit(unit.handle, false, false);
    unit.remainingBuildTime = 0;
    unit.orderState = unit.orderTimer = unit.mainTimer = 0;
    unit.collisionPending = !(definition.flags & 4);
    unit.overlapStopRecovery = unit.collisionPending;
    unit.collisionWait = 1;
    notifyOverlap(unit);
    if (const auto *producer = get(unit.connected); producer && !producer->dead) {
      applyRally(unit, *producer);
      if (auto *child = get(sibling))
        applyRally(*child, *producer);
    }
  }
}
} // namespace mapeditor::simulation
