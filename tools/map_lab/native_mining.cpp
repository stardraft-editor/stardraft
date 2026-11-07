// SPDX-License-Identifier: MIT
#include "native_mining.h"
#include "native_world.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace maplab {
namespace sim = mapeditor::simulation;
namespace {
std::vector<sim::MiningControl>
freshControls(const std::vector<sim::MiningControl> &rules) {
  std::vector<sim::MiningControl> result;
  for (const auto &rule : rules) {
    auto next = sim::mining_search::makeRule(rule.worker, rule.field, rule.spot,
                                             rule.enabled);
    next.cycle = rule.cycle;
    next.neighbor = rule.neighbor;
    next.delay = rule.delay;
    next.detour = rule.detour;
    next.cargoDelay = rule.cargoDelay;
    result.push_back(next);
  }
  return result;
}
} // namespace
std::shared_ptr<NativeMining>
NativeMining::create(const FileReader &read, const Bytes &scenario,
                     int x, int y, int race, int count) {
  if (race < 0 || race > 2 || count < 0 || count > 64)
    throw std::invalid_argument("Invalid mining configuration");
  auto result = std::make_shared<NativeMining>();
  auto &world = *(result->world_ = loadNativeWorld(read, scenario));
  std::set<int> existing;
  for (int id : world.visibleUnits())
    if (world.units()[id]->owner == 0)
      existing.insert(id);
  auto workers = world.startingUnits(race, {x, y}, 0);
  int depotType = race == 0 ? 131 : race == 1 ? 106 : 154;
  for (int id : world.visibleUnits()) {
    const auto &unit = *world.units()[id];
    if (!existing.count(id) && unit.owner == 0 && unit.type == depotType)
      result->depot_ = unit.handle;
  }
  auto *depot = world.get(result->depot_);
  // Only successfully placed workers participate in the mining test.
  workers.erase(std::remove_if(workers.begin(), workers.end(), [&](auto h) {
                  const auto *worker = world.get(h);
                  return !worker || worker->dead || !worker->visible ||
                         !worker->motion;
                }),
                workers.end());
  if (!depot || depot->dead || !depot->visible || workers.empty())
    throw std::runtime_error("Cannot place the mining depot and workers");
  std::sort(workers.begin(), workers.end(), [&](auto a, auto b) {
    return world.get(a)->birth > world.get(b)->birth;
  });
  std::vector<Handle> patches;
  auto range = [&](Handle h) {
    auto p = world.get(h)->position;
    return sim::distance({p.x - depot->position.x, p.y - depot->position.y});
  };
  for (int id : world.visibleUnits()) {
    const auto &unit = *world.units()[id];
    if (unit.type >= 176 && unit.type <= 178 && range(unit.handle) <= 384)
      patches.push_back(unit.handle);
  }
  std::sort(patches.begin(), patches.end(), [&](auto a, auto b) {
    int da = range(a), db = range(b);
    return da == db ? a.index < b.index : da < db;
  });
  if (patches.empty())
    throw std::runtime_error("No mineral patches within 12 tiles of this base");
  count = count ? count : int(patches.size());
  int workerType = race == 0 ? 41 : race == 1 ? 7 : 64;
  while (int(workers.size()) < count) {
    auto h = world.createFinished(workerType, depot->position, 0);
    const auto *worker = world.get(h);
    if (!worker || worker->dead || !worker->visible || !worker->motion)
      throw std::runtime_error("Cannot place a mining worker");
    workers.push_back(h);
  }
  while (int(workers.size()) > count) {
    world.remove(workers.back());
    workers.pop_back();
  }
  result->displayed_ = workers;
  result->displayed_.push_back(result->depot_);
  std::sort(result->displayed_.begin(), result->displayed_.end(),
            [&](auto a, auto b) {
              return world.get(a)->birth > world.get(b)->birth;
            });
  for (std::size_t i = 0; i < workers.size(); ++i) {
    auto patch = patches[i % patches.size()];
    world.setResources(patch, 50000);
    world.harvest(workers[i], patch);
    result->assignments_.push_back({workers[i], patch});
  }
  result->start_ = world.frame();
  world.prepareNavigation(workers.front());
  result->searchSeed_ = std::make_unique<sim::World>(world);
  return result;
}
void NativeMining::resetStats() {
  start_ = world_->frame();
  for (auto &assignment : assignments_) {
    assignment.last = -1;
    assignment.cycle = assignment.deliveries = 0;
  }
}
bool NativeMining::searching() const {
  return std::any_of(statuses_.begin(), statuses_.end(),
                     [](const auto &status) { return status.running; });
}
void NativeMining::poll() {
  if (auto *job = positionJob_.get(); job && statuses_[0].running) {
    auto &status = statuses_[0];
    status.seconds = job->seconds();
    status.validating = job->validating.load();
    double budget = job->sliceLimit.load();
    status.progress =
        budget > 0 ? std::min(1., job->sliceElapsed.load() / budget) : 0;
    if (job->done()) {
      status.running = false;
      status.finished = true;
      status.exhausted = job->exhausted.load();
      status.validating = false;
      status.score = job->result ? job->result->score() : 0;
      if (job->changed.load()) {
        const auto &plan = *job->result;
        world_ = std::make_unique<sim::World>(*job->input);
        jobs_[1].reset();
        jobs_[2].reset();
        cargo_.clear();
        cycle_.clear();
        statuses_[1] = {};
        statuses_[2] = {};
        fixedGuides_.clear();
        status.found = 0;
        for (std::size_t i = 0; i < plan.positions.size(); ++i)
          if (plan.positions[i].stable) {
            auto a = plan.assignments[i];
            world_->relocateWorkerForSearch(a.worker,
                                            plan.positions[i].position);
            world_->harvest(a.worker, a.field);
            if (plan.control[i].mean() - plan.selected[i].mean() > .5 &&
                plan.control[i].mean(1) - plan.selected[i].mean(1) > .1 &&
                plan.control[i].mean(2) - plan.selected[i].mean(2) > .1) {
              ++status.found;
              const auto *w = world_->get(a.worker), *m = world_->get(a.field);
              fixedGuides_.push_back({w->position.x, w->position.y,
                                      m->position.x, m->position.y, 0, 0,
                                      w->type, w->handle.index});
            }
          }
        searchSeed_ = std::make_unique<sim::World>(*world_);
        resetStats();
      }
    }
  }
  for (int kind : {1, 2}) {
    auto *job = jobs_[kind].get();
    auto &status = statuses_[kind];
    if (!job || !status.running)
      continue;
    status.seconds = job->seconds();
    status.validating = job->validating.load();
    double budget = job->sliceLimit.load();
    status.progress =
        budget > 0 ? std::min(1., job->sliceElapsed.load() / budget) : 0;
    if (!job->done())
      continue;
    status.running = false;
    status.finished = true;
    status.exhausted = job->exhausted.load();
    status.validating = false;
    status.score = job->result ? job->result->score() : 0;
    if (!job->changed.load())
      continue;
    world_ = std::make_unique<sim::World>(*job->input);
    if (kind == 1) {
      cargo_ = freshControls(job->result->chosen);
      jobs_[2].reset();
      cycle_.clear();
      statuses_[2] = {};
      status.found =
          int(std::count_if(cargo_.begin(), cargo_.end(),
                            [](const auto &r) { return r.enabled; }));
    } else {
      cycle_ = freshControls(job->result->chosen);
      cargo_ = freshControls(cargo_);
      status.found =
          int(std::count_if(cycle_.begin(), cycle_.end(),
                            [](const auto &r) { return r.enabled; }));
    }
    resetStats();
  }
}
void NativeMining::search(int technique, double seconds) {
  if (technique < 0 || technique > 2 || searching())
    return;
  if (!std::isfinite(seconds))
    throw std::invalid_argument("Nonfinite mining search time allowance");
  seconds = std::clamp(seconds, .05, 600.);
  if (technique == 0) {
    if (positionJob_) {
      if (positionJob_->exhausted.load())
        return;
      positionJob_->extend(seconds);
    } else {
      std::vector<sim::MiningAssignment> assignments;
      for (const auto &a : assignments_)
        assignments.push_back({a.worker, a.patch});
      positionJob_ = std::make_unique<PositionJob>(
          *searchSeed_,
          [assignments](const sim::World &initial, auto *pool) {
            return std::make_unique<PositionPlan>(initial, assignments, pool);
          },
          seconds);
    }
    auto &status = statuses_[0];
    status.running = true;
    status.finished = false;
    status.progress = 0;
    return;
  }
  auto &job = jobs_[technique];
  if (job) {
    if (job->exhausted.load())
      return;
    job->extend(seconds);
  } else {
    std::vector<sim::MiningAssignment> assignments;
    for (const auto &a : assignments_)
      assignments.push_back({a.worker, a.patch});
    auto background =
        technique == 2 ? freshControls(cargo_) : std::vector<Control>{};
    job = std::make_unique<SearchJob>(
        *searchSeed_,
        [assignments, background, technique](const sim::World &initial,
                                             auto *pool) {
          return std::make_unique<SearchPlan>(initial, assignments, background,
                                              pool, technique == 2);
        },
        seconds);
  }
  auto &status = statuses_[technique];
  status.running = true;
  status.finished = false;
  status.progress = 0;
}
void NativeMining::advance(int frames) {
  if (frames < 0)
    throw std::invalid_argument("Negative mining frame count");
  poll();
  if (searching())
    return;
  for (int i = 0; i < frames; ++i) {
    std::vector<bool> carrying;
    for (const auto &a : assignments_)
      carrying.push_back(world_->get(a.worker)->cargo & 2);
    for (auto &rule : cargo_)
      rule.apply(*world_);
    for (auto &rule : cycle_)
      rule.apply(*world_);
    world_->advance();
    for (std::size_t n = 0; n < assignments_.size(); ++n) {
      auto &a = assignments_[n];
      if (carrying[n] && !(world_->get(a.worker)->cargo & 2)) {
        int now = world_->frame();
        if (a.last >= 0)
          a.cycle = now - a.last;
        a.last = now;
        ++a.deliveries;
      }
    }
  }
}
MiningFrame NativeMining::snapshot() const {
  MiningFrame frame;
  frame.frame = world_->frame();
  frame.searches = statuses_;
  for (auto handle : displayed_) {
    const auto *unit = world_->get(handle);
    if (unit && unit->visible && !unit->dead)
      frame.units.push_back({unit->position.x, unit->position.y, unit->type,
                             unit->owner, bool(unit->cargo & 2)});
  }
  for (const auto &image : world_->renderUnits(displayed_))
    if (image.modifier == 0 || image.modifier == 1 || image.modifier == 10)
      frame.sprites.push_back({image.position.x, image.position.y, image.image,
                               image.frame, image.owner, image.flip,
                               image.modifier == 10});
  std::map<int, std::size_t> patchIndices;
  double elapsed = std::max(1, frame.frame - start_), minute = 60000. / 42.;
  for (const auto &a : assignments_) {
    const auto *patch = world_->get(a.patch);
    if (!patch || !patch->visible || patch->dead)
      continue;
    auto [it, inserted] =
        patchIndices.emplace(a.patch.index, frame.patches.size());
    if (inserted)
      frame.patches.push_back({patch->position.x, patch->position.y});
    auto &p = frame.patches[it->second];
    if (a.cycle)
      p.lastCycle = a.cycle;
    auto income = a.deliveries * 8. * minute / elapsed;
    p.perMinute += income;
    frame.income += income;
  }
  frame.guides = fixedGuides_;
  for (const auto *rules : {&cargo_, &cycle_})
    for (const auto &rule : *rules) {
      if (!rule.enabled || !rule.commands)
        continue;
      const auto *worker = world_->get(rule.worker);
      if (!worker || !worker->visible || worker->dead)
        continue;
      if (rule.cycle) {
        const auto *neighbor = world_->get(rule.neighbor),
                   *field = world_->get(rule.field);
        if (!neighbor || !field || !neighbor->visible || !field->visible)
          continue;
        frame.guides.push_back({rule.firstAt.x, rule.firstAt.y,
                                neighbor->position.x, neighbor->position.y, 2,
                                1, worker->type, worker->handle.index});
        if (rule.commands >= 2)
          frame.guides.push_back({rule.secondAt.x, rule.secondAt.y,
                                  field->position.x, field->position.y, 2, 2,
                                  worker->type, worker->handle.index});
      } else
        frame.guides.push_back({rule.issuedAt.x, rule.issuedAt.y, 0, 0, 1, 0,
                                worker->type, worker->handle.index});
    }
  return frame;
}
std::vector<MiningDebugUnit> NativeMining::debugUnits(bool all) const {
  std::vector<MiningDebugUnit> result;
  auto selected = displayed_;
  if (all) {
    selected.clear();
    for (int id : world_->visibleUnits())
      selected.push_back(world_->units()[id]->handle);
  }
  for (auto h : selected) {
    const auto &u = *world_->get(h);
    MiningDebugUnit row;
    row.id = h.index;
    const auto &image = u.sprite.images[u.sprite.main];
    row.animation = {image.type, image.frame(), image.cursor.animation,
                     image.cursor.wait, int(image.cursor.pc)};
    if (u.motion) {
      const auto &motion = *u.motion;
      row.nativeMotionFlags =
          int(motion.moving()) | (int(motion.stopped()) << 1) |
          (int(motion.settling()) << 2) | (int(motion.planning()) << 3) |
          (int(motion.arrived()) << 4) | (int(motion.slidingStep()) << 5) |
          (int(motion.replanning()) << 6) |
          (int(motion.updatesHeading()) << 7) | (int(u.walking) << 8);
    }
    auto raw = u.motion ? u.motion->rawPosition()
                        : sim::Point{u.position.x * 256, u.position.y * 256};
    auto velocity = u.motion ? u.motion->velocity() : sim::Point{};
    row.raw = {raw.x, raw.y};
    row.velocity = {velocity.x, velocity.y};
    row.speed = {u.motion ? u.motion->speed() : 0,
                 u.motion ? u.motion->nextSpeed() : 0};
    row.move = {u.moveTargetPosition.x, u.moveTargetPosition.y};
    row.target = {u.targetPosition.x, u.targetPosition.y,
                  world_->get(u.target) ? u.target.index : -1};
    row.heading = {u.motion ? u.motion->facing() : u.sprite.context.heading,
                   u.motion ? u.motion->travelHeading()
                            : u.sprite.context.heading,
                   u.motion ? u.motion->desiredHeading() : 0};
    row.order = {u.order, u.orderState, u.orderTimer, u.mainTimer};
    if (u.path) {
      row.pathSource = {u.path->source.x, u.path->source.y};
      for (auto p : u.path->points)
        row.path.push_back({p.x, p.y});
    }
    result.push_back(std::move(row));
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  return result;
}
} // namespace maplab
