// SPDX-License-Identifier: MIT
#include <map>
#include <mapeditor/simulation/replay_selection.h>
#include <source_location>
#include <string>
using namespace mapeditor::simulation;
using namespace mapeditor::simulation::replay;
namespace {
void check(bool value,
           std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Selection test failed at line " +
                             std::to_string(where.line()));
}
ClassicCommand select(ClassicOpcode code,
                      std::initializer_list<UnitHandle> units) {
  SelectionCommand payload;
  for (auto handle : units)
    payload.units[payload.count++] = ClassicUnitId::fromHandle(handle).raw();
  return {0, code, 0, 0, payload};
}
ClassicCommand group(int operation, int index = 0) {
  return {0, ClassicOpcode::ControlGroup, 0, 0,
          ControlGroupCommand{std::uint8_t(operation), std::uint8_t(index)}};
}
} // namespace
void testReplaySelection() {
  const UnitHandle worker{1, 33}, otherWorker{2, 1}, building{3, 1},
      enemy{4, 1}, hidden{5, 1};
  std::map<std::uint16_t, SelectionItem> pool;
  auto add = [&](UnitHandle handle, int owner, bool visible, bool multi) {
    pool[ClassicUnitId::fromHandle(handle).raw()] = {handle, owner, visible,
                                                     multi};
  };
  add(worker, 0, true, true);
  add(otherWorker, 0, true, true);
  add(building, 0, true, false);
  add(enemy, 1, true, true);
  add(hidden, 0, false, true);
  ClassicSelection::Lookup lookup =
      [&](std::uint16_t id) -> std::optional<SelectionItem> {
    auto it = pool.find(id);
    return it == pool.end() ? std::nullopt : std::optional(it->second);
  };
  ClassicSelection state;
  {
    ClassicSelection lost;
    lost.apply(0, select(ClassicOpcode::Select, {worker}), lookup);
    lost.apply(0, group(0, 4), lookup);
    lost.apply(0, select(ClassicOpcode::Select, {building}), lookup);
    pool.erase(ClassicUnitId::fromHandle(worker).raw());
    lost.apply(0, group(1, 4), lookup);
    check(lost.selection(0).empty() && lost.group(0, 4).empty());
    add(worker, 0, true, true);
  }
  auto apply = [&](ClassicCommand command) {
    return state.apply(0, command, lookup);
  };
  apply(select(ClassicOpcode::Select,
               {worker, worker, building, hidden, otherWorker}));
  check(state.selection(0) == std::vector<UnitHandle>{worker, otherWorker});
  apply(select(ClassicOpcode::Select, {building, worker, otherWorker}));
  apply(group(0));
  check(state.group(0, 0).size() == 3);
  apply(select(ClassicOpcode::Select, {}));
  apply(group(1));
  check(state.selection(0) == std::vector<UnitHandle>{otherWorker, worker});
  check(state.group(0, 0) ==
        std::vector<std::uint16_t>{ClassicUnitId::fromHandle(otherWorker).raw(),
                                   ClassicUnitId::fromHandle(worker).raw()});
  apply(select(ClassicOpcode::Select, {worker, enemy, otherWorker}));
  apply(group(0));
  check(state.group(0, 0).size() == 1);
  apply(select(ClassicOpcode::Select, {enemy, worker}));
  apply(group(0));
  check(state.group(0, 0).empty());
  auto before = state.selection(0);
  check(!apply(group(1)));
  check(state.selection(0) == before);
  for (int index = 10; index < 256; ++index) {
    check(!apply(group(255, index)));
    check(state.selection(0) == before);
  }
  apply(select(ClassicOpcode::Select, {building}));
  apply(group(0));
  apply(select(ClassicOpcode::Select, {worker}));
  check(!apply(group(2)));
  check(state.group(0, 0).size() == 1);
  // Full handles in selection do not become a newly allocated generation;
  // groups retain short cyclic IDs and resolve the current live generation.
  apply(select(ClassicOpcode::Select, {worker, otherWorker}));
  apply(group(0));
  state.deselect(worker);
  check(state.selection(0) == std::vector<UnitHandle>{otherWorker});
  pool.erase(ClassicUnitId::fromHandle(worker).raw());
  apply(group(1));
  check(state.group(0, 0).size() == 1);
  apply(select(ClassicOpcode::Select, {otherWorker}));
  apply(group(0));
  state.deselect(otherWorker);
  pool.erase(ClassicUnitId::fromHandle(otherWorker).raw());
  UnitHandle reused{otherWorker.index, 33};
  add(reused, 0, true, true);
  apply(group(1));
  check(state.selection(0) == std::vector<UnitHandle>{reused});
  auto copied = state;
  state.deselect(reused);
  check(copied.selection(0).size() == 1 && state.selection(0).empty());
  // Shift-selection checks the wire count before filtering duplicates.
  apply(select(ClassicOpcode::Select, {worker, building}));
  SelectionCommand duplicates;
  duplicates.count = 12;
  duplicates.units.fill(ClassicUnitId::fromHandle(building).raw());
  before = state.selection(0);
  check(!apply({0, ClassicOpcode::AddSelection, 0, 0, duplicates}));
  check(state.selection(0) == before);
  // Independent owner state and stable deselection order.
  state.apply(1, select(ClassicOpcode::Select, {enemy, reused}), lookup);
  state.deselect(reused);
  check(state.selection(1) == std::vector<UnitHandle>{enemy});
  bool threw = false;
  try {
    apply(group(3));
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  check(threw);
  threw = false;
  try {
    apply({0, ClassicOpcode::Stop, 0, 0, ByteCommand{}});
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  check(threw);
}
