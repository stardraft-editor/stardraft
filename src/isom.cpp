#include "stardraft/isom.h"

#include <algorithm>
#include <functional>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <span>

#include "byteio.h"
#include "stardraft/terrain.h"

namespace stardraft {

namespace {

// Corner patterns (left, top, right, bottom) of the 14 shapes of a terrain pair (A, B),
// in shape-id order. 0 = A, 1 = B.
constexpr std::array<std::array<uint8_t, 4>, 14> kPairPatterns = {{
    {0, 0, 1, 1}, {1, 0, 0, 1}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 1}, {1, 0, 0, 0}, {0, 1, 0, 0},
    {0, 0, 1, 0}, {0, 1, 1, 1}, {1, 1, 0, 1}, {1, 0, 1, 1}, {1, 1, 1, 0}, {0, 1, 0, 1}, {1, 0, 1, 0},
}};

// Positional edge ids per slot (left, top, right, bottom) for even / odd rects.
constexpr std::array<std::array<uint16_t, 4>, 2> kEdgeIds = {{{4, 5, 0, 1}, {2, 6, 7, 3}}};

// --- Corpus-learned ISOM -> TILE rules ---------------------------------------------------

struct TileRule {
    uint64_t key;
    uint16_t group;
};

struct TileGroupInfo {
    uint8_t stackBottom, stackTop;  // CV5 vertical stack connections
    uint16_t leftEdge, rightEdge;   // CV5 horizontal edge codes
    uint16_t commonSubtiles;        // bit mask of megatile slots
    uint16_t rareSubtiles;
};

struct TileCandidate {
    uint64_t key;  // ISOM key without context
    uint16_t group;
    uint32_t count;  // how often Blizzard maps use this group for the key
};

#include "data/badlands_tile_rules.inc"
#include "data/platform_tile_rules.inc"
#include "data/install_tile_rules.inc"
#include "data/ashworld_tile_rules.inc"
#include "data/jungle_tile_rules.inc"
#include "data/desert_tile_rules.inc"
#include "data/ice_tile_rules.inc"
#include "data/twilight_tile_rules.inc"

// CV5 fields per tile group that the rules above leave out.
struct TileEdges {
    uint16_t type, topEdge, bottomEdge;
    uint8_t art;  // the group has a megatile other than 0
};
#include "data/tile_edges.inc"

struct TileRules {
    std::span<const TileRule> t1, t2, t0;
    std::span<const TileGroupInfo> groups;
    std::span<const TileCandidate> candidates;  // sorted by key, most common first within a key
    std::span<const uint32_t> hpairs;           // sorted (leftHalf << 31 | rightEdge << 16 | leftEdge)
    std::span<const TileEdges> edges;           // CV5 type, top and bottom edges per group
    // Groups that are the same piece of terrain (CV5 type, four edges, index parity), sorted by that key;
    // they differ only in their stack connections. Built on first use.
    mutable std::vector<std::pair<uint64_t, uint16_t>> siblings;
    mutable std::vector<uint32_t> useCounts;

    static const TileRules* forEra(uint16_t era) {
        static const TileRules badlands{k_badlands_t1, k_badlands_t2, k_badlands_t0, k_badlands_groups, k_badlands_candidates, k_badlands_hpairs, k_badlands_edges};
        static const TileRules platform{k_platform_t1, k_platform_t2, k_platform_t0, k_platform_groups, k_platform_candidates, k_platform_hpairs, k_platform_edges};
        static const TileRules install{k_install_t1, k_install_t2, k_install_t0, k_install_groups, k_install_candidates, k_install_hpairs, k_install_edges};
        static const TileRules ashworld{k_ashworld_t1, k_ashworld_t2, k_ashworld_t0, k_ashworld_groups, k_ashworld_candidates, k_ashworld_hpairs, k_ashworld_edges};
        static const TileRules jungle{k_jungle_t1, k_jungle_t2, k_jungle_t0, k_jungle_groups, k_jungle_candidates, k_jungle_hpairs, k_jungle_edges};
        static const TileRules desert{k_desert_t1, k_desert_t2, k_desert_t0, k_desert_groups, k_desert_candidates, k_desert_hpairs, k_desert_edges};
        static const TileRules ice{k_ice_t1, k_ice_t2, k_ice_t0, k_ice_groups, k_ice_candidates, k_ice_hpairs, k_ice_edges};
        static const TileRules twilight{k_twilight_t1, k_twilight_t2, k_twilight_t0, k_twilight_groups, k_twilight_candidates, k_twilight_hpairs, k_twilight_edges};
        static const TileRules* all[8] = {&badlands, &platform, &install, &ashworld, &jungle, &desert, &ice, &twilight};
        return all[era & 7];
    }

    // The piece key of a group: CV5 type, left, top, right and bottom edge, index parity. 0 if unknown.
    uint64_t pieceKey(int g) const {
        if (g < 0 || size_t(g) >= groups.size() || size_t(g) >= edges.size() || !edges[g].art) return 0;
        const TileGroupInfo& gi = groups[g];
        const TileEdges& e = edges[g];
        return (uint64_t(e.type) << 52) | (uint64_t(gi.leftEdge) << 39) | (uint64_t(e.topEdge) << 26) |
               (uint64_t(gi.rightEdge) << 13) | (uint64_t(e.bottomEdge) << 1) | uint64_t(g & 1) | (uint64_t(1) << 63);
    }
    // Groups with the same piece key as g (g included).
    std::span<const std::pair<uint64_t, uint16_t>> siblingsOf(int g) const {
        static std::mutex m;
        {
            std::lock_guard lock(m);
            if (siblings.empty()) {
                for (size_t i = 0; i < groups.size() && i < 1024; ++i)
                    if (uint64_t k = pieceKey(int(i))) siblings.push_back({k, uint16_t(i)});
                std::sort(siblings.begin(), siblings.end());
            }
        }
        const uint64_t k = pieceKey(g);
        if (!k) return {};
        auto lo = std::lower_bound(siblings.begin(), siblings.end(), std::pair<uint64_t, uint16_t>{k, uint16_t(0)});
        auto hi = std::upper_bound(siblings.begin(), siblings.end(), std::pair<uint64_t, uint16_t>{k, uint16_t(0xFFFF)});
        return {lo, hi};
    }

