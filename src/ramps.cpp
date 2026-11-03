// Ramps of any width (include/stardraft/ramps.h): the kits of src/data/ramp_kits.inc.
#include "stardraft/ramps.h"

#include "stardraft/doodads.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace stardraft {

namespace {

struct RampKitData {
    int era, base;          // the tileset, the block's first doodad
    const char* group;      // the block's doodad group (Cliff, Temple Wall, Basilica...)
    int facing;             // N, NE, E, SE, S, SW, W, NW
    int mode;               // 0: the maps' corners and the corpus's edges; 1: the corners of the places the game's doodad
                            // rule takes (it is not asked on the map: a ramp there has the tiles it asks for no more);
                            // 2: the corners' layout of the spec's kit (a facing the rule takes nowhere)
    const char* low;        // its ground below ("": any but the high) and above
    const char* high;
    int start[2], step[2];  // tiles
    int tiles[4];           // kRampTiles: wall A's first, the slope's, wall B's, the end
    int doodads[3];         // the pieces' doodads (wall A, slope, wall B)
};
struct RampTileData {
    int x, y;
    uint16_t value;
};
struct RampCornerData {
    int kit, kind, px, py;
    const char* terrain;  // "": any terrain but the kit's high ground
    bool high;
};
struct RampNeighborData {
    int era, way;  // way 0: b right of a; 1: b under a
    uint16_t a, b;
};
#include "data/ramp_kits.inc"

constexpr int kKits = int(sizeof kRampKits / sizeof kRampKits[0]);
const char* const kFacingNames[kRampFacings] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

const RampKitData* kitAt(int kit) { return kit >= 0 && kit < kKits ? &kRampKits[kit] : nullptr; }

// The corners of a kit's piece (kRampCorners, by kit and piece).
const std::vector<const RampCornerData*>& cornersOf(int kit, int kind) {
    static const std::map<int, std::vector<const RampCornerData*>> index = [] {
        std::map<int, std::vector<const RampCornerData*>> m;
        for (const RampCornerData& c : kRampCorners) m[c.kit * 4 + c.kind].push_back(&c);
        return m;
    }();
    static const std::vector<const RampCornerData*> none;
    const auto i = index.find(kit * 4 + kind);
    return i == index.end() ? none : i->second;
}

// A kit the brush offers: the maps show where its pieces go (corners for each piece).
bool usable(int kit) { return !cornersOf(kit, 0).empty() && !cornersOf(kit, 1).empty() && !cornersOf(kit, 2).empty(); }

// The first usable kit of a tileset and facing: a block the maps have ramps of first (the spec's), then the others.
int firstKit(int era, int facing) {
    for (const int mode : {0, 1, 2})
        for (int k = 0; k < kKits; ++k)
            if (kRampKits[k].era == era && kRampKits[k].facing == facing && kRampKits[k].mode == mode && usable(k)) return k;
    return -1;
}

}  // namespace

const char* rampFacingName(int facing) { return facing >= 0 && facing < kRampFacings ? kFacingNames[facing] : ""; }

std::vector<int> rampKitsOf(int era, bool all) {
    std::vector<int> out;
    for (int k = 0; k < kKits; ++k)
        if (kRampKits[k].era == era && (all || usable(k))) out.push_back(k);
    return out;
}
int rampKitFacing(int kit) { return kitAt(kit) ? kitAt(kit)->facing : -1; }
int rampKitBase(int kit) { return kitAt(kit) ? kitAt(kit)->base : -1; }
std::pair<std::string, std::string> rampKitTerrains(int kit) {
    return kitAt(kit) ? std::pair<std::string, std::string>{kitAt(kit)->low, kitAt(kit)->high} : std::pair<std::string, std::string>{};
}
int rampKitDoodad(int kit, int kind) { return kitAt(kit) && kind >= 0 && kind < 3 ? kitAt(kit)->doodads[kind] : -1; }
const char* rampKitGroup(int kit) { return kitAt(kit) ? kitAt(kit)->group : ""; }
int rampKit(int era, int facing) { return firstKit(era, facing); }

bool rampsAvailable(int era) { return !rampKitsOf(era).empty(); }

std::pair<int, int> rampKitPieceSize(int kit, int kind) {
    const RampKitData* k = kitAt(kit);
    if (!k || kind < 0 || kind > 2) return {0, 0};
    int w = 0, h = 0;
    for (int i = k->tiles[kind]; i < k->tiles[kind + 1]; ++i) w = std::max(w, kRampTiles[i].x + 1), h = std::max(h, kRampTiles[i].y + 1);
    return {w, h};
}

std::vector<RampPiece> rampKitPieces(int kit, int slopes, int ax, int ay) {
    const RampKitData* k = kitAt(kit);
    if (!k) return {};
    std::vector<RampPiece> out{{0, ax, ay}};
    for (int i = 0; i < slopes; ++i) out.push_back({1, ax + k->start[0] + i * k->step[0], ay + k->start[1] + i * k->step[1]});
    out.push_back({2, ax + k->start[0] + slopes * k->step[0], ay + k->start[1] + slopes * k->step[1]});
    return out;
}

std::vector<Editor::TileAt> rampKitTiles(int kit, int slopes, int ax, int ay) {
    std::vector<Editor::TileAt> out;
    const RampKitData* k = kitAt(kit);
    if (!k) return out;
    for (const RampPiece& p : rampKitPieces(kit, slopes, ax, ay))
        for (int i = k->tiles[p.kind]; i < k->tiles[p.kind + 1]; ++i) out.push_back({p.x + kRampTiles[i].x, p.y + kRampTiles[i].y, kRampTiles[i].value});
    return out;
}

bool rampKitMatches(const Editor& ed, int kit, int slopes, int ax, int ay) {
    const std::vector<Editor::TileAt> tiles = rampKitTiles(kit, slopes, ax, ay);
    for (const Editor::TileAt& c : tiles)
        if (c.x < 0 || c.y < 0 || c.x >= ed.width() || c.y >= ed.height() || ed.terrain().tiles[size_t(c.y) * ed.width() + c.x] != c.value) return false;
    return !tiles.empty();
}

std::pair<int, int> rampPieceSize(int era, int facing, int kind) { return rampKitPieceSize(firstKit(era, facing), kind); }
std::vector<RampPiece> rampPieces(int era, int facing, int slopes, int ax, int ay) { return rampKitPieces(firstKit(era, facing), slopes, ax, ay); }
std::vector<Editor::TileAt> rampTiles(int era, int facing, int slopes, int ax, int ay) { return rampKitTiles(firstKit(era, facing), slopes, ax, ay); }
bool rampMatches(const Editor& ed, int facing, int slopes, int ax, int ay) { return rampKitMatches(ed, firstKit(ed.era() & 7, facing), slopes, ax, ay); }

