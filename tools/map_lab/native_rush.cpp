// SPDX-License-Identifier: MIT
#include "native_rush.h"
#include "native_world.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <sstream>
namespace maplab {
namespace sim = mapeditor::simulation;
struct NativeRush::Impl {
  std::unique_ptr<sim::World> seed;
  struct Runner {
    std::unique_ptr<sim::World> world;
    sim::UnitHandle worker;
    RushPath path;
    int stalled = 0;
  };
  RushPoint start, end;
  int type = 7, interval = 48, retained = 10, frame = 0, nextSpawn = 0,
      serial = 0;
  std::vector<Runner> active;
  std::deque<RushPath> completed;
  void spawn() {
    if (active.size() >= 32)
      return;
    Runner r;
    r.world = std::make_unique<sim::World>(*seed);
    r.worker = r.world->createFinished(type, {start.x, start.y}, 0);
    const auto *unit = r.world->get(r.worker);
    if (!unit || !unit->visible || unit->dead ||
        std::abs(unit->position.x - start.x) > 8 ||
        std::abs(unit->position.y - start.y) > 8)
      throw std::runtime_error(
          "The rush starting point is blocked. Choose an open point.");
    r.path.id = ++serial;
    r.path.type = type;
    r.path.points.push_back({unit->position.x, unit->position.y});
    r.world->move(r.worker, {end.x, end.y});
    active.push_back(std::move(r));
  }
};
NativeRush::NativeRush() = default;
NativeRush::~NativeRush() = default;
std::shared_ptr<NativeRush> NativeRush::create(const FileReader &read,
                                               const Bytes &scenario,
                                               RushPoint start, RushPoint end,
                                               int race) {
  auto terrain = mapeditor::simulation::Scenario::read(scenario).terrain();
  if (race < 0 || race > 2 || start.x < 0 || start.y < 0 || end.x < 0 ||
      end.y < 0 || start.x >= terrain.width * 32 ||
      start.y >= terrain.height * 32 || end.x >= terrain.width * 32 ||
      end.y >= terrain.height * 32)
    throw std::runtime_error("Choose two points inside the map.");
  auto result = std::shared_ptr<NativeRush>(new NativeRush);
  auto &i = *(result->impl_ = std::make_unique<Impl>());
  i.seed = loadNativeWorld(read, scenario);
  i.type = race == 0 ? 41 : race == 1 ? 7 : 64;
  i.start = start;
  i.end = end;
  i.spawn();
  i.nextSpawn = i.interval;
  return result;
}
void NativeRush::configure(double interval, int retained) {
  auto &i = *impl_;
  if (!std::isfinite(interval))
    throw std::runtime_error("Invalid rush interval");
  i.interval = std::clamp(int(std::round(interval / .042)), 12, 1429);
  i.retained = std::clamp(retained, 1, 50);
  i.nextSpawn = i.frame + i.interval;
  while (int(i.completed.size()) > i.retained)
    i.completed.pop_front();
}
void NativeRush::advance(int frames) {
  auto &i = *impl_;
  for (int n = 0; n < std::clamp(frames, 0, 240); ++n) {
    if (i.frame >= i.nextSpawn) {
      i.spawn();
      i.nextSpawn = i.frame + i.interval;
    }
    for (auto it = i.active.begin(); it != i.active.end();) {
      auto &r = *it;
      r.world->advance();
      ++r.path.frames;
      const auto *unit = r.world->get(r.worker);
      if (!unit || !unit->visible || unit->dead)
        r.path.finished = true;
      else {
        auto last = r.path.points.back();
        RushPoint point{unit->position.x, unit->position.y};
        if (last.x != point.x || last.y != point.y) {
          r.path.points.push_back(point);
          r.stalled = 0;
        } else
          ++r.stalled;
        r.path.reached = std::hypot(double(point.x - i.end.x),
                                    double(point.y - i.end.y)) <= 1;
        r.path.finished =
            r.path.reached || r.stalled >= 480 || r.path.frames >= 24000;
      }
      if (r.path.finished) {
        r.path.sprites.clear();
        i.completed.push_back(std::move(r.path));
        while (int(i.completed.size()) > i.retained)
          i.completed.pop_front();
        it = i.active.erase(it);
      } else
        ++it;
    }
    ++i.frame;
  }
}
RushFrame NativeRush::snapshot() const {
  const auto &i = *impl_;
  RushFrame frame{i.start, i.end, {}};
  for (const auto &p : i.completed)
    frame.paths.push_back(p);
  for (const auto &r : i.active) {
    auto path = r.path;
    std::array<sim::UnitHandle, 1> selected{r.worker};
    for (const auto &image : r.world->renderUnits(selected))
      if (image.modifier == 0 || image.modifier == 1 || image.modifier == 10)
        path.sprites.push_back({image.position.x, image.position.y, image.image,
                                image.frame, image.owner, image.flip,
                                image.modifier == 10});
    frame.paths.push_back(std::move(path));
  }
  return frame;
}
std::vector<RushDebugRunner> NativeRush::debugRunners() const {
  std::vector<RushDebugRunner> result;
  for (const auto &runner : impl_->active) {
    const auto &world = *runner.world;
    const auto &u = *world.get(runner.worker);
    MiningDebugUnit row;
    row.id = runner.worker.index;
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
                  world.get(u.target) ? u.target.index : -1};
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
    result.push_back({runner.path.id, runner.path.frames, world.randomState(),
                      std::move(row)});
  }
  return result;
}
std::string NativeRush::debugState() const {
  std::ostringstream out;
  if (impl_->active.empty())
    return "No active runner";
  const auto &runner = impl_->active.front();
  const auto &world = *runner.world;
  const auto *u = world.get(runner.worker);
  if (!u)
    return "Missing runner";
  out << "Native frame " << world.frame() << " random " << world.randomState()
      << " unit " << u->handle.index << " position " << u->position.x << ','
      << u->position.y;
  if (u->motion) {
    auto raw = u->motion->rawPosition(), v = u->motion->velocity();
    out << " raw " << raw.x << ',' << raw.y << " velocity " << v.x << ',' << v.y
        << " speed " << u->motion->speed() << ',' << u->motion->nextSpeed()
        << " heading " << u->motion->facing() << ','
        << u->motion->travelHeading() << ',' << u->motion->desiredHeading();
  }
  const auto &image = u->sprite.images[u->sprite.main];
  out << " order " << u->order << ',' << u->orderState << ',' << u->orderTimer
      << " image " << image.type << ',' << image.frame() << ','
      << image.cursor.wait << ',' << image.cursor.pc;
  if (u->motion) {
    auto goal = u->motion->waypoint();
    out << " waypoint " << goal.x << ',' << goal.y << " motion "
        << u->motion->planning() << ',' << u->motion->arrived() << ','
        << u->motion->settling() << ',' << u->motion->replanning();
  }
  if (u->path) {
    out << " source " << u->path->source.x << ',' << u->path->source.y;
    out << " path";
    for (auto p : u->path->points)
      out << ' ' << p.x << ',' << p.y;
  }
  if (u->path) {
    out << " leg " << u->path->nodes << ',' << u->path->peakFrontier
        << " region " << world.map().regions.regionAt(u->position)
        << " truncated " << u->path->itineraryTruncated << " destination "
        << u->path->destination.x << ',' << u->path->destination.y
        << " itinerary";
    for (auto r : u->path->itinerary) {
      auto center = world.map().regions.entries()[r].center;
      out << ' ' << r << ':' << center.x << ',' << center.y;
    }
  }
  for (const auto &entry : world.units())
    if (entry && entry->order == 166) {
      const auto &critter = *entry;
      out << " critter " << critter.handle.index << ':' << critter.position.x
          << ',' << critter.position.y << ':' << critter.orderState << ','
          << critter.mainTimer << ':' << critter.moveTargetPosition.x << ','
          << critter.moveTargetPosition.y;
      if (critter.path) {
        out << " source " << critter.path->source.x << ','
            << critter.path->source.y;
        for (auto p : critter.path->points)
          out << '/' << p.x << ',' << p.y;
      }
      out << " type=" << critter.type;
      if (critter.motion)
        out << " moving=" << critter.motion->moving()
            << " debt=" << critter.motion->collisionDebt();
    }
  return out.str();
}

} // namespace maplab
