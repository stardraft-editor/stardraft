#pragma once

#include <cstdint>
#include <string>

#include "stardraft/chk.h"

namespace stardraft {

class GameData;

// Build a complete, playable Brood War (Remastered) scenario like StarEdit's "New map": all
// required sections, 8 human players + neutral, 255 locations with Anywhere, default unit,
// upgrade and tech settings taken from the game's .dat files, and terrain filled with `terrain`
// (an ISOM terrain id; requires an ISOM table for the tileset). VCOD is copied from a campaign
// map in the game data.
bool createNewScenario(Chk& chk, const GameData& data, uint16_t width, uint16_t height, uint16_t era, uint16_t terrain,
                       std::string& error);

}  // namespace stardraft
