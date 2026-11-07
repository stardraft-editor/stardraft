// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/damage.h>
#include <mapeditor/simulation/mining_trial.h>
#include <mapeditor/simulation/path_pool.h>
#include <mapeditor/simulation/replay_identity.h>
#include <mapeditor/simulation/world.h>
#include <source_location>
using namespace mapeditor::simulation;
void check(bool value,
           std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("World test failed at line " +
                             std::to_string(at.line()));
}
void testEconomy();
void testProduction(const Definitions &, std::shared_ptr<const WorldMap>);
int main() {
  try {
    {
      UnitVitals unused{2560, 25600, 0};
      auto loss = normalHit(unused, false, 256, 0, 0);
      check(loss.hitpoints == 256 && !loss.shields && unused.shields == 25600);
      UnitVitals spill{2560, 320, 0};
      loss = normalHit(spill, true, 1280, 0, 256);
      check(loss.shields == 320 && loss.hitpoints == 704 && !spill.shields);
      UnitVitals fraction{2560, 255, 0};
      loss = normalHit(fraction, true, 256, 0, 2560);
      check(loss.hitpoints == 128 && fraction.shields == 255);
      UnitVitals small{2560, 0, 0}, medium = small, large = small;
      check(weaponHitLoss(small, false, 1280, 0, 256, 1, 1).hitpoints == 512);
      check(weaponHitLoss(medium, false, 1280, 0, 256, 1, 2).hitpoints == 768);
      check(weaponHitLoss(large, false, 1280, 0, 256, 2, 3).hitpoints == 256);
      UnitVitals bypass{2560, 320, 0};
      loss = weaponHitLoss(bypass, true, 1280, 256, 256, 4, 3);
      check(loss.shields == 320 && loss.hitpoints == 960);
      UnitVitals none{2560, 320, 0};
      loss = weaponHitLoss(none, true, 1280, 0, 0, 0, 3);
      check(loss.shields == 320 && !loss.hitpoints);
      check(weaponHitLoss(none, true, 1280, 0, 0, 0, 3).hitpoints == 128);
    }
    testEconomy();
    PathPool paths;
    auto firstPath = paths.acquire(), secondPath = paths.acquire();
    const std::array<int, 2> firstRegions{18, 17};
    const std::array<int, 1> secondRegions{187};
    paths.route(firstPath, firstRegions, 2);
    paths.route(secondPath, secondRegions, 49);
    paths.release(firstPath);
    paths.release(secondPath);
    auto pathSnapshot = paths;
    auto borrowed = paths.acquire();
    check(borrowed == secondPath && paths.firstRegion(borrowed) == 187 &&
          paths.record(borrowed).created == 49);
    paths.escape(borrowed);
    check(paths.firstRegion(borrowed) == 187 &&
          paths.freeOrder() == std::vector<int>{firstPath} &&
          pathSnapshot.freeOrder() ==
              std::vector<int>({firstPath, secondPath}));
    paths.route(borrowed, secondRegions, 49, {}, true);
    paths.escape(borrowed);
    check(!paths.firstRegion(borrowed) &&
          pathSnapshot.firstRegion(secondPath) == 187);
    paths.release(borrowed);
    bool repeatedRelease = false;
    try {
      paths.release(borrowed);
    } catch (const std::logic_error &) {
      repeatedRelease = true;
    }
    check(repeatedRelease && paths.acquire() == secondPath &&
          paths.acquire() == firstPath && paths.firstRegion(firstPath) == 18);
    SpatialBounds boundaries;
    {
      SpatialBounds sweep;
      for(unsigned i=0;i<60;++i)sweep.insert(i,{int(i*3),int(i*5)},{int(i*3+12),int(i*5+12)});
      auto expected=sweep.axes();
      std::uint32_t state=314159;
      auto next=[&]{state=state*1664525U+1013904223U;return state;};
      for(int n=0;n<5000;++n) {
        const auto id=next()%60;Point first{int(next()%80),int(next()%80)};
        Point last{first.x+int(next()%20),first.y+int(next()%20)};
        for(int axis=0;axis<2;++axis) {
          for(auto& e:expected[axis])if(e.key==id){auto p=e.maximum?last:first;e.coordinate=axis?p.y:p.x;}
          std::stable_sort(expected[axis].begin(),expected[axis].end(),[](auto a,auto b){return a.coordinate<b.coordinate;});
        }
        sweep.move(id,first,last);
        for(int axis=0;axis<2;++axis)for(std::size_t i=0;i<expected[axis].size();++i){const auto a=sweep.axes()[axis][i],b=expected[axis][i];check(a.coordinate==b.coordinate&&a.key==b.key&&a.maximum==b.maximum);}
      }
    }
    boundaries.insert(1, {100, 100}, {120, 120});
    boundaries.insert(2, {100, 100}, {120, 120});
    check(boundaries.axes()[0][0].key == 2 && boundaries.axes()[0][1].key == 1);
    boundaries.insert(3, {50, 50}, {70, 70});
    boundaries.move(3, {100, 100}, {120, 120});
    check(boundaries.axes()[0][0].key == 3 && boundaries.axes()[0][1].key == 2);
    auto boundaryCopy = boundaries;
    boundaries.move(3, {150, 150}, {170, 170});
    boundaries.move(3, {100, 100}, {120, 120});
    check(boundaries.axes()[0][0].key == 2 &&
          boundaries.axes()[0][2].key == 3 &&
          boundaryCopy.axes()[0][0].key == 3);
    boundaries.remove(2);
    check(boundaries.axes()[0].size() == 4 &&
          !boundaries.encounters({100, 100}).contains(2) &&
          boundaryCopy.axes()[0].size() == 6);
    SpatialBounds anchored;
    anchored.insert(10, {100, 100}, {120, 120});
    anchored.insert(11, {108, 108}, {112, 112});
    anchored.insert(12, {70, 1000}, {90, 1020});
    anchored.insert(13, {130, 1000}, {150, 1020});
    auto anchorRanks = anchored.encountersFrom(10);
    check(anchorRanks.at(11) == 0 && anchorRanks.at(12) == 12 &&
          anchorRanks.at(13) == 13);
    SpriteRows rows(64);
    auto anchor = rows.create({100, 100});
    auto older = rows.create({110, 100});
    auto newer = rows.create({120, 100});
    check(rows.ordered() == std::vector<SpriteRows::Key>{anchor, newer, older});
    auto rowCopy = rows;
    rows.move(older, {111, 100});
    check(rows.ordered() == rowCopy.ordered());
    rows.move(older, {111, 140});
    rows.move(older, {111, 100});
    check(rows.ordered() ==
              std::vector<SpriteRows::Key>{anchor, older, newer} &&
          rowCopy.ordered() ==
              std::vector<SpriteRows::Key>{anchor, newer, older});
    rows.remove(anchor);
    check(rows.ordered() == std::vector<SpriteRows::Key>{older, newer});
    auto readFixture = [](const std::string &name) {
          std::size_t size = name == "arr/units.dat"     ? 19876
                             : name == "arr/weapons.dat" ? 5460
                             : name == "arr/orders.dat" ? 4158
                             : name == "arr/flingy.dat"  ? 3135
                             : name == "arr/sprites.dat" ? 3229
                             : name == "arr/images.dat"  ? 37962
                             : name == "arr/images.tbl"  ? 13
                                                         : 64;
          std::vector<std::uint8_t> bytes(size);
          if (name == "arr/images.dat")
            bytes[13986 + 4 * 3996] = 1;
          if (name == "arr/images.tbl") {
            bytes[0] = 1;
            bytes[2] = 4;
            const std::string filename = "birth.lo";
            std::copy(filename.begin(), filename.end(), bytes.begin() + 4);
          }
          if (name == "unit/birth.lo") {
            bytes.assign(8 + 64 * 8, 0);
            bytes[0] = 64;
            bytes[4] = 2;
            for (int frame = 0; frame < 64; ++frame) {
              int start = 8 + 64 * 4 + frame * 4;
              bytes[8 + frame * 4] = std::uint8_t(start);
              bytes[9 + frame * 4] = std::uint8_t(start >> 8);
              bytes[start] = 12;
              bytes[start + 1] = 3;
              bytes[start + 2] = 244;
              bytes[start + 3] = 253;
            }
          }
          if (name == "scripts/iscript.bin") {
            bytes[0] = 56;
            bytes[4] = 'S';
            bytes[5] = 'C';
            bytes[6] = 'P';
            bytes[7] = 'E';
            bytes[12] = 16;
            bytes[14] = 32;
            bytes[16] = 5;
            bytes[17] = 125;
            bytes[18] = 7;
            bytes[19] = 16;
            bytes[32] = 22;
            bytes[58] = 4;
            bytes[60] = 255;
            bytes[61] = 255;
          }
          return bytes;
        };
    auto definitions = std::make_shared<Definitions>(readFixture);
    auto &worker = definitions->units[7];
    worker.flags = 0x18000008U;
    worker.hitpoints = 40 * 256 + 7;
    worker.shieldPoints = 123;
    worker.groupFlags = 2;
    worker.supplyRequired = 2;
    worker.supplyProvided = 4;
    worker.initialDirection = 32;
    worker.humanIdle = 2;
    worker.returnIdle = 2;
    worker.bounds = {11, 11, 11, 11};
    definitions->flingies[0] = {0, 1280, 12227, 67, 40, 0};
    definitions->images[0].directional = true;
    std::vector<std::uint8_t> cv(104), vf(32);
    for (int i = 0; i < 16; ++i)
      vf[i * 2] = 1;
    auto map = std::make_shared<WorldMap>(
        Terrain(64, 64, std::vector<std::uint16_t>(4096, 16), cv, vf));
    testProduction(*definitions, map);

    {
    auto world = std::make_unique<World>(definitions, map);
    auto handle = world->createFinished(7, {1000, 1000}, 0);
    world->move(handle, {1500, 1000});
    world->advance(30);
    auto *unit = world->get(handle);
    check(unit->path && unit->pathSlot >= 0 && unit->motion->moving());
    auto raw = unit->motion->rawPosition();
    int travel = unit->motion->travelHeading(), speed = unit->motion->speed();
    int priorPathFrame = world->pathPool().record(unit->pathSlot).created;
    unit->collisionPending = true;
    unit->collisionWait = 0;
    auto saved = std::make_unique<World>(*world);
    world->advance();
    check(samePoint(unit->motion->rawPosition(), raw) && unit->motion->speed() == speed &&
          unit->motion->travelHeading() == travel && !unit->collisionPending);
    check(unit->pathSlot >= 0 && world->pathPool().record(unit->pathSlot).created == world->frame());
    check(saved->get(handle)->collisionPending &&
          saved->pathPool().record(saved->get(handle)->pathSlot).created == priorPathFrame);
    auto prepared = std::make_unique<World>(*world);
    world->advance();
    check(!samePoint(unit->motion->rawPosition(), raw) &&
          samePoint(prepared->get(handle)->motion->rawPosition(), raw));
    }

    {
      auto powerData = std::make_shared<Definitions>(*definitions);
      std::vector<std::uint8_t> script(68);
      auto word = [&](int at, int value) {
        script[at] = std::uint8_t(value);
        script[at + 1] = std::uint8_t(value >> 8);
      };
      for (int i = 0; i != 4; ++i) script[4+i] = std::uint8_t("SCPE"[i]);
      word(8, 26);
      int loop = int(script.size());
      script.insert(script.end(), {5, 125, 7, std::uint8_t(loop), 0});
      for (int animation = 0; animation != 28; ++animation)
        word(12 + animation * 2, loop);
      int table = int(script.size()); script.resize(table + 8);
      word(0, table); word(table, 0); word(table+2, 4); word(table+4, 65535);
      powerData->program = iscript::Program(script);
      powerData->images[0].script = 0;
      powerData->images[0].animated = true;
      powerData->units[160] = powerData->units[7];
      auto &gateway = powerData->units[160];
      gateway.flags = 1 | 0x80000U; gateway.groupFlags = 4;
      gateway.humanIdle = gateway.returnIdle = 23;
      powerData->units[156] = gateway;
      powerData->units[156].flags = 1;
      World power(powerData, map);
      auto provider = power.createFinished(156, {900, 900}, 0);
      power.get(provider)->psiActive = true;
      auto building = power.createFinished(160, {1000, 900}, 0);
      auto animation = [&](const World &w) {
        const auto &sprite = w.get(building)->sprite;
        return sprite.images[sprite.main].cursor.animation;
      };
      power.advance(); check(animation(power) == 16);
      auto snapshot = power;
      power.remove(provider); power.advance();
      check(animation(power) == 24 && animation(snapshot) == 16);
      auto wait = power.get(building)->sprite.images[0].cursor.wait;
      power.advance();
      check(animation(power) == 24 &&
            power.get(building)->sprite.images[0].cursor.wait == wait - 1);
      auto foreign = power.createFinished(156, {900, 900}, 1);
      power.get(foreign)->psiActive = true;
      power.advance(); check(animation(power) == 24);
      auto fresh = power.createFinished(156, {900, 900}, 0);
      power.get(fresh)->psiActive = true;
      power.advance(); check(animation(power) == 27);
      snapshot.advance(); check(animation(snapshot) == 16);
      auto lifecycleData = std::make_shared<Definitions>(*powerData);
      lifecycleData->units[156].humanIdle = 164;
      for (bool death : {false, true}) {
        World lifecycle(lifecycleData, map);
        auto pylon = lifecycle.createFinished(156, {900, 900}, 0);
        lifecycle.advance();
        int field = lifecycle.get(pylon)->psiSprite;
        check(field >= 0 && lifecycle.effects()[field] &&
              lifecycle.get(pylon)->psiActive);
        auto saved = lifecycle;
        if (death) {
          lifecycle.get(pylon)->order = 0;
          lifecycle.advance();
        } else {
          lifecycle.remove(pylon);
        }
        check(!lifecycle.effects()[field] && saved.effects()[field] &&
              !lifecycle.get(pylon)->psiActive &&
              lifecycle.get(pylon)->psiSprite == -1);
        auto replacement = lifecycle.createFinished(156, {1100, 900}, 0);
        lifecycle.advance();
        check(lifecycle.get(replacement)->psiSprite == field &&
              lifecycle.effects()[field] && saved.effects()[field]);
      }

    }

    for (bool flying : {false, true}) {
      auto reuseData = std::make_shared<Definitions>(*definitions);
      reuseData->units[42] = reuseData->units[7];
      reuseData->units[42].flags |= 4;
      World recycled(reuseData, map);
      auto previous = recycled.createFinished(7, {400, 400}, 0);
      recycled.get(previous)->motion->inheritInertia({1280, 64, {1280, 0}});
      recycled.remove(previous);
      recycled.advance();
      auto snapshot = recycled;
      auto fresh = recycled.createFinished(7, {500, 400}, 0);
      check(fresh.index != previous.index && !recycled.get(fresh)->motion->speed());
      auto next = recycled.createFinished(flying ? 42 : 7, {600, 400}, 0);
      auto *body = recycled.get(next);
      const auto direction = directionVector(body->motion->facing());
      check(next.index == previous.index && next.generation != previous.generation &&
            !recycled.get(previous) && body->motion->speed() == 1280 &&
            !body->motion->nextSpeed() &&
            samePoint(body->motion->velocity(),
                      {fixedProduct(1280, direction.x), fixedProduct(1280, direction.y)}));
      body->motion->inheritInertia({0, 0, {}});
      recycled.remove(next);
      recycled.advance();
      snapshot.createFinished(7, {500, 400}, 0);
      auto copy = snapshot.createFinished(flying ? 42 : 7, {600, 400}, 0);
      check(copy.index == previous.index && snapshot.get(copy)->motion->speed() == 1280 &&
            !recycled.get(next));
    }
    for (int graphics : {0, 1}) {
      auto chaseData = std::make_shared<Definitions>(*definitions);
      chaseData->units[7].groundWeapon = 0;
      chaseData->units[7].sightRange = 11;
      chaseData->weapons[0].graphics = graphics;
      chaseData->weapons[0].maximumRange = 32;
      World chase(chaseData, map);
      auto actor = chase.createFinished(7, {900, 900}, 0);
      auto enemy = chase.createFinished(7, {1150, 900}, 1);
      chase.move(enemy, {1700, 900});
      chase.attack(actor, enemy);
      chase.advance(4);
      check(chase.get(enemy)->motion->moving() &&
            chase.get(actor)->moveTargetUnit == enemy);
      chase.get(actor)->followTimer = chase.get(actor)->orderTimer = 0;
      auto snapshot = chase;
      chase.advance();
      check(chase.get(actor)->target == enemy &&
            (graphics == 0 ? chase.get(actor)->moveTargetUnit.index < 0
                           : chase.get(actor)->moveTargetUnit == enemy) &&
            snapshot.get(actor)->moveTargetUnit == enemy);
    }
    {
      World pursuit(definitions, map);
      auto actor = pursuit.createFinished(7, {900, 900}, 0);
      auto target = pursuit.createFinished(7, {1300, 900}, 1);
      pursuit.move(target, {1500, 900});
      pursuit.move(actor, {1100, 900});
      pursuit.advance(8);
      check(pursuit.get(target)->motion->moving() &&
            pursuit.get(actor)->motion->speed() > 0);
      auto *body = pursuit.get(actor);
      body->order = 10;
      body->orderState = 1;
      body->orderTimer = 8;
      body->followTimer = 15;
      body->target = target;
      body->targetPosition = pursuit.get(target)->position;
      body->moveTargetPosition = {1100, 900};
      body->moveTargetUnit = target;
      body->planRequested = true;
      body->movementPreparation = body->routePlanningFrames = 0;
      pursuit.advance();
      check(body->path && body->path->reached);
      check(samePoint(body->path->destination, {1100, 900}));
      check(samePoint(body->moveTargetPosition, {1100, 900}));
    }
    {
      auto lockedData = std::make_shared<Definitions>(*definitions);
      lockedData->orders[6].interruptible = true;
      lockedData->units[7].groundWeapon = 0;
      World locked(lockedData, map);
      auto actor = locked.createFinished(7, {900, 900}, 0);
      auto enemy = locked.createFinished(7, {1100, 1100}, 1);
      auto *unit = locked.get(actor);
      unit->order = 10;
      unit->targetPosition = {950, 900};
      unit->sprite.context.uninterruptible = true;
      for (int kind : {14, 10, 133}) {
        auto pending = locked;
        if (kind == 14)
          pending.attackMove(actor, {1100, 900});
        else if (kind == 10)
          pending.attack(actor, enemy);
        else
          pending.rememberedRightClick(actor, {1100, 900}, 176);
        const auto *body = pending.get(actor);
        check(body->order == 10 && body->sprite.context.uninterruptible &&
              body->queuedCommands.size() == 1 &&
              body->queuedCommands.front().order == kind);
      }
      auto appended = locked;
      appended.get(actor)->queuedCommands.push_back({14, {1000, 1000}, {}});
      appended.move(actor, {1100, 900}, true);
      check(appended.get(actor)->order == 10 &&
            appended.get(actor)->queuedCommands.size() == 2 &&
            appended.get(actor)->queuedCommands.back().order == 6);
      locked.move(actor, {1100, 900});
      check(unit->order == 10 && samePoint(unit->targetPosition, {950, 900}) &&
            unit->sprite.context.uninterruptible &&
            unit->queuedCommands.size() == 1 &&
            unit->queuedCommands.front().order == 6);
      locked.guard(actor, {600, 1000});
      check(unit->order == 10 && unit->queuedCommands.size() == 1 &&
            unit->queuedCommands.front().order == 2);
      auto replaced = locked;
      replaced.get(actor)->sprite.context.uninterruptible = false;
      replaced.move(actor, {1150, 900});
      replaced.move(actor, {1200, 900}, true);
      check(samePoint(replaced.get(actor)->targetPosition, {1150, 900}) &&
            replaced.get(actor)->queuedCommands.size() == 1);
      auto snapshot = locked;
      unit->sprite.context.uninterruptible = false;
      locked.advance();
      check(unit->order != 10 && unit->queuedCommands.empty() &&
            samePoint(unit->targetPosition, {600, 1000}) &&
            snapshot.get(actor)->order == 10 &&
            snapshot.get(actor)->queuedCommands.size() == 1);
    }
    {
      auto data = std::make_shared<Definitions>(*definitions);
      data->orders[2].queueable = true;
      World moving(data, map);
      auto actor = moving.createFinished(7, {900, 900}, 0);
      moving.move(actor, {1100, 900});
      moving.advance(12);
      auto continued = moving, queued = moving;
      const auto goal = moving.get(actor)->moveTargetPosition;
      moving.guard(actor, {600, 1000});
      queued.guard(actor, {600, 1000}, true);
      moving.advance(3); continued.advance(3); queued.advance(3);
      check(moving.get(actor)->order == 3 &&
            samePoint(moving.get(actor)->targetPosition, {600, 1000}) &&
            samePoint(moving.get(actor)->moveTargetPosition, goal) &&
            samePoint(moving.get(actor)->motion->rawPosition(),
                      continued.get(actor)->motion->rawPosition()) &&
            queued.get(actor)->order == 6 &&
            queued.get(actor)->queuedCommands.size() == 1 &&
            queued.get(actor)->queuedCommands.front().order == 2);
    }
    {
      auto towerData = std::make_shared<Definitions>(*definitions);
      auto &tower = towerData->units[162];
      tower = towerData->units[7];
      tower.flags = 1 | 0x80000U;
      tower.humanIdle = tower.returnIdle = 18;
      tower.bounds = {16, 16, 16, 16};
      tower.placementWidth = tower.placementHeight = 64;
      tower.acquisitionRange = tower.sightRange = 7;
      tower.groundWeapon = tower.airWeapon = 0;
      towerData->weapons[0].maximumRange = 128;
      towerData->weapons[0].cooldown = 22;
      towerData->weapons[0].attackAngle = 128;
      auto &provider = towerData->units[156];
      provider = tower;
      provider.flags = 1;
      provider.humanIdle = 164;
      provider.returnIdle = 23;
      auto &enemy = towerData->units[36];
      enemy = tower;
      enemy.flags = 0;
      enemy.humanIdle = enemy.returnIdle = 23;
      enemy.groundWeapon = enemy.airWeapon = 130;
      enemy.bounds = {4, 4, 4, 4};
      World towers(towerData, map);
      auto cannon = towers.createFinished(162, {1100, 1000}, 0);
      auto target = towers.createFinished(36, {1200, 1000}, 1);
      towers.advance();
      check(towers.get(cannon)->order == 18 &&
            towers.get(cannon)->target.index < 0 &&
            towers.get(cannon)->mainTimer == 0);
      towers.createFinished(156, {1000, 1000}, 0);
      towers.advance();
      towers.get(cannon)->orderTimer = 0;
      towers.advance();
      check(towers.get(cannon)->order == 19 &&
            towers.get(cannon)->target == target &&
            towers.get(cannon)->mainTimer == 15 &&
            towers.get(cannon)->groundCooldown == 0);
      towers.advance();
      check(towers.get(cannon)->groundCooldown > 0 &&
            towers.get(cannon)->groundCooldown == towers.get(cannon)->airCooldown &&
            towers.get(cannon)->attackActive && !towers.get(cannon)->motion);
      auto retainedTarget = towers;
      towers.remove(target);
      towers.get(cannon)->orderTimer = 0;
      towers.advance();
      check(towers.get(cannon)->order == 18 &&
            towers.get(cannon)->target.index < 0 &&
            retainedTarget.get(cannon)->target == target);
    }
    {
      Motion blocked({900, 900}, 64, definitions->flingies[0]);
      blocked.route({{1800, 900}}, 0);
      for (int frame = 0; frame < 20; ++frame)
        blocked.advance();
      auto raw = blocked.rawPosition();
      int facing = blocked.facing(), reserved = blocked.nextSpeed();
      blocked.advance();
      blocked.waitForBody(raw, facing, reserved);
      check(blocked.waitingForBody() && blocked.speed() > 0);
      World retarget(definitions, map);
      auto actor = retarget.createFinished(7, blocked.position(), 0);
      auto obstacle = retarget.createFinished(
          7, {blocked.position().x + 24, blocked.position().y}, 0);
      auto *movingBody = retarget.get(obstacle);
      movingBody->moveTargetPosition = movingBody->targetPosition =
          {blocked.position().x + 400, blocked.position().y};
      movingBody->collisionPending = movingBody->planRequested = false;
      movingBody->order = 6;
      movingBody->orderState = 1;
      movingBody->motion->route({movingBody->moveTargetPosition}, 0);
      auto *unit = retarget.get(actor);
      unit->motion = blocked;
      unit->collisionObstacle = obstacle;
      unit->collisionPending = false;
      unit->order = 6;
      unit->orderState = 1;
      unit->targetPosition = unit->moveTargetPosition = {1800, 1200};
      unit->planRequested = true;
      unit->routePlanningFrames = 2;
      unit->walking = true;
      auto copy = retarget;
      retarget.advance();
      check(retarget.get(actor)->motion->speed() == 0 &&
            retarget.get(actor)->motion->nextSpeed() == 0 &&
            samePoint(retarget.get(actor)->motion->rawPosition(), raw) &&
            copy.get(actor)->motion->speed() == blocked.speed());
      retarget.advance();
      check(retarget.get(actor)->walking && copy.get(actor)->walking);
      retarget.advance();
      check(retarget.get(actor)->walking);
    }
    for (bool moving : {true, false}) {
      auto stopData = std::make_shared<Definitions>(*definitions);
      stopData->units[7].humanIdle = stopData->units[7].returnIdle = 23;
      stopData->units[7].groundWeapon = 0;
      stopData->weapons[0].maximumRange = 64;
      stopData->weapons[0].attackAngle = 128;
      Motion preview({900, 900}, 64, stopData->flingies[0]);
      preview.route({{1800, 900}}, 0);
      for (int frame = 0; frame < 14; ++frame)
        preview.advance();
      auto point = preview.position();
      preview.route({{point.x + 5, point.y}}, 0);
      preview.setRouteAge(1);
      const auto raw = preview.rawPosition();
      const int facing = preview.facing(), reserved = preview.nextSpeed();
      preview.advance();
      preview.blockedWaypoint(raw, facing, reserved, true);
      if (!moving) {
        preview.advance();
        preview.advance();
      }
      check(preview.speed() == 871 && preview.nextSpeed() == 938 &&
            preview.moving() == moving);
      World stopping(stopData, map);
      auto actor = stopping.createFinished(7, preview.position(), 0);
      auto enemy = stopping.createFinished(
          7, {preview.position().x + 24, preview.position().y}, 1);
      stopping.get(enemy)->collisionPending = false;
      auto *unit = stopping.get(actor);
      unit->motion = preview;
      unit->collisionPending = unit->planRequested = false;
      unit->collisionObstacle = unit->moveTargetUnit = enemy;
      unit->moveTargetPosition = unit->targetPosition = {point.x + 5, point.y};
      unit->order = 6;
      unit->orderState = 1;
      stopping.attack(actor, enemy);
      auto copy = stopping;
      stopping.advance();
      check(samePoint(stopping.get(actor)->moveTargetPosition,
                      Point{point.x + 5, point.y}) &&
            stopping.get(actor)->moveTargetUnit == enemy &&
            samePoint(stopping.get(actor)->motion->rawPosition(), raw) &&
            stopping.get(actor)->motion->speed() == 871 &&
            copy.get(actor)->motion->nextSpeed() == 938);
    }
    {
      Motion arrival({900, 900}, 64, definitions->flingies[0]);
      arrival.route({{901, 900}}, 0);
      for (int frame = 0; frame < 16 && !arrival.needsArrivalUpdate(); ++frame)
        arrival.advance();
      check(arrival.needsArrivalUpdate() && arrival.speed() > 0 &&
            samePoint(arrival.position(), {901, 900}));
      World overlap(definitions, map);
      auto actor = overlap.createFinished(7, arrival.position(), 0);
      auto obstacle = overlap.createFinished(7, arrival.position(), 0, false);
      check(samePoint(overlap.get(obstacle)->position, arrival.position()));
      overlap.get(obstacle)->collisionPending = false;
      auto *unit = overlap.get(actor);
      unit->motion = arrival;
      unit->collisionPending = unit->planRequested = false;
      unit->order = 6;
      unit->orderState = 1;
      unit->targetPosition = unit->moveTargetPosition = arrival.position();
      auto copy = overlap;
      auto delayed = copy;
      delayed.get(actor)->collisionPending = true;
      delayed.get(actor)->collisionWait = 2;
      auto acceptedArrival = arrival;
      acceptedArrival.advance();
      delayed.advance();
      check(delayed.get(actor)->motion->speed() == acceptedArrival.speed() &&
            delayed.get(actor)->motion->travelHeading() ==
                acceptedArrival.travelHeading() &&
            samePoint(delayed.get(actor)->motion->rawPosition(),
                      arrival.rawPosition()) &&
            delayed.get(actor)->collisionWait == 1 &&
            copy.get(actor)->motion->speed() == arrival.speed());
      overlap.advance();
      check(overlap.get(actor)->motion->speed() == arrival.speed() &&
            samePoint(overlap.get(actor)->motion->rawPosition(),
                      arrival.rawPosition()) &&
            copy.get(actor)->motion->speed() == arrival.speed());
      for (bool stopRequest : {false, true}) {
        bool checkedParkedArrival = false;
        for (unsigned seed = 0; seed < 16 && !checkedParkedArrival; ++seed) {
          World parked(definitions, map, seed);
          auto waiting = parked.createFinished(7, arrival.position(), 0);
          auto blocking = parked.createFinished(7, arrival.position(), 0, false);
          auto *body = parked.get(blocking);
          // Observe a mobile obstruction without advancing its own recovery.
          body->motion.reset();
          body->moveTargetPosition = {1000, 900};
          auto *waitingUnit = parked.get(waiting);
          waitingUnit->motion = arrival;
          if (stopRequest)
            waitingUnit->motion->brakeTo(waitingUnit->motion->stopTarget());
          waitingUnit->collisionPending = waitingUnit->planRequested = false;
          waitingUnit->order = 6;
          waitingUnit->orderState = 1;
          waitingUnit->targetPosition = waitingUnit->moveTargetPosition =
              arrival.position();
          Random decision(parked.randomState());
          if (decision.scaled(0, 31) >= 24)
            continue;
          auto retained = parked;
          parked.advance();
          check(parked.get(waiting)->motion->speed() == 0 &&
                parked.get(waiting)->collisionPending &&
                samePoint(parked.get(waiting)->motion->rawPosition(),
                          arrival.rawPosition()) &&
                retained.get(waiting)->motion->speed() == arrival.speed());
          checkedParkedArrival = true;
        }
        check(checkedParkedArrival);
      }
    }
    {
      Motion braking({900, 900}, 64, definitions->flingies[0]);
      braking.route({{1800, 900}}, 0);
      for (int frame = 0; frame < 12; ++frame)
        braking.advance();
      braking.brakeTo(braking.stopTarget());
      for (int frame = 0; frame < 64 && !braking.needsArrivalUpdate(); ++frame)
        braking.advance();
      check(braking.needsArrivalUpdate() && braking.brakingTravel());
      World restart(definitions, map);
      auto actor = restart.createFinished(7, braking.position(), 0);
      auto *unit = restart.get(actor);
      unit->motion = braking;
      unit->collisionPending = false;
      unit->planRequested = true;
      unit->routePlanningFrames = 0;
      unit->order = 6;
      unit->orderState = 1;
      unit->targetPosition = unit->moveTargetPosition =
          Point{unit->position.x + 100, unit->position.y};
      auto copy = restart;
      restart.advance();
      check(restart.get(actor)->motion->speed() == braking.speed() + 67 &&
            !samePoint(restart.get(actor)->motion->rawPosition(),
                       braking.rawPosition()) &&
            samePoint(copy.get(actor)->motion->rawPosition(),
                      braking.rawPosition()));
    }
    {
      World deaths(definitions, map);
      auto victim = deaths.createFinished(7, {900, 900}, 0);
      auto observer = deaths.createFinished(7, {1200, 900}, 0);
      deaths.get(observer)->order = 23;
      deaths.get(observer)->target = deaths.get(observer)->moveTargetUnit = victim;
      auto alive = deaths;
      deaths.get(victim)->vitals.hitpoints = 0;
      deaths.get(victim)->order = 0;
      deaths.advance();
      check(deaths.get(victim)->dead && deaths.get(victim)->animatedDeath &&
            !deaths.get(victim)->visible &&
            deaths.population(0).units[7] == 1 &&
            deaths.get(observer)->target.index < 0 &&
            deaths.get(observer)->moveTargetUnit.index < 0 &&
            !alive.get(victim)->dead && alive.population(0).units[7] == 2);
      auto corpse = deaths;
      deaths.advance();
      check(!deaths.get(victim) && corpse.get(victim)->dead);
      deaths.createFinished(7, {1100, 900}, 0);
      auto replacement = deaths.createFinished(7, {1150, 900}, 0);
      check(replacement.index == victim.index &&
            replacement.generation != victim.generation &&
            !deaths.get(victim) && corpse.get(victim));
    }
    for (int idleFrames : {0, 4}) {
      for (bool queued : {false, true}) {
        auto memoryData = std::make_shared<Definitions>(*definitions);
        memoryData->units[7].humanIdle = memoryData->units[7].returnIdle = 23;
        World memory(memoryData, map);
        auto actor = memory.createFinished(7, {900, 900}, 0);
        memory.advance(idleFrames);
        auto idleCopy = memory;
        const auto before = memory.get(actor)->motion->rawPosition();
        memory.rememberedRightClick(actor, {1800, 1800}, 178, queued);
        memory.advance();
        check(memory.get(actor)->order == 133 &&
              memory.get(actor)->orderState == 1 &&
              memory.get(actor)->followTimer == 15 &&
              idleCopy.get(actor)->order == 23);
        memory.advance();
        check(memory.get(actor)->motion->speed() > 0 &&
              !samePoint(memory.get(actor)->motion->rawPosition(), before) &&
              samePoint(idleCopy.get(actor)->motion->rawPosition(), before));
      }
    }
    for (int fastType : {37, 54}) {
      auto fastData = std::make_shared<Definitions>(*definitions);
      fastData->units[fastType] = fastData->units[7];
      fastData->units[fastType].humanIdle = 23;
      World fastOrders(fastData, map);
      auto subject = fastOrders.createFinished(fastType, {800, 800}, 0);
      auto *body = fastOrders.get(subject);
      body->orderTimer = 5;
      body->groundCooldown = 2;
      fastOrders.advance();
      check(body->groundCooldown == 1 && body->orderTimer == 4);
      auto coolingCopy = fastOrders;
      fastOrders.advance();
      check(body->groundCooldown == 0 && body->orderTimer == 8 &&
            coolingCopy.get(subject)->groundCooldown == 1 &&
            coolingCopy.get(subject)->orderTimer == 4);
      fastOrders.advance();
      check(body->orderTimer == 8);
    }
    {
      auto recoveryData = std::make_shared<Definitions>(*definitions);
      recoveryData->units[7].humanIdle = 23;
      recoveryData->units[36].humanIdle = 23;
      recoveryData->units[36].hitpoints = 25600;
      recoveryData->units[36].flags = 0;
      recoveryData->units[36].bounds = {4, 4, 4, 4};
      World idleRecovery(recoveryData, map);
      auto actor = idleRecovery.createFinished(7, {800, 800}, 0);
      idleRecovery.createFinished(36, {810, 800}, 0, false);
      auto *body = idleRecovery.get(actor);
      body->collisionPending = true;
      body->collisionWait = 1;
      auto before = idleRecovery.randomState();
      idleRecovery.advance();
      check(idleRecovery.randomState() == before && body->escapingOverlap &&
            samePoint(body->moveTargetPosition, {794, 800}));
    }
    {
      World combat(definitions, map);
      auto escapeData = std::make_shared<Definitions>(*definitions);
      escapeData->units[7].humanIdle = 23;
      escapeData->units[7].groundWeapon = 0;
      escapeData->weapons[0].maximumRange = 64;
      escapeData->weapons[0].attackAngle = 0;
      escapeData->units[36].humanIdle = 23;
      escapeData->units[36].hitpoints = 25600;
      escapeData->units[36].flags = 0;
      escapeData->units[36].bounds = {4, 4, 4, 4};
      World escapingAttack(escapeData, map);
      auto recovering = escapingAttack.createFinished(7, {1000, 1000}, 0);
      auto obstruction =
          escapingAttack.createFinished(36, {1008, 1000}, 0, false);
      auto *recoveringBody = escapingAttack.get(recovering);
      recoveringBody->collisionPending = true;
      recoveringBody->collisionWait = 1;
      escapingAttack.advance(2);
      check(recoveringBody->escapingOverlap && recoveringBody->motion->moving());
      auto distantFacing = escapingAttack.createFinished(7, {1010, 1035}, 1);
      escapingAttack.attack(recovering, distantFacing);
      escapingAttack.advance();
      check(recoveringBody->escapingOverlap && recoveringBody->orderState == 1 &&
            recoveringBody->orderTimer == 8 && !recoveringBody->attackActive &&
            recoveringBody->groundCooldown == 0);
      const auto committedAttackPoint = recoveringBody->targetPosition;
      auto blockedAttackCopy = escapingAttack;
      escapingAttack.remove(obstruction);
      recoveringBody->collisionPending = true;
      recoveringBody->collisionWait = 1;
      auto *movingEnemy = escapingAttack.get(distantFacing);
      auto enemyPhysics = escapeData->flingies[0];
      enemyPhysics.control = 2;
      movingEnemy->motion.emplace(movingEnemy->position, 64, enemyPhysics);
      movingEnemy->moveTargetPosition = {1080, 1035};
      movingEnemy->motion->route({{1080, 1035}}, 0);
      movingEnemy->motion->scriptSpeed(512);
      escapingAttack.advance(2);
      check(!recoveringBody->escapingOverlap);
      check(samePoint(recoveringBody->targetPosition, committedAttackPoint));
      check(!samePoint(escapingAttack.get(distantFacing)->position,
                       committedAttackPoint));
      check(blockedAttackCopy.get(recovering)->escapingOverlap);
      check(recoveringBody->motion->desiredHeading() ==
                heading({committedAttackPoint.x - recoveringBody->position.x,
                         committedAttackPoint.y - recoveringBody->position.y}));
      auto actor = combat.createFinished(7, {900, 900}, 0);
      auto enemy = combat.createFinished(7, {1400, 1400}, 1);
      combat.move(actor, {1100, 900});
      combat.attack(actor, enemy, true);
      check(combat.get(actor)->order == 6 &&
            combat.get(actor)->queuedCommands.size() == 1 &&
            combat.get(actor)->queuedCommands.front().order == 10 &&
            combat.get(actor)->queuedCommands.front().target == enemy);
      auto snapshot = combat;
      combat.move(actor, {900, 1100});
      check(combat.get(actor)->queuedCommands.empty() &&
            snapshot.get(actor)->queuedCommands.front().target == enemy);
      combat.attack(actor, enemy);
      combat.advance();
      check(combat.get(actor)->order == 10 &&
            combat.get(actor)->orderState == 1 &&
            combat.get(actor)->target == enemy &&
            combat.get(actor)->moveTargetUnit == enemy);
    }
    {
      auto pointDefinitions = std::make_shared<Definitions>(*definitions);
      pointDefinitions->weapons[0].maximumRange = 128;
      pointDefinitions->weapons[0].attackAngle = 0;
      pointDefinitions->units[7].groundWeapon = 0;
      pointDefinitions->units[7].humanIdle = 23;
      World combat(pointDefinitions, map);
      auto actor = combat.createFinished(7, {900, 900}, 0);
      auto enemy = combat.createFinished(7, {970, 950}, 1);
      combat.move(actor, {1150, 900});
      combat.advance(8);
      check(combat.get(actor)->motion->moving() && !combat.get(actor)->motion->braking());
      auto destination = combat.get(actor)->moveTargetPosition;
      auto pursuit = combat;
      pursuit.get(actor)->moveTargetUnit = enemy;
      pursuit.attack(actor, enemy);
      pursuit.advance();
      check(pursuit.get(actor)->orderState == 1 &&
            pursuit.get(actor)->moveTargetUnit == enemy &&
            samePoint(pursuit.get(actor)->moveTargetPosition, destination) &&
            !pursuit.get(actor)->motion->braking());
      auto pursuitSnapshot = pursuit;
      pursuit.advance(); pursuitSnapshot.advance();
      check(pursuit.get(actor)->motion->braking() &&
            pursuit.get(actor)->moveTargetUnit.index < 0 &&
            samePoint(pursuit.get(actor)->moveTargetPosition,
                      pursuitSnapshot.get(actor)->moveTargetPosition));
      auto snapshot = combat;
      combat.attack(actor, enemy);
      combat.get(actor)->followTimer = 4;
      combat.advance();
      check(combat.get(actor)->order == 10 && combat.get(actor)->orderState == 1 &&
            samePoint(combat.get(actor)->moveTargetPosition, destination) &&
            !combat.get(actor)->motion->braking() && snapshot.get(actor)->order == 6 &&
            combat.get(actor)->followTimer == 3);
      auto admittedSnapshot = combat;
      combat.advance(); admittedSnapshot.advance();
      check(combat.get(actor)->motion->braking() &&
            combat.get(actor)->followTimer == 15 &&
            !samePoint(combat.get(actor)->moveTargetPosition, destination) &&
            samePoint(admittedSnapshot.get(actor)->moveTargetPosition,
                      combat.get(actor)->moveTargetPosition));
    }
    {
      auto admissionDefinitions = std::make_shared<Definitions>(*definitions);
      admissionDefinitions->weapons[0].maximumRange = 128;
      admissionDefinitions->weapons[0].attackAngle = 0;
      admissionDefinitions->units[7].groundWeapon = 0;
      admissionDefinitions->units[7].humanIdle = 23;
      World combat(admissionDefinitions, map);
      auto actor = combat.createFinished(7, {900, 900}, 0);
      auto enemy = combat.createFinished(7, {970, 950}, 1);
      combat.attack(actor, enemy);
      combat.advance();
      check(combat.get(actor)->orderState == 1 &&
            combat.get(actor)->followTimer == 0);
      auto enemyPhysics = admissionDefinitions->flingies[0];
      enemyPhysics.control = 2;
      auto *movingEnemy = combat.get(enemy);
      movingEnemy->motion.emplace(movingEnemy->position, 64, enemyPhysics);
      movingEnemy->moveTargetPosition = {1230, 950};
      movingEnemy->motion->route({{1230, 950}}, 0);
      movingEnemy->motion->scriptSpeed(32768);
      combat.advance(2);
      check(combat.get(enemy)->position.x - combat.get(actor)->position.x >
                int(admissionDefinitions->weapons[0].maximumRange) + 32 &&
            combat.get(actor)->moveTargetUnit == enemy &&
            combat.get(actor)->followTimer >= 14);
    }
    {
      auto facingDefinitions = std::make_shared<Definitions>(*definitions);
      facingDefinitions->weapons[0].maximumRange = 128;
      facingDefinitions->units[7].groundWeapon = 0;
      facingDefinitions->units[7].humanIdle = 23;
      World combat(facingDefinitions, map);
      auto enemy = combat.createFinished(7, {935, 900}, 1);
      auto actor = combat.createFinished(7, {900, 900}, 0);
      combat.get(actor)->motion.emplace(Point{900, 900}, 64,
                                        facingDefinitions->flingies[0]);
      combat.get(actor)->sprite.setHeading(64);
      combat.get(actor)->groundCooldown = combat.get(actor)->airCooldown = 1000;
      combat.move(enemy, {935, 750});
      combat.attack(actor, enemy);
      combat.advance(9);
      check(combat.get(actor)->motion->braking() &&
            combat.get(enemy)->motion->moving());
      auto committedPoint = combat.get(actor)->targetPosition;
      auto snapshot = combat;
      combat.move(actor, {1100, 1100});
      snapshot.move(actor, {1100, 1100});
      combat.advance(); snapshot.advance();
      auto point = combat.get(actor)->position;
      check(!samePoint(committedPoint, combat.get(enemy)->position) &&
            combat.get(actor)->target.index < 0 &&
            combat.get(actor)->motion->facing() ==
                heading({committedPoint.x - point.x, committedPoint.y - point.y}) &&
            snapshot.get(actor)->motion->facing() ==
                combat.get(actor)->motion->facing());
      combat.advance();
      check(!combat.get(actor)->motion->braking());
    }
    {
      auto combatDefinitions = std::make_shared<Definitions>(*definitions);
      combatDefinitions->units[7].sightRange = 7;
      combatDefinitions->units[7].acquisitionRange = 2;
      combatDefinitions->units[7].turret = 228;
      combatDefinitions->weapons[0].maximumRange = 64;
      combatDefinitions->units[0].turret = 228;
      World combat(combatDefinitions, map);
      auto actor = combat.createFinished(7, {900, 900}, 0);
      combat.createFinished(7, {930, 900}, 1);
      auto threat = combat.createFinished(0, {950, 900}, 1);
      combat.createFinished(0, {910, 910}, 11);
      check(combat.acquireTarget(actor) == threat);
      auto snapshot = combat;
      combat.setAlliance(0, 1, true);
      check(combat.acquireTarget(actor).index < 0 &&
            snapshot.acquireTarget(actor) == threat && !snapshot.allied(0, 1));
    }
    {
      // A delayed impact lets copied worlds retain the shot's hit decision.
      auto shots = std::make_shared<Definitions>(*definitions);
      std::vector<std::uint8_t> script(80);
      auto word = [&](int at, int value) {
        script[at] = std::uint8_t(value);
        script[at + 1] = std::uint8_t(value >> 8);
      };
      auto idle = int(script.size());
      script.insert(script.end(), {5, 125, 7, std::uint8_t(idle), 0});
      auto firing = int(script.size());
      script.insert(script.end(), {37, 1, 5, 125, 7, std::uint8_t(idle), 0});
      auto impact = int(script.size());
      script.insert(script.end(), {5, 2, 27, 22});
      for (int header : {4, 40})
        for (int i = 0; i < 4; ++i)
          script[header + i] = std::uint8_t("SCPE"[i]);
      word(8, 12);
      for (int animation = 0; animation < 14; ++animation)
        word(12 + animation * 2,
             animation == 2 || animation == 5 ? firing : idle);
      word(44, 0);
      word(48, impact);
      word(50, idle);
      auto table = int(script.size());
      script.resize(script.size() + 12);
      word(0, table);
      word(table, 0); word(table + 2, 4);
      word(table + 4, 1); word(table + 6, 40);
      word(table + 8, 65535);
      shots->program = iscript::Program(script);
      shots->images[0].script = 0;
      shots->images[0].animated = true;
      shots->images[1].script = 1;
      shots->sprites[1].image = 1;
      shots->flingies[1].sprite = 1;
      shots->units[7].groundWeapon = 0;
      shots->units[7].humanIdle = shots->units[7].returnIdle = 23;
      {
        auto lostTarget = std::make_shared<Definitions>(*shots);
        auto polling = script;
        const int entry = int(polling.size());
        polling.insert(polling.end(), {5, 1, 48, 7, std::uint8_t(idle), 0});
        for (int animation : {2, 5}) {
          polling[12 + animation * 2] = std::uint8_t(entry);
          polling[13 + animation * 2] = std::uint8_t(entry >> 8);
        }
        lostTarget->program = iscript::Program(polling);
        World combat(lostTarget, map);
        auto victim = combat.createFinished(7, {1000, 900}, 1);
        auto actor = combat.createFinished(7, {900, 900}, 0);
        auto *unit = combat.get(actor);
        unit->order = 23;
        unit->orderTimer = 100;
        unit->target = victim;
        unit->sprite.context.target = true;
        Random animationRandom;
        unit->sprite.animate(5, lostTarget->resources(), animationRandom);
        auto snapshot = combat;
        combat.get(victim)->order = 0;
        combat.advance();
        check(unit->target.index < 0 && !unit->sprite.context.target &&
              unit->sprite.images[unit->sprite.main].cursor.animation == 8 &&
              snapshot.get(actor)->sprite.images[snapshot.get(actor)->sprite.main].cursor.animation == 2);
      }
      auto &weapon = shots->weapons[0];
      weapon.graphics = 1;
      weapon.behavior = 2;
      weapon.hitType = 1;
      weapon.damageType = 3;
      weapon.damage = 5;
      weapon.maximumRange = 64;
      weapon.attackAngle = 128;
      weapon.cooldown = 22;
      {
        World chasing(shots, map);
        auto actor = chasing.createFinished(7, {900, 900}, 0);
        auto target = chasing.createFinished(7, {1080, 900}, 1);
        chasing.get(actor)->groundCooldown = 6;
        chasing.attack(actor, target);
        chasing.advance();
        check(chasing.get(actor)->groundCooldown == 5 &&
              chasing.get(actor)->orderTimer == 4 &&
              chasing.activeProjectiles().empty());
      }
      for (int animation : {2, 3, 5, 6}) {
        bool sawLethal = false;
        for (unsigned seed = 0; seed != 16 && !sawLethal; ++seed) {
          World lethal(shots, map, seed);
          auto victim = lethal.createFinished(7, {935, 900}, 1);
          auto killer = lethal.createFinished(7, {900, 900}, 0);
          auto *body = lethal.get(victim);
          body->vitals.hitpoints = 256;
          body->orderTimer = 100;
          body->attackActive = true;
          body->sprite.context.uninterruptible = true;
          body->sprite.images[body->sprite.main].cursor.animation = animation;
          lethal.attack(killer, victim);
          for (int frame = 0; frame != 5 && body->vitals.hitpoints; ++frame)
            lethal.advance();
          if (!body->vitals.hitpoints) {
            sawLethal = true;
            check(body->order == 0 && !body->dead && body->visible);
            check(body->sprite.images[body->sprite.main].cursor.animation ==
                  (animation == 2 || animation == 5 ? 8 : 9));
            check(!body->attackActive && !body->sprite.context.uninterruptible);
          }
        }
        check(sawLethal);
      }
      bool sawMiss = false, sawHit = false;
      unsigned hittingSeed = 0, missingSeed = 0;
      for (unsigned seed = 0; seed < 2048 && !(sawMiss && sawHit); ++seed) {
        World combat(shots, map, seed);
        auto actor = combat.createFinished(7, {900, 900}, 0);
        auto target = combat.createFinished(7, {935, 900}, 1);
        combat.attack(actor, target);
        combat.advance();
        check(combat.activeProjectiles().size() == 1);
        bool missed = combat.projectiles()[combat.activeProjectiles().front()]->missed;
        if (!missed)
          check(samePoint(combat.projectiles()[combat.activeProjectiles().front()]->position,
                          {929, 900}));
        auto copy = combat;
        int before = combat.get(target)->vitals.hitpoints;
        combat.advance(); copy.advance();
        check(combat.get(target)->vitals.hitpoints == before - (missed ? 0 : 1280) &&
              copy.get(target)->vitals == combat.get(target)->vitals &&
              copy.randomState() == combat.randomState());
        auto movingCopy = combat;
        int cooldown = movingCopy.get(actor)->groundCooldown;
        movingCopy.move(actor, {1100, 900});
        check(movingCopy.get(actor)->sprite.images[movingCopy.get(actor)->sprite.main]
                  .cursor.animation == 8 && !movingCopy.get(actor)->attackActive &&
              movingCopy.get(actor)->groundCooldown == cooldown);
        // A repeated attack resets the active attack animation before cooldown.
        combat.attack(actor, target);
        check(combat.get(actor)->sprite.images[combat.get(actor)->sprite.main]
                  .cursor.animation == 8 && !combat.get(actor)->attackActive);
        if (missed) missingSeed = seed;
        else hittingSeed = seed;
        check(combat.get(target)->lastAttackingPlayer == (missed ? 8 : 0));
        sawMiss |= missed;
        sawHit |= !missed;
      }
      check(sawMiss && sawHit);
      {
        auto homing = std::make_shared<Definitions>(*shots);
        std::vector<std::uint8_t> program(128);
        auto putWord = [&](int at, int value) {
          program[at] = std::uint8_t(value);
          program[at + 1] = std::uint8_t(value >> 8);
        };
        const int waiting = int(program.size());
        program.insert(program.end(), {5, 125, 7, std::uint8_t(waiting),
                                       std::uint8_t(waiting >> 8)});
        const int fire = int(program.size());
        program.insert(program.end(), {37, 1, 5, 125, 7,
                                       std::uint8_t(waiting), std::uint8_t(waiting >> 8)});
        const int launch = int(program.size());
        program.insert(program.end(), {5, 1, 36, 1, 5, 125, 7,
                                       std::uint8_t(waiting), std::uint8_t(waiting >> 8)});
        const int impact = int(program.size());
        program.insert(program.end(), {27, 22});
        for (int header : {4, 64}) {
          for (int i = 0; i < 4; ++i)
            program[header + i] = std::uint8_t("SCPE"[i]);
          putWord(header + 4, 12);
          for (int animation = 0; animation < 14; ++animation)
            putWord(header + 8 + animation * 2,
                    header == 4 && (animation == 2 || animation == 5) ? fire
                    : header == 64 && animation == 0 ? launch
                    : header == 64 && animation == 1 ? impact : waiting);
        }
        const int table = int(program.size());
        program.resize(program.size() + 12);
        putWord(0, table);
        putWord(table, 0); putWord(table + 2, 4);
        putWord(table + 4, 1); putWord(table + 6, 64);
        putWord(table + 8, 65535);
        homing->program = iscript::Program(program);
        homing->flingies[160] = {1, 2048, 0, 256, 40, 1};
        homing->weapons[0].graphics = 160;
        homing->weapons[0].behavior = 1;
        homing->weapons[0].maximumRange = 256;
        homing->weapons[0].cooldown = 1000;
        homing->weapons[0].lifetime = 255;
        for (bool missing : {false, true}) {
          World combat(homing, map, missing ? missingSeed : hittingSeed);
          auto actor = combat.createFinished(7, {900, 900}, 0);
          auto target = combat.createFinished(7, {1080, 900}, 1);
          const int before = combat.get(target)->vitals.hitpoints;
          combat.attack(actor, target);
          combat.advance();
          check(combat.activeProjectiles().size() == 1);
          const int shot = combat.activeProjectiles().front();
          const auto &projectile = *combat.projectiles()[shot];
          check(projectile.flight && projectile.flight->active &&
                projectile.missed == missing &&
                combat.get(target)->vitals.hitpoints == before);
          auto stationaryCopy = combat;
          auto removedTarget = combat;
          removedTarget.remove(target);
          combat.move(target, {1080, 1100});
          combat.advance(8);
          stationaryCopy.advance(8);
          check(combat.projectiles()[shot] && stationaryCopy.projectiles()[shot]);
          check(samePoint(combat.projectiles()[shot]->flight->goal,
                          stationaryCopy.projectiles()[shot]->flight->goal) == missing);
          combat.advance(120);
          stationaryCopy.advance(120);
          removedTarget.advance(128);
          check(combat.activeProjectiles().empty() &&
                stationaryCopy.activeProjectiles().empty() &&
                removedTarget.activeProjectiles().empty() &&
                combat.get(target)->vitals.hitpoints == before - (missing ? 0 : 1280) &&
                stationaryCopy.get(target)->vitals.hitpoints == before - (missing ? 0 : 1280));
        }
      }
      auto reactive = std::make_shared<Definitions>(*shots);
      reactive->orders[3].fleeOnHit = true;
      reactive->units[7].humanIdle = reactive->units[7].returnIdle = 3;
      for (int scenario = 0; scenario < 5; ++scenario) {
        auto data = std::make_shared<Definitions>(*reactive);
        if (scenario == 4) {
          data->units[7].shields = true;
          data->units[7].shieldPoints = 5;
        }
        World damaged(data, map, scenario == 3 ? missingSeed : hittingSeed);
        auto actor = damaged.createFinished(7, {900, 900}, 0);
        auto target = damaged.createFinished(7, {935, 900}, 1);
        damaged.get(target)->mainTimer = 1000;
        if (scenario == 1) damaged.move(target, {935, 1100});
        if (scenario == 2) damaged.setAlliance(1, 0, true);
        damaged.attack(actor, target);
        int before = damaged.get(target)->vitals.hitpoints;
        damaged.advance();
        bool missed = damaged.projectiles()[damaged.activeProjectiles().front()]->missed;
        check(missed == (scenario == 3));
        auto snapshot = damaged;
        damaged.advance(); snapshot.advance();
        const auto *unit = damaged.get(target);
        check(unit->lastAttackingPlayer == (missed ? 8 : 0) &&
              snapshot.get(target)->lastAttackingPlayer == unit->lastAttackingPlayer &&
              snapshot.get(target)->targetPosition.x == unit->targetPosition.x &&
              snapshot.get(target)->targetPosition.y == unit->targetPosition.y);
        if (scenario == 0 || scenario == 4)
          check(unit->order == 6 && unit->orderState == 0 &&
                samePoint(unit->targetPosition, {1127, 900}) &&
                (scenario != 4 || (unit->vitals.hitpoints == before && unit->vitals.shields == 0)));
        else if (scenario == 1)
          check(unit->order == 6 && samePoint(unit->targetPosition, {935, 1100}));
        else
          check(unit->order == 3);
      }
      {
        auto armed = std::make_shared<Definitions>(*shots);
        armed->units[0] = armed->units[7];
        armed->units[0].flags &= ~8U;
        armed->units[0].turret = 228;
        armed->units[0].sightRange = 7;
        bool checked = false;
        for (unsigned seed = 0; seed < 64 && !checked; ++seed) {
          World combat(armed, map, seed);
          auto actor = combat.createFinished(7, {900, 900}, 0);
          auto victim = combat.createFinished(0, {935, 900}, 1);
          auto ally = combat.createFinished(0, {950, 910}, 1);
          auto workerAlly = combat.createFinished(7, {960, 920}, 1);
          auto armedEnemy = combat.createFinished(0, {950, 960}, 0);
          combat.attack(actor, victim);
          combat.advance();
          if (combat.projectiles()[combat.activeProjectiles().front()]->missed)
            continue;
          combat.advance();
          check(combat.get(victim)->retaliationTarget == actor &&
                combat.get(ally)->retaliationTarget == actor &&
                combat.get(workerAlly)->retaliationTarget.index < 0 &&
                combat.acquireTarget(ally) == actor);
          auto unnotified = combat;
          unnotified.get(ally)->retaliationTarget = {};
          check(unnotified.acquireTarget(ally) == armedEnemy);
          combat.move(ally, {1100, 1000});
          check(combat.get(ally)->retaliationTarget.index < 0 &&
                unnotified.get(victim)->retaliationTarget == actor);
          checked = true;
        }
        check(checked);
      }
      // Damage notification is distinct from ordinary radius acquisition.
      for (int scenario = 0; scenario < 4; ++scenario) {
        auto armed = std::make_shared<Definitions>(*shots);
        armed->units[0] = armed->units[7];
        armed->units[0].flags &= ~8U;
        armed->units[0].turret = 228;
        armed->units[0].acquisitionRange = 1;
        armed->units[0].airWeapon = 130;
        if (scenario == 3) armed->units[7].flags |= 4;
        World combat(armed, map);
        auto actor = combat.createFinished(0, {900, 900}, 0);
        auto enemy = combat.createFinished(7, {1200, 900}, 1);
        combat.get(actor)->order = 3;
        combat.get(actor)->orderTimer = 0;
        combat.get(actor)->mainTimer = 12;
        combat.get(actor)->retaliationTarget = enemy;
        combat.get(enemy)->orderTimer = 50;
        if (scenario == 1) combat.setAlliance(0, 1, true);
        if (scenario == 2) combat.get(enemy)->visible = false;
        check(combat.acquireTarget(actor).index < 0);
        combat.advance();
        const auto *unit = combat.get(actor);
        check(unit->order == (scenario == 0 ? 10 : 3));
        check(unit->mainTimer == 11);
        if (scenario == 0)
          check(unit->target == enemy && unit->orderState == 0 &&
                samePoint(unit->targetPosition, {1200, 900}));
        else
          check(unit->retaliationTarget.index < 0);
      }
      for (int scenario = 0; scenario < 5; ++scenario) {
        World perimeter(shots, map);
        const Point targetPoint{scenario == 2 ? 1081 : 1080, 900};
        auto actor = perimeter.createFinished(7, {900, 900}, 0);
        auto target = perimeter.createFinished(7, targetPoint, 1);
        auto other = perimeter.createFinished(7, {700, 700}, 1);
        auto *unit = perimeter.get(actor);
        unit->order = 10;
        unit->orderState = 1;
        unit->orderTimer = 0;
        unit->target = target;
        unit->targetPosition = targetPoint;
        unit->moveTargetUnit = scenario == 3 ? other : target;
        unit->moveTargetPosition = scenario == 4 ? targetPoint
                                  : Point{scenario == 1 ? 1058 : 1057, 900};
        unit->followTimer = 0;
        unit->immovable = scenario == 0 || scenario == 4;
        const auto accepted = unit->moveTargetPosition;
        auto snapshot = perimeter;
        perimeter.advance();
        const bool retained = scenario == 0 || scenario == 4;
        check(samePoint(unit->moveTargetPosition, retained ? accepted : targetPoint));
        check(unit->followTimer == (retained ? 0 : 15) &&
              unit->planRequested == !retained && !unit->immovable);
        check(!snapshot.get(actor)->planRequested &&
              samePoint(snapshot.get(actor)->moveTargetPosition, accepted));
      }
      for (bool shield : {false, true})
        for (bool victimFirst : {false, true}) {
          auto melee = std::make_shared<Definitions>(*shots);
          auto meleeScript = script;
          int entry = int(meleeScript.size());
          meleeScript.insert(meleeScript.end(),
                             {28, 1, 0, 0, 5, 125, 7, std::uint8_t(idle), 0});
          for (int animation : {2, 5}) {
            meleeScript[12 + animation * 2] = std::uint8_t(entry);
            meleeScript[13 + animation * 2] = std::uint8_t(entry >> 8);
          }
          melee->program = iscript::Program(meleeScript);
          melee->units[7].flags = shield ? melee->units[7].flags & ~0x80U
                                        : melee->units[7].flags | 0x80U;
          melee->units[7].shields = shield;
          melee->units[7].shieldPoints = 5;
          World combat(melee, map);
          UnitHandle actor, victim;
          if (victimFirst)
            victim = combat.createFinished(7, {935, 900}, 1);
          actor = combat.createFinished(7, {900, 900}, 0);
          if (!victimFirst)
            victim = combat.createFinished(7, {935, 900}, 1);
          int maximum = combat.get(victim)->vitals.hitpoints;
          combat.attack(actor, victim);
          combat.advance();
          check(combat.get(victim)->vitals.hitpoints ==
                    maximum - (shield ? 0 : 1280) + (!shield && !victimFirst ? 4 : 0) &&
                (!shield || combat.get(victim)->vitals.shields == (victimFirst ? 0 : 7)));
        }
      auto hillTiles = std::vector<std::uint16_t>(4096, 16);
      hillTiles[28 * 64 + 29] = 17;
      auto hillCv = cv, hillVf = vf;
      hillCv[74] = 1;
      hillVf.resize(64);
      for (int i = 0; i < 16; ++i)
        hillVf[32 + i * 2] = 3;
      auto hill = std::make_shared<WorldMap>(Terrain(64, 64, hillTiles, hillCv, hillVf));
      check(hill->terrain.groundHeight({900, 900}) == 0 &&
            hill->terrain.groundHeight({935, 900}) == 1);
      sawHit = sawMiss = false;
      for (unsigned seed = 0; seed < 64 && !(sawMiss && sawHit); ++seed) {
        World combat(shots, hill, seed);
        auto actor = combat.createFinished(7, {900, 900}, 0);
        auto target = combat.createFinished(7, {935, 900}, 1);
        Random expected(combat.randomState());
        expected.next(); // weapon cooldown jitter precedes the hit roll
        bool missed = (expected.next() & 255) <= 119;
        combat.attack(actor, target);
        combat.advance();
        check(combat.activeProjectiles().size() == 1 &&
              combat.projectiles()[combat.activeProjectiles().front()]->missed == missed);
        int before = combat.get(target)->vitals.hitpoints;
        combat.advance();
        check(combat.get(target)->vitals.hitpoints == before - (missed ? 0 : 1280));
        sawMiss |= missed;
        sawHit |= !missed;
      }
      check(sawMiss && sawHit);
    }
    {
      auto regenerative = std::make_shared<Definitions>(*definitions);
      regenerative->units[7].flags |= 0x80U;
      regenerative->units[7].humanIdle = 23;
      World healing(regenerative, map);
      auto actor = healing.createFinished(7, {500, 500}, 0);
      int maximum = healing.get(actor)->vitals.hitpoints;
      healing.get(actor)->vitals.hitpoints = maximum - 7;
      auto snapshot = healing;
      healing.advance();
      check(healing.get(actor)->vitals.hitpoints == maximum - 3 &&
            snapshot.get(actor)->vitals.hitpoints == maximum - 7);
      healing.advance();
      check(healing.get(actor)->vitals.hitpoints == maximum);
      healing.get(actor)->completed = false;
      healing.get(actor)->vitals.hitpoints = maximum - 10;
      healing.advance();
      check(healing.get(actor)->vitals.hitpoints == maximum - 10);
      healing.get(actor)->completed = true;
      healing.get(actor)->vitals.hitpoints = 0;
      healing.advance();
      check(healing.get(actor)->vitals.hitpoints == 0);
    }
    World original(definitions, map);
    auto first = original.createFinished(7, {300, 300}, 0);
    {
      auto population = original.population(0);
      check(population.units[7] == 1 && population.completed[7] == 1 &&
            population.used == std::array<int, 3>{0, 2, 0} &&
            population.available == std::array<int, 3>{0, 4, 0});
      // Reference-observed race priority, including all upper group bits.
      constexpr std::array<int, 8> raceForMask{-1, 0, 1, 0, 2, 0, 1, 0};
      for (int flags = 0; flags < 256; ++flags) {
        worker.groupFlags = flags;
        population = original.population(0);
        for (int race = 0; race < 3; ++race) {
          bool selected = raceForMask[flags & 7] == race;
          check(population.used[race] == (selected ? 2 : 0) &&
                population.available[race] == (selected ? 4 : 0));
        }
      }
      worker.groupFlags = 2;
      worker.supplyProvided = 255;
      auto overcap = std::make_unique<World>(original);
      overcap->createFinished(7, {600, 600}, 0);
      check(overcap->population(0).available[1] == 510);
      worker.supplyProvided = 4;
      auto tracked = std::make_unique<World>(original);
      tracked->get(first)->completed = false;
      tracked->get(first)->visible = false;
      population = tracked->population(0);
      check(population.units[7] == 1 && population.completed[7] == 0 &&
            population.used[1] == 2 && population.available[1] == 0);
      tracked->remove(first);
      tracked->remove(first);
      population = tracked->population(0);
      check(population.units[7] == 0 && population.used[1] == 0);
      check(original.population(0).units[7] == 1);
      tracked->economy().credit(0, {8, 0});
      check(original.economy().resources(0).minerals == 0 &&
            tracked->economy().resources(0).minerals == 8);
    }
    check(original.get(first)->vitals ==
          UnitVitals{40 * 256 + 7, 123 * 256, 50 * 256});
    auto hero = worker;
    hero.flags |= 0x40;
    check(finishedUnitVitals(7, hero).energy == 250 * 256 / 4);
    check(finishedUnitVitals(172, worker).energy == 100 * 256);
    auto emptyHealth = worker;
    emptyHealth.hitpoints = 0;
    check(finishedUnitVitals(7, emptyHealth).hitpoints == 1);
    emptyHealth.hitpoints = 255;
    check(finishedUnitVitals(7, emptyHealth).hitpoints == 255);
    check(first.index == 0 && first.generation == 1 &&
          original.randomState() == 1961185873U);
    check(original.get(first)->sprite.context.heading == 40 &&
          original.get(first)->completed);
    check(original.get(first)->motion->rawPosition().x == 300 * 256);
    const auto firstWire = replay::ClassicUnitId::fromHandle(first);
    auto aerialData = std::make_shared<Definitions>(*definitions);
    aerialData->units[7].flags |= 4;
    auto aerialWorld = std::make_unique<World>(aerialData, map);
    auto airborne = aerialWorld->createFinished(7, {300, 300}, 0);
    aerialWorld->move(airborne, {500, 300});
    aerialWorld->advance(32);
    check(aerialWorld->get(airborne)->position.x > 300 &&
          aerialWorld->get(airborne)->pathSlot < 0 &&
          aerialWorld->pathPool().records().empty());
    check(firstWire.raw() == 2049 && firstWire.resolve(original) == first);
    const auto identityRandom = original.randomState();
    for (std::uint32_t raw = 0; raw < 65536; ++raw) {
      auto handle = replay::ClassicUnitId(std::uint16_t(raw)).resolve(original);
      check(handle == (raw == firstWire.raw() ? first : UnitHandle{}));
    }
    check(original.randomState() == identityRandom && original.frame() == 0);
    auto recycledStorage = std::make_unique<World>(original);
    auto &recycled = *recycledStorage;
    auto current = first;
    for (int reuse = 1; reuse <= 34; ++reuse) {
      auto stale = replay::ClassicUnitId::fromHandle(current);
      recycled.remove(current);
      check(stale.resolve(recycled) == UnitHandle{});
      recycled.advance();
      check(stale.resolve(recycled) == UnitHandle{});
      int column = (reuse - 1) % 8, row = (reuse - 1) / 8;
      auto anchor =
          recycled.createFinished(7, {400 + column * 40, 500 + row * 80}, 0);
      current =
          recycled.createFinished(7, {400 + column * 40, 540 + row * 80}, 0);
      check(anchor.index >= 0 && current.index == first.index &&
            current.generation == unsigned(reuse + 1));
      check(replay::ClassicUnitId::fromHandle(current).resolve(recycled) ==
            current);
      check(firstWire.resolve(recycled) ==
            (reuse == 32 ? current : UnitHandle{}));
      check(!recycled.get(first));
    }
    check(firstWire.resolve(original) == first &&
          original.randomState() == identityRandom);
    auto repositioned = original;
    auto initialFacing = repositioned.get(first)->motion->facing();
    auto initialState = repositioned.randomState();
    auto initialOrder = repositioned.get(first)->order;
    repositioned.relocateWorkerForSearch(first, {500, 700});
    check(samePoint(repositioned.get(first)->position, {500, 700}) &&
          samePoint(repositioned.get(first)->motion->rawPosition(),
                    {128000, 179200}) &&
          repositioned.get(first)->motion->facing() == initialFacing &&
          repositioned.get(first)->order == initialOrder &&
          repositioned.randomState() == initialState &&
          original.get(first)->position.x == 300);
    repositioned.relocateWorkerForSearch(first, {-100, 99999});
    check(samePoint(repositioned.get(first)->position, {11, 2004}));
    auto advancedRelocation = original;
    advancedRelocation.advance();
    bool rejectedRelocation = false;
    try {
      advancedRelocation.relocateWorkerForSearch(first, {500, 700});
    } catch (const std::runtime_error &) {
      rejectedRelocation = true;
    }
    check(rejectedRelocation &&
          samePoint(advancedRelocation.get(first)->position, {300, 300}));
    MiningCycles singleCycle;
    singleCycle.delivery(0);
    for (int i = 1; i <= 4; ++i) {
      singleCycle.sample({100, 100}, 0);
      auto hash = singleCycle.hash;
      singleCycle.sample({100, 100}, 0);
      check(singleCycle.hash == hash);
      singleCycle.sample({120, 100}, 2);
      singleCycle.sample({100, 100}, 0);
      singleCycle.delivery(i * 147);
    }
    check(singleCycle.stable && singleCycle.period == 1 &&
          singleCycle.frames == 147 && singleCycle.completed == 4);
    MiningCycles supercycle;
    supercycle.delivery(0);
    int deliveryFrame = 0;
    for (int i = 0; i < 12; ++i) {
      int kind = i % 4 < 2 ? 0 : i % 4 - 1;
      supercycle.sample({100 + kind, 100}, 0);
      supercycle.sample({200 + kind, 100}, 2);
      supercycle.delivery(deliveryFrame += kind == 0   ? 147
                                           : kind == 1 ? 150
                                                       : 156);
    }
    check(supercycle.stable && supercycle.period == 4 &&
          supercycle.frames == 150 && supercycle.completed == 12);
    auto cycleCopy = supercycle;
    supercycle.sample({300, 300}, 2);
    check(supercycle.hash != cycleCopy.hash && cycleCopy.stable);
    MiningTrial batchedTrial(original, {}, {}, {}, 10, false, false, 0);
    batchedTrial.advance(4);
    auto splitTrial = batchedTrial;
    batchedTrial.advance(10);
    for (int i = 0; i < 6; ++i)
      splitTrial.advance(1);
    check(batchedTrial.done() && splitTrial.done() &&
          batchedTrial.frame == 10 && splitTrial.frame == 10 &&
          original.frame() == 0 &&
          batchedTrial.world.randomState() == splitTrial.world.randomState());
    auto copy = original;
    check(copy.get(first)->vitals == original.get(first)->vitals);
    copy.get(first)->vitals.hitpoints = 17;
    copy.get(first)->vitals.shields = 29;
    copy.get(first)->vitals.energy = 31;
    check(original.get(first)->vitals ==
          UnitVitals{40 * 256 + 7, 123 * 256, 50 * 256});
    copy.get(first)->vitals = original.get(first)->vitals;
    copy.get(first)->sprite.context.signal = 9;
    copy.get(first)->sprite.images[0].offset.x = 50;
    check(original.get(first)->sprite.context.signal == 0 &&
          original.get(first)->sprite.images[0].offset.x == 0);
    auto second = copy.createFinished(7, {600, 600}, 0);
    check(second.index == 1699 && !original.get(second) &&
          copy.randomState() != original.randomState());
    check(!original.workerPositionBlocked(first, {300, 300}) &&
          original.workerPositionBlocked(first, {-1, 300}) &&
          original.workerPositionBlocked(first, {300, 2047}) &&
          original.mineralDepot(first).index == -1);
    auto placementQuery = copy;
    check(placementQuery.workerPositionBlocked(first, {578, 600}) &&
          !placementQuery.workerPositionBlocked(first, {577, 600}) &&
          !placementQuery.workerPositionBlocked(first, {623, 600}));
    placementQuery.get(first)->gathering = true;
    check(!placementQuery.workerPositionBlocked(first, {600, 600}) &&
          copy.workerPositionBlocked(first, {600, 600}));
    bool invalidPlacement = false;
    try {
      original.workerPositionBlocked(second, {600, 600});
    } catch (const std::invalid_argument &) {
      invalidPlacement = true;
    }
    check(invalidPlacement);
    auto unsupportedWorld = copy;
    auto beforeUnsupported = unsupportedWorld.randomState();
    unsupportedWorld.get(first)->order = 255;
    unsupportedWorld.get(second)->order = 254;
    unsupportedWorld.get(second)->type = 64;
    bool rejectedUnsupported = false;
    try {
      unsupportedWorld.advance();
    } catch (const std::runtime_error &error) {
      rejectedUnsupported =
          std::string(error.what()).find("255 for unit type 7") !=
          std::string::npos;
    }
    check(rejectedUnsupported && unsupportedWorld.frame() == 0 &&
          unsupportedWorld.randomState() == beforeUnsupported);
    copy.remove(first);
    auto third = copy.createFinished(7, {300, 300}, 0);
    check(third.index == 1698 && copy.get(third)->position.x == 300 &&
          copy.get(third)->position.y == 300);
    check(copy.get(first)->dead && original.get(first)->visible);
    check(!copy.get({first.index, first.generation + 1}) && !copy.get({}));
    auto moving = original;
    moving.followRoute(first, {{556, 300}});
    auto split = moving;
    moving.advance(100);
    for (int i = 0; i < 100; ++i)
      split.advance();
    check(moving.frame() == 100 && split.frame() == 100 &&
          moving.randomState() == split.randomState());
    check(moving.get(first)->position.x == 556 &&
          split.get(first)->position.x == 556);
    check(moving.get(first)->order == split.get(first)->order &&
          moving.get(first)->mainTimer == split.get(first)->mainTimer);
    check(original.frame() == 0 && original.get(first)->position.x == 300);
    int noCargoOrder = original.get(first)->order;
    check(!original.returnCargo(first) &&
          original.get(first)->order == noCargoOrder && original.frame() == 0);
    auto idleCommand = original;
    idleCommand.advance();
    int idleTimer = idleCommand.get(first)->mainTimer;
    idleCommand.move(first, {556, 300});
    check(idleCommand.get(first)->mainTimer == idleTimer);
    idleCommand.advance(2);
    check(idleCommand.get(first)->motion->rawPosition().x > 300 * 256);
    auto unchangedDestination = original;
    unchangedDestination.move(first, {556, 300});
    unchangedDestination.advance(20);
    auto continued = unchangedDestination;
    auto retainedSource = unchangedDestination.get(first)->path->source;
    unchangedDestination.move(first, {556, 300});
    unchangedDestination.advance(5);
    continued.advance(5);
    check(samePoint(unchangedDestination.get(first)->motion->rawPosition(),
                    continued.get(first)->motion->rawPosition()) &&
          samePoint(unchangedDestination.get(first)->path->source,
                    retainedSource));
    bool rejectedMove = false;
    auto validTarget = unchangedDestination.get(first)->targetPosition;
    try {
      unchangedDestination.move(first, {-1, 300});
    } catch (const std::out_of_range &) {
      rejectedMove = true;
    }
    check(rejectedMove &&
          samePoint(unchangedDestination.get(first)->targetPosition,
                    validTarget));
    auto commanded = original;
    commanded.move(first, {556, 300});
    commanded.advance(20);
    auto beforeCommand = commanded.get(first)->position;
    auto previousPathSource = commanded.get(first)->path->source;
    commanded.move(first, {128, 300});
    auto commandCopy = commanded;
    commanded.advance();
    commandCopy.advance();
    auto acceptedPosition = commanded.get(first)->position;
    check(acceptedPosition.x > beforeCommand.x &&
          samePoint(commanded.get(first)->path->source, previousPathSource));
    commanded.advance();
    commandCopy.advance();
    check(samePoint(commanded.get(first)->path->source, acceptedPosition));
    commanded.advance(140);
    for (int i = 0; i < 140; ++i)
      commandCopy.advance();
    check(samePoint(commanded.get(first)->position, {128, 300}) &&
          samePoint(commanded.get(first)->motion->rawPosition(),
                    commandCopy.get(first)->motion->rawPosition()) &&
          commanded.randomState() == commandCopy.randomState() &&
          commanded.get(first)->order == commandCopy.get(first)->order &&
          commanded.get(first)->mainTimer == commandCopy.get(first)->mainTimer);
    check(original.frame() == 0 && original.get(first)->position.x == 300);
    auto illusion = original;
    illusion.setHallucinated(first);
    illusion.advance(1349);
    check(illusion.get(first)->visible &&
          illusion.get(first)->removeTimer == 1 &&
          !original.get(first)->hallucinated);
    illusion.setHallucinated(first);
    check(illusion.get(first)->removeTimer == 1);
    auto illusionCopy = illusion;
    auto effectsBefore = illusion.activeEffects().size();
    illusion.advance();
    illusionCopy.advance();
    check(illusion.get(first)->dead && illusionCopy.get(first)->dead &&
          illusion.activeEffects().size() == effectsBefore + 1 &&
          illusion.randomState() == illusionCopy.randomState());
    illusion.advance();
    check(!illusion.get(first) && original.get(first)->visible);
    copy.advance();
    check(!copy.get(first));
    auto nextFresh = copy.createFinished(7, {300, 300}, 0);
    check(nextFresh.index == 1697);
    auto reused = copy.createFinished(7, {800, 800}, 0);
    check(reused.index == first.index &&
          reused.generation == first.generation + 1 && !copy.get(first));
    moving.addSprite(0, {400, 400}, 0);
    moving.addSprite(0, {500, 500}, 0);
    moving.addSprite(0, {600, 600}, 0);
    check(std::vector<int>(moving.activeEffects().begin(),
                           moving.activeEffects().end()) ==
          std::vector<int>({0, 3, 2, 1}));
    auto effectsCopy = moving;
    int wait = moving.effects()[0]->sprite.images[0].cursor.wait;
    effectsCopy.advance();
    check(moving.effects()[0]->sprite.images[0].cursor.wait == wait &&
          effectsCopy.effects()[0]->sprite.images[0].cursor.wait == wait - 1);
    auto &mineralType = definitions->units[176];
    mineralType.flags = 0x2001;
    mineralType.bounds = {32, 16, 31, 15};
    mineralType.placementWidth = 64;
    mineralType.placementHeight = 32;
    mineralType.humanIdle = mineralType.returnIdle = 23;
    {
    Motion collisionApproach({700, 800}, 64, definitions->flingies[0]);
    collisionApproach.route({{1200, 800}}, 0);
    for (int step = 0; step < 30; ++step)
      collisionApproach.advance();
    auto acceptedApproach = collisionApproach.position();
    auto recoveryTraffic = std::make_unique<World>(definitions, map);
    Point stoppedBody{acceptedApproach.x + 23, acceptedApproach.y};
    auto recoveringBlocker = recoveryTraffic->createFinished(7, stoppedBody, 0);
    auto waitingActor = recoveryTraffic->createFinished(7, acceptedApproach, 0);
    auto *recoveryBody = recoveryTraffic->get(recoveringBlocker);
    recoveryBody->motion.emplace(stoppedBody, 64, definitions->flingies[0]);
    Point bodyGoal{stoppedBody.x + 200, stoppedBody.y};
    recoveryBody->motion->route({bodyGoal}, 0);
    recoveryBody->motion->blockedWaypoint(recoveryBody->motion->rawPosition(), 64, 0);
    for (int step = 0; step < 8; ++step)
      recoveryBody->motion->advance();
    check(recoveryBody->motion->replanning() && !recoveryBody->motion->moving());
    recoveryBody->moveTargetPosition = recoveryBody->targetPosition = bodyGoal;
    recoveryBody->order = 6;
    recoveryBody->orderState = 1;
    auto *approachingRecovery = recoveryTraffic->get(waitingActor);
    approachingRecovery->motion = collisionApproach;
    approachingRecovery->moveTargetPosition = approachingRecovery->targetPosition = {1200, 800};
    approachingRecovery->order = 6;
    approachingRecovery->orderState = 1;
    auto stationaryTraffic = std::make_unique<World>(*recoveryTraffic);
    auto *stationaryBody = stationaryTraffic->get(recoveringBlocker);
    stationaryBody->motion.emplace(stoppedBody, 64, definitions->flingies[0]);
    stationaryBody->moveTargetPosition = stationaryBody->targetPosition = stoppedBody;
    stationaryBody->order = 3;
    stationaryBody->orderState = 0;
    auto turningTraffic = std::make_unique<World>(*recoveryTraffic);
    auto *turningBody = turningTraffic->get(recoveringBlocker);
    turningBody->motion->blocked(turningBody->motion->rawPosition(), 64, 0);
    check(turningBody->motion->turningForRoute() && !turningBody->motion->moving());
    turningTraffic->advance();
    recoveryTraffic->advance();
    stationaryTraffic->advance();
    check(approachingRecovery->motion->waitingForBody() &&
          turningTraffic->get(waitingActor)->motion->waitingForBody() &&
          !stationaryTraffic->get(waitingActor)->motion->waitingForBody());
    }
    auto gasDefinitions = std::make_shared<Definitions>(*definitions);
    auto &geyserType = gasDefinitions->units[188];
    geyserType.flags = 0x2001;
    geyserType.bounds = {64, 32, 63, 31};
    geyserType.placementWidth = 128;
    geyserType.placementHeight = 64;
    geyserType.humanIdle = geyserType.returnIdle = 23;
    auto gasWorld = std::make_unique<World>(gasDefinitions, map);
    auto gasWorker = gasWorld->createFinished(7, {1000, 1000}, 0);
    auto geyser = gasWorld->createFinished(188, {1408, 992}, 11);
    auto wrongGasTarget = gasWorld->createFinished(176, {1700, 1000}, 11);
    bool invalidGasRejected = false;
    try {
      gasWorld->harvest(gasWorker, wrongGasTarget, HarvestOrder::Gas);
    } catch (const std::invalid_argument &) {
      invalidGasRejected = true;
    }
    check(invalidGasRejected);
    gasWorld->harvest(gasWorker, geyser, HarvestOrder::Gas);
    gasWorld->advance();
    auto *gasActor = gasWorld->get(gasWorker);
    check(gasActor->order == 81 && gasActor->orderState == 3 &&
          gasActor->gathering && gasActor->resourceTarget == geyser);
    auto queuedFog = std::make_unique<World>(*gasWorld);
    queuedFog->rememberedRightClick(gasWorker, {800, 800}, 176, true);
    auto plainApproach = std::make_unique<World>(*gasWorld);
    for (int frame = 0; frame < 12; ++frame) {
      queuedFog->advance();
      plainApproach->advance();
      check(queuedFog->get(gasWorker)->order == 81 &&
            samePoint(queuedFog->get(gasWorker)->motion->rawPosition(),
                      plainApproach->get(gasWorker)->motion->rawPosition()));
    }
    auto copiedFog = std::make_unique<World>(*queuedFog);
    queuedFog->move(gasWorker, {1000, 1200});
    check(queuedFog->get(gasWorker)->queuedCommands.empty() &&
          copiedFog->get(gasWorker)->queuedCommands.size() == 2 &&
          copiedFog->get(gasWorker)->queuedCommands.front().order == 151 &&
          copiedFog->get(gasWorker)->queuedCommands.back().rememberedType == 176);
    auto interruptedGas = std::make_unique<World>(*gasWorld);
    interruptedGas->move(gasWorker, {1000, 1200});
    interruptedGas->advance(250);
    check(samePoint(interruptedGas->get(gasWorker)->position, {1000, 1200}) &&
          gasActor->order == 81 && gasWorld->frame() == 1);
    gasWorld->advance(250);
    check(gasActor->order == 3 && !gasActor->gathering &&
          gasActor->cargo == 0 && !gasWorld->get(gasWorld->get(geyser)->gatherer) &&
          gasWorld->economy().resources(0).gas == 0 &&
          gasActor->position.x < 1408 - geyserType.bounds.left &&
          gasWorld->get(geyser)->resources == 0);
    // A mineral centered on a tile boundary occupies its quantized center
    // row, rather than the preceding row. Creep must stop at that footprint.
    auto creepDefinitions = std::make_shared<Definitions>(*definitions);
    auto &provider = creepDefinitions->units[143];
    provider.flags = 1;
    provider.placementWidth = provider.placementHeight = 32;
    provider.bounds = {16, 16, 15, 15};
    auto offGridCreep = std::make_unique<World>(creepDefinitions, map);
    offGridCreep->createFinished(176, {416, 320}, 11);
    offGridCreep->createFinished(176, {416, 384}, 11);
    offGridCreep->createFinished(143, {256, 304}, 0, false, true);
    check(!offGridCreep->creep().at(12, 10) &&
          offGridCreep->creep().at(12, 11) &&
          !offGridCreep->creep().at(12, 12));
    auto failedHarvest = std::make_unique<World>(definitions, map);
    auto stranded = failedHarvest->createFinished(7, {1000, 1000}, 0);
    auto unavailable = failedHarvest->createFinished(176, {1000, 500}, 11);
    auto replacement = failedHarvest->createFinished(176, {1200, 1000}, 11);
    auto reservedBy = failedHarvest->createFinished(7, {1500, 1500}, 0);
    failedHarvest->setResources(unavailable, 50000);
    failedHarvest->setResources(replacement, 50000);
    failedHarvest->get(replacement)->gatherer = reservedBy;
    auto *strandedActor = failedHarvest->get(stranded);
    strandedActor->order = 85;
    strandedActor->orderState = 3;
    strandedActor->immovable = true;
    strandedActor->mineral = strandedActor->target = unavailable;
    strandedActor->targetPosition = failedHarvest->get(unavailable)->position;
    auto abandonedHarvest = std::make_unique<World>(*failedHarvest);
    abandonedHarvest->remove(replacement);
    abandonedHarvest->advance();
    check(abandonedHarvest->get(stranded)->order == worker.humanIdle &&
          !abandonedHarvest->get(stranded)->motion->moving() &&
          abandonedHarvest->deliveredMinerals(0) == 0);
    failedHarvest->advance();
    check(strandedActor->order == 85 && strandedActor->orderState == 3 &&
          strandedActor->mineral == replacement &&
          strandedActor->planRequested &&
          samePoint(strandedActor->targetPosition, {1200, 1000}));
    auto retargeting = std::make_unique<World>(definitions, map);
    auto retargetWorker = retargeting->createFinished(7, {1000, 1000}, 0);
    auto *retargetActor = retargeting->get(retargetWorker);
    retargetActor->collisionPending = false;
    Motion incomingLeg({980, 1000}, 64, dat::Flingy{0, 1280, 12227, 67, 40, 0});
    incomingLeg.route({{1000, 1000}}, 0, false, false);
    for (int n = 0; n < 100 && !incomingLeg.arrived(); ++n)
      incomingLeg.advance();
    retargetActor->motion = incomingLeg;
    LocalRouteResult oldLeg;
    oldLeg.source = retargetActor->position;
    oldLeg.destination = {1400, 1000};
    oldLeg.points = {retargetActor->position};
    retargetActor->path = oldLeg;
    retargetActor->order = 6;
    retargetActor->orderState = 1;
    retargetActor->targetPosition = retargetActor->moveTargetPosition = {1000, 1400};
    retargetActor->planRequested = true;
    retargetActor->routePlanningFrames = 0;
    check(retargetActor->motion->arrived() && !retargetActor->path->reached);
    auto continuing = std::make_unique<World>(*retargeting);
    continuing->get(retargetWorker)->planRequested = false;
    retargeting->advance();
    continuing->advance();
    check(retargetActor->path &&
          samePoint(retargetActor->path->destination, {1000, 1400}));
    check(continuing->get(retargetWorker)->path &&
          samePoint(continuing->get(retargetWorker)->path->destination,
                    {1400, 1000}));
    {
      Motion pausedApproach({1000, 1000}, 64, definitions->flingies[0]);
      pausedApproach.route({{1400, 1000}}, 0);
      for (int n = 0; n < 6; ++n)
        pausedApproach.advance();
      auto pauseOrigin = pausedApproach.rawPosition();
      auto pauseFacing = pausedApproach.facing();
      auto pauseSpeed = pausedApproach.nextSpeed();
      pausedApproach.advance();
      pausedApproach.setRouteAge(1);
      pausedApproach.blockedWaypoint(pauseOrigin, pauseFacing, pauseSpeed, true);
      check(pausedApproach.movingCollisionPaused());
      auto blockedRetry = std::make_unique<World>(definitions, map);
      auto retryWorker = blockedRetry->createFinished(7, pausedApproach.position(), 0);
      auto retryBlocker = blockedRetry->createFinished(
          7, {pausedApproach.position().x + 23, pausedApproach.position().y}, 0);
      auto *retryActor = blockedRetry->get(retryWorker);
      retryActor->collisionPending = false;
      retryActor->motion = pausedApproach;
      retryActor->collisionObstacle = retryBlocker;
      retryActor->order = 6;
      retryActor->orderState = 1;
      retryActor->targetPosition = retryActor->moveTargetPosition = {1400, 1000};
      auto clearedRetry = std::make_unique<World>(*blockedRetry);
      clearedRetry->remove(retryBlocker);
      blockedRetry->advance();
      clearedRetry->advance();
      check(samePoint(clearedRetry->get(retryWorker)->motion->rawPosition(),
                      pauseOrigin));
      blockedRetry->advance();
      clearedRetry->advance();
      check(samePoint(retryActor->motion->rawPosition(), pauseOrigin) &&
            retryActor->motion->movingCollisionPaused());
      auto *clearedActor = clearedRetry->get(retryWorker);
      check(!samePoint(clearedActor->motion->rawPosition(), pauseOrigin) &&
            !clearedActor->motion->collisionPaused() &&
            clearedActor->motion->collisionDebt() == retryActor->motion->collisionDebt());
    }
    {
      Motion harvestStop({1000, 1000}, 64, definitions->flingies[0]);
      harvestStop.route({{1400, 1000}}, 0);
      for (int n = 0; n < 6; ++n)
        harvestStop.advance();
      harvestStop.brakeTo(harvestStop.position());
      for (int n = 0; n < 100 && !harvestStop.settling(); ++n)
        harvestStop.advance();
      check(harvestStop.settling() && harvestStop.speed() >= 192);
      auto stopCommand = std::make_unique<World>(definitions, map);
      auto stopWorker = stopCommand->createFinished(7, harvestStop.position(), 0);
      auto stopPatch = stopCommand->createFinished(
          176, {harvestStop.position().x + 200, harvestStop.position().y}, 11);
      stopCommand->get(stopPatch)->resources = 500;
      auto *stopActor = stopCommand->get(stopWorker);
      stopActor->motion = harvestStop;
      stopActor->collisionPending = false;
      auto integerStopCommand = std::make_unique<World>(*stopCommand);
      auto *integerActor = integerStopCommand->get(stopWorker);
      integerActor->motion->clipStep(
          {integerActor->position.x * 256, integerActor->position.y * 256});
      integerStopCommand->harvest(stopWorker, stopPatch, HarvestOrder::Minerals);
      stopCommand->harvest(stopWorker, stopPatch, HarvestOrder::Minerals);
      for (int n = 0; n < 3 && stopActor->orderState == 0; ++n)
        stopCommand->advance();
      check(stopActor->planRequested && stopActor->routePlanningFrames == 1 &&
            stopActor->motion->settling() && stopActor->motion->speed() > 0);
      auto stopRaw = stopActor->motion->rawPosition();
      auto stopSpeed = stopActor->motion->speed();
      stopActor->collisionPending = true;
      stopActor->collisionWait = 0;
      stopCommand->advance();
      check(samePoint(stopActor->motion->rawPosition(), stopRaw) &&
            stopActor->motion->speed() == stopSpeed);
      for (int n = 0; n < 3 && integerActor->orderState == 0; ++n)
        integerStopCommand->advance();
      check(integerActor->planRequested && integerActor->routePlanningFrames == 0);
      auto integerRaw = integerActor->motion->rawPosition();
      integerStopCommand->advance();
      check(!samePoint(integerActor->motion->rawPosition(), integerRaw));
    }
    {
      auto renewedAttack = std::make_unique<World>(definitions, map);
      auto heldAttacker = renewedAttack->createFinished(7, {1000, 1000}, 0);
      auto newEnemy = renewedAttack->createFinished(7, {1200, 1000}, 1);
      auto *heldActor = renewedAttack->get(heldAttacker);
      heldActor->collisionPending = false;
      heldActor->motion.emplace(Point{1000, 1000}, 0, definitions->flingies[0]);
      heldActor->motion->brakeTo({1000, 1000});
      heldActor->motion->faceOnArrival(Point{1000, 900});
      heldActor->sprite.setHeading(0);
      renewedAttack->attack(heldAttacker, newEnemy);
      renewedAttack->advance();
      check(heldActor->sprite.context.heading == 0 &&
            heldActor->motion->facing() == 0 && heldActor->planRequested);
    }
    {
      Motion combatArrival({1000, 1000}, 64, definitions->flingies[0]);
      combatArrival.route({{1100, 1000}}, 0);
      for (int n = 0; n < 8; ++n)
        combatArrival.advance();
      combatArrival.brakeTo({combatArrival.position().x + 8, 1000});
      for (int n = 0; n < 100 && !combatArrival.arrived(); ++n)
        combatArrival.advance();
      check(combatArrival.arrived() && combatArrival.braking());
      auto combatOverlap = std::make_unique<World>(definitions, map);
      auto stoppedAttacker = combatOverlap->createFinished(
          7, combatArrival.position(), 0);
      auto passingWorker = combatOverlap->createFinished(
          7, {combatArrival.position().x + 16, 1000}, 1, false);
      auto *attacker = combatOverlap->get(stoppedAttacker);
      auto *passing = combatOverlap->get(passingWorker);
      attacker->motion = combatArrival;
      attacker->order = 10;
      attacker->orderState = 1;
      attacker->orderTimer = 100;
      attacker->target = passingWorker;
      attacker->targetPosition = passing->position;
      attacker->moveTargetPosition = attacker->position;
      attacker->collisionPending = false;
      attacker->sprite.setHeading(combatArrival.facing());
      passing->gathering = true;
      passing->moveTargetPosition = {passing->position.x + 100, 1000};
      passing->motion->route({passing->moveTargetPosition}, 0);
      passing->motion->advance();
      passing->order = 85;
      passing->orderState = 1;
      passing->orderTimer = 100;
      check(attacker->motion->arrived());
      check(!attacker->gathering);
      check(!attacker->collisionPending && !attacker->escapingOverlap);
      check(!attacker->motion->scriptDriven());
      check(combatOverlap->workerPositionBlocked(stoppedAttacker, attacker->position));
      auto notifiedOverlap = std::make_unique<World>(*combatOverlap);
      notifiedOverlap->get(stoppedAttacker)->collisionPending = true;
      notifiedOverlap->get(stoppedAttacker)->collisionWait = 0;
      combatOverlap->advance();
      notifiedOverlap->advance();
      check(!attacker->collisionPending && !attacker->escapingOverlap &&
            attacker->order == 10);
      const auto *notified = notifiedOverlap->get(stoppedAttacker);
      check(notified->collisionPending || notified->escapingOverlap);
    }
    auto borderMove = std::make_unique<World>(definitions, map);
    auto borderWorker = borderMove->createFinished(7, {1280, 1995}, 0);
    borderMove->createFinished(176, {1492, 1968}, 11);
    borderMove->move(borderWorker, {1520, 1995});
    for (int n = 0;
         n < 300 && borderMove->get(borderWorker)->order != worker.humanIdle;
         ++n)
      borderMove->advance();
    auto *borderActor = borderMove->get(borderWorker);
    check(samePoint(borderActor->position, {1448, 1995}) &&
          borderActor->order == worker.humanIdle && !borderActor->path &&
          samePoint(borderActor->targetPosition, {}));
    World resources(definitions, map);
    auto approaching = resources.createFinished(7, {1000, 1000}, 0);
    auto busyPatch = resources.createFinished(176, {1000, 1028}, 11);
    auto busyWorker = resources.createFinished(7, {1500, 1500}, 0);
    auto nearestFree = resources.createFinished(176, {1100, 1000}, 11);
    auto fartherFree = resources.createFinished(176, {1000, 1200}, 11);
    for (auto patch : {busyPatch, nearestFree, fartherFree})
      resources.setResources(patch, 50000);
    auto &depotType = definitions->units[106];
    depotType.flags = 0x1001;
    depotType.bounds = {32, 16, 31, 15};
    depotType.humanIdle = depotType.returnIdle = 23;
    World cargoWorld(definitions, map);
    auto cargoDepot = cargoWorld.createFinished(106, {300, 300}, 0);
    auto carrier = cargoWorld.createFinished(7, {600, 600}, 0);
    auto cargoField = cargoWorld.createFinished(176, {900, 600}, 11);
    check(cargoWorld.mineralDepot(carrier) == cargoDepot);
    auto depotQuery = cargoWorld;
    auto nearDepot = depotQuery.createFinished(106, {400, 600}, 0);
    depotQuery.createFinished(106, {600, 700}, 1);
    auto incompleteDepot = depotQuery.createFinished(106, {600, 500}, 0);
    depotQuery.get(incompleteDepot)->completed = false;
    check(depotQuery.mineralDepot(carrier) == nearDepot);
    auto returnQuery = depotQuery;
    returnQuery.get(carrier)->cargo = 2;
    returnQuery.get(carrier)->cargoAmount = 8;
    returnQuery.get(carrier)->mineral = cargoField;
    check(returnQuery.returnCargo(carrier));
    returnQuery.advance();
    check(returnQuery.get(carrier)->target == nearDepot);
    check(samePoint(returnQuery.get(carrier)->targetPosition, {}));
    auto explicitReturn = depotQuery;
    explicitReturn.get(carrier)->cargo = 2;
    explicitReturn.get(carrier)->cargoAmount = 8;
    explicitReturn.get(carrier)->mineral = cargoField;
    check(explicitReturn.returnCargo(carrier, false, nearDepot));
    explicitReturn.advance();
    check(explicitReturn.get(carrier)->target == nearDepot &&
          samePoint(explicitReturn.get(carrier)->targetPosition,
                    explicitReturn.get(nearDepot)->position));
    auto obstructedReturn = depotQuery;
    auto *obstructedCarrier = obstructedReturn.get(carrier);
    obstructedReturn.createFinished(106, obstructedCarrier->position, 0);
    obstructedCarrier->cargo = 2;
    obstructedCarrier->cargoAmount = 8;
    obstructedCarrier->mineral = cargoField;
    obstructedCarrier->collisionPending = true;
    obstructedCarrier->collisionWait = 2;
    check(obstructedReturn.returnCargo(carrier, false, nearDepot));
    obstructedReturn.advance(2);
    check(obstructedCarrier->moveTargetUnit == nearDepot &&
          samePoint(obstructedCarrier->moveTargetPosition,
                    obstructedReturn.get(nearDepot)->position));
    auto gatheringReturn = depotQuery;
    gatheringReturn.get(carrier)->cargo = 2;
    gatheringReturn.get(carrier)->cargoAmount = 8;
    gatheringReturn.get(carrier)->mineral = cargoField;
    gatheringReturn.get(carrier)->gathering = true;
    check(gatheringReturn.returnCargo(carrier, false, nearDepot));
    check(gatheringReturn.get(carrier)->order == 151);
    gatheringReturn.advance();
    check(gatheringReturn.get(carrier)->order == 90 &&
          gatheringReturn.get(carrier)->target == nearDepot &&
          samePoint(gatheringReturn.get(carrier)->targetPosition,
                    gatheringReturn.get(nearDepot)->position));
    auto queuedReturn = depotQuery;
    auto *waitingCarrier = queuedReturn.get(carrier);
    waitingCarrier->cargo = 2;
    waitingCarrier->cargoAmount = 8;
    waitingCarrier->mineral = cargoField;
    queuedReturn.move(carrier, {700, 600});
    check(queuedReturn.returnCargo(carrier, true) &&
          waitingCarrier->order == 6 &&
          waitingCarrier->queuedCommands.size() == 1 &&
          waitingCarrier->queuedCommands.front().order == 90);
    auto queuedReturnCopy = queuedReturn;
    queuedReturn.move(carrier, {800, 600});
    check(waitingCarrier->queuedCommands.empty() &&
          queuedReturnCopy.get(carrier)->queuedCommands.front().order == 90);
    waitingCarrier->cargo = 0;
    check(!queuedReturn.returnCargo(carrier, true) &&
          waitingCarrier->order == 6 && waitingCarrier->queuedCommands.empty());
    auto gasCargoData = std::make_shared<Definitions>(*definitions);
    gasCargoData->units[188].flags = 0x2001;
    gasCargoData->units[188].bounds = {32, 16, 31, 15};
    gasCargoData->units[188].humanIdle = gasCargoData->units[188].returnIdle = 23;
    auto gasCargoWorld = std::make_unique<World>(gasCargoData, map);
    gasCargoWorld->createFinished(106, {300, 300}, 0);
    auto gasCarrier = gasCargoWorld->createFinished(7, {600, 600}, 0);
    auto gasSource = gasCargoWorld->createFinished(188, {900, 600}, 11);
    gasCargoWorld->get(gasCarrier)->cargo = 1;
    gasCargoWorld->get(gasCarrier)->cargoAmount = 8;
    gasCargoWorld->get(gasCarrier)->mineral = gasSource;
    check(gasCargoWorld->returnCargo(gasCarrier));
    gasCargoWorld->advance();
    check(gasCargoWorld->get(gasCarrier)->order == 84 &&
          gasCargoWorld->get(gasCarrier)->queuedCommands.size() == 1 &&
          gasCargoWorld->get(gasCarrier)->queuedCommands.front().order == 151);
    for (int n = 0; n != 400 && gasCargoWorld->economy().resources(0).gas == 0; ++n)
      gasCargoWorld->advance();
    check(gasCargoWorld->economy().resources(0).gas == 8 &&
          gasCargoWorld->economy().resources(0).minerals == 0 &&
          gasCargoWorld->deliveredMinerals(0) == 0 &&
          gasCargoWorld->get(gasCarrier)->cargo == 0 &&
          gasCargoWorld->get(gasCarrier)->order == 81);
    depotQuery.remove(nearDepot);
    check(depotQuery.mineralDepot(carrier) == cargoDepot &&
          cargoWorld.mineralDepot(carrier) == cargoDepot);
    cargoWorld.setResources(cargoField, 50000);
    cargoWorld.move(carrier, {300, 300});
    cargoWorld.advance(20);
    auto *carrying = cargoWorld.get(carrier);
    carrying->cargo = 2;
    carrying->cargoAmount = 8;
    carrying->mineral = cargoField;
    carrying->mainTimer = 17;
    auto cargoRaw = carrying->motion->rawPosition();
    int cargoSpeed = carrying->motion->speed();
    check(cargoWorld.returnCargo(carrier) && carrying->order == 90 &&
          carrying->mainTimer == 17 &&
          samePoint(carrying->motion->rawPosition(), cargoRaw) &&
          carrying->motion->speed() == cargoSpeed);
    auto cargoCopy = cargoWorld;
    cargoWorld.advance();
    cargoCopy.advance();
    check(carrying->order == 90 && carrying->orderState == 1 &&
          carrying->target == cargoDepot && carrying->mainTimer == 16 &&
          samePoint(carrying->motion->rawPosition(),
                    cargoCopy.get(carrier)->motion->rawPosition()) &&
          cargoWorld.randomState() == cargoCopy.randomState());
    MiningControl cargoControl;
    cargoControl.worker = carrier;
    cargoControl.field = cargoField;
    cargoControl.enabled = true;
    cargoControl.cargoDelay = 0;
    auto controlled = cargoWorld;
    auto *controlledWorker = controlled.get(carrier);
    controlledWorker->resourceTarget = cargoField;
    // Enabling midway through a loaded trip must wait for a known pickup.
    check(!cargoControl.apply(controlled) && cargoControl.loaded &&
          !cargoControl.cargoSawEmpty && cargoControl.commands == 0);
    controlledWorker->cargo = 0;
    check(!cargoControl.apply(controlled) && cargoControl.cargoSawEmpty);
    controlledWorker->cargo = 2;
    check(cargoControl.apply(controlled) && cargoControl.commands == 1 &&
          controlledWorker->order == 151 &&
          cargoControl.cargoLoadedFrame == controlled.frame());
    check(!cargoControl.apply(controlled) && cargoControl.commands == 1);
    auto controlledCopy = controlled;
    auto controlCopy = cargoControl;
    controlledWorker->cargo = 0;
    cargoControl.apply(controlled);
    check(!cargoControl.loaded && controlCopy.loaded &&
          controlledCopy.get(carrier)->cargo == 2);
    auto &larvaType = definitions->units[35];
    auto retryGeometryData = std::make_shared<Definitions>(*definitions);
    auto &retryBuilding = retryGeometryData->units[106];
    retryBuilding.flags = 1;
    retryBuilding.bounds = {30, 30, 30, 30};
    retryBuilding.placementWidth = 128;
    retryBuilding.placementHeight = 96;
    retryBuilding.hitpoints = 100 * 256;
    retryBuilding.humanIdle = retryBuilding.returnIdle = 23;
    auto retryGeometry = std::make_unique<World>(retryGeometryData, map);
    retryGeometry->createFinished(106, {800, 800}, 0, false);
    auto geometricRetryHandle =
        retryGeometry->createFinished(7, {775, 800}, 0, false);
    auto *retryState = retryGeometry->get(geometricRetryHandle);
    retryState->motion.emplace(retryState->position, 64,
                               retryGeometryData->flingies[0]);
    retryState->motion->route({{775, 790}}, 0);
    retryState->moveTargetPosition = retryState->targetPosition = {1000, 800};
    retryState->overlapReturnTarget = {1000, 800};
    retryState->order = 6;
    retryState->orderState = 1;
    retryState->collisionPending = retryState->escapingOverlap = true;
    retryState->overlapOrderChanged = retryState->overlapFreshOrder = true;
    retryState->collisionWait = 0;
    retryState->overlapStopRecovery = retryState->collisionRouteReset = false;
    retryState->planRequested = false;
    auto retryRandom = retryGeometry->randomState();
    retryGeometry->advance();
    check(retryGeometry->randomState() == retryRandom &&
          retryState->motion->waypoint().x + 11 < 770 &&
          retryState->motion->waypoint().y == 800);
    auto ordinaryArrival = std::make_unique<World>(definitions, map);
    auto arrivalActor =
        ordinaryArrival->createFinished(7, {800, 800}, 0, false);
    auto arrivalBlocker =
        ordinaryArrival->createFinished(7, {804, 800}, 0, false);
    auto *ordinaryActor = ordinaryArrival->get(arrivalActor);
    ordinaryActor->gathering = false;
    ordinaryActor->collisionPending = false;
    ordinaryActor->collisionWait = 0;
    ordinaryActor->moveTargetPosition =
        ordinaryActor->targetPosition = {800, 800};
    ordinaryActor->order = 6;
    ordinaryActor->orderState = 1;
    ordinaryActor->motion.emplace(Point{799, 800}, 64,
                                  definitions->flingies[0]);
    ordinaryActor->motion->route({{800, 800}}, 0);
    for (int step = 0; step < 10 && !ordinaryActor->motion->arrived(); ++step)
      ordinaryActor->motion->advance();
    check(ordinaryActor->motion->arrived());
    ordinaryArrival->get(arrivalBlocker)->collisionPending = false;
    ordinaryArrival->get(arrivalBlocker)->moveTargetPosition = {804, 800};
    auto arrivalBefore = *ordinaryArrival;
    auto clearArrival = arrivalBefore;
    clearArrival.relocateWorkerForSearch(arrivalBlocker, {1000, 800});
    clearArrival.advance();
    ordinaryArrival->advance();
    check((ordinaryActor->collisionPending || ordinaryActor->escapingOverlap) &&
          !clearArrival.get(arrivalActor)->collisionPending &&
          !clearArrival.get(arrivalActor)->escapingOverlap &&
          samePoint(ordinaryActor->position, {800, 800}) &&
          !arrivalBefore.get(arrivalActor)->collisionPending &&
          !arrivalBefore.get(arrivalActor)->escapingOverlap);
    larvaType.flags = 0x18000000U;
    larvaType.bounds = {2, 2, 2, 2};
    larvaType.humanIdle = larvaType.returnIdle = 23;
    auto restingWorld = cargoWorld;
    auto restingHandle = restingWorld.createFinished(35, {800, 800}, 0, false);
    auto *restingState = restingWorld.get(restingHandle);
    restingState->motion.emplace(restingState->position, 0,
                                 dat::Flingy{0, 512, 0, 1, 20, 2});
    restingState->sprite.setHeading(0);
    restingState->motion->faceOnArrival(Point{900, 900});
    for (int frame = 0; frame < 5; ++frame)
      restingWorld.advance();
    check(restingState->motion->facing() == heading({100, 100}) &&
          restingState->sprite.context.heading ==
              restingState->motion->facing() &&
          samePoint(restingState->position, {800, 800}));
    auto overlapping = cargoWorld;
    overlapping.get(carrier)->gathering = true;
    auto larva = overlapping.createFinished(
        35, overlapping.get(carrier)->position, 0, false);
    auto *larvaState = overlapping.get(larva);
    larvaState->motion->route(
        {{larvaState->position.x + 100, larvaState->position.y}}, 0);
    check(overlapping.returnCargo(carrier));
    overlapping.advance();
    check(larvaState->collisionPending);
    auto overlapCopy = overlapping;
    auto larvaRaw = larvaState->motion->rawPosition();
    int larvaHeading = larvaState->motion->facing();
    overlapping.advance();
    check(!larvaState->collisionPending &&
          overlapCopy.get(larva)->collisionPending &&
          samePoint(larvaState->motion->rawPosition(), larvaRaw) &&
          larvaState->motion->facing() == larvaHeading);
    overlapping.advance();
    check(!samePoint(larvaState->motion->rawPosition(), larvaRaw) ||
          larvaState->motion->facing() != larvaHeading);
    auto arrivalOverlap = overlapCopy;
    auto *arrivalLarva = arrivalOverlap.get(larva);
    arrivalLarva->motion.emplace(arrivalLarva->position, 0,
                                 dat::Flingy{0, 512, 0, 1, 20, 2});
    arrivalLarva->motion->scriptSpeed(512);
    arrivalLarva->motion->route({arrivalLarva->position}, 0);
    arrivalLarva->moveTargetPosition = arrivalLarva->position;
    check(arrivalLarva->motion->arrived() && arrivalLarva->collisionPending);
    arrivalLarva->motion->faceOnArrival(
        Point{arrivalLarva->position.x + 30, arrivalLarva->position.y + 30});
    auto collisionArrivalHeading = arrivalLarva->motion->facing();
    arrivalOverlap.advance();
    check(!arrivalLarva->collisionPending && !arrivalLarva->motion->moving() &&
          arrivalLarva->motion->speed() == 0 &&
          samePoint(arrivalLarva->motion->velocity(), {}) &&
          arrivalLarva->motion->facing() == collisionArrivalHeading);
    auto idleOverlap = cargoWorld;
    idleOverlap.get(carrier)->gathering = true;
    auto idleLarva = idleOverlap.createFinished(
        35, idleOverlap.get(carrier)->position, 0, false);
    auto *idleLarvaState = idleOverlap.get(idleLarva);
    check(idleOverlap.returnCargo(carrier));
    idleOverlap.advance();
    check(idleLarvaState->collisionPending &&
          idleLarvaState->collisionWait == 2);
    auto idlePosition = idleLarvaState->position;
    auto idleRaw = idleLarvaState->motion->rawPosition();
    int idleFacing = idleLarvaState->motion->facing();
    Point resumedGoal{idlePosition.x + 100, idlePosition.y};
    idleLarvaState->moveTargetPosition = resumedGoal;
    idleLarvaState->planRequested = true;
    idleLarvaState->routePlanningFrames = 0;
    idleOverlap.advance();
    check(idleLarvaState->collisionPending &&
          idleLarvaState->collisionWait == 1 && !idleLarvaState->path &&
          samePoint(idleLarvaState->motion->rawPosition(), idleRaw));
    idleOverlap.advance();
    check(!idleLarvaState->collisionPending && idleLarvaState->path &&
          samePoint(idleLarvaState->path->source, idlePosition) &&
          samePoint(idleLarvaState->path->destination, resumedGoal) &&
          samePoint(idleLarvaState->motion->rawPosition(), idleRaw) &&
          idleLarvaState->motion->facing() == idleFacing);
    idleOverlap.advance();
    check(!samePoint(idleLarvaState->motion->rawPosition(), idleRaw) ||
          idleLarvaState->motion->facing() != idleFacing);
    auto *approach = resources.get(approaching);
    approach->mineral = approach->target = busyPatch;
    approach->targetPosition = resources.get(busyPatch)->position;
    approach->order = 85;
    approach->orderState = 1;
    resources.get(busyPatch)->gatherer = busyWorker;
    auto alternative = resources;
    alternative.get(nearestFree)->gatherer = busyWorker;
    resources.advance();
    alternative.advance();
    check(resources.get(approaching)->mineral == nearestFree &&
          alternative.get(approaching)->mineral == fartherFree &&
          resources.get(approaching)->orderState == 1 &&
          resources.get(approaching)->planRequested);
    auto arrivalCommand = resources;
    auto *arrivingActor = arrivalCommand.get(approaching);
    Motion nearArrival({996, 1000}, 64, definitions->flingies[0]);
    nearArrival.route({{1000, 1000}}, 0);
    for (int i = 0; i < 6; ++i)
      nearArrival.advance();
    arrivingActor->motion = nearArrival;
    arrivingActor->position = nearArrival.position();
    arrivingActor->order = 85;
    arrivingActor->orderState = arrivingActor->orderTimer = 0;
    arrivingActor->mineral = nearestFree;
    arrivingActor->moveTargetUnit = {};
    arrivingActor->planRequested = false;
    arrivalCommand.advance();
    auto arrivalRaw = arrivingActor->motion->rawPosition();
    int arrivalSpeed = arrivingActor->motion->speed();
    check(samePoint(arrivalRaw, {256000, 256000}) && arrivalSpeed > 0);
    arrivalCommand.advance();
    check(samePoint(arrivingActor->motion->rawPosition(), arrivalRaw) &&
          arrivingActor->motion->speed() == arrivalSpeed);
    auto fractionalCommand = resources;
    auto *fractionalActor = fractionalCommand.get(approaching);
    Motion fractionalArrival({996, 1000}, 64, definitions->flingies[0]);
    fractionalArrival.route({{1100, 1000}}, 0);
    fractionalArrival.advance();
    fractionalArrival.advance();
    fractionalArrival.advance();
    fractionalArrival.route({fractionalArrival.position()}, 0);
    check(fractionalArrival.arrived() && fractionalArrival.speed() > 0);
    fractionalActor->motion = fractionalArrival;
    fractionalActor->position = fractionalArrival.position();
    fractionalActor->order = 85;
    fractionalActor->orderState = fractionalActor->orderTimer = 0;
    fractionalActor->gathering = false;
    fractionalActor->mineral = nearestFree;
    fractionalActor->moveTargetUnit = {};
    fractionalActor->planRequested = false;
    fractionalCommand.advance();
    const auto fractionalRaw = fractionalActor->motion->rawPosition();
    check(fractionalActor->planRequested && fractionalActor->routePlanningFrames == 0);
    auto fractionalCopy = fractionalCommand;
    fractionalCommand.advance();
    fractionalCopy.advance();
    check(!samePoint(fractionalActor->motion->rawPosition(), fractionalRaw) &&
          samePoint(fractionalCopy.get(approaching)->motion->rawPosition(),
                    fractionalActor->motion->rawPosition()));
    auto cancelledWork = resources;
    auto *workingActor = cancelledWork.get(approaching);
    workingActor->planRequested = false;
    workingActor->order = 87;
    workingActor->orderState = 5;
    workingActor->mainTimer = 40;
    workingActor->mineral = busyPatch;
    workingActor->gathering = true;
    auto *waitingActor = cancelledWork.get(busyWorker);
    waitingActor->order = 86;
    waitingActor->orderState = 2;
    waitingActor->mineral = busyPatch;
    auto *reserved = cancelledWork.get(busyPatch);
    reserved->gatherer = approaching;
    reserved->mineralQueue = {busyWorker};
    auto interruptedFollow = std::make_unique<World>(cancelledWork);
    interruptedFollow->follow(approaching, busyWorker);
    interruptedFollow->advance(2);
    const auto *followingActor = interruptedFollow->get(approaching);
    check(followingActor->order == 49 && followingActor->orderState == 1 &&
          followingActor->mainTimer == 38 && !followingActor->gathering &&
          followingActor->moveTargetUnit == busyWorker &&
          !followingActor->motion->braking());
    auto interruptedWork = std::make_unique<World>(cancelledWork);
    auto *interruptedActor = interruptedWork->get(approaching);
    bool invalidInterruption = false;
    try {
      interruptedWork->move(approaching, {-1, 1000});
    } catch (const std::out_of_range &) {
      invalidInterruption = true;
    }
    check(invalidInterruption && interruptedActor->order == 87 &&
          interruptedActor->gathering &&
          !interruptedActor->harvestContinuation);
    interruptedWork->move(approaching, {1200, 1000});
    check(interruptedActor->order == 88 && interruptedActor->gathering &&
          interruptedActor->mainTimer == 40 &&
          interruptedActor->harvestContinuation &&
          interruptedWork->get(busyPatch)->gatherer == approaching);
    auto interruptionSnapshot = std::make_unique<World>(*interruptedWork);
    interruptedWork->advance();
    check(interruptedActor->order == 151 && interruptedActor->gathering &&
          interruptedWork->get(busyPatch)->gatherer == busyWorker &&
          interruptedWork->get(busyPatch)->mineralQueue.empty() &&
          interruptionSnapshot->get(busyPatch)->gatherer == approaching &&
          interruptionSnapshot->get(approaching)->order == 88);
    interruptedWork->advance();
    check(interruptedActor->order == 6 && interruptedActor->orderState == 1 &&
          !interruptedActor->gathering &&
          !interruptedActor->harvestContinuation &&
          interruptedActor->collisionRouteReset &&
          interruptedActor->collisionWait == 2 &&
          samePoint(interruptedActor->targetPosition, {1200, 1000}));
    interruptionSnapshot->advance(2);
    check(interruptionSnapshot->randomState() ==
              interruptedWork->randomState() &&
          samePoint(interruptionSnapshot->get(approaching)->position,
                    interruptedActor->position) &&
          interruptionSnapshot->get(approaching)->order == 6);
    cancelledWork.harvest(approaching, fartherFree);
    check(workingActor->order == 88 && workingActor->mainTimer == 40);
    auto cancelledCopy = cancelledWork;
    cancelledWork.advance();
    cancelledCopy.advance();
    check(reserved->gatherer == busyWorker && reserved->mineralQueue.empty() &&
          workingActor->order == 151 && workingActor->mainTimer == 39);
    auto replacedCleanup = cancelledWork;
    replacedCleanup.harvest(approaching, nearestFree);
    check(replacedCleanup.get(approaching)->order == 151 &&
          replacedCleanup.get(approaching)->pendingHarvest == nearestFree &&
          workingActor->pendingHarvest == fartherFree);
    replacedCleanup.advance();
    check(replacedCleanup.get(approaching)->order == 85 &&
          replacedCleanup.get(approaching)->target == nearestFree &&
          replacedCleanup.get(approaching)->pendingHarvest.index < 0);
    cancelledWork.advance();
    cancelledCopy.advance();
    check(workingActor->order == 85 && workingActor->orderState == 0 &&
          workingActor->mainTimer == 38 &&
          workingActor->target == fartherFree &&
          cancelledCopy.get(approaching)->target == fartherFree &&
          cancelledWork.randomState() == cancelledCopy.randomState());
    auto cancelledWait = resources;
    auto *queuedActor = cancelledWait.get(approaching);
    queuedActor->planRequested = false;
    queuedActor->order = 86;
    queuedActor->orderState = 2;
    queuedActor->mineral = busyPatch;
    queuedActor->mainTimer = 17;
    cancelledWait.get(busyPatch)->mineralQueue = {approaching};
    auto interruptedWait = std::make_unique<World>(cancelledWait);
    interruptedWait->move(approaching, {1200, 1000});
    check(interruptedWait->get(approaching)->order == 89);
    interruptedWait->advance();
    check(interruptedWait->get(busyPatch)->mineralQueue.empty() &&
          interruptedWait->get(busyPatch)->gatherer == busyWorker &&
          interruptedWait->get(approaching)->order == 151);
    interruptedWait->advance();
    check(interruptedWait->get(approaching)->order == 6 &&
          !interruptedWait->get(approaching)->gathering &&
          interruptedWait->get(approaching)->cargo == 0);
    cancelledWait.harvest(approaching, fartherFree);
    check(queuedActor->order == 89 && queuedActor->mainTimer == 17);
    cancelledWait.advance();
    check(cancelledWait.get(busyPatch)->gatherer == busyWorker &&
          cancelledWait.get(busyPatch)->mineralQueue.empty() &&
          queuedActor->order == 151 && queuedActor->mainTimer == 16);
    World boundarySelection(definitions, map);
    auto boundaryWorker = boundarySelection.createFinished(7, {1000, 1000}, 0);
    auto boundaryHolder = boundarySelection.createFinished(7, {600, 600}, 0);
    auto boundaryBusy = boundarySelection.createFinished(176, {1000, 1100}, 11);
    auto positiveLimit =
        boundarySelection.createFinished(176, {1256, 1000}, 11);
    auto negativeLimit = boundarySelection.createFinished(176, {744, 1000}, 11);
    for (auto patch : {boundaryBusy, positiveLimit, negativeLimit})
      boundarySelection.setResources(patch, 50000);
    auto *boundaryActor = boundarySelection.get(boundaryWorker);
    boundaryActor->mineral = boundaryActor->target = boundaryBusy;
    boundaryActor->targetPosition =
        boundarySelection.get(boundaryBusy)->position;
    boundaryActor->order = 85;
    boundaryActor->orderState = 1;
    boundarySelection.get(boundaryBusy)->gatherer = boundaryHolder;
    auto negativeSelection = boundarySelection;
    boundarySelection.get(negativeLimit)->gatherer = boundaryHolder;
    negativeSelection.get(positiveLimit)->gatherer = boundaryHolder;
    boundarySelection.advance();
    negativeSelection.advance();
    check(boundarySelection.get(boundaryWorker)->mineral == boundaryBusy &&
          boundarySelection.get(boundaryWorker)->order == 86);
    check(negativeSelection.get(boundaryWorker)->mineral == negativeLimit &&
          negativeSelection.get(boundaryWorker)->planRequested);
    World neighboringContact(definitions, map);
    auto touching = neighboringContact.createFinished(7, {1043, 996}, 0);
    auto touchingTarget =
        neighboringContact.createFinished(176, {1000, 1000}, 11);
    neighboringContact.createFinished(176, {1032, 968}, 11);
    auto *touchingActor = neighboringContact.get(touching);
    touchingActor->motion.emplace(Point{1043, 1000}, 0,
                                  definitions->flingies[0]);
    touchingActor->sprite.setHeading(0);
    touchingActor->motion->route({{1043, 900}}, 0);
    for (int i = 0; i < 5; ++i)
      touchingActor->motion->advance();
    touchingActor->motion->clipStep({1043 * 256 + 176, 996 * 256 + 5});
    touchingActor->motion->route({{1043, 995}}, 0);
    touchingActor->moveTargetUnit = touchingTarget;
    touchingActor->targetPosition =
        touchingActor->moveTargetPosition = {1043, 995};
    touchingActor->order = 6;
    touchingActor->orderState = 1;
    touchingActor->orderTimer = 8;
    neighboringContact.advance();
    check(samePoint(touchingActor->motion->rawPosition(),
                    {1043 * 256 + 176, 996 * 256 + 5}) &&
          touchingActor->motion->speed() == 0 &&
          touchingActor->motion->nextSpeed() == 0 &&
          samePoint(touchingActor->moveTargetPosition, {1043, 996}));
    {
      auto slidingData = std::make_shared<Definitions>(*definitions);
      slidingData->units[7].bounds = {8, 4, 7, 11};
      slidingData->flingies[0].control = 2;
      World sliding(slidingData, map);
      auto mover = sliding.createFinished(7, {807, 1281}, 0, false);
      auto blocker = sliding.createFinished(7, {813, 1265}, 0, false);
      auto *actor = sliding.get(mover);
      auto *body = sliding.get(blocker);
      body->order = actor->order = 23;
      body->orderTimer = actor->orderTimer = 100;
      body->moveTargetPosition = {640, 1248};
      body->motion->route({body->moveTargetPosition}, 0);
      body->motion->waitForBody(body->motion->rawPosition(), -70, 512);
      actor->motion.emplace(Point{807, 1281}, -59, slidingData->flingies[0]);
      actor->motion->route({{754, 1273}}, 0);
      actor->motion->slideBlocked(actor->motion->rawPosition(), -58, -64, 2048);
      actor->collisionObstacle = actor->slideObstacle = blocker;
      actor->targetPosition = actor->moveTargetPosition = {640, 1248};
      actor->planRequested = true;
      actor->routePlanningFrames = 0;
      sliding.advance();
      check(actor->path && !actor->path->points.empty());
      check(samePoint(actor->path->points.front(), {797, 1281}));
    }
    {
      auto pausedData = std::make_shared<Definitions>(*definitions);
      pausedData->flingies[0].control = 2;
      World paused(pausedData, map);
      auto mover = paused.createFinished(7, {1000, 1000}, 0);
      auto blocker = paused.createFinished(7, {1030, 1000}, 0);
      auto *actor = paused.get(mover);
      auto *body = paused.get(blocker);
      actor->order = body->order = 23;
      actor->orderTimer = body->orderTimer = 100;
      actor->targetPosition = actor->moveTargetPosition = {1200, 1000};
      body->targetPosition = body->moveTargetPosition = {900, 1000};
      actor->motion.emplace(Point{1000, 1000}, 64, pausedData->flingies[0]);
      actor->motion->route({{1200, 1000}}, 0);
      actor->motion->advance();
      actor->motion->scriptSpeed(2048, true);
      actor->motion->setRouteAge(2);
      body->motion.emplace(Point{1030, 1000}, -64, pausedData->flingies[0]);
      body->motion->route({{900, 1000}}, 0);
      body->motion->advance();
      body->motion->scriptSpeed(2048, true);
      body->motion->advance();
      body->motion->setRouteAge(2);
      body->motion->blockedWaypoint({1030 * 256, 1000 * 256}, -64, 2048, true);
      check(body->motion->movingCollisionPaused() && body->motion->moving());
      paused.advance();
      check(actor->collisionObstacle == blocker);
      check(actor->motion->movingCollisionPaused() && !actor->motion->waitingForBody());
    }
    {
      auto occupiedData = std::make_shared<Definitions>(*definitions);
      occupiedData->units[7].bounds = {8, 4, 7, 11};
      occupiedData->flingies[0].control = 2;
      for (int scenario = 0; scenario != 3; ++scenario) {
      World occupied(occupiedData, map);
      auto mover = occupied.createFinished(7, {700, 1273}, 0);
      auto blocker = occupied.createFinished(7, {681, 1274}, 0);
      auto destination = occupied.createFinished(176, {640, 1248}, 1);
      auto *actor = occupied.get(mover);
      auto *body = occupied.get(blocker);
      actor->order = body->order = 23;
      actor->orderTimer = body->orderTimer = 100;
      actor->targetPosition = {640, 1248};
      actor->moveTargetPosition = scenario == 2 ? Point{640, 1248}
                                               : Point{669, 1269};
      if (scenario != 1)
        actor->moveTargetUnit = destination;
      actor->motion.emplace(Point{700, 1273}, -64, occupiedData->flingies[0]);
      actor->motion->route({{676, 1273}, {669, 1269}}, 0);
      actor->motion->advance();
      actor->motion->scriptSpeed(1536, true);
      actor->motion->setRouteAge(10);
      body->moveTargetPosition = body->position;
      occupied.advance();
      check(actor->collisionObstacle == blocker);
      check(actor->motion->replanning() == (scenario != 1));
      check(samePoint(actor->position, {700, 1273}));
      }
    }
    {
      auto lockedData = std::make_shared<Definitions>(*definitions);
      std::vector<std::uint8_t> script(64);
      auto word = [&](int at, int value) {
        script.at(at) = std::uint8_t(value);
        script.at(at + 1) = std::uint8_t(value >> 8);
      };
      for (int i = 0; i != 4; ++i)
        script[4 + i] = std::uint8_t("SCPE"[i]);
      auto idle = int(script.size());
      script.insert(script.end(), {5, 125, 7, std::uint8_t(idle), 0});
      word(8, 12);
      for (int animation = 0; animation != 14; ++animation)
        word(12 + animation * 2, idle);
      word(0, 48); word(48, 0); word(50, 4); word(52, 65535);
      lockedData->program = iscript::Program(std::move(script));
      lockedData->images[0].animated = true;
      for (bool scripted : {false, true})
      for (bool locked : {false, true}) {
        lockedData->flingies[0].control = scripted ? 2 : 0;
        World stopping(lockedData, map);
        auto mover = stopping.createFinished(7, {1000, 1000}, 0);
        auto *actor = stopping.get(mover);
        actor->order = 23;
        actor->orderTimer = 100;
        actor->walking = true;
        actor->attackActive = true;
        Random fixtureRandom;
        actor->sprite.animate(2, lockedData->resources(), fixtureRandom);
        actor->sprite.context.uninterruptible = locked;
        actor->moveTargetPosition = actor->position;
        stopping.advance();
        if (locked)
          check(actor->sprite.images[actor->sprite.main].cursor.animation == 2);
        else
          check(actor->sprite.images[actor->sprite.main].cursor.animation == 12);
        check(!actor->walking);
      }
    }
    {
      auto fireData = std::make_shared<Definitions>([&](const std::string &name) {
        auto bytes = readFixture(name);
        if (name == "arr/images.dat")
          bytes[13986 + 2 * 3996] = 1;
        return bytes;
      });
      fireData->units[7] = definitions->units[7];
      fireData->units[7].hitpoints = 25600;
      World fire(fireData, map);
      auto building = fire.createFinished(7, {1000, 1000}, 0);
      auto images = [&](const World &world) {
        std::vector<int> result;
        const auto &sprite = world.get(building)->sprite;
        for (auto id : sprite.order)
          if (sprite.images[id].type >= 450 && sprite.images[id].type < 494)
            result.push_back(sprite.images[id].type);
        return result;
      };
      check(images(fire).empty());
      auto expected = Random(fire.randomState()); expected.next();
      fire.setHitpoints(building, 22187);
      auto first = images(fire);
      check(first.size() == 1 && (first[0] == 450 || first[0] == 451));
      check(fire.randomState() == expected.state());
      fire.setHitpoints(building, 18774);
      auto grown = images(fire);
      check(grown == std::vector<int>{first[0] + 22} &&
            fire.randomState() == expected.state());
      auto snapshot = fire;
      expected.next();
      fire.setHitpoints(building, 15361);
      auto two = images(fire);
      check(two.size() == 2 && two[0] != first[0] &&
            (two[0] == 450 || two[0] == 451) && two[1] == grown[0]);
      check(fire.randomState() == expected.state());
      fire.setHitpoints(building, 11948);
      two = images(fire);
      check(two.size() == 2 && two[0] >= 472 && two[1] >= 472 &&
            fire.randomState() == expected.state());
      fire.setHitpoints(building, 11949);
      check(images(fire).size() == 2 && images(fire)[0] < 472);
      fire.setHitpoints(building, 25600);
      check(images(fire).empty() && fire.randomState() == expected.state());
      check(images(snapshot) == grown &&
            snapshot.get(building)->vitals.hitpoints == 18774);
      fire.get(building)->completed = false;
      fire.setHitpoints(building, 1);
      check(images(fire).empty());
    }
    {
      auto towerData = std::make_shared<Definitions>(*definitions);
      towerData->units[162] = towerData->units[7];
      auto &cannon = towerData->units[162];
      cannon.flags = 1;
      cannon.humanIdle = cannon.returnIdle = 18;
      cannon.sightRange = 7;
      cannon.groundWeapon = 0;
      towerData->units[156] = cannon;
      towerData->units[156].humanIdle = towerData->units[156].returnIdle = 23;
      towerData->weapons[0].maximumRange = 160;
      towerData->weapons[0].attackAngle = 128;
      World tower(towerData, map);
      auto gun = tower.createFinished(162, {1000, 1000}, 0, false);
      tower.createFinished(156, {1000, 1064}, 0, false);
      auto near = tower.createFinished(7, {1050, 1000}, 1, false);
      auto far = tower.createFinished(7, {1080, 1000}, 1, false);
      for (auto enemy : {near, far}) {
        tower.get(enemy)->order = 23;
        tower.get(enemy)->orderTimer = 100;
      }
      tower.get(gun)->retaliationTarget = far;
      check(tower.acquireTarget(gun) == near);
      auto expected = Random(tower.randomState()); expected.next();
      tower.advance();
      check(tower.get(gun)->order == 19 &&
            (tower.get(gun)->target == near || tower.get(gun)->target == far));
      check(tower.randomState() == expected.state());
    }
    {
      auto retargetData = std::make_shared<Definitions>(*definitions);
      retargetData->units[7].humanIdle = retargetData->units[7].returnIdle = 23;
      retargetData->units[7].sightRange = 7;
      retargetData->units[7].acquisitionRange = 5;
      retargetData->units[7].groundWeapon = 0;
      retargetData->units[7].turret = 228;
      retargetData->weapons[0].maximumRange = 64;
      retargetData->weapons[0].attackAngle = 0;
      for (bool automatic : {true, false}) {
        World retarget(retargetData, map);
        auto actor = retarget.createFinished(7, {900, 900}, 0);
        auto enemy = retarget.createFinished(7, {960, 900}, 1);
        retarget.get(enemy)->orderTimer = 100;
        auto *unit = retarget.get(actor);
        unit->order = 10;
        unit->orderState = 1;
        unit->autoAttackTargeting = automatic;
        unit->groundCooldown = unit->airCooldown = 7;
        unit->queuedCommands.push_back({6, {1200, 1000}, {}});
        auto snapshot = retarget;
        retarget.advance(); snapshot.advance();
        if (automatic) {
          check(unit->order == 10 && unit->target == enemy && unit->orderState == 1);
          check(unit->queuedCommands.size() == 1 && unit->autoAttackTargeting);
          check(unit->groundCooldown == 6 && snapshot.get(actor)->target == enemy);
        } else {
          check(unit->order == 6 && unit->target.index < 0 && unit->queuedCommands.empty());
        }
      }
      {
        World empty(retargetData, map);
        auto actor = empty.createFinished(7, {900, 900}, 0);
        empty.get(actor)->order = 10;
        empty.get(actor)->orderState = 1;
        empty.get(actor)->autoAttackTargeting = true;
        empty.get(actor)->queuedCommands.push_back({6, {1200, 1000}, {}});
        empty.advance();
        check(empty.get(actor)->order == 6 && !empty.get(actor)->autoAttackTargeting);
      }
      retargetData->units[8] = retargetData->units[7];
      retargetData->units[8].flags = 1;
      retargetData->units[8].groundWeapon = retargetData->units[8].airWeapon = 130;
      retargetData->units[9] = retargetData->units[7];
      retargetData->units[9].flags &= ~8U;
      for (int scenario = 0; scenario != 5; ++scenario) {
        World priority(retargetData, map);
        auto actor = priority.createFinished(7, {900, 900}, 0);
        auto old = priority.createFinished(scenario < 2 || scenario == 4 ? 8 : 9,
            scenario == 3 ? Point{1040, 900} : Point{950, 900}, 1, false);
        auto threat = priority.createFinished(9, {940, 925}, 1, false);
        priority.get(old)->orderTimer = priority.get(threat)->orderTimer = 100;
        auto *unit = priority.get(actor);
        unit->order = 10; unit->orderState = 1; unit->target = old;
        unit->targetPosition = priority.get(old)->position;
        unit->autoAttackTargeting = scenario != 1;
        unit->groundCooldown = unit->airCooldown = 100;
        UnitHandle preferred = scenario == 1 || scenario == 2 ? old : threat;
        if (scenario == 4) {
          auto aggressor = priority.createFinished(9, {970, 900}, 1, false);
          priority.get(aggressor)->orderTimer = 100;
          unit->retaliationTarget = aggressor;
          preferred = aggressor;
        }
        priority.advance();
        check(unit->target == preferred);
      }
      {
        World origins(retargetData, map);
        auto actor = origins.createFinished(7, {900, 900}, 0);
        auto enemy = origins.createFinished(7, {960, 900}, 1);
        origins.get(enemy)->orderTimer = 100;
        origins.get(actor)->order = 3;
        origins.advance();
        check(origins.get(actor)->order == 10 && origins.get(actor)->autoAttackTargeting);
        origins.attack(actor, enemy);
        check(!origins.get(actor)->autoAttackTargeting);
      }
    }
    World tangentWorld(definitions, map);
    auto tangentWorker = tangentWorld.createFinished(7, {1000, 1000}, 0);
    auto tangentOriginal = tangentWorld.createFinished(176, {1044, 964}, 11);
    tangentWorld.createFinished(176, {1012, 1028}, 11);
    auto *tangentActor = tangentWorld.get(tangentWorker);
    tangentActor->motion.emplace(Point{1000, 1000}, -128,
                                 definitions->flingies[0]);
    auto &tangentMotion = *tangentActor->motion;
    tangentMotion.route({{1000, 1500}}, 0);
    for (int i = 0; i < 20; ++i)
      tangentMotion.advance();
    tangentMotion.slideBlocked(tangentMotion.rawPosition(), -128, -128, 1280);
    tangentMotion.advance();
    tangentMotion.finishSlide(tangentMotion.rawPosition(), false);
    for (int i = 0; i < 10; ++i) {
      tangentMotion.advance();
      tangentMotion.finishSlide(tangentMotion.rawPosition(), false);
    }
    const Point tangentRaw{1000 * 256 + 239, 1000 * 256 + 54};
    tangentMotion.clipStep(tangentRaw);
    tangentActor->position = tangentMotion.position();
    tangentActor->slideObstacle = tangentOriginal;
    tangentActor->targetPosition =
        tangentActor->moveTargetPosition = {1000, 1500};
    tangentActor->order = 6;
    tangentActor->orderState = 1;
    tangentActor->orderTimer = 8;
    tangentWorld.advance();
    check(samePoint(tangentMotion.rawPosition(), tangentRaw) &&
          tangentMotion.speed() == 1280 && tangentMotion.nextSpeed() == 1280 &&
          tangentMotion.replanning() && tangentMotion.desiredHeading() == -128);
    auto tangentCopy = tangentWorld;
    tangentWorld.advance();
    tangentCopy.advance();
    check(samePoint(tangentMotion.rawPosition(), tangentRaw) &&
          tangentMotion.speed() == 0 && !tangentMotion.updatesHeading() &&
          samePoint(tangentCopy.get(tangentWorker)->motion->rawPosition(),
                    tangentRaw) &&
          tangentCopy.get(tangentWorker)->motion->speed() == 0);
    World cornerContact(definitions, map);
    auto cornerWorker = cornerContact.createFinished(7, {1000, 1004}, 0);
    cornerContact.createFinished(176, {1044, 1000}, 11);
    cornerContact.createFinished(176, {1000, 1032}, 11);
    auto *cornerMover = cornerContact.get(cornerWorker);
    cornerMover->motion.emplace(Point{1000, 1004}, 96,
                                definitions->flingies[0]);
    cornerMover->motion->route({{1100, 1104}}, 0);
    cornerMover->order = 6;
    cornerMover->orderState = 1;
    cornerMover->targetPosition =
        cornerMover->moveTargetPosition = {1100, 1104};
    for (int i = 0; i < 6; ++i) {
      cornerContact.advance();
      if (!samePoint(cornerContact.get(cornerWorker)->position, {1000, 1004}))
        break;
    }
    check(cornerContact.get(cornerWorker)->position.x == 1000 &&
          cornerContact.get(cornerWorker)->position.y < 1004);
    auto escapeCv = cv;
    escapeCv[72] = 1;
    std::vector<std::uint8_t> escapeVf(64);
    for (int i = 0; i < 16; ++i)
      escapeVf[32 + i * 2] = 1;
    std::vector<std::uint16_t> escapeTiles(4096, 16);
    escapeTiles[30 * 64 + 30] = escapeTiles[31 * 64 + 30] = 0;
    escapeTiles[29 * 64 + 31] = 0;
    auto escapeMap = std::make_shared<WorldMap>(
        Terrain(64, 64, escapeTiles, escapeCv, escapeVf));
    World terrainEscape(definitions, escapeMap);
    auto escapeWorker = terrainEscape.createFinished(7, {1004, 1000}, 0);
    auto *escapeActor = terrainEscape.get(escapeWorker);
    escapeActor->motion.emplace(Point{1004, 1000}, -64,
                                definitions->flingies[0]);
    escapeActor->motion->route({{944, 1000}}, 0, false, false);
    escapeActor->order = 6;
    escapeActor->orderState = 1;
    escapeActor->targetPosition = escapeActor->moveTargetPosition = {944, 1000};
    bool escapedNorth = false, escapeReplanned = false;
    for (int i = 0; i < 100; ++i) {
      terrainEscape.advance();
      auto &motion = *terrainEscape.get(escapeWorker)->motion;
      escapedNorth |= motion.position().y < 1000;
      if (motion.replanning()) {
        check(escapedNorth && motion.moving());
        auto contact = motion.rawPosition();
        terrainEscape.advance();
        check(samePoint(motion.rawPosition(), contact) && motion.speed() == 0 &&
              !motion.replanning());
        escapeReplanned = true;
        break;
      }
    }
    check(escapedNorth && escapeReplanned);
    auto retryCv = cv;
    retryCv.resize(156);
    retryCv[124] = 1;
    std::vector<std::uint8_t> retryVf(64);
    for (int i = 0; i < 32; ++i)
      retryVf[i * 2] = 1;
    retryVf[62] = 0;
    std::vector<std::uint16_t> retryTiles(4096, 16);
    retryTiles[31 * 64 + 31] = 32;
    auto retryMap = std::make_shared<WorldMap>(
        Terrain(64, 64, retryTiles, retryCv, retryVf));
    World slowerRetry(definitions, retryMap);
    auto retryWorker = slowerRetry.createFinished(7, {1004, 1004}, 0);
    auto *retryActor = slowerRetry.get(retryWorker);
    auto cornerPhysics = definitions->flingies[0];
    cornerPhysics.speed = 610;
    Motion cornerApproach({944, 1004}, 64, cornerPhysics);
    cornerApproach.route({{1004, 1004}}, 0, false, false);
    for (int i = 0; i < 100 && !cornerApproach.arrived(); ++i)
      cornerApproach.advance();
    check(cornerApproach.arrived() && cornerApproach.speed() == 610);
    cornerApproach.route({{1004, 1004}, {1000, 1008}}, 0, false, true);
    retryActor->motion = std::move(cornerApproach);
    retryActor->order = 6;
    retryActor->orderState = 1;
    retryActor->targetPosition = retryActor->moveTargetPosition = {1000, 1008};
    slowerRetry.advance();
    check(samePoint(retryActor->motion->rawPosition(),
                    {1004 * 256 + 420, 1004 * 256 + 224}) &&
          retryActor->motion->speed() == 476 &&
          retryActor->motion->nextSpeed() == 476 &&
          retryActor->motion->collisionDebt() == 1);
    {
      World occupiedGoal(definitions, map);
      auto mover = occupiedGoal.createFinished(7, {1000, 1000}, 0);
      auto obstacle = occupiedGoal.createFinished(7, {1040, 1000}, 0);
      auto *actor = occupiedGoal.get(mover);
      occupiedGoal.get(obstacle)->order = 23;
      occupiedGoal.get(obstacle)->orderTimer = 100;
      actor->order = 6;
      actor->orderState = 1;
      actor->orderTimer = 100;
      actor->userMovementPending = true;
      actor->targetPosition = actor->moveTargetPosition = {1040, 1000};
      actor->motion.emplace(Point{1000, 1000}, 64, definitions->flingies[0]);
      actor->motion->route({{1040, 1000}}, 0);
      for (int frame = 0; frame < 30 && actor->moveTargetPosition.x == 1040; ++frame)
        occupiedGoal.advance();
      check(actor->moveTargetPosition.x < 1040);
      check(!actor->userMovementPending);
    }
    {
      World collisionRoute(definitions, map);
      auto mover = collisionRoute.createFinished(7, {1000, 1000}, 0);
      auto obstacle = collisionRoute.createFinished(7, {1040, 1000}, 0);
      auto *actor = collisionRoute.get(mover);
      auto *body = collisionRoute.get(obstacle);
      actor->order = body->order = 6;
      actor->orderState = body->orderState = 1;
      actor->orderTimer = body->orderTimer = 8;
      actor->targetPosition = actor->moveTargetPosition = {1100, 1000};
      body->targetPosition = body->moveTargetPosition = {1140, 1000};
      actor->motion.emplace(Point{1000, 1000}, 64, definitions->flingies[0]);
      actor->motion->route({{1100, 1000}}, 0);
      actor->motion->setRouteAge(10);
      actor->collisionObstacle = obstacle;
      actor->motion->blockedWaypoint(actor->motion->rawPosition(), 64, 0);
      collisionRoute.advance();
      actor = collisionRoute.get(mover);
      check(actor->path && !actor->path->points.empty() &&
            actor->path->points.front().y != 1000);
    }
    {
      auto waitData = std::make_shared<Definitions>(*definitions);
      waitData->flingies[0].control = 2;
      waitData->flingies[0].turnRate = 27;
      auto preparedWait = [&](int direction, Point obstacleGoal) {
        World waiting(waitData, map);
        auto mover = waiting.createFinished(7, {1000, 1000}, 0);
        auto obstacle = waiting.createFinished(7, {1080, 1000}, 0);
        auto *actor = waiting.get(mover);
        auto *body = waiting.get(obstacle);
        actor->order = body->order = 6;
        actor->orderState = body->orderState = 1;
        actor->orderTimer = body->orderTimer = 8;
        actor->targetPosition = actor->moveTargetPosition = {1200, 1000};
        body->targetPosition = body->moveTargetPosition = obstacleGoal;
        body->motion.emplace(Point{1080, 1000}, direction, waitData->flingies[0]);
        body->motion->route({obstacleGoal}, 0);
        body->motion->advance();
        body->motion->scriptSpeed(512, true);
        body->motion->advance();
        body->position = body->motion->position();
        actor->motion.emplace(Point{1000, 1000}, 64, waitData->flingies[0]);
        actor->motion->route({{1200, 1000}}, 0);
        actor->motion->advance();
        actor->motion->scriptSpeed(2048, true);
        actor->motion->advance();
        actor->motion->setRouteAge(2);
        actor->collisionObstacle = obstacle;
        actor->motion->waitForBody({1000 * 256, 1000 * 256}, 64, 2048, true);
        return std::pair{std::move(waiting), mover};
      };
      auto [outgoing, outHandle] = preparedWait(64, {1200, 1000});
      auto [incoming, inHandle] = preparedWait(-64, {900, 1000});
      outgoing.advance();
      incoming.advance();
      check(outgoing.get(outHandle)->position.x > 1000 &&
            incoming.get(inHandle)->position.x == 1000);
    }
    {
      World touchingRoute(definitions, map);
      auto mover = touchingRoute.createFinished(7, {1018, 1000}, 0);
      auto obstacle = touchingRoute.createFinished(7, {1040, 1000}, 0);
      auto *actor = touchingRoute.get(mover);
      auto *body = touchingRoute.get(obstacle);
      actor->order = body->order = 6;
      actor->orderState = body->orderState = 1;
      actor->orderTimer = body->orderTimer = 8;
      actor->targetPosition = actor->moveTargetPosition = {1100, 1000};
      body->targetPosition = body->moveTargetPosition = {1140, 1000};
      actor->motion.emplace(Point{1018, 1000}, 64, definitions->flingies[0]);
      actor->motion->route({{1100, 1000}}, 0);
      actor->motion->setRouteAge(10);
      actor->collisionObstacle = obstacle;
      actor->motion->blockedWaypoint(actor->motion->rawPosition(), 64, 0);
      touchingRoute.advance();
      actor = touchingRoute.get(mover);
      check(actor->path && !actor->path->points.empty() &&
            actor->path->points.front().y != 1000);
    }
    World clippedWorld(definitions, map);
    auto clippedWorldWorker = clippedWorld.createFinished(7, {1000, 1000}, 0);
    auto *approachActor = clippedWorld.get(clippedWorldWorker);
    approachActor->motion.emplace(Point{1000, 1000}, 64,
                                  definitions->flingies[0]);
    approachActor->sprite.setHeading(64);
    approachActor->motion->route({{1100, 1000}}, 0);
    approachActor->targetPosition =
        approachActor->moveTargetPosition = {1100, 1000};
    approachActor->order = 6;
    approachActor->orderState = 1;
    approachActor->orderTimer = 8;
    clippedWorld.advance();
    check(approachActor->walking && approachActor->motion->speed() == 67);
    approachActor->motion->clipStep({1100 * 256 - 32, 1000 * 256});
    approachActor->position = approachActor->motion->position();
    clippedWorld.advance();
    check(!approachActor->motion->speed() && approachActor->motion->moving() &&
          approachActor->walking);
    clippedWorld.advance(2);
    check(!approachActor->walking && approachActor->position.x == 1100);
    auto chamberDefinitions = std::make_shared<Definitions>(*definitions);
    chamberDefinitions->units[106].flags = 1;
    chamberDefinitions->units[106].humanIdle = 23;
    chamberDefinitions->units[106].bounds = {10, 64, 10, 64};
    chamberDefinitions->units[154].flags = 1;
    chamberDefinitions->units[154].humanIdle = 23;
    chamberDefinitions->units[154].bounds = {64, 10, 64, 10};
    chamberDefinitions->units[93] = chamberDefinitions->units[7];
    chamberDefinitions->units[93].humanIdle = 166;
    chamberDefinitions->flingies[0].control = 2;
    World chamber(chamberDefinitions, map);
    chamber.createFinished(106, {978, 1000}, 11);
    chamber.createFinished(106, {1022, 1000}, 11);
    auto gate = chamber.createFinished(154, {1000, 978}, 11);
    chamber.createFinished(154, {1000, 1022}, 11);
    auto trapped = chamber.createFinished(93, {1000, 1000}, 11, false);
    auto *actor = chamber.get(trapped);
    actor->orderState = 1;
    actor->orderTimer = 8;
    // Keep the goal beyond every wall's contact perimeter, so this
    // exercises a genuinely failed route until the north gate opens.
    actor->targetPosition = actor->moveTargetPosition = {1000, 900};
    actor->planRequested = true;
    actor->routePlanningFrames = 0;
    chamber.advance();
    check(actor->path && actor->path->points.empty() && !actor->immovable &&
          actor->orderState == 1 && actor->moveTargetPosition.y == 900);
    auto closedChamber = chamber;
    chamber.remove(gate);
    chamber.advance(12);
    closedChamber.advance(12);
    check(chamber.get(trapped)->path &&
          !chamber.get(trapped)->path->points.empty() &&
          chamber.get(trapped)->orderState == 1 &&
          !chamber.get(trapped)->immovable);
    check(closedChamber.get(trapped)->immovable &&
          closedChamber.get(trapped)->orderState == 2 &&
          closedChamber.get(trapped)->mainTimer == 75 &&
          closedChamber.get(gate)->visible);
    std::cout << "PASS: native unit allocation, initial motion, snapshot "
                 "isolation and generation handles\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