    static uint64_t key(int parity, int half, uint16_t left, uint16_t right, uint16_t ctx) {
        return (uint64_t(parity) << 63) | (uint64_t(half) << 62) | (uint64_t(left) << 44) | (uint64_t(right) << 26) |
               ctx;
    }
    static int find(std::span<const TileRule> t, uint64_t k) {
        auto it = std::lower_bound(t.begin(), t.end(), k, [](const TileRule& r, uint64_t v) { return r.key < v; });
        return it != t.end() && it->key == k ? it->group : -1;
    }
    uint8_t stackBottom(int group) const {
        return group >= 0 && size_t(group) < groups.size() ? groups[group].stackBottom : 0;
    }
    const TileGroupInfo* info(int g) const { return g >= 0 && size_t(g) < groups.size() ? &groups[g] : nullptr; }
    // Does group g sit correctly below `above` and right of `left` (-1 = no neighbor)?
    bool fits(int g, int above, int left, int leftHalf) const {
        const TileGroupInfo* gi = info(g);
        if (!gi) return false;
        if (const TileGroupInfo* a = info(above); a && a->stackBottom != gi->stackTop) return false;
        if (const TileGroupInfo* l = info(left)) {
            uint32_t pair = (uint32_t(leftHalf) << 31) | (uint32_t(l->rightEdge) << 16) | gi->leftEdge;
            if (!std::binary_search(hpairs.begin(), hpairs.end(), pair)) return false;
        }
        return true;
    }
    // Best group for a tile: the context rule if it fits its neighbors, else the most common
    // candidate for this ISOM combination that does, else the context rule anyway.
    int choose(int parity, int half, uint16_t leftShape, uint16_t rightShape, int above, int left) const {
        int g = lookup(parity, half, leftShape, rightShape, above);
        int leftHalf = half ^ 1;  // the tile to the left is the other half (or the previous rect's right half)
        if (g >= 0 && fits(g, above, left, leftHalf)) return g;
        uint64_t k = key(parity, half, leftShape, rightShape, 0);
        auto it = std::lower_bound(candidates.begin(), candidates.end(), k,
                                   [](const TileCandidate& c, uint64_t v) { return c.key < v; });
        // Prefer fitting both neighbors, then at least the tile above (cliff stacks). When no candidate that
        // Blizzard's maps use for this rect fits, the same piece of terrain with other stack connections
        // (a CV5 sibling: same type, edges and half) can: a cliff or wall piece under a piece that Blizzard's
        // maps never put above it.
        for (int pass = 0; pass < 2; ++pass) {
            for (auto c = it; c != candidates.end() && c->key == k; ++c)
                if (fits(c->group, above, pass == 0 ? left : -1, leftHalf)) return c->group;
            for (auto c = it; c != candidates.end() && c->key == k; ++c)
                for (const auto& s : siblingsOf(c->group))
                    if (s.second != c->group && fits(s.second, above, pass == 0 ? left : -1, leftHalf)) return s.second;
        }
        return g;
    }
    // A group for an ISOM combination that Blizzard's maps do not have (terrain painted in a way StarEdit
    // never did, for example two edges of Temple close together). A tile's CV5 edges come from the
    // corner terrains of its rect's two diamonds (checked on every Blizzard map, no exceptions):
    //   even rect: left and top edge from the left diamond; right and bottom edge from the right and
    //              bottom corners of both diamonds;
    //   odd rect:  left and bottom edge from the left diamond; top and right edge from the top and right
    //              corners of both diamonds.
    // Each half is looked up in the combinations Blizzard's maps do have; the result is a CV5 group of the
    // same half with both, the one Blizzard's maps use most that fits the tiles above and to the left.
    // -1 if no group has both halves.
    int predict(const IsomTerrainTable& table, int parity, int half, uint16_t leftShape, uint16_t rightShape, int above, int left) const {
        const auto cl = table.corners(leftShape), cr = table.corners(rightShape);
        auto bKey = [&](const std::array<uint16_t, 4>& a, const std::array<uint16_t, 4>& b) {
            return parity == 0 ? std::array<uint16_t, 4>{a[2], a[3], b[2], b[3]} : std::array<uint16_t, 4>{a[1], a[2], b[1], b[2]};
        };
        const auto want = bKey(cl, cr);
        auto edgesA = [&](int g) { return parity == 0 ? std::pair{groups[g].leftEdge, edges[g].topEdge} : std::pair{groups[g].leftEdge, edges[g].bottomEdge}; };
        auto edgesB = [&](int g) { return parity == 0 ? std::pair{groups[g].rightEdge, edges[g].bottomEdge} : std::pair{edges[g].topEdge, groups[g].rightEdge}; };
        std::vector<std::pair<uint16_t, uint16_t>> setA, setB;
        const uint64_t lo = key(parity, half, 0, 0, 0), hi = key(parity, half, 0xFFFF, 0xFFFF, 0xFFFF);
        auto it = std::lower_bound(candidates.begin(), candidates.end(), lo, [](const TileCandidate& c, uint64_t v) { return c.key < v; });
        uint64_t prev = ~uint64_t(0);
        for (; it != candidates.end() && it->key <= hi; ++it) {
            if (it->key == prev) continue;  // the first (most common) group of each combination
            prev = it->key;
            const int g = it->group;
            if (size_t(g) >= groups.size() || size_t(g) >= edges.size()) continue;
            const uint16_t ls = uint16_t((it->key >> 44) & 0x3FFFF), rs = uint16_t((it->key >> 26) & 0x3FFFF);
            if (ls == leftShape) setA.push_back(edgesA(g));
            if (bKey(table.corners(ls), table.corners(rs)) == want) setB.push_back(edgesB(g));
        }
        if (setA.empty() || setB.empty()) return -1;
        std::vector<std::pair<uint64_t, int>> found;  // (-uses, group)
        for (size_t g = size_t(half); g < groups.size() && g < edges.size() && g < 1024; g += 2) {
            if (!edges[g].art) continue;
            if (std::find(setA.begin(), setA.end(), edgesA(int(g))) == setA.end()) continue;
            if (std::find(setB.begin(), setB.end(), edgesB(int(g))) == setB.end()) continue;
            found.push_back({~uint64_t(uses(int(g))), int(g)});
        }
        if (found.empty()) return -1;
        std::sort(found.begin(), found.end());
        const int leftHalf = half ^ 1;
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& f : found)
                if (fits(f.second, above, pass == 0 ? left : -1, leftHalf)) return f.second;
        return found.front().second;
    }
    // How often Blizzard's maps use a group (summed over the candidates).
    uint32_t uses(int g) const {
        static std::mutex m;
        std::lock_guard lock(m);
        if (useCounts.empty()) {
            useCounts.assign(groups.size(), 0);
            for (const auto& c : candidates)
                if (c.group < useCounts.size()) useCounts[c.group] += c.count;
        }
        return size_t(g) < useCounts.size() ? useCounts[g] : 0;
    }
    // The candidates for an ISOM combination, then their CV5 siblings (other stack connections).
    std::vector<int> candidatesAndSiblings(int parity, int half, uint16_t leftShape, uint16_t rightShape) const {
        std::vector<int> out;
        for (const auto& c : candidatesFor(parity, half, leftShape, rightShape)) out.push_back(c.group);
        const size_t n = out.size();
        for (size_t i = 0; i < n; ++i)
            for (const auto& s : siblingsOf(out[i]))
                if (std::find(out.begin(), out.end(), int(s.second)) == out.end()) out.push_back(s.second);
        return out;
    }
    // Groups Blizzard's maps use for an ISOM combination (most common first).
    std::span<const TileCandidate> candidatesFor(int parity, int half, uint16_t leftShape, uint16_t rightShape) const {
        uint64_t k = key(parity, half, leftShape, rightShape, 0);
        auto lo = std::lower_bound(candidates.begin(), candidates.end(), k, [](const TileCandidate& c, uint64_t v) { return c.key < v; });
        auto hi = lo;
        while (hi != candidates.end() && hi->key == k) ++hi;
        return {lo, hi};
    }
    // Group for a tile given the group above it (-1 for the top row).
    int lookup(int parity, int half, uint16_t left, uint16_t right, int above) const {
        // The tile above only matters when it stacks into this one (cliff pieces). Using it
        // otherwise lets hand-edited corpus patterns repeat down a column.
        uint8_t link = stackBottom(above);
        int g = link ? find(t1, key(parity, half, left, right, uint16_t(above))) : -1;
        if (g < 0) g = find(t2, key(parity, half, left, right, link));
        if (g < 0) g = find(t0, key(parity, half, left, right, 0));
        return g;
    }
};

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// Subtile for a rect: common variants weighted 10x over rare ones (as seen in the corpus).
int pickSubtile(const TileGroupInfo& info, uint32_t seed) {
    int common = std::popcount(info.commonSubtiles), rare = std::popcount(info.rareSubtiles);
    if (common + rare == 0) return 0;
    uint32_t r = hash32(seed) % uint32_t(common * 10 + rare);
    uint16_t mask = r < uint32_t(common * 10) ? info.commonSubtiles : info.rareSubtiles;
    int n = r < uint32_t(common * 10) ? int(r / 10) : int(r - common * 10);
    for (int s = 0; s < 16; ++s)
        if ((mask >> s) & 1 && n-- == 0) return s;
    return 0;
}

// Could the ISOM give group `g` to a tile of this rect (parity, half, the rect's two diamond shapes, the group of
// the tile above)? A learned candidate, or what the rules pick. False for doodad groups and for tiles placed as
// they are (rectangular terrain, N.7): the correction keeps those unless an edit changes their own rect.
// A combination Blizzard's maps do not have gives no answer: true.
bool groupFromIsom(const TileRules& rules, int g, int above, int parity, int half, uint16_t leftShape, uint16_t rightShape) {
    if (g < 0 || g >= 1024) return false;
    const auto cands = rules.candidatesFor(parity, half, leftShape, rightShape);
    if (cands.empty()) return true;
    for (const auto& c : cands)
        if (c.group == g) return true;
    // A sibling of a candidate (the same piece with other stack connections: regenerateTiles picks those so cliffs
    // stack) is the shape's too, not a tile placed by hand.
    for (const auto& c : cands)
        for (const auto& sib : rules.siblingsOf(c.group))
            if (int(sib.second) == g) return true;
    return rules.lookup(parity, half, leftShape, rightShape, above) == g;
}

}  // namespace

// --- IsomTerrainTable ----------------------------------------------------------------------

IsomTerrainTable::IsomTerrainTable(uint16_t firstTransition, std::vector<Pair> pairs, std::vector<const char*> names)
    : firstTransition_(firstTransition), pairs_(std::move(pairs)), names_(std::move(names)) {}

const IsomTerrainTable* IsomTerrainTable::forEra(uint16_t era) {
    // Derived from Blizzard's maps: the first transition id and each 14-shape
    // block's terrain pair, read from where solid diamonds meet transitions (every tileset forms one
    // tree with no cycles; badlands reproduces the table found by hand earlier). Names are StarEdit's
    // palette names, matched to ids by CV5 terrain type codes and appearance.
    // Ids that never appear as terrain have no name and are not brushes.
    static const IsomTerrainTable badlands(13, {{13, 1, 2}, {27, 3, 1}, {41, 1, 4}, {55, 1, 6}, {69, 2, 7}, {83, 1, 5}, {97, 5, 8}, {111, 1, 9}},
                                           {"Dirt", "High Dirt", "Water", "Grass", "Asphalt", "Rocky Ground", "High Grass", "Structure",
                                            "Mud"});
    static const IsomTerrainTable platform(24, {{24, 1, 2}, {38, 2, 4}, {52, 2, 8}, {66, 9, 2}, {80, 10, 2}, {94, 2, 11}, {108, 4, 12},
                                                {122, 2, 13}, {136, 2, 14}},
                                           {"Space", "Platform", "", "High Platform", "", "", "", "Solar Array", "Low Platform", "Rusty Pit",
                                            "Plating", "High Plating", "Elevated Catwalk", "Dark Platform"});
    static const IsomTerrainTable install(8, {{22, 1, 2}, {36, 2, 3}, {50, 1, 4}, {64, 2, 5}, {78, 1, 6}, {92, 7, 1}},
                                          {"Substructure", "Floor", "Roof", "Substructure Plating", "Plating", "Substructure Panels",
                                           "Bottomless Pit"});
    static const IsomTerrainTable ashworld(13, {{27, 1, 2}, {41, 2, 5}, {55, 2, 3}, {69, 5, 6}, {83, 2, 4}, {97, 5, 7}, {111, 2, 8}},
                                           {"Magma", "Dirt", "Lava", "Shale", "High Dirt", "High Lava", "High Shale", "Broken Rock"});
    // Jungle, desert, ice and twilight share one layout (same blocks, same CV5 terrain type codes).
    const std::vector<Pair> jungleLayout{{17, 1, 2},  {31, 3, 1},  {45, 1, 4},  {59, 1, 6},   {73, 4, 5},   {87, 4, 7},
                                         {101, 4, 8}, {115, 2, 9}, {129, 9, 10}, {143, 9, 11}, {157, 9, 12}, {171, 1, 13}};
    static const IsomTerrainTable jungle(17, jungleLayout,
                                         {"Dirt", "High Dirt", "Water", "Jungle", "Raised Jungle", "Rocky Ground", "Ruins", "Temple",
                                          "High Jungle", "High Ruins", "High Raised Jungle", "High Temple", "Mud"});
    static const IsomTerrainTable desert(17, jungleLayout,
                                         {"Dirt", "High Dirt", "Tar", "Sand Dunes", "Sandy Sunken Pit", "Rocky Ground", "Crags", "Compound",
                                          "High Sand Dunes", "High Crags", "High Sandy Sunken Pit", "High Compound", "Dried Mud"});
    static const IsomTerrainTable ice(17, jungleLayout,
                                      {"Snow", "High Snow", "Ice", "Dirt", "Water", "Rocky Snow", "Grass", "Outpost", "High Dirt", "High Grass",
                                       "High Water", "High Outpost", "Moguls"});
    static const IsomTerrainTable twilight(17, jungleLayout,
                                           {"Dirt", "High Dirt", "Water", "Crushed Rock", "Sunken Ground", "Crevices", "Flagstones", "Basilica",
                                            "High Crushed Rock", "High Flagstones", "High Sunken Ground", "High Basilica", "Mud"});
    static const IsomTerrainTable* all[8] = {&badlands, &platform, &install, &ashworld, &jungle, &desert, &ice, &twilight};
    return all[era & 7];
}

bool IsomTerrainTable::linked(uint16_t a, uint16_t b) const {
    for (const auto& p : pairs_)
        if ((p.a == a && p.b == b) || (p.a == b && p.b == a)) return true;
    return false;
}

std::array<uint16_t, 4> IsomTerrainTable::corners(uint16_t shape) const {
    if (isTerrain(shape)) return {shape, shape, shape, shape};
    for (const auto& p : pairs_) {
        if (shape >= p.firstShape && shape < p.firstShape + 14) {
            const auto& pat = kPairPatterns[shape - p.firstShape];
            std::array<uint16_t, 4> c;
            for (int i = 0; i < 4; ++i) c[i] = pat[i] ? p.b : p.a;
            return c;
        }
    }
    return {0, 0, 0, 0};
}