namespace {

// The terrain ids of a map's tileset by name (once for each tileset, and for a mod's: its names of the game terrains, as
// "High Dirt (Dirt)", with the ground they stand on; the game terrains come first).
uint16_t terrainId(const Editor& ed, const char* name) {
    static std::map<std::tuple<int, std::string, std::string>, uint16_t> ids;
    const auto key = std::make_tuple(int(ed.era() & 7), ed.terrainName(2), std::string(name));
    auto i = ids.find(key);
    if (i == ids.end()) {
        uint16_t v = 0;
        for (uint16_t id = 1; id < 256 && !v; ++id)
            if (ed.terrainName(id) == name) v = id;
        for (uint16_t id = 1; id < 256 && !v; ++id)
            if (ed.terrainName(id).rfind(std::string(name) + " (", 0) == 0) v = id;
        i = ids.emplace(key, v).first;
    }
    return i->second;
}

// A kit on one map: the terrain id of each of its pieces' corners there (0: any but the high ground), its doodads.
struct Fit {
    const Editor& ed;
    int kit;
    const RampKitData& k;
    std::vector<std::vector<std::pair<const RampCornerData*, std::vector<uint16_t>>>> corners;  // by piece: the terrains it may be
    uint16_t high = 0;
    bool ok = true;
    Fit(const Editor& e, int kitIndex) : ed(e), kit(kitIndex), k(kRampKits[kitIndex]), corners(3) {
        high = terrainId(ed, k.high);
        ok = high != 0;
        for (int kind = 0; kind < 3; ++kind) {
            for (const RampCornerData* c : cornersOf(kit, kind)) {
                // (its terrains: names apart by "|")
                std::vector<uint16_t> ids;
                for (const char *s = c->terrain, *e = s; *s; s = *e ? e + 1 : e) {
                    e = std::strchr(s, '|');
                    if (!e) e = s + std::strlen(s);
                    if (const uint16_t id = terrainId(ed, std::string(s, e).c_str())) ids.push_back(id);
                }
                corners[size_t(kind)].push_back({c, ids});
            }
        }
    }
};

// Whether the ISOM corners under a piece (its top left at tile x, y) have the terrains of the maps' ramps' pieces.
bool pieceFits(const Fit& f, int kind, int x, int y) {
    for (const auto& [c, ids] : f.corners[size_t(kind)]) {
        const int px = 32 * x + c->px, py = 32 * y + c->py;
        if (px % 64 || py % 32 || px < 0 || py < 0 || !((px / 64 + py / 32) & 1)) return false;  // (not a corner there: off the grid)
        const uint16_t t = f.ed.terrainAt(px, py);
        if (ids.empty() ? (!t || t == f.high) : std::find(ids.begin(), ids.end(), t) == ids.end()) return false;
    }
    return !f.corners[size_t(kind)].empty();
}

// A piece is on the map and fits its corners.
bool placeFits(const Fit& f, const RampPiece& p) {
    const auto [w, h] = rampKitPieceSize(f.kit, p.kind);
    return p.x >= 0 && p.y >= 0 && p.x + w <= f.ed.width() && p.y + h <= f.ed.height() && pieceFits(f, p.kind, p.x, p.y);
}

// All the ramp's pieces fit their corners.
bool cornersFit(const Fit& f, int slopes, int ax, int ay) {
    if (!f.ok) return false;
    for (const RampPiece& p : rampKitPieces(f.kit, slopes, ax, ay))
        if (!placeFits(f, p)) return false;
    return true;
}

// A tile of a terrain group (groups below 1024; above: doodads' pieces) as all of its group's tiles: random variants.
uint16_t variant(uint16_t v) { return (v >> 4) < 1024 ? uint16_t(v & ~15) : v; }

uint64_t pairKey(int era, int way, uint16_t a, uint16_t b) { return uint64_t(era) << 40 | uint64_t(way) << 32 | uint64_t(a) << 16 | b; }

// Whether the corpus has tile b next to tile a (right of it, or under it) in that tileset, a ramp piece's tile among them.
bool neighbors(int era, int way, uint16_t a, uint16_t b) {
    static const std::unordered_set<uint64_t> set = [] {
        std::unordered_set<uint64_t> s;
        for (const RampNeighborData& n : kRampNeighbors) s.insert(pairKey(n.era, n.way, n.a, n.b));
        return s;
    }();
    return set.count(pairKey(era, way, variant(a), variant(b))) != 0;
}

// Whether a kit's own ramp has tile b next to tile a. A ramp there already counts as fitting the new one only where the
// kits make that pair, so a new ramp does not end inside an old one.
bool kitNeighbors(int era, int way, uint16_t a, uint16_t b) {
    static std::map<int, std::unordered_set<uint64_t>> pairs;
    if (!pairs.count(era)) {
        std::unordered_set<uint64_t>& s = pairs[era];
        for (int k = 0; k < kKits; ++k)
            if (kRampKits[k].era == era)
                for (int n = 0; n <= 2; ++n) {
                    std::map<std::pair<int, int>, uint16_t> at;
                    for (const Editor::TileAt& c : rampKitTiles(k, n, 0, 0)) at[{c.x, c.y}] = c.value;
                    for (const auto& [p, v] : at) {
                        if (auto r = at.find({p.first + 1, p.second}); r != at.end()) s.insert(pairKey(era, 0, v, r->second));
                        if (auto u = at.find({p.first, p.second + 1}); u != at.end()) s.insert(pairKey(era, 1, v, u->second));
                    }
                }
    }
    return pairs[era].count(pairKey(era, way, a, b)) != 0;
}

// Whether a tile is a ramp piece's (of any kit of the tileset).
bool isPiece(int era, uint16_t v) {
    static std::map<int, std::unordered_set<uint16_t>> pieces;
    if (!pieces.count(era)) {
        std::unordered_set<uint16_t>& s = pieces[era];
        for (int k = 0; k < kKits; ++k)
            if (kRampKits[k].era == era)
                for (const Editor::TileAt& c : rampKitTiles(k, 1, 0, 0)) s.insert(c.value);
    }
    return pieces[era].count(v) != 0;
}

// Whether the ramp's edge meets the map's tiles round it as the corpus has them (a ramp out of step with the cliff's
// pattern does not), or as the kits' own ramps do (a ramp there already).
bool edgeFits(const Editor& ed, int era, const std::vector<Editor::TileAt>& tiles) {
    std::set<std::pair<int, int>> on;
    for (const Editor::TileAt& c : tiles) on.insert({c.x, c.y});
    const auto at = [&](int x, int y) { return ed.terrain().tiles[size_t(y) * ed.width() + x]; };
    const auto fits = [&](int way, uint16_t a, uint16_t b) { return kitNeighbors(era, way, a, b) || neighbors(era, way, a, b); };
    for (const Editor::TileAt& c : tiles) {
        if (c.x + 1 < ed.width() && !on.count({c.x + 1, c.y}) && !fits(0, c.value, at(c.x + 1, c.y))) return false;
        if (c.x > 0 && !on.count({c.x - 1, c.y}) && !fits(0, at(c.x - 1, c.y), c.value)) return false;
        if (c.y + 1 < ed.height() && !on.count({c.x, c.y + 1}) && !fits(1, c.value, at(c.x, c.y + 1))) return false;
        if (c.y > 0 && !on.count({c.x, c.y - 1}) && !fits(1, at(c.x, c.y - 1), c.value)) return false;
    }
    return true;
}

// Whether a tile is a slope's (of any kit of the tileset) and no wall's: the walls of some blocks are their cliff's tiles.
bool isSlope(int era, uint16_t v) {
    static std::map<int, std::unordered_set<uint16_t>> slopes;
    if (!slopes.count(era)) {
        std::unordered_set<uint16_t>& s = slopes[era];
        std::unordered_set<uint16_t> walls;
        for (int k = 0; k < kKits; ++k)
            if (kRampKits[k].era == era)
                for (int kind = 0; kind < 3; ++kind)
                    for (int i = kRampKits[k].tiles[kind]; i < kRampKits[k].tiles[kind + 1]; ++i) (kind == 1 ? s : walls).insert(kRampTiles[i].value);
        for (uint16_t w : walls) s.erase(w);
    }
    return slopes[era].count(v) != 0;
}

// Whether the ramp goes over no slope that is there and meets the slopes round it as the kits' own ramps do (or as the
// corpus has them): a new ramp does not end inside one that is there. (edgeFits does this and more for the kits the maps
// show.)
bool piecesFit(const Editor& ed, int era, const std::vector<Editor::TileAt>& tiles) {
    std::set<std::pair<int, int>> on;
    for (const Editor::TileAt& c : tiles) on.insert({c.x, c.y});
    const auto at = [&](int x, int y) { return ed.terrain().tiles[size_t(y) * ed.width() + x]; };
    const auto fits = [&](int way, uint16_t a, uint16_t b) { return kitNeighbors(era, way, a, b) || neighbors(era, way, a, b); };
    const auto out = [&](int x, int y) { return !on.count({x, y}) && isSlope(era, at(x, y)); };
    for (const Editor::TileAt& c : tiles) {
        if (c.x >= 0 && c.y >= 0 && c.x < ed.width() && c.y < ed.height() && at(c.x, c.y) != c.value && isSlope(era, at(c.x, c.y))) return false;  // (over a slope)
        if (c.x + 1 < ed.width() && out(c.x + 1, c.y) && !fits(0, c.value, at(c.x + 1, c.y))) return false;
        if (c.x > 0 && out(c.x - 1, c.y) && !fits(0, at(c.x - 1, c.y), c.value)) return false;
        if (c.y + 1 < ed.height() && out(c.x, c.y + 1) && !fits(1, c.value, at(c.x, c.y + 1))) return false;
        if (c.y > 0 && out(c.x, c.y - 1) && !fits(1, at(c.x, c.y - 1), c.value)) return false;
    }
    return true;
}

}  // namespace

