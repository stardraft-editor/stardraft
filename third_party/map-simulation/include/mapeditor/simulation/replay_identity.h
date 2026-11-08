// SPDX-License-Identifier: MIT
#pragma once
#include "replay.h"
#include "world.h"

namespace mapeditor::simulation::replay {
// A recorded actor is an ID, not the player's position in the header. Retain
// full-width header IDs so unused sentinels and values >255 cannot alias
// actors. Duplicate IDs resolve to their first slot, including observer
// slots 8..11.
class ClassicActors {
public:
  explicit ClassicActors(const Metadata &);
  std::optional<int> resolve(std::uint8_t actor) const noexcept;
  const std::array<std::uint32_t, 12> &ids() const noexcept { return ids_; }

private:
  std::array<std::uint32_t, 12> ids_{};
};

// Classic wire IDs have an 11-bit one-based slot and a five-bit cyclic tag.
// Native handles retain their full generation; resolution must return that
// current handle rather than constructing a native handle from the short tag.
class ClassicUnitId {
public:
  explicit ClassicUnitId(std::uint16_t raw = 0) : raw_(raw) {}
  static ClassicUnitId fromHandle(UnitHandle);
  std::uint16_t raw() const noexcept { return raw_; }
  int slot() const noexcept { return int(raw_ % 2048) - 1; }
  std::uint8_t generationTag() const noexcept {
    return std::uint8_t(raw_ / 2048);
  }
  bool empty() const noexcept { return slot() < 0; }
  // Dead, released, out-of-pool and mismatched-tag IDs resolve to a null
  // handle. Hidden/live units remain addressable. No owner/selection policy is
  // applied.
  UnitHandle resolve(const World &) const noexcept;

private:
  std::uint16_t raw_ = 0;
};
} // namespace mapeditor::simulation::replay