uint16_t IsomTerrainTable::shapeFor(const std::array<uint16_t, 4>& c) const {
    if (c[0] == c[1] && c[1] == c[2] && c[2] == c[3]) return isTerrain(c[0]) ? c[0] : 0;
    for (const auto& p : pairs_) {
        std::array<uint8_t, 4> pat;
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) {
            if (c[i] == p.a) pat[i] = 0;
            else if (c[i] == p.b) pat[i] = 1;
            else ok = false;
        }
        if (!ok) continue;
        for (size_t k = 0; k < kPairPatterns.size(); ++k)
            if (kPairPatterns[k] == pat) return uint16_t(p.firstShape + k);
    }
    return 0;
}

int IsomTerrainTable::distance(uint16_t a, uint16_t b) const {
    const int n = firstTransition_;
    if (distances_.empty()) {
        distances_.assign(size_t(n) * n, 1000);
        for (uint16_t i = 1; i < n; ++i)
            for (uint16_t j = 1; j < n; ++j) {
                auto p = path(i, j);
                if (!p.empty()) distances_[size_t(i) * n + j] = int(p.size()) - 1;
            }
    }
    return a < n && b < n ? distances_[size_t(a) * n + b] : 1000;
}

std::vector<uint16_t> IsomTerrainTable::path(uint16_t from, uint16_t to) const {
    if (!isTerrain(from) || !isTerrain(to)) return {};
    std::vector<int> prev(firstTransition_, -1);
    std::deque<uint16_t> queue{from};
    prev[from] = from;
    while (!queue.empty()) {
        uint16_t t = queue.front();
        queue.pop_front();
        if (t == to) break;
        for (const auto& p : pairs_) {
            uint16_t n = p.a == t ? p.b : p.b == t ? p.a : 0;
            if (n && prev[n] < 0) {
                prev[n] = t;
                queue.push_back(n);
            }
        }
    }
    if (prev[to] < 0) return {};
    std::vector<uint16_t> out{to};
    while (out.back() != from) out.push_back(uint16_t(prev[out.back()]));
    std::reverse(out.begin(), out.end());
    return out;
}

// --- IsomGrid --------------------------------------------------------------------------------

uint16_t& IsomGrid::corner(int x, int y) { return corners_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)]; }
uint16_t IsomGrid::corner(int x, int y) const { return corners_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)]; }

std::array<uint16_t, 4> IsomGrid::cornersOf(int x, int y) const {
    return {corner(x - 1, y), corner(x, y - 1), corner(x + 1, y), corner(x, y + 1)};
}

uint16_t IsomGrid::shapeAt(int x, int y) const {
    return inGrid(x, y) && !((x + y) & 1) ? shapes_[size_t(y) * (rectCols_ + 1) + x] : 0;
}

bool IsomGrid::load(const Chk& chk, std::string& error) {
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h)) {
        error = "missing DIM section";
        return false;
    }
    era_ = readEra(chk);
    table_ = IsomTerrainTable::forEra(era_);
    if (!table_) {
        error = std::string("no ISOM terrain table for tileset ") + tilesetName(era_);
        return false;
    }
    mapWidth_ = w;
    mapHeight_ = h;
    rectCols_ = w / 2 + 1;
    rectRows_ = h + 1;

    const Section* s = chk.find("ISOM");
    size_t slots = size_t(rectCols_) * rectRows_ * 4;
    if (!s || s->data.size() < slots * 2) {
        error = "ISOM section missing or too small";
        return false;
    }
    isom_.resize(slots);
    for (size_t i = 0; i < slots; ++i) isom_[i] = readU16(s->data, i * 2);

    // Diamond shapes: read from the first rect quadrant that holds them.
    shapes_.assign(size_t(rectCols_ + 1) * (rectRows_ + 1), 0);
    auto slot = [&](int rx, int ry, int k) -> int {
        if (rx < 0 || ry < 0 || rx >= rectCols_ || ry >= rectRows_) return -1;
        return isom_[(size_t(ry) * rectCols_ + rx) * 4 + k] >> 4;
    };
    for (int y = 0; y <= rectRows_; ++y)
        for (int x = (y & 1); x <= rectCols_; x += 2) {
            int v = slot(x, y, 0);                // BR quadrant: left slot
            if (v < 0) v = slot(x - 1, y - 1, 2); // TL quadrant: right slot
            if (v < 0) v = slot(x, y - 1, 0);     // TR quadrant: left slot
            if (v < 0) v = slot(x - 1, y, 1);     // BL quadrant: top slot
            shapes_[size_t(y) * (rectCols_ + 1) + x] = uint16_t(std::max(v, 0));
        }

    // Corners from shapes. Corners touched by no known shape inherit a neighbor's value.
    corners_.assign(size_t(rectCols_ + 3) * (rectRows_ + 3), 0);
    for (int y = 0; y <= rectRows_; ++y)
        for (int x = (y & 1); x <= rectCols_; x += 2) {
            auto c = table_->corners(shapeAt(x, y));
            const int pts[4][2] = {{x - 1, y}, {x, y - 1}, {x + 1, y}, {x, y + 1}};
            for (int k = 0; k < 4; ++k)
                if (c[k] && !corner(pts[k][0], pts[k][1])) corner(pts[k][0], pts[k][1]) = c[k];
        }
    for (bool changed = true; changed;) {
        changed = false;
        for (int y = -1; y <= rectRows_ + 1; ++y)
            for (int x = -1; x <= rectCols_ + 1; ++x) {
                if (!((x + y) & 1) || corner(x, y)) continue;
                const int nb[4][2] = {{x - 1, y - 1}, {x + 1, y - 1}, {x - 1, y + 1}, {x + 1, y + 1}};
                for (const auto& n : nb)
                    if (n[0] >= -1 && n[1] >= -1 && n[0] <= rectCols_ + 1 && n[1] <= rectRows_ + 1 && corner(n[0], n[1])) {
                        corner(x, y) = corner(n[0], n[1]);
                        changed = true;
                        break;
                    }
            }
    }

    diamondDirty_.assign(shapes_.size(), 0);
    dirtyRects_.assign(size_t(rectCols_) * rectRows_, 0);
    return true;
}