bool rampJoins(const Tileset& ts, const std::vector<Editor::TileAt>& tiles) {
    // (the minitiles of the ramp's tiles: walkable, and their ground level, from the VF4 flags: 0 low, 1 middle, 2 high)
    std::map<std::pair<int, int>, int> level;
    for (const Editor::TileAt& c : tiles) {
        const int m = ts.megatileFor(c.value);
        if (m < 0 || size_t(m) >= ts.minitileFlags.size()) continue;
        for (int i = 0; i < 16; ++i)
            if (const uint16_t f = ts.minitileFlags[size_t(m)][size_t(i)]; f & Tileset::kWalkable)
                level[{c.x * 4 + i % 4, c.y * 4 + i / 4}] = (f & 4) ? 2 : (f & 2) ? 1 : 0;
    }
    int lowest = 3;
    for (const auto& [p, l] : level) lowest = std::min(lowest, l);
    std::set<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> todo;
    for (const auto& [p, l] : level)
        if (l == lowest) todo.push_back(p), seen.insert(p);
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        if (level[{x, y}] > lowest) return true;
        for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            if (level.count({x + dx, y + dy}) && seen.insert({x + dx, y + dy}).second) todo.push_back({x + dx, y + dy});
    }
    return false;
}

namespace {

// Whether a ramp joins what it should: two ground levels, or (a kit whose pieces are all of one level: a pit's, as Sunken
// Ground's, on the level of the ground round it) whatever it is.
bool joinsAsKit(const Editor& ed, int kit, const std::vector<Editor::TileAt>& tiles) {
    static std::map<std::pair<int, int>, bool> twoLevels;  // (kit, tileset: its own pieces join two levels)
    const auto key = std::make_pair(kit, int(ed.era() & 7));
    auto i = twoLevels.find(key);
    if (i == twoLevels.end()) i = twoLevels.emplace(key, rampJoins(ed.tileset(), rampKitTiles(kit, 1, 0, 0))).first;
    return !i->second || rampJoins(ed.tileset(), tiles);
}

}  // namespace

bool rampKitFits(const Editor& ed, int kit, int slopes, int ax, int ay) {
    if (!kitAt(kit) || !usable(kit) || !ed.canPaintTerrain() || kRampKits[kit].era != (ed.era() & 7)) return false;
    const Fit f(ed, kit);
    if (!cornersFit(f, slopes, ax, ay)) return false;
    const std::vector<Editor::TileAt> tiles = rampKitTiles(kit, slopes, ax, ay);
    return (f.k.mode ? piecesFit(ed, f.k.era, tiles) : edgeFits(ed, f.k.era, tiles)) && joinsAsKit(ed, kit, tiles);
}

bool rampFits(const Editor& ed, int facing, int slopes, int ax, int ay) { return rampKitFits(ed, firstKit(ed.era() & 7, facing), slopes, ax, ay); }

