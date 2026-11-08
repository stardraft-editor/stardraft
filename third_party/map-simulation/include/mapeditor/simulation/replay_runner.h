// SPDX-License-Identifier: MIT
#pragma once
#include "replay_selection.h"
#include <algorithm>
#include <vector>
namespace mapeditor::simulation::replay {
// Applies commands before their frame's world update. Replay/world must outlive
// the runner. Unsupported effects fail explicitly; a failed runner cannot
// resume.
class ClassicRunner {
public:
  ClassicRunner(const Replay &recording, World &world)
      : replay_(recording), world_(world), actors_(recording.metadata()) {
    if (recording.format() != Format::Legacy || world.frame() != 0)
      throw std::invalid_argument(
          "Classic runner requires frame-zero legacy state");
  }
  void advanceTo(int frame) {
    if (failed_ || frame < world_.frame() ||
        std::uint32_t(frame) > replay_.metadata().frames)
      throw std::invalid_argument("Invalid native replay advance");
    try {
      while (world_.frame() < frame) {
        for (const auto &block : replay_.blocksAt(world_.frame())) {
          ClassicCommands commands(replay_.payload(block));
          while (auto command = commands.next()) {
            auto owner = actors_.resolve(command->player);
            if (!owner || *owner >= 8)
              throw std::runtime_error("Unsupported replay actor");
            switch (command->code) {
            case ClassicOpcode::Build:
              if (selection_.selection(*owner).size() == 1) {
                auto handle = selection_.selection(*owner).front();
                const auto *unit = world_.get(handle);
                if (unit && !unit->dead && unit->owner == *owner) {
                  const auto &build = std::get<BuildCommand>(command->payload);
                  world_.build(handle, build.unitType,
                               {build.tileX, build.tileY}, build.order);
                }
              }
              break;
            case ClassicOpcode::Select:
            case ClassicOpcode::AddSelection:
            case ClassicOpcode::RemoveSelection:
            case ClassicOpcode::ControlGroup:
              selection_.apply(*owner, *command, selectionLookup(world_));
              break;
            case ClassicOpcode::Train:
              if (selection_.selection(*owner).size() == 1) {
                auto handle = selection_.selection(*owner).front();
                auto *unit = world_.get(handle);
                if (unit && !unit->dead && unit->owner == *owner)
                  world_.train(handle,
                               std::get<WordCommand>(command->payload).value);
              }
              break;
            case ClassicOpcode::CancelConstruction:
              if (selection_.selection(*owner).size() == 1) {
                auto handle = selection_.selection(*owner).front();
                const auto *unit = world_.get(handle);
                if (unit && !unit->dead && unit->owner == *owner)
                  world_.cancelConstruction(handle);
              }
              break;
            case ClassicOpcode::CancelMorph:
              for (auto handle : selection_.selection(*owner)) {
                const auto *unit = world_.get(handle);
                if (!unit || unit->dead || unit->owner != *owner)
                  break;
                world_.cancelConstruction(handle);
              }
              break;
            case ClassicOpcode::CancelQueue:
              if (selection_.selection(*owner).size() == 1) {
                auto handle = selection_.selection(*owner).front();
                const auto *unit = world_.get(handle);
                if (unit && !unit->dead && unit->owner == *owner)
                  world_.cancelQueue(
                      handle, std::get<WordCommand>(command->payload).value);
              }
              break;
            case ClassicOpcode::Morph:
              for (auto handle : selection_.selection(*owner)) {
                auto *unit = world_.get(handle);
                if (unit && !unit->dead && unit->owner == *owner)
                  world_.morph(handle,
                               std::get<WordCommand>(command->payload).value);
              }
              break;
            case ClassicOpcode::RightClick:
              rightClick(*owner, std::get<TargetCommand>(command->payload));
              break;
            case ClassicOpcode::ReturnCargo:
              for (auto handle : selection_.selection(*owner)) {
                const auto *unit = world_.get(handle);
                if (unit && !unit->dead && unit->visible && unit->completed &&
                    unit->motion && unit->owner == *owner &&
                    (world_.definitions().units[unit->type].flags & 8))
                  world_.returnCargo(
                      handle, std::get<ByteCommand>(command->payload).value != 0);
              }
              break;
            case ClassicOpcode::Order: {
              const auto &target = std::get<TargetCommand>(command->payload);
              if (target.order == 8 || target.order == 10) {
                const auto destination = ClassicUnitId(target.unit).resolve(world_);
                if (world_.get(destination)) {
                  for (auto member : selection_.selection(*owner)) {
                    const auto *unit = world_.get(member);
                    if (unit && !unit->dead && unit->visible && unit->completed && unit->owner == *owner) {
                      world_.get(member)->userMovementPending = true;
                      world_.attack(member, destination, target.queue);
                    }
                  }
                } else {
                  if (target.order == 10)
                    throw std::runtime_error("Missing explicit AttackUnit target is not implemented yet");
                  if (target.unitType != 228)
                    throw std::runtime_error("Remembered explicit attack target is not implemented yet");
                  rightClick(*owner, target, true, true);
                }
                break;
              }
              if (target.order == 14) {
                rightClick(*owner, target, true);
                break;
              }
              if (target.order != 39 && target.order != 40)
                throw std::runtime_error("Unsupported native replay order " +
                                         std::to_string(target.order) +
                                         " at frame " +
                                         std::to_string(world_.frame()));
              for (auto member : selection_.selection(*owner)) {
                const auto *unit = world_.get(member);
                if (!unit || unit->dead || unit->owner != *owner)
                  continue;
                if (target.order == 40)
                  world_.rallyPoint(member, {target.x, target.y});
                else
                  world_.rallyUnit(member,
                                   ClassicUnitId(target.unit).resolve(world_));
              }
              break;
            }
            default:
              throw std::runtime_error("Unsupported native replay command " +
                                       std::to_string(int(command->code)) +
                                       " at frame " +
                                       std::to_string(world_.frame()));
            }
            ++commands_;
          }
        }
        world_.advance();
        for (int owner = 0; owner < 8; ++owner) {
          auto selected = selection_.selection(owner);
          for (auto handle : selected) {
            const auto *unit = world_.get(handle);
            if (!unit || unit->dead)
              selection_.deselect(handle);
          }
        }
      }
    } catch (...) {
      failed_ = true;
      throw;
    }
  }
  std::size_t commands() const { return commands_; }
  const ClassicSelection &selections() const { return selection_; }

private:
  void rightClick(int owner, const TargetCommand &target, bool assault = false,
                  bool defaultAttack = false) {
    if (target.x >= world_.map().terrain.width() * 32 ||
        target.y >= world_.map().terrain.height() * 32)
      return;
    auto handle = assault ? UnitHandle{} : ClassicUnitId(target.unit).resolve(world_);
    const auto *mineral = world_.get(handle);
    bool remembered = !assault && target.unitType != 228;
    if (!mineral && remembered &&
        (world_.visibleAt({target.x / 32, target.y / 32}) & (1U << owner))) {
      remembered = false;
      handle = world_.rememberedTarget(owner, {target.x, target.y}, target.unitType);
      mineral = world_.get(handle);
    }
    bool mobile = false;
    for (auto member : selection_.selection(owner)) {
      const auto *unit = world_.get(member);
      if (!unit || unit->dead || unit->owner != owner || !unit->visible)
        continue;
      if (!assault && world_.canRally(member)) {
        if (mineral && !mineral->dead)
          world_.rallyUnit(member, handle);
        else
          world_.rallyPoint(member, {target.x, target.y});
      } else if (world_.definitions().units[unit->type].rightClickAction) {
        mobile = true;
        if (unit->completed && unit->motion && handle != member)
          world_.get(member)->userMovementPending = true;
      }
    }
    if (!mobile)
      return;
    if (!mineral) {
      if (remembered) {
        for (auto member : selection_.selection(owner)) {
          const auto *unit = world_.get(member);
          if (unit && !unit->dead && unit->visible && unit->completed &&
              unit->owner == owner && unit->motion &&
              world_.definitions().units[unit->type].rightClickAction)
            world_.rememberedRightClick(member, {target.x, target.y},
                                        target.unitType, target.queue);
        }
        return;
      }
      std::vector<UnitHandle> participants;
      std::vector<OrderGroupMember> footprints;
      const auto &selected = selection_.selection(owner);
      for (auto member : selected) {
        const auto *unit = world_.get(member);
        if (unit && !unit->dead && unit->visible) {
          const auto &definition = world_.definitions().units[unit->type];
          participants.push_back(member);
          footprints.push_back({unit->position, definition.bounds,
                                !(definition.flags & 4)});
        }
      }
      auto projected = projectGroupOrderTargets(
          world_.map().terrain, world_.map().contours, world_.map().regions,
          footprints, {target.x, target.y});
      if (!projected)
        throw std::runtime_error("Unsupported native replay group terrain target");
      for (std::size_t index = 0; index < participants.size(); ++index) {
        auto member = participants[index];
        const auto *unit = world_.get(member);
        if (!unit || unit->dead || !unit->visible || unit->owner != owner ||
            !unit->completed)
          continue;
        const auto &type = world_.definitions().units[unit->type];
        if (!type.rightClickAction)
          continue;
        if (!unit->motion)
          throw std::runtime_error(
              "Unsupported native replay right-click movement");
        Point destination = projected->destinations[index];
        if (assault) {
          const int order = defaultAttack ? type.attackMove : 14;
          if (order == 2)
            world_.guard(member, destination, target.queue);
          else if (order == 14)
            world_.attackMove(member, destination, target.queue);
          else
            throw std::runtime_error("Default ground attack order " +
                                     std::to_string(order) +
                                     " is not implemented yet");
        }
        else
          world_.move(member, destination, target.queue);
      }
      return;
    }
    if (mineral->visible && !world_.allied(owner, mineral->owner) && mineral->type < 176) {
      for (auto member : selection_.selection(owner)) {
        const auto *unit = world_.get(member);
        if (unit && !unit->dead && unit->visible && unit->completed &&
            unit->owner == owner && unit->motion &&
            world_.definitions().units[unit->type].rightClickAction)
          world_.attack(member, handle, target.queue);
      }
      return;
    }
    const bool resourceTarget = mineral->visible &&
                                (mineral->type == 188 ||
                                 (mineral->type >= 176 && mineral->type <= 178));
    const bool friendlyTarget = mineral->visible &&
                                world_.allied(owner, mineral->owner) &&
                                mineral->type < 176;
    if (target.queue && !resourceTarget && !friendlyTarget)
      throw std::runtime_error("Unsupported native replay queued unit target");
    if (mineral->visible && mineral->type == 188) {
      for (auto member : selection_.selection(owner)) {
        const auto *unit = world_.get(member);
        if (!unit || unit->dead || !unit->visible || !unit->completed ||
            unit->owner != owner)
          continue;
        const auto &type = world_.definitions().units[unit->type];
        if (type.rightClickAction == 1) {
          world_.follow(member, handle, target.queue);
          continue;
        }
        if (!(type.flags & 8))
          throw std::runtime_error("Non-worker gas target actions are not implemented");
        world_.harvest(member, handle, HarvestOrder::Gas, target.queue);
      }
      return;
    }
    if (friendlyTarget) {
      for (auto member : selection_.selection(owner)) {
        const auto *unit = world_.get(member);
        if (!unit || unit->dead || !unit->visible || !unit->completed ||
            unit->owner != owner)
          continue;
        const auto &type = world_.definitions().units[unit->type];
        if (!type.rightClickAction)
          continue;
        if (!(type.flags & 8) && type.rightClickAction != 1)
          throw std::runtime_error(
              "Unsupported native replay friendly-unit action");
        const auto &destination = world_.definitions().units[mineral->type];
        if ((type.rightClickAction == 4 || type.rightClickAction == 5) &&
            (type.flags & 8) && (unit->cargo & 3) && mineral->owner == owner &&
            mineral->completed && (destination.flags & 0x1000U))
          world_.returnCargo(member, target.queue, handle);
        else {
          if (type.rightClickAction == 5 && destination.building() &&
              (!mineral->completed || mineral->vitals.hitpoints < destination.hitpoints))
            throw std::runtime_error("Native construction/repair right-click is not implemented yet");
          world_.follow(member, handle, target.queue);
        }
      }
      return;
    }
    if (!mineral->visible || mineral->type < 176 || mineral->type > 178 ||
        mineral->resources <= 0)
      throw std::runtime_error(
          "Unsupported native replay right-click target or queue");
    for (auto worker : selection_.selection(owner)) {
      auto *unit = world_.get(worker);
      if (!unit || unit->dead || unit->owner != owner)
        continue;
      if (!world_.definitions().units[unit->type].rightClickAction)
        continue;
      if (unit->completed && unit->visible && unit->motion &&
          world_.definitions().units[unit->type].rightClickAction == 1) {
        world_.follow(worker, handle, target.queue);
        continue;
      }
      if (!unit->completed || !unit->visible || !unit->motion ||
          !(world_.definitions().units[unit->type].flags & 8))
        throw std::runtime_error("Unsupported native replay right-click unit");
      world_.harvest(worker, handle, HarvestOrder::Minerals, target.queue);
    }
    return;
  }
  const Replay &replay_;
  World &world_;
  ClassicActors actors_;
  ClassicSelection selection_;
  std::size_t commands_ = 0;
  bool failed_ = false;
};
} // namespace mapeditor::simulation::replay
