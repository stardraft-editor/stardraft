// SPDX-License-Identifier: MIT
#include <iostream>
#include <limits>
#include <mapeditor/simulation/dat.h>
#include <vector>
using namespace mapeditor::simulation;
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Malformed DAT was accepted");
}
void put(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v, int n) {
  for (int i = 0; i < n; ++i)
    b.at(at + i) = std::uint8_t(v >> (8 * i));
}
int main() {
  try {
    {
      std::vector<std::uint8_t> bytes(4158);
      for (int id = 0; id < 189; ++id) {
        bytes[1323 + id] = id % 2 ? 255 : 0;
        bytes[1701 + id] = id % 3 ? 0 : 1;
        bytes[2268 + id] = id % 5 ? 7 : 0;
      }
      auto values = dat::orders(bytes);
      for (int id = 0; id < 189; ++id)
        check(values[id].interruptible == bool(id % 2) &&
                  values[id].queueable == (id % 3 == 0) &&
                  values[id].fleeOnHit == bool(id % 5),
              "Order admission and damage-response columns");
      bytes.pop_back();
      rejects([&] { (void)dat::orders(bytes); });
      bytes.resize(4159);
      rejects([&] { (void)dat::orders(bytes); });
    }
    // A sparse hand-authored column fixture exercises the first/last entries,
    // partial-column boundaries, inclusive extents and unsigned fixed-point HP.
    std::vector<std::uint8_t> b(19876);
    put(b, 228, 228, 2);
    put(b, 3156, 0xfedcba98, 4);
    put(b, 2472, 1, 1);
    put(b, 2700, 100, 2);
    put(b, 7032, 1, 4);
    put(b, 8172, 7, 1);
    put(b, 4296, 193, 1);
    put(b, 4296 + 227, 255, 1);
    put(b, 9084, 6, 1);
    put(b, 9084 + 227, 255, 1);
    put(b, 12580, 8, 2);
    put(b, 12582, 9, 2);
    put(b, 12584, 10, 2);
    put(b, 12586, 11, 2);
    put(b, 14860 + 227 * 2, 65535, 2);
    put(b, 16684, 255, 1);
    put(b, 16912, 254, 1);
    put(b, 17140 + 227, 253, 1);
    put(b, 8628, 1, 1);
    put(b, 8628 + 227, 3, 1);
    put(b, 5436, 10, 1);
    put(b, 5436 + 227, 175, 1);
    put(b, 5664, 2, 1);
    put(b, 5664 + 227, 50, 1);
    auto u = dat::units(b);
    check(u[0].attackUnit == 10 && u[0].attackMove == 2 &&
              u[227].attackUnit == 175 && u[227].attackMove == 50 &&
              u[1].attackUnit == 0 && u[1].attackMove == 0,
          "Default attack order columns mismatch");
    check(u[0].size == 1 && u[227].size == 3 && u[1].size == 0,
          "Unit size column mismatch");
    check(u[0].groupFlags == 255 && u[0].supplyProvided == 254 &&
              u[227].supplyRequired == 253 && u[227].supplyProvided == 0,
          "Supply/group columns mismatch");
    check(u[0].hitpoints == 0xfedcba98 && u[0].shields &&
              u[0].shieldPoints == 100 && u[0].building() &&
              u[0].turret == 228 && u[0].sightRange == 7 &&
              u[0].movementFlags == 193 && u[227].movementFlags == 255 &&
              u[0].rightClickAction == 6 && u[227].rightClickAction == 255 &&
              u[0].bounds.left == 8 && u[0].bounds.top == 9 &&
              u[0].bounds.right == 10 && u[0].bounds.bottom == 11 &&
              u[227].mineralCost == 65535,
          "Unit layout mismatch");
    rejects([&] { dat::units(std::span(b).first(b.size() - 1)); });
    b.push_back(0);
    rejects([&] { dat::units(b); });
    dat::Reader reader(b, b.size(), "test");
    rejects(
        [&] { reader.value(0, std::numeric_limits<std::size_t>::max(), 4); });
    rejects([&] { reader.value(b.size(), 0, 1); });
    rejects([&] { reader.value(0, 0, 0); });
    std::vector<std::uint8_t> w(5460);
    put(w, 1170 + 129 * 4, 0xfedcba98, 4);
    put(w, 3640 + 129 * 2, 65000, 2);
    put(w, 260, 208, 4);
    put(w, 2470, 5, 1);
    put(w, 4290, 2, 1);
    put(w, 4160 + 129, 255, 1);
    put(w, 4420, 128, 1);
    put(w, 2340, 3, 1);
    put(w, 4550, 128, 1);
    put(w, 4680, 255, 1);
    put(w, 4810, 127, 1);
    auto weapons = dat::weapons(w);
    check(weapons[129].minimumRange == 0xfedcba98 &&
              weapons[129].damage == 65000 && weapons[129].cooldown == 255 &&
              weapons[0].attackAngle == 128 && weapons[0].damageType == 3,
          "Weapon layout mismatch");
    check(weapons[0].graphics == 208 && weapons[0].behavior == 5 &&
              weapons[0].projectileCount == 2 &&
              weapons[0].headingOffset == -128 &&
              weapons[0].forwardOffset == -1 && weapons[0].upwardOffset == 127,
          "Projectile columns and signed offsets");
    std::vector<std::uint8_t> a(1281), t(836), f(3135);
    put(a, 976 + 120, 500, 2);
    put(a, 610 + 120, 601, 2);
    put(t, 616 + 86, 700, 2);
    put(t, 264 + 86, 800, 2);
    put(f, 418 + 208 * 4, 1280, 4);
    put(f, 1254 + 208 * 2, 67, 2);
    put(f, 2926 + 208, 2, 1);
    check(dat::upgrades(a)[60].label == 500 &&
              dat::upgrades(a)[60].costs[5] == 601 &&
              dat::techs(t)[43].label == 700 &&
              dat::techs(t)[43].costs[3] == 800 &&
              dat::flingies(f)[208].speed == 1280 &&
              dat::flingies(f)[208].acceleration == 67 &&
              dat::flingies(f)[208].control == 2,
          "DAT last-entry mismatch");
    rejects([&] { dat::weapons({}); });
    rejects([&] { dat::upgrades({}); });
    rejects([&] { dat::techs({}); });
    rejects([&] { dat::flingies({}); });
    std::cout << "PASS: classic DAT layout and bounds\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
