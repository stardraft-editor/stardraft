// SPDX-License-Identifier: MIT
#include <mapeditor/simulation/world.h>
#include <mapeditor/simulation/damage.h>
#include <mapeditor/simulation/psi.h>
#include <tuple>
#include <set>

namespace mapeditor::simulation {
bool World::onTargetPerimeter(const UnitState &unit,
                              const UnitState &target) const {
  const auto &shape = definitions_->units[unit.type].bounds;
  const auto &other = definitions_->units[target.type].bounds;
  // Accepted order destinations use the existing Follow perimeter, which
  // differs from the rectangle used by route contact adjustment.
  const PixelBox boundary{
      target.position.x - other.left - shape.right - 1,
      target.position.y - other.top - shape.bottom - 1,
      target.position.x + other.right + shape.left + shape.top + 2,
      target.position.y + other.bottom};
  const auto destination = unit.moveTargetPosition;
  return boundary.contains(destination) &&
         (destination.x == boundary.left || destination.x == boundary.right ||
          destination.y == boundary.top || destination.y == boundary.bottom);
}
bool World::allied(int owner, int other) const {
  if (owner < 0 || owner >= 12 || other < 0 || other >= 12)
    throw std::out_of_range("Invalid alliance player");
  return (alliances_[owner] & (1U << other)) != 0;
}
void World::setAlliance(int owner, int other, bool enabled) {
  (void)allied(owner, other);
  const auto bit = std::uint16_t(1U << other);
  alliances_[owner] = enabled ? alliances_[owner] | bit : alliances_[owner] & ~bit;
}
int World::attackPriority(const UnitState &actor, const UnitState &other) const {
  const auto &targetType = definitions_->units[other.type];
  const auto &body = definitions_->units[other.type];
  const auto &guns = body.turret >= 0 && body.turret < 228
                         ? definitions_->units[body.turret] : body;
  auto weapon = [&](const dat::Unit &type, bool air) {
    int index = air ? type.airWeapon : type.groundWeapon;
    return index >= 0 && index < int(definitions_->weapons.size()) ? index : -1;
  };
  int category;
  const bool immature = other.type == 35 || other.type == 36 ||
                        other.type == 59 || other.type == 97;
  if (immature)
    category = 5;
  else if (targetType.flags & 8)
    category = 2;
  else if (weapon(guns, definitions_->units[actor.type].flags & 4) >= 0)
    category = 0;
  else if (weapon(guns, false) >= 0 || weapon(guns, true) >= 0)
    category = 2;
  else
    category = targetType.flags & 0x08000000U ? 3 : 4;
  if (!immature && !other.completed)
    ++category;
  return category;
}
std::vector<UnitHandle> World::targetCandidates(UnitHandle handle, bool includeRetaliation) const {
  const auto *actor = get(handle);
  if (!actor || actor->dead || !actor->visible)
    return {};
  const auto &actorType = definitions_->units[actor->type];
  const int range = definitions_->acquisitionRange(actor->type) * 32;
  if (!range)
    return {};
  auto armament = [&](int type) -> const dat::Unit & {
    const auto &body = definitions_->units[type];
    return body.turret >= 0 && body.turret < 228 ? definitions_->units[body.turret] : body;
  };
  auto weapon = [&](const dat::Unit &type, bool air) {
    const int index = air ? type.airWeapon : type.groundWeapon;
    return index >= 0 && index < int(definitions_->weapons.size()) ? index : -1;
  };
  std::vector<UnitHandle> selected;
  int bestCategory = 6;
  std::array<int, 6> admitted{};
  std::set<int> seen;
  for (const auto &edge : spatial_.axes()[0]) {
    if (edge.coordinate < actor->position.x - range - 64 ||
        edge.coordinate >= actor->position.x + range + 64 ||
        !seen.insert(int(edge.key)).second)
      continue;
    const auto &other = *units_[edge.key];
    if (other.handle == handle || other.dead || !other.visible ||
        allied(actor->owner, other.owner) ||
        !(visibleAt({other.position.x / 32, other.position.y / 32}) & (1U << actor->owner)))
      continue;
    const auto &targetType = definitions_->units[other.type];
    const int index = weapon(armament(actor->type), targetType.flags & 4);
    if (index < 0)
      continue;
    const auto &projectile = definitions_->weapons[index];
    const auto &a = actorType.bounds;
    const auto &b = targetType.bounds;
    Point gap{std::max({0, other.position.x - b.left - actor->position.x - a.right,
                        actor->position.x - a.left - other.position.x - b.right}),
              std::max({0, other.position.y - b.top - actor->position.y - a.bottom,
                        actor->position.y - a.top - other.position.y - b.bottom})};
    // Footprints occupy inclusive pixels; separation starts beyond that edge.
    gap.x = std::max(0, gap.x - 1);
    gap.y = std::max(0, gap.y - 1);
    const int separation = distance(gap);
    if (separation > range || (projectile.minimumRange && separation <= int(projectile.minimumRange)))
      continue;
    if (includeRetaliation && !actorType.building() &&
        other.handle == actor->retaliationTarget)
      return {other.handle};
    const int category = attackPriority(*actor, other);
    if (++admitted.at(category) > 16)
      continue;
    if (category < bestCategory) {
      bestCategory = category;
      selected.clear();
    }
    if (category == bestCategory)
      selected.push_back(other.handle);
  }
  return selected;
}
UnitHandle World::acquireTarget(UnitHandle handle) const {
  return acquireTarget(handle, true);
}
UnitHandle World::acquireTarget(UnitHandle handle, bool includeRetaliation) const {
  const auto candidates = targetCandidates(handle, includeRetaliation);
  if (candidates.empty())
    return {};
  const auto origin = get(handle)->position;
  return *std::min_element(candidates.begin(), candidates.end(), [&](auto a, auto b) {
    auto range = [&](auto target) {
      const auto point = get(target)->position;
      return distance({point.x - origin.x, point.y - origin.y});
    };
    return range(a) < range(b);
  });
}
bool World::processGuardRetaliation(UnitState &unit) {
  const auto *target = get(unit.retaliationTarget);
  if (!target)
    return false;
  const auto &body = definitions_->units[unit.type];
  const bool air = definitions_->units[target->type].flags & 4;
  const int weapon = air ? body.airWeapon : body.groundWeapon;
  if (target->dead || !target->visible || allied(unit.owner, target->owner) ||
      weapon < 0 || weapon >= int(definitions_->weapons.size())) {
    unit.retaliationTarget = {};
    return false;
  }
  // A remembered aggressor bypasses the periodic radius search. Preserve
  // that search's countdown when the damage response changes the order.
  const int timer = unit.mainTimer;
  autoAttack(unit.handle, target->handle);
  unit.mainTimer = timer;
  return true;
}
void World::processTower(UnitState &unit) {
  const auto &body = definitions_->units[unit.type];
  bool powered = !(body.flags & 0x80000U);
  if (!powered)
    for (int id : visible_) {
      const auto &provider = *units_[id];
      if (!provider.dead && provider.type == 156 && provider.psiActive &&
          provider.owner == unit.owner &&
          withinPsiField({unit.position.x - provider.position.x,
                          unit.position.y - provider.position.y})) {
        powered = true;
        break;
      }
    }
  auto idle = [&] {
    prepareOrderTransition(unit);
    unit.order = body.returnIdle;
    unit.orderState = unit.orderTimer = unit.mainTimer = 0;
    unit.target = {};
    unit.targetPosition = {};
    activateQueuedCommand(unit);
  };
  if (unit.order == 18) {
    if (!powered || unit.mainTimer)
      return;
    unit.mainTimer = 15;
    const auto candidates = targetCandidates(unit.handle);
    unit.target = candidates.empty()
                      ? UnitHandle{}
                      : candidates[candidates.size() == 1 ? 0
                                        : random_.next() % candidates.size()];
    if (const auto *target = get(unit.target)) {
      prepareOrderTransition(unit);
      unit.targetPosition = target->position;
      unit.order = 19;
      unit.orderState = unit.orderTimer = 0;
    }
    return;
  }
  const auto *target = get(unit.target);
  if (!powered || !target || target->dead || !target->visible ||
      allied(unit.owner, target->owner) ||
      !(visibleAt({target->position.x / 32, target->position.y / 32}) &
        (1U << unit.owner))) {
    idle();
    return;
  }
  const auto &other = definitions_->units[target->type];
  const bool airborne = other.flags & 4;
  const int index = airborne ? body.airWeapon : body.groundWeapon;
  if (index < 0 || index >= int(definitions_->weapons.size())) {
    idle();
    return;
  }
  unit.targetPosition = target->position;
  const auto &weapon = definitions_->weapons[index];
  const int cooldown = airborne ? unit.airCooldown : unit.groundCooldown;
  if (cooldown)
    unit.orderTimer = std::min(unit.orderTimer, cooldown - 1);
  else if (!unit.attackActive) {
    const auto &a = body.bounds;
    const auto &b = other.bounds;
    Point gap{std::max({0, target->position.x - b.left - unit.position.x - a.right - 1,
                         unit.position.x - a.left - target->position.x - b.right - 1}),
              std::max({0, target->position.y - b.top - unit.position.y - a.bottom - 1,
                         unit.position.y - a.top - target->position.y - b.bottom - 1})};
    const int separation = distance(gap);
    const int wanted = heading({target->position.x - unit.position.x,
                                target->position.y - unit.position.y});
    if (separation < int(weapon.minimumRange) ||
        separation > int(weapon.maximumRange) ||
        std::abs(signedHeading(wanted - unit.sprite.context.heading)) > weapon.attackAngle) {
      idle();
      return;
    }
    unit.groundCooldown = unit.airCooldown =
        std::max(0, weapon.cooldown + int(random_.next() & 3) - 1);
    unit.attackActive = true;
    unit.sprite.context.target = true;
    unit.sprite.context.targetHeading = wanted;
    unit.sprite.context.targetDistance = separation;
    unit.sprite.animate(airborne ? 6 : 5, definitions_->resources(), random_,
                        [&](const auto &instruction, const auto &layer) {
                          return effect(unit.handle, instruction, layer);
                        });
  }
  unit.moveTargetPosition = unit.position;
}
void World::attackMove(UnitHandle handle, Point destination, bool queued) {
  auto *unit = get(handle);
  if (!unit || unit->dead || !unit->visible || !unit->completed || !unit->motion ||
      destination.x < 0 || destination.y < 0 ||
      destination.x >= map_->terrain.width() * 32 || destination.y >= map_->terrain.height() * 32)
    throw std::invalid_argument("Invalid attack-move command");
  if (queued) {
    unit->queuedCommands.push_back({14, destination, {}});
    activateQueuedCommand(*unit);
    return;
  }
  if (deferLockedCommand(*unit, {14, destination, {}}))
    return;
  move(handle, destination);
  if (unit->harvestContinuation && unit->harvestContinuation->order == 6)
    unit->harvestContinuation->order = 14;
  else if (unit->landingContinuation && unit->landingContinuation->order == 6)
    unit->landingContinuation->order = 14;
  else if (unit->order == 6)
    unit->order = 14;
}
void World::brakeForCombat(UnitState &unit, bool initial) {
  const auto end = unit.motion->stopTarget();
  unit.moveTargetPosition = end;
  unit.moveTargetUnit = {};
  unit.motion->brakeTo(end, initial);
  if (!initial)
    unit.followTimer = 15;
  unit.overlapStopPending = unit.escapingOverlap;
  unit.overlapOrderChanged = unit.overlapFreshOrder = false;
  unit.planRequested = false;
  unit.path.reset();
}
void World::processAttackMove(UnitState &unit) {
  if (unit.mainTimer)
    return;
  unit.mainTimer = 15;
  if (samePoint(unit.position, unit.moveTargetPosition)) {
    unit.order = definitions_->units[unit.type].returnIdle;
    unit.orderState = unit.orderTimer = unit.mainTimer = 0;
    unit.target = {};
    unit.targetPosition = {};
    if (!unit.escapingOverlap)
      unit.overlapOrderChanged = unit.overlapFreshOrder = false;
    return;
  }
  auto selected = acquireTarget(unit.handle);
  if (selected.index < 0)
    return;
  const Point destination = unit.targetPosition;
  auto remaining = std::move(unit.queuedCommands);
  // A landing reset has discarded its physical path before this new order
  // acquires a target. Its pending turn still belongs to the new request.
  const auto pendingDirection =
      !unit.planRequested
          ? std::optional<int>(unit.motion->desiredHeading()) : std::nullopt;
  brakeForCombat(unit, unit.planRequested || unit.collisionRouteReset);
  const bool pendingStop = unit.motion->needsArrivalUpdate();
  if (unit.collisionRouteReset || pendingStop)
    unit.motion->faceOnArrival(
        destination, pendingStop,
        pendingStop && !unit.collisionRouteReset
            ? pendingDirection : std::nullopt);
  autoAttack(unit.handle, selected);
  unit.mainTimer = 15;
  unit.queuedCommands.push_back({14, destination, {}});
  unit.queuedCommands.insert(unit.queuedCommands.end(), remaining.begin(), remaining.end());
}
void World::attack(UnitHandle handle, UnitHandle destination, bool queued) {
  beginAttack(handle, destination, queued, false);
}
void World::autoAttack(UnitHandle handle, UnitHandle destination) {
  beginAttack(handle, destination, false, true);
}
void World::beginAttack(UnitHandle handle, UnitHandle destination,
                        bool queued, bool automatic) {
  auto *unit = get(handle);
  const auto *target = get(destination);
  if (!unit || unit->dead || !unit->visible || !unit->completed ||
      !unit->motion || !target || target->dead || !target->visible ||
      handle == destination)
    throw std::invalid_argument("Invalid attack command");
  const auto &attackerType = definitions_->units[unit->type];
  const bool airborne = definitions_->units[target->type].flags & 4;
  const int weapon = airborne ? attackerType.airWeapon : attackerType.groundWeapon;
  if (weapon < 0 || weapon >= int(definitions_->weapons.size()))
    throw std::invalid_argument("Unit has no weapon for this target");
  if (queued) {
    unit->queuedCommands.push_back({10, target->position, destination, 228, automatic});
    activateQueuedCommand(*unit);
    return;
  }
  if (deferLockedCommand(*unit, {10, target->position, destination, 228, automatic}))
    return;
  unit->queuedCommands.clear();
  unit->queuedUserOverride = false;
  if (unit->order == 70 && unit->orderState == 2) {
    unit->landingContinuation = UnitCommand{10, target->position, destination, 228, automatic};
    return;
  }
  if (interruptHarvest(*unit, {10, target->position, destination, 228, automatic}))
    return;
  prepareOrderTransition(*unit);
  unit->autoAttackTargeting = automatic;
  unit->target = destination;
  unit->targetPosition = target->position;
  unit->pendingRoute.clear();
  unit->failedRouteFrames = 0;
  unit->followTimer = 0;
  if (unit->escapingOverlap)
    unit->overlapDirectRoute = true;
  unit->order = 10;
  unit->orderState = unit->orderTimer = unit->mainTimer = 0;
}
void World::prepareOrderTransition(UnitState &unit) {
  unit.autoAttackTargeting = false;
  unit.interruptedMoveGoal = unit.motion
                                 ? std::optional<Point>(unit.moveTargetPosition)
                                 : std::nullopt;
  // The pending movement update still belongs to the old order. Its turn
  // consumes the last committed point, before the new order replaces it.
  if (unit.motion && unit.order == 10 && unit.motion->braking() &&
      !(unit.motion->stopped() && unit.orderState == 0))
    unit.motion->faceOnArrival(unit.targetPosition);
  const int animation = unit.sprite.images[unit.sprite.main].cursor.animation;
  if (animation == 2 || animation == 5 || animation == 3 || animation == 6) {
    unit.sprite.context.uninterruptible = false;
    unit.sprite.animate(animation == 2 || animation == 5 ? 8 : 9,
                        definitions_->resources(), random_,
                        [&](const auto &instruction, const auto &layer) {
                          return effect(unit.handle, instruction, layer);
                        });
  }
  unit.attackActive = false;
  unit.attackPointAdmission = false;
  unit.retaliationTarget = {};
}
void World::reacquireAttackTarget(UnitState &actor) {
  struct Choice { UnitHandle handle; std::tuple<int, int, int> score; };
  std::optional<Choice> best;
  const auto &body = definitions_->units[actor.type];
  const auto &guns = body.turret >= 0 && body.turret < 228
                         ? definitions_->units[body.turret] : body;
  auto weaponFor = [&](const UnitState &target) {
    return definitions_->units[target.type].flags & 4 ? guns.airWeapon : guns.groundWeapon;
  };
  auto legal = [&](const UnitState *target) {
    if (!target || target->dead || !target->visible)
      return false;
    int weapon = weaponFor(*target);
    return weapon >= 0 && weapon < int(definitions_->weapons.size());
  };
  auto movementRange = [&](const UnitState &target) {
    const auto &other = definitions_->units[target.type].bounds;
    const auto &shape = body.bounds;
    Point gap{
        std::max({0, target.position.x - other.left - actor.position.x - shape.right,
                  actor.position.x - shape.left - target.position.x - other.right}),
        std::max({0, target.position.y - other.top - actor.position.y - shape.bottom,
                  actor.position.y - shape.top - target.position.y - other.bottom})};
    gap.x = std::max(0, gap.x - 1); gap.y = std::max(0, gap.y - 1);
    const auto &weapon = definitions_->weapons[weaponFor(target)];
    const bool sameDirection = actor.motion && target.motion && target.motion->moving() &&
        std::abs(signedHeading(target.motion->acceptedTravelHeading() -
                              actor.motion->acceptedTravelHeading())) <= 32;
    int allowance = actor.motion && actor.motion->moving() && !sameDirection
                        ? int(actor.motion->stopDistance() / 256) : 0;
    int separation = distance(gap);
    return separation >= int(weapon.minimumRange) &&
           separation <= int(weapon.maximumRange) + allowance;
  };
  // Rank retained intent, a remembered aggressor, and ordinary acquisition
  // options together. Equal priorities preserve existing intent; only ordinary
  // candidates use center distance to resolve their ties.
  auto consider = [&](UnitHandle handle, int origin) {
    const auto *target = get(handle);
    if (!legal(target) || (origin && allied(actor.owner, target->owner)))
      return;
    int priority = attackPriority(actor, *target);
    if (origin < 2 && priority == 0 && !movementRange(*target))
      priority = 1;
    auto score = std::tuple{priority, origin,
        origin == 2 ? distance({target->position.x - actor.position.x,
                               target->position.y - actor.position.y}) : 0};
    if (!best || score < best->score)
      best = Choice{handle, score};
  };
  consider(actor.target, 0);
  auto *remembered = get(actor.retaliationTarget);
  if (legal(remembered) && !allied(actor.owner, remembered->owner))
    consider(actor.retaliationTarget, 1);
  else
    actor.retaliationTarget = {};
  for (auto candidate : targetCandidates(actor.handle, false))
    consider(candidate, 2);
  actor.target = best ? best->handle : UnitHandle{};
}
void World::processAttack(UnitState &unit) {
  if (unit.autoAttackTargeting)
    reacquireAttackTarget(unit);
  const auto *target = get(unit.target);
  if (!target || target->dead || !target->visible) {
    prepareOrderTransition(unit);
    brakeForCombat(unit, false);
    unit.target = {};
    unit.targetPosition = {};
    unit.order = definitions_->units[unit.type].returnIdle;
    unit.orderState = unit.orderTimer = unit.mainTimer = 0;
    activateQueuedCommand(unit);
    return;
  }
  unit.targetPosition = target->position;
  const auto &shape = definitions_->units[unit.type].bounds;
  const auto &otherType = definitions_->units[target->type];
  const auto &other = otherType.bounds;
  const auto &attackerType = definitions_->units[unit.type];
  const int weaponIndex = otherType.flags & 4 ? attackerType.airWeapon
                                            : attackerType.groundWeapon;
  const auto &weapon = definitions_->weapons.at(weaponIndex);
  Point gap{
      std::max({0, target->position.x - other.left - unit.position.x - shape.right,
                unit.position.x - shape.left - target->position.x - other.right}),
      std::max({0, target->position.y - other.top - unit.position.y - shape.bottom,
                unit.position.y - shape.top - target->position.y - other.bottom})};
  gap.x = std::max(0, gap.x - 1);
  gap.y = std::max(0, gap.y - 1);
  const int separation = distance(gap);
  const bool initial = unit.orderState == 0;
  if (initial) {
    const int remaining = otherType.flags & 4 ? unit.airCooldown
                                             : unit.groundCooldown;
    if (remaining > 0)
      unit.orderTimer = std::min(unit.orderTimer, remaining - 1);
  }
  const bool finishPointAdmission = !initial && unit.attackPointAdmission;
  if (!initial)
    unit.attackPointAdmission = false;
  const auto stopping = unit.motion->stopDistance();
  const bool followingDirection =
      target->motion && target->motion->moving() &&
      std::abs(signedHeading(target->motion->acceptedTravelHeading() -
                             unit.motion->acceptedTravelHeading())) <= 32;
  const int approachAllowance =
      unit.motion->moving() && !followingDirection ? int(stopping / 256) : 0;
  if (separation <= int(weapon.maximumRange) + approachAllowance) {
    const bool continuingStop = initial && unit.motion->braking();
    const bool admittedRoute = initial && unit.motion->hasRoute() &&
                                    (unit.motion->moving() ||
                                     unit.motion->collisionPaused()) &&
                                    !continuingStop;
    if (admittedRoute) {
      unit.attackPointAdmission = true;
      unit.motion->faceOnArrival(unit.targetPosition);
    }
    if (!initial)
      unit.followTimer = 15;
    if ((!initial && get(unit.moveTargetUnit)) || finishPointAdmission ||
        (initial && !continuingStop && !admittedRoute))
      brakeForCombat(unit, initial);
    unit.orderState = 1;
    if (separation >= int(weapon.minimumRange) &&
        separation <= int(weapon.maximumRange)) {
      int cooldown = otherType.flags & 4 ? unit.airCooldown : unit.groundCooldown;
      if (cooldown) {
        unit.orderTimer = std::min(unit.orderTimer, cooldown - 1);
        return;
      }
      if (unit.attackActive)
        return;
      if (unit.escapingOverlap)
        return;
      const int wanted = heading({target->position.x - unit.position.x,
                                  target->position.y - unit.position.y});
      if (std::abs(signedHeading(wanted - unit.sprite.context.heading)) > weapon.attackAngle) {
        unit.orderTimer = 0;
        return;
      }
      unit.groundCooldown = unit.airCooldown =
          std::max(0, weapon.cooldown + int(random_.next() & 3) - 1);
      unit.attackActive = true;
      unit.sprite.context.target = true;
      unit.sprite.context.targetHeading = wanted;
      unit.sprite.context.targetDistance = separation;
      unit.sprite.animate(otherType.flags & 4 ? 6 : 5, definitions_->resources(), random_,
                          [&](const auto &instruction, const auto &layer) {
                            return effect(unit.handle, instruction, layer);
                          });
    }
    return;
  }
  const bool retainedDestination =
      unit.moveTargetUnit == target->handle &&
      (samePoint(unit.moveTargetPosition, target->position) ||
       onTargetPerimeter(unit, *target));
  if (!initial && unit.followTimer == 0 && retainedDestination)
    unit.immovable = false;
  if (initial || (unit.followTimer == 0 && !retainedDestination)) {
    const int groundWeapon = attackerType.groundWeapon;
    const bool pointPursuit = !initial && groundWeapon >= 0 &&
        groundWeapon < int(definitions_->weapons.size()) &&
        definitions_->weapons[groundWeapon].graphics == 0 &&
        target->motion && target->motion->moving() &&
        (visibleAt({target->position.x / 32, target->position.y / 32}) &
         (1U << unit.owner));
    Point pursuitPoint = target->position;
    if (pointPursuit) {
      const auto projected = unit.motion->projectFollowTarget(
          target->position, target->motion->acceptedTravelHeading());
      if (projected.x >= 0 && projected.y >= 0 &&
          projected.x < map_->terrain.width() * 32 &&
          projected.y < map_->terrain.height() * 32)
        pursuitPoint = projected;
    }
    unit.moveTargetPosition = pursuitPoint;
    unit.moveTargetUnit = pointPursuit ? UnitHandle{} : target->handle;
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
  }
  unit.orderState = 1;
}
void World::meleeHit(UnitState &source) {
  weaponHit(definitions_->units[source.type].groundWeapon, source.target,
            source.sprite.context.heading, source.handle);
}
void World::respondToDamage(UnitState &victim, UnitHandle source) {
  const auto *attacker = get(source);
  if (!attacker || attacker->owner == victim.owner)
    return;
  victim.lastAttackingPlayer = attacker->owner;
  revealSightAt(attacker->position, victim.owner, 1,
                definitions_->units[attacker->type].flags & 4);
  if (allied(victim.owner, attacker->owner))
    return;
  auto armament = [&](const UnitState &unit, const UnitState &enemy) {
    const auto &type = definitions_->units[unit.type];
    const auto &gun = type.turret >= 0 && type.turret < 228
                          ? definitions_->units[type.turret] : type;
    return definitions_->units[enemy.type].flags & 4 ? gun.airWeapon : gun.groundWeapon;
  };
  auto canAttack = [&](const UnitState &unit, const UnitState &enemy) {
    int weapon = armament(unit, enemy);
    return !enemy.dead && enemy.visible && weapon >= 0 && weapon < 130;
  };
  auto inRange = [&](const UnitState &unit, const UnitState &enemy) {
    int index = armament(unit, enemy);
    if (index < 0 || index >= 130)
      return false;
    const auto &a = definitions_->units[unit.type].bounds;
    const auto &b = definitions_->units[enemy.type].bounds;
    Point gap{std::max({0, enemy.position.x - b.left - unit.position.x - a.right - 1,
                         unit.position.x - a.left - enemy.position.x - b.right - 1}),
              std::max({0, enemy.position.y - b.top - unit.position.y - a.bottom - 1,
                         unit.position.y - a.top - enemy.position.y - b.bottom - 1})};
    return distance(gap) <= int(definitions_->weapons[index].maximumRange);
  };
  auto prefersSource = [&](const UnitState &unit) {
    if (unit.retaliationTarget == source)
      return false;
    const auto *old = get(unit.retaliationTarget);
    return !old || !canAttack(unit, *old) || allied(unit.owner, old->owner) ||
           (inRange(unit, *attacker) && !inRange(unit, *old));
  };
  const auto &body = definitions_->units[victim.type];
  if (victim.type != 35 && prefersSource(victim)) {
    if (body.building() || (!(body.flags & 8) && canAttack(victim, *attacker)))
      victim.retaliationTarget = source;
    else if (victim.motion && (body.flags & 0x8000000U) &&
             victim.order >= 0 && victim.order < 189 &&
             definitions_->orders[victim.order].fleeOnHit) {
      auto vector = directionVector(heading({victim.position.x - attacker->position.x,
                                            victim.position.y - attacker->position.y}));
      Point destination = victim.position;
      int from = map_->regions.regionAt(victim.position);
      const auto &regions = map_->regions.entries();
      for (int extent = 192; extent >= 16; extent = extent == 192 ? 128 : extent / 2) {
        Point candidate{std::clamp(victim.position.x + fixedProduct(vector.x, extent),
                                   0, map_->terrain.width() * 32 - 1),
                        std::clamp(victim.position.y + fixedProduct(vector.y, extent),
                                   0, map_->terrain.height() * 32 - 1)};
        int to = map_->regions.regionAt(candidate);
        const bool entry = std::any_of(
            regions[to].walking.begin(), regions[to].walking.end(),
            [&](int neighbor) {
              return regions[neighbor].group == regions[from].group;
            });
        const bool reachable = (body.flags & 4) || victim.owner >= 8 || from == to ||
                               (entry && regions[from].group == regions[to].group);
        if (reachable) {
          destination = candidate;
          break;
        }
      }
      move(victim.handle, destination);
    }
  }
  // Nearby armed allies remember the aggressor without replacing their orders.
  for (int id : visible_) {
    auto &ally = *units_[id];
    const auto &type = definitions_->units[ally.type];
    const auto &bounds = type.bounds;
    if (ally.dead || ally.handle == victim.handle || ally.owner != victim.owner ||
        ally.type == 71 || (type.flags & 8) || !canAttack(ally, *attacker) ||
        ally.position.x + bounds.right < victim.position.x - 96 ||
        ally.position.x - bounds.left > victim.position.x + 96 ||
        ally.position.y + bounds.bottom < victim.position.y - 96 ||
        ally.position.y - bounds.top > victim.position.y + 96)
      continue;
    if (prefersSource(ally))
      ally.retaliationTarget = source;
  }
}
void World::updateDamageOverlay(UnitState &unit) {
  if (!unit.completed || unit.sprite.images.empty())
    return;
  auto &sprite = unit.sprite;
  const auto &base = sprite.images[sprite.main];
  const auto *offsets = definitions_->offsetTable(base.type, 1);
  if (!offsets || offsets->values().empty())
    return;
  std::vector<int> slots;
  const auto &points = offsets->values().front();
  for (int index = 0; index < int(points.size()); ++index)
    if (!samePoint(points[index], {127, 127})) {
      if (index >= 22)
        throw std::runtime_error("Damage offset has no flame image");
      slots.push_back(index);
    }
  const auto maximum = definitions_->units[unit.type].hitpoints;
  if (slots.empty() || !maximum)
    return;
  // Raw-HP sweeps give equally spaced visual transitions. Each usable
  // location has a small and a large stage; further damage saturates the images.
  const int stages = int(slots.size()) * 2;
  const auto quantum = std::max<std::int64_t>(
      1, ((std::int64_t(maximum) * 2 + 2) / 3) / (stages + 1));
  const int desired = int(std::clamp<std::int64_t>(
      (std::int64_t(maximum) - unit.vitals.hitpoints) / quantum, 0, stages));
  constexpr int small = 450, large = 472, end = 494;
  int level = 0;
  for (auto id : sprite.order) {
    const int image = sprite.images[id].type;
    level += image >= small && image < large ? 1
             : image >= large && image < end ? 2 : 0;
  }
  auto first = [&](int begin, int limit) -> std::optional<std::size_t> {
    for (auto id : sprite.order)
      if (sprite.images[id].type >= begin && sprite.images[id].type < limit)
        return id;
    return std::nullopt;
  };
  auto add = [&](int family, int index) {
    const auto offset = definitions_->offset(base.type, base.frame(), 1,
                                             index, base.flip);
    sprite.topOverlay(family + index, offset, definitions_->resources(), random_,
                   [&](const auto &instruction, const auto &image) {
                     return effect(unit.handle, instruction, image);
                   });
  };
  while (level != desired) {
    const bool increasing = level < desired;
    const int from = increasing ? small : large;
    if (auto image = first(from, from + 22)) {
      const int index = sprite.images[*image].type - from;
      sprite.detach(*image);
      add(increasing ? large : small, index);
    } else if (!increasing) {
      auto image = first(small, large);
      if (!image)
        throw std::logic_error("Damage image level is inconsistent");
      sprite.detach(*image);
    } else {
      const auto start = random_.next() % slots.size();
      std::optional<int> available;
      for (std::size_t offset = 0; offset < slots.size(); ++offset) {
        int index = slots[(start + offset) % slots.size()];
        if (!first(large + index, large + index + 1)) {
          available = index;
          break;
        }
      }
      if (!available)
        throw std::logic_error("Damage image locations are exhausted");
      add(small, *available);
    }
    level += increasing ? 1 : -1;
  }
}
void World::setHitpoints(UnitHandle handle, int amount) {
  auto *unit = get(handle);
  if (!unit || unit->dead)
    throw std::invalid_argument("Invalid hitpoint target");
  unit->vitals.hitpoints =
      std::clamp(amount, 0, int(definitions_->units[unit->type].hitpoints));
  updateDamageOverlay(*unit);
}
void World::weaponHit(int weaponIndex, UnitHandle destination, int impactHeading,
                      UnitHandle attacker) {
  auto *target = get(destination);
  if (!target || target->dead || target->vitals.hitpoints <= 0)
    return;
  const auto &weapon = definitions_->weapons.at(weaponIndex);
  const auto &type = definitions_->units[target->type];
  respondToDamage(*target, attacker);
  auto loss = weaponHitLoss(target->vitals, type.shields, weapon.damage * 256,
                           0, type.armor * 256, weapon.damageType, type.size);
  if (target->vitals.hitpoints == 0) {
    prepareOrderTransition(*target);
    target->order = target->orderState = 0;
    target->target = {};
    target->queuedCommands.clear();
    target->queuedUserOverride = false;
    target->attackActive = false;
    return;
  }
  updateDamageOverlay(*target);
  if (loss.shields && target->vitals.shields > 0) {
    const auto &base = target->sprite.images[target->sprite.main];
    const int facing = ((impactHeading & 255) + 4) / 8;
    const int impact = (facing + 16) % 32;
    const auto offset = definitions_->offset(base.type, base.frame(), 5, impact, base.flip);
    target->sprite.overlay(424, offset, impact, definitions_->resources(), random_,
                           [&](const auto &instruction, const auto &layer) {
                             return effect(target->handle, instruction, layer);
                           });
  }
}
} // namespace mapeditor::simulation
