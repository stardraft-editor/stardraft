#pragma once
// Bridges of any size, made of tile pieces (the Terrain tool strip's Bridge tool). A bridge has a length (along the deck)
// and a width (across it), each a count of middle pieces: 0 is size 0.5 (the two ends only), n is size n. The pieces
// are the bridge kits of src/data/bridge_kits.inc, learned from approved example bridges. A bridge goes over a straight
// river that ISOM painting made: ground, then water, then ground; its length comes from the water's width.
#include "stardraft/editor.h"
#include "stardraft/passages.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stardraft {

// Whether the tileset (ERA value) has bridge kits.
bool bridgesAvailable(int era);
// Whether the map's tileset has bridges: the game's kits, or the passages' (stardraft/passages.h: a mod's).
bool bridgesAvailable(const Editor& ed);
// All the tiles a tileset's bridge kits (both orientations) can put down: the tiles a passage maps.
std::vector<uint16_t> bridgeKitTileValues(int era);
// A game bridge kit (length 1, width 1) as a mapping's canvas (stardraft/passages.h): its tiles as the start cap, the
// deck and the end cap (by their places along the deck), the deck and the cells over the river's water marked water,
// the others coast; its grid and terrains. A start for a mod's tileset that has the game's bridge tiles, and the tests' canvas.
passages::BridgeMapping bridgeCanvasOfKit(int era, bool u);

// The tiles of a bridge with its origin (the kit's cell 0, 0) at tile (ox, oy). u: the mirror kit of d (the kits' decks
// go along the two diagonals; which way is d depends on the tileset). seed: the choice among a piece's variants. Empty:
// no kit. on: the map's tileset; then each piece's variant is the one whose tiles meet the tiles round them best (a
// pair the game's maps have; else the least difference of the pixels at their edge), also the map's tiles round the
// bridge (around). Without it: the approved bridge of that size, else the variants of the pairs the corpus has.
std::vector<Editor::TileAt> bridgeTiles(int era, bool u, int length, int width, int ox, int oy, uint32_t seed, const Editor* on = nullptr,
                                        bool around = false);
// How badly a bridge's tiles meet each other (and, around: the map's tiles round them): the sum over each two tiles side
// by side or one above the other of 0 (the game's maps have them so) or 1 and the mean difference of the pixels at their
// edge (0 to 255).
double bridgeSeams(const Editor& ed, const std::vector<Editor::TileAt>& tiles, bool around);
// Whether the map's tiles have that bridge at (ox, oy): each pair is one of its piece's variants.
bool bridgeMatches(const Editor& ed, bool u, int length, int width, int ox, int oy);

struct BridgePlan {
    // Why there is no bridge (None: there is one).
    // Short: the drag has not crossed water yet; ReachGround: it ends on the water; NeedsTerrain: it starts on the water
    // (or crosses no whole water); TooNarrow: less water than the shortest bridge.
    enum class Problem { None, NoKit, NoIsom, Short, ReachGround, NeedsTerrain, TooNarrow, OffMap };
    Problem problem = Problem::Short;
    std::string ground, water;  // the kit's terrains (for the messages)
    int kit = -1;               // -1: the game's kit; else the passages' bridge kit it is
    bool u = false;
    int length = 0, width = 0;   // in middle pieces (0: size 0.5)
    int ox = 0, oy = 0;          // the bridge's origin (bridgeTiles)
    uint32_t seed = 0;
    std::vector<Editor::TileAt> tiles;
    // No bridge (after the drag started on the map): a bridge as long as the drag, with its middle on the drag's middle,
    // for a preview of where it goes (the tiles on the map only).
    std::vector<Editor::TileAt> draft;
    // The shore that the bridge needs (ISOM corners): the water's rows straight under all of it and past its sides, a
    // bank of ground at each side of the water, and ground (Badlands: its road, Asphalt) under its ends. Each list is of
    // one terrain, in the order to paint them (the water last).
    std::vector<std::vector<CornerValue>> shore;
};

// The bridge on the map: its shore, then its tiles. False: nothing changed.
bool applyBridge(Editor& ed, const BridgePlan& plan);

// The bridge that a drag from map pixel (x0, y0) to (x1, y1) makes: the drag goes from land (any land terrain, as far
// from the water as it likes) over water to the land on the other side, along one of the map's two diagonals (2:1). The bridge is as long as the water on the drag's line is
// wide, snaps to the river's diamonds and lies across the drag's line (its shore makes the river straight under it);
// width: middle pieces across.
BridgePlan planBridge(const Editor& ed, int x0, int y0, int x1, int y1, int width, uint32_t seed);

}  // namespace stardraft
