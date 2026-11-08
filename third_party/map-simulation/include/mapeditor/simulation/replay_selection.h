// SPDX-License-Identifier: MIT
#pragma once
#include "replay_commands.h"
#include "replay_identity.h"
#include <functional>

namespace mapeditor::simulation::replay {
struct SelectionItem {
  UnitHandle handle;
  int owner = 0;
  bool visible = false, multi = false;
};
// Eligibility is supplied by match state, rather than guessed from wire IDs.
// Selection stores full native handles; groups retain classic cyclic IDs.
class ClassicSelection {
public:
  using Lookup = std::function<std::optional<SelectionItem>(std::uint16_t)>;
  bool apply(int owner, const ClassicCommand &, const Lookup &);
  const std::vector<UnitHandle> &selection(int owner) const;
  const std::vector<std::uint16_t> &group(int owner, int index) const;
  // Match lifecycle must notify this object when a unit is deselected/removed.
  // Group IDs intentionally survive removal and are checked on recall.
  void deselect(UnitHandle);

private:
  std::array<std::vector<UnitHandle>, 8> selected_;
  std::array<std::array<std::vector<std::uint16_t>, 10>, 8> groups_;
};
bool classicMultiSelectionType(int type, const dat::Unit &);

// Current native state eligibility, including static special unit categories.
// Dynamic disabling/status effects remain part of the unfinished match model.
ClassicSelection::Lookup selectionLookup(const World &);
} // namespace mapeditor::simulation::replay
