// SPDX-License-Identifier: MIT
#include <iostream>
#include <mapeditor/simulation/creep.h>
using namespace mapeditor::simulation;
void check(bool value) {
  if (!value)
    throw std::runtime_error("Creep test failed");
}
int main() {
  try {
    std::vector<std::uint8_t> cv(104), vf(32), occupied(4096);
    cv[2] = 128;
    for (int i = 0; i < 16; ++i)
      vf[i * 2] = 1;
    std::vector<std::uint16_t> tiles(4096, 16);
    Terrain terrain(64, 64, tiles, cv, vf);
    Creep creep(terrain);
    Random random;
    creep.spread(terrain, {1024, 1008}, {128, 96}, true, occupied, random);
    int count = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        auto dx = std::int64_t(x * 32 - 1024 + 16),
             dy = std::int64_t(y * 32 - 1008 + 16);
        bool expected = dx * dx * 25 + dy * dy * 64 <= 320 * 320 * 25;
        check(creep.at(x, y) == expected);
        count += expected;
      }
    Random expected;
    for (int i = 12; i < count; ++i)
      expected.next();
    check(random.state() == expected.state());
    auto copy = creep;
    creep.spread(terrain, {1024, 1008}, {128, 96}, true, occupied, random);
    check(random.state() == expected.state() && creep.cells() == copy.cells());
    // A periodic tick grows exactly one tile, and a blocked frontier remains
    // eligible for later retries without consuming randomness.
    auto oneMissing = creep.cells();
    oneMissing[31 * 64 + 36] = 0;
    occupied[31 * 64 + 36] = 1;
    Creep periodic(terrain, oneMissing);
    auto beforeGrowth = random.state();
    auto blocked =
        periodic.grow(terrain, {1024, 1008}, {128, 96}, true, occupied, random);
    check(!blocked.grew && blocked.occupied && random.state() == beforeGrowth);
    occupied[31 * 64 + 36] = 0;
    auto grew =
        periodic.grow(terrain, {1024, 1008}, {128, 96}, true, occupied, random);
    check(grew.grew && !grew.occupied && periodic.cells() == creep.cells());
    auto complete =
        periodic.grow(terrain, {1024, 1008}, {128, 96}, true, occupied, random);
    check(!complete.grew && !complete.occupied);
    for (int y = 0; y < 64; ++y)
      tiles[y * 64 + 33] = 0;
    Terrain barrier(64, 64, tiles, cv, vf);
    Creep limited(barrier);
    limited.spread(barrier, {960, 1008}, {128, 96}, true, occupied, random);
    check(limited.at(32, 31) && !limited.at(33, 31) && !limited.at(34, 31));
    check(!limited.at(-1, 31) && !limited.at(64, 31));
    Creep groundCover(terrain);
    std::vector<std::uint8_t> neighboringBuildings(4096);
    neighboringBuildings[29 * 64 + 29] = 1;
    groundCover.establish(terrain, {1024, 1008}, {128, 96}, true,
                          neighboringBuildings);
    int covered = 0;
    for (auto cell : groundCover.cells())
      covered += cell;
    check(covered == 35 && !groundCover.at(29, 29) && !groundCover.at(28, 29) &&
          !groundCover.at(35, 33) && groundCover.at(28, 30) &&
          groundCover.at(34, 29));
    cv[54] = 64;
    Terrain seeded(64, 64, std::vector<std::uint16_t>(4096, 16), cv, vf);
    Creep inherited(seeded);
    check(inherited.at(1, 1) && !inherited.at(1, 63));
    std::cout << "PASS: creep ellipse, deterministic growth, blocked islands "
                 "and initial terrain flags\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
