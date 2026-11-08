// SPDX-License-Identifier: MIT
#include <limits>
#include <mapeditor/simulation/economy.h>
#include <stdexcept>
using namespace mapeditor::simulation;
void testEconomy() {
  auto check = [](bool value) {
    if (!value)
      throw std::runtime_error("Economy test failed");
  };
  Economy state;
  state.set(0, {50, 25});
  check(!state.spend(0, {51, 0}) &&
        state.resources(0) == PlayerResources{50, 25});
  check(!state.spend(0, {49, 26}) &&
        state.resources(0) == PlayerResources{50, 25});
  check(state.spend(0, {50, 25}) && state.resources(0) == PlayerResources{});
  state.credit(0, {8, 0});
  state.credit(0, {8, 7});
  check(state.resources(0) == PlayerResources{16, 7});
  auto copy = state;
  copy.credit(0, {3, 4});
  check(state.resources(0) == PlayerResources{16, 7} &&
        copy.resources(0) == PlayerResources{19, 11});
  state.set(11, {std::numeric_limits<std::int32_t>::max(), 0});
  state.credit(11, {1, -1});
  check(state.resources(11) ==
        PlayerResources{std::numeric_limits<std::int32_t>::min(), -1});
  state.credit(11, {-1, 1});
  check(state.resources(11) ==
        PlayerResources{std::numeric_limits<std::int32_t>::max(), 0});
  bool rejected = false;
  try {
    state.spend(0, {-1, 0});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected && state.resources(0) == PlayerResources{16, 7});
  for (int player : {-1, 12}) {
    rejected = false;
    try {
      state.credit(player, {1, 1});
    } catch (const std::out_of_range &) {
      rejected = true;
    }
    check(rejected);
  }
}
