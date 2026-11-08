// SPDX-License-Identifier: MIT
#pragma once
#include "vitals.h"
namespace mapeditor::simulation {
struct HitLoss {
  int shields = 0, hitpoints = 0;
};
// Damage categories and target sizes are the data-file numeric values.
// Armor applies before the size adjustment; shields use the unscaled amount.
inline HitLoss weaponHitLoss(UnitVitals &vitals, bool hasShields, int amount,
                             int shieldArmor, int hitpointArmor,
                             int damageType, int size) {
  if (amount < 0 || shieldArmor < 0 || hitpointArmor < 0 ||
      damageType < 0 || damageType > 4 || size < 0 || size > 3)
    throw std::invalid_argument("Invalid weapon hit input");
  const bool armored = damageType != 4;
  int remaining = std::max(128, amount);
  HitLoss loss;
  if (hasShields && vitals.shields >= 256) {
    if (armored)
      remaining = remaining > shieldArmor ? remaining - shieldArmor : 128;
    loss.shields = std::min(int(vitals.shields), remaining);
    remaining -= loss.shields;
  }
  if (armored)
    remaining = std::max(0, remaining - hitpointArmor);
  int quarters = 4;
  if (damageType == 0 || ((damageType == 1 || damageType == 2) && size == 0))
    quarters = 0;
  else if (damageType == 1 && size < 3)
    quarters = size + 1;
  else if (damageType == 2 && size > 1)
    quarters = 4 - size;
  remaining = int(std::int64_t(remaining) * quarters / 4);
  if (!loss.shields)
    remaining = std::max(128, remaining);
  loss.hitpoints = std::min(std::max(0, int(vitals.hitpoints)), remaining);
  vitals.shields -= loss.shields;
  vitals.hitpoints -= loss.hitpoints;
  return loss;
}
// Normal weapon damage in signed24.8 units. Stored, unused shields do not
// absorb hits; sub-point displayed shields retain their fractional remainder.
inline HitLoss normalHit(UnitVitals &vitals, bool hasShields, int amount,
                         int shieldArmor, int hitpointArmor) {
  if (amount < 0 || shieldArmor < 0 || hitpointArmor < 0)
    throw std::invalid_argument("Negative hit input");
  const int incoming = std::max(128, amount);
  const bool shielded = hasShields && vitals.shields >= 256;
  const int afterShieldArmor =
      shielded ? (incoming > shieldArmor ? incoming - shieldArmor : 128)
               : incoming;
  HitLoss loss;
  loss.shields = shielded ? std::min(int(vitals.shields), afterShieldArmor) : 0;
  const int remainder = std::max(0, afterShieldArmor - loss.shields - hitpointArmor);
  loss.hitpoints = std::min(std::max(0, int(vitals.hitpoints)),
                           loss.shields ? remainder : std::max(128, remainder));
  vitals.shields -= loss.shields;
  vitals.hitpoints -= loss.hitpoints;
  return loss;
}
} // namespace mapeditor::simulation
