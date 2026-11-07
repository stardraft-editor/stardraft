// SPDX-License-Identifier: MIT
// Map Editor first-party search selection using independent world trials.
#pragma once
#include "search_compute.h"
#include <mapeditor/simulation/mining_trial.h>
#include <set>
#include <tuple>
namespace mapeditor::simulation::mining_search {
using state = simulation::World;
using Assignment = MiningAssignment;
using Rule = MiningControl;
using Trial = MiningTrial;
using Metric = MiningMetric;
inline int pixelDistance(Point a, Point b) {
  return distance({a.x - b.x, a.y - b.y});
}
inline Rule makeRule(UnitHandle worker, UnitHandle field, Point spot,
                     bool enabled) {
  Rule r;
  r.worker = worker;
  r.field = field;
  r.spot = spot;
  r.enabled = enabled;
  return r;
}
struct SearchConfig {
  int cargoStride = 4;
  bool adaptive = true, earlyReject = true, pickupTiming = true;
  std::vector<int> delays{0, 4, 8, 12, 20, 28};
  std::vector<int> detours{2, 4, 8, 12, 20, 28};
};
struct Plan {
  using Output = Plan;
  std::unique_ptr<state> initial;
  std::vector<Assignment> assignments;
  std::vector<Rule> chosen, background;
  std::vector<std::vector<Rule>> candidates, allCandidates;
  std::vector<std::vector<std::pair<double, Rule>>> promising;
  std::vector<bool> refined, dense;
  std::vector<Rule> anchors;
  std::vector<std::size_t> tested;
  std::vector<Rule> rescueOptions;
  double rescueRate = 0;
  bool polishing = false;
  std::size_t batchStart = 0;
  bool cycleSearch = false;
  SearchConfig config;
  std::vector<Metric> baseline, selected;
  std::unique_ptr<Trial> trial;
  mining_compute::Executor *executor = nullptr;
  std::vector<std::shared_future<std::vector<Metric>>> searches;
  mining_compute::Memo<std::vector<Metric>> memo;
  std::vector<Metric> shortBaseline;
  mutable std::vector<Metric> checkpointControl;
  mutable std::map<std::vector<std::int64_t>, std::vector<Metric>> longCache;
  bool exhaustive = false;
  std::size_t batchEnd = 0;
  std::vector<std::size_t> exhaustiveCursor;
  std::size_t batchWorker = std::size_t(-1);
  std::size_t worker = 0, candidate = 0;
  int phase = 0, rejected = 0;
  static constexpr int kSearchFrames = 6144, kCheckFrames = 16384;
  double bestRate = 0;

