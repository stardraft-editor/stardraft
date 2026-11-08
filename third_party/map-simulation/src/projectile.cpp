// SPDX-License-Identifier: MIT
#include <mapeditor/simulation/world.h>

namespace mapeditor::simulation {
void World::useWeapon(UnitState &source, int weapon) {
  const auto &definition = definitions_->weapons.at(weapon);
  if (definition.graphics == definitions_->flingies.size())
    return;
  const bool homing = definition.behavior == 1;
  if ((!homing && definition.behavior != 2 && definition.behavior != 5) ||
      (definition.hitType != 0 && definition.hitType != 1) || definition.headingOffset != 0)
    throw std::runtime_error("Unsupported projectile behavior");
  const auto &physics = definitions_->flingies.at(definition.graphics);
  if (homing && definition.graphics != 160)
    throw std::runtime_error("Unimplemented homing projectile movement profile");
  auto resources = definitions_->resources();
  for (int shot = 0; shot < (definition.projectileCount == 2 ? 2 : 1); ++shot) {
    int id = 0;
    while (id < int(projectiles_.size()) && projectiles_[id])
      ++id;
    if (id == int(projectiles_.size()))
      return;
    auto &projectile = projectiles_[id].emplace();
    projectile.weapon = weapon;
    projectile.owner = source.owner;
    projectile.source = source.handle;
    projectile.target = source.target;
    auto *target = get(source.target);
    projectile.position = source.targetPosition;
    if (definition.behavior == 2 && target) {
      const auto &bounds = definitions_->units[target->type].bounds;
      projectile.position = projectileImpactPoint(
          source.position, {target->position.x - bounds.left,
                            target->position.y - bounds.top},
                           {target->position.x + bounds.right,
                            target->position.y + bounds.bottom});
    }
    if (homing) {
      const auto forward = directionVector(source.sprite.context.heading);
      projectile.position = {
          source.position.x + fixedProduct(forward.x, definition.forwardOffset),
          source.position.y + fixedProduct(forward.y, definition.forwardOffset) -
              definition.upwardOffset};
      auto &flight = projectile.flight.emplace();
      flight.raw = {projectile.position.x * 256, projectile.position.y * 256};
      flight.goal = target ? target->position : source.targetPosition;
      flight.travelHeading = source.sprite.context.heading;
      flight.turnRate = physics.turnRate;
      flight.lifetime = definition.lifetime;
    }
    projectile.sprite.sceneKey = spriteRows_.create(projectile.position);
    projectile.elevation =
        definitions_->units[target ? target->type : source.type].elevation + 1;
    projectile.sprite.context = source.sprite.context;
    try {
      projectile.sprite.initialize(
          definitions_->sprites.at(physics.sprite).image, resources, random_,
          [&](const auto &instruction, const auto &layer) {
            return definition.hitType == 0
                       ? effect(source.handle, instruction, layer)
                       : projectileEffect(id, instruction, layer);
          });
      source.sprite.context = projectile.sprite.context;
      projectile.sprite.setHeading(source.sprite.context.heading);
      auto &context = projectile.sprite.context;
      context.unitContext = false;
      context.flingyContext = true;
      context.target = context.resourceContainer = false;
      context.signal = 0;
      int missThreshold = 0;
      if (target && !(definitions_->units[source.type].flags & 4) &&
          !(definitions_->units[target->type].flags & 4) &&
          map_->terrain.groundHeight(target->position) >
              map_->terrain.groundHeight(source.position))
        missThreshold = 119;
      projectile.missed = (homing || definition.behavior == 2) && target &&
                          int(random_.next() & 255) <= missThreshold;
      if (projectile.missed) {
        auto vector = directionVector(context.heading);
        if (projectile.flight) {
          projectile.flight->goal.x -= fixedProduct(vector.x, 30);
          projectile.flight->goal.y -= fixedProduct(vector.y, 30);
        } else {
          projectile.position.x -= fixedProduct(vector.x, 30);
          projectile.position.y -= fixedProduct(vector.y, 30);
        }
      }
      spriteRows_.move(projectile.sprite.sceneKey, projectile.position);
      auto at = activeProjectiles_.begin();
      if (at != activeProjectiles_.end())
        ++at;
      activeProjectiles_.insert(at, id);
    } catch (...) {
      spriteRows_.remove(projectile.sprite.sceneKey);
      projectiles_[id].reset();
      throw;
    }
  }
}
iscript::HostAction
World::projectileEffect(int id, const iscript::Instruction &instruction,
                        const SpriteImage &layer) {
  const auto &projectile = *projectiles_[id];
  if (spawnEffect(instruction, layer, projectile.position,
                  projectile.elevation))
    return iscript::HostAction::Continue;
  if (instruction.opcode == 27 || instruction.opcode == 48) {
    const auto hit = definitions_->weapons[projectile.weapon].hitType;
    if (hit == 1 && !projectile.missed)
      weaponHit(projectile.weapon, projectile.target, projectile.sprite.context.heading,
                projectile.source);
    if (hit == 0 || hit == 1)
      return iscript::HostAction::Continue;
  }
  throw std::runtime_error("Unimplemented projectile animation effect " +
                           std::to_string(instruction.opcode));
}
void World::advanceProjectiles() {
  auto resources = definitions_->resources();
  for (auto at = activeProjectiles_.begin(); at != activeProjectiles_.end();) {
    int id = *at++;
    auto &projectile = *projectiles_[id];
    auto host = [&](const auto &instruction, const auto &layer) {
      return projectileEffect(id, instruction, layer);
    };
    projectile.sprite.tick(resources, random_, host);
    if (projectile.flight && !projectile.dying) {
      auto &flight = *projectile.flight;
      const auto &physics = definitions_->flingies[
          definitions_->weapons[projectile.weapon].graphics];
      if (!flight.active && (projectile.sprite.context.signal & 1)) {
        projectile.sprite.context.signal &= ~1;
        projectile.sprite.animate(2, resources, random_, host);
        flight.active = true;
      }
      if (flight.active) {
        if (!projectile.missed)
          if (const auto *target = get(projectile.target); target && !target->dead)
            if (!samePoint(flight.goal, target->position)) {
              flight.goal = target->position;
              flight.turning = true;
            }
        const int desired = heading({flight.goal.x - projectile.position.x,
                                     flight.goal.y - projectile.position.y});
        // This projectile's accelerating spin crosses the signed direction
        // boundary independently of its travel direction.
        const int cap = signedHeading(flight.turnRate);
        const int facing = projectile.sprite.context.heading;
        const int travelLimit = std::max(1, flight.turnRate / 2);
        if (flight.turning) {
          projectile.sprite.setHeading(
              cap >= 0 ? turnToward(facing, desired, cap)
                       : signedHeading(facing +
                           (signedHeading(desired - facing) > cap ? cap : -cap)));
          ++flight.turnRate;
        }
        flight.travelHeading = turnToward(flight.travelHeading, desired, travelLimit);
        if (projectile.sprite.context.heading == desired &&
            flight.travelHeading == desired)
          flight.turning = false;
        flight.speed = std::min(int(physics.speed), flight.speed + physics.acceleration);
        const auto direction = directionVector(flight.travelHeading);
        flight.velocity = {fixedProduct(flight.speed, direction.x),
                           fixedProduct(flight.speed, direction.y)};
        const Point destination{flight.goal.x * 256, flight.goal.y * 256};
        const bool arrived = distance({destination.x - flight.raw.x,
                                       destination.y - flight.raw.y}) <= flight.speed;
        if (arrived)
          flight.raw = destination;
        else {
          flight.raw.x += flight.velocity.x;
          flight.raw.y += flight.velocity.y;
        }
        projectile.position = {flight.raw.x / 256, flight.raw.y / 256};
        const Point bounded{std::clamp(projectile.position.x, 0, map_->terrain.width() * 32 - 1),
                            std::clamp(projectile.position.y, 0, map_->terrain.height() * 32 - 1)};
        const bool outside = !samePoint(bounded, projectile.position);
        if (outside) {
          projectile.position = bounded;
          flight.raw = {bounded.x * 256, bounded.y * 256};
        }
        spriteRows_.move(projectile.sprite.sceneKey, projectile.position);
        if (arrived || outside || flight.lifetime-- <= 0) {
          projectile.dying = true;
          projectile.sprite.animate(1, resources, random_, host);
        }
      }
    } else if (!projectile.dying && (projectile.sprite.context.signal & 1)) {
      projectile.sprite.context.signal &= ~1;
      projectile.dying = true;
      projectile.sprite.animate(1, resources, random_, host);
    }
    if (projectile.sprite.order.empty()) {
      activeProjectiles_.remove(id);
      spriteRows_.remove(projectile.sprite.sceneKey);
      projectiles_[id].reset();
    }
  }
}
} // namespace mapeditor::simulation
