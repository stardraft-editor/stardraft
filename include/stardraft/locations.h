#pragma once

// Locations (MRGN section): 64 entries in original maps, 255 in Hybrid/Brood War maps.

#include <cstdint>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

struct Location {
    uint32_t left = 0, top = 0, right = 0, bottom = 0;  // pixels; right/bottom may be smaller (inverted)
    uint16_t nameString = 0;                            // string id, 0 = default name
    uint16_t elevation = 0;                             // LocationElevation bits; a set bit excludes that elevation

    static constexpr size_t kSize = 20;
    bool used() const { return left || top || right || bottom || nameString; }
};

// The elevation flags of a location (MRGN). A set bit excludes the elevation: units there are not "in the
// location" for triggers (StarEdit's unchecked boxes). 0 = all elevations; bits 6-15 are not used.
namespace LocationElevation {
inline constexpr uint16_t LowGround = 1 << 0, MediumGround = 1 << 1, HighGround = 1 << 2;
inline constexpr uint16_t LowAir = 1 << 3, MediumAir = 1 << 4, HighAir = 1 << 5;
inline constexpr uint16_t All = 0x3F;  // the six bits
}  // namespace LocationElevation

// Index of the "Anywhere" location (location 64).
inline constexpr int kAnywhereLocation = 63;

std::vector<Location> readLocations(const Chk& chk);
void writeLocations(Chk& chk, const std::vector<Location>& locations);

}  // namespace stardraft
