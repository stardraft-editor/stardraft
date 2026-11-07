// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/navigation.h>
#include <mapeditor/simulation/psi.h>
#include <mapeditor/simulation/world.h>
#include <stdexcept>
#include <tuple>
namespace mapeditor::simulation {
namespace {
template <class Container> void anchoredInsert(Container &values, int id) {
  auto position = values.begin();
  if (position != values.end())
    ++position;
  values.insert(position, id);
}
} // namespace
World::World(std::shared_ptr<const Definitions> definitions,
             std::shared_ptr<const WorldMap> map, std::uint32_t seed,
             WorldStart start)
    : definitions_(std::move(definitions)), map_(std::move(map)), random_(seed),
      creep_(map_ ? map_->terrain
                  : throw std::invalid_argument("Missing world terrain")),
      units_(1700), generations_(1700), releasedInertia_(1700), effects_(500) {
  if (!definitions_)
    throw std::invalid_argument("Missing world definitions");
  for (int owner = 0; owner < 12; ++owner)
    alliances_[owner] = std::uint16_t((1U << owner) | (1U << 11));
  alliances_[11] = 0x0fff;
  spriteRows_ = SpriteRows(map_->terrain.height());
  air_ = AirField(map_->terrain.width() * 32, map_->terrain.height() * 32);
  explored_.resize(std::size_t(map_->terrain.width()) * map_->terrain.height());
  visibleTo_.resize(explored_.size());
  free_.push_back(0);
  for (int id = 1699; id > 0; --id)
    free_.push_back(id);
  int cursor = addSprite(318, {}, 0);
  if (cursor >= 0)
    effects_[cursor]->hidden = true;
  orderCycle_ = 10;
  if (start == WorldStart::Ready) {
    advance(2);
    frame_ = 0;
  }
}
UnitState *World::get(UnitHandle handle) {
  if (handle.index < 0 || handle.index >= int(units_.size()))
    return nullptr;
  auto &slot = units_[handle.index];
  return slot && slot->handle == handle ? &*slot : nullptr;
}
std::uint8_t World::exploredAt(Point tile) const {
  map_->terrain.tile(tile.x, tile.y);
  return explored_.at(std::size_t(tile.y) * map_->terrain.width() + tile.x);
}
std::uint8_t World::visibleAt(Point tile) const {
  map_->terrain.tile(tile.x, tile.y);
  return visibleTo_.at(std::size_t(tile.y) * map_->terrain.width() + tile.x);
}
void World::advanceVitals(UnitState &unit) {
  const int previousHitpoints = unit.vitals.hitpoints;
  unit.groundCooldown = std::max(0, unit.groundCooldown - 1);
  unit.airCooldown = std::max(0, unit.airCooldown - 1);
  if ((unit.type == 37 || unit.type == 54) && !unit.groundCooldown)
    unit.orderTimer = 0;
  const auto &type = definitions_->units[unit.type];
  if (unit.completed && (type.flags & 0x80U) && unit.vitals.hitpoints > 0)
    unit.vitals.hitpoints = std::int32_t(std::min<std::int64_t>(
        type.hitpoints, std::int64_t(unit.vitals.hitpoints) + 4));
  // Incomplete construction includes its shield regeneration in construction gain.
  if (unit.completed && type.shields)
    unit.vitals.shields = std::int32_t(std::min<std::int64_t>(
        std::int64_t(type.shieldPoints) * 256,
        std::int64_t(unit.vitals.shields) + 7));
  if (unit.vitals.hitpoints != previousHitpoints)
    updateDamageOverlay(unit);
}
void World::revealSight(const UnitState &unit) {
  if (!unit.visible || unit.dead || unit.owner >= 8)
    return;
  const auto &definition = definitions_->units[unit.type];
  int range =
      definition.building() && !unit.completed ? 4 : definition.sightRange;
  revealSightAt(unit.position, unit.owner, range, definition.flags & 4);
}
void World::revealSightAt(Point position, int owner, int range, bool airborne) {
  if (owner < 0 || owner >= 8)
    return;
  Point origin{position.x / 32, position.y / 32};
  auto inMap = [&](Point point) {
    return point.x >= 0 && point.y >= 0 && point.x < map_->terrain.width() &&
           point.y < map_->terrain.height();
  };
  auto reveal = [&](Point tile) {
    auto index = std::size_t(tile.y) * map_->terrain.width() + tile.x;
    auto bit = std::uint8_t(1U << owner);
    explored_[index] |= bit;
    visibleTo_[index] |= bit;
  };
  if (airborne) {
    for (auto offset : sightFootprint(range, true)) {
      Point tile{origin.x + offset.x, origin.y + offset.y};
      if (inMap(tile))
        reveal(tile);
    }
    return;
  }
  // Evaluate inward visibility dependencies in expanding square bands. Each
  // off-axis tile has a diagonal and an axial approach; axes and diagonals
  // keep a single approach. This avoids a precomputed propagation table.
  std::array<std::array<bool, 25>, 25> allowed{}, lit{};
  for (auto offset : sightFootprint(range, false))
    allowed[offset.y + 12][offset.x + 12] = true;
  int observerHeight = map_->terrain.groundHeight(position);
  auto transmits = [&](Point offset) {
    Point tile{origin.x + offset.x, origin.y + offset.y};
    return lit[offset.y + 12][offset.x + 12] && inMap(tile) &&
           map_->terrain.tile(tile.x, tile.y).sightHeight <= observerHeight;
  };
  for (int band = 0; band <= range + 1; ++band)
    for (int y = -band; y <= band; ++y)
      for (int x = -band; x <= band; ++x) {
        if (std::max(std::abs(x), std::abs(y)) != band ||
            !allowed[y + 12][x + 12])
          continue;
        Point tile{origin.x + x, origin.y + y};
        if (!inMap(tile))
          continue;
        int sx = (x > 0) - (x < 0), sy = (y > 0) - (y < 0);
        bool visible = band <= 1 || transmits({x - sx, y - sy});
        if (!visible && x && y && std::abs(x) != std::abs(y)) {
          Point axial =
              std::abs(x) > std::abs(y) ? Point{x - sx, y} : Point{x, y - sy};
          visible = transmits(axial);
        }
        lit[y + 12][x + 12] = visible;
        if (visible)
          reveal(tile);
      }
}
const UnitState *World::get(UnitHandle handle) const {
  return const_cast<World *>(this)->get(handle);
}
Population World::population(int owner) const {
  if (owner < 0 || owner >= 12)
    throw std::out_of_range("Invalid population owner");
  Population result;
  for (const auto &slot : units_) {
    if (!slot || slot->dead || slot->owner != owner)
      continue;
    const auto &unit = *slot;
    const auto &type = definitions_->units.at(unit.type);
    ++result.units.at(unit.type);
    if (unit.completed)
      ++result.completed.at(unit.type);
    for (int race = 0; race < 3; ++race) {
      if (!(type.groupFlags & (1 << race)))
        continue;
      bool reservation =
          (unit.type == 36 || (unit.type == 35 && unit.order == 42)) &&
          !unit.buildQueue.empty();
      const auto &required =
          reservation ? definitions_->units.at(unit.buildQueue.front()) : type;
      result.used[race] += required.supplyRequired *
                          (reservation || !unit.completed
                               ? required.productionCount() : 1);
      if (unit.completed)
        result.available[race] += type.supplyProvided;
      break;
    }
  }
  return result;
}
void World::releasePsiField(UnitState &unit) {
  unit.psiActive = false;
  int field = unit.psiSprite;
  unit.psiSprite = -1;
  if (field < 0 || !effects_.at(field))
    return;
  spriteRows_.remove(effects_[field]->sprite.sceneKey);
  activeEffects_.remove(field);
  effects_[field].reset();
  anchoredInsert(freeEffects_, field);
}
void World::remove(UnitHandle handle) {
  if (auto *unit = get(handle); unit && !unit->dead) {
    releasePsiField(*unit);
    discardPath(*unit);
    spriteRows_.remove(unit->sprite.sceneKey);
    spatial_.remove(handle.index);
    air_.erase(unit->air);
    unit->visible = false;
    unit->dead = true;
    visible_.remove(handle.index);
    hidden_.remove(handle.index);
    anchoredInsert(dead_, handle.index);
  }
}
void World::processDeath(UnitState &unit) {
  releasePsiField(unit);
  releaseMineral(unit);
  unit.gathering = false;
  unit.harvestContinuation.reset();
  unit.landingContinuation.reset();
  unit.queuedCommands.clear();
  unit.queuedUserOverride = false;
  discardPath(unit);
  spatial_.remove(unit.handle.index);
  air_.erase(unit.air);
  unit.vitals.hitpoints = 0;
  unit.order = 0;
  unit.orderState = 1;
  unit.dead = unit.animatedDeath = true;
  unit.visible = false;
  visible_.remove(unit.handle.index);
  hidden_.remove(unit.handle.index);
  anchoredInsert(dead_, unit.handle.index);
  for (auto &slot : units_) {
    if (!slot)
      continue;
    auto &other = *slot;
    if (other.target == unit.handle)
      other.target = {};
    if (other.moveTargetUnit == unit.handle)
      other.moveTargetUnit = {};
    if (other.retaliationTarget == unit.handle)
      other.retaliationTarget = {};
  }
  if (unit.consumedByConstruction) {
    unit.animatedDeath = false;
    spriteRows_.remove(unit.sprite.sceneKey);
    return;
  }
  unit.sprite.animate(1, definitions_->resources(), random_,
                      [&](const auto &instruction, const auto &layer) {
                        return effect(unit.handle, instruction, layer);
                      });
  unit.sprite.tick(definitions_->resources(), random_,
                   [&](const auto &instruction, const auto &layer) {
                     return effect(unit.handle, instruction, layer);
                   });
}
void World::moveSpatial(const UnitState &unit, bool initial) {
  const auto &bounds = definitions_->units[unit.type].bounds;
  Point first{unit.position.x - bounds.left, unit.position.y - bounds.top};
  Point last{unit.position.x + bounds.right, unit.position.y + bounds.bottom};
  if (initial)
    spatial_.insert(unit.handle.index, first, last);
  else
    spatial_.move(unit.handle.index, first, last);
}
const GroundGeometry &World::terrainGeometry(const dat::Extents &shape) {
  if (groundCreep_ != creep_.cells()) {
    groundCache_.clear();
    groundCreep_ = creep_.cells();
  }
  std::array<int, 4> key{shape.left, shape.top, shape.right, shape.bottom};
  for (const auto &entry : groundCache_)
    if (entry.shape == key)
      return *entry.geometry;
  auto geometry = std::make_shared<const GroundGeometry>(
      groundGeometry(map_->terrain, map_->contours, shape, groundCreep_));
  groundCache_.push_back({key, std::move(geometry)});
  return *groundCache_.back().geometry;
}
void World::prepareNavigation(UnitHandle handle) {
  const auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->motion)
    throw std::invalid_argument("Missing navigation worker");
  terrainGeometry(definitions_->units[unit->type].bounds);
}
std::vector<RenderedImage>
World::renderUnits(std::span<const UnitHandle> handles) const {
  std::map<std::uint64_t, const UnitState *> selected;
  for (auto handle : handles)
    if (auto *unit = get(handle); unit && unit->visible && !unit->dead)
      selected.emplace(unit->sprite.sceneKey, unit);
  for (int id : dead_)
    if (units_[id]->animatedDeath && !units_[id]->sprite.order.empty())
      selected.emplace(units_[id]->sprite.sceneKey, &*units_[id]);
  std::vector<const UnitState *> drawing;
  for (auto key : spriteRows_.ordered())
    if (auto found = selected.find(key); found != selected.end())
      drawing.push_back(found->second);
  if (drawing.size() != selected.size())
    throw std::logic_error("Missing unit sprite in draw order");
  auto depth = [&](const auto *unit) {
    int elevation = definitions_->units[unit->type].elevation +
                    int(unit->gasLanding);
    return std::pair{elevation, elevation <= 4 ? unit->position.y : 0};
  };
  std::stable_sort(drawing.begin(), drawing.end(),
                   [&](auto *a, auto *b) { return depth(a) < depth(b); });
  std::vector<RenderedImage> result;
  for (const auto *unit : drawing)
    for (auto at = unit->sprite.order.rbegin(); at != unit->sprite.order.rend();
         ++at) {
      const auto &image = unit->sprite.images[*at];
      if (image.hidden || !definitions_->frameCount(image.type))
        continue;
      result.push_back(
          {{unit->position.x + image.offset.x,
            image.frozenWorldY.value_or(unit->position.y + image.offset.y)},
           image.type,
           image.frame(),
           unit->owner,
           image.modifier,
           image.flip});
    }
  return result;
}
void World::followRoute(UnitHandle handle, std::vector<Point> points) {
  auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->completed ||
      !unit->motion)
    throw std::invalid_argument("Unit cannot follow a worker route");
  if (!unit->motion->stopped())
    throw std::runtime_error(
        "Active route replacement requires a move command");
  if (unit->gathering || unit->cargo)
    throw std::runtime_error(
        "Harvest movement interruption is not implemented yet");
  if (points.empty())
    throw std::invalid_argument("Empty worker route");
  for (auto p : points)
    if (p.x < 0 || p.y < 0 || p.x >= map_->terrain.width() * 32 ||
        p.y >= map_->terrain.height() * 32)
      throw std::out_of_range("Worker route outside world");
  unit->queuedCommands.clear();
  unit->queuedUserOverride = false;
  unit->target = {};
  unit->moveTargetUnit = {};
  unit->targetPosition = points.back();
  unit->moveTargetPosition = points.back();
  unit->pendingRoute = std::move(points);
  unit->order = 6;
  unit->orderState = 0;
  unit->orderTimer = 0;
  unit->mainTimer = 0;
}
void World::follow(UnitHandle handle, UnitHandle destination, bool queued) {
  auto *unit = get(handle);
  const auto *target = get(destination);
  if (!unit || unit->dead || !unit->visible || !unit->completed ||
      !unit->motion || !target || target->dead || !target->visible ||
      (!allied(unit->owner, target->owner) &&
       !(definitions_->units[target->type].flags & 0x2000U)))
    throw std::invalid_argument("Invalid friendly follow command");
  if (handle == destination)
    return;
  if (queued) {
    unit->queuedCommands.push_back({49, target->position, destination});
    activateQueuedCommand(*unit);
    return;
  }
  if (deferLockedCommand(*unit, {49, target->position, destination}))
    return;
  unit->queuedCommands.clear();
  unit->queuedUserOverride = false;
  if (unit->order == 70 && unit->orderState == 2) {
    unit->landingContinuation = UnitCommand{49, target->position, destination};
    return;
  }
  if (interruptHarvest(*unit, {49, target->position, destination}))
    return;
  prepareOrderTransition(*unit);
  bool alreadyFollowing = unit->order == 49;
  if (!alreadyFollowing || unit->target != destination) {
    unit->followPosition = {};
    unit->followTimer = 0;
  }
  unit->target = destination;
  unit->targetPosition = target->position;
  if (unit->escapingOverlap)
    unit->overlapDirectRoute = true;
  unit->pendingRoute.clear();
  unit->failedRouteFrames = 0;
  unit->order = 49;
  unit->orderState = unit->orderTimer = 0;
}
void World::move(UnitHandle handle, Point destination, bool queued) {
  auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->completed ||
      !unit->motion)
    throw std::invalid_argument("Unit cannot receive a move command");
  if (destination.x < 0 || destination.y < 0 ||
      destination.x >= map_->terrain.width() * 32 ||
      destination.y >= map_->terrain.height() * 32)
    throw std::out_of_range("Move destination outside world");
  if (queued) {
    if ((unit->gathering && unit->order != 81) ||
        (unit->order != 2 && unit->order != 3 && unit->order != 23 &&
         unit->order != 6 && unit->order != 10 && unit->order != 14 &&
         unit->order != 49 && unit->order != 31 && unit->order != 81))
      throw std::runtime_error(
          "Queued movement for this order is not implemented");
    unit->queuedCommands.push_back({6, destination, {}});
    activateQueuedCommand(*unit);
    return;
  }
  if (deferLockedCommand(*unit, {6, destination, {}}))
    return;
  unit->queuedCommands.clear();
  unit->queuedUserOverride = false;
  unit->rememberedType = 228;
  if (unit->order == 70 && unit->orderState == 2) {
    unit->landingContinuation = UnitCommand{6, destination, {}};
    return;
  }
  if (interruptHarvest(*unit, {6, destination, {}}))
    return;
  prepareOrderTransition(*unit);
  unit->target = {};
  unit->targetPosition = destination;
  if (unit->escapingOverlap)
    unit->overlapDirectRoute = true;
  unit->pendingRoute.clear();
  unit->failedRouteFrames = 0;
  unit->order = 6;
  unit->orderState = unit->orderTimer = 0;
}
void World::guard(UnitHandle handle, Point destination, bool queued) {
  auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->completed ||
      !unit->motion || destination.x < 0 || destination.y < 0 ||
      destination.x >= map_->terrain.width() * 32 ||
      destination.y >= map_->terrain.height() * 32)
    throw std::invalid_argument("Invalid guard command");
  if (queued && definitions_->orders[2].queueable) {
    unit->queuedCommands.push_back({2, destination, {}});
    activateQueuedCommand(*unit);
    return;
  }
  if (deferLockedCommand(*unit, {2, destination, {}}))
    return;
  move(handle, destination);
  if (unit->harvestContinuation && unit->harvestContinuation->order == 6)
    unit->harvestContinuation->order = 2;
  else if (unit->landingContinuation && unit->landingContinuation->order == 6)
    unit->landingContinuation->order = 2;
  else if (unit->order == 6)
    unit->order = 2;
}
UnitHandle World::rememberedTarget(int owner, Point point, int type) const {
  if (owner < 0 || owner >= 12 || type < 0 || type >= int(definitions_->units.size()))
    throw std::invalid_argument("Invalid remembered-target query");
  if (!(visibleAt({point.x / 32, point.y / 32}) & (1U << owner)))
    return {};
  // Inspect candidates in the world's existing spatial order. A remembered
  // type carries no handle until the clicked footprint is visible.
  for (const auto &entry : spatial_.axes()[0]) {
    if (entry.coordinate < point.x - 96 || entry.coordinate >= point.x + 96)
      continue;
    const auto &candidate = *units_[entry.key];
    const auto &definition = definitions_->units[candidate.type];
    if (candidate.dead || !candidate.visible || candidate.type != type ||
        candidate.position.x - definition.bounds.left >= point.x + 96 ||
        candidate.position.y - definition.bounds.top >= point.y + 96 ||
        candidate.position.y + definition.bounds.bottom < point.y - 96)
      continue;
    if (point.x > candidate.position.x - definition.placementWidth / 2 &&
        point.x <= candidate.position.x + definition.placementWidth / 2 &&
        point.y > candidate.position.y - definition.placementHeight / 2 &&
        point.y <= candidate.position.y + definition.placementHeight / 2)
      return candidate.handle;
  }
  return {};
}
void World::rememberedRightClick(UnitHandle handle, Point position, int type,
                                 bool queued) {
  auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->completed ||
      !unit->motion || type < 0 || type >= 228 || position.x < 0 ||
      position.y < 0 || position.x >= map_->terrain.width() * 32 ||
      position.y >= map_->terrain.height() * 32)
    throw std::invalid_argument("Invalid remembered right-click");
  if (unit->gathering && unit->order != 81)
    throw std::runtime_error("Remembered action during mineral work is not implemented");
  if (queued) {
    unit->queuedCommands.push_back({133, position, {}, type});
    activateQueuedCommand(*unit);
    return;
  }
  if (deferLockedCommand(*unit, {133, position, {}, type}))
    return;
  unit->gathering = false;
  unit->harvestContinuation.reset();
  unit->pendingHarvest = {};
  move(handle, position);
  unit->order = 133;
  unit->rememberedType = type;
}
bool World::deferLockedCommand(UnitState &unit, UnitCommand command) {
  if (!unit.sprite.context.uninterruptible)
    return false;
  // A replacement command retains its place until animation permits a switch.
  // Earlier mandatory queue entries survive replacement of the pending suffix.
  while (!unit.queuedCommands.empty()) {
    const int previous = unit.queuedCommands.back().order;
    if (previous != command.order && !definitions_->orders[previous].interruptible)
      break;
    unit.queuedCommands.pop_back();
  }
  unit.queuedCommands.push_back(command);
  unit.queuedUserOverride = true;
  return true;
}
bool World::activateQueuedCommand(UnitState &unit) {
  if (unit.queuedCommands.empty() || unit.sprite.context.uninterruptible ||
      (!unit.queuedUserOverride &&
       unit.order != 2 && unit.order != 3 && unit.order != 23))
    return false;
  unit.queuedUserOverride = false;
  auto remaining = std::move(unit.queuedCommands);
  unit.queuedCommands.clear();
  auto command = remaining.front();
  remaining.pop_front();
  if (command.order == 151) {
    unit.queuedCommands = std::move(remaining);
    if (!unit.queuedCommands.empty()) {
      unit.queuedUserOverride = true;
      return activateQueuedCommand(unit);
    }
    if (!interruptHarvest(
            unit, {definitions_->units[unit.type].returnIdle, {}, {}})) {
      prepareOrderTransition(unit);
      unit.order = definitions_->units[unit.type].returnIdle;
      unit.orderState = unit.orderTimer = unit.mainTimer = 0;
    }
    return true;
  }
  if (command.order == 133)
    rememberedRightClick(unit.handle, command.position, command.rememberedType);
  else if (command.order == 10)
    beginAttack(unit.handle, command.target, false, command.automaticAttack);
  else if (command.order == 14)
    attackMove(unit.handle, command.position);
  else if (command.order == 6)
    move(unit.handle, command.position);
  else if (command.order == 2)
    guard(unit.handle, command.position);
  else if (command.order == 49)
    follow(unit.handle, command.target);
  else if (command.order == 84 || command.order == 90)
    returnCargo(unit.handle, false, command.target);
  else if (command.order == 79 || command.order == 81 || command.order == 85)
    harvest(unit.handle, command.target,
            command.order == 81 ? HarvestOrder::Gas
            : command.order == 85 ? HarvestOrder::Minerals : HarvestOrder::Gather);
  else
    throw std::logic_error("Unsupported queued command kind");
  unit.queuedCommands.insert(unit.queuedCommands.end(), remaining.begin(),
                             remaining.end());
  return true;
}
bool World::workerPositionBlocked(UnitHandle handle, Point position) const {
  const auto *worker = get(handle);
  if (!worker || worker->dead || !worker->visible || !worker->completed ||
      !(definitions_->units[worker->type].flags & 8) ||
      (definitions_->units[worker->type].flags & 4))
    throw std::invalid_argument("Invalid ground-worker placement query");
  const auto &shape = definitions_->units[worker->type].bounds;
  const auto &terrain = map_->terrain;
  if (position.x < shape.left || position.y < shape.top ||
      position.x >= terrain.width() * 32 - shape.right ||
      position.y >= terrain.height() * 32 - shape.bottom)
    return true;
  auto covered = creep_.cells()[std::size_t(position.y / 32) * terrain.width() +
                                position.x / 32];
  if ((!covered && !terrain.walkable(position.x / 8, position.y / 8)) ||
      !map_->contours.permits(position, shape))
    return true;
  for (int id : visible_) {
    const auto &other = *units_[id];
    const auto &type = definitions_->units[other.type];
    if (other.handle == handle || other.dead || !other.visible ||
        (type.flags & 4) || other.type == 35 ||
        (worker->gathering && !type.building()))
      continue;
    const auto &bounds = type.bounds;
    if ((!bounds.left && !bounds.right) || (!bounds.top && !bounds.bottom))
      continue;
    if (position.x - shape.left <= other.position.x + bounds.right &&
        position.x + shape.right >= other.position.x - bounds.left &&
        position.y - shape.top <= other.position.y + bounds.bottom &&
        position.y + shape.bottom >= other.position.y - bounds.top)
      return true;
  }
  return false;
}
void World::relocateWorkerForSearch(UnitHandle handle, Point position) {
  auto *worker = get(handle);
  if (!worker || worker->dead || !worker->visible || !worker->completed ||
      !worker->motion || !(definitions_->units[worker->type].flags & 8) ||
      (definitions_->units[worker->type].flags & 4))
    throw std::invalid_argument("Invalid position-search worker");
  if (frame_ || worker->movementPreparation != 2 || worker->gathering ||
      worker->cargo || worker->path || worker->planRequested ||
      !worker->pendingRoute.empty())
    throw std::runtime_error(
        "Position relocation requires an untouched worker");
  const auto &shape = definitions_->units[worker->type].bounds;
  int right = map_->terrain.width() * 32 - shape.right - 1;
  int bottom = map_->terrain.height() * 32 - shape.bottom - 33;
  if (right < shape.left || bottom < shape.top)
    throw std::out_of_range("No worker relocation domain");
  position = {std::clamp(position.x, shape.left, right),
              std::clamp(position.y, shape.top, bottom)};
  worker->motion->relocateInitial(position);
  worker->position = position;
  worker->moveTargetPosition = position;
  spriteRows_.move(worker->sprite.sceneKey, position);
  spatial_.remove(handle.index);
  moveSpatial(*worker, true);
}
UnitHandle World::mineralDepot(UnitHandle handle) const {
  const auto *worker = get(handle);
  if (!worker || worker->dead || !worker->visible ||
      !(definitions_->units[worker->type].flags & 8))
    throw std::invalid_argument("Invalid worker depot query");
  UnitHandle selected;
  int closest = std::numeric_limits<int>::max();
  int firstEncounter = std::numeric_limits<int>::max();
  auto encounter = spatial_.encountersFrom(worker->handle.index);
  int sourceRegion = map_->regions.regionAt(worker->position);
  for (int id : visible_) {
    const auto &candidate = *units_[id];
    if (candidate.owner != worker->owner || !candidate.completed ||
        !(definitions_->units[candidate.type].flags & 0x1000U))
      continue;
    int targetRegion = map_->regions.regionAt(candidate.position);
    if (map_->regions.entries()[sourceRegion].group !=
        map_->regions.entries()[targetRegion].group)
      continue;
    int away = distance({candidate.position.x - worker->position.x,
                         candidate.position.y - worker->position.y});
    if (away < closest ||
        (away == closest && encounter.at(id) < firstEncounter)) {
      closest = away;
      firstEncounter = encounter.at(id);
      selected = candidate.handle;
    }
  }
  return selected;
}
void World::setHallucinated(UnitHandle handle) {
  auto *unit = get(handle);
  if (!unit || unit->dead)
    throw std::invalid_argument("Missing hallucination unit");
  unit->hallucinated = true;
  if (!unit->removeTimer || unit->removeTimer > 1350)
    unit->removeTimer = 1350;
}
void World::setResources(UnitHandle handle, int amount) {
  auto *unit = get(handle);
  if (!unit || unit->dead)
    throw std::invalid_argument("Missing resource unit");
  if (!(definitions_->units[unit->type].flags & 0x2000U))
    return;
  unit->resources = amount;
  unit->sprite.context.resources = amount;
  if (unit->type >= 176 && unit->type <= 178) {
    int animation = amount < 250   ? 13
                    : amount < 500 ? 14
                    : amount < 750 ? 15
                                   : 20;
    if (animation != unit->resourceAnimation) {
      unit->resourceAnimation = animation;
      unit->sprite.animate(animation, definitions_->resources(), random_,
                           [&](const auto &i, const auto &layer) {
                             return effect(handle, i, layer);
                           });
    }
  }
}
void World::releasePathSlot(UnitState &unit) {
  if (unit.pathSlot < 0)
    return;
  pathPool_.release(unit.pathSlot);
  unit.pathSlot = -1;
}
void World::discardPath(UnitState &unit) {
  releasePathSlot(unit);
  unit.path.reset();
  unit.pathTarget = {};
}
void World::recordPath(UnitState &unit, const LocalRouteResult &path) {
  unit.pathTarget = unit.moveTargetUnit;
  unit.routeContinuesAtWaypoint =
      unit.motion && !path.stuck &&
      (!path.reached ||
       distance({path.destination.x - unit.motion->waypoint().x,
                 path.destination.y - unit.motion->waypoint().y}) >= 16);

  if (definitions_->units[unit.type].flags & 4) {
    releasePathSlot(unit);
    return;
  }
  if (unit.pathSlot < 0)
    unit.pathSlot = pathPool_.acquire();
  auto regions = path.retained.regions;
  if (regions.empty())
    regions.push_back(map_->regions.regionAt(unit.position));
  std::optional<int> conversionRegion;
  if (path.points.size() > 1) {
    // Public conversion probes retain the second waypoint's X value as the
    // regional identity. Keep it only when it names a region in this world.
    auto retained = path.points[1].x;
    if (retained >= 0 && retained < int(map_->regions.entries().size()))
      conversionRegion = retained;
  }
  pathPool_.route(unit.pathSlot, regions, frame_, conversionRegion,
                  path.points.size() > 1);
}
void World::recordEscape(UnitState &unit) {
  if (definitions_->units[unit.type].flags & 4) {
    releasePathSlot(unit);
    unit.overlapRegion.reset();
    return;
  }
  if (unit.pathSlot < 0)
    unit.pathSlot = pathPool_.acquire();
  pathPool_.escape(unit.pathSlot);
  unit.overlapRegion = pathPool_.firstRegion(unit.pathSlot);
}
void World::refreshPowerState() {
  std::vector<std::pair<int, Point>> providers;
  for (int id : visible_) {
    const auto &unit = *units_[id];
    if (!unit.dead && unit.type == 156 && unit.psiActive)
      providers.emplace_back(unit.owner, unit.position);
  }
  auto resources = definitions_->resources();
  for (int id : visible_) {
    auto &unit = *units_[id];
    const auto &body = definitions_->units[unit.type];
    if (unit.dead || !body.building() || !(body.flags & 0x80000U) ||
        !(body.groupFlags & 4) || unit.sprite.context.flying)
      continue;
    bool disabled = std::none_of(providers.begin(), providers.end(),
        [&](const auto &provider) {
          return provider.first == unit.owner &&
              withinPsiField({unit.position.x - provider.second.x,
                              unit.position.y - provider.second.y});
        });
    if (disabled == unit.psiDisabled)
      continue;
    unit.psiDisabled = disabled;
    if (!unit.completed)
      continue;
    auto host = [&](const auto &instruction, const auto &layer) {
      return effect(unit.handle, instruction, layer);
    };
    unit.sprite.animate(disabled ? 24 : 27, resources, random_, host);
  }
}
void World::advance(int frames) {
  if (frames < 0)
    throw std::invalid_argument("Negative frame count");
  if (!frames)
    return;
  auto supported = [](int order) {
    return order == 0 || order == 2 || order == 3 || order == 6 || order == 10 || order == 14 || order == 18 || order == 19 || order == 22 ||
           order == 23 || order == 166 || order == 133 || order == 79 || order == 81 || order == 85 ||
           order == 86 || order == 87 || order == 88 || order == 89 ||
           order == 84 || order == 90 || order == 151 || order == 77 || order == 93 ||
           order == 101 || order == 41 || order == 42 || order == 49 ||
           order == 25 || order == 30 || order == 31 || order == 70 ||
           order == 48 || order == 164 || order == 26 || order == 45 ||
           order == 150;
  };
  const UnitState *unsupported = nullptr;
  for (int id : visible_) {
    const auto &slot = units_[id];
    if (slot && slot->visible && !slot->dead && !supported(slot->order) &&
        (!unsupported || slot->handle.index < unsupported->handle.index))
      unsupported = &*slot;
  }
  if (unsupported)
    throw std::runtime_error(
        "Unimplemented native order " + std::to_string(unsupported->order) +
        " for unit type " + std::to_string(unsupported->type));
  auto resources = definitions_->resources();
  for (int n = 0; n < frames; ++n) {
    if (frame_ == std::numeric_limits<int>::max())
      throw std::overflow_error("World frame counter overflow");
    ++frame_;
    if (!visionRefreshIn_)
      visionRefreshIn_ = 100;
    bool refreshVision = --visionRefreshIn_ == 0;
    if (refreshVision)
      std::fill(visibleTo_.begin(), visibleTo_.end(), 0);
    for (auto at = dead_.begin(); at != dead_.end();) {
      int id = *at++;
      auto &unit = *units_[id];
      if (unit.animatedDeath) {
        unit.sprite.tick(resources, random_,
                         [&](const auto &instruction, const auto &layer) {
                           return effect(unit.handle, instruction, layer);
                         });
        if (!unit.sprite.order.empty())
          continue;
        spriteRows_.remove(unit.sprite.sceneKey);
      }
      dead_.remove(id);
      if (unit.motion)
        releasedInertia_[id] = unit.motion->inertia();
      else if (unit.inactiveInertia)
        releasedInertia_[id] = unit.inactiveInertia;
      units_[id].reset();
      anchoredInsert(free_, id);
    }
    if (--orderCycle_ == 0) {
      orderCycle_ = 150;
      int phase = 0;
      for (int id : visible_) {
        units_[id]->orderTimer = phase;
        phase = (phase + 1) % 8;
      }
    }
    if (--secondaryCycle_ == 0) {
      secondaryCycle_ = 300;
      int phase = 0;
      for (int id : visible_) {
        units_[id]->secondaryTimer = phase;
        phase = (phase + 1) % 30;
      }
    }
    for (int id : visible_)
      if (units_[id]->motion) {
        auto &unit = *units_[id];
        if (unit.movementPreparation > 0)
          --unit.movementPreparation;
        auto &motion = *unit.motion;
        bool arrivalUpdated = false, legalityArrivalPending = false;
        if (unit.collisionPending && unit.collisionWait > 1 &&
            motion.needsArrivalUpdate() &&
            samePoint(unit.position, unit.moveTargetPosition)) {
          // A legality delay parks the next departure, but the accepted
          // arrival still consumes its final stationary physics update.
          motion.advance();
          arrivalUpdated = true;
        }
        if (unit.landingArrivalPending) {
          unit.landingArrivalPending = false;
          if (motion.arrived() && motion.moving()) {
            motion.advance();
            arrivalUpdated = true;
          }
        }
        if (unit.path && !unit.escapingOverlap)
          unit.routeContinuesAtWaypoint =
              !unit.path->stuck &&
              (!unit.path->reached ||
               distance({unit.path->destination.x - motion.waypoint().x,
                         unit.path->destination.y - motion.waypoint().y}) >=
                   16);

        // Braking ends the prior path continuation, including after its path
        // record has already been released.
        if (motion.braking())
          unit.routeContinuesAtWaypoint = false;
        if (!unit.gasLanding && !unit.collisionPending && !unit.escapingOverlap &&
            !unit.gathering && !(definitions_->units[unit.type].flags & 4) &&
            !motion.scriptDriven() && !motion.brakingTravel() && motion.arrived() &&
            !clearOverlap(unit, unit.position)) {
          // Final logical arrival enters recovery with accepted physics.
          // Local route endpoints can still require an arrival update.
          if (motion.needsArrivalUpdate() &&
              !samePoint(unit.position, unit.moveTargetPosition)) {
            motion.advance();
            arrivalUpdated = true;
          }
          legalityArrivalPending = motion.needsArrivalUpdate();
          unit.collisionPending = true;
          unit.collisionWait = 0;
          unit.overlapReturnTarget = unit.moveTargetPosition;
        }
        // Idle units enter the overlap recheck on the following update; an
        // already active route enters it directly. Both keep accepted physics
        // while the check prepares a fresh path.
        bool waitingCollision = unit.collisionPending && unit.collisionWait > 1;
        if (waitingCollision)
          --unit.collisionWait;
        bool collisionRecheck = unit.collisionPending && !waitingCollision;
        // A newborn's first clear-space check prepares its route before the
        // following frame accepts movement.
        bool initialCollision = collisionRecheck && (!motion.hasRoute() ||
                                                     unit.collisionRouteReset);
        bool overlapCleared = false, collisionRetry = false,
             resumedOverlapOrder = false;
        auto escapeGoal = motion.waypoint();
        int escapeDistance = distance(
            {escapeGoal.x - unit.position.x, escapeGoal.y - unit.position.y});
        bool lostEscapeAlignment =
            unit.overlapRegion &&
            *unit.overlapRegion == map_->regions.regionAt(unit.position) &&
            !motion.scriptDriven() &&
            !samePoint(unit.moveTargetPosition, escapeGoal) &&
            escapeDistance <= 16 &&
            std::abs(signedHeading(motion.desiredHeading() -
                                   motion.travelHeading())) >
                escapeDistance * 2;
        bool deferOverlap = unit.escapingOverlap &&
                            (motion.arrived() || lostEscapeAlignment) &&
                            !collisionRecheck && !waitingCollision;
        if (deferOverlap) {
          releasePathSlot(unit);
          unit.collisionPending = true;
          unit.collisionWait = 1;
          notifyOverlap(unit);
        }
        bool settleCollision =
            collisionRecheck &&
            samePoint(unit.position, unit.moveTargetPosition) &&
            (motion.arrived() || motion.settling());
        const bool prepareClearedRoute =
            collisionRecheck && unit.path && !unit.escapingOverlap &&
            !samePoint(unit.position, unit.moveTargetPosition) &&
            !motion.braking();
        if (collisionRecheck) {
          if (unit.type != 35) {
            const auto &shape = definitions_->units[unit.type].bounds;
            if (clearOverlap(unit, unit.position)) {
              if (!unit.escapingOverlap &&
                  samePoint(unit.position, unit.moveTargetPosition))
                motion.finishRecoveryWait();
              if (unit.escapingOverlap && unit.overlapOrderChanged) {
                unit.moveTargetPosition = unit.overlapReturnTarget;
                discardPath(unit);
                unit.planRequested = true;
                unit.routePlanningFrames = 0;
                resumedOverlapOrder = true;
              } else if (unit.escapingOverlap)
                motion.hold(unit.order == 10 ? unit.targetPosition
                                            : unit.overlapReturnTarget);
              unit.escapingOverlap = false;
              unit.overlapOrderChanged = false;
              unit.overlapFreshOrder = false;
              unit.overlapResumeDirect = false;
              if (prepareClearedRoute) {
                // A route admitted by a legality recheck starts on the next
                // movement update; arrival recovery keeps its own timing.
                discardPath(unit);
                unit.planRequested = true;
                unit.routePlanningFrames = 0;
              }
              overlapCleared = true;
            } else {
              bool mobileBlocker = false, fixedBlocker = false,
                   buildingBlocker = false;
              const UnitState *resetBlocker = nullptr;
              std::vector<int> resetCandidates;
              for (const auto &body : bodies(id)) {
                const auto &other = *units_[body.key];
                const auto &type = definitions_->units[other.type];
                if (body.flying || other.type == 35 ||
                    (unit.gathering && !type.building()))
                  continue;
                if (unit.position.x + shape.right >=
                        body.position.x - body.bounds.left &&
                    unit.position.x - shape.left <=
                        body.position.x + body.bounds.right &&
                    unit.position.y + shape.bottom >=
                        body.position.y - body.bounds.top &&
                    unit.position.y - shape.top <=
                        body.position.y + body.bounds.bottom) {
                  resetCandidates.push_back(body.key);
                  fixedBlocker |= !(type.flags & 0x8000000U);
                  buildingBlocker |= type.building();
                  mobileBlocker |=
                      other.escapingOverlap ||
                      (other.collisionPending && other.collisionWait <= 1) ||
                      !samePoint(other.position, other.moveTargetPosition);
                }
              }
              std::pair<std::int64_t, std::int64_t> blockerRank{
                  std::numeric_limits<std::int64_t>::max(),
                  std::numeric_limits<std::int64_t>::max()};
              for (int candidate : visible_)
                if (std::find(resetCandidates.begin(), resetCandidates.end(),
                              candidate) != resetCandidates.end()) {
                  const auto &other = *units_[candidate];
                  const auto &bounds = definitions_->units[other.type].bounds;
                  // Within a footprint-size class, obstruction checks encounter
                  // the first horizontal footprint edge in the actor's query.
                  // A closer center need not be the primary blocker.
                  auto left = other.position.x - bounds.left;
                  auto edge = left >= unit.position.x - shape.left
                                  ? left
                                  : other.position.x + bounds.right;
                  auto area = std::int64_t(bounds.left + bounds.right + 1) *
                              (bounds.top + bounds.bottom + 1);
                  auto rank = std::pair{-area, std::int64_t(edge)};
                  if (rank <= blockerRank) {
                    blockerRank = rank;
                    resetBlocker = &other;
                  }
                }
              if (resetBlocker) {
                const auto &type = definitions_->units[resetBlocker->type];
                bool stationaryHarvest = resetBlocker->gathering &&
                                         resetBlocker->motion &&
                                         !resetBlocker->motion->moving() &&
                                         ((resetBlocker->order == 87 &&
                                           resetBlocker->orderState == 5) ||
                                          (resetBlocker->order == 86 &&
                                           resetBlocker->orderState == 2));
                fixedBlocker =
                    !(type.flags & 0x8000000U) || stationaryHarvest ||
                    (!unit.path && !resetBlocker->escapingOverlap &&
                     resetBlocker->motion && resetBlocker->motion->moving() &&
                     (resetBlocker->motion->settling() ||
                      resetBlocker->motion->arrived()));
                buildingBlocker = type.building();
                mobileBlocker = resetBlocker->escapingOverlap ||
                                (resetBlocker->collisionPending &&
                                 resetBlocker->collisionWait <= 1) ||
                                !samePoint(resetBlocker->position,
                                           resetBlocker->moveTargetPosition);
              }
              if (fixedBlocker)
                mobileBlocker = false;
              if (!unit.escapingOverlap)
                unit.overlapReturnTarget = unit.moveTargetPosition;
              std::optional<Point> exposedExit;
              bool freshMove =
                  unit.order == 6 &&
                  (unit.planRequested || unit.collisionRouteReset || unit.path);
              bool replacingRoute =
                  unit.collisionRouteReset ||
                  (freshMove && !unit.overlapOrderChanged);
              if (fixedBlocker || (!replacingRoute && !mobileBlocker)) {
                // Idle bodies also try the nearest geometric exit from a
                // fixed obstruction before spending random escape samples.
                // Route replacement can depart from a connected footprint.
                // A stopped recheck instead uses one footprint or a pairwise
                // junction; larger connected groups resume recovery sampling.
                std::vector<PixelBox> occupied;
                for (const auto &body : bodies(id)) {
                  const auto &other = *units_[body.key];
                  const auto &type = definitions_->units[other.type];
                  if (body.flying || other.type == 35 ||
                      (unit.gathering && !type.building()))
                    continue;
                  occupied.push_back(
                      {body.position.x - body.bounds.left - shape.right,
                       body.position.y - body.bounds.top - shape.bottom,
                       body.position.x + body.bounds.right + shape.left,
                       body.position.y + body.bounds.bottom + shape.top});
                }
                // Resolve contact against a common constraint window before
                // validating its nearest perimeter projection. An invalid
                // nearest projection resumes sampling; it does not select a
                // more distant exposed corner of the footprint union.
                if (resetBlocker) {
                  const auto &bounds =
                      definitions_->units[resetBlocker->type].bounds;
                  PixelBox perimeter{
                      resetBlocker->position.x - bounds.left - shape.right - 1,
                      resetBlocker->position.y - bounds.top - shape.bottom - 1,
                      resetBlocker->position.x + bounds.right + shape.left + 1,
                      resetBlocker->position.y + bounds.bottom + shape.top + 1};
                  PixelBox window{unit.position.x - 128, unit.position.y - 128,
                                  unit.position.x + 128, unit.position.y + 128};
                  for (const auto &box : occupied) {
                    if (box.left >= perimeter.right ||
                        box.right <= perimeter.left ||
                        box.top >= perimeter.bottom ||
                        box.bottom <= perimeter.top)
                      continue;
                    window.left = std::max(window.left, box.left - 1);
                    window.top = std::max(window.top, box.top - 1);
                    window.right = std::min(window.right, box.right + 1);
                    window.bottom = std::min(window.bottom, box.bottom + 1);
                  }
                  if (window.left <= window.right &&
                      window.top <= window.bottom) {
                    std::optional<Point> closest;
                    int best = std::numeric_limits<int>::max();
                    for (PixelBox side : std::array<PixelBox, 4>{
                             PixelBox{perimeter.left, perimeter.top,
                                      perimeter.right, perimeter.top},
                             PixelBox{perimeter.right, perimeter.top,
                                      perimeter.right, perimeter.bottom},
                             PixelBox{perimeter.left, perimeter.bottom,
                                      perimeter.right, perimeter.bottom},
                             PixelBox{perimeter.left, perimeter.top,
                                      perimeter.left, perimeter.bottom}}) {
                      int left = std::max(side.left, window.left);
                      int top = std::max(side.top, window.top);
                      int right = std::min(side.right, window.right);
                      int bottom = std::min(side.bottom, window.bottom);
                      if (left > right || top > bottom)
                        continue;
                      Point point{std::clamp(unit.position.x, left, right),
                                  std::clamp(unit.position.y, top, bottom)};
                      int rank = distance({point.x - unit.position.x,
                                           point.y - unit.position.y});
                      if (rank < best) {
                        best = rank;
                        closest = point;
                      }
                    }
                    if (closest &&
                        Placement(map_->terrain, map_->regions, {})
                            .terrainFits(*closest, shape) &&
                        std::none_of(occupied.begin(), occupied.end(),
                                     [&](const auto &box) {
                                       return box.contains(*closest);
                                     }))
                      exposedExit = closest;
                  }
                }
              }
              if (exposedExit) {
                // A direct departure created before regional planning has no
                // assigned path region. Retain an existing route's context
                // when a replacement order reuses its escape.
                if (unit.path) {
                  const auto &regional = unit.path->retained;
                  unit.overlapRegion =
                      regional.cursor < regional.regions.size()
                          ? regional.regions[regional.cursor]
                          : map_->regions.regionAt(unit.path->source);
                } else if (!unit.escapingOverlap)
                  unit.overlapRegion.reset();
                if (freshMove && !unit.overlapOrderChanged) {
                  unit.overlapOrderChanged = true;
                  unit.overlapFreshOrder = true;
                }
                recordEscape(unit);
                unit.path.reset();
                unit.planRequested = false;
                unit.escapingOverlap = true;
                if (!unit.overlapOrderChanged) {
                  unit.moveTargetPosition = *exposedExit;
                  unit.moveTargetUnit = {};
                }
                bool stopAtEnd =
                    !unit.overlapResumeDirect && !unit.routeContinuesAtWaypoint;
                unit.overlapDirectRoute =
                    unit.overlapResumeDirect || !unit.overlapOrderChanged;
                unit.overlapResumeDirect = false;
                motion.route({*exposedExit}, 0, false, stopAtEnd);
              } else if (mobileBlocker && random_.scaled(0, 31) < 24) {
                collisionRetry = true;
                // The initial route request remains accepted while its
                // moving blocker is rechecked. A later escape returns to it.
                if (unit.order == 6 && unit.planRequested &&
                    !unit.overlapOrderChanged) {
                  unit.overlapOrderChanged = true;
                  unit.overlapFreshOrder = true;
                }
                // A parked legality retry finishes arrival inertia once;
                // an accepted escape retains it for its later departure.
                if (legalityArrivalPending) {
                  motion.advance();
                  arrivalUpdated = true;
                }
                // A retry preserves residual braking travel. Ordinary recovery
                // waits in place without replacing the pending destination.
                if (!motion.braking() &&
                    !((unit.order == 6 || unit.order == 49) &&
                      unit.overlapOrderChanged))
                  motion.waitForRecovery(unit.overlapReturnTarget);
                overlapCleared = true;
              } else {
                auto direction =
                    motion.travelHeading() + random_.scaled(-3, 3) * 16;
                auto radius = random_.scaled(2, 4) * (buildingBlocker ? 12 : 4);
                auto vector = directionVector(direction);
                Point escape{unit.position.x + fixedProduct(vector.x, radius),
                             unit.position.y + fixedProduct(vector.y, radius)};
                bool fallback = !clearOverlap(unit, escape);
                if (fallback) {
                  Point toward{unit.moveTargetPosition.x - unit.position.x,
                               unit.moveTargetPosition.y - unit.position.y};
                  // Distant accepted goals admit a second, goal-directed
                  // candidate. Nearby goals keep the local square sampling.
                  bool local =
                      distance(toward) <= 32 || random_.scaled(0, 31) >= 24;
                  if (local) {
                    escape = {unit.position.x + random_.scaled(-2, 2) * 16,
                              unit.position.y + random_.scaled(-2, 2) * 16};
                  } else {
                    auto direction =
                        heading(toward) + random_.scaled(-1, 1) * 16;
                    auto radius = (8 + random_.scaled(0, 2) * 4) *
                                  (buildingBlocker ? 3 : 1);
                    auto directionStep = directionVector(direction);
                    escape = {unit.position.x +
                                  fixedProduct(directionStep.x, radius),
                              unit.position.y +
                                  fixedProduct(directionStep.y, radius)};
                  }
                }
                escape.x =
                    std::clamp(escape.x, shape.left,
                               map_->terrain.width() * 32 - shape.right - 1);
                escape.y =
                    std::clamp(escape.y, shape.top,
                               map_->terrain.height() * 32 - shape.bottom - 33);
                bool rejected =
                    samePoint(escape, unit.position) ||
                    (fallback && !Placement(map_->terrain, map_->regions, {})
                                      .terrainFits(escape, shape));
                if (fallback && mobileBlocker)
                  for (const auto &body : bodies(id)) {
                    const auto &other = *units_[body.key];
                    const auto &type = definitions_->units[other.type];
                    if (body.flying || other.type == 35 ||
                        (type.flags & 0x8000000U))
                      continue;
                    if (escape.x + shape.right >=
                            body.position.x - body.bounds.left &&
                        escape.x - shape.left <=
                            body.position.x + body.bounds.right &&
                        escape.y + shape.bottom >=
                            body.position.y - body.bounds.top &&
                        escape.y - shape.top <=
                            body.position.y + body.bounds.bottom)
                      rejected = true;
                  }
                if (rejected) {
                  collisionRetry = true;
                  if (legalityArrivalPending) {
                    motion.advance();
                    arrivalUpdated = true;
                  }
                  if (!motion.braking() &&
                      !((unit.order == 6 || unit.order == 49) &&
                        unit.overlapOrderChanged))
                    motion.waitForRecovery(unit.overlapReturnTarget);
                  overlapCleared = true;
                } else {
                  bool continuingRoute =
                      unit.overlapResumeDirect ||
                      unit.routeContinuesAtWaypoint ||
                      (unit.escapingOverlap && unit.overlapOrderChanged &&
                       !motion.brakesAtRouteEnd()) ||
                      (unit.overlapOrderChanged && unit.path &&
                       !samePoint(motion.waypoint(), unit.moveTargetPosition));
                  if (unit.order == 6 &&
                      (unit.planRequested || unit.collisionRouteReset) &&
                      !unit.overlapOrderChanged) {
                    unit.overlapOrderChanged = true;
                    unit.overlapFreshOrder = true;
                  }
                  if (!unit.overlapOrderChanged) {
                    unit.moveTargetPosition = escape;
                    unit.moveTargetUnit = {};
                  }
                  unit.overlapRegion = map_->regions.regionAt(unit.position);
                  recordEscape(unit);
                  unit.path.reset();
                  unit.planRequested = false;
                  unit.escapingOverlap = true;
                  unit.overlapDirectRoute =
                      continuingRoute || !unit.overlapOrderChanged;
                  unit.overlapResumeDirect = false;
                  motion.route({escape}, 0, false, !continuingRoute);
                }
              }
            }
          }
          if (unit.type == 35) {
            const auto &shape = definitions_->units[unit.type].bounds;
            if (!map_->contours.permits(unit.position, shape))
              throw std::runtime_error(
                  "Illegal larva terrain recovery is not implemented yet");
            for (const auto &body : bodies(id))
              if (definitions_->units[units_[body.key]->type].building() &&
                  unit.position.x + shape.right >=
                      body.position.x - body.bounds.left &&
                  unit.position.x - shape.left <=
                      body.position.x + body.bounds.right &&
                  unit.position.y + shape.bottom >=
                      body.position.y - body.bounds.top &&
                  unit.position.y - shape.top <=
                      body.position.y + body.bounds.bottom)
                throw std::runtime_error(
                    "Illegal larva building recovery is not implemented yet");
            motion.faceOnArrival(std::nullopt);
          }
          unit.overlapStopRecovery = false;
          unit.collisionRouteReset = false;
          unit.collisionPending = collisionRetry;
          unit.collisionWait = 0;
          if (collisionRetry && (unit.order == 6 || unit.order == 49) &&
              unit.overlapOrderChanged) {
            resumedOverlapOrder = true;
            unit.moveTargetPosition = unit.overlapReturnTarget;
            unit.planRequested = true;
            unit.routePlanningFrames = 1;
            discardPath(unit);
          }
        }
        bool retryFailure = !unit.planRequested && unit.failedRouteFrames > 0 &&
                            --unit.failedRouteFrames == 0;
        if (unit.planRequested)
          unit.failedRouteFrames = 0;
        // A collision-reset order can prepare the old harvest path before
        // its queued target becomes active. Physical recovery resumes next
        // tick.
        bool harvestResetPath = waitingCollision && unit.order == 151 &&
                                unit.escapingOverlap &&
                                get(unit.pendingHarvest);
        if (harvestResetPath) {
          unit.planRequested = true;
          unit.routePlanningFrames = 1;
          resumedOverlapOrder = true;
        }
        // A newly accepted destination supersedes the old leg even when
        // arrival and replanning happen in the same update.
        bool continuation = !unit.planRequested && unit.path &&
                            !unit.path->reached && motion.arrived();
        const auto *followTarget = get(unit.moveTargetUnit);
        const auto *oldPathTarget = unit.path ? get(unit.pathTarget) : nullptr;
        const bool finishLogicalArrival = motion.needsArrivalUpdate() &&
            (samePoint(unit.position, unit.moveTargetPosition) ||
             (unit.interruptedMoveGoal &&
              samePoint(unit.position, *unit.interruptedMoveGoal)));
        bool expiredMovingContact = false;
        if (unit.path && unit.path->reached && !unit.path->stuck &&
            motion.arrived() && !finishLogicalArrival &&
            oldPathTarget && !oldPathTarget->dead &&
            oldPathTarget->motion && oldPathTarget->motion->moving() &&
            !(definitions_->units[unit.type].flags & 4) &&
            !(definitions_->units[oldPathTarget->type].flags & 4)) {
          const auto &a = definitions_->units[unit.type].bounds;
          const auto &b = definitions_->units[oldPathTarget->type].bounds;
          const auto p = oldPathTarget->position;
          const PixelBox contact{p.x - b.left - a.right - 1,
                                 p.y - b.top - a.bottom - 1,
                                 p.x + b.right + a.left + 1,
                                 p.y + b.bottom + a.top + 1};
          expiredMovingContact = !contact.contains(motion.waypoint());
        }
        bool followContactContinuation =
            !unit.planRequested && unit.path && unit.path->reached &&
            !unit.escapingOverlap && !unit.collisionPending && followTarget &&
            followTarget->motion && followTarget->motion->moving() &&
            (((unit.order == 49 || unit.order == 10) &&
              motion.needsWaypointContinuation()) || expiredMovingContact);
        bool recovery = motion.replanning();
        bool deferredPlan = unit.planRequested && unit.routePlanningFrames > 1;
        if (deferredPlan)
          --unit.routePlanningFrames;
        if (!deferredPlan && (!waitingCollision || harvestResetPath) &&
            (unit.planRequested || continuation || followContactContinuation ||
             recovery || retryFailure)) {
          const Point destination =
              continuation ? unit.path->destination : unit.moveTargetPosition;
          bool flying = (definitions_->units[unit.type].flags & 4) ||
                        unit.gasLanding;
          auto contactBodies = bodies(id);
          std::erase_if(contactBodies, [&](const auto &body) {
            const auto &other = *units_[body.key];
            // An explicitly followed body defines contact even when ordinary
            // traffic, such as larvae, does not obstruct this unit's route.
            if (get(unit.moveTargetUnit) &&
                body.key == std::uint32_t(unit.moveTargetUnit.index))
              return false;
            if ((unit.gathering || unit.type == 35) &&
                !definitions_->units[other.type].building())
              return true;
            if (other.type == 35)
              return true;
            // Recovery retains the body that rejected this route even when
            // that body is moving. Dropping it can reproduce the blocked leg.
            if ((motion.reroutesRecordedObstacle() || motion.waitingForBody()) &&
                other.handle == unit.collisionObstacle)
              return false;
            return !motion.reroutesMovingObstacles() &&
                   !samePoint(other.position, other.moveTargetPosition);
          });
          auto obstacles = contactBodies;
          std::erase_if(obstacles, [&](const auto &body) {
            const auto &shape = definitions_->units[unit.type].bounds;
            PixelBox occupied{body.position.x - body.bounds.left - shape.right,
                              body.position.y - body.bounds.top - shape.bottom,
                              body.position.x + body.bounds.right + shape.left,
                              body.position.y + body.bounds.bottom + shape.top};
            const bool touchingRecordedBody =
                motion.reroutesRecordedObstacle() &&
                body.key == std::uint32_t(unit.collisionObstacle.index) &&
                !occupied.contains(destination) &&
                occupied.contains(unit.position) &&
                (unit.position.x == occupied.left ||
                 unit.position.x == occupied.right ||
                 unit.position.y == occupied.top ||
                 unit.position.y == occupied.bottom);
            return occupied.contains(unit.position) && !touchingRecordedBody;
          });
          LocalRouteResult plan;
          if (flying) {
            plan.points = {unit.targetPosition};
            plan.source = unit.position;
            plan.destination = unit.targetPosition;
            plan.reached = true;
          } else {
            const auto &shape = definitions_->units[unit.type].bounds;
            auto cachedSpace = terrainGeometry(shape).cachedWithBodies(
                obstacles, motion.reroutesRecordedObstacle()
                               ? std::optional<Point>(unit.position)
                               : std::nullopt);
            const auto &space = *cachedSpace;
            std::optional<PixelBox> contact;
            std::optional<std::pair<int, int>> contactRank;
            auto encounter = spatial_.encounters(unit.position);
            int widest = 0;
            for (const auto &type : definitions_->units)
              widest = std::max(widest, type.bounds.left + type.bounds.right + 1);
            std::unordered_map<std::uint32_t, int> destinationOrder;
            const int range = widest / 2 + 1;
            const auto &boundaries = spatial_.axes()[0];
            for (int index = 0; index < int(boundaries.size()); ++index) {
              const auto &boundary = boundaries[index];
              if (boundary.coordinate >= destination.x - range &&
                  boundary.coordinate < destination.x + range)
                destinationOrder.try_emplace(boundary.key, index);
            }
            for (const auto &body : contactBodies) {
              if (body.flying || !body.collides)
                continue;
              if (get(unit.moveTargetUnit) &&
                  body.key != std::uint32_t(unit.moveTargetUnit.index))
                continue;
              if (!get(unit.moveTargetUnit) && !destinationOrder.contains(body.key))
                continue;
              PixelBox expanded{
                  body.position.x - body.bounds.left - shape.right,
                  body.position.y - body.bounds.top - shape.bottom,
                  body.position.x + body.bounds.right + shape.left,
                  body.position.y + body.bounds.bottom + shape.top};
              PixelBox perimeter{expanded.left - 1, expanded.top - 1,
                                 expanded.right + 1, expanded.bottom + 1};
              // A fresh point command includes the body's touching border.
              bool newPointContact =
                  !get(unit.moveTargetUnit) &&
                  samePoint(destination, unit.targetPosition) &&
                  perimeter.contains(destination);
              bool recoveringContact = (recovery || resumedOverlapOrder) &&
                                       perimeter.contains(destination);
              if (get(unit.moveTargetUnit) || expanded.contains(destination) ||
                  newPointContact || recoveringContact) {
                // Ordinary travel retains an accepted perimeter point.
                // Collision recovery resolves the contact again as bodies move.
                bool approach = recovery || resumedOverlapOrder ||
                                newPointContact ||
                                !perimeter.contains(destination) ||
                                (destination.x != perimeter.left &&
                                 destination.x != perimeter.right &&
                                 destination.y != perimeter.top &&
                                 destination.y != perimeter.bottom);
                Point gap{std::max({0, expanded.left - unit.position.x,
                                    unit.position.x - expanded.right}),
                          std::max({0, expanded.top - unit.position.y,
                                    unit.position.y - expanded.bottom})};
                std::pair rank{distance(gap), encounter.at(body.key)};
                if (!get(unit.moveTargetUnit))
                  rank = {destinationOrder.at(body.key), 0};
                if (!contactRank || rank < *contactRank) {
                  contact = approach ? std::optional(perimeter) : std::nullopt;
                  contactRank = rank;
                }
              }
            }
            auto adjusted =
                contact ? std::optional<Point>{}
                        : adjustGroundTarget(map_->terrain, map_->regions,
                                             shape, destination);
            try {
              plan = planGroundRoute(space, map_->regions, unit.position,
                                     destination, contact, adjusted, nullptr,
                                     continuation ? &*unit.path : nullptr);
            } catch (const std::exception &failure) {
              throw std::runtime_error(
                  "Unit " + std::to_string(unit.type) + " route from " +
                  std::to_string(unit.position.x) + "," +
                  std::to_string(unit.position.y) + " to " +
                  std::to_string(unit.targetPosition.x) + "," +
                  std::to_string(unit.targetPosition.y) + ": " +
                  failure.what());
            }
          }
          if (plan.points.empty()) {
            if (!motion.stopped())
              throw std::runtime_error(
                  "Failed route recovery while moving is not implemented yet");
            if (retryFailure) {
              unit.moveTargetPosition = unit.position;
              unit.immovable = true;
            } else {
              // Keep the requested destination during the failed-path pause.
              // Rebuild once after the observed failure/retry interval before
              // reporting an immovable destination to the order controller.
              unit.failedRouteFrames = 12;
              unit.immovable = false;
            }
          } else {
            unit.failedRouteFrames = 0;
            const bool failedMoving = recovery &&
                                      motion.reroutesMovingObstacles() &&
                                      plan.stuck;
            // A partial route is only the next leg of the trip. Preserve the
            // requested goal until the planner can actually reach its endpoint.
            if (plan.reached && !failedMoving) {
              unit.moveTargetPosition = plan.destination;
              // Only an adjusted endpoint creates a new projected goal.
              // A continuation to the existing request retains that request.
              if (get(unit.moveTargetUnit) &&
                  !samePoint(plan.destination, destination)) {
                const auto *target = get(unit.moveTargetUnit);
                if (target && target->motion && target->motion->moving()) {
                  auto targetHeading = target->motion->acceptedTravelHeading();
                  auto projected = motion.speed() > 0
                                       ? motion.projectFollowTarget(
                                             target->position, targetHeading)
                                       : destination;
                  if (projected.x >= 0 && projected.y >= 0 &&
                      projected.x < map_->terrain.width() * 32 &&
                      projected.y < map_->terrain.height() * 32)
                    unit.moveTargetPosition = projected;
                }
              }
              if (unit.overlapOrderChanged)
                unit.overlapReturnTarget = unit.moveTargetPosition;
            } else if (!plan.reached)
              plan.destination = destination;
            unit.immovable = plan.stuck;
            if (recovery)
              motion.replan(plan.points, plan.reached || plan.stuck,
                            failedMoving ? std::optional<Point>(destination)
                                         : std::nullopt);
            else {
              bool originOnly =
                  unit.path && unit.path->points.size() == 1 &&
                  samePoint(unit.path->source, unit.path->points.front()) &&
                  samePoint(unit.path->source, unit.position);
              const bool freshUserPath = unit.userMovementPending &&
                                         !unit.initialRoutePending &&
                                         (motion.stopped() || motion.waitingToStop());
              const int userDelay = freshUserPath && motion.scriptDriven()
                                        ? random_.scaled(0, 2) : -1;
              if (freshUserPath)
                unit.userMovementPending = false;
              motion.route(
                  plan.points,
                  userDelay >= 0 ? userDelay
                  : resumedOverlapOrder ? 1
                  : unit.planRequested && collisionRecheck &&
                            motion.braking() && !motion.brakingTravel() &&
                            !motion.moving()
                      ? std::max(1, unit.routePlanningFrames)
                  : unit.planRequested && !flying && !collisionRecheck &&
                            !motion.brakingTravel() &&
                            !expiredMovingContact &&
                            motion.brakesAtWaypoint() &&
                            (motion.needsArrivalUpdate() ||
                             (motion.arrived() && motion.moving()))
                      ? std::max(1, unit.routePlanningFrames)
                  : unit.planRequested && unit.gathering &&
                            unit.routePlanningFrames > 0 && motion.settling()
                      ? unit.routePlanningFrames
                  : flying || continuation || followContactContinuation ||
                          collisionRecheck ||
                          (unit.order == 166 && motion.hasRoute())
                      ? 0
                      : unit.routePlanningFrames,
                  unit.path && (unit.path->stuck || unit.path->points.empty() ||
                                originOnly),
                  plan.reached || plan.stuck, flying && unit.order == 166);
              if (initialCollision && overlapCleared)
                motion.resumeAfterLegalityCheck();
            }
          }
          recordPath(unit, plan);
          unit.path = std::move(plan);
          unit.interruptedMoveGoal.reset();
          unit.planRequested = false;
          unit.initialRoutePending = false;
        }
        motion.setMovementTarget(
            unit.escapingOverlap && !unit.routeContinuesAtWaypoint
                ? std::optional<Point>(unit.moveTargetPosition)
                : std::nullopt);
        if (unit.order == 10 &&
            (motion.braking() || motion.needsArrivalUpdate() ||
             motion.settling()) &&
            !(unit.orderState == 0 && motion.waitingToStop()) &&
            !(motion.stopped() && unit.orderState == 0))
          motion.faceOnArrival(unit.targetPosition);
        bool planning = motion.planning();
        auto previousRaw = motion.rawPosition();
        bool arrivedLogicalGoal =
            unit.escapingOverlap && unit.overlapOrderChanged &&
            samePoint(unit.position, unit.moveTargetPosition);
        int previousFacing = motion.facing();
        int previousNextSpeed = motion.nextSpeed();
        // Keep the pre-frame physics for a clear moving-obstacle retry, so
        // accepting it advances the route age and collision debt only once.
        std::optional<Motion> pausedRetry;
        bool resumedMovingPause = false;
        if (motion.movingCollisionRetryReady() || motion.preparedBodyRetryReady() ||
            motion.overlapBodyRetryReady())
          pausedRetry = motion;
        if (motion.staticCollisionWaitReady()) {
          const auto *obstacle = get(unit.collisionObstacle);
          if (obstacle && obstacle->motion && obstacle->motion->moving())
            motion.awaitMovingObstacle();
        }
        if (arrivalUpdated || (!motion.braking() &&
            ((deferredPlan && !motion.waitingForBody()) ||
             ((initialCollision || prepareClearedRoute) && overlapCleared) ||
             (collisionRecheck && !overlapCleared &&
              (!settleCollision || unit.type != 35)) ||
             waitingCollision || deferOverlap)))
          motion.prepareRouteFrame();
        else
          motion.advance();
        if (unit.path && unit.pathSlot >= 0) {
          const auto &record = pathPool_.record(unit.pathSlot);
          if (!record.singleStep)
            motion.setRouteAge(std::max(0, frame_ - record.created));
        }
        if (arrivedLogicalGoal)
          motion.clipStep(previousRaw);
        if (definitions_->units[unit.type].flags & 4) {
          auto push =
              unit.air.cell >= 0
                  ? air_.push(unit.air, unit.position, unit.moveTargetPosition,
                              definitions_->units[unit.type].bounds)
                  : std::optional<Point>{};
          if (push)
            motion.applyAirDisplacement(*push);
          auto raw = motion.rawPosition();
          const auto &bounds = definitions_->units[unit.type].bounds;
          Point position{
              std::clamp(raw.x / 256, bounds.left,
                         map_->terrain.width() * 32 - bounds.right - 1),
              std::clamp(raw.y / 256, bounds.top,
                         map_->terrain.height() * 32 - bounds.bottom - 1)};
          motion.setAirPixelPosition(position);
          if (unit.air.cell >= 0)
            air_.move(unit.air, position, random_);
          if (push &&
              std::max(std::abs(unit.moveTargetPosition.x - position.x),
                       std::abs(unit.moveTargetPosition.y - position.y)) < 24) {
            unit.moveTargetPosition = position;
            motion.acceptNearbyAirGoal();
          }
        }
        if (pausedRetry && motion.scriptDriven() &&
            motion.reroutesMovingObstacles()) {
          const auto *obstacle = get(unit.collisionObstacle);
          if (obstacle && obstacle->visible && obstacle->motion) {
            int axis = ((obstacle->motion->collisionStepHeading() + 32) & 255) /
                       64 * 64;
            int toward = heading({obstacle->position.x - unit.position.x,
                                  obstacle->position.y - unit.position.y});
            // Facing can change while preparation is pending. Select the
            // body wait from the obstacle's direction when the delay ends.
            if (std::abs(signedHeading(axis - toward)) <= 64)
              motion.acceptDeferredBodyWait();
          }
        }
        if (((motion.moving() &&
              !samePoint(previousRaw, motion.rawPosition())) ||
             motion.waitingForBody() || motion.movingCollisionPaused() ||
             (pausedRetry && (pausedRetry->preparedBodyRetryReady() ||
                              pausedRetry->overlapBodyRetryReady()))) &&
            !unit.escapingOverlap &&
            !unit.gasLanding &&
            !(definitions_->units[unit.type].flags & 4)) {
          auto shape = definitions_->units[unit.type].bounds;
          auto fits = [&](Point p, int *collider = nullptr,
                          std::optional<Point> from = {}, bool terrain = true) {
            auto origin = from.value_or(unit.position);
            if (collider)
              *collider = -1;
            if (p.x < shape.left || p.y < shape.top ||
                p.x + shape.right >= map_->terrain.width() * 32 ||
                p.y + shape.bottom >= map_->terrain.height() * 32 - 32 ||
                (terrain && !map_->contours.permits(p, shape)) ||
                (terrain && !map_->terrain.walkable(p.x / 8, p.y / 8) &&
                 !creep_
                      .cells()[(p.y / 32) * map_->terrain.width() + p.x / 32]))
              return false;
            using Hit = std::tuple<int, int, int, int>;
            std::optional<Hit> firstHit;
            std::array<int, 2> nextLow{p.x - shape.left, p.y - shape.top},
                nextHigh{p.x + shape.right, p.y + shape.bottom},
                delta{p.x - origin.x, p.y - origin.y},
                oldLow{origin.x - shape.left, origin.y - shape.top},
                oldHigh{origin.x + shape.right, origin.y + shape.bottom};
            for (int otherId : visible_) {
              if (otherId == id)
                continue;
              const auto &other = *units_[otherId];
              const auto &type = definitions_->units[other.type];
              if (((unit.gathering || unit.type == 35) && !type.building()) ||
                  other.type == 35 || (type.flags & 4))
                continue;
              std::array<int, 2> low{other.position.x - type.bounds.left,
                                     other.position.y - type.bounds.top},
                  high{other.position.x + type.bounds.right,
                       other.position.y + type.bounds.bottom};
              if (!collider) {
                if (nextLow[0] <= high[0] && nextHigh[0] >= low[0] &&
                    nextLow[1] <= high[1] && nextHigh[1] >= low[1])
                  return false;
                continue;
              }
              // Rank crossed footprint boundaries, with X before Y. This
              // distinguishes simultaneous corner contacts without relying on
              // the world's allocation/list order.
              for (int axis = 0; axis < 2; ++axis) {
                if (!delta[axis])
                  continue;
                int cross = 1 - axis;
                if (low[cross] > nextHigh[cross] ||
                    high[cross] < nextLow[cross])
                  continue;
                int sign = delta[axis] > 0 ? 1 : -1;
                int leading = sign > 0 ? oldHigh[axis] : oldLow[axis];
                for (bool maximum : {false, true}) {
                  int edge = maximum ? high[axis] : low[axis];
                  int away = sign * (edge - leading);
                  if (away < 0 || away > std::abs(delta[axis]))
                    continue;
                  // Equal edges retain their spatial ordering as bodies move.
                  // Allocation age alone cannot distinguish a moving body
                  // joining an already occupied boundary.
                  const auto &events = spatial_.axes()[axis];
                  auto entry = std::find_if(
                      events.begin(), events.end(), [&](const auto &event) {
                        return event.key == otherId && event.maximum == maximum;
                      });
                  if (entry == events.end())
                    throw std::logic_error("Missing collision boundary");
                  int sequence = int(entry - events.begin()) * sign;
                  Hit hit{axis, away, sequence, otherId};
                  if (!firstHit || hit < *firstHit)
                    firstHit = hit;
                }
              }
            }
            if (firstHit) {
              *collider = std::get<3>(*firstHit);
              return false;
            }
            return true;
          };
          auto obstacleClearing = [&] {
            const auto *obstacle = get(unit.collisionObstacle);
            bool clearing = !obstacle || !obstacle->visible;
            if (obstacle && obstacle->motion && obstacle->motion->moving()) {
              int axis = ((obstacle->motion->acceptedTravelHeading() + 32) & 255) /
                         64 * 64;
              int toward = heading({obstacle->position.x - unit.position.x,
                                    obstacle->position.y - unit.position.y});
              clearing = std::abs(signedHeading(axis - toward)) <= 64;
            }
            return clearing;
          };
          if (pausedRetry && motion.movingCollisionPaused()) {
            if (obstacleClearing()) {
              pausedRetry->retryCollision();
              pausedRetry->advance();
              int retryCollider = -1;
              if (fits(pausedRetry->position(), &retryCollider)) {
                motion = std::move(*pausedRetry);
                resumedMovingPause = true;
              }
            }
          }
          if (pausedRetry && (pausedRetry->preparedBodyRetryReady() ||
                              pausedRetry->overlapBodyRetryReady())) {
            // Overlap preparation can become clearance waiting when its body
            // heads away. Other recovery decisions retain their route delay.
            if (pausedRetry->preparedBodyRetryReady() || obstacleClearing()) {
              auto trial = *pausedRetry;
              trial.retryPreparedBodyWait();
              trial.advance();
              int retryCollider = -1;
              if (fits(trial.position(), &retryCollider)) {
                motion = std::move(trial);
                resumedMovingPause = true;
              } else {
                motion.holdPreparedBodyRetry(trial);
              }
            }
          } else if (motion.waitingForBody() && motion.speed() == 0) {
            auto trial = motion;
            trial.resumeBodyWait();
            trial.advance();
            // A moving body can leave an overlap behind. Retry against crossed
            // boundaries, just as for ordinary movement, rather than requiring
            // the entire proposed footprint to be empty.
            int retryCollider = -1;
            if (fits(trial.position(), &retryCollider)) {
              resumedMovingPause =
                  motion.clearanceBodyWait() || motion.scriptDriven();
              motion = std::move(trial);
            } else {
              motion.acceptBlockedWaitTurn(trial);
              if (motion.collisionDebt() >= 25)
                motion.expireBodyWait(random_.next() >= 16383);
            }
          }
          if (!motion.waitingForBody()) {
            if (motion.slidingStep()) {
              auto raw = motion.rawPosition();
              int remaining = std::max(0, motion.speed() - 256);
              bool resume = false;
              int tangentCollider = -1;
              bool tangentBlocked =
                  !fits({raw.x / 256, raw.y / 256}, &tangentCollider);
              if (tangentBlocked)
                raw = previousRaw;
              while (!tangentBlocked) {
                Point p{raw.x / 256, raw.y / 256};
                auto goal = motion.waypoint();
                int direction = turnToward(
                    motion.travelHeading(),
                    heading({goal.x - p.x, goal.y - p.y}),
                    std::max(1, motion.turnRate() /
                                    (motion.scriptDriven() ? 1 : 2)));
                auto prediction =
                    motion.waypointStep(raw, direction, motion.speed());
                Point trial{prediction.x / 256, prediction.y / 256};
                int nextBody = -1;
                if (fits(trial, &nextBody, p)) {
                  resume = true;
                  break;
                }
                // A new obstacle ends this tangent increment. Keep sliding
                // against the original body on the following frame.
                if (nextBody >= 0 && nextBody != unit.slideObstacle.index)
                  break;
                if (!remaining)
                  break;
                int step = std::min(remaining, 256);
                Point next = motion.slideStep(raw, step);
                int stepCollider = -1;
                if (!fits({next.x / 256, next.y / 256}, &stepCollider, p)) {
                  tangentBlocked = true;
                  break;
                }
                raw = next;
                remaining -= step;
              }
              if (tangentBlocked)
                motion.blockedSlide(raw);
              else
                motion.finishSlide(raw, resume);
            }
            int collider = -1;
            // A building can overlap a terrain contour. Preserve its collision
            // identity before selecting terrain recovery for the rejected step.
            fits(motion.position(), &collider, {}, false);
            bool bodyBlocked = collider >= 0;
            if (!bodyBlocked && !map_->contours.permitsMovement(
                                    unit.position, motion.position(), shape)) {
              if (motion.terrainEscapeStep())
                motion.blockedTerrainEscape(previousRaw);
              else {
                bool previewAccepted = false;
                if (!motion.terrainRetryStep()) {
                  motion.terrainBlocked(previousRaw, previousNextSpeed);
                  if (!motion.scriptDriven() &&
                      motion.speed() < motion.nextSpeed()) {
                    auto v = motion.velocity();
                    Point retry{previousRaw.x + v.x, previousRaw.y + v.y};
                    Point pixel{retry.x / 256, retry.y / 256};
                    if (map_->contours.permitsMovement(unit.position, pixel,
                                                       shape) &&
                        fits(pixel)) {
                      motion.acceptTerrainRetry(retry);
                      previewAccepted = true;
                    }
                  }
                }
                // A direction accepted before contact can select a tangent now.
                // A turn accepted on this contact frame first waits for a
                // retry.
                if (!previewAccepted && motion.terrainRetryStep() &&
                    motion.terrainRetryChanged())
                  motion.holdTerrainRetry(previousRaw);
                else if (!previewAccepted && !motion.terrainRetryChanged()) {
                  auto raw = previousRaw, v = motion.terrainProbeVelocity();
                  Point horizontal{(raw.x + v.x) / 256, raw.y / 256};
                  Point vertical{raw.x / 256, (raw.y + v.y) / 256};
                  bool xClear = v.x && map_->contours.permitsMovement(
                                           unit.position, horizontal, shape);
                  bool yClear = v.y && map_->contours.permitsMovement(
                                           unit.position, vertical, shape);
                  Point tangent = xClear ? Point{v.x < 0 ? -1 : 1, 0}
                                         : Point{0, v.y < 0 ? -1 : 1};
                  if (!xClear && !yClear) {
                    std::optional<Point> escape;
                    int axis = ((motion.travelHeading() + 32) & 255) / 64 * 64;
                    for (int turn : {64, -64}) {
                      auto vector = directionVector(axis + turn);
                      Point candidate{
                          raw.x + fixedProduct(motion.terrainProbeSpeed(), vector.x),
                          raw.y + fixedProduct(motion.terrainProbeSpeed(), vector.y)};
                      if (map_->contours.permitsMovement(
                              unit.position,
                              {candidate.x / 256, candidate.y / 256}, shape)) {
                        escape = Point{vector.x / 256, vector.y / 256};
                        break;
                      }
                    }
                    if (!escape)
                      throw std::runtime_error(
                          "Enclosed terrain recovery is not implemented yet");
                    tangent = *escape;
                  }
                  motion.rejectTerrainRetry(previousRaw, heading(tangent),
                                            !xClear && !yClear);
                }
              }
            } else if (bodyBlocked || !fits(motion.position(), &collider)) {
              if (collider >= 0) {
                auto &other = *units_[collider];
                if (other.motion && other.motion->waitingForBody() &&
                    !other.collisionPending) {
                  const auto &bounds = definitions_->units[other.type].bounds;
                  if (unit.position.x + shape.right >=
                          other.position.x - bounds.left &&
                      unit.position.x - shape.left <=
                          other.position.x + bounds.right &&
                      unit.position.y + shape.bottom >=
                          other.position.y - bounds.top &&
                      unit.position.y - shape.top <=
                          other.position.y + bounds.bottom) {
                    other.collisionPending = true;
                    other.collisionWait = 1;
                  }
                }
              }
              auto intended = motion.rawPosition();
              bool clipped = false;
              if (bodyBlocked && motion.braking()) {
                motion.brakeBlocked(previousRaw);
                if (unit.order == 10)
                  unit.followTimer = 15;
                unit.moveTargetPosition = unit.position;
                unit.immovable = false;
                clipped = true;
              }
              auto velocity = motion.velocity();
              bool canReduce =
                  !clipped && !motion.shortenedStep() &&
                  map_->contours.permitsMovement(unit.position,
                                                 motion.position(), shape) &&
                  (collider < 0 || std::max(velocity.x, velocity.y) >= 32);
              for (int divisor : {2, 4}) {
                if (!canReduce)
                  break;
                Point raw{
                    previousRaw.x + (intended.x - previousRaw.x) / divisor,
                    previousRaw.y + (intended.y - previousRaw.y) / divisor};
                int reducedBody = -1;
                if (fits({raw.x / 256, raw.y / 256}, &reducedBody)) {
                  motion.clipStep(raw);
                  clipped = true;
                  break;
                }
              }
              if (!clipped && collider >= 0 && get(unit.moveTargetUnit)) {
                const auto &other = *get(unit.moveTargetUnit);
                auto bounds = definitions_->units[other.type].bounds;
                PixelBox contact{
                    other.position.x - bounds.left - shape.right - 1,
                    other.position.y - bounds.top - shape.bottom - 1,
                    other.position.x + bounds.right + shape.left + 1,
                    other.position.y + bounds.bottom + shape.top + 1};
                auto p = unit.position;
                if (contact.contains(p) &&
                    (p.x == contact.left || p.x == contact.right ||
                     p.y == contact.top || p.y == contact.bottom)) {
                  motion.contactBlocked(previousRaw, previousFacing,
                                        motion.waypoint());
                  unit.moveTargetPosition = p;
                  unit.immovable = false;
                  clipped = true;
                }
              }
              if (!clipped) {
                std::optional<Point> slide;
                bool blockedWaypoint = false;
                bool rerouteMoving = false;
                bool movingObstacle = false;
                bool occupiedFinalGoal = false;
                bool waitForTurningBody = false;
                if (collider >= 0) {
                  const auto &other = *units_[collider];
                  auto bounds = definitions_->units[other.type].bounds;
                  PixelBox box{other.position.x - bounds.left - shape.right,
                               other.position.y - bounds.top - shape.bottom,
                               other.position.x + bounds.right + shape.left,
                               other.position.y + bounds.bottom + shape.top};
                  auto p = unit.position, goal = motion.waypoint();
                  waitForTurningBody =
                      other.motion && other.motion->turningForRoute() &&
                      (!motion.scriptDriven() || box.contains(goal));
                  Point displacement{
                      other.moveTargetPosition.x - other.position.x,
                      other.moveTargetPosition.y - other.position.y};
                  // Escape travel can remain active at its pixel destination.
                  movingObstacle = other.motion &&
                                   (other.motion->moving() ||
                                    other.motion->replanning() ||
                                    other.motion->turningForRoute()) &&
                                   (displacement.x || displacement.y ||
                                    other.escapingOverlap);
                  occupiedFinalGoal =
                      unit.order == 6 && !get(unit.moveTargetUnit) &&
                      !movingObstacle && box.contains(unit.moveTargetPosition);
                  PixelBox intendedBox{
                      box.left + displacement.x, box.top + displacement.y,
                      box.right + displacement.x, box.bottom + displacement.y};
                  // A unit-bound destination must remain reachable even when
                  // its adjusted endpoint is occupied by another body. Point
                  // destinations keep their separate blocked-goal handling.
                  blockedWaypoint = box.contains(goal) &&
                                    intendedBox.contains(goal) &&
                                    (!box.contains(unit.moveTargetPosition) ||
                                     get(unit.moveTargetUnit));
                  Point obstructedGoal = box.contains(unit.moveTargetPosition)
                                             ? unit.moveTargetPosition
                                             : goal;
                  const int obstacleHeading =
                      other.motion ? other.motion->collisionStepHeading() : 0;
                  // Waiting and stationary turning retain a movable body's
                  // route. Its direction still decides whether to plan around it.
                  if (other.motion &&
                      (movingObstacle || other.motion->collisionPaused()) &&
                      box.contains(obstructedGoal) &&
                      !intendedBox.contains(obstructedGoal)) {
                    int axis =
                        ((obstacleHeading + 32) & 255) / 64 * 64;
                    int towardObstacle = heading(
                        {other.position.x - p.x, other.position.y - p.y});
                    rerouteMoving =
                        std::abs(signedHeading(axis - towardObstacle)) > 64;
                  }
                  // Existing overlaps can clear through ordinary travel. The
                  // obstacle's direction decides whether a fresh route helps.
                  bool recoveringBody =
                      other.escapingOverlap || other.collisionPending ||
                      (other.motion && (other.motion->replanning() ||
                                        other.motion->collisionPaused()));
                  if (movingObstacle && recoveringBody && p.x > box.left &&
                      p.x < box.right && p.y > box.top && p.y < box.bottom) {
                    int axis =
                        ((obstacleHeading + 32) & 255) / 64 * 64;
                    int towardObstacle = heading(
                        {other.position.x - p.x, other.position.y - p.y});
                    rerouteMoving =
                        std::abs(signedHeading(axis - towardObstacle)) > 64;
                  }
                  // A body preparing its own moving-obstacle reroute has no
                  // accepted clearing step yet. Plan around an opposing body
                  // rather than treating that preparation as clear travel.
                  if (movingObstacle && other.motion->movingCollisionPaused()) {
                    const int axis =
                        ((obstacleHeading + 32) & 255) / 64 * 64;
                    const int towardObstacle = heading(
                        {other.position.x - p.x, other.position.y - p.y});
                    rerouteMoving =
                        std::abs(signedHeading(axis - towardObstacle)) > 64;
                  }
                  int inward = p.x > box.right     ? 3
                               : p.x < box.left    ? 1
                               : p.y > box.bottom  ? 0
                               : p.y < box.top     ? 2
                               : p.x == box.right  ? 3
                               : p.x == box.left   ? 1
                               : p.y == box.bottom ? 0
                               : p.y == box.top    ? 2
                                                   : -1;
                  int goalHeading = heading({goal.x - p.x, goal.y - p.y});
                  int quadrant = (goalHeading & 255) / 64;
                  if (motion.awaitingFailedDeparture()) {
                    blockedWaypoint = false;
                    rerouteMoving = false;
                  }
                  // A body turning onto a replacement route occupies its
                  // current footprint until the turn finishes. Its pending
                  // movement flag does not prevent travel along that edge.
                  bool turningObstacle =
                      other.motion && other.motion->scriptDriven() &&
                      other.motion->turningForRoute();
                  if (!blockedWaypoint && !rerouteMoving &&
                      (!movingObstacle || turningObstacle) &&
                      inward >= 0 &&
                      std::abs(signedHeading(goalHeading -
                                             motion.travelHeading())) < 80) {
                    int tangent = quadrant == inward ? (inward + 1) % 4
                                  : quadrant == (inward + 3) % 4
                                      ? (inward + 3) % 4
                                      : -1;
                    if (tangent >= 0) {
                      auto vector = directionVector(tangent * 64);
                      for (int sign : {1, -1}) {
                        Point direction{sign * vector.x / 256,
                                        sign * vector.y / 256};
                        Point corner = p;
                        if (inward == 1)
                          corner.x = box.left - 1;
                        if (inward == 3)
                          corner.x = box.right + 1;
                        if (inward == 0)
                          corner.y = box.bottom + 1;
                        if (inward == 2)
                          corner.y = box.top - 1;
                        if (direction.x)
                          corner.x =
                              direction.x < 0 ? box.left - 1 : box.right + 1;
                        if (direction.y)
                          corner.y =
                              direction.y < 0 ? box.top - 1 : box.bottom + 1;
                        if (fits(corner)) {
                          slide = direction;
                          break;
                        }
                      }
                    }
                  }
                  int slideCollider = -1;
                  if (slide && !fits({p.x + slide->x, p.y + slide->y},
                                     &slideCollider, p))
                    slide.reset();
                }
                if (occupiedFinalGoal) {
                  motion.finishBlockedPoint(previousRaw, previousFacing);
                  unit.moveTargetPosition = unit.position;
                  unit.immovable = false;
                  unit.userMovementPending = false;
                } else if (blockedWaypoint || rerouteMoving) {
                  unit.collisionObstacle =
                      collider >= 0 ? units_[collider]->handle : UnitHandle{};
                  motion.blockedWaypoint(previousRaw, previousFacing,
                                         previousNextSpeed, rerouteMoving);
                } else if (slide) {
                  unit.collisionObstacle = unit.slideObstacle = units_[collider]->handle;
                  motion.slideBlocked(previousRaw, previousFacing,
                                      heading(*slide), previousNextSpeed);
                } else if (collider < 0 && motion.scriptDriven())
                  motion.terrainBlocked(previousRaw, previousNextSpeed);
                else if (collider >= 0 && units_[collider]->motion &&
                         (units_[collider]->motion->moving() ||
                          units_[collider]->motion->replanning() ||
                          waitForTurningBody)) {
                  unit.collisionObstacle = units_[collider]->handle;
                  if (motion.awaitingFailedDeparture())
                    motion.waitForFailedDeparture(previousRaw, previousFacing,
                                                previousNextSpeed);
                  else if (units_[collider]->motion->replanning())
                    motion.prepareBodyWait(previousRaw, previousFacing,
                                           previousNextSpeed);
                  else if (!canReduce && !motion.scriptDriven())
                    motion.waitForClearance(previousRaw, previousFacing,
                                            previousNextSpeed);
                  else
                    motion.waitForBody(previousRaw, previousFacing,
                                       previousNextSpeed,
                                       units_[collider]->escapingOverlap);
                  if (unit.order == 6) {
                    unit.overlapReturnTarget = unit.moveTargetPosition;
                    unit.overlapOrderChanged = unit.overlapFreshOrder = true;
                  }
                } else {
                  if (motion.scriptDriven() && collider >= 0)
                    unit.collisionObstacle = units_[collider]->handle;
                  motion.blocked(previousRaw, previousFacing,
                                 previousNextSpeed);
                }
              }
            }
          }
        }
        if (motion.terrainSlidingStep()) {
          auto raw = motion.rawPosition(), v = motion.terrainProbeVelocity();
          const Point probe = motion.scriptDriven()
                                  ? motion.waypointStep(raw, motion.travelHeading(),
                                                        motion.terrainProbeSpeed())
                                  : Point{raw.x + v.x, raw.y + v.y};
          Point from = motion.position(),
                to{probe.x / 256, probe.y / 256};
          const auto &shape = definitions_->units[unit.type].bounds;
          if (!motion.advancesTerrainCorner() &&
              !map_->contours.permitsMovement(from, to, shape))
            motion.continueTerrainSlide();
        }
        if (!unit.gasLanding && !(definitions_->units[unit.type].flags & 4) &&
            !motion.slidingStep() && !motion.terrainSlidingStep() &&
            !motion.terrainRetryStep() && !resumedMovingPause &&
            !samePoint(unit.position, motion.position()))
          motion.acceptedGroundStep();
        if (!samePoint(previousRaw, motion.rawPosition()))
          motion.acceptMovement();
        unit.position = motion.position();
        if (refreshVision || previousRaw.x / 8192 != unit.position.x / 32 ||
            previousRaw.y / 8192 != unit.position.y / 32)
          revealSight(unit);
        // A Follow braking request drops the route before its next movement
        // update releases the leased path, even while residual travel remains.
        if (!unit.path && motion.braking())
          releasePathSlot(unit);
        if (unit.overlapStopPending) {
          releasePathSlot(unit);
          unit.overlapStopPending = false;
          unit.overlapStopRecovery = true;
          unit.escapingOverlap = false;
          unit.collisionPending = true;
          unit.collisionWait = 1;
        }
        // Completed point moves release their active path while the final
        // stop animation still retains velocity for this frame. Partial
        // regional legs retain their continuation history.
        if (unit.pathSlot >= 0 && motion.settling() && !motion.moving() &&
            samePoint(unit.position, unit.moveTargetPosition))
          releasePathSlot(unit);
        if (unit.order == 6 && unit.path && unit.path->reached &&
            motion.settling() && !motion.moving() &&
            samePoint(unit.position, unit.moveTargetPosition))
          discardPath(unit);
        moveSpatial(unit);
        spriteRows_.move(unit.sprite.sceneKey, unit.position);
        if (!planning &&
            (motion.updatesHeading() || motion.terrainSlidingStep())) {
          if (unit.sprite.context.heading != motion.facing())
            unit.sprite.setHeading(motion.facing(), &resources);
          else
            unit.sprite.refreshOffsets(resources);
        }
        if (motion.scriptDriven())
          unit.sprite.context.speed = motion.speed();
        auto host = [&](const auto &i, const auto &layer) {
          return effect(unit.handle, i, layer);
        };
        if (!unit.walking && motion.moving() && !arrivedLogicalGoal &&
            (motion.updatesHeading() || motion.terrainSlidingStep()) &&
            (motion.scriptDriven() || motion.speed())) {
          unit.sprite.animate(11, resources, random_, host);
          unit.walking = true;
        } else if (unit.walking &&
                   (!motion.moving() || motion.settling() ||
                    arrivedLogicalGoal) &&
                   !motion.retainsBodyRestartWalk() &&
                   !(motion.waitingForBody() &&
                     (motion.collisionDebt() <= 2 ||
                      (motion.updatesHeading() && !motion.turningBodyWait())))) {
          if (!unit.sprite.context.uninterruptible)
            unit.sprite.animate(12, resources, random_, host);
          unit.walking = false;
        }
        if (motion.slidingStep())
          unit.sprite.refreshOffsets(resources);
      }
    for (auto at = visible_.begin(); at != visible_.end();) {
      auto &unit = *units_[*at++];
      const bool motionAtOrderStart = bool(unit.motion);
      advanceVitals(unit);
      if (unit.order == 0) {
        processDeath(unit);
        continue;
      }
      if (unit.removeTimer > 0 && --unit.removeTimer == 0) {
        if (!unit.hallucinated)
          throw std::runtime_error("Normal unit death is not implemented yet");
        createEffect(374, unit.position, 0,
                     definitions_->units[unit.type].elevation + 1);
        remove(unit.handle);
        continue;
      }
      if (unit.mainTimer > 0)
        --unit.mainTimer;
      if (unit.followTimer > 0)
        --unit.followTimer;
      if (unit.order == 166) {
        if (unit.orderState == 0) {
          bool flying = unit.type == 94;
          if (!unit.motion || (!flying && !unit.motion->scriptDriven()))
            throw std::runtime_error("Unsupported critter movement physics");
          auto vector = directionVector(random_.scaled(0, 255));
          int travel = flying ? random_.scaled(160, 640) : 32;
          const auto &bounds = definitions_->units[unit.type].bounds;
          Point destination{
              std::clamp(unit.position.x + fixedProduct(vector.x, travel),
                         bounds.left,
                         map_->terrain.width() * 32 - bounds.right - 1),
              std::clamp(unit.position.y + fixedProduct(vector.y, travel),
                         bounds.top,
                         map_->terrain.height() * 32 - bounds.bottom - 33)};
          unit.targetPosition = destination;
          unit.moveTargetPosition = destination;
          unit.planRequested = true;
          unit.immovable = false;
          unit.orderState = 1;
        } else if (unit.orderState == 1 &&
                   samePoint(unit.position, unit.moveTargetPosition)) {
          if (unit.type == 94)
            unit.orderState = 0;
          else if (unit.immovable)
            unit.mainTimer = 75;
          else {
            unit.mainTimer = random_.scaled(0, 75);
            if (unit.mainTimer <= 15)
              unit.mainTimer = 0;
          }
          if (unit.type != 94)
            unit.orderState = 2;
        } else if (unit.orderState == 2 && !unit.mainTimer) {
          unit.orderState = 0;
        }
      }
      if (unit.order == 151)
        processHarvest(unit);
      if (unit.order == 41 || unit.order == 42)
        processMorph(unit);
      if (unit.order == 25 || unit.order == 30 || unit.order == 31 ||
          unit.order == 26 || unit.order == 45 || unit.order == 150)
        processConstruction(unit);
      if (unit.order == 48 || unit.order == 164)
        processWarp(unit);
      activateQueuedCommand(unit);
      if (unit.orderTimer > 0)
        --unit.orderTimer;
      else {
        unit.orderTimer = 8;
        switch (unit.order) {
        case 18:
        case 19:
          processTower(unit);
          break;
        case 70:
          processConstruction(unit);
          break;
        case 77:
          processLarva(unit);
          break;
        case 93:
          if (get(unit.target))
            throw std::runtime_error("Transport pickup is not implemented yet");
          break;
        case 101:
          if (unit.type != 131 && unit.type != 132 && unit.type != 133 &&
              unit.type != 143 && unit.type != 144 && unit.type != 146)
            throw std::runtime_error("Unsupported periodic creep provider");
          unit.creepGrowth = true;
          unit.spawnLarvae = unit.type >= 131 && unit.type <= 133;
          if (unit.spawnLarvae)
            unit.secondaryOrder = 102;
          unit.order = 23;
          unit.orderState = unit.orderTimer = unit.mainTimer = 0;
          break;
        case 79:
        case 81:
        case 84:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
        case 90:
          processHarvest(unit);
          break;
        case 2:
          unit.mainTimer = random_.scaled(0, 15);
          unit.order = 3;
          break;
        case 3:
          if (processGuardRetaliation(unit))
            break;
          if (!unit.mainTimer) {
            unit.mainTimer = 15;
            auto acquired = acquireTarget(unit.handle);
            if (acquired.index >= 0) {
              autoAttack(unit.handle, acquired);
              unit.mainTimer = 15;
            }
          }
          break;
        case 133:
        case 14:
        case 6:
          if (unit.orderState == 0) {
            if (!unit.motion)
              throw std::runtime_error("Move order has no planned route");
            unit.motion->faceOnArrival(std::nullopt);
            if (!samePoint(unit.moveTargetPosition, unit.targetPosition))
              unit.motion->keepMomentumForQueuedMove(
                  unit.queuedCommands.empty()
                      ? std::nullopt
                      : std::optional<Point>(unit.targetPosition));
            if (unit.escapingOverlap) {
              bool changedDestination =
                  !samePoint(unit.moveTargetPosition, unit.targetPosition);
              unit.moveTargetPosition = unit.targetPosition;
              unit.moveTargetUnit = {};
              unit.overlapReturnTarget = unit.targetPosition;
              unit.overlapOrderChanged = true;
              unit.overlapFreshOrder = false;
              if (changedDestination)
                unit.motion->refreshRouteHeading();
              unit.path.reset();
              unit.planRequested = false;
            } else if (unit.pendingRoute.empty()) {
              if (!samePoint(unit.moveTargetPosition, unit.targetPosition) ||
                  !unit.motion->hasRoute()) {
                unit.moveTargetPosition = unit.targetPosition;
                unit.moveTargetUnit = {};
                unit.planRequested = true;
                unit.immovable = false;
                unit.initialRoutePending = unit.movementPreparation > 0;
                unit.routePlanningFrames =
                    unit.order == 133 ? (unit.motion->moving() ? 2 : 0)
                                      : unit.movementPreparation ? 1 : 0;
              }
            } else
              unit.motion->route(std::move(unit.pendingRoute), 1);
            if (unit.order == 133)
              unit.followTimer = 15;
            unit.orderState = 1;
          } else if (unit.order == 133) {
            const auto point = unit.targetPosition;
            if (!(visibleAt({point.x / 32, point.y / 32}) & (1U << unit.owner)))
              break;
            auto match = rememberedTarget(unit.owner, point, unit.rememberedType);
            if (const auto *target = get(match)) {
              if (target->type >= 176 && target->type <= 178)
                harvest(unit.handle, match, HarvestOrder::Minerals);
              else if (target->type == 188)
                harvest(unit.handle, match, HarvestOrder::Gas);
              else if (target->owner == unit.owner)
                follow(unit.handle, match);
              else
                throw std::runtime_error("Remembered enemy target action is not implemented");
            } else
              move(unit.handle, point);
            unit.rememberedType = 228;
          } else if (unit.order != 14 && samePoint(unit.position, unit.moveTargetPosition)) {
            unit.order = definitions_->units[unit.type].returnIdle;
            // An active escape still needs the accepted command destination
            // to clamp motion there until its overlap recovery finishes.
            if (!unit.escapingOverlap) {
              unit.overlapOrderChanged = false;
              unit.overlapFreshOrder = false;
            }
            unit.orderState = 0;
            unit.orderTimer = 0;
            unit.mainTimer = 0;
            unit.target = {};
            unit.targetPosition = {};
          }
          break;
        case 10:
          processAttack(unit);
          break;
        case 49: {
          const auto *target = get(unit.target);
          if (!target || target->dead)
            throw std::runtime_error(
                "Native follow target loss is not implemented");
          if (!unit.motion)
            throw std::runtime_error("Native follow requires mobile unit");
          auto requestFollow = [&] {
            unit.moveTargetPosition = target->position;
            unit.moveTargetUnit = target->handle;
            unit.followPosition = target->position;
            unit.followTimer = 15;
            if (unit.escapingOverlap) {
              unit.overlapReturnTarget = target->position;
              unit.overlapOrderChanged = true;
              unit.overlapFreshOrder = false;
              unit.motion->refreshRouteHeading();
              unit.path.reset();
              unit.planRequested = false;
            } else {
              unit.planRequested = true;
              unit.initialRoutePending = unit.movementPreparation > 0;
              unit.routePlanningFrames = unit.movementPreparation ? 1 : 0;
            }
          };
          const auto &shape = definitions_->units[unit.type].bounds;
          const auto &other = definitions_->units[target->type].bounds;
          auto retainedFollowBoundary = [&] {
            return onTargetPerimeter(unit, *target);
          };
          bool initial = unit.orderState == 0;
          if (initial) {
            if (unit.moveTargetUnit != target->handle ||
                (!samePoint(unit.moveTargetPosition, target->position) &&
                 !retainedFollowBoundary()))
              requestFollow();
            unit.orderState = 1;
          }
          if (unit.mainTimer)
            break;
          Point gap{std::max({0,
                              target->position.x - other.left -
                                  unit.position.x - shape.right,
                              unit.position.x - shape.left -
                                  target->position.x - other.right}),
                    std::max({0,
                              target->position.y - other.top - unit.position.y -
                                  shape.bottom,
                              unit.position.y - shape.top - target->position.y -
                                  other.bottom})};
          bool differentDestination =
              !samePoint(unit.moveTargetPosition, target->position);
          // The periodic Follow check retains the perimeter of this observed
          // asymmetric box. Both leading extents expand its right edge; its
          // bottom remains the target's bottom. This differs from route contact
          // geometry, so a valid route endpoint can still require a refresh.
          bool retainedBoundary = retainedFollowBoundary();
          const auto &targetType = definitions_->units[target->type];
          if (distance(gap) <= 96 &&
              ((targetType.building() && !(targetType.flags & 4)) ||
               !unit.queuedCommands.empty())) {
            unit.order = definitions_->units[unit.type].returnIdle;
            unit.orderState = unit.orderTimer = 0;
            unit.mainTimer = 7;
            unit.followTimer = 0;
            unit.target = {};
            unit.targetPosition = {};
            activateQueuedCommand(unit);
            break;
          }
          if (distance(gap) > 96) {
            if (((differentDestination && !retainedBoundary) ||
                 !get(unit.moveTargetUnit)) &&
                unit.followTimer == 0)
              requestFollow();
          } else if ((initial || get(unit.moveTargetUnit) ||
                      unit.escapingOverlap) &&
                     (initial ||
                      (unit.motion->moving() && !unit.motion->settling() &&
                       !unit.motion->stopped()))) {
            // Escape movement can have cleared moveTargetUnit already. The
            // follow order still replaces that escape with a braking endpoint.
            const auto end = unit.motion->stopTarget();
            unit.moveTargetPosition = end;
            unit.moveTargetUnit = {};
            unit.motion->brakeTo(end, initial);
            unit.overlapStopPending = unit.escapingOverlap;
            unit.overlapOrderChanged = unit.overlapFreshOrder = false;
            unit.planRequested = false;
            unit.path.reset();
          }
          if (distance(gap) <= 96)
            unit.followTimer = 15;
          unit.orderState = 1;
          unit.mainTimer = 7;
          break;
        }
        }
        if (unit.order == 14)
          processAttackMove(unit);
      }
      activateQueuedCommand(unit);
      if (refreshVision && !unit.motion)
        revealSight(unit);
      processSecondary(unit);
      const auto *animationTarget = get(unit.target);
      unit.sprite.context.target = animationTarget && !animationTarget->dead &&
                                   animationTarget->visible;
      unit.sprite.tick(resources, random_,
                       [&](const auto &i, const auto &layer) {
                         return effect(unit.handle, i, layer);
                       });
      if (unit.queuedUserOverride)
        activateQueuedCommand(unit);
      if (unit.motion)
        unit.motion->setTopSpeed(unit.sprite.context.topSpeed);
      if (unit.motion && unit.motion->scriptDriven() &&
          (motionAtOrderStart || unit.sprite.context.speedWritten))
        unit.motion->scriptSpeed(unit.sprite.context.speed,
                                 unit.sprite.context.speedWritten);
      unit.sprite.context.speedWritten = false;
    }
    for (auto at = hidden_.begin(); at != hidden_.end();) {
      int id = *at++;
      auto &unit = *units_[id];
      advanceVitals(unit);
      if (unit.order == 0) {
        for (int parent : visible_)
          if (units_[parent]->buildUnit == unit.handle)
            units_[parent]->buildUnit = {};
        remove(unit.handle);
        continue;
      }
      unit.sprite.tick(resources, random_,
                       [&](const auto &i, const auto &layer) {
                         return effect(unit.handle, i, layer);
                       });
    }
    refreshPowerState();
    advanceProjectiles();
    for (auto at = activeEffects_.begin(); at != activeEffects_.end();) {
      int id = *at++;
      auto &effect = *effects_[id];
      effect.sprite.tick(resources, random_,
                         [&](const auto &i, const auto &layer) {
                           return effectSprite(id, i, layer);
                         });
      if (effect.sprite.order.empty()) {
        activeEffects_.remove(id);
        spriteRows_.remove(effect.sprite.sceneKey);
        effects_[id].reset();
        anchoredInsert(freeEffects_, id);
      }
    }
  }
}
void World::prepareImportedMap() {
  if (frame_ != 0)
    throw std::logic_error("Map preparation must precede simulation");
  orderCycle_ = 10;
  advance(2);
  pathPool_.shiftFrames(-2);
  frame_ = 0;
}
int World::addSprite(int type, Point position, int owner) {
  return createEffect(type, position, owner, 4);
}
int World::createEffect(int type, Point position, int owner, int elevation,
                        UnitHandle context) {
  const auto &definition = definitions_->sprites.at(type);
  if (owner < 0 || owner >= 12)
    throw std::out_of_range("Invalid sprite owner");
  if (position.x < 0 || position.y < 0 ||
      position.x >= map_->terrain.width() * 32 ||
      position.y >= map_->terrain.height() * 32)
    return -1;
  int id;
  if (!freeEffects_.empty()) {
    id = freeEffects_.front();
    freeEffects_.pop_front();
  } else {
    if (allocatedEffects_ == 500)
      return -1;
    id = allocatedEffects_++;
  }
  auto &effect = effects_[id].emplace();
  effect.sprite.sceneKey = spriteRows_.create(position);
  effect.sprite.context.tileset = map_->tileset;
  effect.type = type;
  effect.hidden = !definition.visible;
  effect.position = position;
  effect.owner = owner;
  auto *unit = get(context);
  if (unit)
    effect.sprite.context = unit->sprite.context;
  else {
    effect.sprite.context.unitContext = false;
    effect.sprite.context.flingyContext = false;
  }
  try {
    effect.sprite.initialize(definition.image, definitions_->resources(),
                             random_, [&](const auto &i, const auto &layer) {
                               return unit ? this->effect(context, i, layer)
                                           : effectSprite(id, i, layer);
                             });
    if (effect.sprite.order.empty())
      throw std::runtime_error("Sprite Init ended immediately");
    if (unit) {
      unit->sprite.context = effect.sprite.context;
      unit->sprite.setHeading(unit->sprite.context.heading);
    }
    effect.sprite.context.unitContext = false;
    effect.sprite.context.flingyContext = false;
    effect.sprite.context.target = false;
    effect.sprite.context.resourceContainer = false;
    effect.elevation = elevation;
    anchoredInsert(activeEffects_, id);
    return id;
  } catch (...) {
    spriteRows_.remove(effect.sprite.sceneKey);
    effects_[id].reset();
    freeEffects_.push_front(id);
    throw;
  }
}
bool World::spawnEffect(const iscript::Instruction &instruction,
                        const SpriteImage &layer, Point origin, int elevation,
                        UnitHandle context) {
  int opcode = instruction.opcode;
  if (opcode != 15 && opcode != 17 && opcode != 19 && opcode != 20 &&
      opcode != 21 && opcode != 66)
    return false;
  const auto &a = instruction.arguments;
  if ((opcode == 19 || opcode == 20 || opcode == 21) && a[0] == 517)
    return true;
  Point delta = opcode == 21 ? definitions_->offset(layer.type, layer.frame(),
                                                    a[1], 0, layer.flip)
                             : Point{a[1], a[2]};
  Point position{origin.x + layer.offset.x + delta.x,
                 origin.y + layer.offset.y + delta.y};
  if (opcode == 66 &&
      !Placement(map_->terrain, map_->regions, {})
           .terrainFits(position, definitions_->units[0].bounds))
    return true;
  int level = opcode == 17 ? 1
                           : elevation + (opcode == 19   ? 0
                                          : opcode == 20 ? -1
                                                         : 1);
  int id = createEffect(a[0], position, 0, level, context);
  if (id >= 0 && (opcode == 19 || opcode == 20 || opcode == 21)) {
    int direction = layer.flip ? 32 - layer.direction : layer.direction;
    effects_[id]->sprite.setHeading(direction * 8);
  }
  return true;
}
iscript::HostAction World::effectSprite(int id,
                                        const iscript::Instruction &instruction,
                                        const SpriteImage &layer) {
  const auto &effect = *effects_[id];
  if (spawnEffect(instruction, layer, effect.position, effect.elevation))
    return iscript::HostAction::Continue;
  switch (instruction.opcode) {
  case 27:
  case 28:
  case 37:
  case 38:
  case 39:
  case 40:
  case 42:
  case 49:
  case 68:
    return iscript::HostAction::Continue;
  }
  throw std::runtime_error("Unimplemented sprite animation effect " +
                           std::to_string(instruction.opcode));
}
std::vector<CollisionBody> World::bodies(int ignore) const {
  std::vector<CollisionBody> result;
  result.reserve(visible_.size());
  for (int id : visible_) {
    const auto &slot = units_[id];
    if (slot && slot->visible && !slot->dead && slot->handle.index != ignore) {
      const auto &unit = *slot;
      const auto &type = definitions_->units[unit.type];
      result.push_back({std::uint32_t(unit.handle.index), unit.position,
                        type.bounds, bool(type.flags & 4) || unit.gasLanding});
    }
  }
  // Placement and route ties use slot order, independently of update order.
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.key < b.key; });
  return result;
}
std::vector<std::uint8_t> World::occupancy(int ignore) const {
  int w = map_->terrain.width(), h = map_->terrain.height();
  std::vector<std::uint8_t> result(std::size_t(w) * h);
  for (int id : visible_) {
    const auto &slot = units_[id];
    if (slot && slot->visible && !slot->dead && slot->handle.index != ignore) {
      const auto &unit = *slot;
      const auto &type = definitions_->units[unit.type];
      if (!type.building() || (type.flags & 4))
        continue;
      // Tile occupancy centers the placement footprint after quantizing the
      // center. Odd tile sizes therefore retain their center tile even when
      // a map places the building away from the usual half-tile alignment.
      int left = unit.position.x / 32 - type.placementWidth / 64,
          top = unit.position.y / 32 - type.placementHeight / 64;
      for (int y = std::max(0, top);
           y < std::min(h, top + type.placementHeight / 32); ++y)
        for (int x = std::max(0, left);
             x < std::min(w, left + type.placementWidth / 32); ++x)
          result[std::size_t(y) * w + x] = 1;
    }
  }
  return result;
}
iscript::HostAction World::effect(UnitHandle handle,
                                  const iscript::Instruction &instruction,
                                  const SpriteImage &layer) {
  auto *unit = get(handle);
  if (!unit)
    throw std::runtime_error("Animation refers to missing unit");
  if (spawnEffect(instruction, layer, unit->position,
                  definitions_->units[unit->type].elevation, handle))
    return iscript::HostAction::Continue;
  switch (instruction.opcode) {
  case 48: {
    unit->sprite.context.uninterruptible = false;
    const int animation =
        unit->sprite.images[unit->sprite.main].cursor.animation;
    if (animation == 2 || animation == 5 || animation == 3 || animation == 6) {
      unit->attackActive = false;
      unit->sprite.animate(animation == 2 || animation == 5 ? 8 : 9,
                            definitions_->resources(), random_,
                            [&](const auto &event, const auto &image) {
                              return effect(handle, event, image);
                            });
      return iscript::HostAction::Suspend;
    }
    return iscript::HostAction::Continue;
  }
  case 28:
    meleeHit(*unit);
    return iscript::HostAction::Continue;
  case 42:
    unit->attackActive = false;
    return iscript::HostAction::Continue;
  case 40:
    useWeapon(*unit, instruction.arguments.at(0));
    return iscript::HostAction::Continue;
  case 37: {
    if (!get(unit->target))
      return iscript::HostAction::Continue;
    const auto &type = definitions_->units[unit->type];
    useWeapon(*unit, instruction.arguments.at(0) == 1 ? type.groundWeapon : type.airWeapon);
    return iscript::HostAction::Continue;
  }
  case 38: {
    const auto *target = get(unit->target);
    if (!target)
      return iscript::HostAction::Continue;
    const auto &type = definitions_->units[unit->type];
    useWeapon(*unit, definitions_->units[target->type].flags & 4 ? type.airWeapon : type.groundWeapon);
    return iscript::HostAction::Continue;
  }
  case 39:
    if (!unit->sprite.context.target)
      return iscript::HostAction::Continue;
    break;
  }
  throw std::runtime_error(
      "Unimplemented world animation effect " +
      std::to_string(instruction.opcode) +
      (instruction.arguments.empty()
           ? std::string{}
           : " argument " + std::to_string(instruction.arguments.front())));
}
void World::replaceImages(UnitState &unit, int image) {
  auto heading = unit.sprite.context.heading;
  auto resources = definitions_->resources();
  unit.sprite.initialize(image, resources, random_,
                         [&](const auto &i, const auto &layer) {
                           return effect(unit.handle, i, layer);
                         });
  unit.sprite.setHeading(heading);
}
UnitHandle World::createFinished(int type, Point position, int owner,
                                 bool relocate, bool initial) {
  return createUnit(type, position, owner, relocate, initial, true);
}
UnitHandle World::createConstruction(int type, Point position, int owner) {
  auto handle = createUnit(type, position, owner, false, false, false);
  auto *unit = get(handle);
  if (!unit)
    return handle;
  const auto &definition = definitions_->units[type];
  unit->visible = true;
  hidden_.remove(handle.index);
  anchoredInsert(visible_, handle.index);
  moveSpatial(*unit, true);
  if (definition.shields)
    unit->vitals.shields /= 10;
  auto full = finishedUnitVitals(type, definition).hitpoints;
  auto duration = std::max(1, definition.buildTime);
  unit->constructionGain = int(
      (std::int64_t(full) - unit->vitals.hitpoints + duration - 1) / duration);
  replaceImages(*unit, 211);
  unit->order = 48;
  notifyOverlap(*unit);
  revealSight(*unit);
  return handle;
}
UnitHandle World::createUnit(int type, Point position, int owner, bool relocate,
                             bool initial, bool finished) {
  const auto &definition = definitions_->units.at(type);
  const auto &physics = definitions_->flingies.at(definition.graphics);
  const auto &spriteDefinition = definitions_->sprites.at(physics.sprite);
  if (owner < 0 || owner >= 12)
    throw std::out_of_range("Invalid unit owner");
  random_.next();
  if (free_.empty())
    return {};
  if (position.x < definition.bounds.left ||
      position.y < definition.bounds.top ||
      position.x + definition.bounds.right >= map_->terrain.width() * 32 ||
      position.y + definition.bounds.bottom >= map_->terrain.height() * 32)
    return {};
  int id = free_.front();
  free_.pop_front();
  UnitHandle handle{id, ++generations_[id]};
  auto &unit = units_[id].emplace();
  unit.handle = handle;
  unit.birth = nextBirth_++;
  unit.type = type;
  unit.vitals = finishedUnitVitals(type, definition);
  unit.owner = owner;
  unit.position = position;
  unit.sprite.sceneKey = spriteRows_.create(position);
  auto resources = definitions_->resources();
  auto host = [&](const auto &instruction, const auto &layer) {
    return effect(handle, instruction, layer);
  };
  try {
    unit.sprite.context.topSpeed = int(physics.speed);
    unit.sprite.context.tileset = map_->tileset;
    unit.sprite.initialize(spriteDefinition.image, resources, random_, host);
    unit.sprite.setHeading(0);
    unit.sprite.context.signal = 0;
    unit.sprite.context.resourceContainer = bool(definition.flags & 0x2000U);
    unit.sprite.tick(resources, random_, host);
    unit.wireframeRandomizer = random_.next() & 255;
    if (!finished) {
      unit.vitals.hitpoints =
          std::max<std::int32_t>(1, unit.vitals.hitpoints / 10);
      unit.order = 23;
      unit.moveTargetPosition = position;
      unit.remainingBuildTime = definition.buildTime;
      auto full = finishedUnitVitals(type, definition).hitpoints;
      unit.constructionGain = int((std::int64_t(full) - unit.vitals.hitpoints) /
                                      std::max(1, definition.buildTime) +
                                  1);
      anchoredInsert(hidden_, id);
      return handle;
    }
    return completeUnit(handle, relocate, initial);
  } catch (...) {
    air_.erase(unit.air);
    spriteRows_.remove(unit.sprite.sceneKey);
    spatial_.remove(id);
    units_[id].reset();
    visible_.remove(id);
    free_.push_front(id);
    throw;
  }
}
UnitHandle World::completeUnit(UnitHandle handle, bool relocate, bool initial) {
  auto &unit = *get(handle);
  bool wasVisible = unit.visible;
  const auto &definition = definitions_->units.at(unit.type);
  const auto &physics = definitions_->flingies.at(definition.graphics);
  const auto &spriteDefinition = definitions_->sprites.at(physics.sprite);
  int type = unit.type, id = handle.index;
  Point position = unit.position;
  auto resources = definitions_->resources();
  auto host = [&](const auto &instruction, const auto &layer) {
    return effect(handle, instruction, layer);
  };
  unit.visible = definition.building();
  if (definition.building() && !wasVisible)
    replaceImages(unit, definition.constructionImage
                            ? definition.constructionImage
                            : spriteDefinition.image);
  bool provider = type == 131 || type == 132 || type == 133 || type == 143 ||
                  type == 144 || type == 146;
  if (initial && (provider || (definition.flags & 0x20000U)))
    creep_.spread(map_->terrain, position,
                  {definition.placementWidth, definition.placementHeight},
                  provider, occupancy(), random_);
  if (definition.building()) {
    if (!wasVisible)
      replaceImages(unit, spriteDefinition.image);
    unit.sprite.animate(16, resources, random_, host);
  } else if (definition.flags & 0x10000000U) {
    int direction = definition.initialDirection;
    if (direction == 32)
      direction = random_.next() % 32;
    unit.sprite.setHeading(direction * 8);
  }
  if (!unit.visible && relocate) {
    auto occupants = bodies(id);
    auto placed = Placement(map_->terrain, map_->regions, occupants)
                      .find({position, definition.bounds,
                             bool(definition.flags & 4), std::uint32_t(id)});
    if (!placed) {
      remove(handle);
      return handle;
    }
    unit.position = *placed;
  }
  spriteRows_.move(unit.sprite.sceneKey, unit.position);
  unit.visible = true;
  hidden_.remove(id);
  moveSpatial(unit, !(wasVisible && definition.building()));
  unit.moveTargetPosition = unit.position;
  if (!wasVisible)
    anchoredInsert(visible_, id);
  unit.completed = true;
  unit.order = definition.humanIdle;
  unit.sprite.context.flying = bool(definition.flags & 4);
  if ((definition.flags & 4) && (definition.flags & 0x8000000U) &&
      !definition.building() && type != 73) {
    if (unit.air.cell < 0)
      air_.insert(unit.air, unit.position, random_);
    else
      air_.move(unit.air, unit.position, random_);
  }
  if ((definition.flags & 0x8000000U) &&
      (physics.control == 0 || physics.control == 2) &&
      physics.acceleration > 0 && physics.speed) {
    unit.motion.emplace(unit.position, unit.sprite.context.heading, physics,
                        bool(definition.flags & 4));
    if (unit.inactiveInertia) {
      if (!(definition.flags & 4))
        unit.motion->inheritInertia(*unit.inactiveInertia);
    } else if (releasedInertia_[id]) {
      unit.motion->inheritInertia(*releasedInertia_[id], true);
    }
    unit.inactiveInertia.reset();
    releasedInertia_[id].reset();
    if (physics.control == 2) {
      const auto &main = unit.sprite.images[unit.sprite.main];
      auto speed = iscript::Animation(definitions_->program, random_)
                       .averageWalkingSpeed(main.cursor.script);
      if (speed) {
        unit.sprite.context.topSpeed = *speed;
        unit.motion->setTopSpeed(*speed);
      }
    }
  }
  if (!wasVisible || !definition.building())
    revealSight(unit);
  return handle;
}
UnitHandle World::larva(UnitHandle depot) {
  auto *base = get(depot);
  if (!base)
    return {};
  auto bounds = definitions_->units[base->type].bounds;
  auto origin = base->position;
  auto occupied = occupancy();
  auto fits = [&](Point p) {
    auto e = definitions_->units[35].bounds;
    return p.x >= e.left && p.y >= e.top &&
           p.x + e.right < map_->terrain.width() * 32 &&
           p.y + e.bottom < map_->terrain.height() * 32 - 32;
  };
  struct Candidate {
    Point point, offset;
    int side;
  };
  std::vector<Candidate> choices{
      {{origin.x, origin.y + bounds.bottom + 10}, {0, 22}, 3},
      {{origin.x - bounds.left - 10, origin.y}, {-22, 0}, 0},
      {{origin.x + bounds.right + 10, origin.y}, {22, 0}, 2},
      {{origin.x, origin.y - bounds.top - 10}, {0, -22}, 1}};
  int halfWidth = (bounds.left + bounds.right + 1) / 2,
      halfHeight = (bounds.top + bounds.bottom + 1) / 2;
  for (int side = 0; side < 4; ++side) {
    auto [point, out, weightSide] = choices[side];
    for (int direction : {-1, 1}) {
      auto p = point;
      if (out.x)
        p.y += direction * halfHeight;
      else
        p.x += direction * halfWidth;
      choices.push_back({p, out, weightSide});
    }
  }
  std::stable_sort(choices.begin(), choices.begin() + 4,
                   [&](const auto &a, const auto &b) {
                     return base->larvaSpawnWeights[a.side] <
                            base->larvaSpawnWeights[b.side];
                   });
  for (std::size_t index = 0; index < choices.size(); ++index) {
    auto [p, out, side] = choices[index];
    if (index < 4 && base->larvaSpawnWeights[side] > 100)
      continue;
    Point neighbor{p.x + out.x, p.y + out.y};
    if (!fits(p) || !fits(neighbor) || !creep_.at(p.x / 32, p.y / 32))
      continue;
    int x = neighbor.x / 32, y = neighbor.y / 32;
    if (occupied[std::size_t(y) * map_->terrain.width() + x])
      continue;
    if (!map_->terrain.tile(x, y).category && !creep_.at(x, y))
      continue;
    auto handle = createFinished(35, p, base->owner, false);
    if (auto *unit = get(handle)) {
      if (unit->motion)
        unit->motion->inheritInertia({});
      unit->connected = depot;
      unit->orderState = p.x < origin.x - bounds.left    ? 0
                         : p.y < origin.y - bounds.top   ? 1
                         : p.x > origin.x + bounds.right ? 2
                                                         : 3;
      notifyOverlap(*unit);
    }
    return handle;
  }
  return {};
}
std::vector<UnitHandle> World::startingUnits(int race, Point position,
                                             int owner) {
  if (race < 0 || race > 2)
    throw std::out_of_range("Invalid starting race");
  int depotType = race == 0   ? 131
                  : race == 1 ? 106
                              : 154,
      worker = race == 0   ? 41
               : race == 1 ? 7
                           : 64;
  const auto &type = definitions_->units[depotType];
  Point center{(position.x - type.placementWidth / 2) / 32 * 32 +
                   type.placementWidth / 2,
               (position.y - type.placementHeight / 2) / 32 * 32 +
                   type.placementHeight / 2};
  for (const auto &body : bodies()) {
    if (body.position.x + body.bounds.right >=
            center.x - type.placementWidth / 2 &&
        body.position.x - body.bounds.left <
            center.x + type.placementWidth / 2 &&
        body.position.y + body.bounds.bottom >=
            center.y - type.placementHeight / 2 &&
        body.position.y - body.bounds.top < center.y + type.placementHeight / 2)
      remove(units_[body.key]->handle);
  }
  auto depot = createFinished(depotType, center, owner);
  if (race == 0) {
    if (get(depot)) {
      creep_.spread(map_->terrain, center,
                    {type.placementWidth, type.placementHeight}, true,
                    occupancy(), random_);
      larva(depot);
      larva(depot);
    }
    Point size{type.bounds.left + type.bounds.right + 1,
               type.bounds.top + type.bounds.bottom + 1};
    createFinished(
        42,
        {position.x +
             (position.x >= map_->terrain.width() * 16 ? -size.x : size.x),
         position.y +
             (position.y >= map_->terrain.height() * 16 ? -size.y : size.y)},
        owner);
  }
  std::vector<UnitHandle> workers;
  for (int i = 0; i < 4; ++i)
    workers.push_back(createFinished(worker, position, owner));
  return workers;
}
} // namespace mapeditor::simulation