  Plan(const state &st, std::vector<Assignment> a, std::vector<Rule> other = {},
       mining_compute::Executor *pool = nullptr, bool cycle = false,
       SearchConfig cfg = {})
      : assignments(std::move(a)), background(std::move(other)),
        cycleSearch(cycle), config(std::move(cfg)), executor(pool) {
    initial = std::make_unique<state>(st);
    for (const auto &v : assignments)
      chosen.push_back({v.worker, v.field});
    trial = std::make_unique<Trial>(*initial, assignments, chosen, background,
                                    kSearchFrames, true, cycleSearch);
  }
  ~Plan() {
    for (auto &task : searches)
      if (task.valid())
        task.wait();
  }
  static std::vector<std::int64_t> ruleKey(const std::vector<Rule> &v,
                                           int length = 0) {
    std::vector<std::int64_t> key{length};
    for (auto &r : v) {
      key.push_back(r.enabled);
      if (r.enabled) {
        key.push_back(r.worker.index);
        key.push_back(r.field.index);
        key.push_back(r.cycle);
        key.push_back(r.spot.x);
        key.push_back(r.spot.y);
        key.push_back(r.neighbor.index);
        key.push_back(r.delay);
        key.push_back(r.detour);
        key.push_back(r.cargoDelay);
      }
    }
    return key;
  }
  auto proposal() const { return ruleKey(chosen); }
  std::size_t selections() const {
    return std::count_if(chosen.begin(), chosen.end(),
                         [](const auto &r) { return r.enabled; });
  }
  double score() const {
    double n = 0;
    for (auto &m : selected)
      if (m.mean())
        n += 8.0 * 1429 / m.mean();
    return n;
  }
  bool expand() {
    if (exhaustive)
      return false;
    exhaustive = true;
    candidates = allCandidates;
    if (cycleSearch) {
      const auto &world = *initial;
      for (std::size_t i = 0; i < assignments.size(); ++i) {
        auto *m = world.get(assignments[i].field);
        std::vector<std::size_t> neighbors;
        for (int id : world.visibleUnits()) {
          const auto *n = &*world.units()[id];
          if (n != m && n->type >= 176 && n->type <= 178 &&
              pixelDistance(n->position, m->position) <= 160)
            neighbors.push_back(id);
        }
        std::sort(neighbors.begin(), neighbors.end());
        // Both commands precede the 75-frame mining action. Even with zero
        // remaining travel, delays consuming the control's entire transport
        // time cannot improve it.
        int bound = std::max(1, int(std::ceil(shortBaseline[i].mean())) - 75);
        std::set<std::tuple<std::size_t, int, int>> added;
        for (int step : {16, 8, 4, 2, 1})
          for (int delay = 0; delay < bound; delay += step)
            for (int detour = 1; detour < bound - delay; detour += step)
              for (auto n : neighbors) {
                if (!added.emplace(n, delay, detour).second)
                  continue;
                auto r = makeRule(assignments[i].worker, assignments[i].field,
                                  {}, true);
                r.cycle = true;
                r.neighbor = world.units()[n]->handle;
                r.delay = delay;
                r.detour = detour;
                candidates[i].push_back(r);
              }
      }
    }
    if (candidates.empty())
      return false;
    exhaustiveCursor.assign(assignments.size(), 0);
    baseline = shortBaseline;
    worker = 0;
    candidate = 0;
    polishing = true;
    batchWorker = std::size_t(-1);
    refined.assign(assignments.size(), true);
    dense.assign(assignments.size(), true);
    auto it = memo.entries.find(ruleKey(chosen, kSearchFrames));
    if (it != memo.entries.end()) {
      selected = it->second.get();
      bestRate = score();
      phase = 1;
      trial = std::make_unique<Trial>(*initial, assignments, chosen, background,
                                      kSearchFrames, false, cycleSearch,
                                      Trial::kWarmup, config.earlyReject);
      startCandidate();
    } else {
      phase = 6;
      trial = std::make_unique<Trial>(*initial, assignments, chosen, background,
                                      kSearchFrames, false, cycleSearch,
                                      Trial::kWarmup, config.earlyReject);
    }
    return true;
  }
  template <class Check> std::unique_ptr<Plan> checkpoint(Check check) const {
    auto cfg = config;
    cfg.adaptive = false;
    auto out = std::make_unique<Plan>(*initial, assignments, background,
                                      nullptr, cycleSearch, cfg);
    out->chosen = chosen;
    out->rejected = rejected;
    out->longCache = longCache;
    if (done()) {
      out->baseline = baseline;
      out->selected = selected;
      out->phase = 4;
      out->trial.reset();
      return out;
    }
    if (checkpointControl.empty()) {
      auto none = chosen;
      for (auto &r : none)
        r.enabled = false;
      out->phase = 2;
      out->trial =
          std::make_unique<Trial>(*initial, assignments, none, background,
                                  kCheckFrames, false, cycleSearch, 4096);
      while (out->phase == 2) {
        check();
        out->advance(128);
      }
      checkpointControl = out->baseline;
      longCache = out->longCache;
    } else {
      out->baseline = checkpointControl;
      out->bestRate = 0;
      for (auto &m : out->baseline)
        if (m.mean())
          out->bestRate += 8.0 * 1429 / m.mean();
      out->phase = 3;
      out->trial =
          std::make_unique<Trial>(*initial, assignments, chosen, background,
                                  kCheckFrames, false, cycleSearch, 4096);
    }
    if (std::none_of(chosen.begin(), chosen.end(),
                     [](const auto &r) { return r.enabled; })) {
      out->selected = out->baseline;
      out->phase = 4;
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
  bool done() const { return phase == 4; }
  static bool improves(const Metric &m, const Metric &control,
                       bool final = false) {
    if (final)
      for (std::size_t i = 0; i < m.blocks.size(); ++i) {
        const auto &b = m.blocks[i], &c = control.blocks[i];
        if (!b.stable || b.trips < 8 || b.commandedTrips != b.trips ||
            !c.mean() || b.mean() >= c.mean() - 0.1)
          return false;
      }
    return !m.switched && m.trips >= 8 && m.commandedTrips == m.trips &&
           m.cycles.stable && control.mean() > 0 &&
           m.mean() < control.mean() - 0.5;
  }
  static bool same(const Rule &a, const Rule &b) {
    return samePoint(a.spot, b.spot) && a.cycle == b.cycle &&
           a.neighbor == b.neighbor && a.delay == b.delay &&
           a.detour == b.detour && a.cargoDelay == b.cargoDelay;
  }
  void append(std::size_t i, Rule r) {
    auto &v = candidates[i];
    if (std::none_of(v.begin(), v.end(),
                     [&](const Rule &old) { return same(old, r); }))
      v.push_back(r);
  }
  void refineWorker() {
    refined[worker] = true;
    anchors[worker] = chosen[worker];
    if (!config.adaptive)
      return;
    if (!cycleSearch && promising[worker].empty()) {
      // A coarse miss gets dense coverage rather than silently losing a narrow
      // spot.
      for (auto r : allCandidates[worker])
        append(worker, r);
      dense[worker] = true;
    }
    std::size_t added = candidates[worker].size();
    for (const auto &p : promising[worker]) {
      auto r = p.second;
      if (cycleSearch) {
        for (int d : {0, -1, 1, -2, 2})
          for (int t : {0, -1, 1, -2, 2}) {
            auto n = r;
            n.delay += d;
            n.detour += t;
            if (n.delay >= 0 && n.delay <= 30 && n.detour >= 1 &&
                n.detour <= 30)
              append(worker, n);
            if (candidates[worker].size() >= added + 40)
              return;
          }
      } else {
        auto &all = allCandidates[worker];
        for (std::size_t i = 0; i < all.size(); ++i)
          if (same(all[i], r))
            for (int j = std::max(0, int(i) - config.cargoStride + 1);
                 j < int(all.size()) && j <= int(i) + config.cargoStride - 1;
                 ++j)
              append(worker, all[j]);
      }
    }
  }
  void startCandidate() {
    while (worker < assignments.size()) {
      if (baseline[worker].switched) {
        ++worker;
        candidate = 0;
        continue;
      }
      if (candidate < candidates[worker].size())
        break;
      if (!refined[worker]) {
        refineWorker();
        batchWorker = std::size_t(-1);
        if (candidate < candidates[worker].size())
          break;
      }
      if (!cycleSearch && config.adaptive && !polishing && !dense[worker]) {
        dense[worker] = true;
        batchWorker = std::size_t(-1);
        // Competitive coarse spots do not prove there is no better narrow
        // basin.
        for (auto r : allCandidates[worker])
          append(worker, r);
        if (candidate < candidates[worker].size())
          break;
      }
      ++worker;
      candidate = 0;
    }
    if (worker == assignments.size()) {
      if (config.adaptive && !polishing) {
        // Revisit the best alternatives after later workers changed their
        // choices.
        polishing = true;
        worker = 0;
        candidate = 0;
        batchWorker = std::size_t(-1);
        for (std::size_t i = 0; i < candidates.size(); ++i) {
          candidates[i].clear();
          if (chosen[i].enabled)
            append(i, chosen[i]);
          for (auto p : promising[i])
            append(i, p.second);
        }
        startCandidate();
        return;
      }
      phase = 2;
      std::vector<Rule> none = chosen;
      for (auto &r : none)
        r.enabled = false;
      trial = std::make_unique<Trial>(*initial, assignments, none, background,
                                      kCheckFrames, false, cycleSearch, 4096);
      return;
    }
    if (executor) {
      if (batchWorker != worker) {
        searches.clear();
        batchWorker = worker;
        batchStart = candidate;
        // Earlier workers are fixed. The current worker's rule is replaced in
        // every candidate, so these trials are independent. Consume in original
        // order below to preserve the serial greedy selection and tie breaking
        // exactly.
        batchEnd = std::min(candidates[worker].size(), candidate + 32);
        for (std::size_t j = candidate; j < batchEnd; ++j) {
          auto rule = candidates[worker][j];
          auto r = chosen;
          r[worker] = rule;
          auto key = ruleKey(r, kSearchFrames);
          searches.push_back(
              memo.submit(*executor, std::move(key), [this, r = std::move(r)] {
                executor->check();

                Trial t(*initial, assignments, r, background, kSearchFrames,
                        false, cycleSearch, Trial::kWarmup, config.earlyReject);
                while (!t.done()) {
                  executor->check();
                  t.advance(128);
                }
                return std::move(t.metrics);
              }));
        }
      }
      return;
    }
    auto r = chosen;
    r[worker] = candidates[worker][candidate];
    trial = std::make_unique<Trial>(
        *initial, assignments, std::move(r), background, kSearchFrames, false,
        cycleSearch, Trial::kWarmup, config.earlyReject);
  }
  bool sustained(const std::vector<Metric> &metrics) const {
    for (std::size_t i = 0; i < metrics.size(); ++i)
      if (metrics[i].switched && !baseline[i].switched)
        return false;
    for (std::size_t b = 0; b < 2; ++b) {
      double normal = 0, boosted = 0;
      for (std::size_t i = 0; i < baseline.size(); ++i) {
        if (baseline[i].blocks[b].mean())
          normal += 8.0 * 1429 / baseline[i].blocks[b].mean();
        if (metrics[i].blocks[b].mean())
          boosted += 8.0 * 1429 / metrics[i].blocks[b].mean();
      }
      if (boosted < normal + 0.01)
        return false;
    }
    return true;
  }
  void startRescue() {
    searches.clear();
    candidate = 0;
    rescueOptions.clear();
    if (config.adaptive)
      while (worker < assignments.size()) {
        if (!chosen[worker].enabled) {
          auto add = [&](Rule r) {
            if (!r.enabled ||
                std::any_of(rescueOptions.begin(), rescueOptions.end(),
                            [&](const Rule &old) { return same(old, r); }))
              return;
            rescueOptions.push_back(r);
          };
          add(anchors[worker]);
          for (auto p : promising[worker])
            add(p.second);
          if (!rescueOptions.empty())
            break;
        }
        ++worker;
      }
    if (!config.adaptive || worker == assignments.size()) {
      trial.reset();
      phase = 4;
      return;
    }
    phase = 5;
    if (executor)
      for (auto rule : rescueOptions) {
        auto rules = chosen;
        rules[worker] = rule;
        auto key = ruleKey(rules, kCheckFrames);
        auto cached = longCache.find(key);
        if (cached != longCache.end()) {
          searches.push_back(
              memo.submit(*executor, std::move(key),
                          [metrics = cached->second] { return metrics; }));
          continue;
        }
        searches.push_back(memo.submit(
            *executor, std::move(key), [this, rules = std::move(rules)] {
              executor->check();
              Trial t(*initial, assignments, rules, background, kCheckFrames,
                      false, cycleSearch, 4096);
              while (!t.done()) {
                executor->check();
                t.advance(128);
              }
              return std::move(t.metrics);
            }));
      }
    else {
      auto rules = chosen;
      rules[worker] = rescueOptions[0];
      trial = std::make_unique<Trial>(*initial, assignments, rules, background,
                                      kCheckFrames, false, cycleSearch, 4096);
    }
  }
  void advance(int budget) {
    if (done())
      return;
    if (executor && (phase == 1 || phase == 5)) {
      auto &task = searches[phase == 1 ? candidate - batchStart : candidate];
      if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      trial->metrics = task.get();
    } else {
      auto rules = chosen;
      if (phase == 2)
        for (auto &r : rules)
          r.enabled = false;
      if (phase == 5 && !executor)
        rules[worker] = rescueOptions[candidate];
      auto key = ruleKey(rules, kCheckFrames);
      auto it = longCache.find(key);
      if ((phase == 2 || phase == 3 || phase == 5) && it != longCache.end())
        trial->metrics = it->second;
      else {
        trial->advance(budget);
        if (!trial->done())
          return;
        if (phase == 2 || phase == 3 || phase == 5)
          longCache.emplace(std::move(key), trial->metrics);
      }
    }
    if (phase == 6) {
      selected = trial->metrics;
      memo.remember(ruleKey(chosen, kSearchFrames), selected);
      bestRate = trial->rate();
      phase = 1;
      startCandidate();
      return;
    }
    if (phase == 5) {
      ++tested[worker];
      bool good = improves(trial->metrics[worker], baseline[worker], true) &&
                  trial->rate() > rescueRate + 0.01 &&
                  sustained(trial->metrics);
      for (std::size_t i = 0; good && i < chosen.size(); ++i)
        if (chosen[i].enabled && i != worker &&
            !improves(trial->metrics[i], baseline[i], true))
          good = false;
      if (good) {
        chosen[worker] = rescueOptions[candidate];
        selected = trial->metrics;
        rescueRate = trial->rate();
      }
      if (++candidate == rescueOptions.size()) {
        ++worker;
        startRescue();
      } else if (!executor) {
        auto rules = chosen;
        rules[worker] = rescueOptions[candidate];
        trial =
            std::make_unique<Trial>(*initial, assignments, rules, background,
                                    kCheckFrames, false, cycleSearch, 4096);
      }
      return;
    }
    if (phase == 0) {
      shortBaseline = baseline = selected = trial->metrics;
      bestRate = trial->rate();
      memo.remember(ruleKey(chosen, kSearchFrames), baseline);
      candidates.resize(assignments.size());
      allCandidates.resize(assignments.size());
      promising.resize(assignments.size());
      refined.assign(assignments.size(), false);
      dense.assign(assignments.size(), false);
      anchors.resize(assignments.size());
      tested.resize(assignments.size());
      const auto &world = *initial;
      for (std::size_t i = 0; i < assignments.size(); ++i) {
        auto a = assignments[i];
        if (!cycleSearch) {
          for (auto spot : trial->spots[i])
            allCandidates[i].push_back(makeRule(a.worker, a.field, spot, true));
          for (std::size_t j = 0; j < allCandidates[i].size();
               j += std::max(1, config.cargoStride))
            append(i, allCandidates[i][j]);
          if (!allCandidates[i].empty())
            append(i, allCandidates[i].back());
          // Cover every integer pickup-relative frame of the observed return
          // leg. Seed these independently of the spatial stride so early narrow
          // timing basins are not shifted by the number of observed
          // control-route pixels.
          for (int delay = 0;
               config.pickupTiming && delay <= trial->returnFrames[i];
               ++delay) {
            auto r = makeRule(a.worker, a.field, {}, true);
            r.cargoDelay = delay;
            allCandidates[i].push_back(r);
            if (delay % std::max(1, config.cargoStride) == 0 ||
                delay == trial->returnFrames[i])
              append(i, r);
          }
          continue;
        }
        auto *original = world.get(a.field);
        if (!original || !original->visible || original->dead)
          continue;
        std::vector<std::size_t> neighbors;
        for (int id : world.visibleUnits()) {
          const auto *m = &*world.units()[id];
          if (m != original && m->type >= 176 && m->type <= 178 &&
              pixelDistance(m->position, original->position) <= 160)
            neighbors.push_back(id);
        }
        std::sort(neighbors.begin(), neighbors.end(),
                  [&](std::size_t x, std::size_t y) {
                    int dx = pixelDistance(world.units()[x]->position,
                                           original->position);
                    int dy = pixelDistance(world.units()[y]->position,
                                           original->position);
                    return dx != dy ? dx < dy : x < y;
                  });
        if (neighbors.size() > 3)
          neighbors.resize(3);
        for (std::size_t n : neighbors)
          for (int delay : config.delays)
            for (int detour : config.detours) {
              auto r = makeRule(a.worker, a.field, {}, true);
              r.cycle = true;
              r.neighbor = world.units()[n]->handle;
              r.delay = delay;
              r.detour = detour;
              candidates[i].push_back(r);
            }
      }
      phase = 1;
      startCandidate();
      return;
    }
    if (phase == 1) {
      ++tested[worker];
      const auto &m = trial->metrics[worker];
      bool viable = true;
      for (std::size_t i = 0; i < chosen.size(); ++i)
        if (chosen[i].enabled || i == worker)
          if (trial->metrics[i].switched ||
              trial->metrics[i].commandedTrips != trial->metrics[i].trips)
            viable = false;
      // A truncated invalid group cannot seed refinement from partial timing
      // samples.
      if (!polishing && viable && !m.switched && m.trips >= 8 &&
          m.commandedTrips == m.trips && m.cycles.stable &&
          m.mean() < baseline[worker].mean() + (cycleSearch ? 0.5 : 1.0)) {
        auto &v = promising[worker];
        v.push_back({trial->rate(), candidates[worker][candidate]});
        std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) {
          return a.first > b.first;
        });
        if (v.size() > 2)
          v.resize(2);
      }
      bool good =
          improves(trial->metrics[worker], baseline[worker]) &&
          (!chosen[worker].enabled ||
           trial->metrics[worker].mean() < selected[worker].mean() - 0.05) &&
          trial->rate() > bestRate + 0.01;
      for (std::size_t i = 0; good && i < chosen.size(); ++i)
        if (chosen[i].enabled && i != worker &&
            !improves(trial->metrics[i], baseline[i]))
          good = false;
      if (good) {
        chosen[worker] = candidates[worker][candidate];
        selected = trial->metrics;
        bestRate = trial->rate();
      }
      ++candidate;
      if (exhaustive) {
        exhaustiveCursor[worker] = candidate;
        if (candidate >= batchEnd || candidate >= candidates[worker].size()) {
          std::size_t next = assignments.size();
          for (std::size_t offset = 1; offset <= assignments.size(); ++offset) {
            std::size_t j = (worker + offset) % assignments.size();
            if (!baseline[j].switched &&
                exhaustiveCursor[j] < candidates[j].size()) {
              next = j;
              break;
            }
          }
          worker = next;
          candidate = next < assignments.size() ? exhaustiveCursor[next] : 0;
          batchWorker = std::size_t(-1);
        }
      } else if (executor && candidate >= batchEnd)
        batchWorker = std::size_t(-1);
      startCandidate();
      return;
    }
    if (phase == 2) {
      baseline = trial->metrics;
      bestRate = trial->rate();
      phase = 3;
      trial = std::make_unique<Trial>(*initial, assignments, chosen, background,
                                      kCheckFrames, false, cycleSearch, 4096);
      return;
    }
    bool retry = false;
    for (std::size_t i = 0; i < chosen.size(); ++i)
      if (chosen[i].enabled &&
          !improves(trial->metrics[i], baseline[i], true)) {
        chosen[i].enabled = false;
        ++rejected;
        retry = true;
      }
    if (!retry &&
        (trial->rate() < bestRate + 0.01 || !sustained(trial->metrics))) {
      for (auto &r : chosen)
        r.enabled = false;
      selected = baseline;
      rescueRate = bestRate;
      worker = 0;
      startRescue();
      return;
    }
    if (retry)
      trial = std::make_unique<Trial>(*initial, assignments, chosen, background,
                                      kCheckFrames, false, cycleSearch, 4096);
    else {
      selected = trial->metrics;
      rescueRate = trial->rate();
      worker = 0;
      startRescue();
    }
  }
};

} // namespace mapeditor::simulation::mining_search
