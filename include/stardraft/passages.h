#pragma once
// Passages files (.scpass): ramps and bridges that a mod's tilesets add (Tools > Cosmonarchy > Ramp and bridge mapper).
// A mapping is a canvas: tiles of its tileset at free places (a cell grid, tile units), each cell with a role (the piece
// it is part of: a ramp's wall A, slope or wall B; a bridge's start cap, deck or end cap) and a mark (the ISOM terrain
// under it: a ramp's cliff (high) or ground (low); a bridge's water or coast). Cells with a role are the tiles the tools
// put down; the marks give the ISOM corners the pieces need under and round them. The Ramp brush and the Bridge tool
// use each mapping as a kit of its own (src/ramps.cpp, src/bridges.cpp).
//
// The file is JSON: {"format": "StarCraft passages", "version": 2, "ramps": [...], "bridges": [...]}. A ramp: {"name",
// "tileset" (era 0..7), "facing" (0..7, -1: from the marks), "repeat" (the slope repeats along the edge), "grid" (0..3:
// where the ISOM corners are in the canvas), "gridAuto", "terrains": [{"ground", "cliff"}...], "cells": [[x, y, tile
// (-1: none), role, mark]...]}. A bridge: {"name", "tileset", "deck" (0 down-right, 1 up-right, -1: from the caps),
// "grid", "gridAuto", "terrains": [{"coastA", "water", "coastB"}...], "cells"}. Terrain names are the map's
// (Editor::terrainName).
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace stardraft::passages {

struct RampTerrains {
    std::string ground, cliff;
};
struct BridgeTerrains {
    std::string coastA, water, coastB;  // coast A: under the bridge's start cap; coast B: under its end cap
};

// A cell's role: the piece its tile is part of (none: a tile that shows only, not put down).
enum Role : uint8_t { kNone = 0, kWallA = 1, kSlope = 2, kWallB = 3, kStart = 4, kDeck = 5, kEnd = 6 };
// A cell's mark: the ISOM terrain under it. A ramp: kHigh the cliff, kLow the ground; a bridge: kHigh the water, kLow the
// coast.
enum Mark : uint8_t { kUnmarked = 0, kHigh = 1, kLow = 2 };

// The canvas's cells: x 0 to kCanvasW - 1, y 0 to kCanvasH - 1.
constexpr int kCanvasW = 64, kCanvasH = 48;

struct Cell {
    int x = 0, y = 0;
    int tile = -1;  // MTXM value; -1: none (a role or a mark only)
    uint8_t role = kNone, mark = kUnmarked;
};

struct RampMapping {
    std::string name;
    int era = 0;         // the tileset (ERA value & 7)
    int facing = -1;     // N, NE, E, SE, S, SW, W, NW (the way down); -1: from the marks (rampFacing)
    bool repeat = false;  // the slope repeats along the edge (any width); else the ramp is as painted
    int grid = 0;        // the ISOM corners' places in the canvas (cornerOnGrid)
    bool gridAuto = true;  // the mapper sets grid from the tiles' halves
    std::vector<RampTerrains> terrains;
    std::vector<Cell> cells;
};
struct BridgeMapping {
    std::string name;
    int era = 0;
    int deck = -1;  // 0: the deck goes down-right (d), 1: up-right (u); -1: from the caps (bridgeDeck)
    int grid = 0;
    bool gridAuto = true;
    std::vector<BridgeTerrains> terrains;
    std::vector<Cell> cells;
};
struct Passages {
    std::vector<RampMapping> ramps;
    std::vector<BridgeMapping> bridges;
};

constexpr const char* kExtension = ".scpass";

std::string toText(const Passages& p);
bool parse(const std::string& text, Passages& out, std::string& error);
bool load(const std::filesystem::path& file, Passages& out, std::string& error);
bool save(const std::filesystem::path& file, const Passages& p, std::string& error);

// The passages the tools use (none: the game's kits only). revision() changes on each use(), so the kits made from
// them are made again.
void use(const Passages& p);
const Passages& inUse();
int revision();

// --- The canvas as the tools read it ---------------------------------------------------------

// Whether the canvas point (x, y) (a tile corner, tile units) is an ISOM corner for the grid (0..3): the canvas's cell
// (0, 0) is the map's tile (grid & 1, grid >> 1) of the ISOM pattern (corners at even tile x, (x / 2 + y) odd).
bool cornerOnGrid(int grid, int x, int y);

// An ISOM corner a mapping's pieces need (canvas tile-corner point): the high terrain (a ramp's cliff, a bridge's water)
// or the low one (the ground, the coast); piece: the piece (0, 1, 2: wall A, slope, wall B or start, deck, end) it goes
// with, nearest it.
struct CanvasCorner {
    int x, y;
    bool high;
    int piece;
};
// The corners of the marks: an ISOM corner of the grid whose cells round it (2 x 2) that have a mark all have the high
// mark or all the low one (a cell with no mark says nothing). Mixed: none.
std::vector<CanvasCorner> cornersOf(const std::vector<Cell>& cells, int grid, bool bridge);

// A ramp's shape (the Ramp brush's kit): its pieces (wall A, slope, wall B; their cells, canvas places), its facing,
// the step from one slope to the next (along the edge, from wall A to wall B: a whole ISOM period), its widths (in
// slopes: as painted, 1 (0 with no slope); with repeat, 1 to 64) and its corners. ok false: why (a short text).
struct RampShape {
    bool ok = false;
    std::string why;
    int facing = -1;
    std::array<std::vector<Cell>, 3> pieces;
    int stepX = 0, stepY = 0;
    int minSlopes = 1, maxSlopes = 1;
    std::vector<CanvasCorner> corners;
};
RampShape rampShape(const RampMapping& m);
// The facing (m.facing, else from the marks: from the cliff's corners to the ground's), -1: none.
int rampFacing(const RampMapping& m);
// The same from corners alone.
int facingOfCorners(const std::vector<CanvasCorner>& corners);

// A bridge's shape (the Bridge tool's kit): start cap, deck, end cap; the deck's way (0 d: down-right, 1 u: up-right)
// and its step (a diamond's along it, times what keeps two decks apart), the corners. The deck repeats (1 to 64).
struct BridgeShape {
    bool ok = false;
    std::string why;
    int deck = -1;
    std::array<std::vector<Cell>, 3> pieces;
    int stepX = 0, stepY = 0;
    std::vector<CanvasCorner> corners;
};
BridgeShape bridgeShape(const BridgeMapping& m);
int bridgeDeck(const BridgeMapping& m);

// The grid (0..3) that most of the canvas's tiles' halves of an ISOM rect say: `rectParity(tile)` gives a tile's half (0
// left, 1 right) and the rect's parity ((rx + ry) & 1) as half * 2 + parity, -1 when not known. -1: none known.
template <class F>
int guessGrid(const std::vector<Cell>& cells, F rectParity) {
    int votes[4] = {0, 0, 0, 0};
    for (const Cell& c : cells) {
        if (c.tile < 0) continue;
        const int r = rectParity(uint16_t(c.tile));
        if (r < 0) continue;
        const int half = r >> 1, parity = r & 1;
        for (int g = 0; g < 4; ++g) {
            const int mx = (g & 1) + c.x - half, my = (g >> 1) + c.y;  // (the rect's left tile on the map)
            const int rx = mx >= 0 ? mx / 2 : -((1 - mx) / 2);  // (floor)
            if (((mx % 2) + 2) % 2 == 0 && (((rx + my) % 2) + 2) % 2 == parity) ++votes[g];
        }
    }
    int best = -1;
    for (int g = 0; g < 4; ++g)
        if (votes[g] > 0 && (best < 0 || votes[g] > votes[best])) best = g;
    return best;
}

}  // namespace stardraft::passages
