#pragma once

// Rectangle match suggestions: after a person has matched a few template tiles (a game border's) to the mod's tiles,
// the mod tiles the other template tiles most likely are, each learned from the matches so far:
//  - likeness: both tiles in gray, each tile's brightness and contrast made the same (the shapes count, not colours);
//  - sides: each tile as its two sides only, terrain or ground, by colour (each tileset's tiles split into their two
//    main colours; the texture and the colours themselves left out): the matches say which mod side is which template
//    side;
//  - colours: the template's colours put through a colour map fitted on the matches: the mod's sets of one art on other
//    grounds have the same shapes, and only the ground's colour tells them apart;
//  - a rect's two halves together: the step between them in the mod (the next subtile, the next group) and the subtile
//    they start on (pairs packed 0+1, 2+3...) as the matches have them.
// Only mod tiles near the matched ones (their tile groups, give or take a window) and not taken are suggested.
// tools/plugins/cmbw/probes/cmbw_auto_match (seed, bench) measures them on hand-made and built-in terrains.

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "stardraft/tileset.h"

namespace stardraft::tilematch {

struct Input {
    const Tileset* tmplTs = nullptr;  // (the template's tiles)
    const Tileset* modTs = nullptr;   // (the mod's tiles)
    std::vector<uint16_t> slots;      // the template tiles to match (each once)
    std::map<uint16_t, uint16_t> known;  // the matches so far: template tile -> mod tile
    std::set<uint16_t> taken;            // mod tiles not to suggest (other terrains')
    int window = 8;                      // tile groups around the matched ones that are candidates
    std::set<int> onlyGroups;            // the search space: set, the candidates are these groups' tiles only (no window)
    // The template's rects: each variant's tile pair (left, right): a rect's halves join (seams), and the steps between
    // the tiles of one rect (its halves, its variants) are its layout.
    std::vector<std::array<uint16_t, 2>> pairs;
    // (the signals' weights, as measured: likeness, sides, colours and the pairs gave the most right; seams (a rect's
    // halves join cleanly in the mod), layout (steps within a rect) and edges (where the gray changes) on top gave fewer)
    float wLike = 1, wMask = 1, wColor = 1, wSeam = 0, wLayout = 0, wEdge = 0;
    // Learn the likeness, sides and colour weights from the matches (24 or more): the ones that rank each matched tile's
    // own mod tile highest among the candidates (its colour map fitted without it), in place of the three above when
    // clearly better. Off: measured (cmbw_auto_match seed, LEARN=1) it gave no more right than the weights above.
    bool learn = false;
    struct Weights {
        float like = 1, mask = 1, color = 1;
    };
    Weights* learned = nullptr;  // (set: the weights used)
};

struct Suggestion {
    uint16_t tmpl = 0, mod = 0;
    float score = 0;       // (higher: better)
    float confidence = 0;  // 0..1: how far it is ahead of the slot's next best mod tile
    std::vector<uint16_t> alts;  // (the slot's next best mod tiles, best first: matched ones left out)
};

// A suggestion for each slot with no match yet (the best first), each mod tile once. None without a match.
std::vector<Suggestion> suggest(const Input& in);
// The mod tile groups suggest() takes its candidates from: onlyGroups when set, else a window around each matched tile's.
std::set<int> searchGroups(const Input& in);

// How alike two tiles are pixel by pixel, 0..1: both in gray, each with its brightness and contrast made the same (the
// shapes count, not the colours), or along each one's own main colour line when that is more alike (the same picture in
// another tint); 1: the same picture, 0: nothing alike (or a tile with no picture).
float pixelMatch(const Tileset& a, uint16_t va, const Tileset& b, uint16_t vb);

// The tile of b with the same picture as tile va of a: each pixel's colour within a few steps of the other's (the same
// picture in another tileset's palette), the nearest one (the same tile number when it is as near); 0xFFFF: none.
// (Keep the original: a mod that has the game's tiles too.)
uint16_t samePicture(const Tileset& a, uint16_t va, const Tileset& b);

}  // namespace stardraft::tilematch