bool IsomGrid::rebuildFromTiles(Chk& chk, std::string& error) {
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h)) {
        error = "missing DIM section";
        return false;
    }
    const uint16_t era = readEra(chk);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    const TileRules* rules = TileRules::forEra(era);
    if (!table || !rules) {
        error = std::string("no ISOM terrain table for tileset ") + tilesetName(era);
        return false;
    }
    const TerrainLayer tiles = chk.find("TILE") ? readTerrain(chk, "TILE") : readTerrain(chk, "MTXM");
    if (tiles.tiles.size() < size_t(w) * h) {
        error = "missing tiles (TILE, MTXM)";
        return false;
    }
    const int cols = w / 2 + 1, rows = h + 1;

    // Which ISOM combinations (parity, half, left and right shape) give each tile group, and how often.
    struct Use {
        uint8_t parity, half;
        uint16_t left, right;
        double weight;
    };
    std::vector<std::vector<Use>> uses(1024);
    for (const TileCandidate& c : rules->candidates) {
        const Use u{uint8_t(c.key >> 63), uint8_t((c.key >> 62) & 1), uint16_t((c.key >> 44) & 0x3FFFF), uint16_t((c.key >> 26) & 0x3FFFF),
                    1.0 + std::log(1.0 + double(c.count))};
        if (c.group < 1024) uses[c.group].push_back(u);
        for (const auto& sib : rules->siblingsOf(c.group))  // the same piece with other stack connections
            if (sib.second != c.group && sib.second < 1024) uses[sib.second].push_back(u);
    }

    // Each rect's two tiles vote for the shapes of its two diamonds.
    const int gridW = cols + 1;
    std::vector<std::vector<std::pair<uint16_t, double>>> votes(size_t(gridW) * (rows + 1));
    auto vote = [&](int x, int y, uint16_t shape, double wgt) {
        if (x < 0 || y < 0 || x > cols || y > rows || !shape) return;
        auto& v = votes[size_t(y) * gridW + x];
        for (auto& [s, n] : v)
            if (s == shape) return void(n += wgt);
        v.push_back({shape, wgt});
    };
    for (int ry = 0; ry < h; ++ry)
        for (int rx = 0; rx < cols; ++rx) {
            const int par = (rx + ry) & 1;
            const int lx = par ? rx : rx, ly = par ? ry + 1 : ry, rxd = rx + 1, ryd = par ? ry : ry + 1;
            for (int half = 0; half < 2; ++half) {
                const int tx = rx * 2 + half;
                if (tx >= w) continue;
                const int g = tiles.tiles[size_t(ry) * w + tx] >> 4;
                if (g < 0 || g >= 1024) continue;
                for (const Use& u : uses[size_t(g)])
                    if (u.parity == par && u.half == half) vote(lx, ly, u.left, u.weight), vote(rxd, ryd, u.right, u.weight);
            }
        }

    // Corners: each takes the terrain that most of the diamonds around it give (by their voted shapes).
    const int cw = cols + 3;
    std::vector<std::vector<std::pair<uint16_t, double>>> cornerVotes(size_t(cw) * (rows + 3));
    auto cornerVote = [&](int x, int y, uint16_t t, double wgt) {
        if (x < -1 || y < -1 || x > cols + 1 || y > rows + 1 || !t) return;
        auto& v = cornerVotes[size_t(y + 1) * cw + (x + 1)];
        for (auto& [s, n] : v)
            if (s == t) return void(n += wgt);
        v.push_back({t, wgt});
    };
    for (int y = 0; y <= rows; ++y)
        for (int x = (y & 1); x <= cols; x += 2) {
            const auto& v = votes[size_t(y) * gridW + x];
            if (v.empty()) continue;
            const auto best = std::max_element(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
            const auto c = table->corners(best->first);
            const int pts[4][2] = {{x - 1, y}, {x, y - 1}, {x + 1, y}, {x, y + 1}};
            for (int k = 0; k < 4; ++k) cornerVote(pts[k][0], pts[k][1], c[k], best->second);
        }
    std::vector<uint16_t> corner(size_t(cw) * (rows + 3), 0);
    for (size_t i = 0; i < corner.size(); ++i)
        if (!cornerVotes[i].empty())
            corner[i] = std::max_element(cornerVotes[i].begin(), cornerVotes[i].end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    // Corners that no tile gave (null tiles, tiles placed by hand): a neighbor's terrain.
    for (bool changed = true; changed;) {
        changed = false;
        for (int y = -1; y <= rows + 1; ++y)
            for (int x = -1; x <= cols + 1; ++x) {
                uint16_t& c = corner[size_t(y + 1) * cw + (x + 1)];
                if (!((x + y) & 1) || c) continue;
                for (const auto& n : {std::pair{x - 1, y - 1}, std::pair{x + 1, y - 1}, std::pair{x - 1, y + 1}, std::pair{x + 1, y + 1}})
                    if (n.first >= -1 && n.second >= -1 && n.first <= cols + 1 && n.second <= rows + 1)
                        if (const uint16_t t = corner[size_t(n.second + 1) * cw + (n.first + 1)]) {
                            c = t, changed = true;
                            break;
                        }
            }
    }
    // The shape of each diamond from its corners (a combination the table does not have: the voted shape).
    std::vector<uint16_t> shapes(size_t(gridW) * (rows + 1), 0);
    auto cornerAt = [&](int x, int y) -> uint16_t { return x < -1 || y < -1 || x > cols + 1 || y > rows + 1 ? 0 : corner[size_t(y + 1) * cw + (x + 1)]; };
    for (int y = 0; y <= rows; ++y)
        for (int x = (y & 1); x <= cols; x += 2) {
            uint16_t s = table->shapeFor({cornerAt(x - 1, y), cornerAt(x, y - 1), cornerAt(x + 1, y), cornerAt(x, y + 1)});
            if (!s) {
                const auto& v = votes[size_t(y) * gridW + x];
                if (!v.empty()) s = std::max_element(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
            }
            shapes[size_t(y) * gridW + x] = s;
        }
    // The ISOM section: each rect's four slots, as encodeDirty writes them.
    Bytes data;
    data.reserve(size_t(cols) * rows * 8);
    for (int ry = 0; ry < rows; ++ry)
        for (int rx = 0; rx < cols; ++rx) {
            const int par = (rx + ry) & 1;
            int dx[4], dy[4];
            if (!par) dx[0] = dx[1] = rx, dy[0] = dy[1] = ry, dx[2] = dx[3] = rx + 1, dy[2] = dy[3] = ry + 1;
            else dx[0] = dx[3] = rx, dy[0] = dy[3] = ry + 1, dx[1] = dx[2] = rx + 1, dy[1] = dy[2] = ry;
            for (int k = 0; k < 4; ++k) {
                const uint16_t s = dx[k] <= cols && dy[k] <= rows ? shapes[size_t(dy[k]) * gridW + dx[k]] : 0;
                appendU16(data, uint16_t(s << 4 | kEdgeIds[par][k] << 1));
            }
        }
    chk.set("ISOM", std::move(data));
    return load(chk, error);
}

bool IsomGrid::create(Chk& chk, uint16_t width, uint16_t height, uint16_t era, uint16_t terrain, std::string& error) {
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    if (!table || !table->isTerrain(terrain)) {
        error = "unsupported tileset or terrain";
        return false;
    }
    Bytes dim, eraBytes, isom, tiles;
    appendU16(dim, width);
    appendU16(dim, height);
    appendU16(eraBytes, era);
    size_t rects = size_t(width / 2 + 1) * (height + 1);
    for (size_t i = 0; i < rects * 4; ++i) appendU16(isom, uint16_t(terrain << 4));
    tiles.assign(size_t(width) * height * 2, 0);
    chk.set("DIM ", std::move(dim));
    chk.set("ERA ", std::move(eraBytes));
    chk.set("ISOM", std::move(isom));
    chk.set("TILE", tiles);
    chk.set("MTXM", std::move(tiles));
    if (!regenerateTiles(chk, std::vector<uint8_t>(rects, 1), error)) return false;
    return load(chk, error);
}

void IsomGrid::markDiamondDirty(int x, int y) {
    if (inGrid(x, y) && !((x + y) & 1)) diamondDirty_[size_t(y) * (rectCols_ + 1) + x] = 1;
}

bool IsomGrid::paint(int x, int y, uint16_t terrain, int brushSize) {
    if (!table_ || !table_->isTerrain(terrain) || ((x + y) & 1)) return false;
    brushSize = std::max(brushSize, 1);
    // Diamond lattice coords: a = (x + y) / 2, b = (x - y) / 2. Diamond (a, b) has corners at
    // corner-lattice coords (u, v) = (a-1, b), (a-1, b+1), (a, b+1), (a, b) with
    // u = (x + y - 1) / 2, v = (x - y + 1) / 2, so a square of diamonds is a square of corners.
    // StarEdit's footprint (starEditBrushDiamonds): an even size grows down and left, toward a + and b -.
    int a = (x + y) / 2, b = (x - y) / 2;
    int a0 = a - (brushSize - 1) / 2, a1 = a0 + brushSize - 1;
    int b0 = b - brushSize / 2, b1 = b0 + brushSize - 1;
    std::vector<CornerValue> locked;
    for (int u = a0 - 1; u <= a1; ++u)
        for (int v = b0; v <= b1 + 1; ++v) {
            int cx = u + v, cy = u - v + 1;  // inverse of the (u, v) mapping
            if (isCorner(cx, cy)) locked.push_back({cx, cy, terrain});
        }
    return apply(locked);
}

bool IsomGrid::apply(const std::vector<CornerValue>& locked) {
    if (!table_ || locked.empty()) return false;
    for (const auto& c : locked)
        if (!table_->isTerrain(c.terrain)) return false;
    const size_t n = corners_.size();
    if (source_.size() != n) source_.assign(n, 0);
    std::fill(source_.begin(), source_.end(), 0);
    locked_.assign(n, 0);
    for (const auto& c : locked)
        if (isCorner(c.x, c.y)) locked_[size_t(c.y + 1) * (rectCols_ + 3) + (c.x + 1)] = 1;
    settleFrom(locked, true);
    lastTerrain_ = locked.front().terrain;
    repairUntileable();
    return true;
}

// Sets the given corners, then works outward breadth-first: each corner takes the terrain closest to
// its old one that is within one tree step of every already-settled neighbor; only changed corners
// keep the wave going. `markSource` records the edit's terrain on every corner it changes.
void IsomGrid::settleFrom(const std::vector<CornerValue>& seeds, bool markSource) {
    const size_t n = corners_.size();
    std::vector<uint8_t> processed(n, 0);
    auto idx = [&](int x, int y) { return size_t(y + 1) * (rectCols_ + 3) + (x + 1); };
    // Corners sharing a diamond: (+-1, +-1), (+-2, 0), (0, +-2).
    static const int nb[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}};

    std::vector<std::pair<int, int>> queue;
    for (const auto& c : seeds) {
        if (!isCorner(c.x, c.y)) continue;
        if (corner(c.x, c.y) != c.terrain) setCorner(c.x, c.y, c.terrain);
        processed[idx(c.x, c.y)] = 1;
        if (markSource) sourceAt(c.x, c.y) = c.terrain;
        queue.push_back({c.x, c.y});
    }
    const uint16_t terrains = table_->terrainCount();
    for (size_t head = 0; head < queue.size(); ++head) {
        auto [x, y] = queue[head];
        for (const auto& d : nb) {
            int cx = x + d[0], cy = y + d[1];
            if (!isCorner(cx, cy) || processed[idx(cx, cy)]) continue;
            processed[idx(cx, cy)] = 1;
            uint16_t settled[8];
            int ns = 0;
            for (const auto& e : nb) {
                int sx = cx + e[0], sy = cy + e[1];
                if (isCorner(sx, sy) && processed[idx(sx, sy)] && !(sx == cx && sy == cy)) settled[ns++] = corner(sx, sy);
            }
            uint16_t old = corner(cx, cy);
            auto worst = [&](uint16_t t) {
                int m = 0;
                for (int i = 0; i < ns; ++i) m = std::max(m, table_->distance(t, settled[i]));
                return m;
            };
            if (worst(old) <= 1) continue;
            uint16_t best = old;
            int bestWorst = 1 << 20, bestDist = 1 << 20;
            for (uint16_t t = 1; t <= terrains; ++t) {
                if (!table_->isTerrain(t)) continue;
                // Prefer terrains compatible with every settled neighbor, then the closest to the old one.
                int w = worst(t), dd = table_->distance(t, old);
                int key = w <= 1 ? 0 : w;
                if (key < bestWorst || (key == bestWorst && dd < bestDist)) best = t, bestWorst = key, bestDist = dd;
            }
            if (best == old) continue;
            setCorner(cx, cy, best);
            if (markSource) sourceAt(cx, cy) = sourceAt(x, y);
            queue.push_back({cx, cy});
        }
    }
}

void IsomGrid::cornerAtPixel(int px, int py, int& x, int& y) const {
    // Corners sit on rect-grid points (64 px x 32 px) with x + y odd.
    double fx = px / 64.0, fy = py / 32.0;
    int bx = int(std::floor(fx)), by = int(std::floor(fy));
    double best = 1e9;
    x = bx, y = by + 1;
    for (int cy = by - 1; cy <= by + 2; ++cy)
        for (int cx = bx - 1; cx <= bx + 2; ++cx) {
            if (!((cx + cy) & 1)) continue;
            double d = std::abs(fx - cx) + std::abs(fy - cy);
            if (d < best) best = d, x = cx, y = cy;
        }
}

std::vector<std::pair<int, int>> IsomGrid::brushCorners(int px, int py, const Brush& brush) const {
    std::vector<std::pair<int, int>> out;
    if (brush.starEdit()) {  // StarEdit's brush: the corners of its diamonds (the core paints those); else between them
        for (auto [dx, dy] : starEditBrushAt(px, py, int(brush.size)))
            for (auto [cx, cy] : {std::pair{dx - 1, dy}, {dx, dy - 1}, {dx + 1, dy}, {dx, dy + 1}})
                if (isCorner(cx, cy) && std::find(out.begin(), out.end(), std::make_pair(cx, cy)) == out.end()) out.push_back({cx, cy});
        if (!out.empty()) return out;
    }
    const double w = std::max(0.5, double(brush.size)) * 64, h = std::max(0.5, double(brush.height())) * 32;  // half extents in pixels
    int x0 = int(std::floor((px - w) / 64)) - 1, x1 = int(std::ceil((px + w) / 64)) + 1;
    int y0 = int(std::floor((py - h) / 32)) - 1, y1 = int(std::ceil((py + h) / 32)) + 1;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!isCorner(x, y)) continue;
            double dx = std::abs(x * 64.0 - px) / w, dy = std::abs(y * 32.0 - py) / h;
            bool in = brush.shape == Brush::Diamond ? dx + dy <= 1.0001
                    : brush.shape == Brush::Square  ? dx <= 1.0001 && dy <= 1.0001
                                                    : dx * dx + dy * dy <= 1.0001;
            if (in) out.push_back({x, y});
        }
    if (out.empty()) {
        int x, y;
        cornerAtPixel(px, py, x, y);
        if (isCorner(x, y)) out.push_back({x, y});
    }
    return out;
}