namespace {

// The usable kits of the map's tileset on the map.
// Whether a kit's pieces are the map's tileset's: each piece's tile group belongs to the piece's doodad (CV5).
bool kitOnTileset(const Editor& ed, int kit) {
    const RampKitData& k = kRampKits[kit];
    for (int kind = 0; kind < 3; ++kind) {
        if (k.tiles[kind] >= k.tiles[kind + 1]) return false;
        const size_t g = kRampTiles[k.tiles[kind]].value >> 4;
        if (g >= ed.tileset().groups.size() || ed.tileset().groups[g].extra[0] != k.doodads[kind]) return false;
    }
    return true;
}

std::map<int, Fit> fitsOn(const Editor& ed) {
    std::map<int, Fit> fits;
    for (int k : rampKitsOf(ed.era() & 7))
        if (kitOnTileset(ed, k)) fits.emplace(k, Fit(ed, k));
    return fits;
}

// A ramp doodad's tiles with its top left at tile (x, y) (its empty cells left out).
std::vector<Editor::TileAt> doodadTiles(const Editor& ed, const DoodadType& t, int x, int y) {
    std::vector<Editor::TileAt> out;
    for (int r = 0; r < t.height; ++r)
        for (int c = 0; c < t.width; ++c)
            if (const uint16_t v = uint16_t((t.firstGroup + r) << 4 | c); ed.tileset().megatileFor(v) > 0) out.push_back({x + c, y + r, v});
    return out;
}

// The ramp doodad that fits nearest a stroke's point (3 tiles or nearer), where the game's rule lets it go.
RampPlan planDoodadRamp(const Editor& ed, const std::vector<uint16_t>& ids, int sx, int sy) {
    RampPlan plan;
    double best = 3 * 32 + 1;
    for (const uint16_t id : ids) {
        const DoodadType* t = ed.doodadTypes().find(id);
        if (!t) continue;
        const auto at = ed.nearestDoodadFit(id, sx, sy, true, 3);
        if (!at) continue;
        if (const double d = std::hypot(at->first - sx, at->second - sy); d < best) {
            best = d, plan.doodad = id, plan.x = at->first, plan.y = at->second;
            int tx = 0, ty = 0;
            ed.doodadOrigin(*t, at->first, at->second, tx, ty);
            plan.tiles = doodadTiles(ed, *t, tx, ty);
        }
    }
    if (plan.doodad >= 0) plan.problem = RampPlan::Problem::None;
    return plan;
}

// The edges one planRamps call has tried: an edge where no ramp fit (its kit and a wall A's place on it) is not tried
// again for the stroke's other points on it, and the call makes at most kMaxEdgeTries full searches of an edge (a drag
// back and forth over cliffs where no ramp fits asked for one at each of its points: the program stopped answering).
struct EdgeTries {
    struct Edge {
        int kit, ax, ay;
    };
    std::vector<Edge> failed;
    int searches = 0;
};
constexpr int kMaxEdgeTries = 24;

RampPlan planRampWith(const Editor& ed, const std::map<int, Fit>& fits, std::span<const std::pair<int, int>> stroke, EdgeTries* tries = nullptr) {
    RampPlan plan;
    const int era = ed.era() & 7;
    if (fits.empty()) return plan.problem = RampPlan::Problem::NoKit, plan;
    plan.low = fits.begin()->second.k.low, plan.high = fits.begin()->second.k.high;
    if (!ed.canPaintTerrain()) return plan.problem = RampPlan::Problem::NoIsom, plan;
    if (stroke.empty()) return plan;
    // The middle of a ramp (map pixels).
    const auto middle = [&](int kit, int n, int ax, int ay) {
        double cx = 0, cy = 0;
        const std::vector<Editor::TileAt> t = rampKitTiles(kit, n, ax, ay);
        for (const Editor::TileAt& c : t) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
        return std::pair<double, double>{cx / double(t.size()), cy / double(t.size())};
    };
    // The ramp of no slope whose corners fit nearest the stroke's start: its kit (block and facing) and its edge.
    const auto [sx, sy] = stroke.front();
    {
        // (only one terrain round the start, as far as a ramp's pieces go: no cliff edge there)
        const uint16_t t0 = ed.terrainAt(std::max(0, sx), std::max(0, sy));
        bool two = false;
        for (int py = sy - 12 * 32; py <= sy + 12 * 32 && !two; py += 32)
            for (int px = sx - 16 * 32; px <= sx + 16 * 32 && !two; px += 32)
                if (px >= 0 && py >= 0 && px < 32 * ed.width() && py < 32 * ed.height()) two = ed.terrainAt(px, py) != t0;
        if (!two) return plan;
    }
    double best = 1e18;
    int kit = -1;
    // (the same map and point as before, as each move of a drag asks again for the points so far: the answer then)
    struct Near {
        int kit, ax, ay;
    };
    static uint64_t cachedFor = 0;
    static std::unordered_map<uint64_t, Near> cache;
    if (cachedFor != ed.terrainRevision() || cache.size() > 100000) cache.clear(), cachedFor = ed.terrainRevision();
    const uint64_t at = uint64_t(uint32_t(sx)) << 32 | uint32_t(sy);
    if (const auto c = cache.find(at); c != cache.end()) {
        kit = c->second.kit, plan.ax = c->second.ax, plan.ay = c->second.ay;
    } else {
        for (const auto& [k, f] : fits)
            for (int ay = sy / 32 - 10; ay <= sy / 32 + 6; ++ay)
                for (int ax = sx / 32 - 12; ax <= sx / 32 + 6; ++ax) {
                    if (!f.ok || !placeFits(f, {0, ax, ay}) || !cornersFit(f, 0, ax, ay)) continue;  // (wall A first: most places end there)
                    const auto [mx, my] = middle(k, 0, ax, ay);
                    if (const double d = std::hypot(mx - sx, my - sy); d < best && d < 192) best = d, kit = k, plan.ax = ax, plan.ay = ay;
                }
        cache[at] = {kit, plan.ax, plan.ay};
    }
    if (kit < 0) return plan;
    if (tries) {  // (an edge where no ramp fit already, the wall A on its line; or the searches all made)
        const RampKitData& kd = kRampKits[kit];
        const int spacing = std::max(std::abs(kd.step[0]), std::abs(kd.step[1]));
        for (const EdgeTries::Edge& f : tries->failed)
            if (f.kit == kit && std::abs((plan.ax - f.ax) * kd.step[1] - (plan.ay - f.ay) * kd.step[0]) < spacing) return plan.kit = -1, plan;
        if (tries->searches >= kMaxEdgeTries) return plan.kit = -1, plan;
        ++tries->searches;
    }
    const Fit& fit = fits.at(kit);
    plan.kit = kit, plan.facing = fit.k.facing, plan.low = fit.k.low, plan.high = fit.k.high;
    // Along the edge (the slopes' step), from wall A's middle: how far the stroke goes each way. Of each anchor on this
    // edge (a whole tile from the first one along it) and each width there, the ramp whose wall A is nearest the stroke's
    // first end and wall B nearest its other end (an edge can have places half a slope apart that fit), of those that
    // fit: the corners under each piece, the edge, the join of the ground levels.
    const RampKitData& k = fit.k;
    const double ux = 32.0 * k.step[0], uy = 32.0 * k.step[1], len = std::hypot(ux, uy);
    const auto pieceMiddle = [&](const RampPiece& p) {
        const auto [w, h] = rampKitPieceSize(kit, p.kind);
        return std::pair<double, double>{p.x * 32.0 + w * 16.0, p.y * 32.0 + h * 16.0};
    };
    const auto [ax0, ay0] = pieceMiddle(rampKitPieces(kit, 0, plan.ax, plan.ay).front());
    const auto along = [&](double x, double y) { return ((x - ax0) * ux + (y - ay0) * uy) / len; };
    const double vx = -uy / len, vy = ux / len;  // (across the edge)
    const auto across = [&](double x, double y) { return (x - ax0) * vx + (y - ay0) * vy; };
    // (the stroke's stretch along the edge: its points 3 tiles or nearer to the edge's line; the rest is for other ramps)
    double lo = 1e18, hi = -1e18;
    for (const auto& [x, y] : stroke)
        if (std::fabs(across(x, y)) <= 96) lo = std::min(lo, along(x, y)), hi = std::max(hi, along(x, y));
    if (lo > hi) lo = hi = along(sx, sy);
    // (the same edge and stretch on the same map as before: the ramp then)
    static uint64_t plannedFor = 0;
    static std::map<std::tuple<int, int, int, long long, long long>, RampPlan> planned;
    if (plannedFor != ed.terrainRevision() || planned.size() > 20000) planned.clear(), plannedFor = ed.terrainRevision();
    const auto key = std::make_tuple(kit, plan.ax, plan.ay, std::llround(lo * 16), std::llround(hi * 16));
    if (const auto c = planned.find(key); c != planned.end()) return c->second;
    const auto done = [&](RampPlan& r) -> RampPlan& { return planned[key] = r, r; };
    const int x0 = plan.ax, y0 = plan.ay;
    double best2 = 1e18;
    const int reach = int(std::ceil((hi - lo) / 32)) + 8;
    // (the anchors on the edge's line whose wall A fits, nearest the stroke's first end first: the search stops at the
    // first that cannot beat the best)
    struct Anchor {
        int ax, ay;
        double a;
    };
    std::vector<Anchor> anchors;
    for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx) {
            const double wx = ax0 + 32.0 * dx, wy = ay0 + 32.0 * dy;
            if (std::fabs(across(wx, wy)) > 8 || !placeFits(fit, {0, x0 + dx, y0 + dy})) continue;
            anchors.push_back({x0 + dx, y0 + dy, along(wx, wy)});
        }
    std::sort(anchors.begin(), anchors.end(), [&](const Anchor& p, const Anchor& q) { return std::fabs(p.a - lo) < std::fabs(q.a - lo); });
    // (a ramp there already, round the stroke: the widths go on past the stroke's end, so that the old ramp, or a wider one
    // over it, can be the one; a new ramp may not end inside it)
    bool overRamp = false;
    for (int y = std::max(0, y0 - reach); y < std::min(int(ed.height()), y0 + reach) && !overRamp; ++y)
        for (int x = std::max(0, x0 - reach); x < std::min(int(ed.width()), x0 + reach) && !overRamp; ++x)
            overRamp = isPiece(era, ed.terrain().tiles[size_t(y) * ed.width() + x]);
    for (const auto& [ax, ay, a] : anchors) {
        if (std::fabs(a - lo) >= best2) break;  // (no width of it, or of those after it, can be nearer)
        // (each width adds one slope and moves wall B: only those two pieces are checked again)
        for (int n = 0; n <= 64; ++n) {
            const std::vector<RampPiece> pieces = rampKitPieces(kit, n, ax, ay);
            if (n && !placeFits(fit, pieces[size_t(n)])) break;  // (the new slope)
            const auto [bx, by] = pieceMiddle(pieces.back());
            const double cost = std::fabs(a - lo) + std::fabs(along(bx, by) - hi);
            if (cost < best2 && placeFits(fit, pieces.back())) {
                const std::vector<Editor::TileAt> tiles = rampKitTiles(kit, n, ax, ay);
                if ((k.mode ? piecesFit(ed, era, tiles) : edgeFits(ed, era, tiles)) && joinsAsKit(ed, kit, tiles)) best2 = cost, plan.ax = ax, plan.ay = ay, plan.slopes = n;
            }
            if (along(bx, by) > hi + len && !overRamp) break;
        }
    }
    if (best2 > 1e17) {  // (no ramp on that edge fits and joins the ground)
        if (tries) tries->failed.push_back({kit, x0, y0});
        return plan.facing = -1, plan.kit = -1, done(plan);
    }
    plan.tiles = rampKitTiles(kit, plan.slopes, plan.ax, plan.ay);
    plan.problem = RampPlan::Problem::None;
    return done(plan);
}

}  // namespace

