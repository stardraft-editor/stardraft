#pragma once

// Zerg creep (docs: the creep rules): which tiles can carry creep, the creep that a map's buildings have at the start
// of a game, and how a creep tile and a creep edge are drawn. Plain values, no editor or renderer: the map view, the
// simulation and the tests use it.

#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

namespace stardraft {

// The units that make creep: Hatchery, Lair, Hive, Creep Colony, Sunken Colony, Spore Colony (by type: no units.dat
// flag says it). `complete`: a Hatchery and a Creep Colony make creep only when complete; the others always.
bool makesCreep(uint16_t type, bool complete = true);

// A building for the creep at the start: its centre (map pixels), its placement box (pixels), whether it makes creep
// and whether it requires creep (units.dat flag 0x20000: it gets creep under it).
struct CreepBuilding {
    int x = 0, y = 0;
    int placeW = 0, placeH = 0;
    bool maker = false, requiresCreep = false;
};

// The creep area's half-axes (pixels) round a maker's centre: a tile is in it when its centre is.
constexpr int kCreepHalfWidth = 320, kCreepHalfHeight = 200;
bool inCreepArea(int centreX, int centreY, int tileX, int tileY);
// A building's footprint in tiles: left, top, width, height.
struct TileRect {
    int x = 0, y = 0, w = 0, h = 0;
};
TileRect footprintOf(const CreepBuilding& b);

// Which tiles can carry creep (width x height, 1: can): not unbuildable, not partly walkable, and the tile south of it
// not unbuildable (the bottom row passes that test). `unbuildable` and `partlyWalkable` per tile.
std::vector<uint8_t> creepCarriers(int width, int height, const std::vector<uint8_t>& unbuildable,
                                   const std::vector<uint8_t>& partlyWalkable);
// The map's edges are unbuildable: the bottom row, and the five tiles at each end of the row above it.
void unbuildableMapEdges(int width, int height, std::vector<uint8_t>& unbuildable);
// The same from a tileset and a map's terrain (the cv5 group's unbuildable flag; vf4: some but not all of a
// megatile's 16 minitiles walkable).
std::vector<uint8_t> creepCarriers(const Tileset& tileset, const TerrainLayer& terrain);

// The creep at the start of a game (1: creep): each maker's footprint and the tiles of its area that can be reached
// from the footprint by 8-neighbour steps inside the area over tiles that can carry creep and that no other building
// stands on; each building that requires creep gets the tiles of its footprint that can carry creep. `painted` (may
// be empty): creep that the map's terrain has (its creep tiles), kept.
std::vector<uint8_t> startCreep(int width, int height, const std::vector<uint8_t>& carriers,
                                const std::vector<CreepBuilding>& buildings, const std::vector<uint8_t>& painted = {});
// The same for a map: its terrain, its units (the placed buildings, units.dat's placement sizes and flags).
std::vector<uint8_t> startCreep(const Tileset& tileset, const TerrainLayer& terrain, const std::vector<Unit>& units,
                                const UnitTypes& types);

// Drawing. A creep tile is a megatile of the tileset's creep group (cv5 group 1) chosen by its variant; a tile with no
// creep next to creep draws an edge frame of the tileset's creep GRP ("tileset\<name>.grp", 37 frames) over itself.
constexpr uint16_t kCreepGroup = 1;
// The 8 neighbours with creep as bits: SE 1, S 2, SW 4, E 8, W 16, NE 32, N 64, NW 128 (outside the map: none).
uint8_t creepNeighbours(const std::vector<uint8_t>& creep, int width, int height, int x, int y);
// The edge frame (1 to 37) for a tile with no creep by its neighbours; 0: none.
int creepEdgeFrame(uint8_t neighbours);
// A creep tile's variant, drawn once a session: with 4% chance 6 to 12, else 0 to 5 (`seed`: a session's stream).
int creepVariant(uint32_t& seed);
// The terrain with creep: each creep tile becomes the creep group's tile of its variant (`variants`: one per tile).
void applyCreep(TerrainLayer& terrain, const std::vector<uint8_t>& creep, const std::vector<uint8_t>& variants);
// The tileset's creep edge GRP in the game data.
std::string creepGrpPath(uint16_t era);

}  // namespace stardraft