std::vector<std::pair<int, int>> IsomGrid::floodCorners(int px, int py) const {
    std::vector<std::pair<int, int>> out;
    int sx, sy;
    cornerAtPixel(px, py, sx, sy);
    if (!isCorner(sx, sy)) return out;
    uint16_t t = corner(sx, sy);
    std::vector<uint8_t> seen(corners_.size(), 0);
    auto idx = [&](int x, int y) { return size_t(y + 1) * (rectCols_ + 3) + (x + 1); };
    // Corners are connected along diamond edges: (+-1, +-1).
    static const int nb[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    out.push_back({sx, sy});
    seen[idx(sx, sy)] = 1;
    for (size_t head = 0; head < out.size(); ++head) {
        auto [x, y] = out[head];
        for (const auto& d : nb) {
            int cx = x + d[0], cy = y + d[1];
            if (!isCorner(cx, cy) || seen[idx(cx, cy)] || corner(cx, cy) != t) continue;
            seen[idx(cx, cy)] = 1;
            out.push_back({cx, cy});
        }
    }
    return out;
}

std::vector<std::pair<int, int>> IsomGrid::cornersInRect(int left, int top, int right, int bottom) const {
    std::vector<std::pair<int, int>> out;
    for (int y = std::max(-1, top / 32 - 1); y <= std::min(rectRows_ + 1, bottom / 32 + 1); ++y)
        for (int x = std::max(-1, left / 64 - 1); x <= std::min(rectCols_ + 1, right / 64 + 1); ++x)
            if (isCorner(x, y) && x * 64 >= left && x * 64 < right && y * 32 >= top && y * 32 < bottom) out.push_back({x, y});
    return out;
}

uint16_t IsomGrid::sourceNear(int rx, int ry) const {
    if (rx < 0 || ry < 0 || rx >= rectCols_ || ry >= rectRows_ || source_.empty()) return lastTerrain_;
    int par = (rx + ry) & 1;
    int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
    const int pts[6][2] = {{ax - 1, ay}, {ax, ay - 1}, {ax + 1, ay}, {ax, ay + 1}, {bx + 1, by}, {bx, par ? by - 1 : by + 1}};
    for (const auto& p : pts)
        if (isCorner(p[0], p[1]) && sourceAt(p[0], p[1])) return sourceAt(p[0], p[1]);
    // Look one step further out (the edit may have stopped just short of this rect).
    for (const auto& p : pts)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                if (isCorner(p[0] + dx, p[1] + dy) && sourceAt(p[0] + dx, p[1] + dy)) return sourceAt(p[0] + dx, p[1] + dy);
    return lastTerrain_;
}

void IsomGrid::setCorner(int x, int y, uint16_t terrain) {
    corner(x, y) = terrain;
    const int centers[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
    for (const auto& c : centers) {
        if (!inGrid(c[0], c[1])) continue;
        markDiamondDirty(c[0], c[1]);
        size_t i = size_t(c[1]) * (rectCols_ + 1) + c[0];
        if (uint16_t s = table_->shapeFor(cornersOf(c[0], c[1]))) shapes_[i] = s;
    }
}

bool IsomGrid::rectTileable(int rx, int ry) const {
    const TileRules* rules = TileRules::forEra(era_);
    if (!rules || ry >= rectRows_ - 1) return true;  // last ISOM row has no tiles
    int par = (rx + ry) & 1;
    uint16_t left = par ? shapeAt(rx, ry + 1) : shapeAt(rx, ry);
    uint16_t right = par ? shapeAt(rx + 1, ry) : shapeAt(rx + 1, ry + 1);
    for (int half = 0; half < 2; ++half)
        if (TileRules::find(rules->t0, TileRules::key(par, half, left, right, 0)) < 0) return false;
    return true;
}

bool IsomGrid::stacksWithAbove(int rx, int ry) const {
    const TileRules* rules = TileRules::forEra(era_);
    if (!rules || ry <= 0 || ry >= rectRows_ - 1 || rx < 0 || rx >= rectCols_) return true;
    auto slots = [&](int x, int y, uint16_t& l, uint16_t& r) {
        int par = (x + y) & 1;
        l = par ? shapeAt(x, y + 1) : shapeAt(x, y);
        r = par ? shapeAt(x + 1, y) : shapeAt(x + 1, y + 1);
        return par;
    };
    uint16_t al, ar, bl, br;
    int pa = slots(rx, ry - 1, al, ar), pb = slots(rx, ry, bl, br);
    for (int half = 0; half < 2; ++half) {
        bool any = false;
        for (const auto& ca : rules->candidatesFor(pa, half, al, ar)) {
            for (const auto& cb : rules->candidatesFor(pb, half, bl, br))
                if (rules->stackBottom(ca.group) == (rules->info(cb.group) ? rules->info(cb.group)->stackTop : 0)) {
                    any = true;
                    break;
                }
            if (any) break;
        }
        if (!any) return false;
    }
    return true;
}

bool IsomGrid::fixRectMinimal(int rx, int ry, uint16_t prefer, bool stacks, bool grow) {
    if (rx < 0 || ry < 0 || rx >= rectCols_ || ry >= rectRows_ || !table_) return false;
    const int par = (rx + ry) & 1;
    const int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
    const int pts[6][2] = {{ax - 1, ay}, {ax, ay - 1}, {ax + 1, ay}, {ax, ay + 1}, {bx + 1, by}, {bx, par ? by - 1 : by + 1}};
    auto lockedAt = [&](int x, int y) {
        return !locked_.empty() && locked_[size_t(y + 1) * (rectCols_ + 3) + (x + 1)] != 0;
    };
    // Corners that may change, and the values they may take: terrains already in the rect, plus the
    // painted one.
    std::vector<std::pair<int, int>> movable;
    std::vector<uint16_t> values;
    for (const auto& p : pts) {
        if (!isCorner(p[0], p[1])) continue;
        uint16_t t = corner(p[0], p[1]);
        if (std::find(values.begin(), values.end(), t) == values.end()) values.push_back(t);
        if (!lockedAt(p[0], p[1]) && std::find(movable.begin(), movable.end(), std::pair{p[0], p[1]}) == movable.end()) movable.push_back({p[0], p[1]});
    }
    if (prefer && table_->isTerrain(prefer) && std::find(values.begin(), values.end(), prefer) == values.end()) values.push_back(prefer);
    if (movable.empty()) return false;
    // Good: tileable, and with `stacks` also stacking with the rects above and below. Rects around
    // this one that are good now must stay so; the rect itself must become so.
    // Tileable neighbors stay tileable, and (with `stacks`) stacking ones keep stacking.
    auto stacking = [&](int x, int y) { return stacksWithAbove(x, y) && stacksWithAbove(x, y + 1); };
    auto good = [&](int x, int y) { return rectTileable(x, y) && (!stacks || stacking(x, y)); };
    std::vector<std::pair<int, int>> nearRects, nearStacking;
    for (int y = ry - 3; y <= ry + 3; ++y)  // every rect a changed corner can reach
        for (int x = rx - 3; x <= rx + 3; ++x) {
            if (x < 0 || y < 0 || x >= rectCols_ || y >= rectRows_ || (x == rx && y == ry)) continue;
            if (rectTileable(x, y)) nearRects.push_back({x, y});
            if (stacks && stacking(x, y)) nearStacking.push_back({x, y});
        }
    auto diamondsValid = [&](const std::vector<std::pair<int, int>>& changed) {
        for (auto [cx, cy] : changed)
            for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                int x = cx + dx, y = cy + dy;  // diamond centers touching this corner
                if (inGrid(x, y) && !table_->shapeFor(cornersOf(x, y))) return false;
            }
        return true;
    };
    struct Best {
        int changes = 1 << 20, toward = -1;
        std::vector<CornerValue> set;
    } best;
    std::vector<CornerValue> trial;
    std::vector<uint16_t> saved;
    // Try every set of up to three movable corners and every value combination; corners are changed in
    // place (setCorner keeps shapes current) and restored after each trial.
    const int nf = int(movable.size());
    for (int k = 1; k <= std::min(3, nf); ++k) {
        if (best.changes <= k - 1) break;
        std::vector<int> pick(static_cast<size_t>(k), 0);
        for (int i = 0; i < k; ++i) pick[size_t(i)] = i;
        while (true) {
            std::vector<int> val(size_t(k), 0);
            while (true) {
                bool noop = false;
                trial.clear();
                for (int i = 0; i < k; ++i) {
                    auto [x, y] = movable[size_t(pick[size_t(i)])];
                    uint16_t v = values[size_t(val[size_t(i)])];
                    // !grow: the painted terrain neither spreads nor shrinks (earlier strokes stay too).
                    if (v == corner(x, y) || (!grow && (v == prefer || corner(x, y) == prefer))) noop = true;
                    trial.push_back({x, y, v});
                }
                if (!noop) {
                    saved.clear();
                    std::vector<std::pair<int, int>> changed;
                    for (const auto& t : trial) saved.push_back(corner(t.x, t.y)), setCorner(t.x, t.y, t.terrain), changed.push_back({t.x, t.y});
                    bool ok = diamondsValid(changed) && good(rx, ry);
                    for (size_t r = 0; ok && r < nearRects.size(); ++r) ok = rectTileable(nearRects[r].first, nearRects[r].second);
                    for (size_t r = 0; ok && r < nearStacking.size(); ++r) ok = stacking(nearStacking[r].first, nearStacking[r].second);
                    if (ok) {
                        int toward = 0;
                        for (const auto& t : trial) toward += t.terrain == prefer;
                        if (k < best.changes || (k == best.changes && toward > best.toward)) best = {k, toward, trial};
                    }
                    for (size_t i = trial.size(); i-- > 0;) setCorner(trial[i].x, trial[i].y, saved[i]);
                }
                int i = 0;  // next value combination
                while (i < k && ++val[size_t(i)] == int(values.size())) val[size_t(i++)] = 0;
                if (i == k) break;
            }
            int i = k - 1;  // next corner subset
            while (i >= 0 && pick[size_t(i)] == nf - k + i) --i;
            if (i < 0) break;
            ++pick[size_t(i)];
            for (int j = i + 1; j < k; ++j) pick[size_t(j)] = pick[size_t(j - 1)] + 1;
        }
    }
    if (best.set.empty()) return false;
    for (const auto& t : best.set) setCorner(t.x, t.y, t.terrain);
    return true;
}

bool IsomGrid::pushRectToward(int rx, int ry, uint16_t terrain) {
    if (rx < 0 || ry < 0 || rx >= rectCols_ || ry >= rectRows_) return false;
    int par = (rx + ry) & 1;
    // Diamond centers of this rect, and its six corner points.
    int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
    const int pts[6][2] = {{ax - 1, ay}, {ax, ay - 1}, {ax + 1, ay}, {ax, ay + 1}, {bx + 1, by}, {bx, par ? by - 1 : by + 1}};
    // Move the corner(s) farthest from `terrain` (in the terrain tree) one step closer.
    size_t far = 0;
    for (const auto& p : pts) far = std::max(far, table_->path(terrain, corner(p[0], p[1])).size());
    if (far <= 1) return false;
    std::vector<CornerValue> moved;
    for (const auto& p : pts) {
        auto path = table_->path(terrain, corner(p[0], p[1]));
        if (path.size() == far) moved.push_back({p[0], p[1], path[path.size() - 2]});
    }
    // Neighbors outside this rect may not accept the new values; settle them too, or a push next to
    // unusual terrain can leave an invalid diamond behind.
    if (!moved.empty()) settleFrom(moved, false);
    return !moved.empty();
}

void IsomGrid::repairUntileable() {
    for (int pass = 0; pass < 16; ++pass) {
        bool changed = false;
        for (int ry = 0; ry < rectRows_; ++ry)
            for (int rx = 0; rx < rectCols_; ++rx) {
                int par = (rx + ry) & 1;
                int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
                auto dirtyAt = [&](int x, int y) { return inGrid(x, y) && diamondDirty_[size_t(y) * (rectCols_ + 1) + x]; };
                if (!dirtyAt(ax, ay) && !dirtyAt(bx, by)) continue;
                if (rectTileable(rx, ry)) continue;
                // The smallest fix first (keeps thin walls thin), the old push toward the painted
                // terrain only when none exists.
                changed |= fixRectMinimal(rx, ry, sourceNear(rx, ry)) || pushRect(rx, ry);
            }
        if (!changed) break;
    }
}

void diamondAtPixel(int px, int py, int& x, int& y) {
    // Rect grid points are 64 px apart horizontally and 32 px vertically.
    x = (px + 32) / 64, y = (py + 16) / 32;
    if ((x + y) & 1) {
        // Pick whichever even neighbor is nearer.
        double fx = px / 64.0 - x, fy = py / 32.0 - y;
        if (std::abs(fx) > std::abs(fy)) x += fx > 0 ? 1 : -1;
        else y += fy > 0 ? 1 : -1;
    }
}

std::vector<std::pair<int, int>> starEditBrushDiamonds(int x, int y, int extent) {
    extent = std::max(extent, 1);
    // (IsomTerrain's placeIsomTerrain: offsets brushMin .. brushMax - 1 on both diagonals, one further for an even extent)
    int lo = -(extent / 2), hi = lo + extent;
    if (extent % 2 == 0) ++lo, ++hi;
    std::vector<std::pair<int, int>> out;
    for (int ox = lo; ox < hi; ++ox)
        for (int oy = lo; oy < hi; ++oy) out.push_back({x + ox - oy, y + ox + oy});
    return out;
}

std::vector<std::pair<int, int>> starEditBrushAt(int px, int py, int extent) {
    extent = std::max(extent, 1);
    // The nearest grid point (64 px across, 32 down; with the diamond distance, rounding each axis is nearest).
    int x = int(std::lround(px / 64.0)), y = int(std::lround(py / 32.0));
    if (extent % 2 == 1) {  // centered on a diamond: the nearest one
        diamondAtPixel(px, py, x, y);
        return starEditBrushDiamonds(x, y, extent);
    }
    if (!((x + y) & 1)) {  // centered on a corner; a diamond is nearest: the nearest of its four corners
        double best = 1e9;
        int bx = x, by = y;
        for (auto [cx, cy] : {std::pair{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}}) {
            const double d = std::abs(px - cx * 64.0) / 64 + std::abs(py - cy * 32.0) / 32;
            if (d < best) best = d, bx = cx, by = cy;
        }
        x = bx, y = by;
    }
    // (an even block of starEditBrushDiamonds around diamond (x, y - 1) has its middle at the corner (x, y))
    return starEditBrushDiamonds(x, y - 1, extent);
}

bool IsomGrid::paintAtPixel(int px, int py, uint16_t terrain, int brushSize) {
    int x = 0, y = 0;
    diamondAtPixel(px, py, x, y);
    return paint(x, y, terrain, brushSize);
}

int IsomGrid::invalidDiamonds() const {
    int n = 0;
    for (int y = 1; y < rectRows_ - 1; ++y)
        for (int x = 2 - (y & 1); x < rectCols_ - 1; x += 2)
            if (!table_->shapeFor(cornersOf(x, y))) ++n;
    return n;
}

bool IsomGrid::store(Chk& chk, std::string& error) {
    std::vector<uint8_t> touched(dirtyRects_.size(), 0);  // every rect changed by this store
    for (int pass = 0;; ++pass) {
        encodeDirty();
        for (size_t i = 0; i < touched.size(); ++i) touched[i] |= dirtyRects_[i];
        writeIsom(chk);
        const TerrainLayer before = readTerrain(chk, "TILE");
        if (!regenerateTiles(chk, dirtyRects_, error)) return false;
        {  // tiles that changed beyond the edited rects (down a column, along a row) are part of the edit too
            const TerrainLayer after = readTerrain(chk, "TILE");
            for (int ty = 0; ty < mapHeight_ && size_t(ty) * mapWidth_ < after.tiles.size(); ++ty)
                for (int tx = 0; tx < mapWidth_; ++tx) {
                    const size_t i = size_t(ty) * mapWidth_ + tx;
                    if (i < before.tiles.size() && before.tiles[i] != after.tiles[i]) touched[size_t(ty) * rectCols_ + tx / 2] = 1;
                }
        }
        std::fill(diamondDirty_.begin(), diamondDirty_.end(), 0);
        std::fill(dirtyRects_.begin(), dirtyRects_.end(), 0);
        // Cliff pieces must stack: where a generated tile cannot sit under the one above, the ISOM
        // shape is one StarEdit would not produce. Widen the edit there and regenerate.
        if (pass >= 6 || !lastTerrain_) break;
        bool pushed = false;
        const auto breaks = stackBreaks(chk, touched);
        for (auto [rx, ry] : breaks) {
            // The smallest change that lets the pieces stack, on either rect; the old push toward the
            // painted terrain only if there is none (it widens thin walls).
            uint16_t src = sourceNear(rx, ry);
            // (regenerateTiles has re-picked the tiles around each piece that did not fit: what is left needs the
            // terrain to change.)
            // Only changes that do not spread the painted terrain beyond what was painted: two small cliffs
            // painted near each other must stay two small cliffs (a seam between their pieces is the
            // lesser evil than terrain nobody painted).
            if (fixRectMinimal(rx, ry, src, true, false) || fixRectMinimal(rx, ry - 1, src, true, false)) pushed = true;
            // No such change: a piece that cannot fit is a seam in the game. Let the painted terrain grow by a corner or
            // two, as StarEdit's brush does, when that is the only way (a small brush between two edges).
            else if (fixRectMinimal(rx, ry, src, true, true) || fixRectMinimal(rx, ry - 1, src, true, true)) pushed = true;
            else  // still none: a corner change in a rect next to them (the spot is too tight for these two alone)
                for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{-1, -1}, std::pair{1, -1}, std::pair{0, 1}, std::pair{0, -2}})
                    if (fixRectMinimal(rx + dx, ry + dy, src, true, true)) {
                        pushed = true;
                        break;
                    }
        }
        if (!pushed) break;
        repairUntileable();
    }
    return true;
}