RampPlan planRamp(const Editor& ed, const std::vector<std::pair<int, int>>& stroke) { return planRampWith(ed, fitsOn(ed), stroke); }

std::vector<RampPlan> planRamps(const Editor& ed, const std::vector<std::pair<int, int>>& stroke, RampPlan* why) {
    std::vector<RampPlan> out;
    std::set<std::pair<int, int>> taken;
    const std::map<int, Fit> fits = fitsOn(ed);
    // (no kit on this tileset: its ramp doodads, one where the stroke is near the place it fits)
    const std::vector<uint16_t> doodads = fits.empty() ? rampDoodads(ed) : std::vector<uint16_t>{};
    if (fits.empty() && !doodads.empty() && why) why->problem = ed.canPaintTerrain() ? RampPlan::Problem::NoCliff : RampPlan::Problem::NoIsom;
    EdgeTries tries;
    // (the stroke's points that are not on the ramps planned so far or next to them, with their places in the stroke:
    // made again only when a ramp goes in, not for each point)
    std::vector<std::pair<int, int>> live;
    std::vector<size_t> liveAt;
    size_t livePos = 0, liveFor = size_t(-1);
    size_t i = 0;
    while (i < stroke.size() && out.size() < 32) {
        // (the stroke from i on without its points on the ramps planned already or next to them: the next ramp goes on to
        // them, not over them)
        const auto onTaken = [&](std::pair<int, int> q) {
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (taken.count({q.first / 32 + dx, q.second / 32 + dy})) return true;
            return false;
        };
        if (!taken.empty() && onTaken(stroke[i])) {
            ++i;
            continue;
        }
        if (liveFor != taken.size()) {
            live.clear(), liveAt.clear(), livePos = 0, liveFor = taken.size();
            for (size_t j = i; j < stroke.size(); ++j)
                if (taken.empty() || !onTaken(stroke[j])) live.push_back(stroke[j]), liveAt.push_back(j);
        }
        while (livePos < liveAt.size() && liveAt[livePos] < i) ++livePos;
        const std::span<const std::pair<int, int>> rest(live.data() + livePos, live.size() - livePos);
        const RampPlan p = !doodads.empty() ? planDoodadRamp(ed, doodads, stroke[i].first, stroke[i].second) : planRampWith(ed, fits, rest, &tries);
        if (why && i == 0) *why = p;
        if (p.problem != RampPlan::Problem::None) {
            if (p.problem != RampPlan::Problem::NoCliff) break;
            ++i;  // (that point is on no edge: the next one)
            continue;
        }
        // (the stroke's points by this ramp, 3 tiles or nearer, from i on: the next ramp starts after them)
        size_t next = i + 1;
        for (size_t j = i; j < stroke.size(); ++j) {
            bool near = false;
            for (const Editor::TileAt& c : p.tiles)
                if (std::hypot(32.0 * c.x + 16 - stroke[j].first, 32.0 * c.y + 16 - stroke[j].second) <= 96) {
                    near = true;
                    break;
                }
            if (!near) break;
            next = j + 1;
        }
        bool overlaps = false;
        for (const Editor::TileAt& c : p.tiles) overlaps |= taken.count({c.x, c.y}) != 0;
        if (overlaps) {  // (over a ramp planned already: the next point may give one beside it)
            ++i;
            continue;
        }
        for (const Editor::TileAt& c : p.tiles) taken.insert({c.x, c.y});
        out.push_back(p);
        i = next;
    }
    return out;
}

