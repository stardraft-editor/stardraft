// Ramps of any width (include/stardraft/ramps.h): the kits of src/data/ramp_kits.inc.
#include "stardraft/ramps.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_set>

namespace stardraft {

namespace {

struct RampKitData {
    int era;
    const char* facing;
    const char* low;
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
    int era, facing, kind, px, py;
    bool high;
};
struct RampNeighborData {
    int era, way;  // way 0: b right of a; 1: b under a
    uint16_t a, b;
};
#include "data/ramp_kits.inc"

const char* const kFacingNames[kRampFacings] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

const RampKitData* kitOf(int era, int facing) {
    if (facing < 0 || facing >= kRampFacings) return nullptr;
    for (const RampKitData& k : kRampKits)
        if (k.era == era && !std::strcmp(k.facing, kFacingNames[facing])) return &k;
    return nullptr;
}

}  // namespace

const char* rampFacingName(int facing) { return facing >= 0 && facing < kRampFacings ? kFacingNames[facing] : ""; }

bool rampsAvailable(int era) {
    for (int f = 0; f < kRampFacings; ++f)
        if (!kitOf(era, f)) return false;
    return true;
}

std::pair<int, int> rampPieceSize(int era, int facing, int kind) {
    const RampKitData* k = kitOf(era, facing);
    if (!k || kind < 0 || kind > 2) return {0, 0};
    int w = 0, h = 0;
    for (int i = k->tiles[kind]; i < k->tiles[kind + 1]; ++i) w = std::max(w, kRampTiles[i].x + 1), h = std::max(h, kRampTiles[i].y + 1);
    return {w, h};
}

std::vector<RampPiece> rampPieces(int era, int facing, int slopes, int ax, int ay) {
    const RampKitData* k = kitOf(era, facing);
    if (!k) return {};
    std::vector<RampPiece> out{{0, ax, ay}};
    for (int i = 0; i < slopes; ++i) out.push_back({1, ax + k->start[0] + i * k->step[0], ay + k->start[1] + i * k->step[1]});
    out.push_back({2, ax + k->start[0] + slopes * k->step[0], ay + k->start[1] + slopes * k->step[1]});
    return out;
}

std::vector<Editor::TileAt> rampTiles(int era, int facing, int slopes, int ax, int ay) {
    std::vector<Editor::TileAt> out;
    const RampKitData* k = kitOf(era, facing);
    if (!k) return out;
    for (const RampPiece& p : rampPieces(era, facing, slopes, ax, ay))
        for (int i = k->tiles[p.kind]; i < k->tiles[p.kind + 1]; ++i) out.push_back({p.x + kRampTiles[i].x, p.y + kRampTiles[i].y, kRampTiles[i].value});
    return out;
}

bool rampMatches(const Editor& ed, int facing, int slopes, int ax, int ay) {
    const std::vector<Editor::TileAt> tiles = rampTiles(ed.era() & 7, facing, slopes, ax, ay);
    for (const Editor::TileAt& c : tiles)
        if (c.x < 0 || c.y < 0 || c.x >= ed.width() || c.y >= ed.height() || ed.terrain().tiles[size_t(c.y) * ed.width() + c.x] != c.value) return false;
    return !tiles.empty();
}

namespace {

// A ramp on one map: its kit and the terrain ids of its high and low ground there.
struct Fit {
    const Editor& ed;
    const RampKitData& k;
    int facing;
    uint16_t low = 0, high = 0;
    Fit(const Editor& e, const RampKitData& kit, int f) : ed(e), k(kit), facing(f) {
        // (the ids by name, once for each kit)
        static std::map<const RampKitData*, std::pair<uint16_t, uint16_t>> ids;
        const RampKitData* key = &k;
        auto i = ids.find(key);
        if (i == ids.end()) {
            std::pair<uint16_t, uint16_t> v{0, 0};
            for (uint16_t id = 1; id < 256; ++id) {
                const std::string n = ed.terrainName(id);
                if (n == k.low && !v.first) v.first = id;
                if (n == k.high && !v.second) v.second = id;
            }
            i = ids.emplace(key, v).first;
        }
        low = i->second.first, high = i->second.second;
    }
};

// The approved ramps' corners of a piece (kRampCorners, by era, facing and piece).
const std::vector<const RampCornerData*>& cornersOf(int era, int facing, int kind) {
    static const std::map<int, std::vector<const RampCornerData*>> index = [] {
        std::map<int, std::vector<const RampCornerData*>> m;
        for (const RampCornerData& c : kRampCorners) m[c.era * 100 + c.facing * 10 + c.kind].push_back(&c);
        return m;
    }();
    static const std::vector<const RampCornerData*> none;
    const auto i = index.find(era * 100 + facing * 10 + kind);
    return i == index.end() ? none : i->second;
}

// Whether the ISOM corners under a piece (its top left at tile x, y) are the high and low ground of the approved ramps'.
bool pieceFits(const Fit& f, int kind, int x, int y) {
    const std::vector<const RampCornerData*>& corners = cornersOf(f.k.era, f.facing, kind);
    for (const RampCornerData* c : corners) {
        const int px = 32 * x + c->px, py = 32 * y + c->py;
        if (px % 64 || py % 32 || px < 0 || py < 0 || !((px / 64 + py / 32) & 1)) return false;  // (not a corner there: off the grid)
        if (f.ed.terrainAt(px, py) != (c->high ? f.high : f.low)) return false;
    }
    return !corners.empty();
}

// A piece is on the map and fits its corners.
bool placeFits(const Fit& f, const RampPiece& p) {
    const auto [w, h] = rampPieceSize(f.k.era, f.facing, p.kind);
    return p.x >= 0 && p.y >= 0 && p.x + w <= f.ed.width() && p.y + h <= f.ed.height() && pieceFits(f, p.kind, p.x, p.y);
}

// All the ramp's pieces fit their corners.
bool cornersFit(const Fit& f, int slopes, int ax, int ay) {
    if (!f.low || !f.high) return false;
    for (const RampPiece& p : rampPieces(f.k.era, f.facing, slopes, ax, ay))
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
        for (int f = 0; f < kRampFacings; ++f)
            for (int n = 0; n <= 2; ++n) {
                std::map<std::pair<int, int>, uint16_t> at;
                for (const Editor::TileAt& c : rampTiles(era, f, n, 0, 0)) at[{c.x, c.y}] = c.value;
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
    if (!pieces.count(era))
        for (int f = 0; f < kRampFacings; ++f)
            for (const Editor::TileAt& c : rampTiles(era, f, 1, 0, 0)) pieces[era].insert(c.value);
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

bool rampFits(const Editor& ed, int facing, int slopes, int ax, int ay) {
    const RampKitData* k = kitOf(ed.era() & 7, facing);
    if (!k || !ed.canPaintTerrain()) return false;
    const Fit f(ed, *k, facing);
    if (!cornersFit(f, slopes, ax, ay)) return false;
    const std::vector<Editor::TileAt> tiles = rampTiles(k->era, facing, slopes, ax, ay);
    return edgeFits(ed, k->era, tiles) && rampJoins(ed.tileset(), tiles);
}

RampPlan planRamp(const Editor& ed, const std::vector<std::pair<int, int>>& stroke) {
    RampPlan plan;
    const int era = ed.era() & 7;
    if (!rampsAvailable(era)) return plan.problem = RampPlan::Problem::NoKit, plan;
    plan.low = kitOf(era, 0)->low, plan.high = kitOf(era, 0)->high;
    if (!ed.canPaintTerrain()) return plan.problem = RampPlan::Problem::NoIsom, plan;
    if (stroke.empty()) return plan;
    std::vector<Fit> fits;
    for (int f = 0; f < kRampFacings; ++f) fits.emplace_back(ed, *kitOf(era, f), f);
    // The middle of a ramp (map pixels).
    const auto middle = [&](int f, int n, int ax, int ay) {
        double cx = 0, cy = 0;
        const std::vector<Editor::TileAt> t = rampTiles(era, f, n, ax, ay);
        for (const Editor::TileAt& c : t) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
        return std::pair<double, double>{cx / double(t.size()), cy / double(t.size())};
    };
    // The ramp of no slope whose corners fit nearest the stroke's start: its facing and its edge.
    const auto [sx, sy] = stroke.front();
    double best = 1e18;
    for (int f = 0; f < kRampFacings; ++f)
        for (int ay = sy / 32 - 10; ay <= sy / 32 + 6; ++ay)
            for (int ax = sx / 32 - 12; ax <= sx / 32 + 6; ++ax) {
                if (!cornersFit(fits[size_t(f)], 0, ax, ay)) continue;
                const auto [mx, my] = middle(f, 0, ax, ay);
                if (const double d = std::hypot(mx - sx, my - sy); d < best && d < 192) best = d, plan.facing = f, plan.ax = ax, plan.ay = ay;
            }
    if (plan.facing < 0) return plan;
    const Fit& fit = fits[size_t(plan.facing)];
    // Along the edge (the slopes' step), from wall A's middle: how far the stroke goes each way. Of each anchor on this
    // edge (a whole tile from the first one along it) and each width there, the ramp whose wall A is nearest the stroke's
    // first end and wall B nearest its other end (an edge can have places half a slope apart that fit), of those that
    // fit: the corners under each piece, the edge, the join of the ground levels.
    const RampKitData& k = fit.k;
    const double ux = 32.0 * k.step[0], uy = 32.0 * k.step[1], len = std::hypot(ux, uy);
    const auto pieceMiddle = [&](const RampPiece& p) {
        const auto [w, h] = rampPieceSize(era, plan.facing, p.kind);
        return std::pair<double, double>{p.x * 32.0 + w * 16.0, p.y * 32.0 + h * 16.0};
    };
    const auto [ax0, ay0] = pieceMiddle(rampPieces(era, plan.facing, 0, plan.ax, plan.ay).front());
    const auto along = [&](double x, double y) { return ((x - ax0) * ux + (y - ay0) * uy) / len; };
    double lo = 1e18, hi = -1e18;
    for (const auto& [x, y] : stroke) lo = std::min(lo, along(x, y)), hi = std::max(hi, along(x, y));
    const double vx = -uy / len, vy = ux / len;  // (across the edge)
    const auto across = [&](double x, double y) { return (x - ax0) * vx + (y - ay0) * vy; };
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
            const std::vector<RampPiece> pieces = rampPieces(era, plan.facing, n, ax, ay);
            if (n && !placeFits(fit, pieces[size_t(n)])) break;  // (the new slope)
            const auto [bx, by] = pieceMiddle(pieces.back());
            const double cost = std::fabs(a - lo) + std::fabs(along(bx, by) - hi);
            if (cost < best2 && placeFits(fit, pieces.back())) {
                const std::vector<Editor::TileAt> tiles = rampTiles(era, plan.facing, n, ax, ay);
                if (edgeFits(ed, era, tiles) && rampJoins(ed.tileset(), tiles)) best2 = cost, plan.ax = ax, plan.ay = ay, plan.slopes = n;
            }
            if (along(bx, by) > hi + len && !overRamp) break;
        }
    }
    if (best2 > 1e17) return plan.facing = -1, plan;  // (no ramp on that edge fits and joins the ground)
    plan.tiles = rampTiles(era, plan.facing, plan.slopes, plan.ax, plan.ay);
    plan.problem = RampPlan::Problem::None;
    return plan;
}

std::vector<RampPlan> planRamps(const Editor& ed, const std::vector<std::pair<int, int>>& stroke, RampPlan* why) {
    std::vector<RampPlan> out;
    std::set<std::pair<int, int>> taken;
    size_t i = 0;
    while (i < stroke.size() && out.size() < 32) {
        const RampPlan p = planRamp(ed, std::vector<std::pair<int, int>>(stroke.begin() + std::ptrdiff_t(i), stroke.end()));
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
        if (!overlaps) {
            for (const Editor::TileAt& c : p.tiles) taken.insert({c.x, c.y});
            out.push_back(p);
        }
        i = next;
    }
    return out;
}

}  // namespace stardraft