void IsomGrid::encodeDirty() {
    auto dirty = [&](int x, int y) { return inGrid(x, y) && diamondDirty_[size_t(y) * (rectCols_ + 1) + x]; };
    for (int ry = 0; ry < rectRows_; ++ry)
        for (int rx = 0; rx < rectCols_; ++rx) {
            int par = (rx + ry) & 1;
            // Diamond feeding each slot (left, top, right, bottom).
            int dx[4], dy[4];
            if (!par) {
                dx[0] = dx[1] = rx, dy[0] = dy[1] = ry;
                dx[2] = dx[3] = rx + 1, dy[2] = dy[3] = ry + 1;
            } else {
                dx[0] = dx[3] = rx, dy[0] = dy[3] = ry + 1;
                dx[1] = dx[2] = rx + 1, dy[1] = dy[2] = ry;
            }
            uint16_t* q = &isom_[(size_t(ry) * rectCols_ + rx) * 4];
            for (int k = 0; k < 4; ++k) {
                if (!dirty(dx[k], dy[k])) continue;
                uint16_t v = uint16_t(shapeAt(dx[k], dy[k]) << 4 | kEdgeIds[par][k] << 1);
                if (q[k] != v) {
                    q[k] = v;
                    dirtyRects_[size_t(ry) * rectCols_ + rx] = 1;
                }
            }
        }
}

void IsomGrid::writeIsom(Chk& chk) const {
    Bytes data;
    data.reserve(isom_.size() * 2);
    for (uint16_t v : isom_) appendU16(data, v);
    // Keep any bytes beyond the expected size (unusual, but preserves the original).
    if (const Section* s = chk.find("ISOM"); s && s->data.size() > data.size())
        data.insert(data.end(), s->data.begin() + data.size(), s->data.end());
    chk.set("ISOM", std::move(data));
}

std::vector<std::pair<int, int>> IsomGrid::stackBreaks(const Chk& chk, const std::vector<uint8_t>& area) const {
    std::vector<std::pair<int, int>> out;
    const TileRules* rules = TileRules::forEra(era_);
    if (!rules) return out;
    TerrainLayer tile = readTerrain(chk, "TILE");
    // A tile placed as it is (not from the ISOM) is not the terrain's fault: a change of the terrain cannot fix it.
    auto placed = [&](int tx, int ty) {
        const size_t i = size_t(ty) * mapWidth_ + tx;
        const int rx = tx / 2;
        auto shape = [&](int k) { return uint16_t(isom_[(size_t(ty) * rectCols_ + rx) * 4 + k] >> 4); };
        return !groupFromIsom(*rules, tile.tiles[i] >> 4, ty > 0 ? tile.tiles[i - mapWidth_] >> 4 : -1, (rx + ty) & 1, tx & 1, shape(0), shape(2));
    };
    for (int ty = 1; ty < mapHeight_; ++ty)
        for (int tx = 0; tx < mapWidth_; ++tx) {
            int rx = tx / 2;
            // Only look at the edited area (and the row just below it).
            bool near = area[size_t(ty) * rectCols_ + rx] || area[size_t(ty - 1) * rectCols_ + rx];
            if (!near) continue;
            if (placed(tx, ty - 1) || placed(tx, ty)) continue;
            const TileGroupInfo* a = rules->info(tile.tiles[size_t(ty - 1) * mapWidth_ + tx] >> 4);
            const TileGroupInfo* b = rules->info(tile.tiles[size_t(ty) * mapWidth_ + tx] >> 4);
            if (a && b && a->stackBottom != b->stackTop) out.push_back({rx, ty});
        }
    // Horizontal neighbors whose edge codes never meet in Blizzard maps.
    for (int ty = 0; ty < mapHeight_; ++ty)
        for (int tx = 0; tx + 1 < mapWidth_; ++tx) {
            if (!area[size_t(ty) * rectCols_ + tx / 2] && !area[size_t(ty) * rectCols_ + (tx + 1) / 2]) continue;
            int l = tile.tiles[size_t(ty) * mapWidth_ + tx] >> 4, r = tile.tiles[size_t(ty) * mapWidth_ + tx + 1] >> 4;
            if (placed(tx, ty) || placed(tx + 1, ty)) continue;
            if (rules->info(l) && rules->info(r) && !rules->fits(r, -1, l, tx & 1)) {
                out.push_back({tx / 2, ty + 1});  // +1: the caller also pushes the rect above
                out.push_back({(tx + 1) / 2, ty + 1});
            }
        }
    return out;
}

