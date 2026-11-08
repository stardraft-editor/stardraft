// SPDX-License-Identifier: MIT
// Original reader for the classic Brood War column-major DAT formats.
// Layout references and scope are recorded in packages/simulation/README.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace mapeditor::simulation::dat {
class Reader {
public:
  Reader(std::span<const std::uint8_t> bytes, std::size_t size,
         const char *name)
      : bytes_(bytes) {
    if (bytes.size() != size)
      throw std::runtime_error(std::string("Unsupported or truncated ") + name);
  }
  std::uint32_t value(std::size_t offset, std::size_t index,
                      std::size_t width) const {
    if ((width != 1 && width != 2 && width != 4) || offset > bytes_.size() ||
        index >= (bytes_.size() - offset) / width)
      throw std::out_of_range("DAT column access");
    auto p = offset + index * width;
    std::uint32_t result = 0;
    for (std::size_t n = 0; n < width; ++n)
      result |= std::uint32_t(bytes_[p + n]) << (n * 8);
    return result;
  }

private:
  std::span<const std::uint8_t> bytes_;
};
struct Order {
  bool interruptible = false, queueable = false, fleeOnHit = false;
};
inline std::array<Order, 189> orders(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 4158, "orders.dat (classic 189-entry layout required)");
  std::array<Order, 189> result;
  for (std::size_t id = 0; id < result.size(); ++id) {
    result[id].interruptible = r.value(1323, id, 1) != 0;
    result[id].queueable = r.value(1701, id, 1) != 0;
    result[id].fleeOnHit = r.value(2268, id, 1) != 0;
  }
  return result;
}
struct Extents {
  int left = 0, top = 0, right = 0, bottom = 0;
};
struct Unit {
  int graphics = 0, turret = 228;
  int initialDirection = 0, placementWidth = 0, placementHeight = 0;
  int constructionImage = 0, elevation = 0, movementFlags = 0, humanIdle = 0,
      computerIdle = 0, returnIdle = 0, rightClickAction = 0;
  int attackUnit = 23, attackMove = 23;
  bool shields = false;
  int shieldPoints = 0;
  std::uint32_t hitpoints = 0, flags = 0;
  int groundWeapon = 130, airWeapon = 130, sightRange = 0, armor = 0;
  int acquisitionRange = 0, size = 0;
  Extents bounds;
  int mineralCost = 0, gasCost = 0, buildTime = 0;
  int groupFlags = 0, supplyProvided = 0, supplyRequired = 0;
  bool building() const { return (flags & 1) != 0; }
  int productionCount() const { return (flags & 0x400U) ? 2 : 1; }
};
inline std::array<Unit, 228> units(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 19876, "units.dat (classic 228-entry layout required)");
  std::array<Unit, 228> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    auto &u = result[i];
    u.graphics = r.value(0, i, 1);
    u.turret = r.value(228, i, 2);
    u.initialDirection = r.value(2244, i, 1);
    u.constructionImage = r.value(1332, i, 4);
    u.elevation = r.value(4068, i, 1);
    u.movementFlags = r.value(4296, i, 1);
    u.computerIdle = r.value(4752, i, 1);
    u.humanIdle = r.value(4980, i, 1);
    u.returnIdle = r.value(5208, i, 1);
    u.attackUnit = r.value(5436, i, 1);
    u.attackMove = r.value(5664, i, 1);
    u.placementWidth = r.value(11284, i * 2, 2);
    u.placementHeight = r.value(11284, i * 2 + 1, 2);
    u.shields = r.value(2472, i, 1) != 0;
    u.shieldPoints = r.value(2700, i, 2);
    u.hitpoints = r.value(3156, i, 4); // unsigned 24.8, preserved exactly
    u.groundWeapon = r.value(5892, i, 1);
    u.airWeapon = r.value(6348, i, 1);
    u.flags = r.value(7032, i, 4);
    u.sightRange = r.value(8172, i, 1);
    u.acquisitionRange = r.value(7944, i, 1);
    u.size = r.value(8628, i, 1);
    u.armor = r.value(8856, i, 1);
    u.rightClickAction = r.value(9084, i, 1);
    u.bounds = {
        int(r.value(12580, i * 4, 2)), int(r.value(12580, i * 4 + 1, 2)),
        int(r.value(12580, i * 4 + 2, 2)), int(r.value(12580, i * 4 + 3, 2))};
    u.mineralCost = r.value(14860, i, 2);
    u.gasCost = r.value(15316, i, 2);
    u.buildTime = r.value(15772, i, 2);
    u.groupFlags = r.value(16684, i, 1);
    u.supplyProvided = r.value(16912, i, 1);
    u.supplyRequired = r.value(17140, i, 1);
  }
  return result;
}
struct Weapon {
  int label = 0;
  std::uint32_t minimumRange = 0, maximumRange = 0;
  int damage = 0, bonus = 0, cooldown = 0, attackAngle = 0, damageType = 0;
  std::uint32_t graphics = 0;
  int behavior = 0, lifetime = 0, hitType = 0, projectileCount = 0;
  int headingOffset = 0, forwardOffset = 0, upwardOffset = 0;
};
inline std::array<Weapon, 130> weapons(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 5460, "weapons.dat");
  std::array<Weapon, 130> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    auto &w = result[i];
    w.label = r.value(0, i, 2);
    w.minimumRange = r.value(1170, i, 4);
    w.maximumRange = r.value(1690, i, 4);
    w.damage = r.value(3640, i, 2);
    w.bonus = r.value(3900, i, 2);
    w.cooldown = r.value(4160, i, 1);
    w.attackAngle = r.value(4420, i, 1);
    w.damageType = r.value(2340, i, 1);
    w.graphics = r.value(260, i, 4);
    w.behavior = r.value(2470, i, 1);
    w.lifetime = r.value(2600, i, 1);
    w.hitType = r.value(2730, i, 1);
    w.projectileCount = r.value(4290, i, 1);
    auto signedByte = [&](std::size_t offset) {
      int value = r.value(offset, i, 1);
      return value >= 128 ? value - 256 : value;
    };
    w.headingOffset = signedByte(4550);
    w.forwardOffset = signedByte(4680);
    w.upwardOffset = signedByte(4810);
  }
  return result;
}
struct Upgrade {
  int label = 0;
  std::array<int, 6>
      costs{}; // minerals base/factor, gas base/factor, time base/factor
};
inline std::array<Upgrade, 61> upgrades(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 1281, "upgrades.dat");
  std::array<Upgrade, 61> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i].label = r.value(976, i, 2);
    for (std::size_t c = 0; c < 6; ++c)
      result[i].costs[c] = r.value(c * 122, i, 2);
  }
  return result;
}
struct Tech {
  int label = 0;
  std::array<int, 4> costs{}; // minerals, gas, research time, energy
};
inline std::array<Tech, 44> techs(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 836, "techdata.dat");
  std::array<Tech, 44> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i].label = r.value(616, i, 2);
    for (std::size_t c = 0; c < 4; ++c)
      result[i].costs[c] = r.value(c * 88, i, 2);
  }
  return result;
}
struct Flingy {
  int sprite = 0;
  std::uint32_t speed = 0, haltDistance = 0;
  int acceleration = 0, turnRate = 0, control = 0;
  int id = -1;
};
inline std::array<Flingy, 209> flingies(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 3135, "flingy.dat");
  std::array<Flingy, 209> result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = {int(r.value(0, i, 2)),
                 r.value(418, i, 4),
                 r.value(1672, i, 4),
                 int(r.value(1254, i, 2)),
                 int(r.value(2508, i, 1)),
                 int(r.value(2926, i, 1)),
                 int(i)};
  return result;
}
struct Sprite {
  int image = 0;
  bool visible = false;
};
inline std::array<Sprite, 517> sprites(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 3229, "sprites.dat");
  std::array<Sprite, 517> result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = {int(r.value(0, i, 2)), r.value(1938, i, 1) != 0};
  return result;
}
struct Image {
  std::uint32_t grp = 0, script = 0;
  bool directional = false, clickable = false, animated = false,
       alwaysVisible = false;
  int modifier = 0, colorShift = 0;
  std::array<std::uint32_t, 6>
      offsets{}; // shields, attack, damage, special, landing, lift
};
inline std::array<Image, 999> images(std::span<const std::uint8_t> bytes) {
  Reader r(bytes, 37962, "images.dat");
  std::array<Image, 999> result;
  for (std::size_t i = 0; i < result.size(); ++i) {
    auto &image = result[i];
    image.grp = r.value(0, i, 4);
    image.directional = r.value(3996, i, 1) != 0;
    image.clickable = r.value(4995, i, 1) != 0;
    image.animated = r.value(5994, i, 1) != 0;
    image.alwaysVisible = r.value(6993, i, 1) != 0;
    image.modifier = r.value(7992, i, 1);
    image.colorShift = r.value(8991, i, 1);
    image.script = r.value(9990, i, 4);
    for (std::size_t n = 0; n < 6; ++n)
      image.offsets[n] = r.value(13986 + n * 3996, i, 4);
  }
  return result;
}
} // namespace mapeditor::simulation::dat