namespace {

// A ramp that fits once a few ISOM corners under its pieces get the terrains they need: its kit, width, wall A's place
// and those corners.
struct Reshape {
    int kit = -1, slopes = 1, ax = 0, ay = 0;
    std::vector<CornerValue> corners;
};

// Of the kits' ramps (one slope) near a point (map pixels), the one whose pieces' ISOM corners need the fewest changes
// (1 to 4, and 2 more for each slope's width of straight cliff past each wall (`straight`); a third of its corners at
// most: the cliff is near, a small change), nearest the point; none on protected tiles, on the ramps there or planned
// (`taken`) or next to them. kit -1: none.
Reshape findReshape(const Editor& ed, const std::map<int, Fit>& fits, int sx, int sy, const std::set<std::pair<int, int>>& taken, int straight) {
    const int era = ed.era() & 7;
    Reshape best;
    double bestCost = 1e18;
    for (const auto& [kit, f] : fits) {
        if (!f.ok) continue;
        // (a corner that may be any but the high ground and is not: the kit's first ground below)
        std::string lowName = f.k.low;
        lowName = lowName.substr(0, lowName.find('|'));
        const uint16_t low = lowName.empty() ? 0 : terrainId(ed, lowName.c_str());
        for (int ay = sy / 32 - 10; ay <= sy / 32 + 6; ++ay)
            for (int ax = sx / 32 - 12; ax <= sx / 32 + 6; ++ax) {
                std::vector<RampPiece> pieces = rampKitPieces(kit, best.slopes, ax, ay);
                // (and the cliff `straight` slopes' width past each wall, straight: the corners of a slope there, as the tiles
                // round a ramp on the game's maps are a straight cliff's)
                const RampKitData& kd = f.k;
                for (int i = 1; i <= straight; ++i)
                    for (const int at : {-i, best.slopes + i}) pieces.push_back({1, ax + kd.start[0] + at * kd.step[0], ay + kd.start[1] + at * kd.step[1]});
                std::map<std::pair<int, int>, uint16_t> need;  // (each corner once: the pieces share some)
                int total = 0;
                bool bad = false;
                double mx = 0, my = 0;
                for (const RampPiece& p : pieces) {
                    const auto [w, h] = rampKitPieceSize(kit, p.kind);
                    if (p.x < 0 || p.y < 0 || p.x + w > ed.width() || p.y + h > ed.height()) bad = true;
                    mx += p.x * 32.0 + w * 16.0, my += p.y * 32.0 + h * 16.0;
                    for (const auto& [c, ids] : f.corners[size_t(p.kind)]) {
                        if (bad) break;
                        const int px = 32 * p.x + c->px, py = 32 * p.y + c->py;
                        if (px % 64 || py % 32 || !((px / 64 + py / 32) & 1)) bad = true;  // (off the corners' grid)
                        if (bad) break;
                        ++total;
                        const uint16_t t = ed.terrainAt(px, py);
                        const bool ok = ids.empty() ? (t && t != f.high) : std::find(ids.begin(), ids.end(), t) != ids.end();
                        if (ok) continue;
                        const uint16_t to = ids.empty() ? low : ids.front();
                        if (!to) bad = true;
                        const auto [it, added] = need.insert({{px / 64, py / 32}, to});
                        if (!added && it->second != to) bad = true;
                    }
                    if (bad) break;
                }
                const int changes = int(need.size());
                if (bad || !changes || changes > 4 + 2 * straight || 3 * changes > total) continue;
                mx /= double(pieces.size()), my /= double(pieces.size());
                const double cost = 64.0 * changes + std::hypot(mx - sx, my - sy);
                if (std::hypot(mx - sx, my - sy) > 128 || cost >= bestCost) continue;
                // (its tiles and those round them: not protected, no ramp there or planned)
                bool clear = true;
                for (const Editor::TileAt& c : rampKitTiles(kit, best.slopes, ax, ay))
                    for (int dy = -2; dy <= 2 && clear; ++dy)
                        for (int dx = -2; dx <= 2 && clear; ++dx) {
                            const int x = c.x + dx, y = c.y + dy;
                            if (x < 0 || y < 0 || x >= ed.width() || y >= ed.height()) continue;
                            clear = !ed.tileProtected(x, y) && !taken.count({x, y}) && !isPiece(era, ed.terrain().tiles[size_t(y) * ed.width() + x]);
                        }
                // (nor round the corners that change: the terrain fitted to them reaches a few tiles further)
                for (auto n = need.begin(); n != need.end() && clear; ++n)
                    for (int dy = -4; dy <= 4 && clear; ++dy)
                        for (int dx = -6; dx <= 6 && clear; ++dx) {
                            const int x = 2 * n->first.first + dx, y = n->first.second + dy;
                            if (x < 0 || y < 0 || x >= ed.width() || y >= ed.height()) continue;
                            clear = !ed.tileProtected(x, y) && !taken.count({x, y}) && !isPiece(era, ed.terrain().tiles[size_t(y) * ed.width() + x]);
                        }
                if (!clear) continue;
                bestCost = cost, best.kit = kit, best.ax = ax, best.ay = ay, best.corners.clear();
                for (const auto& [at, t] : need) best.corners.push_back({at.first, at.second, t});
            }
    }
    return best;
}

}  // namespace