// --- TILE regeneration -------------------------------------------------------------------------

int solidTileGroup(uint16_t era, uint16_t terrain, int half) {
    const TileRules* rules = TileRules::forEra(era);
    return rules ? TileRules::find(rules->t0, TileRules::key(0, half & 1, terrain, terrain, 0)) : -1;
}

int countTileMisfits(const Chk& chk, std::vector<std::pair<int, int>>* where) {
    const TileRules* rules = TileRules::forEra(readEra(chk));
    if (!rules) return 0;
    const TerrainLayer t = readTerrain(chk, "TILE");
    int n = 0;
    for (int y = 0; y < t.height; ++y)
        for (int x = 0; x < t.width; ++x) {
            const int g = t.tiles[size_t(y) * t.width + x] >> 4;
            if (g >= 1024 || !rules->info(g)) continue;  // doodads: no rule
            const int above = y > 0 ? t.tiles[size_t(y - 1) * t.width + x] >> 4 : -1, left = x > 0 ? t.tiles[size_t(y) * t.width + x - 1] >> 4 : -1;
            if (rules->fits(g, above < 1024 ? above : -1, left < 1024 ? left : -1, (x & 1) ^ 1)) continue;
            ++n;
            if (where) where->push_back({x, y});
        }
    return n;
}

TileRegenStats& tileRegenStats() {
    static TileRegenStats s;
    return s;
}

bool regenerateTiles(Chk& chk, const std::vector<uint8_t>& dirtyRects, std::string& error) {
    uint16_t w = 0, h = 0;
    if (!readDimensions(chk, w, h)) {
        error = "missing DIM section";
        return false;
    }
    uint16_t era = readEra(chk);
    const TileRules* rules = TileRules::forEra(era);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    if (!rules) {
        error = std::string("no tile rules for tileset ") + tilesetName(era);
        return false;
    }
    const Section* isomSec = chk.find("ISOM");
    int rectCols = w / 2 + 1, rectRows = h + 1;
    if (!isomSec || isomSec->data.size() < size_t(rectCols) * rectRows * 8) {
        error = "ISOM section missing or too small";
        return false;
    }
    if (dirtyRects.size() != size_t(rectCols) * rectRows) {
        error = "dirty rect mask has the wrong size";
        return false;
    }

    TerrainLayer tile = readTerrain(chk, "TILE");
    TerrainLayer mtxm = readTerrain(chk, "MTXM");
    auto slotShape = [&](int rx, int ry, int k) {
        return uint16_t(readU16(isomSec->data, ((size_t(ry) * rectCols + rx) * 4 + k) * 2) >> 4);
    };

    // A tile placed as it is (rectangular terrain, N.7) stays unless the edit changed the ISOM of its own rect.
    auto placed = [&](int tx, int ty) {
        const size_t i = size_t(ty) * w + tx;
        const int rx = tx / 2;
        return !groupFromIsom(*rules, tile.tiles[i] >> 4, ty > 0 ? tile.tiles[i - w] >> 4 : -1, (rx + ty) & 1, tx & 1, slotShape(rx, ty, 0),
                              slotShape(rx, ty, 2));
    };

    std::vector<uint8_t> touched(size_t(w) * h, 0);  // tiles this call changed (the repair below looks at and next to them)
    std::vector<uint8_t> pending(w, 0);  // column still differs from the existing tiles
    std::vector<uint8_t> changedInRow(w, 0);
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            int rx = tx / 2;
            bool dirty = dirtyRects[size_t(ty) * rectCols + rx] != 0;
            bool leftChanged = tx > 0 && changedInRow[tx - 1];
            changedInRow[tx] = 0;
            if (!dirty && !pending[tx] && !leftChanged) continue;
            if (!dirty && placed(tx, ty)) {
                pending[tx] = 0;
                continue;
            }
            size_t i = size_t(ty) * w + tx;
            int above = ty > 0 ? tile.tiles[i - w] >> 4 : -1;
            int left = tx > 0 ? tile.tiles[i - 1] >> 4 : -1;
            int current = tile.tiles[i] >> 4;
            // A clean tile next to a changed one only changes if it no longer fits.
            if (!dirty && !pending[tx] && rules->fits(current, above, left, (tx & 1) ^ 1)) continue;
            int g = rules->choose((rx + ty) & 1, tx & 1, slotShape(rx, ty, 0), slotShape(rx, ty, 2), above, left);
            if (g >= 0) ++tileRegenStats().chosen;
            else if (table && (g = rules->predict(*table, (rx + ty) & 1, tx & 1, slotShape(rx, ty, 0), slotShape(rx, ty, 2), above, left)) >= 0)
                ++tileRegenStats().predicted;
            if (g < 0) {
                ++tileRegenStats().unknown;
                pending[tx] = 0;  // unknown combination: keep the existing tile
                continue;
            }
            const TileGroupInfo& info = size_t(g) < rules->groups.size() ? rules->groups[g] : rules->groups[0];
            const uint16_t avail = info.commonSubtiles | info.rareSubtiles;
            auto has = [&](int sub) { return ((avail >> sub) & 1) != 0; };
            // Subtile (variant), as StarEdit picks it in Blizzard's maps: a tile stacked under another
            // (cliff faces) repeats the variant above it (99.3%); the right tile of a rect repeats the
            // left one (99.5%); otherwise a weighted random variant per rect.
            int sub = -1;
            if (above >= 0 && size_t(above) < rules->groups.size() && rules->groups[above].stackBottom && info.stackTop &&
                has(tile.tiles[i - w] & 15))
                sub = tile.tiles[i - w] & 15;
            else if ((tx & 1) && has(tile.tiles[i - 1] & 15) && (tile.tiles[i - 1] >> 4) < 1024)
                sub = tile.tiles[i - 1] & 15;
            if (sub < 0) sub = pickSubtile(info, uint32_t(ty) * 65536u + uint32_t(rx));
            if (!has(sub)) sub = 0;
            uint16_t value = uint16_t(g << 4 | sub);
            if (tile.tiles[i] == value) {
                pending[tx] = 0;
                continue;
            }
            changedInRow[tx] = 1;
            tile.tiles[i] = value;
            mtxm.tiles[i] = value;
            touched[i] = 1;
            pending[tx] = 1;
        }
    }

    // The pass above picks each tile knowing only the tiles above and to the left, so a cliff piece can
    // end up unable to stack onto the piece chosen above it although their ISOM shapes have pieces that
    // do (two small cliffs one above the other). Re-pick that pair from their candidates so they stack,
    // keeping their other neighbors, instead of changing the terrain to suit the tiles.
    {
        auto group = [&](int tx, int ty) { return tx >= 0 && ty >= 0 && tx < w && ty < h ? int(tile.tiles[size_t(ty) * w + tx] >> 4) : -1; };
        auto real = [&](int g) { return g >= 0 && g < 1024 ? g : -1; };  // doodads do not constrain
        auto keyOf = [&](int tx, int ty, int& par, uint16_t& l, uint16_t& r) {
            int rx = tx / 2;
            par = (rx + ty) & 1, l = slotShape(rx, ty, 0), r = slotShape(rx, ty, 2);
        };
        auto setTile = [&](int tx, int ty, int g) {
            size_t i = size_t(ty) * w + tx;
            const TileGroupInfo* gi = rules->info(g);
            const uint16_t avail = gi ? gi->commonSubtiles | gi->rareSubtiles : 1;
            int sub = tile.tiles[i] & 15;
            const int a = group(tx, ty - 1);
            if (ty > 0 && rules->info(a) && rules->info(a)->stackBottom && gi && gi->stackTop) sub = tile.tiles[i - w] & 15;  // stacked: as above
            if (!((avail >> sub) & 1)) sub = 0;
            tile.tiles[i] = mtxm.tiles[i] = uint16_t(g << 4 | sub);
            touched[i] = 1;
        };
        for (int ty = 1; ty < h; ++ty)
            for (int tx = 0; tx < w; ++tx) {
                const int rx = tx / 2;
                const bool dirtyA = dirtyRects[size_t(ty - 1) * rectCols + rx], dirtyB = dirtyRects[size_t(ty) * rectCols + rx];
                if (!dirtyA && !dirtyB) continue;
                if ((!dirtyA && placed(tx, ty - 1)) || (!dirtyB && placed(tx, ty))) continue;  // placed tiles stay
                const int ga = group(tx, ty - 1), gb = group(tx, ty);
                const TileGroupInfo *ia = rules->info(ga), *ib = rules->info(gb);
                if (!ia || !ib || ia->stackBottom == ib->stackTop) continue;
                int pa, pb;
                uint16_t la, ra, lb, rb;
                keyOf(tx, ty - 1, pa, la, ra), keyOf(tx, ty, pb, lb, rb);
                const int aboveA = real(group(tx, ty - 2)), leftA = real(group(tx - 1, ty - 1)), rightA = real(group(tx + 1, ty - 1));
                const int leftB = real(group(tx - 1, ty)), rightB = real(group(tx + 1, ty)), belowB = real(group(tx, ty + 1));
                bool done = false;
                for (const int ca : rules->candidatesAndSiblings(pa, tx & 1, la, ra)) {
                    if (!rules->fits(ca, ty >= 2 ? aboveA : -1, leftA, (tx & 1) ^ 1)) continue;
                    if (rightA >= 0 && !rules->fits(rightA, -1, ca, tx & 1)) continue;
                    for (const int cb : rules->candidatesAndSiblings(pb, tx & 1, lb, rb)) {
                        if (!rules->fits(cb, ca, leftB, (tx & 1) ^ 1)) continue;
                        if (rightB >= 0 && !rules->fits(rightB, -1, cb, tx & 1)) continue;
                        if (belowB >= 0 && !rules->fits(belowB, cb, -1, 0)) continue;
                        setTile(tx, ty - 1, ca);
                        setTile(tx, ty, cb);
                        // Cliff faces further down repeat this variant.
                        for (int y = ty + 1; y < h; ++y) {
                            const TileGroupInfo *up = rules->info(group(tx, y - 1)), *me = rules->info(group(tx, y));
                            if (!up || !me || !up->stackBottom || !me->stackTop) break;
                            if (!dirtyRects[size_t(y) * rectCols + rx] && placed(tx, y)) break;
                            setTile(tx, y, group(tx, y));
                        }
                        done = true;
                        break;
                    }
                    if (done) break;
                }
            }

        // Then every tile in and around the edit that still does not fit the tile above or to its left (seen as a
        // glitch; Blizzard's maps have none): re-pick it, else its left or upper neighbor, else it with its left
        // neighbor, from the pieces Blizzard's maps use for their ISOM shapes, so that every neighbor of the changed
        // tiles fits too. A small brush makes such spots (two edges close together).
        auto rectDirty = [&](int tx, int ty) {
            if (tx < 0 || ty < 0 || tx >= w || ty >= h) return false;
            return dirtyRects[size_t(ty) * rectCols + tx / 2] != 0;
        };
        auto changedAt = [&](int tx, int ty) {
            return tx >= 0 && ty >= 0 && tx < w && ty < h && (touched[size_t(ty) * w + tx] || rectDirty(tx, ty));
        };
        auto near = [&](int tx, int ty) {  // the tile, or one next to it, changed (in the edit, or after it down a column or along a row)
            return changedAt(tx, ty) || changedAt(tx - 1, ty) || changedAt(tx + 1, ty) || changedAt(tx, ty - 1) || changedAt(tx, ty + 1);
        };
        auto mayChange = [&](int tx, int ty) {  // placed tiles (rectangular terrain) stay unless their own rect changed
            return tx >= 0 && ty >= 0 && tx < w && ty < h && real(group(tx, ty)) >= 0 && (rectDirty(tx, ty) || !placed(tx, ty));
        };
        // Group g at (tx, ty) fits the tiles around it (with `skip` taken as it will be: -2 none).
        auto fitsAround = [&](int tx, int ty, int g) {
            const int above = real(group(tx, ty - 1)), left = real(group(tx - 1, ty)), right = real(group(tx + 1, ty)), below = real(group(tx, ty + 1));
            if (!rules->fits(g, above, left, (tx & 1) ^ 1)) return false;
            if (right >= 0 && !rules->fits(right, real(group(tx + 1, ty - 1)), g, tx & 1)) return false;
            if (below >= 0 && !rules->fits(below, g, real(group(tx - 1, ty + 1)), (tx & 1) ^ 1)) return false;
            return true;
        };
        auto misfit = [&](int tx, int ty) {
            const int g = real(group(tx, ty));
            return g >= 0 && !rules->fits(g, real(group(tx, ty - 1)), real(group(tx - 1, ty)), (tx & 1) ^ 1);
        };
        auto candidatesAt = [&](int tx, int ty) {
            int par;
            uint16_t l, r;
            keyOf(tx, ty, par, l, r);
            std::vector<int> c = rules->candidatesAndSiblings(par, tx & 1, l, r);
            if (c.empty() && table)  // a shape Blizzard's maps lack: the predicted piece
                if (const int p = rules->predict(*table, par, tx & 1, l, r, real(group(tx, ty - 1)), real(group(tx - 1, ty))); p >= 0) c.push_back(p);
            return c;
        };
        auto repick = [&](int tx, int ty) {  // one tile: the most common piece that fits all around
            if (!mayChange(tx, ty)) return false;
            const int was = group(tx, ty);
            for (const int c : candidatesAt(tx, ty))
                if (c != was && fitsAround(tx, ty, c)) return setTile(tx, ty, c), true;
            return false;
        };
        // A block of bw x bh tiles at (x0, y0): pieces for each tile that may change (row by row, from its ISOM
        // candidates, the one it has first) so that every tile in the block fits the tiles above and to its left, and
        // the tiles right of and under the block still fit. True: set. A few thousand tries at most.
        auto solveBlock = [&](int x0, int y0, int bw, int bh) {
            struct Cell {
                int x, y;
                std::vector<int> cands;
                int keep;
            };
            std::vector<Cell> cells;
            for (int y = y0; y < y0 + bh; ++y)
                for (int x = x0; x < x0 + bw; ++x) {
                    if (!mayChange(x, y)) continue;
                    Cell c{x, y, {group(x, y)}, group(x, y)};
                    for (const int g : candidatesAt(x, y))
                        if (g != c.keep) c.cands.push_back(g);
                    cells.push_back(std::move(c));
                }
            if (cells.empty()) return false;
            auto inBlock = [&](int x, int y) { return x >= x0 && y >= y0 && x < x0 + bw && y < y0 + bh; };
            auto cellOk = [&](int x, int y) {  // the tile fits above and left; the tiles outside the block right and below fit it
                const int g = real(group(x, y));
                if (g < 0) return true;
                if (!rules->fits(g, real(group(x, y - 1)), real(group(x - 1, y)), (x & 1) ^ 1)) return false;
                return true;
            };
            auto edgesOk = [&]() {
                for (const Cell& c : cells) {
                    const int g = real(group(c.x, c.y));
                    if (!inBlock(c.x + 1, c.y)) {
                        const int r = real(group(c.x + 1, c.y));
                        if (r >= 0 && !rules->fits(r, real(group(c.x + 1, c.y - 1)), g, c.x & 1)) return false;
                    }
                    if (!inBlock(c.x, c.y + 1)) {
                        const int b2 = real(group(c.x, c.y + 1));
                        if (b2 >= 0 && !rules->fits(b2, g, real(group(c.x - 1, c.y + 1)), (c.x & 1) ^ 1)) return false;
                    }
                }
                return true;
            };
            int tries = 0;
            std::function<bool(size_t)> place = [&](size_t i) {
                if (i == cells.size()) return edgesOk();
                Cell& c = cells[i];
                for (const int g : c.cands) {
                    if (++tries > 4000) return false;
                    setTile(c.x, c.y, g);
                    if (cellOk(c.x, c.y) && place(i + 1)) return true;
                }
                setTile(c.x, c.y, c.keep);
                return false;
            };
            if (place(0)) return true;
            for (const Cell& c : cells) setTile(c.x, c.y, c.keep);
            return false;
        };
        for (int pass = 0; pass < 4; ++pass) {
            bool changed = false;
            for (int ty = 0; ty < h; ++ty)
                for (int tx = 0; tx < w; ++tx) {
                    if (!near(tx, ty) || !misfit(tx, ty)) continue;
                    if (repick(tx, ty) || repick(tx - 1, ty) || repick(tx, ty - 1)) {
                        changed = true;
                        continue;
                    }
                    // Else a small block around it (2 x 2, then 3 x 2, then 3 x 3), searched for pieces that all fit.
                    if (solveBlock(tx - 1, ty - 1, 2, 2) || solveBlock(tx - 1, ty - 1, 3, 2) || solveBlock(tx - 1, ty - 1, 3, 3)) {
                        changed = true;
                        continue;
                    }
                    if (std::getenv("STARDRAFT_TILE_DEBUG")) {
                        int par;
                        uint16_t l, r;
                        keyOf(tx, ty, par, l, r);
                        std::fprintf(stderr, "misfit (%d,%d) g=%d above=%d left=%d | shapes %u/%u | cands %zu\n", tx, ty, group(tx, ty), group(tx, ty - 1),
                                     group(tx - 1, ty), l, r, candidatesAt(tx, ty).size());
                    }
                }
            if (!changed) break;
        }
    }

    auto pack = [](const TerrainLayer& t) {
        Bytes out;
        out.reserve(t.tiles.size() * 2);
        for (uint16_t v : t.tiles) appendU16(out, v);
        return out;
    };
    chk.set("TILE", pack(tile));
    chk.set("MTXM", pack(mtxm));
    return true;
}

