// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <mapeditor/simulation/mining_control.h>
#include <stdexcept>
#include <utility>
#include <vector>
namespace mapeditor::simulation {
// Map Editor's own scoring policy, independent of either simulation backend.
struct MiningCycles {
  std::uint64_t hash = 1469598103934665603ULL, previous = 0;
  Point last{};
  int lastCargo = -1, lastFrame = -1, repeats = 0, minFrames = 0, maxFrames = 0;
  int totalFrames = 0, consecutiveRepeats = 0, period = 0, completed = 0;
  static constexpr int kMaxPeriod = 8, kHistory = kMaxPeriod * 3;
  std::array<std::uint64_t, kHistory> routes{};
  std::array<int, kHistory> durations{};
  bool started = false, stable = false;
  double frames = 0;
  void sample(Point position, int cargo) {
    cargo &= 2;
    if (samePoint(position, last) && cargo == lastCargo)
      return;
    last = position;
    lastCargo = cargo;
    for (int v : {last.x, last.y, cargo}) {
      hash ^= (std::uint32_t)v;
      hash *= 1099511628211ULL;
    }
  }
  void delivery(int now) {
    if (started) {
      int dt = now - lastFrame;
      if (hash == previous) {
        ++consecutiveRepeats;
        minFrames = std::min(minFrames, dt);
        maxFrames = std::max(maxFrames, dt);
        totalFrames += dt;
      } else {
        consecutiveRepeats = 1;
        minFrames = maxFrames = totalFrames = dt;
      }
      previous = hash;
      repeats = consecutiveRepeats;
      stable = repeats >= 3;
      period = stable ? 1 : 0;
      frames = double(totalFrames) / consecutiveRepeats;
      routes[completed % kHistory] = hash;
      durations[completed % kHistory] = dt;
      ++completed;
      // BW's order phases and other workers can produce a repeating supercycle,
      // e.g. A,A,B,C routes taking 147,147,150,156 frames. Require three
      // complete copies of the exact geometric sequence, not merely a low
      // average time.
      for (int p = 2; !stable && p <= kMaxPeriod && completed >= 3 * p; ++p) {
        auto route = [&](int back) {
          return routes[(completed - 1 - back) % kHistory];
        };
        bool matches = true;
        for (int j = 0; j < 2 * p; ++j)
          if (route(j) != route(j + p)) {
            matches = false;
            break;
          }
        if (!matches)
          continue;
        int sum = 0;
        minFrames = INT_MAX;
        maxFrames = 0;
        for (int j = 0; j < 3 * p; ++j) {
          int n = durations[(completed - 1 - j) % kHistory];
          sum += n;
          minFrames = std::min(minFrames, n);
          maxFrames = std::max(maxFrames, n);
        }
        stable = true;
        period = p;
        repeats = 3;
        frames = double(sum) / (3 * p);
      }
    }
    started = true;
    lastFrame = now;
    hash = 1469598103934665603ULL;
    lastCargo = -1;
  }
};

struct MiningAssignment {
  UnitHandle worker, field;
};
struct MiningBlock {
  int trips = 0, frames = 0, commandedTrips = 0;
  double distance = 0;
  bool stable = false;
  double mean() const { return trips ? double(frames) / trips : 0; }
  double path() const { return trips ? distance / trips : 0; }
};
struct MiningMetric {
  MiningCycles cycles;
  std::array<MiningBlock, 2> blocks;
  int lastDelivery = -1, trips = 0, frames = 0, commandedTrips = 0,
      tripCommands = 0;
  double distance = 0, tripDistance = 0;
  bool switched = false;
  double mean() const { return trips ? double(frames) / trips : 0; }
  double path() const { return trips ? distance / trips : 0; }
};
struct MiningTrial {
  World world;
  std::vector<MiningAssignment> assignments;
  std::vector<MiningControl> rules, background;
  std::vector<MiningMetric> metrics;
  std::vector<std::vector<Point>> spots;
  std::vector<int> cargoSince, returnFrames;
  int frame = 0, duration, warmup, split;
  bool discover, cycleSearch, earlyReject;
  static constexpr int kWarmup = 1536;
  MiningTrial(const World &initial, const std::vector<MiningAssignment> &a,
              std::vector<MiningControl> r,
              const std::vector<MiningControl> &other, int frames,
              bool collect = false, bool cycle = false, int warm = kWarmup,
              bool rejectEarly = false)
      : world(initial), assignments(a), rules(std::move(r)), background(other),
        duration(frames), warmup(warm),
        split(int((std::int64_t(frames) + warm) / 2)), discover(collect),
        cycleSearch(cycle), earlyReject(rejectEarly) {
    if (frames < 0 || warm < 0 || rules.size() > a.size())
      throw std::invalid_argument("Invalid mining trial configuration");
    metrics.resize(a.size());
    spots.resize(a.size());
    cargoSince.assign(a.size(), -1);
    returnFrames.resize(a.size());
  }
  bool done() const { return frame >= duration; }
  double rate() const {
    double result = 0;
    for (const auto &m : metrics)
      if (m.mean())
        result += 8.0 * 1429 / m.mean();
    return result;
  }
  void advance(int budget) {
    if (budget < 0)
      throw std::invalid_argument("Negative mining trial frame budget");
    while (budget-- > 0 && !done()) {
      for (auto &r : background)
        r.apply(world);
      for (std::size_t i = 0; i < rules.size(); ++i)
        if (rules[i].apply(world))
          ++metrics[i].tripCommands;
      std::vector<int> cargo(assignments.size());
      std::vector<Point> positions(assignments.size());
      for (std::size_t i = 0; i < assignments.size(); ++i) {
        const auto *worker = world.get(assignments[i].worker);
        if (!worker || worker->dead)
          continue;
        cargo[i] = worker->cargo;
        positions[i] = worker->position;
        if ((cargo[i] & 3) != 2)
          cargoSince[i] = -1;
        else {
          if (cargoSince[i] < 0)
            cargoSince[i] = frame;
          if (discover && frame >= warmup)
            returnFrames[i] = std::max(returnFrames[i], frame - cargoSince[i]);
        }
        if (discover && frame >= warmup && (cargo[i] & 3) == 2 &&
            worker->order == 90 && worker->orderState == 1 && worker->motion &&
            worker->motion->speed() > 0) {
          auto &v = spots[i];
          if (v.size() < 512 && std::none_of(v.begin(), v.end(), [&](Point p) {
                return samePoint(p, worker->position);
              }))
            v.push_back(worker->position);
        }
      }
      world.advance();
      ++frame;
      bool impossible = false;
      for (std::size_t i = 0; i < assignments.size(); ++i) {
        auto &m = metrics[i];
        const auto *worker = world.get(assignments[i].worker);
        const auto *field = world.get(assignments[i].field);
        if (!worker || worker->dead || !worker->visible || !field ||
            field->dead) {
          m.switched = true;
          continue;
        }
        if (world.get(worker->resourceTarget) &&
            worker->resourceTarget != assignments[i].field) {
          bool detouring = i < rules.size() && rules[i].cycle &&
                           rules[i].enabled && !(worker->cargo & 3) &&
                           worker->resourceTarget == rules[i].neighbor &&
                           worker->order != 86 && worker->order != 87;
          if (!detouring)
            m.switched = true;
        }
        if (i < rules.size() && rules[i].failed)
          m.switched = true;
        if (cycleSearch ? !(cargo[i] & 3) : (cargo[i] & 2)) {
          double dx = worker->position.x - positions[i].x,
                 dy = worker->position.y - positions[i].y;
          m.tripDistance += std::sqrt(dx * dx + dy * dy);
        }
        m.cycles.sample(worker->position, worker->cargo);
        if ((cargo[i] & 2) && !(worker->cargo & 2)) {
          m.cycles.delivery(frame);
          if (m.lastDelivery >= warmup) {
            ++m.trips;
            m.frames += frame - m.lastDelivery;
            m.distance += m.tripDistance;
            if (m.tripCommands == (cycleSearch ? 2 : 1))
              ++m.commandedTrips;
            if (earlyReject && i < rules.size() && rules[i].enabled &&
                m.tripCommands != (cycleSearch ? 2 : 1))
              impossible = true;
            int block = m.lastDelivery >= split ? 1 : 0;
            if (block == 1 || frame <= split) {
              auto &b = m.blocks[block];
              ++b.trips;
              b.frames += frame - m.lastDelivery;
              b.distance += m.tripDistance;
              if (m.tripCommands == (cycleSearch ? 2 : 1))
                ++b.commandedTrips;
            }
          }
          m.lastDelivery = frame;
          m.tripDistance = 0;
          m.tripCommands = 0;
        }
        if (frame == split)
          m.blocks[0].stable = m.cycles.stable;
        if (frame == duration)
          m.blocks[1].stable = m.cycles.stable;
        if (earlyReject && i < rules.size() && rules[i].enabled && m.switched)
          impossible = true;
      }
      if (earlyReject && cycleSearch)
        for (const auto &r : rules)
          if (r.failed)
            impossible = true;
      if (impossible)
        duration = frame;
    }
  }
};
} // namespace mapeditor::simulation
