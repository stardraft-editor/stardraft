// SPDX-License-Identifier: MIT
#include "native_world.h"

namespace maplab {
std::unique_ptr<mapeditor::simulation::World>
loadNativeWorld(const FileReader &read, const Bytes &scenario) {
  return loadNativeWorld(
      std::make_shared<mapeditor::simulation::Definitions>(
          [&](const std::string &name) { return read(name); }),
      read, scenario);
}
std::unique_ptr<mapeditor::simulation::World>
loadNativeWorld(std::shared_ptr<const mapeditor::simulation::Definitions> definitions,
                const FileReader &read, const Bytes &scenario) {
  namespace sim = mapeditor::simulation;
  auto document = sim::Scenario::read(scenario);
  auto terrain = document.terrain();
  static const char *names[]{"badlands", "platform", "install", "AshWorld",
                             "Jungle",   "Desert",   "Ice",     "Twilight"};
  auto prefix = std::string("tileset/") + names[terrain.tileset];
  auto map = sim::buildScenarioTerrain(terrain, read(prefix + ".cv5"),
                                        read(prefix + ".vf4"));
  auto world = std::make_unique<sim::World>(definitions, map, 42,
                                            sim::WorldStart::MapImport);
  for (const auto &sprite : document.sprites()) {
    if (sprite.flags & 0x1000) {
      if (sprite.type == 65535)
        continue;
      world->addSprite(sprite.type, {sprite.x, sprite.y}, sprite.owner);
      continue;
    }
    int owner = sprite.type >= 205 && sprite.type <= 208 ? 11 : sprite.owner;
    if (owner != 11)
      continue;
    auto handle = world->createFinished(sprite.type, {sprite.x, sprite.y},
                                        owner, true, true);
    if (auto *unit = world->get(handle))
      unit->order = owner < 8 ? 22 : definitions->units[sprite.type].humanIdle;
    else
      throw std::runtime_error("Cannot import map sprite-unit");
  }
  for (const auto &source : document.units()) {
    if (source.type == 65535)
      continue;
    if (source.type < 0 || source.type >= 228 || source.owner < 0 ||
        source.owner >= 12)
      throw std::out_of_range("Invalid map unit type or owner");
    if (source.type == 214)
      continue;
    bool resource =
        (source.type >= 176 && source.type <= 178) || source.type == 188;
    bool critter = source.type == 89 || source.type == 90 ||
                   (source.type >= 93 && source.type <= 96);
    if (source.owner != 11 && !resource && !critter)
      continue;
    auto handle = world->createFinished(source.type, {source.x, source.y},
                                        source.owner, true, true);
    auto *unit = world->get(handle);
    if (!unit)
      continue;
    if (unit->dead)
      throw std::runtime_error(
          "Cannot place imported map unit type " + std::to_string(source.type) +
          " at " + std::to_string(source.x) + "," + std::to_string(source.y));
    unit->order =
        source.owner < 8 ? 22 : definitions->units[source.type].humanIdle;
    if (source.validProperties & 0x10)
      world->setResources(handle, int(source.resources));
    if ((source.validState & source.state & 8) != 0)
      world->setHallucinated(handle);
  }
  world->prepareImportedMap();
  return world;
}
} // namespace maplab
