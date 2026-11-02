#pragma once
// Ramps of any width, made of the game's cliff pieces (the Terrain tool strip's Ramp brush). A ramp is cut into a cliff
// edge that faces one of 8 ways (N, NE, E, SE, S, SW, W, NW: the way down): wall A, slopes, wall B, written as tiles. Its
// width is a count of slopes (0: the two walls only, size 0.5). The kits are src/data/ramp_kits.inc: each block of the
// game's ramp doodads (a tileset has one or more: Cliff, Temple Wall, Basilica...) in each facing. Where a piece fits is
// learned from the ramps of the game's maps and the approved example maps: the terrains of the ISOM corners under it.
#include "stardraft/editor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stardraft {

// The ways a cliff edge faces (the way down the ramp).
constexpr int kRampFacings = 8;
const char* rampFacingName(int facing);  // N, NE, E, SE, S, SW, W, NW

// Whether the tileset (ERA value) has ramp kits that the maps show where they go.
bool rampsAvailable(int era);

// The kits (indexes) of a tileset that the brush offers (all: also those the maps do not show); a kit's facing, its
// block's first doodad and its doodad group.
std::vector<int> rampKitsOf(int era, bool all = false);
int rampKitFacing(int kit);
int rampKitBase(int kit);
int rampKitDoodad(int kit, int kind);  // a piece's doodad (0 wall A, 1 slope, 2 wall B)
// A kit's ground below (several: apart by "|") and above.
std::pair<std::string, std::string> rampKitTerrains(int kit);
const char* rampKitGroup(int kit);
// The first kit of a tileset and facing (its spec's block where it has one), -1: none.
int rampKit(int era, int facing);

// The pieces of a ramp: 0 wall A, 1 slope, 2 wall B; each piece's top left (tiles).
struct RampPiece {
    int kind, x, y;
};
// A kit's ramp with `slopes` slopes, its wall A's top left at tile (ax, ay): its pieces, a piece's size (tiles), its tiles.
std::vector<RampPiece> rampKitPieces(int kit, int slopes, int ax, int ay);
std::pair<int, int> rampKitPieceSize(int kit, int kind);
std::vector<Editor::TileAt> rampKitTiles(int kit, int slopes, int ax, int ay);
// Whether the map's tiles have that ramp: each tile of each piece as the kit has it.
bool rampKitMatches(const Editor& ed, int kit, int slopes, int ax, int ay);
// Whether the ramp fits the map's cliff there: under each piece the ISOM corners have the terrains of the maps' ramps'
// pieces, its edge meets the tiles round it as the game's maps have them, all its tiles are on the map, and it joins the
// two ground levels.
bool rampKitFits(const Editor& ed, int kit, int slopes, int ax, int ay);

// The same with the first kit of the tileset and facing (rampKit).
std::vector<Editor::TileAt> rampTiles(int era, int facing, int slopes, int ax, int ay);
std::vector<RampPiece> rampPieces(int era, int facing, int slopes, int ax, int ay);
std::pair<int, int> rampPieceSize(int era, int facing, int kind);
bool rampMatches(const Editor& ed, int facing, int slopes, int ax, int ay);
bool rampFits(const Editor& ed, int facing, int slopes, int ax, int ay);

// Whether a ramp's tiles join two ground levels: a walkable way through its minitiles from the lowest level to a higher
// one (the game's VF4 flags). A ramp of the two walls only does not on some edges (Ashworld's E and W).
bool rampJoins(const Tileset& ts, const std::vector<Editor::TileAt>& tiles);

struct RampPlan {
    // NoCliff: no ramp fits a cliff edge near the stroke's start.
    enum class Problem { None, NoKit, NoIsom, NoCliff };
    Problem problem = Problem::NoCliff;
    std::string low, high;  // the kit's ground (for the messages)
    int kit = -1, facing = -1, slopes = 0, ax = 0, ay = 0;
    std::vector<Editor::TileAt> tiles;
};

// The ramp that a brush stroke (map pixels) paints: the ramp of the kit (any block of the tileset) that fits a cliff edge
// nearest the stroke's start (its facing: the edge's), on the edge as far as the stroke goes along it, either way.
RampPlan planRamp(const Editor& ed, const std::vector<std::pair<int, int>>& stroke);

// The ramps that a stroke paints along one edge or more (round a plateau's corner): a ramp for the stroke's first
// stretch along an edge (planRamp), then for the rest of the stroke from its first point away from that ramp, and so on;
// a ramp that overlaps an earlier one is left out. Empty: none (the first planRamp's problem says why).
std::vector<RampPlan> planRamps(const Editor& ed, const std::vector<std::pair<int, int>>& stroke, RampPlan* why = nullptr);

// The ramps a stroke (map pixels) touches: the ramp pieces' tiles within a diamond of it, and all the ramp tiles joined
// to them (whole ramps). Shift with the Ramp brush makes them the cliff again (Editor::remakeTilesFromIsom).
std::vector<std::pair<int, int>> rampTilesUnder(const Editor& ed, const std::vector<std::pair<int, int>>& stroke);

// The kits' ramps in a map's tiles (MTXM, w x h): each ramp's kit, slopes and wall A's place. near: only the ramps with
// a tile marked in it (one byte a tile).
struct RampOnMap {
    int kit, slopes, ax, ay;
};
std::vector<RampOnMap> rampsIn(int era, int w, int h, const std::vector<uint16_t>& tiles, const std::vector<uint8_t>* near = nullptr);

// After a terrain edit (the map's tiles before it: `before`, MTXM): each ramp of `before` whose tiles the edit changed, or
// whose pieces the ground under them no longer fits (none on protected tiles), is fitted to the cliffs again along the
// same stretch: its tiles become the cliff (the ISOM's), then the ramps that fit there go in (shorter, moved or split
// round the new cliffs; none where none fits). Returns the ramps made again. Part of the edit's undo step when the edit is a stroke.
int refitRamps(Editor& ed, const std::vector<uint16_t>& before);

}  // namespace stardraft
