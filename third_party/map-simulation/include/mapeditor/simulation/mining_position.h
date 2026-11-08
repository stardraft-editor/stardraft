// SPDX-License-Identifier: MIT
// First-party position-discovery and lineup validation policies.
#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <mapeditor/simulation/mining_trial.h>
#include <mapeditor/simulation/search_compute.h>
#include <memory>
#include <set>
#include <vector>
namespace mapeditor::simulation::position_search {
using Cycles = MiningCycles;
struct Result {
  bool stable = false;
  Point position{};
  double frames = 0;
  uint64_t route = 0;
  bool miningStart = false;
};

// Only the half of the patch facing the worker's return depot is a boost spawn.
// A rear spawn can converge to a good front contact after a long first return,
// but that detour is not a useful starting position. Score the later front
// contact itself.
struct DepotApproach {
  Point mineral{}, depot{};
  bool valid = false;
  bool allows(Point p) const {
    int64_t dx = int64_t(depot.x) - mineral.x,
            dy = int64_t(depot.y) - mineral.y;
    return valid && (dx || dy) &&
           (int64_t(p.x) - mineral.x) * dx + (int64_t(p.y) - mineral.y) * dy >=
               0;
  }
};
inline DepotApproach depotApproach(const World &world, UnitHandle worker,
                                   UnitHandle field) {
  const auto *w = world.get(worker), *m = world.get(field);
  if (!w || !m || !w->visible || !m->visible || w->dead || m->dead)
    return {};
  const auto *depot = world.get(world.mineralDepot(worker));
  if (!depot || !depot->visible || depot->dead)
    return {};
  return {m->position, depot->position, true};
}
inline void relocate(World &world, UnitHandle worker, Point point) {
  world.relocateWorkerForSearch(worker, point);
}

// One worker and one patch in a private copy of the initial engine World. No
// live-World writes, resource refills, movement cheats, forced returns, or
// repeated orders during a trial. Explicit settings shared by the editor and
// repeatable headless experiments.
struct SearchConfig {
  int stride = 4;
  std::vector<int> gaps{1, 5};
  bool adaptive = true, record = false;
};
struct Observation {
  Result result;
  int frames;
  bool cached;
};
struct Search {
  std::unique_ptr<World> seed, trial;
  UnitHandle workerIndex, fieldIndex;
  std::vector<Point> candidates, perimeter;
  std::vector<Result> samples;
  bool refined = false;
  std::set<std::pair<int, int>> seen;
  Point miningPosition{};
  DepotApproach approach;
  size_t next = 0;
  int elapsed = 0, tested = 0, accepted = 0;
  Cycles cycle;
  SearchConfig config;
  std::vector<Observation> observations;
  Result best, baseline;
  std::vector<Result> options;
  static constexpr int kTrialFrames = 4096;

