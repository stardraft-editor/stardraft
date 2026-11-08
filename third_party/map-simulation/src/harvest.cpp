// SPDX-License-Identifier: MIT
#include <algorithm>
#include <limits>
#include <mapeditor/simulation/world.h>
namespace mapeditor::simulation {
bool World::interruptHarvest(UnitState &worker, UnitCommand command) {
  if (!worker.gathering)
    return false;
  worker.harvestContinuation = command;
  worker.pendingHarvest = {};
  worker.checkHarvestOverlap = true;
  if (worker.escapingOverlap)
    worker.overlapDirectRoute = true;
  if (worker.order == 88 || worker.order == 89 || worker.order == 151)
    return true;
  bool working = worker.order == 87;
  bool queued = worker.order == 86 && worker.orderState == 2;
  worker.order = working ? 88 : queued ? 89 : 151;
  worker.orderState = worker.orderTimer = 0;
  worker.target = {};
  worker.targetPosition = {};
  if ((working || queued) &&
      worker.sprite.images[worker.sprite.main].cursor.animation == 15)
    worker.sprite.animate(8, definitions_->resources(), random_,
                          [&](const auto &i, const auto &layer) {
                            return effect(worker.handle, i, layer);
                          });
  return true;
}
void World::harvest(UnitHandle handle, UnitHandle patch, HarvestOrder order,
                    bool queueCommand) {
  auto *worker = get(handle);
  auto *mineral = get(patch);
  if (!worker || worker->dead || !worker->visible || !worker->motion ||
      !(definitions_->units[worker->type].flags & 8) || !mineral ||
      mineral->dead ||
      (order == HarvestOrder::Gas
           ? mineral->type != 188
           : mineral->type < 176 || mineral->type > 178 ||
                 mineral->resources <= 0))
    throw std::invalid_argument("Invalid harvesting command");
  if (queueCommand) {
    worker->queuedCommands.push_back(
        {harvestOrderCode(order), mineral->position, patch});
    activateQueuedCommand(*worker);
    return;
  }
  worker->queuedCommands.clear();
  worker->queuedUserOverride = false;
  if (worker->order == 70 && worker->orderState == 2) {
    worker->landingContinuation = UnitCommand{
        harvestOrderCode(order), mineral->position, patch};
    return;
  }
  prepareOrderTransition(*worker);
  worker->harvestContinuation.reset();
  worker->pendingHarvestOrder = order;
  if (worker->escapingOverlap) {
    if (worker->collisionPending) {
      // An accepted escape endpoint has already spent its stationary frame.
      // Its pending clear-space check can prepare the old route immediately,
      // before the harvest order replaces that destination.
      if (!worker->motion->arrived())
        worker->collisionWait = std::max(2, worker->collisionWait);
      worker->overlapResumeDirect = true;
    } else
      worker->overlapDirectRoute = true;
  }
  if (worker->order == 88 || worker->order == 89 ||
      (worker->order == 151 && worker->pendingHarvest.index >= 0)) {
    worker->pendingHarvest = patch;
    return;
  }
  bool working = worker->order == 87;
  bool queued = worker->order == 86 && worker->orderState == 2;
  if (working || queued) {
    worker->pendingHarvest = patch;
    worker->target = {};
    worker->targetPosition = {};
    worker->order = working ? 88 : 89;
    worker->orderState = worker->orderTimer = 0;
    if (worker->sprite.images[worker->sprite.main].cursor.animation == 15)
      worker->sprite.animate(8, definitions_->resources(), random_,
                             [&](const auto &i, const auto &layer) {
                               return effect(worker->handle, i, layer);
                             });
    return;
  }
  if (worker->gathering) {
    worker->pendingHarvest = patch;
    worker->target = {};
    worker->targetPosition = {};
    worker->order = 151;
    worker->orderState = worker->orderTimer = 0;
    return;
  }
  worker->mineral = patch;
  worker->target = patch;
  worker->targetPosition = mineral->position;
  worker->order = harvestOrderCode(order);
  worker->orderState = worker->orderTimer = 0;
}
bool World::returnCargo(UnitHandle handle, bool queued, UnitHandle depot) {
  auto *worker = get(handle);
  if (!worker || worker->dead || !worker->visible || !worker->completed ||
      !worker->motion || !(definitions_->units[worker->type].flags & 8))
    throw std::invalid_argument("Invalid cargo return command");
  if (!(worker->cargo & 3))
    return false;
  const int order = worker->cargo & 2 ? 90 : 84;
  const auto *destination = get(depot);
  if (destination && (destination->dead || !destination->visible ||
                      !destination->completed || destination->owner != worker->owner ||
                      !(definitions_->units[destination->type].flags & 0x1000U)))
    destination = nullptr;
  if (!destination)
    depot = {};
  const UnitCommand command{order, destination ? destination->position : Point{}, depot};
  if (queued) {
    worker->queuedCommands.push_back(command);
    activateQueuedCommand(*worker);
    return true;
  }
  if (deferLockedCommand(*worker, command))
    return true;
  worker->queuedCommands.clear();
  worker->queuedUserOverride = false;
  if (worker->gathering)
    worker->queuedCommands.push_back(command);
  prepareOrderTransition(*worker);
  worker->harvestContinuation.reset();
  worker->checkHarvestOverlap = worker->gathering;
  worker->pendingHarvest = {};
  worker->target = worker->gathering ? UnitHandle{} : depot;
  worker->targetPosition = worker->gathering ? Point{} : command.position;
  worker->order = worker->gathering ? 151 : order;
  worker->orderState = worker->orderTimer = 0;
  return true;
}
void World::releaseMineral(UnitState &worker) {
  auto *patch = get(worker.mineral);
  if (!patch || patch->dead)
    return;
  if (patch->gatherer != worker.handle) {
    std::erase(patch->mineralQueue, worker.handle);
    return;
  }
  patch->gatherer = {};
  while (!patch->mineralQueue.empty()) {
    auto handle = patch->mineralQueue.front();
    patch->mineralQueue.pop_front();
    auto *next = get(handle);
    if (!next || next->dead || next->order != 86 || next->orderState != 2)
      continue;
    patch->gatherer = handle;
    next->order = 87;
    next->orderState = next->orderTimer = 0;
    break;
  }
}
void World::moveForHarvest(UnitState &worker, UnitHandle target,
                           int planningFrames) {
  auto *destination = get(target);
  if (!destination || destination->dead)
    throw std::runtime_error("Harvest movement target disappeared");
  worker.target = target;
  // Legacy command acceptance uses an asymmetric footprint: its south limit
  // stays at the target's edge, while both near-side margins extend east.
  // This differs from the symmetric collision perimeter used by routing.
  const auto &a = definitions_->units[destination->type].bounds;
  const auto &b = definitions_->units[worker.type].bounds;
  PixelBox accepted{-a.left - b.right - 1, -a.top - b.bottom - 1,
                    a.right + b.left + b.top + 2, a.bottom};
  Point local{worker.moveTargetPosition.x - destination->position.x,
              worker.moveTargetPosition.y - destination->position.y};
  bool onEdge = accepted.contains(local) &&
                (local.x == accepted.left || local.x == accepted.right ||
                 local.y == accepted.top || local.y == accepted.bottom);
  if ((worker.order == 85 || worker.order == 81) && worker.moveTargetUnit == target &&
      (samePoint(local, {}) || onEdge) && !worker.planRequested &&
      worker.failedRouteFrames == 0) {
    if (samePoint(worker.position, worker.moveTargetPosition))
      worker.motion->hold(destination->position);
    return;
  }
  if (worker.moveTargetUnit != target || (!samePoint(local, {}) && !onEdge))
    worker.moveTargetPosition = destination->position;
  worker.moveTargetUnit = target;
  if (worker.escapingOverlap) {
    worker.overlapReturnTarget = worker.moveTargetPosition;
    worker.overlapOrderChanged = true;
    worker.overlapFreshOrder = false;
    worker.motion->refreshRouteHeading();
    worker.planRequested = false;
    return;
  }
  worker.planRequested = true;
  worker.immovable = false;
  worker.routePlanningFrames = std::max(
      planningFrames, int(worker.motion->arrived() && worker.motion->moving()));
}
UnitHandle World::freeMineral(const UnitState &worker, bool requireFree,
                              int range) const {
  // Nearby free patches can replace a busy approach target before a
  // worker joins its queue. Search value state, never cached traces.
  UnitHandle selected;
  int closest = std::numeric_limits<int>::max();
  // Express the spatial encounter order as a rank over footprint
  // boundary events. Distance remains primary; equal distances keep
  // the earliest event among the four interleaved axis directions.
  auto encounter = spatial_.encounters(worker.position);
  int firstEncounter = std::numeric_limits<int>::max();
  auto occupied = occupancy();
  auto here = map_->regions.regionAt(worker.position);
  for (int id : visible_) {
    const auto &candidate = *units_[id];
    if (candidate.type < 176 || candidate.type > 178 ||
        candidate.resources <= 0 || (requireFree && get(candidate.gatherer)))
      continue;
    auto p = candidate.position;
    auto delta = Point{p.x - worker.position.x, p.y - worker.position.y};
    if (delta.x < -range || delta.x >= range || delta.y < -range ||
        delta.y >= range)
      continue;
    auto there = map_->regions.regionAt(p);
    if (map_->regions.entries()[here].group !=
            map_->regions.entries()[there].group ||
        map_->terrain.tile(p.x / 32, p.y / 32).category !=
            map_->terrain.tile(worker.position.x / 32, worker.position.y / 32)
                .category)
      continue;
    auto bounds = definitions_->units[candidate.type].bounds;
    int left = (p.x - bounds.left) / 32, right = (p.x + bounds.right) / 32,
        top = (p.y - bounds.top) / 32, bottom = (p.y + bounds.bottom) / 32;
    bool available = false;
    for (int ty = std::max(0, top - 1);
         ty <= std::min(map_->terrain.height() - 1, bottom + 1); ++ty)
      for (int tx = std::max(0, left - 1);
           tx <= std::min(map_->terrain.width() - 1, right + 1); ++tx) {
        bool horizontal =
            tx >= left && tx <= right && (ty == top - 1 || ty == bottom + 1);
        bool vertical =
            ty >= top && ty <= bottom && (tx == left - 1 || tx == right + 1);
        if ((horizontal || vertical) &&
            !occupied[std::size_t(ty) * map_->terrain.width() + tx])
          available = true;
      }
    int away = distance(delta);
    if (available && (away < closest ||
                      (away == closest && encounter.at(id) < firstEncounter))) {
      selected = candidate.handle;
      closest = away;
      firstEncounter = encounter.at(id);
    }
  }
  return selected;
}
void World::processHarvest(UnitState &worker) {
  // Braking and a pending legality check still own an arrival update.
  // A clear, completed point stop may retain fractional coordinates and speed.
  const bool finishingExternalArrival =
      !worker.gathering && worker.motion &&
      (worker.motion->braking() || worker.collisionPending) &&
      worker.motion->settling() && worker.motion->speed() > 0 &&
      !samePoint(worker.motion->rawPosition(),
                 {worker.position.x * 256, worker.position.y * 256});
  auto change = [&](int order) {
    worker.order = order;
    worker.orderState = worker.orderTimer = 0;
  };
  auto beginGathering = [&] {
    worker.gathering = true;
    if (std::none_of(worker.queuedCommands.begin(), worker.queuedCommands.end(),
                     [](const auto &command) { return command.order == 151; }))
      worker.queuedCommands.push_front({151, {}, {}});
  };
  auto startReturn = [&](int order) {
    auto depot = worker.target;
    const auto *destination = get(depot);
    if (!destination || destination->dead || !destination->visible ||
        !destination->completed || destination->owner != worker.owner ||
        !(definitions_->units[destination->type].flags & 0x1000U))
      depot = mineralDepot(worker.handle);
    if (!get(depot))
      throw std::runtime_error("No resource return depot");
    const bool startingGathering = !worker.gathering;
    beginGathering();
    moveForHarvest(worker, depot, startingGathering ? 0 : 2);
    if (startingGathering && worker.collisionPending) {
      worker.overlapReturnTarget = worker.moveTargetPosition;
      worker.overlapOrderChanged = worker.overlapFreshOrder = true;
    }
    if (worker.escapingOverlap && worker.collisionPending)
      worker.collisionWait = std::max(2, worker.collisionWait);
    worker.order = order;
    worker.orderState = 1;
  };
  auto animate = [&](int animation) {
    worker.sprite.context.target = bool(get(worker.target));
    worker.sprite.context.targetHeading =
        heading({worker.targetPosition.x - worker.position.x,
                 worker.targetPosition.y - worker.position.y});
    worker.sprite.context.targetDistance =
        distance({worker.targetPosition.x - worker.position.x,
                  worker.targetPosition.y - worker.position.y});
    worker.sprite.animate(animation, definitions_->resources(), random_,
                          [&](const auto &i, const auto &layer) {
                            return effect(worker.handle, i, layer);
                          });
  };
  switch (worker.order) {
  case 79:
    beginGathering();
    change(85);
    break;
  case 81: {
    auto *resource = get(worker.mineral);
    if (!resource || resource->dead || resource->type != 188)
      throw std::runtime_error("Gas approach target loss is not implemented yet");
    beginGathering();
    if (worker.orderState == 0) {
      worker.resourceTarget = worker.mineral;
      worker.targetPosition = resource->position;
      moveForHarvest(worker, worker.mineral, finishingExternalArrival ? 1 : 0);
      worker.orderState = 3;
    } else if (samePoint(worker.position, worker.moveTargetPosition)) {
      worker.motion->settle();
      interruptHarvest(worker,
                       {definitions_->units[worker.type].returnIdle, {}, {}});
    }
    break;
  }
  case 85:
    beginGathering();
    if (worker.orderState == 0) {
      auto *patch = get(worker.mineral);
      if (!patch || patch->dead || patch->resources <= 0)
        throw std::runtime_error(
            "Mineral target reselection is not implemented yet");
      worker.targetPosition = patch->position;
      worker.resourceTarget = worker.mineral;
      moveForHarvest(worker, worker.mineral, finishingExternalArrival ? 1 : 0);
      worker.orderState = 1;
    } else {
      const auto *mineral = get(worker.mineral);
      if (!mineral || mineral->dead)
        throw std::runtime_error("Mineral movement target disappeared");
      const auto &patch = *mineral;
      auto a = definitions_->units[worker.type].bounds;
      auto b = definitions_->units[patch.type].bounds;
      int dx = std::max(
          {0, patch.position.x - b.left - worker.position.x - a.right - 1,
           worker.position.x - a.left - patch.position.x - b.right - 1});
      int dy = std::max(
          {0, patch.position.y - b.top - worker.position.y - a.bottom - 1,
           worker.position.y - a.top - patch.position.y - b.bottom - 1});
      bool arrived = samePoint(worker.position, worker.moveTargetPosition);
      if (worker.orderState == 1 && (arrived || distance({dx, dy}) <= 10)) {
        if (get(patch.gatherer)) {
          auto selected = freeMineral(worker);
          if (auto *next = get(selected)) {
            worker.mineral = selected;
            worker.resourceTarget = selected;
            worker.targetPosition = next->position;
            moveForHarvest(
                worker, selected,
                worker.motion->arrived() && worker.motion->moving() ? 1 : 0);
            break;
          }
        }
        worker.orderState = 3;
      }
      if (worker.orderState == 3 && arrived) {
        if (worker.immovable) {
          auto replacement = freeMineral(worker, false, 384);
          if (auto *next = get(replacement)) {
            worker.mineral = worker.resourceTarget = replacement;
            worker.targetPosition = next->position;
            moveForHarvest(worker, replacement);
          } else {
            worker.motion->hold(worker.moveTargetPosition);
            interruptHarvest(
                worker, {definitions_->units[worker.type].returnIdle, {}, {}});
            processHarvest(worker);
          }
          break;
        }
        if (worker.cargo & 2) {
          worker.target = {};
          worker.targetPosition = {};
          change(151);
        } else {
          worker.motion->hold(patch.position);
          change(86);
        }
      }
    }
    break;
  case 86: {
    auto *patch = get(worker.mineral);
    if (!patch || patch->dead)
      throw std::runtime_error("Mineral reservation target disappeared");
    if (worker.orderState == 2)
      break;
    if (get(patch->gatherer) && patch->gatherer != worker.handle) {
      patch->mineralQueue.push_back(worker.handle);
      worker.orderState = 2;
      worker.orderTimer = 0;
      break;
    }
    patch->gatherer = worker.handle;
    change(87);
    break;
  }
  case 87: {
    auto *patch = get(worker.mineral);
    if (!patch || patch->dead || patch->gatherer != worker.handle)
      throw std::runtime_error("Invalid mineral reservation");
    if (worker.orderState == 0) {
      int facing = heading({patch->position.x - worker.position.x,
                            patch->position.y - worker.position.y});
      if (worker.motion->facing() != facing)
        break;
      auto vector = directionVector(worker.motion->facing());
      worker.targetPosition = {worker.position.x + fixedProduct(vector.x, 20),
                               worker.position.y + fixedProduct(vector.y, 20)};
      animate(15);
      worker.motion->settle();
      worker.mainTimer = 75;
      worker.orderState = 5;
    } else if (worker.orderState == 5 && worker.mainTimer == 0) {
      int amount = std::min(8, patch->resources);
      if (amount <= 0)
        throw std::runtime_error("Mineral exhaustion is not implemented yet");
      setResources(worker.mineral, patch->resources - amount);
      worker.cargo = 2;
      worker.cargoAmount = amount;
      releaseMineral(worker);
      animate(8);
      worker.cargoImage = worker.sprite.attach(
          amount == 8 ? 397 : 398, definitions_->resources(), random_,
          [&](const auto &i, const auto &layer) {
            return effect(worker.handle, i, layer);
          });
      worker.target = {};
      worker.targetPosition = {};
      change(151);
    }
    break;
  }
  case 88:
  case 89:
    releaseMineral(worker);
    worker.order = 151;
    worker.orderState = 0;
    break;
  case 151: {
    if (worker.checkHarvestOverlap) {
      worker.checkHarvestOverlap = false;
      const auto &shape = definitions_->units[worker.type].bounds;
      PixelBox footprint{
          worker.position.x - shape.left, worker.position.y - shape.top,
          worker.position.x + shape.right, worker.position.y + shape.bottom};
      for (int id : visible_) {
        auto &other = *units_[id];
        if (other.handle == worker.handle)
          continue;
        const auto &type = definitions_->units[other.type];
        bool overlaps =
            other.position.x + type.bounds.right >= footprint.left &&
            other.position.x - type.bounds.left <= footprint.right &&
            other.position.y + type.bounds.bottom >= footprint.top &&
            other.position.y - type.bounds.top <= footprint.bottom;
        if (overlaps && !(type.flags & 4)) {
          if (type.building())
            worker.collisionPending = true;
          else if (!other.gathering && !other.collisionPending) {
            other.collisionPending = true;
            other.collisionWait =
                other.motion && other.motion->stopped() ? 2 : 1;
          }
        }
      }
    }
    if (worker.harvestContinuation) {
      std::erase_if(worker.queuedCommands,
                    [](const auto &command) { return command.order == 151; });
      auto command = *worker.harvestContinuation;
      worker.harvestContinuation.reset();
      worker.gathering = false;
      worker.pendingHarvest = {};
      discardPath(worker);
      worker.collisionRouteReset = worker.collisionPending = true;
      worker.collisionWait = 2;
      if (command.order == definitions_->units[worker.type].returnIdle) {
        worker.order = command.order;
        worker.orderState = worker.orderTimer = worker.mainTimer = 0;
      } else if (command.order == 6) {
        move(worker.handle, command.position);
        worker.overlapOrderChanged = worker.overlapFreshOrder =
            !samePoint(command.position, worker.position);
        worker.overlapReturnTarget = command.position;
      } else if (command.order == 14) {
        attackMove(worker.handle, command.position);
        worker.overlapOrderChanged = worker.overlapFreshOrder = true;
        worker.overlapReturnTarget = command.position;
      } else if (command.order == 10) {
        attack(worker.handle, command.target);
        worker.overlapOrderChanged = worker.overlapFreshOrder = true;
        worker.overlapReturnTarget = worker.targetPosition;
      } else if (command.order == 49) {
        auto remaining = worker.mainTimer;
        follow(worker.handle, command.target);
        worker.mainTimer = remaining;
        worker.overlapOrderChanged = true;
        worker.overlapFreshOrder = true;
        worker.overlapReturnTarget = worker.targetPosition;
      } else if (command.order == 25 || command.order == 30 ||
                 command.order == 31) {
        beginConstruction(worker, command.position, command.order);
        worker.overlapOrderChanged = worker.overlapFreshOrder = true;
        worker.overlapReturnTarget = command.position;
      } else
        throw std::logic_error("Unsupported harvesting continuation");
      break;
    }
    if (auto *next = get(worker.pendingHarvest)) {
      worker.mineral = worker.target = worker.pendingHarvest;
      worker.targetPosition = next->position;
      worker.pendingHarvest = {};
      change(harvestOrderCode(worker.pendingHarvestOrder));
      break;
    }
    int returnOrder = worker.cargo & 2 ? 90 : 84;
    if (!worker.queuedCommands.empty() &&
        (worker.queuedCommands.front().order == 90 ||
         worker.queuedCommands.front().order == 84)) {
      returnOrder = worker.queuedCommands.front().order;
      worker.target = worker.queuedCommands.front().target;
      worker.targetPosition = worker.queuedCommands.front().position;
      worker.queuedCommands.pop_front();
    }
    startReturn(returnOrder);
    worker.orderTimer = 0;
    break;
  }
  case 84:
  case 90:
    if (worker.orderState == 0) {
      startReturn(worker.order);
      break;
    }
    if (samePoint(worker.position, worker.moveTargetPosition)) {
      if (auto *depot = get(worker.target);
          depot &&
          (depot->type == 131 || depot->type == 132 || depot->type == 133)) {
        auto &weights = depot->larvaSpawnWeights;
        for (auto &weight : weights)
          weight = std::max(0, weight - 1);
        const auto &bounds = definitions_->units[depot->type].bounds;
        int side = worker.position.y < depot->position.y - bounds.top      ? 1
                   : worker.position.y > depot->position.y + bounds.bottom ? 3
                   : worker.position.x < depot->position.x - bounds.left   ? 0
                                                                           : 2;
        if (weights[side] < 100)
          weights[side] += 25;
      }
      const bool gas = !(worker.cargo & 2) && (worker.cargo & 1);
      if (gas)
        economy_.credit(worker.owner, {0, worker.cargoAmount});
      else {
        deliveredMinerals_.at(worker.owner) += worker.cargoAmount;
        economy_.credit(worker.owner, {worker.cargoAmount, 0});
      }
      worker.cargo = worker.cargoAmount = 0;
      if (worker.cargoImage) {
        worker.sprite.detach(*worker.cargoImage);
        worker.cargoImage.reset();
      }
      worker.target = worker.mineral;
      auto *patch = get(worker.mineral);
      if (!patch || patch->dead)
        throw std::runtime_error("Mineral return target disappeared");
      worker.targetPosition = patch->position;
      change(gas ? 81 : 85);
    }
    break;
  }
}
} // namespace mapeditor::simulation
