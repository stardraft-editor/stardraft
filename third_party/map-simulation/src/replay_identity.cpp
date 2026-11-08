// SPDX-License-Identifier: MIT
#include <mapeditor/simulation/replay_identity.h>

namespace mapeditor::simulation::replay {
ClassicActors::ClassicActors(const Metadata &metadata) {
  for (std::size_t i = 0; i < ids_.size(); ++i)
    ids_[i] = metadata.players[i].id;
}
std::optional<int> ClassicActors::resolve(std::uint8_t actor) const noexcept {
  for (std::size_t i = 0; i < ids_.size(); ++i)
    if (ids_[i] == std::uint32_t(actor))
      return int(i);
  return std::nullopt;
}
ClassicUnitId ClassicUnitId::fromHandle(UnitHandle handle) {
  if (handle.index == -1)
    return ClassicUnitId{};
  if (handle.index < 0 || handle.index >= 2047)
    throw std::out_of_range("Native unit slot does not fit a classic wire ID");
  return ClassicUnitId(std::uint16_t(2048 * (handle.generation % 32) +
                                     unsigned(handle.index + 1)));
}
UnitHandle ClassicUnitId::resolve(const World &world) const noexcept {
  const auto &pool = world.units();
  auto index = slot();
  if (index < 0 || std::size_t(index) >= pool.size())
    return {};
  const auto &unit = pool[std::size_t(index)];
  if (!unit || unit->dead || unit->handle.generation % 32 != generationTag())
    return {};
  return unit->handle;
}
} // namespace mapeditor::simulation::replay
