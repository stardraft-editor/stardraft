// SPDX-License-Identifier: MIT
#include <algorithm>
#include <mapeditor/simulation/replay_selection.h>

namespace mapeditor::simulation::replay {
bool classicMultiSelectionType(int type, const dat::Unit &data) {
  if ((data.flags & 0x801) != 0)
    return false;
  // Protocol unit categories that permit only a single selection: traps,
  // critters, mines, eggs and the two persistent area-spell units.
  if ((type >= 203 && type <= 213) || (type >= 93 && type <= 96))
    return false;
  switch (type) {
  case 13:
  case 36:
  case 89:
  case 90:
  case 105:
  case 202:
    return false;
  default:
    return true;
  }
}
ClassicSelection::Lookup selectionLookup(const World &world) {
  return [&world](std::uint16_t raw) -> std::optional<SelectionItem> {
    auto handle = ClassicUnitId(raw).resolve(world);
    const auto *unit = world.get(handle);
    if (!unit)
      return {};
    return SelectionItem{
        handle, unit->owner, unit->visible,
        classicMultiSelectionType(unit->type,
                                  world.definitions().units.at(unit->type))};
  };
}
const std::vector<UnitHandle> &ClassicSelection::selection(int owner) const {
  return selected_.at(std::size_t(owner));
}
const std::vector<std::uint16_t> &ClassicSelection::group(int owner,
                                                          int index) const {
  return groups_.at(std::size_t(owner)).at(std::size_t(index));
}
void ClassicSelection::deselect(UnitHandle unit) {
  for (auto &selection : selected_)
    std::erase(selection, unit);
}
bool ClassicSelection::apply(int owner, const ClassicCommand &command,
                             const Lookup &lookup) {
  auto &selected = selected_.at(std::size_t(owner));
  auto append = [&](const SelectionItem &item) {
    if (item.visible && (selected.empty() || item.multi) &&
        std::find(selected.begin(), selected.end(), item.handle) ==
            selected.end()) {
      if (selected.size() == 12)
        throw std::length_error("Classic selection exceeds twelve units");
      selected.push_back(item.handle);
    }
  };
  switch (command.code) {
  case ClassicOpcode::Select:
  case ClassicOpcode::AddSelection:
  case ClassicOpcode::RemoveSelection: {
    const auto &input = std::get<SelectionCommand>(command.payload);
    if (input.count > 12)
      throw std::length_error("Classic selection command exceeds twelve units");
    if (command.code == ClassicOpcode::AddSelection &&
        selected.size() + input.count > 12)
      return false;
    if (command.code == ClassicOpcode::Select)
      selected.clear();
    for (auto raw : std::span(input.units).first(input.count)) {
      if (auto item = lookup(raw)) {
        if (command.code == ClassicOpcode::RemoveSelection)
          std::erase(selected, item->handle);
        else
          append(*item);
      }
    }
    return true;
  }
  case ClassicOpcode::ControlGroup: {
    const auto &input = std::get<ControlGroupCommand>(command.payload);
    if (input.group >= 10)
      return false;
    auto &saved = groups_.at(std::size_t(owner)).at(input.group);
    if (input.operation > 2)
      throw std::invalid_argument("Unknown classic control group operation");
    if (input.operation == 1) {
      if (saved.empty())
        return false;
      selected.clear();
      for (std::size_t i = 0; i < saved.size();) {
        auto item = lookup(saved[i]);
        if (!item || item->owner != owner || !item->visible ||
            (saved.size() > 1 && !item->multi)) {
          saved[i] = saved.back();
          saved.pop_back();
        } else
          ++i;
      }
      if (saved.empty())
        return false;
      for (auto raw : saved)
        if (auto item = lookup(raw))
          selected.push_back(item->handle);
    } else {
      if (input.operation == 0)
        saved.clear();
      if (input.operation == 2 && !saved.empty()) {
        auto firstSaved = lookup(saved.front());
        if (firstSaved && !firstSaved->multi)
          return false;
      }
      if (!selected.empty()) {
        auto first = lookup(ClassicUnitId::fromHandle(selected.front()).raw());
        if (first && first->handle == selected.front() &&
            first->owner == owner) {
          for (auto handle : selected) {
            auto raw = ClassicUnitId::fromHandle(handle).raw();
            auto item = lookup(raw);
            if (!item || item->handle != handle || item->owner != owner)
              break;
            if ((input.operation == 0 || saved.empty() || item->multi) &&
                std::find(saved.begin(), saved.end(), raw) == saved.end() &&
                saved.size() < 12)
              saved.push_back(raw);
          }
        }
      }
    }
    return true;
  }
  default:
    throw std::invalid_argument("Not a classic selection command");
  }
}
} // namespace mapeditor::simulation::replay