int reshapeForRamps(Editor& ed, const std::vector<std::pair<int, int>>& stroke, const std::vector<RampPlan>& ramps) {
    if (!ed.canPaintTerrain()) return 0;
    const std::map<int, Fit> fits = fitsOn(ed);
    if (fits.empty()) return 0;
    std::set<std::pair<int, int>> taken;
    for (const RampPlan& p : ramps)
        for (const Editor::TileAt& c : p.tiles) taken.insert({c.x, c.y});
    // (the stroke's points away from the ramps planned, each 4 tiles or more from the points tried before: at most
    // kMaxReshapeTries, as a drag back and forth asks again at each move)
    constexpr int kMaxReshapeTries = 12;
    // (a point's answer on the same tiles, with the same ramps planned, as before: the answer then, as each move of a drag
    // asks again for the whole stroke on a new copy of the map; kit -1: no change there makes a ramp fit)
    static std::unordered_map<uint64_t, Reshape> answers;
    if (answers.size() > 50000) answers.clear();
    const auto hashOf = [](uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; };
    const auto tilesHash = [&] {
        uint64_t h = 14695981039346656037ull ^ (uint64_t(ed.era() & 7) << 32 | uint64_t(ed.width()) << 16 | ed.height());
        for (const char ch : ed.terrainName(2)) h = hashOf(h, uint8_t(ch));  // (the tileset: a mod's has other names)
        const std::vector<uint16_t>& tiles = ed.terrain().tiles;
        for (size_t i = 0; i < tiles.size(); ++i)  // (each tile and whether it is protected)
            h = hashOf(h, uint64_t(tiles[i]) << 1 | uint64_t(ed.tileProtected(int(i % ed.width()), int(i / ed.width()))));
        return h;
    };
    const auto takenHash = [&] {
        uint64_t h = 1469598103934665603ull;
        for (const auto& [x, y] : taken) h = hashOf(h, uint64_t(uint32_t(x)) << 32 | uint32_t(y));
        return h;
    };
    uint64_t mapHash = hashOf(tilesHash(), takenHash());
    // (the corners of each terrain in one edit: the terrain round them fitted to them, so the ISOM stays valid; one undo
    // step, taken back where no ramp fits after it)
    const auto apply = [&](const Reshape& r) {
        std::map<uint16_t, std::vector<CornerValue>> by;
        for (const CornerValue& c : r.corners) by[c.terrain].push_back(c);
        bool changed = false;
        ed.beginStroke();
        for (const auto& [t, cs] : by) changed |= ed.setTerrainCorners(cs);
        ed.endStroke();
        return changed;
    };
    std::vector<std::pair<int, int>> tried;
    int made = 0, tries = 0;
    for (const auto& [x, y] : stroke) {
        if (tries >= kMaxReshapeTries) break;
        bool skip = false;
        for (const auto& [tx, ty] : tried) skip = skip || std::hypot(double(x - tx), double(y - ty)) < 128;
        for (auto c = taken.begin(); c != taken.end() && !skip; ++c) skip = std::hypot(32.0 * c->first + 16 - x, 32.0 * c->second + 16 - y) <= 96;
        if (skip) continue;
        tried.push_back({x, y});
        ++tries;
        const uint64_t key = hashOf(mapHash, uint64_t(uint32_t(x)) << 32 | uint32_t(y));
        Reshape found;
        if (const auto a = answers.find(key); a != answers.end()) {
            if (a->second.kit < 0 || !apply(a->second)) continue;
            if (!rampKitFits(ed, a->second.kit, a->second.slopes, a->second.ax, a->second.ay)) {  // (as the first time; else as it was)
                ed.undo(1);
                continue;
            }
            found = a->second;
        } else {
            // (the smallest change first: the ramp's own corners; then with the cliff straight past its walls, as the
            // tiles round it must join it as on the game's maps)
            for (int straight = 0; straight <= 2 && found.kit < 0; ++straight) {
                const Reshape r = findReshape(ed, fits, x, y, taken, straight);
                if (r.kit < 0 || !apply(r)) continue;
                if (!rampKitFits(ed, r.kit, r.slopes, r.ax, r.ay)) {  // (the fitting moved the cliff again, or the edge does not join: as it was)
                    ed.undo(1);
                    continue;
                }
                found = r;
            }
            answers[key] = found;
            if (found.kit < 0) continue;
        }
        for (const Editor::TileAt& c : rampKitTiles(found.kit, found.slopes, found.ax, found.ay)) taken.insert({c.x, c.y});
        mapHash = hashOf(tilesHash(), takenHash());
        ++made;
    }
    return made;
}

std::vector<std::pair<int, int>> rampBrushStroke(const std::vector<std::pair<int, int>>& pts, float size) {
    size = std::max(0.5f, size);
    const int rx = int(size * 32), ry = int(size * 16);
    std::vector<std::pair<int, int>> offsets;
    for (int dy = -ry; dy <= ry; dy += 16)
        for (int dx = -rx; dx <= rx; dx += 32)
            if (std::abs(dx) * ry + std::abs(dy) * rx <= rx * ry) offsets.push_back({dx, dy});
    std::sort(offsets.begin(), offsets.end(), [](auto a, auto b) { return std::hypot(a.first, 2 * a.second) < std::hypot(b.first, 2 * b.second); });
    std::vector<std::pair<int, int>> out;
    std::set<std::pair<int, int>> seen;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto [x1, y1] = pts[i];
        const auto [x0, y0] = i ? pts[i - 1] : pts[i];
        const int n = std::max(1, int(std::hypot(x1 - x0, y1 - y0) / 32));
        for (int k = i ? 1 : 0; k <= n; ++k) {
            const int px = x0 + (x1 - x0) * k / n, py = y0 + (y1 - y0) * k / n;
            for (const auto& [dx, dy] : offsets)
                if (seen.insert({(px + dx) >> 4, (py + dy) >> 3}).second) out.push_back({px + dx, py + dy});
        }
    }
    return out;
}

bool rampsAvailable(const Editor& ed) { return !fitsOn(ed).empty() || !rampDoodads(ed).empty(); }

std::vector<uint16_t> rampDoodads(const Editor& ed) {
    if (!fitsOn(ed).empty()) return {};
    // (once for each tileset: by its doodads' count and its groups')
    static std::map<std::tuple<int, size_t, size_t>, std::vector<uint16_t>> cache;
    const auto key = std::make_tuple(int(ed.era() & 7), ed.doodadTypes().all().size(), ed.tileset().groups.size());
    if (const auto c = cache.find(key); c != cache.end()) return c->second;
    std::vector<uint16_t>& out = cache[key];
    for (const DoodadType& t : ed.doodadTypes().all()) {
        if (!t.inPalette || !t.needsTiles() || t.width < 4 || t.height < 4) continue;  // (no rule for where it goes; small pieces: not ramps)
        const std::vector<Editor::TileAt> tiles = doodadTiles(ed, t, 0, 0);
        if (tiles.empty() || !rampJoins(ed.tileset(), tiles)) continue;
        // (a ramp is mostly ground to walk on: the maps' ramps 60 % and more of their minitiles, the rocks and ruins that
        // also join two levels 45 % and less)
        int all = 0, walk = 0;
        for (const Editor::TileAt& c : tiles)
            if (const int m = ed.tileset().megatileFor(c.value); m >= 0 && size_t(m) < ed.tileset().minitileFlags.size())
                for (const uint16_t f : ed.tileset().minitileFlags[size_t(m)]) ++all, walk += (f & Tileset::kWalkable) != 0;
        if (2 * walk >= all) out.push_back(t.id);
    }
    return out;
}

