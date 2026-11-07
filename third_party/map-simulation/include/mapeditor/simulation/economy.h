// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace mapeditor::simulation {
struct PlayerResources {
  std::int32_t minerals = 0, gas = 0;
  bool operator==(const PlayerResources &) const = default;
};
// Classic balances retain signed 32-bit storage, including modular credits.
// Production is responsible for deciding when an accepted order pays/refunds.
class Economy {
public:
  const PlayerResources &resources(int player) const {
    return balances_.at(std::size_t(player));
  }
  void set(int player, PlayerResources value) {
    balances_.at(std::size_t(player)) = value;
  }
  void credit(int player, PlayerResources amount) {
    auto &balance = balances_.at(std::size_t(player));
    balance = {add(balance.minerals, amount.minerals),
               add(balance.gas, amount.gas)};
  }
  bool spend(int player, PlayerResources cost) {
    if (cost.minerals < 0 || cost.gas < 0)
      throw std::invalid_argument("Negative production cost");
    auto &balance = balances_.at(std::size_t(player));
    if (balance.minerals < cost.minerals || balance.gas < cost.gas)
      return false;
    balance.minerals -= cost.minerals;
    balance.gas -= cost.gas;
    return true;
  }

private:
  static std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
  }
  std::array<PlayerResources, 12> balances_{};
};
struct Population {
  std::array<int, 228> units{}, completed{};
  // Raw half-supply units, ordered Zerg/Terran/Protoss. Available totals are
  // uncapped; the production admission rule must separately enforce 200 supply.
  std::array<int, 3> used{}, available{};
};
} // namespace mapeditor::simulation