  Search(const World &initial, UnitHandle worker, UnitHandle field,
         SearchConfig cfg = {})
      : workerIndex(worker), fieldIndex(field), config(std::move(cfg)) {
    seed = std::make_unique<World>(initial);
    auto *w = seed->get(worker), *m = seed->get(field);
    if (!w || !m || !w->visible || !m->visible || w->dead || m->dead)
      throw std::invalid_argument(
          "Position discovery requires a worker and mineral");
    approach = depotApproach(*seed, worker, field);
    std::vector<UnitHandle> remove;
    for (const auto &slot : seed->units())
      if (slot && !slot->dead &&
          (seed->definitions().units[slot->type].flags & 8) &&
          slot->handle != worker)
        remove.push_back(slot->handle);
    for (auto h : remove)
      seed->remove(h);
    // Preserve the untouched control even if an unusual map spawns it behind
    // the patch. It is measured as baseline only; it cannot become a boost
    // option on the rear side.
    candidates.push_back(w->position);
    seen.emplace(w->position.x, w->position.y);
    const auto &mineralShape = seed->definitions().units[m->type].bounds;
    const auto &workerShape = seed->definitions().units[w->type].bounds;
    PixelBox bb{m->position.x - mineralShape.left,
                m->position.y - mineralShape.top,
                m->position.x + mineralShape.right,
                m->position.y + mineralShape.bottom};
    // All integer-pixel contact positions around the patch, expanded by the
    // worker's real collision dimensions. A second ring covers the
    // stopping-distance variation of BW movement.
    for (int gap : config.gaps) {
      int left = bb.left - workerShape.right - gap;
      int right = bb.right + workerShape.left + gap;
      int top = bb.top - workerShape.bottom - gap;
      int bottom = bb.bottom + workerShape.top + gap;
      auto add = [&](Point p) { addCandidate(p); };
      for (int x = left; x <= right; x += std::max(1, config.stride)) {
        add({x, top});
        add({x, bottom});
      }
      for (int y = top + 1; y < bottom; y += std::max(1, config.stride)) {
        add({left, y});
        add({right, y});
      }
      for (int x = left; x <= right; ++x) {
        perimeter.push_back({x, top});
        perimeter.push_back({x, bottom});
      }
      for (int y = top + 1; y < bottom; ++y) {
        perimeter.push_back({left, y});
        perimeter.push_back({right, y});
      }
    }
  }

