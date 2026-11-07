// SPDX-License-Identifier: MIT
#include <mapeditor/simulation/replay_runner.h>
#include <source_location>
#include <stdexcept>
using namespace mapeditor::simulation;
namespace {
void check(bool value,
           std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Production test failed at line " +
                             std::to_string(at.line()));
}
void append32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (int n = 0; n < 4; ++n)
    out.push_back(std::uint8_t(value >> (n * 8)));
}
void section(std::vector<std::uint8_t> &out,
             const std::vector<std::uint8_t> &data) {
  std::uint32_t crc = 0xffffffffu;
  for (auto byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
  }
  append32(out, crc);
  append32(out, data.empty() ? 0 : 1);
  if (!data.empty()) {
    append32(out, std::uint32_t(data.size()));
    out.insert(out.end(), data.begin(), data.end());
  }
}
replay::Replay recording(UnitHandle producer, bool unsupported, bool cancel = false) {
  std::vector<std::uint8_t> out, header(633), commands;
  header[1] = 8;
  auto wire = replay::ClassicUnitId::fromHandle(producer).raw();
  append32(commands, 0);
  commands.push_back(9);
  commands.insert(commands.end(), {0, 9, 1, std::uint8_t(wire),
                                   std::uint8_t(wire >> 8), 0, 31, 7, 0});
  if (unsupported) {
    append32(commands, 1);
    commands.insert(commands.end(), {8, 0, 31, 7, 0, 0, 53, 41, 0});
  }
  if (cancel) {
    append32(commands, 1);
    commands.insert(commands.end(), {4, 0, 32, 254, 0});
  }
  section(out, {'r', 'e', 'R', 'S'});
  section(out, header);
  std::vector<std::uint8_t> size;
  append32(size, std::uint32_t(commands.size()));
  section(out, size);
  section(out, commands);
  size.clear();
  append32(size, 0);
  section(out, size);
  section(out, {});
  return replay::Replay::read(out);
}
replay::Replay cancelBuildingRecording(UnitHandle building, std::uint8_t opcode) {
  std::vector<std::uint8_t> out, header(633), commands;
  header[1] = 8;
  const auto wire = replay::ClassicUnitId::fromHandle(building).raw();
  append32(commands, 0);
  commands.insert(commands.end(), {7, 0, 9, 1, std::uint8_t(wire),
                                   std::uint8_t(wire >> 8), 0, opcode});
  append32(commands, 1);
  commands.insert(commands.end(), {2, 0, opcode});
  section(out, {'r', 'e', 'R', 'S'});
  section(out, header);
  std::vector<std::uint8_t> size;
  append32(size, std::uint32_t(commands.size()));section(out, size);
  section(out, commands);size.clear();append32(size, 0);
  section(out, size);section(out, {});
  return replay::Replay::read(out);
}
replay::Replay clickRecording(std::span<const UnitHandle> selected,
                              UnitHandle target, Point point, bool queued) {
  std::vector<std::uint8_t> out, header(633), payload{0, 9};
  header[1] = 8;
  payload.push_back(std::uint8_t(selected.size()));
  auto word = [&](std::uint16_t value) {
    payload.push_back(std::uint8_t(value));
    payload.push_back(std::uint8_t(value >> 8));
  };
  for (auto handle : selected)
    word(replay::ClassicUnitId::fromHandle(handle).raw());
  payload.insert(payload.end(), {0, 20});
  word(std::uint16_t(point.x));
  word(std::uint16_t(point.y));
  word(replay::ClassicUnitId::fromHandle(target).raw());
  word(228);
  payload.push_back(queued ? 1 : 0);
  std::vector<std::uint8_t> commands;
  append32(commands, 0);
  commands.push_back(std::uint8_t(payload.size()));
  commands.insert(commands.end(), payload.begin(), payload.end());
  section(out, {'r', 'e', 'R', 'S'});
  section(out, header);
  std::vector<std::uint8_t> size;
  append32(size, std::uint32_t(commands.size()));
  section(out, size);
  section(out, commands);
  size.clear();
  append32(size, 0);
  section(out, size);
  section(out, {});
  return replay::Replay::read(out);
}
void testZergConstruction(const Definitions &seed,
                          std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  std::vector<std::uint8_t> bytes(108);
  auto word = [&](std::size_t at, int value) {
    bytes[at] = std::uint8_t(value);
    bytes[at + 1] = std::uint8_t(value >> 8);
  };
  int idle = int(bytes.size());
  bytes.insert(bytes.end(),
               {5, 1, 7, std::uint8_t(idle), std::uint8_t(idle >> 8)});
  int finish = int(bytes.size());
  bytes.insert(bytes.end(),
               {36, 4, 5, 1, 7, std::uint8_t(idle), std::uint8_t(idle >> 8)});
  for (int script = 0; script < 2; ++script) {
    int at = 4 + script * 52;
    for (int i = 0; i < 4; ++i)
      bytes[at + i] = std::uint8_t("SCPE"[i]);
    word(at + 4, 20);
    for (int animation = 0; animation < 22; ++animation)
      word(at + 8 + 2 * animation, idle);
  }
  word(56 + 8 + 2 * 15, finish);
  int table = int(bytes.size());
  for (int i = 0; i < 4; ++i)
    bytes[i] = std::uint8_t(table >> (8 * i));
  bytes.resize(bytes.size() + 12);
  word(table, 0);
  word(table + 2, 4);
  word(table + 4, 1);
  word(table + 6, 56);
  word(table + 8, 65535);
  data->program = iscript::Program(bytes);
  for (int image = 0; image < 2; ++image) {
    data->images[image].script = image;
    data->images[image].animated = true;
  }
  data->flingies[0] = {0, 1280, 12227, 67, 40, 0};
  data->sprites[0].image = 0;
  auto &drone = data->units[41];
  drone.graphics = 0;
  drone.flags = 0x8000008U;
  drone.groupFlags = 1;
  drone.movementFlags = 193;
  drone.bounds = {11, 11, 11, 11};
  drone.humanIdle = drone.returnIdle = 3;
  drone.sightRange = 7;
  drone.hitpoints = 40 * 256;
  auto &hatchery = data->units[131];
  hatchery.graphics = 0;
  hatchery.flags = 1;
  hatchery.groupFlags = 1;
  hatchery.bounds = {49, 32, 49, 32};
  hatchery.placementWidth = 128;
  hatchery.placementHeight = 96;
  hatchery.humanIdle = hatchery.returnIdle = 23;
  hatchery.constructionImage = 1;
  hatchery.hitpoints = 1000 * 256;
  hatchery.buildTime = 8;
  hatchery.mineralCost = 300;
  hatchery.gasCost = 0;
  auto &pool = data->units[142];
  pool = hatchery;
  pool.flags = 0x20001U;
  pool.hitpoints = 500 * 256;
  pool.mineralCost = 200;
  pool.placementWidth = pool.placementHeight = 64;
  pool.bounds = {16, 16, 16, 16};
  data->units[132] = data->units[133] = hatchery;
  {
    auto gasData = std::make_shared<Definitions>(*data);
    auto &extractor = gasData->units[149];
    extractor = hatchery;
    extractor.placementHeight = 64;
    extractor.bounds = {63, 31, 63, 31};
    extractor.mineralCost = 50;
    gasData->units[188] = extractor;
    gasData->units[188].groupFlags = 0;
    gasData->units[188].flags |= 0x2000U;
    World gasWorld(gasData, map);
    auto vent = gasWorld.createFinished(188, {800, 800}, 11, false);
    gasWorld.get(vent)->resources = 5000;
    auto builder = gasWorld.createFinished(41, {900, 900}, 0, false);
    gasWorld.economy().set(0, {500, 0});
    check(!gasWorld.build(builder, 149, {24, 24}, 25));
    check(gasWorld.build(builder, 149, {23, 24}, 25));
    check(gasWorld.get(builder)->order == 25 &&
          gasWorld.get(builder)->buildQueue == std::vector<int>{149} &&
          gasWorld.economy().resources(0).minerals == 500 &&
          gasWorld.get(vent)->type == 188 &&
          gasWorld.get(vent)->owner == 11 &&
          gasWorld.get(vent)->resources == 5000);
    auto retained = gasWorld;
    check(!gasWorld.build(builder, 149, {23, 22}, 25) &&
          retained.get(builder)->order == 25 &&
          samePoint(retained.get(builder)->targetPosition, {800, 800}));
    auto landing = std::make_unique<World>(gasData, map);
    auto landingVent = landing->createFinished(188, {800, 800}, 11, false);
    landing->get(landingVent)->resources = 5000;
    auto arriving = landing->createFinished(41, {875, 843}, 0, false);
    landing->economy().set(0, {500, 0});
    check(landing->build(arriving, 149, {23, 24}, 25));
    for (int tick = 0; tick < 100 &&
                       !(landing->get(arriving)->order == 70 &&
                         landing->get(arriving)->orderState == 2); ++tick)
      landing->advance();
    check(landing->get(arriving)->order == 70 &&
          landing->get(arriving)->orderState == 2);
    const auto departure = landing->get(arriving)->motion->rawPosition();
    auto parked = std::make_unique<World>(*landing);
    landing->advance(5);
    check(!samePoint(landing->get(arriving)->motion->rawPosition(), departure) &&
          samePoint(parked->get(arriving)->motion->rawPosition(), departure));
    for (int tick = 0; tick < 200 && landing->get(arriving)->order != 26; ++tick)
      landing->advance();
    check(landing->get(arriving)->order == 26);
    landing->get(arriving)->vitals.hitpoints = 18 * 256;
    auto uncommitted = std::make_unique<World>(*landing);
    landing->advance();
    check(landing->get(landingVent)->type == 149 &&
          landing->get(landingVent)->owner == 0 &&
          !landing->get(landingVent)->completed &&
          landing->get(landingVent)->resources == 5000 &&
          landing->get(arriving)->type == 41 &&
          landing->get(arriving)->order == 0 &&
          !landing->get(arriving)->dead &&
          landing->economy().resources(0).minerals == 450 &&
          uncommitted->get(landingVent)->type == 188 &&
          uncommitted->get(arriving)->order == 26);
    landing->advance();
    check(!landing->get(arriving) || landing->get(arriving)->dead);
    auto cancellation = std::make_unique<World>(*landing);
    auto *spentBuilder = cancellation->get(arriving);
    check(spentBuilder && spentBuilder->dead && spentBuilder->motion);
    spentBuilder->motion.emplace(spentBuilder->position, 0, gasData->flingies[0]);
    spentBuilder->motion->inheritInertia({201, 0, {}});
    cancellation->advance();
    check(!cancellation->get(arriving));
    cancellation->createFinished(188, {200, 200}, 11, false);
    auto cancellationBefore = std::make_unique<World>(*cancellation);
    check(cancellation->cancelConstruction(landingVent));
    const auto *restoredGas = cancellation->get(landingVent);
    check(restoredGas && restoredGas->type == 188 && restoredGas->owner == 11 &&
          restoredGas->completed && restoredGas->visible && !restoredGas->dead &&
          restoredGas->resources == 5000 &&
          cancellation->economy().resources(0).minerals == 487);
    UnitHandle returnedDrone;
    int droneCount = 0;
    for (int id : cancellation->visibleUnits()) {
      const auto &unit = *cancellation->units()[id];
      if (unit.type == 41 && unit.owner == 0 && !unit.dead) {
        returnedDrone = unit.handle;
        ++droneCount;
      }
    }
    check(droneCount == 1 && cancellation->get(returnedDrone)->completed &&
          returnedDrone.index == arriving.index && returnedDrone != arriving &&
          cancellation->get(returnedDrone)->vitals.hitpoints == 40 * 256 &&
          cancellation->get(returnedDrone)->motion->speed() == 0 &&
          cancellation->get(returnedDrone)->collisionPending &&
          cancellation->get(returnedDrone)->collisionWait == 2 &&
          samePoint(cancellation->get(returnedDrone)->position, {800, 800}) &&
          !cancellation->cancelConstruction(landingVent) &&
          !cancellation->cancelConstruction(returnedDrone) &&
          cancellation->economy().resources(0).minerals == 487 &&
          cancellationBefore->get(landingVent)->type == 149 &&
          cancellationBefore->economy().resources(0).minerals == 450);
    for (auto [resourceHp, expectedHp] :
         {std::pair{4735, 4608}, std::pair{255, 256}, std::pair{1, 256}}) {
      auto damagedGas = std::make_unique<World>(*uncommitted);
      damagedGas->get(landingVent)->vitals.hitpoints = resourceHp;
      damagedGas->advance();
      check(damagedGas->get(landingVent)->constructionPreviousHp == expectedHp &&
            damagedGas->cancelConstruction(landingVent));
      bool foundReplacement = false;
      for (int id : damagedGas->visibleUnits()) {
        const auto &unit = *damagedGas->units()[id];
        if (unit.type == 41 && unit.owner == 0 && unit.handle != arriving) {
          foundReplacement = true;
          check(unit.vitals.hitpoints == expectedHp);
        }
      }
      check(foundReplacement && uncommitted->get(landingVent)->type == 188);
    }
    for (std::uint8_t opcode : {std::uint8_t(0x18), std::uint8_t(0x19)}) {
      auto commandWorld = std::make_unique<World>(gasData, map);
      auto foundation = commandWorld->createFinished(149, {800, 800}, 0, false);
      auto *foundationState = commandWorld->get(foundation);
      foundationState->completed = false;
      foundationState->order = 45;
      foundationState->remainingBuildTime = 100;
      foundationState->constructionPreviousHp = 18 * 256;
      foundationState->resources = 5000;
      commandWorld->economy().set(0, {450, 0});
      auto commands = cancelBuildingRecording(foundation, opcode);
      replay::ClassicRunner runner(commands, *commandWorld);
      runner.advanceTo(2);
      check(commandWorld->get(foundation)->type == 188 &&
            commandWorld->get(foundation)->owner == 11 &&
            commandWorld->get(foundation)->resources == 5000 &&
            commandWorld->economy().resources(0).minerals == 487 &&
            runner.selections().selection(0) == std::vector<UnitHandle>{foundation});
      int replacements = 0;
      for (int id : commandWorld->visibleUnits())
        if (commandWorld->units()[id]->type == 41 &&
            commandWorld->units()[id]->owner == 0)
          ++replacements;
      check(replacements == 1);
    }
    for (int tick = 0; tick < 10 &&
                       landing->get(landingVent)->orderState < 2; ++tick)
      landing->advance();
    const auto &builtSprite = landing->get(landingVent)->sprite;
    check(std::none_of(builtSprite.order.begin(), builtSprite.order.end(),
                       [&](auto id) { return builtSprite.images[id].type == 345; }));
  }
  {
    auto larvaData = std::make_shared<Definitions>(*data);
    auto &larvaType = larvaData->units[35];
    larvaType = larvaData->units[41];
    larvaType.flags = 0x18000000U;
    larvaType.bounds = {2, 2, 2, 2};
    larvaType.hitpoints = 25 * 256;
    larvaType.humanIdle = larvaType.returnIdle = 77;
    auto recycled = std::make_unique<World>(larvaData, map);
    auto depot = recycled->createFinished(131, {832, 816}, 0, false, true);
    recycled->get(depot)->mainTimer = recycled->get(depot)->orderTimer = 1000;
    auto expired = recycled->createFinished(41, {1200, 1100}, 0, false);
    recycled->get(expired)->consumedByConstruction = true;
    recycled->get(expired)->order = 0;
    recycled->advance();
    check(recycled->get(expired) && recycled->get(expired)->dead);
    recycled->get(expired)->motion.emplace(Point{1200, 1100}, 0, larvaData->flingies[0]);
    recycled->get(expired)->motion->inheritInertia({201, 0, {}});
    recycled->advance();
    check(!recycled->get(expired));
    recycled->createFinished(131, {200, 200}, 11, false);
    recycled->get(depot)->spawnLarvae = true;
    recycled->get(depot)->orderTimer = 1;
    recycled->get(depot)->larvaTimer = 0;
    recycled->advance();
    UnitHandle newborn;
    for (int id : recycled->visibleUnits())
      if (recycled->units()[id]->type == 35 &&
          recycled->units()[id]->connected == depot)
        newborn = recycled->units()[id]->handle;
    check(newborn.index == expired.index && newborn != expired &&
          recycled->get(newborn)->motion);
    check(recycled->get(newborn)->motion->speed() == 0 &&
          samePoint(recycled->get(newborn)->motion->velocity(), {}));
  }
  auto poolWorld = std::make_unique<World>(data, map);
  auto home = poolWorld->createFinished(131, {832, 816}, 0, false, true);
  auto poolActor = poolWorld->createFinished(41, {960, 1000}, 0, false);
  poolWorld->economy().set(0, {500, 0});
  check(poolWorld->creep().at(26, 28) && poolWorld->creep().at(27, 29));
  auto absent = std::make_unique<World>(*poolWorld);
  absent->remove(home);
  check(!absent->build(poolActor, 142, {26, 28}, 25) &&
        absent->get(poolActor)->buildQueue.empty() &&
        absent->economy().resources(0).minerals == 500);
  auto unfinished = std::make_unique<World>(*poolWorld);
  unfinished->get(home)->completed = false;
  check(!unfinished->build(poolActor, 142, {26, 28}, 25));
  auto foreign = std::make_unique<World>(*poolWorld);
  foreign->get(home)->owner = 1;
  check(!foreign->build(poolActor, 142, {26, 28}, 25));
  auto advanced = std::make_unique<World>(*absent);
  auto lair = advanced->createFinished(132, {600, 600}, 0, false);
  advanced->get(lair)->completed = false;
  check(advanced->build(poolActor, 142, {26, 28}, 25));
  auto illusion = std::make_unique<World>(*poolWorld);
  illusion->setHallucinated(poolActor);
  check(!illusion->build(poolActor, 142, {26, 28}, 25));
  check(!poolWorld->creep().at(35, 35) &&
        !poolWorld->build(poolActor, 142, {35, 35}, 25));
  // An original boundary state isolates typed intent and snapshot ownership.
  // Controlled replay fixtures separately check real resource reservations.
  auto harvesting = std::make_unique<World>(*poolWorld);
  harvesting->get(poolActor)->gathering = true;
  harvesting->get(poolActor)->order = 85;
  check(harvesting->build(poolActor, 142, {26, 28}, 25));
  check(harvesting->get(poolActor)->order == 151 &&
        harvesting->get(poolActor)->harvestContinuation &&
        harvesting->get(poolActor)->harvestContinuation->order == 25 &&
        harvesting->economy().resources(0).minerals == 500);
  auto retainedBuild = std::make_unique<World>(*harvesting);
  harvesting->move(poolActor, {1200, 1200});
  harvesting->advance(100);
  retainedBuild->advance(100);
  check(harvesting->get(poolActor)->type == 41 &&
        harvesting->economy().resources(0).minerals == 500 &&
        retainedBuild->get(poolActor)->type == 142 &&
        retainedBuild->get(poolActor)->completed &&
        retainedBuild->economy().resources(0).minerals == 300);
  check(poolWorld->build(poolActor, 142, {26, 28}, 25) &&
        poolWorld->economy().resources(0).minerals == 500);
  poolWorld->advance(100);
  check(poolWorld->get(poolActor)->type == 142 &&
        poolWorld->get(poolActor)->completed &&
        poolWorld->get(poolActor)->vitals.hitpoints == 500 * 256 &&
        poolWorld->economy().resources(0).minerals == 300);
  // This approach reaches the first landing pixel before its fractional motion
  // has settled. The next leg prepares once, then touchdown aligns the anchor.
  auto activeLanding = std::make_unique<World>(data, map);
  auto activeActor = activeLanding->createFinished(41, {832, 816}, 0, false);
  auto *active = activeLanding->get(activeActor);
  active->motion.emplace(active->position, -103, data->flingies[0]);
  active->sprite.setHeading(-103);
  activeLanding->economy().set(0, {500, 0});
  check(activeLanding->build(activeActor, 131, {24, 24}, 25));
  for (int n = 0; n < 100 && active->orderState != 2; ++n) {
    active->orderTimer = 0;
    activeLanding->advance();
  }
  check(active->order == 70 && active->orderState == 2 &&
        active->motion->moving());
  auto handoffRaw = active->motion->rawPosition();
  auto handoffSpeed = active->motion->speed();
  activeLanding->advance();
  check(samePoint(active->motion->rawPosition(), handoffRaw) &&
        active->motion->speed() == handoffSpeed);
  for (int n = 0; n < 100 && active->order != 26; ++n)
    activeLanding->advance();
  check(active->order == 26 && active->collisionPending &&
        active->collisionWait == 2 && active->motion->speed() > 0 &&
        samePoint(active->motion->rawPosition(), {832 * 256, 816 * 256}));
  auto rejectedLanding = std::make_unique<World>(*activeLanding);
  rejectedLanding->economy().set(0, {0, 0});
  int arrivalSpeed = active->motion->speed();
  auto arrivalRaw = active->motion->rawPosition();
  rejectedLanding->advance();
  auto *rejected = rejectedLanding->get(activeActor);
  check(rejected->order == 150 &&
        rejected->motion->speed() == std::max(0, arrivalSpeed - 67) &&
        samePoint(rejected->motion->rawPosition(), arrivalRaw));
  rejectedLanding->advance(3);
  check(rejected->motion->speed() == 0 && !rejected->motion->moving());
  activeLanding->advance();
  check(active->type == 131 && !active->motion && active->inactiveInertia &&
        active->inactiveInertia->speed > 0);
  auto world = std::make_unique<World>(data, map);
  auto actor = world->createFinished(41, {832, 809}, 0, false);
  world->economy().set(0, {500, 0});
  check(world->build(actor, 131, {24, 24}, 25));
  check(world->economy().resources(0).minerals == 500);
  auto unfunded = std::make_unique<World>(*world);
  unfunded->economy().set(0, {250, 0});
  world->advance(11);
  check(world->get(actor)->order == 70 && world->get(actor)->orderState == 2);
  auto deferred = std::make_unique<World>(*world);
  deferred->move(actor, {1200, 1200});
  check(deferred->get(actor)->order == 70 &&
        deferred->get(actor)->orderState == 2 &&
        samePoint(deferred->get(actor)->moveTargetPosition, {832, 816}));
  for (int n = 0; n < 50 && deferred->get(actor)->order != 6; ++n)
    deferred->advance();
  check(deferred->get(actor)->order == 6);
  auto resting = deferred->get(actor)->motion->rawPosition();
  deferred->advance(2);
  check(samePoint(deferred->get(actor)->motion->rawPosition(), resting));
  deferred->advance();
  check(!samePoint(deferred->get(actor)->motion->rawPosition(), resting));
  world->advance(89);
  deferred->advance(89);
  unfunded->advance(100);
  auto *building = world->get(actor);
  check(building && building->type == 131 && building->completed &&
        building->buildQueue.empty() && building->remainingBuildTime == 0 &&
        building->vitals.hitpoints == 1000 * 256 &&
        samePoint(building->position, {832, 816}) &&
        samePoint(building->targetPosition, {}) &&
        world->economy().resources(0).minerals == 200);
  check(unfunded->get(actor)->type == 41 &&
        unfunded->get(actor)->buildQueue == std::vector<int>{131} &&
        unfunded->economy().resources(0).minerals == 250);
  check(deferred->get(actor)->type == 41 &&
        deferred->get(actor)->buildQueue == std::vector<int>{131} &&
        deferred->economy().resources(0).minerals == 500);
}
void testProtossConstruction(const Definitions &seed,
                             std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  // Small original scripts isolate construction ordering from game assets.
  std::vector<std::uint8_t> bytes(4 + 3 * 64);
  auto word = [&](std::size_t at, int value) {
    bytes[at] = std::uint8_t(value);
    bytes[at + 1] = std::uint8_t(value >> 8);
  };
  int idle = int(bytes.size());
  bytes.insert(bytes.end(),
               {5, 1, 7, std::uint8_t(idle), std::uint8_t(idle >> 8)});
  int signal = int(bytes.size());
  bytes.insert(bytes.end(),
               {36, 1, 5, 1, 7, std::uint8_t(idle), std::uint8_t(idle >> 8)});
  for (int index = 0; index < 3; ++index) {
    int at = 4 + index * 64;
    for (int i = 0; i < 4; ++i)
      bytes[at + i] = std::uint8_t("SCPE"[i]);
    word(at + 4, 26);
    for (int animation = 0; animation < 28; ++animation)
      word(at + 8 + animation * 2, idle);
  }
  word(4 + 64 + 8 + 13 * 2, signal);
  word(4 + 128 + 8, signal);
  word(4 + 8 + 21 * 2, signal);
  int table = int(bytes.size());
  for (int i = 0; i < 4; ++i)
    bytes[i] = std::uint8_t(table >> (i * 8));
  bytes.resize(bytes.size() + 16);
  for (int i = 0; i < 3; ++i) {
    word(table + i * 4, i == 2 ? 193 : i);
    word(table + i * 4 + 2, 4 + i * 64);
  }
  word(table + 12, 65535);
  data->program = iscript::Program(bytes);
  data->images[0].script = 0;
  data->images[211].script = 1;
  data->images[0].animated = data->images[211].animated = true;
  data->flingies[0].speed = 1280;
  data->flingies[0].acceleration = 67;
  data->flingies[0].turnRate = 40;
  data->flingies[0].control = 0;
  data->flingies[0].sprite = 0;
  data->sprites[0].image = data->sprites[321].image = 0;
  auto &probe = data->units[64];
  probe.flags = 0x8000008U;
  probe.graphics = 0;
  probe.groupFlags = 4;
  probe.bounds = {1, 1, 1, 1};
  probe.humanIdle = probe.returnIdle = 3;
  probe.sightRange = 7;
  auto &pylon = data->units[156];
  pylon.flags = 1;
  pylon.graphics = 0;
  pylon.groupFlags = 4;
  pylon.humanIdle = 164;
  pylon.returnIdle = 23;
  pylon.bounds = {16, 12, 16, 20};
  pylon.placementWidth = pylon.placementHeight = 64;
  pylon.shields = true;
  pylon.shieldPoints = 100;
  pylon.hitpoints = 100 * 256;
  pylon.buildTime = 3;
  pylon.mineralCost = 100;
  pylon.gasCost = 0;
  pylon.supplyProvided = 16;
  pylon.sightRange = 8;
  auto world = std::make_unique<World>(data, map);
  auto worker = world->createFinished(64, {792, 832}, 0, false);
  world->economy().set(0, {200, 0});
  check(!world->build(worker, 156, {1, 1}, 31));
  check(world->get(worker)->buildQueue.empty() &&
        world->economy().resources(0).minerals == 200);
  check(world->build(worker, 156, {25, 25}, 31));
  check(world->economy().resources(0).minerals == 200 &&
        world->get(worker)->buildQueue == std::vector<int>{156});
  auto unfunded = std::make_unique<World>(*world);
  unfunded->economy().set(0, {50, 0});
  world->advance(30);
  unfunded->advance(30);
  const UnitState *created = nullptr;
  for (int id : world->visibleUnits())
    if (world->units()[id]->type == 156)
      created = &*world->units()[id];
  check(created && created->completed && created->psiActive &&
        created->remainingBuildTime == 0 && created->order == 23 &&
        created->vitals.hitpoints == 100 * 256 &&
        created->vitals.shields == 100 * 256);
  check(world->economy().resources(0).minerals == 100 &&
        world->population(0).available[2] == 16 &&
        world->get(worker)->buildQueue.empty());
  check(unfunded->economy().resources(0).minerals == 50 &&
        unfunded->population(0).units[156] == 0 &&
        unfunded->get(worker)->buildQueue == std::vector<int>{156});
  auto &forge = data->units[166];
  forge = pylon;
  forge.flags |= 0x80000U;
  forge.placementWidth = 96;
  forge.humanIdle = forge.returnIdle = 23;
  forge.mineralCost = 150;
  forge.supplyProvided = 0;
  auto &nexus = data->units[154];
  nexus = pylon;
  nexus.humanIdle = nexus.returnIdle = 23;
  nexus.placementWidth = 128;
  nexus.placementHeight = 96;
  auto forgeWorld = std::make_unique<World>(*world);
  forgeWorld->economy().set(0, {500, 0});
  check(!forgeWorld->build(worker, 166, {28, 25}, 31));
  auto nexusHandle = forgeWorld->createFinished(154, {1100, 900}, 0, false);
  auto incompleteBase = std::make_unique<World>(*forgeWorld);
  incompleteBase->get(nexusHandle)->completed = false;
  check(!incompleteBase->build(worker, 166, {28, 25}, 31));
  auto foreignBase = std::make_unique<World>(*forgeWorld);
  foreignBase->get(nexusHandle)->owner = 1;
  check(!foreignBase->build(worker, 166, {28, 25}, 31));
  auto unpoweredForge = std::make_unique<World>(*forgeWorld);
  unpoweredForge->get(created->handle)->psiActive = false;
  check(!unpoweredForge->build(worker, 166, {28, 25}, 31) &&
        unpoweredForge->get(worker)->buildQueue.empty());
  auto foreignPower = std::make_unique<World>(*forgeWorld);
  foreignPower->get(created->handle)->owner = 1;
  check(!foreignPower->build(worker, 166, {28, 25}, 31));
  auto &gateway = data->units[160];
  gateway = forge;
  gateway.placementWidth = 128;
  gateway.placementHeight = 96;
  auto absentGatewayBase = std::make_unique<World>(*world);
  absentGatewayBase->economy().set(0, {500, 0});
  check(!absentGatewayBase->build(worker, 160, {28, 25}, 31));
  check(!incompleteBase->build(worker, 160, {28, 25}, 31) &&
        !foreignBase->build(worker, 160, {28, 25}, 31));
  check(!unpoweredForge->build(worker, 160, {28, 25}, 31) &&
        !foreignPower->build(worker, 160, {28, 25}, 31));
  auto gatewayWorld = std::make_unique<World>(*forgeWorld);
  check(gatewayWorld->build(worker, 160, {28, 25}, 31) &&
        gatewayWorld->get(worker)->buildQueue == std::vector<int>{160} &&
        gatewayWorld->economy().resources(0).minerals == 500);
  gatewayWorld->advance(80);
  check(gatewayWorld->population(0).completed[160] == 1 &&
        gatewayWorld->economy().resources(0).minerals == 350 &&
        forgeWorld->population(0).units[160] == 0);
  check(forgeWorld->build(worker, 166, {28, 25}, 31) &&
        forgeWorld->get(worker)->buildQueue == std::vector<int>{166} &&
        forgeWorld->economy().resources(0).minerals == 500);
  auto &cannon = data->units[162];
  cannon = forge;
  cannon.placementWidth = 64;
  auto cannonWorld = std::make_unique<World>(*world);
  cannonWorld->economy().set(0, {500, 0});
  check(!cannonWorld->build(worker, 162, {28, 28}, 31));
  auto forgeHandle = cannonWorld->createFinished(166, {1100, 1050}, 0, false);
  auto incompleteForge = std::make_unique<World>(*cannonWorld);
  incompleteForge->get(forgeHandle)->completed = false;
  check(!incompleteForge->build(worker, 162, {28, 28}, 31));
  auto foreignForge = std::make_unique<World>(*cannonWorld);
  foreignForge->get(forgeHandle)->owner = 1;
  check(!foreignForge->build(worker, 162, {28, 28}, 31));
  auto unpoweredCannon = std::make_unique<World>(*cannonWorld);
  unpoweredCannon->get(created->handle)->psiActive = false;
  check(!unpoweredCannon->build(worker, 162, {28, 28}, 31));
  auto shortCannon = std::make_unique<World>(*cannonWorld);
  shortCannon->economy().set(0, {149, 0});
  check(!shortCannon->build(worker, 162, {28, 28}, 31));
  check(cannonWorld->build(worker, 162, {28, 28}, 31));
  cannonWorld->advance(80);
  check(cannonWorld->population(0).completed[162] == 1 &&
        cannonWorld->economy().resources(0).minerals == 350);
  auto movingQueue = std::make_unique<World>(*world);
  movingQueue->move(worker, {1100, 900});
  movingQueue->move(worker, {1100, 1100}, true);
  check(movingQueue->get(worker)->queuedCommands.size() == 1 &&
        samePoint(movingQueue->get(worker)->targetPosition, {1100, 900}));
  auto independentQueue = std::make_unique<World>(*movingQueue);
  movingQueue->move(worker, {1000, 800});
  check(movingQueue->get(worker)->queuedCommands.empty() &&
        independentQueue->get(worker)->queuedCommands.size() == 1);
  movingQueue->advance(500);
  independentQueue->advance(500);
  check(samePoint(movingQueue->get(worker)->position, {1000, 800}) &&
        samePoint(independentQueue->get(worker)->position, {1100, 1100}) &&
        independentQueue->get(worker)->queuedCommands.empty());
  // A visible building gains its completed sight range at the next refresh.
  check(world->exploredAt({34, 26}) == 0);
  world->remove(worker);
  // Ready worlds have already consumed two initialization updates.
  world->advance(68);
  check(world->exploredAt({34, 26}) == 0);
  world->advance(1);
  check(world->exploredAt({34, 26}) == 1);
}
void testVisibility(const Definitions &seed) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].sightRange = 6;
  data->units[7].bounds = {1, 1, 1, 1};
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  struct Case {
    Point wall;
    std::uint64_t digest;
  };
  // Public-API observations with one opaque tile and otherwise flat terrain.
  const Case cases[] = {
      {{1, 0}, 11209192419691659424ULL},  {{2, 0}, 16953286484434400754ULL},
      {{2, 1}, 1149142630379745961ULL},   {{1, 2}, 1149142630379745961ULL},
      {{2, 2}, 14634184541437589829ULL},  {{3, 2}, 1149142630379745961ULL},
      {{2, 3}, 1149142630379745961ULL},   {{-2, 1}, 1149142630379745961ULL},
      {{2, -1}, 1149142630379745961ULL},  {{-3, -2}, 1149142630379745961ULL},
      {{0, 3}, 2161726845211930877ULL},   {{4, 0}, 5004658610496690893ULL},
      {{-1, -1}, 3946834903093444485ULL}, {{-6, 2}, 1149142630379745961ULL},
  };
  for (const auto &item : cases) {
    std::vector<std::uint16_t> tiles(64 * 64);
    tiles[(16 + item.wall.y) * 64 + 16 + item.wall.x] = 16;
    std::vector<std::uint8_t> cv5(104), vf4(64);
    cv5[72] = 1;
    for (int i = 0; i < 16; ++i) {
      vf4[i * 2] = 1;
      vf4[32 + i * 2] = 9;
    }
    auto terrain = std::make_shared<WorldMap>(Terrain(64, 64, tiles, cv5, vf4));
    auto world = std::make_unique<World>(data, terrain);
    auto actor = world->createFinished(7, {512, 512}, 0, false);
    std::uint64_t digest = 14695981039346656037ULL;
    for (int y = -7; y <= 7; ++y)
      for (int x = -7; x <= 7; ++x)
        if (world->exploredAt({16 + x, 16 + y}) & 1)
          for (int value : {x + 12, y + 12}) {
            digest ^= std::uint8_t(value);
            digest *= 1099511628211ULL;
          }
    check(digest == item.digest);
    auto snapshot = std::make_unique<World>(*world);
    world->remove(actor);
    world->advance(100);
    check(world->exploredAt({16, 16}) == 1 && world->visibleAt({16, 16}) == 0);
    check(snapshot->exploredAt({16, 16}) == 1 &&
          snapshot->visibleAt({16, 16}) == 1 && snapshot->get(actor) &&
          !snapshot->get(actor)->dead);
  }
}
void testRightClicks(const Definitions &seed,
                     std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].rightClickAction = 4;
  auto &mineral = data->units[176];
  mineral.flags = 0x2001;
  mineral.bounds = {32, 16, 31, 15};
  mineral.humanIdle = mineral.returnIdle = 23;
  auto original = std::make_unique<World>(data, map);
  std::array<UnitHandle, 2> workers{original->createFinished(7, {600, 800}, 0),
                                    original->createFinished(7, {640, 800}, 0)};
  auto patch = original->createFinished(176, {1000, 800}, 11);
  original->setResources(patch, 1500);
  auto mining = std::make_unique<World>(*original);
  auto rep = clickRecording(workers, patch, {1, 1}, false);
  replay::ClassicRunner runner(rep, *mining);
  runner.advanceTo(1);
  for (auto handle : workers) {
    const auto &worker = *mining->get(handle);
    check(worker.order == 85 && worker.orderState == 1 &&
          worker.target == patch &&
          samePoint(worker.targetPosition, {1000, 800}));
  }
  check(runner.commands() == 2 && original->get(workers[0])->order == 2);
  auto moving = std::make_unique<World>(*original);
  rep = clickRecording(workers, {}, {800, 1000}, false);
  replay::ClassicRunner moveRunner(rep, *moving);
  moveRunner.advanceTo(1);
  check(samePoint(moving->get(workers[0])->targetPosition, {780, 1000}) &&
        samePoint(moving->get(workers[1])->targetPosition, {820, 1000}));
  auto queued = std::make_unique<World>(*original);
  rep = clickRecording(workers, patch, {}, true);
  replay::ClassicRunner queuedRunner(rep, *queued);
  queuedRunner.advanceTo(1);
  check(queued->frame() == 1 && queuedRunner.commands() == 2);
  for (auto handle : workers) {
    const auto &worker = *queued->get(handle);
    check(worker.order == 85 && worker.orderState == 1 &&
          worker.target == patch && worker.queuedCommands.size() == 1 &&
          worker.queuedCommands.front().order == 151);
  }
  auto busy = std::make_unique<World>(*original);
  auto actor = workers[0];
  busy->move(actor, {800, 900});
  busy->harvest(actor, patch, HarvestOrder::Minerals, true);
  check(busy->get(actor)->order == 6 &&
        busy->get(actor)->queuedCommands.size() == 1 &&
        busy->get(actor)->queuedCommands.front().order == 85);
  auto saved = std::make_unique<World>(*busy);
  busy->move(actor, {900, 900});
  check(busy->get(actor)->queuedCommands.empty() &&
        saved->get(actor)->queuedCommands.front().target == patch);
  auto locked = std::make_unique<World>(*queued);
  locked->get(actor)->sprite.context.uninterruptible = true;
  locked->move(actor, {800, 900});
  check(locked->get(actor)->order == 85 &&
        locked->get(actor)->queuedCommands.size() == 2 &&
        locked->get(actor)->queuedCommands.front().order == 151 &&
        locked->get(actor)->queuedCommands.back().order == 6);
  locked->get(actor)->sprite.context.uninterruptible = false;
  for (int n = 0; n != 8; ++n)
    locked->advance();
  check(locked->get(actor)->order == 6 &&
        locked->get(actor)->queuedCommands.empty() &&
        !locked->get(actor)->gathering &&
        samePoint(locked->get(actor)->targetPosition, {800, 900}) &&
        queued->get(actor)->queuedCommands.front().order == 151);
  auto foreign = std::make_unique<World>(*original);
  auto enemy = foreign->createFinished(7, {600, 1000}, 1);
  std::array<UnitHandle, 1> selection{enemy};
  rep = clickRecording(selection, patch, {}, false);
  replay::ClassicRunner foreignRunner(rep, *foreign);
  foreignRunner.advanceTo(1);
  check(foreign->get(enemy)->order == 3 && foreignRunner.commands() == 2);
  auto following = std::make_unique<World>(*original);
  auto friendUnit = following->createFinished(7, {1100, 900}, 0);
  rep = clickRecording(workers, friendUnit, {1, 1}, false);
  replay::ClassicRunner followRunner(rep, *following);
  followRunner.advanceTo(1);
  for (auto actor : workers) {
    const auto &worker = *following->get(actor);
    check(worker.order == 49 && worker.orderState == 1 &&
          worker.target == friendUnit &&
          samePoint(worker.targetPosition, {1100, 900}) &&
          samePoint(worker.moveTargetPosition, {1100, 900}));
    auto delay = worker.mainTimer;
    following->follow(actor, friendUnit);
    check(worker.orderTimer == 0 && worker.orderState == 0 &&
          worker.mainTimer == delay);
  }
  check(original->get(workers[0])->order == 2 && followRunner.commands() == 2);
  auto selfClick = std::make_unique<World>(*original);
  std::array<UnitHandle, 1> selfSelection{workers[0]};
  auto selfRecording = clickRecording(selfSelection, workers[0], {}, false);
  replay::ClassicRunner selfRunner(selfRecording, *selfClick);
  selfRunner.advanceTo(1);
  check(!selfClick->get(workers[0])->userMovementPending &&
        selfClick->get(workers[0])->queuedCommands.empty());
  auto queuedFollow = std::make_unique<World>(*original);
  auto follower = workers[0];
  auto leader = queuedFollow->createFinished(7, {1100, 900}, 0);
  queuedFollow->move(follower, {800, 900});
  queuedFollow->follow(follower, leader, true);
  check(queuedFollow->get(follower)->order == 6 &&
        queuedFollow->get(follower)->queuedCommands.size() == 1 &&
        queuedFollow->get(follower)->queuedCommands.front().order == 49);
  auto savedFollow = std::make_unique<World>(*queuedFollow);
  queuedFollow->move(follower, {900, 900});
  check(queuedFollow->get(follower)->queuedCommands.empty() &&
        savedFollow->get(follower)->queuedCommands.front().target == leader);
  data->units[106].flags = 0x1001;
  data->units[106].bounds = {32, 16, 31, 15};
  data->units[106].humanIdle = data->units[106].returnIdle = 23;
  auto clickedDepot = std::make_unique<World>(*original);
  clickedDepot->createFinished(106, {600, 1000}, 0);
  auto destinationDepot = clickedDepot->createFinished(106, {1100, 900}, 0);
  clickedDepot->get(workers[0])->cargo = 2;
  clickedDepot->get(workers[0])->cargoAmount = 8;
  auto queuedDepot = std::make_unique<World>(*clickedDepot);
  for (auto handle : workers)
    queuedDepot->move(handle, {800, 900});
  auto queuedClick = clickRecording(workers, destinationDepot, {}, true);
  replay::ClassicRunner queuedDepotRunner(queuedClick, *queuedDepot);
  queuedDepotRunner.advanceTo(1);
  check(queuedDepot->get(workers[0])->queuedCommands.front().order == 90 &&
        queuedDepot->get(workers[0])->queuedCommands.front().target == destinationDepot &&
        queuedDepot->get(workers[1])->queuedCommands.front().order == 49 &&
        queuedDepot->get(workers[1])->queuedCommands.front().target == destinationDepot);
  auto depotClick = clickRecording(workers, destinationDepot, {}, false);
  replay::ClassicRunner depotRunner(depotClick, *clickedDepot);
  depotRunner.advanceTo(1);
  check(clickedDepot->get(workers[0])->order == 90 &&
        clickedDepot->get(workers[0])->target == destinationDepot &&
        clickedDepot->get(workers[1])->order == 49 &&
        clickedDepot->get(workers[1])->target == destinationDepot);
  auto armyData = std::make_shared<Definitions>(*data);
  armyData->units[37] = armyData->units[7];
  armyData->units[37].flags &= ~8U;
  armyData->units[37].rightClickAction = 1;
  auto army = std::make_unique<World>(armyData, map);
  auto soldier = army->createFinished(37, {600, 800}, 0);
  auto armyMineral = army->createFinished(176, {1000, 800}, 11);
  army->setResources(armyMineral, 1500);
  auto armyDepot = army->createFinished(106, {1100, 900}, 0);
  army->get(soldier)->cargo = 2;
  std::array<UnitHandle, 1> armySelection{soldier};
  auto armyDepotCopy = std::make_unique<World>(*army);
  for (auto item : {std::pair{army.get(), armyMineral},
                    std::pair{armyDepotCopy.get(), armyDepot}}) {
    auto recording = clickRecording(armySelection, item.second, {}, false);
    replay::ClassicRunner armyRunner(recording, *item.first);
    armyRunner.advanceTo(1);
    check(item.first->get(soldier)->order == 49 &&
          item.first->get(soldier)->target == item.second &&
          !item.first->get(soldier)->gathering &&
          item.first->get(soldier)->cargo == 2);
  }
  bool foreignFollowRejected = false;
  auto nearbyBuilding = std::make_unique<World>(data, map);
  auto nearbyActor = nearbyBuilding->createFinished(7, {600, 800}, 0);
  auto nearbyDepot = nearbyBuilding->createFinished(106, {672, 800}, 0);
  nearbyBuilding->move(nearbyActor, {800, 900});
  nearbyBuilding->follow(nearbyActor, nearbyDepot);
  auto nearbyCopy = std::make_unique<World>(*nearbyBuilding);
  nearbyBuilding->advance();
  const auto &finishedFollow = *nearbyBuilding->get(nearbyActor);
  check(finishedFollow.order == data->units[7].returnIdle &&
        finishedFollow.orderState == 0 && finishedFollow.orderTimer == 0 &&
        finishedFollow.mainTimer == 7 && finishedFollow.target.index < 0 &&
        samePoint(finishedFollow.targetPosition, {}) &&
        samePoint(finishedFollow.moveTargetPosition, {672, 800}) &&
        finishedFollow.moveTargetUnit == nearbyDepot &&
        nearbyCopy->get(nearbyActor)->order == 49);
  auto repeatedBuilding = std::make_unique<World>(*nearbyBuilding);
  repeatedBuilding->follow(nearbyActor, nearbyDepot);
  check(repeatedBuilding->get(nearbyActor)->mainTimer == 7);
  repeatedBuilding->advance();
  check(repeatedBuilding->get(nearbyActor)->order == 49 &&
        repeatedBuilding->get(nearbyActor)->mainTimer == 6);
  auto previousForeignOrder = foreign->get(workers[0])->order;
  try {
    foreign->follow(workers[0], enemy);
  } catch (const std::invalid_argument &) {
    foreignFollowRejected = true;
  }
  check(foreignFollowRejected &&
        foreign->get(workers[0])->order == previousForeignOrder);
}
void testOverlapOrders(const Definitions &seed,
                       std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  auto &base = data->units[106];
  base.flags = 1;
  base.bounds = {40, 24, 40, 24};
  base.humanIdle = base.returnIdle = 23;
  auto &mineral = data->units[176];
  mineral.flags = 0x2001;
  mineral.bounds = {32, 16, 31, 15};
  mineral.humanIdle = mineral.returnIdle = 23;
  auto world = std::make_unique<World>(data, map);
  world->createFinished(106, {800, 776}, 0, false);
  auto worker = world->createFinished(7, {808, 800}, 0, false);
  auto patch = world->createFinished(176, {1200, 800}, 11);
  world->setResources(patch, 1500);
  // Resume an owned snapshot during illegal-position recovery, with a new
  // mineral target beyond the building. The geometric exit must not draw RNG.
  auto *actor = world->get(worker);
  actor->motion.emplace(actor->position, 64, data->flingies[0]);
  actor->order = 85;
  actor->orderState = 1;
  actor->gathering = true;
  actor->mineral = actor->target = patch;
  actor->targetPosition = actor->moveTargetPosition = {1200, 800};
  actor->overlapReturnTarget = {1200, 800};
  actor->escapingOverlap = actor->overlapOrderChanged = true;
  actor->collisionPending = true;
  actor->collisionWait = 1;
  auto random = world->randomState();
  world->advance();
  check(world->randomState() == random &&
        samePoint(actor->position, {808, 800}) &&
        samePoint(actor->motion->waypoint(), {808, 812}) &&
        samePoint(actor->moveTargetPosition, {1200, 800}));
  auto pending = std::make_unique<World>(*world);
  auto *copy = pending->get(worker);
  copy->collisionPending = true;
  copy->collisionWait = 1;
  pending->harvest(worker, patch, HarvestOrder::Minerals);
  pending->advance(2);
  check(pending->randomState() == random &&
        samePoint(copy->position, {808, 800}) &&
        samePoint(copy->motion->waypoint(), {808, 812}) && copy->order == 85 &&
        copy->orderState == 1 && copy->overlapDirectRoute &&
        !actor->overlapDirectRoute && world->frame() == 1);
}
void testRallies(const Definitions &seed, std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[106].groupFlags |= 32;
  auto world = std::make_unique<World>(data, map);
  auto factory = world->createFinished(106, {800, 800}, 0);
  auto friendly = world->createFinished(7, {1000, 1000}, 0);
  world->economy().set(0, {500, 250});
  check(world->canRally(factory) && !world->canRally(friendly));
  check(world->rallyPoint(factory, {1000, 800}));
  check(!world->rallyPoint(factory, {-1, 800}) &&
        samePoint(world->get(factory)->rallyPosition, {1000, 800}));
  check(world->train(factory, 7));
  world->advance();
  auto child = world->get(factory)->buildUnit;
  auto snapshot = std::make_unique<World>(*world);
  check(world->rallyPoint(factory, {1200, 800}));
  world->advance(3);
  check(world->get(child)->completed && world->get(child)->order == 6 &&
        samePoint(world->get(child)->targetPosition, {1200, 800}));
  check(world->get(child)->collisionPending &&
        world->get(child)->collisionWait == 2);
  check(samePoint(snapshot->get(factory)->rallyPosition, {1000, 800}) &&
        !snapshot->get(child)->completed);
  check(snapshot->rallyUnit(factory, friendly));
  snapshot->advance(3);
  check(snapshot->get(child)->order == 49 &&
        snapshot->get(child)->target == friendly);
  auto self = std::make_unique<World>(data, map);
  factory = self->createFinished(106, {800, 800}, 0);
  self->economy().set(0, {500, 250});
  check(self->rallyUnit(factory, {}) &&
        self->get(factory)->rallyTarget == factory && self->train(factory, 7));
  self->advance();
  child = self->get(factory)->buildUnit;
  self->advance(3);
  check(self->get(child)->completed && self->get(child)->order != 6 &&
        self->get(child)->order != 49);
}
void testFreshBodyDeparture(const Definitions &seed,
                            std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[36].flags = 0;
  data->units[36].bounds = {16, 16, 15, 15};
  data->units[36].humanIdle = data->units[36].returnIdle = 23;
  auto world = std::make_unique<World>(data, map);
  world->createFinished(36, {800, 800}, 0, false);
  auto actor = world->createFinished(7, {803, 803}, 0, false);
  auto *unit = world->get(actor);
  unit->motion.emplace(unit->position, 104, data->flingies[0]);
  unit->order = 6;
  unit->orderState = 1;
  unit->targetPosition = unit->moveTargetPosition = {1200, 800};
  unit->planRequested = unit->collisionPending = true;
  unit->collisionWait = 1;
  auto random = world->randomState();
  auto before = std::make_unique<World>(*world);
  world->advance();
  check(world->randomState() == random &&
        samePoint(unit->position, {803, 803}) &&
        samePoint(unit->motion->waypoint(), {827, 803}) &&
        samePoint(unit->moveTargetPosition, {1200, 800}) &&
        unit->overlapOrderChanged && unit->overlapFreshOrder);
  check(!before->get(actor)->escapingOverlap &&
        before->get(actor)->planRequested);
}
void testArrivalFocus(const Definitions &seed) {
  auto physics = seed.flingies[0];
  physics.control = 2;
  physics.speed = 512;
  physics.haltDistance = 0;
  Motion motion({300, 300}, 64, physics);
  motion.route({{320, 300}}, 0);
  motion.scriptSpeed(512);
  for (int n = 0; n < 100 && !motion.arrived(); ++n) {
    motion.scriptSpeed(512);
    motion.advance();
  }
  check(motion.arrived());
  auto plain = motion;
  motion.faceOnArrival(Point{377, 330});
  motion.advance();
  plain.advance();
  check(motion.facing() == 83 && motion.travelHeading() == 83 &&
        plain.facing() == 64 &&
        samePoint(motion.position(), plain.position()) && motion.speed() == 0);
  motion.route({{400, 300}}, 0);
  motion.scriptSpeed(512);
  for (int n = 0; n < 100 && !motion.arrived(); ++n) {
    motion.scriptSpeed(512);
    motion.advance();
  }
  check(motion.arrived());
  motion.advance();
  check(motion.facing() == 64 && plain.facing() == 64);
}
void testOverlapDeparture(const Definitions &seed,
                          std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = {11, 11, 11, 11};
  data->units[7].flags |= 8;
  data->units[106].flags = 1;
  data->units[106].bounds = {32, 32, 32, 32};
  data->units[106].humanIdle = 23;
  bool observed = false;
  for (unsigned scenarioSeed = 0; scenarioSeed < 16 && !observed;
       ++scenarioSeed) {
    auto world = std::make_unique<World>(data, map, scenarioSeed);
    auto actor = world->createFinished(7, {800, 800}, 0, false);
    auto blocker = world->createFinished(7, {812, 800}, 0, false);
    world->createFinished(106, {1000, 800}, 0, false);
    world->get(blocker)->moveTargetPosition = {900, 900};
    auto *unit = world->get(actor);
    unit->motion.emplace(unit->position, 64, data->flingies[0]);
    unit->order = 6;
    unit->orderState = 1;
    unit->targetPosition = unit->moveTargetPosition = {1200, 800};
    unit->overlapReturnTarget = unit->moveTargetPosition;
    unit->escapingOverlap = unit->overlapOrderChanged =
        unit->overlapFreshOrder = true;
    unit->collisionPending = true;
    unit->collisionWait = 1;
    auto before = std::make_unique<World>(*world);
    world->advance();
    if (!unit->collisionPending || !unit->path)
      continue;
    observed = true;
    check(unit->path->points.size() > 1 && unit->path->reached &&
          samePoint(unit->position, {800, 800}) &&
          world->workerPositionBlocked(actor, unit->position));
    check(before->frame() == 0 && !before->get(actor)->path);
  }
  check(observed);
}
void testBodyWaitRelease(const Definitions &seed,
                         std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = data->units[106].bounds = {11, 11, 11, 11};
  data->units[7].humanIdle = data->units[106].humanIdle = 23;
  data->units[106].flags = 1;
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  auto world = std::make_unique<World>(data, map);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto blocker = world->createFinished(7, {800, 775}, 0, false);
  world->createFinished(106, {784, 822}, 0, false);
  auto *unit = world->get(actor);
  unit->motion.emplace(unit->position, 8, physics);
  unit->motion->route({{872, 400}}, 0);
  for (int n = 0; n < 14; ++n)
    unit->motion->advance();
  check(unit->motion->speed() == 938);
  unit->motion->waitForBody({800 * 256, 800 * 256}, 8, 871);
  unit->order = 6;
  unit->orderState = 1;
  unit->targetPosition = unit->moveTargetPosition = {872, 400};
  unit->collisionPending = false;
  world->advance();
  check(unit->motion->waitingForBody() && unit->motion->speed() == 0 &&
        unit->motion->nextSpeed() == 0 &&
        samePoint(unit->position, {800, 800}));
  auto before = std::make_unique<World>(*world);
  world->remove(blocker);
  world->advance();
  check(!unit->motion->waitingForBody());
  check(unit->motion->speed() == 1005);
  check(samePoint(unit->motion->rawPosition(),
                  {800 * 256 + 196, 800 * 256 - 986}));
  check(before->get(actor)->motion->waitingForBody() &&
        samePoint(before->get(actor)->position, {800, 800}));
}
void testMovingBodyBoundary(const Definitions &seed,
                            std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = {11, 11, 11, 11};
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  for (Point origin : {Point{800, 800}, Point{1000, 900}}) {
    for (int separation : {22, 23}) {
      auto world = std::make_unique<World>(data, map);
      Point obstacle{origin.x - separation, origin.y - 3};
      auto blocker = world->createFinished(7, obstacle, 0, false);
      auto *body = world->get(blocker);
      body->collisionPending = false;
      body->moveTargetPosition = {obstacle.x + 5, obstacle.y - 5};
      body->motion.emplace(obstacle, 32, physics);
      body->motion->route({body->moveTargetPosition}, 0);
      body->motion->advance();
      auto actor = world->createFinished(7, origin, 0, false);
      auto *unit = world->get(actor);
      unit->motion.emplace(origin, -95, physics);
      unit->motion->route({{origin.x - 62, origin.y + 61}}, 0);
      for (int i = 0; i < 5; ++i)
        unit->motion->advance();
      Point raw{origin.x * 256 + 10, origin.y * 256 + 18};
      unit->motion->waitForBody(raw, -95, 268);
      unit->motion->resumeBodyWait();
      unit->collisionPending = false;
      unit->order = 6;
      unit->orderState = 1;
      unit->targetPosition =
          unit->moveTargetPosition = {origin.x - 71, origin.y + 40};
      world->advance();
      check(samePoint(unit->motion->rawPosition(), raw));
      check(unit->motion->waitingForBody());
    }
    auto world = std::make_unique<World>(data, map);
    auto blocker =
        world->createFinished(7, {origin.x - 20, origin.y - 6}, 0, false);
    world->get(blocker)->collisionPending = false;
    world->get(blocker)->motion.reset();
    auto actor = world->createFinished(7, origin, 0, false);
    auto *unit = world->get(actor);
    unit->motion.emplace(origin, -95, physics);
    unit->motion->route({{origin.x - 62, origin.y + 61}}, 0);
    for (int i = 0; i < 6; ++i)
      unit->motion->advance();
    Point raw{origin.x * 256 + 10, origin.y * 256 + 18};
    unit->motion->waitForBody(raw, -95, 335);
    unit->collisionPending = false;
    unit->order = 6;
    unit->orderState = 1;
    unit->targetPosition =
        unit->moveTargetPosition = {origin.x - 71, origin.y + 40};
    auto snapshot = std::make_unique<World>(*world);
    world->advance();
    check(!unit->motion->waitingForBody() && unit->motion->speed() == 469);
    check(!samePoint(unit->motion->rawPosition(), raw));
    check(snapshot->get(actor)->motion->waitingForBody() &&
          samePoint(snapshot->get(actor)->motion->rawPosition(), raw));
  }
}
void testStationaryBodyBoundary(const Definitions &seed,
                                std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = {11, 11, 11, 11};
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  for (Point origin : {Point{800, 800}, Point{1000, 900}}) {
    for (int separation : {22, 23}) {
      auto world = std::make_unique<World>(data, map);
      auto blocker = world->createFinished(
          7, {origin.x - separation, origin.y - 3}, 0, false);
      world->get(blocker)->motion.reset();
      world->get(blocker)->collisionPending = false;
      auto actor = world->createFinished(7, origin, 0, false);
      auto *unit = world->get(actor);
      unit->motion.emplace(origin, -95, physics);
      unit->motion->route({{origin.x - 62, origin.y + 61}}, 0);
      for (int i = 0; i < 5; ++i)
        unit->motion->advance();
      Point raw{origin.x * 256 + 10, origin.y * 256 + 18};
      unit->motion->waitForBody(raw, -95, 268);
      unit->motion->resumeBodyWait();
      unit->collisionPending = false;
      unit->order = 6;
      unit->orderState = 1;
      unit->targetPosition =
          unit->moveTargetPosition = {origin.x - 71, origin.y + 40};
      world->advance();
      check(samePoint(unit->motion->rawPosition(), raw));
      check(!unit->motion->waitingForBody() && unit->motion->speed() == 402);
      world->advance();
      check(unit->motion->speed() == 323 &&
            samePoint(unit->motion->rawPosition(), {raw.x, raw.y + 323}));
    }
    auto world = std::make_unique<World>(data, map);
    Point otherPosition{origin.x - 23, origin.y - 17};
    auto older = world->createFinished(
        7, {otherPosition.x - 1, otherPosition.y}, 0, false);
    auto original =
        world->createFinished(7, {origin.x - 23, origin.y - 12}, 0, false);
    world->get(original)->motion.reset();
    world->get(original)->collisionPending = false;
    auto *moving = world->get(older);
    moving->collisionPending = false;
    moving->gathering = true;
    moving->motion.emplace(moving->position, 64, physics);
    moving->motion->route({otherPosition}, 0);
    moving->moveTargetPosition = otherPosition;
    for (int i = 0; i < 20 && !samePoint(moving->position, otherPosition); ++i)
      world->advance();
    check(samePoint(moving->position, otherPosition));
    moving->motion.reset();
    moving->gathering = false;
    auto actor = world->createFinished(7, origin, 0, false);
    auto *unit = world->get(actor);
    unit->motion.emplace(origin, -95, physics);
    unit->motion->route({{origin.x - 67, origin.y + 57}}, 0);
    for (int i = 0; i < 9; ++i)
      unit->motion->advance();
    Point raw{origin.x * 256 + 32, origin.y * 256 + 156};
    unit->motion->slideBlocked({raw.x, raw.y - 368}, -95, -128, 536);
    unit->motion->advance();
    unit->motion->finishSlide(raw, false);
    check(unit->motion->speed() == 368);
    unit->slideObstacle = original;
    unit->collisionPending = false;
    unit->order = 6;
    unit->orderState = 1;
    unit->targetPosition =
        unit->moveTargetPosition = {origin.x - 76, origin.y + 36};
    auto withoutOriginal = std::make_unique<World>(*world);
    withoutOriginal->remove(original);
    world->advance();
    withoutOriginal->advance();
    check(unit->motion->speed() == 435 &&
          samePoint(unit->motion->rawPosition(), {raw.x, raw.y + 435}));
    check(withoutOriginal->get(actor)->motion->speed() == 435 &&
          samePoint(withoutOriginal->get(actor)->motion->rawPosition(),
                    {raw.x, raw.y + 256}));
  }
}
void testLarvaTraffic(const Definitions &seed,
                      std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  auto &baseType = data->units[131];
  baseType.flags = 1;
  baseType.bounds = {49, 32, 49, 32};
  baseType.placementWidth = 128;
  baseType.placementHeight = 96;
  baseType.humanIdle = baseType.returnIdle = 23;
  data->units[176].flags = 0x2001;
  data->units[176].bounds = {32, 16, 31, 15};
  data->units[176].humanIdle = data->units[176].returnIdle = 23;
  auto deposit = [&](std::array<int, 4> weights, Point point,
                     std::array<int, 4> expected) {
    auto world = std::make_unique<World>(data, map);
    auto base = world->createFinished(131, {800, 800}, 0, false);
    auto patch = world->createFinished(176, {1400, 800}, 11, false);
    world->setResources(patch, 1500);
    auto worker = world->createFinished(7, point, 0, false);
    auto *unit = world->get(worker);
    world->get(base)->larvaSpawnWeights = weights;
    unit->cargo = 2;
    unit->cargoAmount = 8;
    unit->order = 90;
    unit->orderState = 1;
    unit->orderTimer = 0;
    unit->target = unit->moveTargetUnit = base;
    unit->mineral = patch;
    unit->moveTargetPosition = point;
    auto before = std::make_unique<World>(*world);
    world->advance();
    check(world->get(base)->larvaSpawnWeights == expected && unit->cargo == 0 &&
          world->economy().resources(0).minerals == 8);
    check(before->get(base)->larvaSpawnWeights == weights &&
          before->get(worker)->cargo == 2);
  };
  deposit({7, 8, 9, 10}, {861, 816}, {6, 7, 33, 9});
  deposit({100, 100, 100, 100}, {861, 816}, {99, 99, 124, 99});
  deposit({101, 101, 101, 101}, {861, 816}, {100, 100, 100, 100});
  deposit({}, {861, 767}, {0, 25, 0, 0});
  deposit({}, {861, 768}, {0, 0, 25, 0});
  deposit({}, {861, 832}, {0, 0, 25, 0});
  deposit({}, {861, 833}, {0, 0, 0, 25});
  deposit({}, {739, 800}, {25, 0, 0, 0});
  deposit({}, {751, 800}, {0, 0, 25, 0});
  data->units[35].flags = 0x8000000U;
  data->units[35].bounds = {8, 8, 7, 7};
  data->units[35].humanIdle = data->units[35].returnIdle = 23;
  auto spawn = [&](std::array<int, 4> weights, std::optional<Point> expected,
                   int side) {
    auto world = std::make_unique<World>(data, map);
    auto base = world->createFinished(131, {800, 800}, 0, false, true);
    auto *unit = world->get(base);
    unit->larvaSpawnWeights = weights;
    unit->spawnLarvae = true;
    unit->orderTimer = 1;
    world->advance();
    const UnitState *child = nullptr;
    for (auto id : world->visibleUnits())
      if (world->units()[id]->type == 35)
        child = &*world->units()[id];
    check(bool(child) == bool(expected));
    if (child)
      check(samePoint(child->position, *expected) &&
            child->orderState == side && child->connected == base);
    check(unit->larvaSpawnWeights == weights &&
          unit->larvaTimer == (child ? 37 : 3));
  };
  spawn({}, Point{800, 842}, 3);
  spawn({0, 0, 70, 71}, Point{741, 800}, 0);
  spawn({100, 100, 100, 100}, Point{800, 842}, 3);
  spawn({101, 101, 101, 101}, Point{751, 842}, 3);
  spawn({9, 1, 9, 9}, Point{800, 758}, 1);
  spawn({9, 9, 1, 9}, Point{859, 800}, 2);
  spawn({1, 1, 9, 9}, Point{741, 800}, 0);
  spawn({9, 1, 1, 9}, Point{859, 800}, 2);
}
void testCollisionPauseReplacement(const Definitions &seed) {
  auto physics = seed.flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  Motion motion({800, 800}, 95, physics);
  motion.route({{1000, 1000}}, 0);
  for (int n = 0; n < 4; ++n)
    motion.advance();
  auto accepted = motion.rawPosition();
  motion.advance();
  motion.blockedWaypoint(accepted, 95, 268, true);
  auto velocity = motion.velocity();
  motion.advance();
  check(motion.moving());
  motion.advance();
  check(!motion.moving() && motion.speed() == 335 &&
        motion.nextSpeed() == 268 && samePoint(motion.velocity(), velocity) &&
        samePoint(motion.rawPosition(), accepted));
  auto snapshot = motion;
  snapshot.advance();
  check(snapshot.reroutesMovingObstacles() && snapshot.speed() == 335 &&
        snapshot.nextSpeed() == 268 &&
        samePoint(snapshot.rawPosition(), accepted));
  auto p = motion.position();
  motion.route({{p.x + 64, p.y - 4}}, 0);
  motion.advance();
  check(samePoint(motion.rawPosition(), accepted) && motion.facing() == 95);
  motion.advance();
  check(samePoint(motion.rawPosition(), accepted) && motion.facing() == 62 &&
        motion.travelHeading() == 75);
  motion.advance();
  check(samePoint(motion.rawPosition(), accepted) &&
        motion.travelHeading() == 62);
  motion.advance();
  check(motion.speed() == 335 && motion.moving() &&
        !samePoint(motion.rawPosition(), accepted) && !snapshot.moving() &&
        snapshot.speed() == 335);
}
void testFollowEscapeStop(const Definitions &seed,
                          std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  auto world = std::make_unique<World>(data, map);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto target = world->createFinished(7, {850, 820}, 0, false);
  auto *unit = world->get(actor);
  unit->motion.emplace(unit->position, 120, physics);
  unit->motion->route({{817, 1000}}, 0);
  for (int n = 0; n < 8; ++n)
    unit->motion->advance();
  unit->position = unit->motion->position();
  unit->order = 49;
  unit->orderState = 1;
  unit->orderTimer = unit->mainTimer = 0;
  unit->target = target;
  unit->moveTargetUnit = {};
  unit->moveTargetPosition = {817, 1000};
  unit->escapingOverlap = true;
  auto before = std::make_unique<World>(*world);
  world->advance();
  check(unit->escapingOverlap && unit->overlapStopPending &&
        !unit->planRequested && unit->moveTargetUnit.index < 0 && !unit->path &&
        samePoint(unit->motion->waypoint(), unit->moveTargetPosition) &&
        unit->moveTargetPosition.y < 900 &&
        before->get(actor)->escapingOverlap);
  auto endpoint = unit->moveTargetPosition;
  world->advance(2);
  unit->orderTimer = unit->mainTimer = 0;
  world->advance();
  check(samePoint(unit->moveTargetPosition, endpoint));
  world->advance(40);
  check(unit->motion->stopped() && unit->motion->speed() == 0 &&
        samePoint(unit->position, unit->moveTargetPosition));
}
void testFollowRetarget(const Definitions &seed,
                        std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto world = std::make_unique<World>(data, map);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto target = world->createFinished(7, {1200, 900}, 0, false);
  auto *unit = world->get(actor);
  unit->motion.emplace(unit->position, 64, data->flingies[0]);
  unit->motion->route({{820, 800}}, 0);
  unit->moveTargetPosition = unit->overlapReturnTarget = {1000, 800};
  unit->escapingOverlap = unit->overlapOrderChanged = unit->overlapFreshOrder =
      true;
  auto before = std::make_unique<World>(*world);
  world->follow(actor, target);
  world->advance();
  check(unit->order == 49 && unit->orderState == 1 && unit->target == target &&
        unit->moveTargetUnit == target && !unit->planRequested &&
        unit->escapingOverlap && unit->overlapOrderChanged &&
        !unit->overlapFreshOrder &&
        samePoint(unit->motion->waypoint(), {820, 800}) &&
        samePoint(unit->moveTargetPosition, {1200, 900}) &&
        samePoint(unit->overlapReturnTarget, {1200, 900}));
  check(before->get(actor)->target.index < 0 &&
        samePoint(before->get(actor)->moveTargetPosition, {1000, 800}));
}
void testOverlappingFixedBodies(const Definitions &seed,
                                std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = {11, 11, 11, 11};
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  data->units[106].flags = 1;
  data->units[106].bounds = {49, 32, 49, 32};
  data->units[106].humanIdle = data->units[106].returnIdle = 23;
  data->units[36].flags = 0;
  data->units[36].bounds = {16, 16, 15, 15};
  data->units[36].humanIdle = data->units[36].returnIdle = 23;
  for (Point base : {Point{800, 800}, Point{1000, 900}}) {
    auto world = std::make_unique<World>(data, map, 99);
    world->createFinished(106, base, 0, false);
    auto egg = world->createFinished(36, {base.x - 57, base.y + 21}, 0, false);
    auto actor = world->createFinished(7, {base.x - 36, base.y + 34}, 0, false);
    auto *unit = world->get(actor);
    unit->motion.emplace(unit->position, 104, data->flingies[0]);
    unit->order = 6;
    unit->orderState = unit->mainTimer = unit->orderTimer = 1;
    unit->targetPosition =
        unit->moveTargetPosition = {base.x + 86, base.y + 14};
    unit->collisionPending = unit->collisionRouteReset = true;
    unit->collisionWait = 1;
    auto random = world->randomState();
    auto single = std::make_unique<World>(*world);
    single->remove(egg);
    world->advance();
    single->advance();
    check(world->randomState() != random && unit->escapingOverlap);
    check(single->randomState() == random &&
          samePoint(single->get(actor)->motion->waypoint(),
                    {base.x - 36, base.y + 44}));
    // Reference oracle: the fixed pair samples (-41,+57) from the depot
    // origin; removing the smaller footprint selects the bottom edge instead.
    check(random == 4023473042U && world->randomState() == 316121912U);
    check(samePoint(unit->motion->waypoint(), {base.x - 41, base.y + 57}));
  }
}
void testConnectedDeparture(const Definitions &seed,
                            std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = {11, 11, 11, 11};
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto &building = data->units[106];
  building.flags = 1;
  building.bounds = {49, 32, 49, 32};
  building.placementWidth = 128;
  building.placementHeight = 96;
  building.humanIdle = building.returnIdle = 23;
  data->units[36].flags = 0;
  data->units[36].bounds = {16, 16, 15, 15};
  data->units[36].humanIdle = data->units[36].returnIdle = 23;
  struct Case {
    Point body;
    std::optional<Point> exit;
  };
  // Translated public-API footprint observations on synthetic flat terrain.
  std::array<Case, 16> cases{{
      {{772, 852}, Point{799, 844}},
      {{771, 852}, Point{798, 844}},
      {{773, 852}, Point{800, 844}},
      {{772, 863}, Point{799, 844}},
      {{772, 867}, Point{799, 844}},
      {{772, 871}, Point{861, 821}},
      {{772, 872}, Point{861, 821}},
      {{692, 852}, Point{861, 821}},
      {{822, 852}, Point{849, 844}},
      {{842, 852}, Point{861, 824}},
      {{852, 852}, Point{861, 824}},
      {{872, 852}, Point{861, 824}},
      {{772, 768}, Point{799, 756}},
      {{800, 800}, std::nullopt},
      {{772, 808}, std::nullopt},
      {{100, 100}, Point{861, 821}},
  }};
  for (const auto &fixture : cases) {
    auto world = std::make_unique<World>(data, map, 99);
    world->createFinished(106, {800, 800}, 0, false);
    world->createFinished(36, fixture.body, 0, false);
    auto actor = world->createFinished(7, {853, 821}, 0, false);
    auto target = world->createFinished(7, {1200, 900}, 0, false);
    auto *unit = world->get(actor);
    unit->motion.emplace(unit->position, 41, data->flingies[0]);
    unit->order = 49;
    unit->orderState = unit->orderTimer = 1;
    unit->target = target;
    unit->moveTargetPosition = {855, 820};
    unit->overlapStopRecovery = unit->collisionPending = true;
    unit->collisionWait = 1;
    auto random = world->randomState();
    auto snapshot = std::make_unique<World>(*world);
    world->advance();
    check(samePoint(unit->position, {853, 821}) && !unit->overlapStopRecovery);
    if (fixture.exit)
      check(world->randomState() == random && unit->escapingOverlap &&
            samePoint(unit->motion->waypoint(), *fixture.exit) &&
            samePoint(unit->moveTargetPosition, *fixture.exit));
    else
      check(world->randomState() != random);
    check(snapshot->get(actor)->overlapStopRecovery &&
          !snapshot->get(actor)->escapingOverlap &&
          samePoint(snapshot->get(actor)->moveTargetPosition, {855, 820}));
  }
}
void testStoppedOverlapTopology(const Definitions &seed,
                                std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  auto &worker = data->units[7];
  worker.bounds = {11, 11, 11, 11};
  worker.humanIdle = worker.returnIdle = 23;
  auto &mineral = data->units[176];
  mineral.flags = 0x2001;
  mineral.bounds = {32, 16, 31, 15};
  mineral.humanIdle = mineral.returnIdle = 23;
  struct Case {
    std::vector<Point> workers;
    bool neighbor;
    std::optional<Point> exit;
    bool mineral = true;
  };
  // Translated isolated-body reference observations: identical footprints,
  // diagonal junctions, and a third connected footprint have distinct effects.
  const std::array<Case, 6> cases{{
      {{}, false, Point{843, 814}},
      {{{843, 772}}, false, Point{843, 795}},
      {{{843, 772}, {843, 772}}, false, Point{843, 795}},
      {{{843, 772}}, true, std::nullopt},
      {{}, true, Point{811, 772}},
      {{{835, 808}}, false, Point{837, 831}, false},
  }};
  for (const auto &fixture : cases) {
    auto world = std::make_unique<World>(data, map, 99);
    if (fixture.mineral)
      world->createFinished(176, {800, 800}, 11, false);
    for (auto position : fixture.workers) {
      auto body = world->createFinished(7, position, 0, false);
      world->get(body)->motion.reset();
      world->get(body)->collisionPending = false;
    }
    if (fixture.neighbor)
      world->createFinished(176, {768, 768}, 11, false);
    auto actor = world->createFinished(7, {837, 814}, 0, false);
    auto target = world->createFinished(7, {1200, 900}, 0, false);
    auto *unit = world->get(actor);
    unit->motion.emplace(unit->position, 95, data->flingies[0]);
    unit->order = 49;
    unit->orderState = unit->orderTimer = 1;
    unit->target = target;
    unit->moveTargetPosition = unit->position;
    unit->overlapStopRecovery = unit->collisionPending = true;
    unit->collisionWait = 1;
    auto random = world->randomState();
    auto snapshot = std::make_unique<World>(*world);
    world->advance();
    check(samePoint(unit->position, {837, 814}));
    if (fixture.exit)
      check(world->randomState() == random &&
            samePoint(unit->motion->waypoint(), *fixture.exit));
    else
      check(world->randomState() != random);
    check(snapshot->get(actor)->overlapStopRecovery &&
          !snapshot->get(actor)->escapingOverlap && snapshot->frame() == 0);
  }
}
void testFollowContactRefresh(const Definitions &seed,
                             std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto world = std::make_unique<World>(data, map);
  auto target = world->createFinished(7, {600, 600}, 0, false);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto *unit = world->get(actor);
  unit->motion.emplace(unit->position, -32, data->flingies[0]);
  unit->motion->route({{623, 623}}, 0, false, false);
  unit->collisionPending = false;
  unit->order = 49;
  unit->orderState = 1;
  unit->orderTimer = unit->mainTimer = unit->followTimer = 0;
  unit->target = unit->moveTargetUnit = target;
  unit->followPosition = {600, 600};
  unit->moveTargetPosition = {623, 623};
  auto snapshot = std::make_unique<World>(*world);
  world->advance();
  // A cached actual target position does not suppress refreshing a distinct
  // projected/contact destination when the periodic Follow check expires.
  check(samePoint(unit->moveTargetPosition, {600, 600}) &&
        unit->moveTargetUnit == target && unit->planRequested &&
        unit->followTimer == 15);
  world->advance();
  check(unit->path && !unit->planRequested &&
        samePoint(unit->path->destination, {623, 623}) &&
        samePoint(unit->moveTargetPosition, {623, 623}));
  check(samePoint(snapshot->get(actor)->moveTargetPosition, {623, 623}) &&
        !snapshot->get(actor)->planRequested && snapshot->frame() == 0);
}
void testFollowRetainedBoundary(const Definitions &seed,
                                std::shared_ptr<const WorldMap> map) {
  for (bool asymmetric : {false, true}) {
    auto data = std::make_shared<Definitions>(seed);
    data->units[7].humanIdle = data->units[7].returnIdle = 23;
    data->units[41] = data->units[7];
    data->units[7].bounds =
        asymmetric ? decltype(data->units[7].bounds){14, 17, 20, 23}
                   : decltype(data->units[7].bounds){11, 11, 11, 11};
    data->units[41].bounds =
        asymmetric ? decltype(data->units[7].bounds){5, 8, 11, 14}
                   : decltype(data->units[7].bounds){11, 11, 11, 11};
    int left = asymmetric ? -26 : -23, top = asymmetric ? -32 : -23;
    int right = asymmetric ? 44 : 35, bottom = asymmetric ? 14 : 11;
    for (Point center : {Point{600, 600}, Point{900, 700}}) {
      for (auto [offset, retained] :
           std::vector<std::pair<Point, bool>>{{{left, 0}, true},
                                               {{right, 0}, true},
                                               {{0, top}, true},
                                               {{0, bottom}, true},
                                               {{left, top}, true},
                                               {{right, bottom}, true},
                                               {{left - 1, top}, false},
                                               {{right + 1, top}, false},
                                               {{0, top + 1}, false},
                                               {{0, bottom + 1}, false},
                                               {{1, 1}, false},
                                               {{0, 0}, true}}) {
        auto world = std::make_unique<World>(data, map);
        auto target = world->createFinished(41, center, 0, false);
        auto actor = world->createFinished(7, {center.x + 200, center.y + 200},
                                           0, false);
        auto *unit = world->get(actor);
        Point destination{center.x + offset.x, center.y + offset.y};
        unit->motion.emplace(unit->position, -32, data->flingies[0]);
        unit->motion->route({destination}, 0, false, false);
        unit->collisionPending = false;
        unit->order = 49;
        unit->orderState = 1;
        unit->orderTimer = unit->mainTimer = unit->followTimer = 0;
        unit->target = unit->moveTargetUnit = target;
        unit->followPosition = center;
        unit->moveTargetPosition = destination;
        world->advance();
        check(unit->planRequested == !retained);
        check(samePoint(unit->moveTargetPosition,
                        retained ? destination : center));
        check(unit->followTimer == (retained ? 0 : 15));
      }
    }
  }
}
void testBrakingRecoveryRetries(const Definitions &seed,
                                std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto &mineral = data->units[176];
  mineral.flags = 0x2001;
  mineral.bounds = {32, 16, 31, 15};
  mineral.humanIdle = mineral.returnIdle = 23;
  int directRetries = 0, rejectedRetries = 0;
  for (unsigned randomSeed = 0; randomSeed < 32; ++randomSeed) {
    Motion moving({800, 800}, 0, data->flingies[0]);
    moving.route({{800, 400}}, 0, false, false);
    for (int n = 0; n < 11; ++n)
      moving.advance();
    check(moving.speed() == 737);
    auto position = moving.position();
    Point goal{position.x, position.y - 11};
    auto world = std::make_unique<World>(data, map, randomSeed);
    auto body =
        world->createFinished(7, {position.x - 6, position.y - 9}, 0, false);
    world->get(body)->motion.reset();
    world->get(body)->collisionPending = false;
    world->get(body)->moveTargetPosition = {position.x - 6, position.y - 40};
    world->createFinished(176, {position.x - 49, position.y + 30}, 11, false);
    auto actor = world->createFinished(7, position, 0, false);
    auto target = world->createFinished(7, {1200, 900}, 0, false);
    auto *unit = world->get(actor);
    unit->motion = moving;
    unit->moveTargetPosition = goal;
    unit->order = 49;
    unit->orderState = unit->orderTimer = unit->mainTimer = 1;
    unit->target = target;
    unit->collisionPending = unit->overlapStopRecovery = true;
    unit->collisionWait = 1;
    auto ordinary = std::make_unique<World>(*world);
    ordinary->get(actor)->motion->route({goal}, 0);
    unit->motion->brakeTo(goal);
    auto raw = moving.rawPosition();
    Random firstDraw(world->randomState());
    firstDraw.next();
    world->advance();
    ordinary->advance();
    // Reference flag controls preserve the sampler but distinguish residual
    // braking travel from a parked, non-braking recovery retry.
    check(world->randomState() == ordinary->randomState());
    if (unit->collisionPending) {
      check(unit->motion->speed() == 670 &&
            samePoint(unit->motion->rawPosition(), {raw.x, raw.y - 670}) &&
            samePoint(ordinary->get(actor)->motion->rawPosition(), raw) &&
            ordinary->get(actor)->motion->speed() == 737);
      if (world->randomState() == firstDraw.state())
        ++directRetries;
      else
        ++rejectedRetries;
    }
  }
  check(directRetries > 0 && rejectedRetries > 0);
}
void testRepeatedEscapeMove(const Definitions &seed,
                            std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  auto world = std::make_unique<World>(data, map);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto *unit = world->get(actor);
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  unit->motion.emplace(unit->position, 104, physics);
  unit->motion->route({{823, 807}}, 0);
  unit->escapingOverlap = unit->overlapOrderChanged = true;
  unit->moveTargetPosition = unit->targetPosition = {1000, 800};
  unit->order = 6;
  unit->orderState = 1;
  world->advance(3);
  auto repeated = std::make_unique<World>(*world);
  repeated->move(actor, {1000, 800});
  world->advance(4);
  repeated->advance(4);
  const auto &expected = *world->get(actor)->motion;
  const auto &actual = *repeated->get(actor)->motion;
  check(samePoint(expected.rawPosition(), actual.rawPosition()) &&
        samePoint(expected.velocity(), actual.velocity()) &&
        expected.facing() == actual.facing() &&
        expected.travelHeading() == actual.travelHeading() &&
        expected.speed() == actual.speed());
}
void testQueuedSupplyAdmission(const Definitions &seed,
                               std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].mineralCost = 50;
  data->units[7].gasCost = 0;
  data->units[7].supplyRequired = 2;
  data->units[7].supplyProvided = 0;
  data->units[7].groupFlags = 2;
  data->units[106].flags = 1;
  data->units[106].groupFlags = 2;
  data->units[106].supplyProvided = 2;
  auto world = std::make_unique<World>(data, map);
  auto producer = world->createFinished(106, {800, 800}, 0, false);
  world->economy().set(0, {200, 0});
  check(world->train(producer, 7));
  world->createFinished(7, {1000, 800}, 0, false);
  check(world->population(0).used[1] == 2 &&
        world->population(0).available[1] == 2);
  auto snapshot = std::make_unique<World>(*world);
  check(world->train(producer, 7) &&
        world->get(producer)->buildQueue.size() == 2 &&
        world->economy().resources(0) == PlayerResources{100, 0});
  check(snapshot->get(producer)->buildQueue.size() == 1 &&
        snapshot->economy().resources(0) == PlayerResources{150, 0});
  auto empty = std::make_unique<World>(data, map);
  auto idle = empty->createFinished(106, {800, 800}, 0, false);
  empty->createFinished(7, {1000, 800}, 0, false);
  empty->economy().set(0, {200, 0});
  check(!empty->train(idle, 7) && empty->get(idle)->buildQueue.empty() &&
        empty->economy().resources(0) == PlayerResources{200, 0});
}
void testPlannedEggDeparture(const Definitions &seed,
                             std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].bounds = {11, 11, 11, 11};
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  data->units[36].flags = 0;
  data->units[36].bounds = {16, 16, 15, 15};
  data->units[36].humanIdle = data->units[36].returnIdle = 23;
  auto world = std::make_unique<World>(data, map, 99);
  world->createFinished(36, {802, 777}, 0, false);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto *unit = world->get(actor);
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  unit->motion.emplace(unit->position, 124, physics);
  LocalRouteResult route;
  route.source = unit->position;
  route.destination = {1000, 760};
  route.points = {{802, 825}, {866, 825}, route.destination};
  route.reached = true;
  unit->path = route;
  unit->motion->route(route.points, 0);
  unit->targetPosition = unit->moveTargetPosition = route.destination;
  unit->order = 6;
  unit->orderState = unit->orderTimer = 1;
  unit->collisionPending = true;
  unit->collisionWait = 1;
  auto random = world->randomState();
  auto snapshot = std::make_unique<World>(*world);
  world->advance();
  check(world->randomState() == random && unit->escapingOverlap &&
        samePoint(unit->position, {800, 800}) &&
        samePoint(unit->motion->waypoint(), {800, 804}) &&
        samePoint(unit->moveTargetPosition, route.destination) &&
        !unit->motion->brakesAtRouteEnd());
  check(snapshot->get(actor)->path && !snapshot->get(actor)->escapingOverlap &&
        samePoint(snapshot->get(actor)->motion->waypoint(), {802, 825}));
}
void testInitialClearDeparture(const Definitions &seed,
                               std::shared_ptr<const WorldMap> map) {
  auto data = std::make_shared<Definitions>(seed);
  data->units[7].humanIdle = data->units[7].returnIdle = 23;
  auto world = std::make_unique<World>(data, map);
  auto actor = world->createFinished(7, {800, 800}, 0, false);
  auto *unit = world->get(actor);
  unit->motion.emplace(unit->position, 64, data->flingies[0]);
  unit->order = 6;
  unit->orderState = 1;
  unit->moveTargetPosition = unit->targetPosition = {1000, 800};
  unit->collisionPending = unit->planRequested = true;
  unit->collisionWait = 1;
  world->advance();
  check(samePoint(unit->motion->rawPosition(), {204800, 204800}) &&
        unit->motion->speed() == 0 && !unit->collisionPending && unit->path);
  world->advance();
  check(unit->motion->speed() > 0 && unit->motion->rawPosition().x > 204800);

  auto shortExit = std::make_unique<World>(data, map);
  actor = shortExit->createFinished(7, {800, 800}, 0, false);
  unit = shortExit->get(actor);
  auto physics = data->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  unit->motion.emplace(unit->position, 104, physics);
  unit->motion->route({{809, 800}}, 0);
  unit->moveTargetPosition = unit->targetPosition = {1000, 800};
  unit->escapingOverlap = unit->overlapOrderChanged = unit->overlapFreshOrder =
      true;
  unit->order = 6;
  unit->orderState = 1;
  unit->overlapDirectRoute = false;
  shortExit->advance();
  auto regionalExit = std::make_unique<World>(*shortExit);
  regionalExit->get(actor)->overlapRegion =
      map->regions.regionAt(unit->position);
  shortExit->advance();
  check(!unit->collisionPending && unit->motion->speed() == 134 &&
        unit->motion->travelHeading() == 64);
  regionalExit->advance();
  check(regionalExit->get(actor)->collisionPending &&
        regionalExit->get(actor)->motion->speed() == 67);
}
} // namespace
void testProduction(const Definitions &seed,
                    std::shared_ptr<const WorldMap> map) {
  testProtossConstruction(seed, map);
  testZergConstruction(seed, map);
  testVisibility(seed);
  testRepeatedEscapeMove(seed, map);
  testRightClicks(seed, map);
  testOverlapOrders(seed, map);
  testOverlapDeparture(seed, map);
  testFreshBodyDeparture(seed, map);
  testArrivalFocus(seed);
  testBodyWaitRelease(seed, map);
  testMovingBodyBoundary(seed, map);
  testStationaryBodyBoundary(seed, map);
  testLarvaTraffic(seed, map);
  testCollisionPauseReplacement(seed);
  testFollowEscapeStop(seed, map);
  testFollowRetarget(seed, map);
  testOverlappingFixedBodies(seed, map);
  testConnectedDeparture(seed, map);
  testStoppedOverlapTopology(seed, map);
  testFollowContactRefresh(seed, map);
  testFollowRetainedBoundary(seed, map);
  testBrakingRecoveryRetries(seed, map);
  testInitialClearDeparture(seed, map);
  testPlannedEggDeparture(seed, map);
  testQueuedSupplyAdmission(seed, map);
  auto data = std::make_shared<Definitions>(seed);
  auto &worker = data->units[7];
  worker.mineralCost = 50;
  worker.gasCost = 25;
  worker.buildTime = 2;
  worker.supplyProvided = 0;
  worker.supplyRequired = 2;
  worker.groupFlags = 2;
  auto &base = data->units[106];
  base.flags = 1;
  base.bounds = {32, 32, 32, 32};
  base.humanIdle = 23;
  base.groupFlags = 2;
  base.supplyProvided = 8;
  auto &provider = data->units[154];
  provider = base;
  provider.supplyProvided = 4;
  testRallies(*data, map);
  auto world = std::make_unique<World>(data, map);
  auto producer = world->createFinished(106, {800, 800}, 0);
  world->economy().set(0, {500, 250});
  for (int n = 0; n < 5; ++n)
    check(world->train(producer, 7));
  check(!world->train(producer, 7) &&
        world->economy().resources(0) == PlayerResources{250, 125});
  auto cancellation = std::make_unique<World>(*world);
  check(!cancellation->cancelQueue(producer, -1) &&
        !cancellation->cancelQueue(producer, 5));
  check(cancellation->cancelQueue(producer, 2) &&
        cancellation->cancelQueue(producer, 254) &&
        cancellation->get(producer)->buildQueue.size() == 3 &&
        cancellation->economy().resources(0) == PlayerResources{350, 175});
  cancellation->advance();
  auto cancelledChild = cancellation->get(producer)->buildUnit;
  check(cancellation->cancelQueue(producer, 0));
  auto repeatHead = std::make_unique<World>(*cancellation);
  check(repeatHead->cancelQueue(producer, 0) &&
        repeatHead->get(producer)->buildQueue.size() == 1 &&
        repeatHead->economy().resources(0) == PlayerResources{400, 200});
  auto cancellationCopy = std::make_unique<World>(*cancellation);
  for (auto *copy : {cancellation.get(), cancellationCopy.get()}) {
    copy->advance();
    check(copy->get(producer)->buildUnit.index == -1 &&
          copy->get(producer)->secondaryState == 2 &&
          copy->population(0).used[1] == 0 &&
          copy->economy().resources(0) == PlayerResources{400, 200});
    copy->advance();
    check(copy->get(producer)->secondaryState == 0);
    copy->advance();
    check(copy->get(producer)->secondaryState == 2 &&
          copy->get(producer)->buildUnit != cancelledChild &&
          copy->population(0).used[1] == 2);
  }
  check(world->get(producer)->buildQueue.size() == 5);
  world->advance();
  auto child = world->get(producer)->buildUnit;
  check(world->get(child) && !world->get(child)->visible &&
        !world->get(child)->completed &&
        world->get(child)->remainingBuildTime == 2);
  world->advance(2);
  check(world->get(child)->remainingBuildTime == 0 &&
        !world->get(child)->completed);
  world->advance();
  check(world->get(child)->visible && world->get(child)->completed &&
        world->get(producer)->buildQueue.size() == 4);
  world->advance(20);
  check(world->population(0).completed[7] == 4 &&
        world->population(0).used[1] == 8);
  check(world->get(producer)->secondaryState == 1 &&
        world->get(producer)->buildUnit.index == -1 &&
        world->get(producer)->buildQueue.size() == 1);
  world->createFinished(154, {1200, 1200}, 0);
  world->advance(5);
  check(world->population(0).completed[7] == 5 &&
        world->get(producer)->buildQueue.empty());
  check(world->get(producer)->secondaryOrder == 23 &&
        world->economy().resources(0) == PlayerResources{250, 125});
  auto denied = world->createFinished(106, {1400, 800}, 1);
  check(!world->train(denied, 7) && world->get(denied)->buildQueue.empty());
  auto live = std::make_unique<World>(data, map);
  auto factory = live->createFinished(106, {800, 800}, 0);
  live->economy().set(0, {500, 250});
  auto rep = recording(factory, false);
  replay::ClassicRunner runner(rep, *live);
  runner.advanceTo(1);
  runner.advanceTo(1);
  runner.advanceTo(2);
  check(runner.commands() == 2 &&
        live->economy().resources(0) == PlayerResources{450, 225});
  auto cancelReplay = std::make_unique<World>(data, map);
  auto cancelFactory = cancelReplay->createFinished(106, {800, 800}, 0);
  cancelReplay->economy().set(0, {500, 250});
  auto cancelRecording = recording(cancelFactory, false, true);
  replay::ClassicRunner cancelRunner(cancelRecording, *cancelReplay);
  cancelRunner.advanceTo(2);
  check(cancelRunner.commands() == 3 &&
        cancelReplay->get(cancelFactory)->buildQueue.empty() &&
        cancelReplay->economy().resources(0) == PlayerResources{500, 250} &&
        cancelReplay->population(0).used[1] == 0);
  cancelRunner.advanceTo(4);
  check(cancelReplay->get(cancelFactory)->secondaryOrder == 23 &&
        !cancelReplay->cancelQueue(cancelFactory, 254));
  auto failed = std::make_unique<World>(data, map);
  factory = failed->createFinished(106, {800, 800}, 0);
  failed->economy().set(0, {500, 250});
  auto bad = recording(factory, true);
  replay::ClassicRunner poisoned(bad, *failed);
  bool rejected = false;
  try {
    poisoned.advanceTo(2);
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  check(rejected && failed->frame() == 1 && poisoned.commands() == 3);
  rejected = false;
  try {
    poisoned.advanceTo(3);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected &&
        failed->economy().resources(0) == PlayerResources{400, 200});
  auto morphData = std::make_shared<Definitions>(seed);
  std::vector<std::uint8_t> script(96);
  script[0] = 80;
  script[4] = 'S';
  script[5] = 'C';
  script[6] = 'P';
  script[7] = 'E';
  script[8] = 12;
  script[12] = 40;
  script[38] = 48;
  script[34] = script[36] = 40;
  script[40] = 5;
  script[41] = 125;
  script[42] = 7;
  script[43] = 40;
  script[48] = 5;
  script[49] = 1;
  script[50] = 36;
  script[51] = 4;
  script[52] = 5;
  script[53] = 125;
  script[54] = 7;
  script[55] = 52;
  script[82] = 4;
  script[84] = 255;
  script[85] = 255;
  morphData->program = iscript::Program(script);
  morphData->images[0].animated = true;
  for (int type : {35, 36, 37, 41, 42}) {
    auto &definition = morphData->units[type];
    definition.flags = 0;
    definition.humanIdle = 23;
    definition.hitpoints = 25600;
    definition.groupFlags = 1;
    definition.bounds = {4, 4, 4, 4};
    definition.buildTime = 2;
  }
  morphData->units[41].movementFlags = 193;
  morphData->units[41].supplyRequired = 2;
  morphData->units[41].mineralCost = 50;
  morphData->units[42].supplyProvided = 16;
  morphData->units[37].flags = 0x400;
  morphData->units[37].supplyRequired = 1;
  morphData->units[37].mineralCost = 50;
  morphData->units[142].hitpoints = 25600;
  morphData->units[142].humanIdle = 23;
  {
    auto pair = std::make_unique<World>(morphData, map);
    pair->createFinished(42, {500, 500}, 0);
    auto subject = pair->createFinished(35, {800, 800}, 0);
    pair->economy().set(0, {50, 0});
    check(!pair->morph(subject, 37) &&
          pair->economy().resources(0) == PlayerResources{50, 0});
    pair->createFinished(142, {500, 700}, 1);
    check(!pair->morph(subject, 37));
    pair->createFinished(142, {500, 900}, 0);
    auto limited = std::make_unique<World>(*pair);
    for (int i = 0; i < 7; ++i)
      limited->createFinished(41, {1000 + i * 20, 1000}, 0);
    limited->createFinished(37, {1200, 1200}, 0);
    check(limited->population(0).used[0] == 15 &&
          !limited->morph(subject, 37) &&
          limited->economy().resources(0) == PlayerResources{50, 0});
    check(pair->morph(subject, 37) && !pair->morph(subject, 37) &&
          pair->economy().resources(0) == PlayerResources{0, 0} &&
          pair->population(0).used[0] == 2);
    pair->advance();
    check(pair->get(subject)->type == 36 && pair->population(0).used[0] == 2);
    auto unborn = std::make_unique<World>(*pair);
    pair->advance(10);
    check(pair->get(subject)->type == 37 && pair->get(subject)->completed &&
          samePoint(pair->get(subject)->position, {812, 803}) &&
          pair->population(0).units[37] == 2 &&
          pair->population(0).completed[37] == 2 &&
          pair->population(0).used[0] == 2 &&
          unborn->population(0).units[37] == 0 &&
          unborn->population(0).used[0] == 2);
    bool foundSibling = false;
    for (int id : pair->visibleUnits()) {
      const auto *child = &*pair->units()[id];
      if (child && child->type == 37 && child->handle != subject)
        foundSibling = samePoint(child->position, {788, 797}) &&
                       child->completed && child->remainingBuildTime == 0;
    }
    check(foundSibling);
  }
  auto morphWorld = std::make_unique<World>(morphData, map);
  {
    auto movingData = std::make_shared<Definitions>(*morphData);
    auto physics = movingData->flingies[0];
    physics.speed = 1280;
    physics.acceleration = 67;
    physics.turnRate = 40;
    physics.control = 2;
    movingData->flingies[1] = physics;
    movingData->units[37].graphics = 1;
    movingData->units[37].flags |= 0x18000000U;
    auto moving = std::make_unique<World>(movingData, map);
    moving->createFinished(42, {500, 500}, 0);
    moving->createFinished(142, {500, 700}, 0);
    auto subject = moving->createFinished(35, {800, 800}, 0);
    auto *primary = moving->get(subject);
    primary->motion.emplace(primary->position, 0, physics);
    primary->motion->scriptSpeed(512);
    primary->sprite.context.speed = 512;
    moving->economy().set(0, {50, 0});
    check(moving->morph(subject, 37));
    moving->advance();
    for (int i = 0; i < 20 && !primary->completed; ++i)
      moving->advance();
    check(primary->completed && primary->motion &&
          primary->motion->speed() == 512 && primary->motion->nextSpeed() == 0);
    for (int id : moving->visibleUnits()) {
      const auto &child = *moving->units()[id];
      if (child.type == 37 && child.handle != subject)
        check(child.motion && child.motion->speed() == 0);
    }
    moving->advance();
    check(primary->motion->speed() == 0);
  }
  morphWorld->createFinished(42, {500, 500}, 0);
  auto larva = morphWorld->createFinished(35, {800, 800}, 0);
  auto identity = larva;
  morphWorld->economy().set(0, {50, 0});
  check(morphWorld->morph(larva, 41));
  check(!morphWorld->morph(larva, 41));
  check(morphWorld->economy().resources(0) == PlayerResources{0, 0});
  morphWorld->advance();
  check(morphWorld->get(larva)->type == 36 &&
        !morphWorld->get(larva)->completed &&
        morphWorld->get(larva)->remainingBuildTime == 2 &&
        morphWorld->population(0).used[0] == 2);
  auto snapshot = std::make_unique<World>(*morphWorld);
  morphWorld->advance(10);
  check(morphWorld->get(identity)->type == 41 &&
        morphWorld->get(identity)->completed &&
        morphWorld->get(identity)->remainingBuildTime == 0 &&
        morphWorld->get(identity)->buildQueue.empty() &&
        morphWorld->get(identity)->position.y == 793 &&
        morphWorld->population(0).used[0] == 2);
  check(snapshot->get(identity)->type == 36 &&
        !snapshot->get(identity)->completed);
  // Original scripts above isolate a moving Larva's physical state at birth.
  for (int sourceSpeed : {0, 512})
    for (int type : {41, 42}) {
      auto inertiaData = std::make_shared<Definitions>(*morphData);
      auto movingScript = script;
      movingScript[34] = 64;
      movingScript[64] = 41;
      movingScript[65] = std::uint8_t(sourceSpeed / 256);
      movingScript[66] = 5;
      movingScript[67] = 125;
      movingScript[68] = 7;
      movingScript[69] = 66;
      inertiaData->program = iscript::Program(movingScript);
      auto physics = inertiaData->flingies[0];
      physics.speed = 1280;
      physics.acceleration = 67;
      physics.turnRate = 40;
      physics.control = 0;
      inertiaData->flingies[1] = physics;
      for (int product : {41, 42}) {
        auto &definition = inertiaData->units[product];
        definition.graphics = 1;
        definition.flags = 0x18000000U | (product == 42 ? 4U : 8U);
        definition.initialDirection = 13;
      }
      auto active = std::make_unique<World>(inertiaData, map);
      active->createFinished(42, {500, 500}, 0, false);
      auto subject = active->createFinished(35, {800, 800}, 0, false);
      physics.control = 2;
      auto *unit = active->get(subject);
      unit->motion.emplace(unit->position, -13, physics);
      unit->motion->route({{720, 557}}, 0);
      unit->motion->scriptSpeed(sourceSpeed);
      active->economy().set(0, {100, 0});
      check(active->morph(subject, type));
      active->advance();
      check(!unit->motion && unit->inactiveInertia &&
            unit->inactiveInertia->speed == sourceSpeed &&
            unit->inactiveInertia->travelHeading == -13);
      auto frozen = std::make_unique<World>(*active);
      for (int i = 0; i < 20 && !unit->completed; ++i)
        active->advance();
      check(unit->completed && unit->motion && !unit->inactiveInertia);
      int inheritedSpeed = type == 41 ? sourceSpeed : 0;
      auto vector = directionVector(104);
      check(unit->motion->speed() == inheritedSpeed &&
            unit->motion->nextSpeed() == 0 && unit->motion->facing() == 104 &&
            samePoint(unit->motion->velocity(),
                      {fixedProduct(inheritedSpeed, vector.x),
                       fixedProduct(inheritedSpeed, vector.y)}));
      check(!frozen->get(subject)->completed &&
            frozen->get(subject)->inactiveInertia->speed == sourceSpeed);
      active->advance(2);
      check(unit->motion->speed() == 0 &&
            samePoint(unit->motion->velocity(), {}));
    }
  // Controlled birth layout: an Egg constrains the Hatchery's nearest exit.
  auto recoveryData = std::make_shared<Definitions>(*morphData);
  auto physics = recoveryData->flingies[0];
  physics.speed = 1280;
  physics.acceleration = 67;
  physics.turnRate = 40;
  physics.control = 0;
  recoveryData->flingies[1] = physics;
  auto &drone = recoveryData->units[41];
  drone.graphics = 1;
  drone.flags = 0x18000008U;
  drone.initialDirection = 13;
  drone.bounds = {11, 11, 11, 11};
  recoveryData->units[36].bounds = {16, 16, 15, 15};
  auto &hatch = recoveryData->units[131];
  hatch.flags = 1;
  hatch.groupFlags = 33;
  hatch.humanIdle = hatch.returnIdle = 23;
  hatch.bounds = {49, 32, 49, 32};
  hatch.graphics = 2;
  recoveryData->flingies[2].sprite = 1;
  recoveryData->sprites[1].image = 1;
  recoveryData->images[1].script = 0;
  recoveryData->images[1].animated = false;
  auto recovery = std::make_unique<World>(recoveryData, map);
  recovery->createFinished(42, {500, 500}, 0, false);
  auto hatchery = recovery->createFinished(131, {857, 793}, 0, false);
  check(recovery->rallyPoint(hatchery, {970, 804}));
  recovery->createFinished(36, {800, 753}, 0, false);
  auto subject = recovery->createFinished(35, {800, 807}, 0, false);
  recovery->get(subject)->connected = hatchery;
  recovery->economy().set(0, {100, 0});
  check(recovery->morph(subject, 41));
  recovery->advance();
  for (int i = 0; i < 20 && !recovery->get(subject)->completed; ++i)
    recovery->advance();
  check(recovery->get(subject)->completed &&
        samePoint(recovery->get(subject)->position, {800, 800}));
  auto random = recovery->randomState();
  recovery->advance();
  check(samePoint(recovery->get(subject)->motion->waypoint(), {796, 780}) &&
        recovery->randomState() == random);
  auto limitedData = std::make_shared<Definitions>(*morphData);
  limitedData->units[42].supplyProvided = 4;
  auto limited = std::make_unique<World>(limitedData, map);
  limited->createFinished(42, {500, 500}, 0);
  std::vector<UnitHandle> larvae;
  for (int x : {700, 900, 1100})
    larvae.push_back(limited->createFinished(35, {x, 800}, 0));
  limited->economy().set(0, {150, 0});
  check(limited->morph(larvae[0], 41) && limited->morph(larvae[1], 41));
  check(!limited->morph(larvae[2], 41) && limited->population(0).used[0] == 4 &&
        limited->economy().resources(0) == PlayerResources{50, 0});
}
