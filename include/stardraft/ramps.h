#pragma once
// Ramps of any width, made of the game's cliff pieces (the Terrain tool strip's Ramp brush). A ramp is cut into a cliff
// edge that faces one of 8 ways (N, NE, E, SE, S, SW, W, NW: the way down): wall A, slopes, wall B, written as tiles. Its
// width is a count of slopes (0: the two walls only, size 0.5). The kits are src/data/ramp_kits.inc; where a piece fits
// is learned from approved example ramps: the high and low ground of the ISOM corners under it.
#include "stardraft/editor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stardraft {

// The ways a cliff edge faces (the way down the ramp), in kRampKits' order.
constexpr int kRampFacings = 8;
const char* rampFacingName(int facing);  // N, NE, E, SE, S, SW, W, NW

// Whether the tileset (ERA value) has ramp kits.
bool rampsAvailable(int era);

// The tiles of a ramp facing `facing` with `slopes` slopes, its wall A's top left at tile (ax, ay). Empty: no kit.
std::vector<Editor::TileAt> rampTiles(int era, int facing, int slopes, int ax, int ay);

// The pieces of a ramp (for the fit and the tests): 0 wall A, 1 slope, 2 wall B; each piece's top left (tiles).
struct RampPiece {
    int kind, x, y;
};
std::vector<RampPiece> rampPieces(int era, int facing, int slopes, int ax, int ay);
// A piece's size in tiles (width, height).
std::pair<int, int> rampPieceSize(int era, int facing, int kind);

// Whether the map's tiles have that ramp: each tile of each piece as the kit has it.
bool rampMatches(const Editor& ed, int facing, int slopes, int ax, int ay);

// Whether a ramp's tiles join two ground levels: a walkable way through its minitiles from the lowest level to a higher
// one (the game's VF4 flags). A ramp of the two walls only does not on some edges (Ashworld's E and W).
bool rampJoins(const Tileset& ts, const std::vector<Editor::TileAt>& tiles);

// Whether the ramp fits the map's cliff there: under each piece the ISOM corners have the high and the low ground of
// the approved ramps' pieces, its edge meets the tiles round it as the game's maps have them, all its tiles are on the
// map, and it joins the two ground levels.
bool rampFits(const Editor& ed, int facing, int slopes, int ax, int ay);

struct RampPlan {
    // NoCliff: no ramp fits a cliff edge near the stroke's start.
    enum class Problem { None, NoKit, NoIsom, NoCliff };
    Problem problem = Problem::NoCliff;
    std::string low, high;  // the kit's ground (for the messages)
    int facing = -1, slopes = 0, ax = 0, ay = 0;
    std::vector<Editor::TileAt> tiles;
};

// The ramp that a brush stroke (map pixels) paints: the ramp that fits a cliff edge nearest the stroke's start (its
// facing: the edge's), on the edge as far as the stroke goes along it, either way.
RampPlan planRamp(const Editor& ed, const std::vector<std::pair<int, int>>& stroke);

// The ramps that a stroke paints along one edge or more (round a plateau's corner): a ramp for the stroke's first
// stretch along an edge (planRamp), then for the rest of the stroke from its first point away from that ramp, and so on;
// a ramp that overlaps an earlier one is left out. Empty: none (the first planRamp's problem says why).
std::vector<RampPlan> planRamps(const Editor& ed, const std::vector<std::pair<int, int>>& stroke, RampPlan* why = nullptr);

}  // namespace stardraft