bool tileFromIsom(const Chk& chk, int tx, int ty) { return tileFromIsom(chk, readTerrain(chk, "TILE"), tx, ty); }

bool tileFromIsom(const Chk& chk, const TerrainLayer& tile, int tx, int ty) {
    const int w = tile.width, h = tile.height;
    if (tx < 0 || ty < 0 || tx >= w || ty >= h || tile.tiles.size() < size_t(w) * h) return true;
    const TileRules* rules = TileRules::forEra(readEra(chk));
    const Section* isomSec = chk.find("ISOM");
    const int rectCols = w / 2 + 1, rx = tx / 2;
    if (!rules || !isomSec || isomSec->data.size() < size_t(rectCols) * (h + 1) * 8) return true;
    auto shape = [&](int k) { return uint16_t(readU16(isomSec->data, ((size_t(ty) * rectCols + rx) * 4 + k) * 2) >> 4); };
    const size_t i = size_t(ty) * w + tx;
    return groupFromIsom(*rules, tile.tiles[i] >> 4, ty > 0 ? tile.tiles[i - w] >> 4 : -1, (rx + ty) & 1, tx & 1, shape(0), shape(2));
}

std::vector<std::array<uint16_t, 2>> tileGroupTerrains(uint16_t era, size_t groupCount) {
    std::vector<std::array<uint16_t, 2>> out(groupCount, {0, 0});
    const TileRules* rules = TileRules::forEra(era);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    if (!rules || !table) return out;
    // How often Blizzard's maps use each group with each terrain at a corner of its rect, and in a rect of one
    // terrain only.
    std::vector<std::vector<std::pair<uint16_t, uint64_t>>> counts(groupCount), solid(groupCount);
    std::vector<uint64_t> total(groupCount, 0);
    auto add = [](std::vector<std::pair<uint16_t, uint64_t>>& v, uint16_t t, uint64_t n) {
        auto it = std::find_if(v.begin(), v.end(), [t](const auto& p) { return p.first == t; });
        if (it == v.end()) v.push_back({t, n});
        else it->second += n;
    };
    for (const auto& c : rules->candidates) {
        if (c.group >= groupCount) continue;
        const uint16_t shapes[2] = {uint16_t((c.key >> 44) & 0x3FFFF), uint16_t((c.key >> 26) & 0x3FFFF)};
        std::vector<uint16_t> seen;
        for (uint16_t s : shapes)
            for (uint16_t t : table->corners(s))
                if (t && std::find(seen.begin(), seen.end(), t) == seen.end()) seen.push_back(t);
        for (uint16_t t : seen) add(counts[c.group], t, c.count);
        if (seen.size() == 1) add(solid[c.group], seen[0], c.count);
        total[c.group] += c.count;
    }
    auto byCount = [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; };
    for (size_t g = 0; g < groupCount; ++g) {
        auto& v = counts[g];
        if (v.empty()) continue;
        // A tile of one terrain: mostly inside that terrain (its other uses are at the edges of the terrain).
        std::sort(solid[g].begin(), solid[g].end(), byCount);
        if (!solid[g].empty() && solid[g][0].second * 3 >= total[g]) {
            out[g][0] = solid[g][0].first;
            continue;
        }
        std::sort(v.begin(), v.end(), byCount);
        out[g][0] = v[0].first;
        // A second terrain when the group is a transition (used with it about as often), not a stray use.
        if (v.size() > 1 && v[1].second * 4 >= v[0].second) out[g][1] = v[1].first;
    }
    return out;
}

}  // namespace stardraft
