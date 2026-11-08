// SPDX-License-Identifier: MIT
#pragma once
#include "dat.h"
#include <algorithm>
#include <bit>
#include <cstdint>

namespace mapeditor::simulation {
// Raw signed 24.8 values. Stored shields/energy exist even on unit types that
// do not display or use those attributes. No damage/regen or upgrade rules yet.
struct UnitVitals {
  std::int32_t hitpoints = 0, shields = 0, energy = 0;
  bool operator==(const UnitVitals &) const = default;
};
inline UnitVitals finishedUnitVitals(int type, const dat::Unit &definition) {
  if (type < 0 || type >= 228)
    throw std::out_of_range("Invalid unit type for vitals");
  const int baseEnergyLimit = (definition.flags & 0x40) ? 250 : 200;
  return {std::max(std::bit_cast<std::int32_t>(definition.hitpoints), 1),
          definition.shieldPoints * 256,
          type == 172 ? 100 * 256 : baseEnergyLimit * 256 / 4};
}
} // namespace mapeditor::simulation
