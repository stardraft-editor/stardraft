// SPDX-License-Identifier: MIT
#pragma once
#include "mining.h"
#include "native_world.h"
#include <mapeditor/simulation/mining_position.h>
#include <mapeditor/simulation/mining_search.h>
#include <mapeditor/simulation/search_job.h>
#include <mapeditor/simulation/world.h>

namespace maplab {
// Editor mining playback and searches use independent simulation worlds.
class NativeMining {
public:
  static std::shared_ptr<NativeMining> create(const FileReader &read,
                                              const Bytes &scenario, int x,
                                              int y, int race, int workers = 0);
  void advance(int frames);
  void search(int technique, double seconds);
  bool searching() const;
  MiningFrame snapshot() const;
  std::vector<MiningDebugUnit> debugUnits(bool all = false) const;
  const mapeditor::simulation::World &world() const { return *world_; }

private:
  using Handle = mapeditor::simulation::UnitHandle;
  using Control = mapeditor::simulation::MiningControl;
  using SearchPlan = mapeditor::simulation::mining_search::Plan;
  using SearchJob = mapeditor::simulation::mining_compute::Job<SearchPlan>;
  using PositionPlan = mapeditor::simulation::position_search::Plan;
  using PositionJob = mapeditor::simulation::mining_compute::Job<PositionPlan>;
  struct Assignment {
    Handle worker, patch;
    int last = -1, cycle = 0, deliveries = 0;
  };
  std::unique_ptr<mapeditor::simulation::World> world_;
  std::unique_ptr<mapeditor::simulation::World> searchSeed_;
  Handle depot_;
  std::vector<Assignment> assignments_;
  std::vector<Handle> displayed_;
  std::vector<Control> cargo_, cycle_;
  std::vector<MiningGuide> fixedGuides_;
  std::unique_ptr<PositionJob> positionJob_;
  std::array<std::unique_ptr<SearchJob>, 3> jobs_;
  std::array<SearchStatus, 3> statuses_{};
  int start_ = 0;
  void poll();
  void resetStats();
};
} // namespace maplab
