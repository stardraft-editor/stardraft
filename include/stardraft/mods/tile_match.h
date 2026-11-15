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
    // The template's rects: each variant's tile pair (left, right): a rect's halves join (seams), and the steps between
    // the tiles of one rect (its halves, its variants) are its layout.
    std::vector<std::array<uint16_t, 2>> pairs;
    // (the signals' weights, as measured: likeness, sides, colours and the pairs gave the most right; seams (a rect's
    // halves join cleanly in the mod), layout (steps within a rect) and edges (where the gray changes) on top gave fewer)
    float wLike = 1, wMask = 1, wColor = 1, wSeam = 0, wLayout = 0, wEdge = 0;
};

struct Suggestion {
    uint16_t tmpl = 0, mod = 0;
    float score = 0;       // (higher: better)
    float confidence = 0;  // 0..1: how far it is ahead of the slot's next best mod tile
};

// A suggestion for each slot with no match yet (the best first), each mod tile once. None without a match.
std::vector<Suggestion> suggest(const Input& in);

}  // namespace stardraft::tilematch