  bool done() const { return refined && next >= candidates.size() && !trial; }
  void addCandidate(Point p) {
    if (!approach.allows(p))
      return;
    if (!seen.emplace(p.x, p.y).second)
      return;
    if (!seed->workerPositionBlocked(workerIndex, p))
      candidates.push_back(p);
  }
  void refine() {
    refined = true;
    if (!config.adaptive || config.stride <= 1)
      return;
    // Refine competitive route basins at pixel precision. A failed coarse
    // search gets the complete perimeter fallback; geometrically identical
    // routes never share scores.
    if (options.empty()) {
      for (Point p : perimeter)
        addCandidate(p);
      return;
    }
    size_t added = candidates.size();
    std::set<uint64_t> routes;
    for (const auto &r : options) {
      if (!routes.insert(r.route).second)
        continue;
      for (Point p : perimeter)
        if (std::max(std::abs(p.x - r.position.x),
                     std::abs(p.y - r.position.y)) <= config.stride)
          addCandidate(p);
      if (routes.size() == 4 || candidates.size() >= added + 64)
        break;
    }
    // A transition between coarse samples can hide a narrow faster basin.
    for (size_t i = 0; i < samples.size(); ++i)
      for (size_t j = i + 1; j < samples.size(); ++j) {
        const auto &a = samples[i], &b = samples[j];
        if (a.route == b.route ||
            std::min(a.frames, b.frames) > best.frames + 4 ||
            std::max(std::abs(a.position.x - b.position.x),
                     std::abs(a.position.y - b.position.y)) > config.stride)
          continue;
        for (Point p : perimeter)
          if (std::max(std::abs(p.x - a.position.x),
                       std::abs(p.y - a.position.y)) <= config.stride)
            addCandidate(p);
        if (candidates.size() >= added + 96)
          return;
      }
  }
  // Bounded chunks allow the editor to pump messages and paint/cancel between
  // trials.
  void advance(int budget) {
    while (budget > 0 && !done()) {
      if (!trial && next >= candidates.size()) {
        refine();
        if (next >= candidates.size())
          break;
      }
      if (!trial) {
        trial = std::make_unique<World>(*seed);
        relocate(*trial, workerIndex, candidates[next++]);
        trial->harvest(workerIndex, fieldIndex);
        cycle = {};
        elapsed = 0;
        miningPosition = {};
      }
      auto *w = trial->get(workerIndex), *m = trial->get(fieldIndex);
      if (!w || !m) {
        finish(false);
        continue;
      }
      int cargo = w->cargo;
      trial->advance();
      --budget;
      ++elapsed;
      if (!w->visible || w->dead || !m->visible || m->dead ||
          (trial->get(w->resourceTarget) && w->resourceTarget != fieldIndex)) {
        finish(false);
        continue;
      }
      cycle.sample(w->position, w->cargo);
      if (w->order == 87 && w->orderState == 5 &&
          !samePoint(w->position, miningPosition)) {
        miningPosition = w->position;
        // BW can stop between the two geometric rings. Test the actual contact
        // points reached by the engine too, including points reached from the
        // ordinary spawn.
        addCandidate(miningPosition);
      }
      if ((cargo & 2) && !(w->cargo & 2)) {
        cycle.delivery(trial->frame());
      }
      if ((cycle.stable && elapsed >= 1600) || elapsed >= kTrialFrames)
        finish(cycle.stable);
    }
  }
  void finish(bool stable) {
    Result r{stable, candidates[next - 1], cycle.frames, cycle.previous,
             samePoint(candidates[next - 1], miningPosition)};
    if (config.record)
      observations.push_back({r, elapsed, false});
    if (next == 1)
      baseline = r;
    if (stable) {
      if (approach.allows(r.position))
        samples.push_back(r);
      ++accepted;
      if (approach.allows(r.position) &&
          (!best.stable || r.frames < best.frames ||
           (r.frames == best.frames &&
            samePoint(best.position, candidates[0]) && next > 1)))
        best = r;
      if (next > 1) {
        // Preserve two representatives of each route: geometry alone does not
        // identify its arrival timing. Full-lineup trials decide which start
        // actually wins.
        auto same = options.end();
        int count = 0;
        for (auto it = options.begin(); it != options.end(); ++it)
          if (it->route == r.route) {
            ++count;
            same = it;
          }
        if (count < 2)
          same = options.end();
        if (same == options.end())
          options.push_back(r);
        else if (r.frames < same->frames ||
                 (r.frames == same->frames && r.miningStart))
          *same = r;
        std::stable_sort(options.begin(), options.end(),
                         [](const Result &a, const Result &b) {
                           if (a.frames != b.frames)
                             return a.frames < b.frames;
                           return a.miningStart > b.miningStart;
                         });
        if (options.size() > 16)
          options.resize(16);
      }
    }
    ++tested;
    trial.reset();
  }
};

using Assignment = MiningAssignment;
struct PositionMetric {
  Cycles cycles;
  int lastDelivery = -1;
  std::array<int, 3> trips{}, frames{};
  std::array<bool, 2> stable{};
  bool switched = false;
  double mean(int block = 0) const {
    return trips[block] ? double(frames[block]) / trips[block] : 0;
  }
};
struct PositionTrial {
  World world;
  std::vector<Assignment> assignments;
  std::vector<PositionMetric> metrics;
  std::vector<std::vector<Point>> contacts;
  int frame = 0, duration, warmup, split;
  PositionTrial(const World &source, const std::vector<Assignment> &a,
                const std::vector<Result> &positions, int length)
      : world(source), assignments(a), duration(length),
        warmup(length > 8192 ? 4096 : 1536), split((length + warmup) / 2) {
    metrics.resize(a.size());
    contacts.resize(a.size());
    for (std::size_t i = 0; i < a.size(); ++i)
      if (positions[i].stable) {
        if (!depotApproach(world, a[i].worker, a[i].field)
                 .allows(positions[i].position))
          metrics[i].switched = true;
        else
          relocate(world, a[i].worker, positions[i].position);
      }
    for (std::size_t i = 0; i < a.size(); ++i)
      if (positions[i].stable) {
        const auto *w = world.get(a[i].worker);
        if (!w || w->dead || !w->visible ||
            world.workerPositionBlocked(a[i].worker, w->position))
          metrics[i].switched = true;
      }
  }
  bool done() const { return frame >= duration; }
  void advance(int budget) {
    while (budget-- > 0 && !done()) {
      std::vector<int> cargo;
      for (auto a : assignments) {
        const auto *w = world.get(a.worker);
        cargo.push_back(w ? w->cargo : 0);
      }
      world.advance();
      ++frame;
      for (std::size_t i = 0; i < assignments.size(); ++i) {
        auto &m = metrics[i];
        const auto *w = world.get(assignments[i].worker),
                   *p = world.get(assignments[i].field);
        if (!w || !w->visible || w->dead || !p || !p->visible || p->dead) {
          m.switched = true;
          continue;
        }
        if (world.get(w->resourceTarget) &&
            w->resourceTarget != assignments[i].field)
          m.switched = true;
        m.cycles.sample(w->position, w->cargo);
        if (w->order == 87 && w->orderState == 5) {
          auto &v = contacts[i];
          if (v.size() < 8 && std::none_of(v.begin(), v.end(), [&](auto at) {
                return samePoint(at, w->position);
              }))
            v.push_back(w->position);
        }
        if ((cargo[i] & 2) && !(w->cargo & 2)) {
          m.cycles.delivery(frame);
          if (m.lastDelivery >= warmup) {
            ++m.trips[0];
            m.frames[0] += frame - m.lastDelivery;
          }
          int block = m.lastDelivery >= split ? 2 : 1;
          if (m.lastDelivery >= warmup && (block == 2 || frame <= split)) {
            ++m.trips[block];
            m.frames[block] += frame - m.lastDelivery;
          }
          m.lastDelivery = frame;
        }
        if (frame == split)
          m.stable[0] = m.cycles.stable;
        if (frame == duration)
          m.stable[1] = m.cycles.stable;
      }
    }
  }
};
inline double positionRate(const std::vector<PositionMetric> &metrics,
                           int block = 0) {
  double rate = 0;
  for (const auto &m : metrics)
    if (m.mean(block))
      rate += 8.0 * 1429 / m.mean(block);
  return rate;
}
struct Plan {
  using Output = Plan;
  std::unique_ptr<World> initial;
  std::vector<Assignment> assignments;
  std::vector<Result> positions;
  std::vector<std::vector<Result>> options;
  std::vector<size_t> choice;
  std::unique_ptr<Search> search;
  SearchConfig config;
  mining_compute::Executor *executor = nullptr;
  struct Discovery {
    Search search;
    Discovery(const World &st, Assignment a, SearchConfig cfg)
        : search(st, a.worker, a.field, cfg) {}
  };
  std::vector<std::shared_ptr<Discovery>> discoveries;
  bool denseExpanded = false;
  std::vector<std::shared_future<std::vector<Result>>> searches;
  std::vector<std::shared_future<std::vector<PositionMetric>>> checks;
  mining_compute::Memo<std::vector<PositionMetric>> memo;
  std::unique_ptr<PositionTrial> trial;
  std::vector<PositionMetric> control, selected, shortControl;
  mutable std::vector<PositionMetric> checkpointControl;
  mutable std::map<std::vector<int64_t>, std::vector<PositionMetric>> longCache;
  std::vector<double> normalFrames;
  size_t field = 0, worker = 0, candidate = 0;
  int phase = 0, pass = 0, rejected = 0;
  bool changed = false;
  static constexpr int kSearchFrames = 6144, kValidateFrames = 16384;