std::vector<std::pair<int, int>> rampTilesUnder(const Editor& ed, const std::vector<std::pair<int, int>>& stroke) {
    const int era = ed.era() & 7, w = ed.width(), h = ed.height();
    if (fitsOn(ed).empty()) return {};  // (the kits' tiles are not this tileset's: a mod's)
    const auto piece = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && isPiece(era, ed.terrain().tiles[size_t(y) * w + x]); };
    // (the tiles within a diamond, 2 tiles across and 1 down, of each point and of the way between the points)
    std::set<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> todo;
    for (size_t i = 0; i < stroke.size(); ++i) {
        const auto [x1, y1] = stroke[i];
        const auto [x0, y0] = i ? stroke[i - 1] : stroke[i];
        const int n = std::max(1, int(std::hypot(x1 - x0, y1 - y0) / 16));
        for (int k = 0; k <= n; ++k) {
            const int px = x0 + (x1 - x0) * k / n, py = y0 + (y1 - y0) * k / n;
            for (int ty = py / 32 - 1; ty <= py / 32 + 1; ++ty)
                for (int tx = px / 32 - 2; tx <= px / 32 + 2; ++tx)
                    if (piece(tx, ty) && seen.insert({tx, ty}).second) todo.push_back({tx, ty});
        }
    }
    // (and the ramp tiles joined to them)
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
            if (piece(x + dx, y + dy) && seen.insert({x + dx, y + dy}).second) todo.push_back({x + dx, y + dy});
    }
    return std::vector<std::pair<int, int>>(seen.begin(), seen.end());
}

std::vector<RampOnMap> rampsIn(int era, int w, int h, const std::vector<uint16_t>& tiles, const std::vector<uint8_t>* near) {
    std::vector<RampOnMap> out;
    if (tiles.size() != size_t(w) * h) return out;
    const auto idx = [&](int x, int y) { return size_t(y) * w + x; };
    const auto piece = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && isPiece(era, tiles[idx(x, y)]); };
    std::vector<uint8_t> seen(tiles.size(), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if ((near && !(*near)[idx(x, y)]) || seen[idx(x, y)] || !piece(x, y)) continue;
            // (the ramp tiles joined to it: one ramp or more side by side)
            std::vector<std::pair<int, int>> todo{{x, y}};
            seen[idx(x, y)] = 1;
            int x0 = x, y0 = y, x1 = x, y1 = y;
            while (!todo.empty()) {
                const auto [cx, cy] = todo.back();
                todo.pop_back();
                x0 = std::min(x0, cx), y0 = std::min(y0, cy), x1 = std::max(x1, cx), y1 = std::max(y1, cy);
                for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                    if (piece(cx + dx, cy + dy) && !seen[idx(cx + dx, cy + dy)]) seen[idx(cx + dx, cy + dy)] = 1, todo.push_back({cx + dx, cy + dy});
            }
            // (each kit's ramps whose tiles all are there: wall A first, then the widest ramp from it; each tile in one)
            std::set<std::pair<int, int>> taken;
            const auto matches = [&](int k, int n, int ax, int ay) {
                const std::vector<Editor::TileAt> t = rampKitTiles(k, n, ax, ay);
                for (const Editor::TileAt& c : t)
                    if (c.x < 0 || c.y < 0 || c.x >= w || c.y >= h || tiles[idx(c.x, c.y)] != c.value || taken.count({c.x, c.y})) return false;
                return !t.empty();
            };
            const auto wallA = [&](int k, int ax, int ay) {
                for (int i = kRampKits[k].tiles[0]; i < kRampKits[k].tiles[1]; ++i) {
                    const int tx = ax + kRampTiles[i].x, ty = ay + kRampTiles[i].y;
                    if (tx < 0 || ty < 0 || tx >= w || ty >= h || tiles[idx(tx, ty)] != kRampTiles[i].value || taken.count({tx, ty})) return false;
                }
                return true;
            };
            for (int k = 0; k < kKits; ++k) {
                if (kRampKits[k].era != era) continue;
                for (int ay = y0 - 6; ay <= y1; ++ay)
                    for (int ax = x0 - 8; ax <= x1; ++ax) {
                        if (!wallA(k, ax, ay)) continue;
                        int n = -1;
                        for (int s = 0; s <= 64; ++s)
                            if (matches(k, s, ax, ay)) n = s;
                            else if (n >= 0) break;  // (no wider one: its slopes go on from this one's)
                        if (n < 0) continue;
                        out.push_back({k, n, ax, ay});
                        for (const Editor::TileAt& c : rampKitTiles(k, n, ax, ay)) taken.insert({c.x, c.y});
                    }
            }
        }
    return out;
}

int refitRamps(Editor& ed, const std::vector<uint16_t>& before) {
    const int era = ed.era() & 7, w = ed.width(), h = ed.height();
    const std::vector<uint16_t>& now = ed.terrain().tiles;
    if (!rampsAvailable(era) || !ed.canPaintTerrain() || before.size() != now.size() || before.size() != size_t(w) * h) return 0;
    if (fitsOn(ed).empty()) return 0;  // (the kits' tiles are not this tileset's: a mod's)
    const auto idx = [&](int x, int y) { return size_t(y) * w + x; };
    // The ramps of `before` with a tile next to a changed tile (or changed itself).
    std::vector<uint8_t> near(before.size(), 0);
    bool any = false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (before[idx(x, y)] != now[idx(x, y)])
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h) near[idx(x + dx, y + dy)] = 1, any = true;
    if (!any) return 0;
    int made = 0;
    for (const RampOnMap& o : rampsIn(era, w, h, before, &near)) {
        const std::vector<Editor::TileAt> old = rampKitTiles(o.kit, o.slopes, o.ax, o.ay);
        // (a ramp joins two ground levels: walls alone that a cliff edge has, in the blocks whose walls are its tiles, are
        // not a ramp)
        if (!joinsAsKit(ed, o.kit, old)) continue;
        // (the edit did not change its tiles and the ground under it is still the ground its pieces go on: it stays as it
        // is, also where its edge or its join would not let the brush put it there, as on maps made in other editors; on
        // protected tiles: it stays too)
        bool same = true, kept = false;
        for (const Editor::TileAt& c : old) {
            same = same && ed.terrain().tiles[idx(c.x, c.y)] == c.value;
            kept = kept || ed.tileProtected(c.x, c.y);
        }
        if (kept || (same && cornersFit(Fit(ed, o.kit), o.slopes, o.ax, o.ay))) continue;
        // its stretch of the edge: from wall A's middle to wall B's
        std::vector<std::pair<int, int>> stroke;
        for (const RampPiece& p : rampKitPieces(o.kit, o.slopes, o.ax, o.ay)) {
            const auto [pw, ph] = rampKitPieceSize(o.kit, p.kind);
            stroke.push_back({p.x * 32 + pw * 16, p.y * 32 + ph * 16});
        }
        // the old ramp's tiles that are still there become the cliff; then the ramps that fit go in
        std::vector<std::pair<int, int>> left;
        for (const Editor::TileAt& c : old)
            if (ed.terrain().tiles[idx(c.x, c.y)] == c.value) left.push_back({c.x, c.y});
        ed.remakeTilesFromIsom(left);
        for (const RampPlan& p : planRamps(ed, stroke)) {
            bool onProtected = false;
            for (const Editor::TileAt& c : p.tiles) onProtected = onProtected || ed.tileProtected(c.x, c.y);
            if (onProtected) continue;
            ed.placeTileValues(p.tiles);
            ++made;
        }
    }
    return made;
}

}  // namespace stardraft