  Plan(const World &st, std::vector<Assignment> a,
       mining_compute::Executor *pool = nullptr, SearchConfig cfg = {})
      : assignments(std::move(a)), config(std::move(cfg)), executor(pool) {
    initial = std::make_unique<World>(st);
    positions.resize(assignments.size());
    options.resize(assignments.size());
    choice.resize(assignments.size());
    normalFrames.resize(assignments.size());
  }
  ~Plan() {
    for (auto &task : searches)
      if (task.valid())
        task.wait();
    for (auto &task : checks)
      if (task.valid())
        task.wait();
  }
  static std::vector<int64_t> lineupKey(const std::vector<Result> &v,
                                        int length = 0) {
    std::vector<int64_t> key{length};
    for (auto &r : v) {
      key.push_back(r.stable);
      key.push_back(r.stable ? r.position.x : 0);
      key.push_back(r.stable ? r.position.y : 0);
    }
    return key;
  }
  auto proposal() const { return lineupKey(positions); }
  double score() const { return positionRate(selected); }
  size_t selections() const {
    return std::count_if(positions.begin(), positions.end(),
                         [](const auto &r) { return r.stable; });
  }
  bool expand() {
    if (denseExpanded || !executor)
      return false;
    denseExpanded = true;
    for (auto d : discoveries) {
      auto &s = d->search;
      const auto *w = s.seed->get(s.workerIndex),
                 *m = s.seed->get(s.fieldIndex);
      const auto &workerShape = s.seed->definitions().units[w->type].bounds;
      const auto &mineralShape = s.seed->definitions().units[m->type].bounds;
      PixelBox bb{m->position.x - mineralShape.left,
                  m->position.y - mineralShape.top,
                  m->position.x + mineralShape.right,
                  m->position.y + mineralShape.bottom};
      // The fast pass uses two rings; continued search fills every integer gap
      // between them and retains every stable start for full-lineup testing,
      // not just the top 16.
      for (int gap = 1; gap <= 5; ++gap) {
        int left = bb.left - workerShape.right - gap;
        int right = bb.right + workerShape.left + gap;
        int top = bb.top - workerShape.bottom - gap;
        int bottom = bb.bottom + workerShape.top + gap;
        for (int x = left; x <= right; ++x) {
          s.addCandidate({x, top});
          s.addCandidate({x, bottom});
        }
        for (int y = top + 1; y < bottom; ++y) {
          s.addCandidate({left, y});
          s.addCandidate({right, y});
        }
      }
    }
    searches.clear();
    field = 0;
    worker = 0;
    pass = 0;
    changed = false;
    phase = 0;
    return true;
  }
  template <class Check> std::unique_ptr<Plan> checkpoint(Check check) const {
    auto out = std::make_unique<Plan>(*initial, assignments, nullptr, config);
    out->positions = positions;
    out->rejected = rejected;
    out->longCache = longCache;
    if (done()) {
      out->control = control;
      out->selected = selected;
      out->normalFrames = normalFrames;
      out->phase = 3;
      return out;
    }
    if (checkpointControl.empty()) {
      out->phase = 4;
      out->beginCheck(true, kValidateFrames);
      while (out->phase == 4) {
        check();
        out->advance(128);
      }
      checkpointControl = out->control;
      longCache = out->longCache;
    } else {
      out->control = checkpointControl;
      for (size_t i = 0; i < out->control.size(); ++i)
        out->normalFrames[i] = out->control[i].mean();
      out->phase = 5;
      out->beginCheck(false, kValidateFrames);
    }
    if (std::none_of(positions.begin(), positions.end(),
                     [](const auto &r) { return r.stable; })) {
      out->selected = out->control;
      out->phase = 3;
      out->trial.reset();
      return out;
    }
    while (!out->done()) {
      check();
      out->advance(128);
    }
    longCache = out->longCache;
    return out;
  }
  bool done() const { return phase == 3; }
  void beginCheck(bool normal, int length) {
    auto lineup = positions;
    if (normal)
      for (auto &r : lineup)
        r.stable = false;
    trial =
        std::make_unique<PositionTrial>(*initial, assignments, lineup, length);
  }
  void startWorker() {
    checks.clear();
    candidate = 0;
    while (worker < assignments.size() && options[worker].empty())
      ++worker;
    if (worker == assignments.size()) {
      if (changed && ++pass < 2) {
        worker = 0;
        changed = false;
        startWorker();
        return;
      }
      phase = 4;
      beginCheck(true, kValidateFrames);
      return;
    }
    if (executor)
      for (auto r : options[worker]) {
        auto lineup = positions;
        lineup[worker] = r;
        auto key = lineupKey(lineup, kSearchFrames);
        checks.push_back(memo.submit(
            *executor, std::move(key), [this, lineup = std::move(lineup)] {
              executor->check();
              PositionTrial t(*initial, assignments, lineup, kSearchFrames);
              while (!t.done()) {
                executor->check();
                t.advance(128);
              }
              return std::move(t.metrics);
            }));
      }
    else {
      auto lineup = positions;
      lineup[worker] = options[worker][0];
      trial = std::make_unique<PositionTrial>(*initial, assignments, lineup,
                                              kSearchFrames);
    }
  }
  bool valid(const std::vector<PositionMetric> &metrics, bool final) const {
    for (size_t i = 0; i < positions.size(); ++i)
      if (positions[i].stable || (!final && i == worker)) {
        auto &m = metrics[i];
        auto &base = control[i];
        if (m.switched || !m.cycles.stable || m.trips[0] < 8 || !base.mean() ||
            m.mean() > base.mean() + 0.1)
          return false;
        if (final)
          for (int b = 1; b <= 2; ++b)
            if (!m.stable[b - 1] || m.trips[b] < 8 || !base.mean(b) ||
                m.mean(b) > base.mean(b) + 0.1)
              return false;
      }
    return true;
  }
  void advance(int budget) {
    if (done())
      return;
    if (phase == 0) {
      if (executor && searches.empty()) {
        if (discoveries.empty())
          for (auto a : assignments)
            discoveries.push_back(
                std::make_shared<Discovery>(*initial, a, config));
        for (auto d : discoveries)
          searches.push_back(
              executor
                  ->submit([this, d] {
                    while (!d->search.done()) {
                      executor->check();
                      d->search.advance(128);
                    }
                    auto options = d->search.options;
                    if (denseExpanded)
                      for (auto r : d->search.samples)
                        if (!samePoint(r.position,
                                       d->search.candidates.front()) &&
                            std::none_of(options.begin(), options.end(),
                                         [&](const auto &old) {
                                           return samePoint(old.position,
                                                            r.position);
                                         }))
                          options.push_back(r);
                    std::stable_sort(options.begin(), options.end(),
                                     [](const auto &a, const auto &b) {
                                       return a.frames < b.frames;
                                     });
                    return options;
                  })
                  .share());
      }
      if (field == assignments.size()) {
        if (denseExpanded && !shortControl.empty()) {
          control = shortControl;
          auto it = memo.entries.find(lineupKey(positions, kSearchFrames));
          if (it != memo.entries.end()) {
            selected = it->second.get();
            phase = 2;
            startWorker();
          } else {
            phase = 6;
            trial = std::make_unique<PositionTrial>(*initial, assignments,
                                                    positions, kSearchFrames);
          }
        } else {
          phase = 1;
          beginCheck(true, kSearchFrames);
        }
        return;
      }
      if (executor) {
        if (searches[field].wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready)
          return;
        options[field] = searches[field].get();
        ++field;
        return;
      }
      if (!search)
        search = std::make_unique<Search>(*initial, assignments[field].worker,
                                          assignments[field].field, config);
      search->advance(budget);
      if (search->done()) {
        options[field++] = std::move(search->options);
        search.reset();
      }
      return;
    }
    std::vector<PositionMetric> metrics;
    if (phase == 2 && executor) {
      if (checks[candidate].wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready)
        return;
      metrics = checks[candidate].get();
    } else {
      auto lineup = positions;
      if (phase == 4)
        for (auto &r : lineup)
          r.stable = false;
      auto key = lineupKey(lineup, kValidateFrames);
      auto it = longCache.find(key);
      if ((phase == 4 || phase == 5) && it != longCache.end())
        metrics = it->second;
      else {
        trial->advance(budget);
        if (!trial->done())
          return;
        metrics = trial->metrics;
        if (phase == 4 || phase == 5)
          longCache.emplace(std::move(key), metrics);
      }
    }
    if (phase == 6) {
      selected = metrics;
      memo.remember(lineupKey(positions, kSearchFrames), metrics);
      phase = 2;
      startWorker();
      return;
    }
    if (phase == 1) {
      shortControl = control = selected = std::move(metrics);
      for (size_t i = 0; i < assignments.size(); ++i)
        for (Point p : trial->contacts[i])
          if (depotApproach(*initial, assignments[i].worker,
                            assignments[i].field)
                  .allows(p) &&
              !initial->workerPositionBlocked(assignments[i].worker, p) &&
              std::none_of(
                  options[i].begin(), options[i].end(),
                  [&](const Result &r) { return samePoint(r.position, p); }))
            options[i].push_back({true, p, control[i].mean(), 0, true});
      phase = 2;
      startWorker();
      return;
    }
    if (phase == 2) {
      double rate = positionRate(metrics), prior = positionRate(selected);
      bool good = valid(metrics, false) &&
                  (rate > prior + 0.02 ||
                   (!positions[worker].stable && rate >= prior &&
                    metrics[worker].mean() <= selected[worker].mean()));
      if (good) {
        positions[worker] = options[worker][candidate];
        positions[worker].frames = metrics[worker].mean();
        choice[worker] = candidate;
        selected = metrics;
        changed = true;
      } else
        ++rejected;
      if (++candidate == options[worker].size()) {
        ++worker;
        startWorker();
      } else if (!executor) {
        auto lineup = positions;
        lineup[worker] = options[worker][candidate];
        trial = std::make_unique<PositionTrial>(*initial, assignments, lineup,
                                                kSearchFrames);
      }
      return;
    }
    if (phase == 4) {
      control = std::move(metrics);
      for (size_t i = 0; i < control.size(); ++i)
        normalFrames[i] = control[i].mean();
      phase = 5;
      beginCheck(false, kValidateFrames);
      return;
    }
    // Final two disjoint late blocks must both retain each selected route and
    // its benefit.
    bool retry = false;
    for (size_t i = 0; i < positions.size(); ++i)
      if (positions[i].stable) {
        auto &m = metrics[i];
        auto &base = control[i];
        bool good = !m.switched && m.cycles.stable && base.mean() &&
                    m.mean() <= base.mean() + 0.1;
        for (int b = 1; b <= 2; ++b)
          good = good && m.stable[b - 1] && m.trips[b] >= 8 && base.mean(b) &&
                 m.mean(b) <= base.mean(b) + 0.1;
        if (!good) {
          positions[i].stable = false;
          ++rejected;
          retry = true;
        } else
          positions[i].frames = m.mean();
      }
    bool disrupted = false;
    for (size_t i = 0; i < metrics.size(); ++i)
      if (metrics[i].switched && !control[i].switched)
        disrupted = true;
    if (!retry)
      for (int b = 0; b <= 2; ++b)
        if (disrupted ||
            positionRate(metrics, b) + 0.01 < positionRate(control, b)) {
          // Remove the least useful choice first, preserving stronger verified
          // gains.
          size_t worst = positions.size();
          double loss = -1e9;
          for (size_t i = 0; i < positions.size(); ++i)
            if (positions[i].stable &&
                metrics[i].mean() - control[i].mean() > loss) {
              worst = i;
              loss = metrics[i].mean() - control[i].mean();
            }
          if (worst < positions.size()) {
            positions[worst].stable = false;
            ++rejected;
            retry = true;
          }
          break;
        }
    if (retry)
      beginCheck(false, kValidateFrames);
    else {
      selected = std::move(metrics);
      trial.reset();
      phase = 3;
    }
  }
};

} // namespace mapeditor::simulation::position_search
