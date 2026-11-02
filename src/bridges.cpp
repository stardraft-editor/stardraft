// Bridges of any size (include/stardraft/bridges.h). A kit builds one orientation of one tileset: each cell of the
// bridge (a pair of tiles, the first on an even column) has a class along each axis (the start cap, the far cap or the
// middle, with its phase), and the table gives the tile pairs that the approved example bridges have for that class.
// The river: the bridge's origin is the first water diamond of the river (along the d kit's deck) plus the kit's offset;
// the u kit is the d kit's mirror, so its offset is the mirror of d's (found from the two kits' outlines).
#include "stardraft/bridges.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_set>
#include <random>

namespace stardraft {

namespace {

struct BridgeKitData {
    int era;
    bool u;
    int lengthStep[2], widthStep[2];  // pair cells (a pair cell: 2 tiles across, 1 down) for one step along each axis
    int capStart, capFar, side0, side1, periodLength, periodWidth, startLength, startWidth, halfLength, halfWidth;
    const char* ground;
    const char* water;
    int rowsAtSize1;  // the river's water diamonds (across it) under a bridge of length 1
    int offset[2];    // the d kit's origin from the river's first water diamond, in tiles
    int firstKey, keys;
};
struct BridgeKeyData {
    const char* key;
    int first, count;
};
struct BridgeChoiceData {
    uint16_t a, b;
    int weight;
};
struct BridgeExampleData {
    int era;
    bool u;
    int length, width, first, cells;
};
struct BridgeExampleCellData {
    int x, y;
    uint16_t a, b;
};
struct BridgeNeighborData {
    int era, way;  // way 0: b right of a; 1: b under a
    uint16_t a, b;
};
#include "data/bridge_kits.inc"

// Whether the map corpus has tile b next to tile a (right of it, or under it) in that tileset.
bool neighbors(int era, int way, uint16_t a, uint16_t b) {
    static const std::unordered_set<uint64_t> set = [] {
        std::unordered_set<uint64_t> s;
        for (const BridgeNeighborData& n : kBridgeNeighbors) s.insert(uint64_t(n.era) << 40 | uint64_t(n.way) << 32 | uint64_t(n.a) << 16 | n.b);
        return s;
    }();
    return set.count(uint64_t(era) << 40 | uint64_t(way) << 32 | uint64_t(a) << 16 | b) != 0;
}

const BridgeKitData* kitOf(int era, bool u) {
    for (const BridgeKitData& k : kBridgeKits)
        if (k.era == era && k.u == u) return &k;
    return nullptr;
}

// The class of position w along one axis of extent n: S<w> (the start cap), F<i> (the far cap), M<phase> (the middle).
std::string axisClass(int w, int n, int capStart, int capFar, int period) {
    if (w < capStart) return "S" + std::to_string(w);
    if (n - 1 - w < capFar) return "F" + std::to_string(n - 1 - w);
    return "M" + std::to_string((w - capStart) % period);
}

struct Cell {
    int x, y;  // the pair's first tile, from the origin
    const BridgeKeyData* key;
};

// The cells of a bridge with tiles (a key in the table), from its origin.
std::vector<Cell> cellsOf(const BridgeKitData& k, int length, int width) {
    const int extL = k.capStart + k.capFar + k.halfLength + 2 * length, extW = k.side0 + k.side1 + k.halfWidth + 2 * width;
    const int* L = k.lengthStep;
    const int* W = k.widthStep;
    const int det = L[0] * W[1] - L[1] * W[0];
    std::vector<Cell> out;
    if (!det) return out;
    for (int a = 0; a < extL; ++a)
        for (int c = 0; c < extW; ++c) {
            const int wl = a + k.startLength, ww = c + k.startWidth;
            const int pn = wl * W[1] - ww * L[1], yn = L[0] * ww - W[0] * wl;
            if (pn % det || yn % det) continue;  // (not a cell: the axes meet between cells)
            // (the bridge's kind first: length 0.5 or not, width 0.5 or not; some pieces have variants only at 0.5)
            const std::string key = std::string(length ? "n" : "h") + (width ? "n|" : "h|") + axisClass(a, extL, k.capStart, k.capFar, k.periodLength) +
                                    "|" + axisClass(c, extW, k.side0, k.side1, k.periodWidth);
            for (int i = 0; i < k.keys; ++i)
                if (!std::strcmp(kBridgeKeys[k.firstKey + i].key, key.c_str())) {
                    out.push_back({2 * (pn / det), yn / det, &kBridgeKeys[k.firstKey + i]});
                    break;
                }
        }
    return out;
}

// The u kit's origin from the river's first water diamond (in the mirrored river), in tiles: the u bridge is the mirror
// of the d bridge (tile x to -1 - x), so its origin is where its outline meets the mirrored d outline.
std::pair<int, int> uOffset(const BridgeKitData& d, const BridgeKitData& u, int length, int width, std::pair<int, int> offset) {
    int dx = INT_MAX, dy = INT_MAX, ux = INT_MAX, uy = INT_MAX;
    for (const Cell& c : cellsOf(d, length, width)) dx = std::min(dx, -1 - (c.x + 1)), dy = std::min(dy, c.y);
    for (const Cell& c : cellsOf(u, length, width)) ux = std::min(ux, c.x), uy = std::min(uy, c.y);
    if (dx == INT_MAX || ux == INT_MAX) return {0, 0};
    return {dx - ux - offset.first, dy - uy + offset.second};
}

}  // namespace

bool bridgesAvailable(int era) { return kitOf(era, false) && kitOf(era, true); }

std::vector<Editor::TileAt> bridgeTiles(int era, bool u, int length, int width, int ox, int oy, uint32_t seed) {
    std::vector<Editor::TileAt> out;
    const BridgeKitData* k = kitOf(era, u);
    if (!k) return out;
    for (const BridgeExampleData& e : kBridgeExamples)  // (an approved bridge of this size: as it is)
        if (e.era == era && e.u == u && e.length == length && e.width == width) {
            for (int i = e.first; i < e.first + e.cells; ++i) {
                const BridgeExampleCellData& c = kBridgeExampleCells[i];
                out.push_back({ox + c.x, oy + c.y, c.a});
                out.push_back({ox + c.x + 1, oy + c.y, c.b});
            }
            return out;
        }
    std::mt19937 rng(seed);
    // Row by row, left to right: each piece takes a variant whose tiles the corpus has next to the tiles already placed
    // left of it and above it (else the variant with the fewest pairs it does not have), by weight among them.
    std::vector<Cell> cells = cellsOf(*k, length, width);
    std::sort(cells.begin(), cells.end(), [](const Cell& p, const Cell& q) { return p.y != q.y ? p.y < q.y : p.x < q.x; });
    // Then a few passes in which each piece with variants takes the one that fits all its neighbors best (also those
    // right of it and under it), until no piece changes.
    std::map<std::pair<int, int>, uint16_t> placed;
    const auto misfits = [&](int x, int y, const BridgeChoiceData& v) {
        const auto at = [&](int px, int py) { auto i = placed.find({px, py}); return i == placed.end() ? -1 : int(i->second); };
        const auto no = [&](int way, int a, int b) { return a >= 0 && b >= 0 && !neighbors(era, way, uint16_t(a), uint16_t(b)); };
        return int(no(0, v.a, v.b)) + no(0, at(x - 1, y), v.a) + no(0, v.b, at(x + 2, y)) + no(1, at(x, y - 1), v.a) + no(1, at(x + 1, y - 1), v.b) +
               no(1, v.a, at(x, y + 1)) + no(1, v.b, at(x + 1, y + 1));
    };
    std::vector<int> picks(cells.size(), 0);
    const auto choose = [&](size_t i) {
        const Cell& c = cells[i];
        const BridgeChoiceData* ch = &kBridgeChoices[c.key->first];
        placed.erase({c.x, c.y}), placed.erase({c.x + 1, c.y});  // (this piece's own tiles do not count)
        int best = INT_MAX, total = 0;
        std::vector<int> score(size_t(c.key->count));
        for (int j = 0; j < c.key->count; ++j) best = std::min(best, score[size_t(j)] = misfits(c.x, c.y, ch[j]));
        for (int j = 0; j < c.key->count; ++j)
            if (score[size_t(j)] == best) total += ch[j].weight;
        int r = total > 0 ? int(rng() % uint32_t(total)) : 0, pick = -1;
        for (int j = 0; j < c.key->count && pick < 0; ++j)
            if (score[size_t(j)] == best && (r -= ch[j].weight) < 0) pick = j;
        if (pick < 0) pick = 0;
        const bool changed = picks[i] != pick;
        picks[i] = pick;
        placed[{c.x, c.y}] = ch[pick].a, placed[{c.x + 1, c.y}] = ch[pick].b;
        return changed;
    };
    for (size_t i = 0; i < cells.size(); ++i) choose(i);
    for (int pass = 0; pass < 6; ++pass) {
        bool changed = false;
        for (size_t i = 0; i < cells.size(); ++i)
            if (cells[i].key->count > 1) {
                const BridgeChoiceData* ch = &kBridgeChoices[cells[i].key->first];
                placed.erase({cells[i].x, cells[i].y}), placed.erase({cells[i].x + 1, cells[i].y});
                const int now = misfits(cells[i].x, cells[i].y, ch[picks[size_t(i)]]);
                placed[{cells[i].x, cells[i].y}] = ch[picks[i]].a, placed[{cells[i].x + 1, cells[i].y}] = ch[picks[i]].b;
                if (now > 0) changed |= choose(i);  // (a piece that fits keeps its variant)
            }
        if (!changed) break;
    }
    for (size_t i = 0; i < cells.size(); ++i) {
        const BridgeChoiceData& v = kBridgeChoices[cells[i].key->first + picks[i]];
        out.push_back({ox + cells[i].x, oy + cells[i].y, v.a});
        out.push_back({ox + cells[i].x + 1, oy + cells[i].y, v.b});
    }
    return out;
}

bool bridgeMatches(const Editor& ed, bool u, int length, int width, int ox, int oy) {
    const BridgeKitData* k = kitOf(ed.era() & 7, u);
    if (!k) return false;
    const std::vector<uint16_t>& tiles = ed.terrain().tiles;
    const std::vector<Cell> cells = cellsOf(*k, length, width);
    for (const Cell& c : cells) {
        const int x = ox + c.x, y = oy + c.y;
        if (x < 0 || y < 0 || x + 1 >= ed.width() || y >= ed.height()) return false;
        const uint16_t a = tiles[size_t(y) * ed.width() + x], b = tiles[size_t(y) * ed.width() + x + 1];
        bool any = false;
        for (int i = 0; i < c.key->count && !any; ++i) any = kBridgeChoices[c.key->first + i].a == a && kBridgeChoices[c.key->first + i].b == b;
        if (!any) return false;
    }
    return !cells.empty();
}

namespace {

// A diamond of the ISOM grid: its middle is the rect-grid point (x, y), x + y even (map pixel 64x, 32y); its corners
// are (x +- 1, y) and (x, y +- 1).
struct Diamond {
    int x, y;
    Diamond operator+(Diamond o) const { return {x + o.x, y + o.y}; }
    Diamond operator*(int k) const { return {x * k, y * k}; }
};

struct Terrains {
    const Editor& ed;
    std::string ground, water;
    std::string at(int cx, int cy) const { return ed.terrainName(ed.terrainAt(64 * cx, 32 * cy)); }
    // Water: all four corners are the water.
    bool water4(Diamond d) const {
        return at(d.x - 1, d.y) == water && at(d.x + 1, d.y) == water && at(d.x, d.y - 1) == water && at(d.x, d.y + 1) == water;
    }
    // A corner is the water.
    bool water1(Diamond d) const {
        return at(d.x - 1, d.y) == water || at(d.x + 1, d.y) == water || at(d.x, d.y - 1) == water || at(d.x, d.y + 1) == water;
    }
    // Ground: a corner is the ground.
    bool hasGround(Diamond d) const {
        return at(d.x - 1, d.y) == ground || at(d.x + 1, d.y) == ground || at(d.x, d.y - 1) == ground || at(d.x, d.y + 1) == ground;
    }
};

Diamond nearestDiamond(int px, int py) {
    const int fx = int(std::floor(px / 64.0)), fy = int(std::floor(py / 32.0));
    Diamond best{fx, fy};
    double bd = 1e18;
    for (int y = fy - 1; y <= fy + 2; ++y)
        for (int x = fx - 1; x <= fx + 2; ++x) {
            if ((x + y) & 1) continue;
            const double d = std::hypot(64.0 * x - px, 32.0 * y - py);
            if (d < bd) bd = d, best = {x, y};
        }
    return best;
}

}  // namespace

BridgePlan planBridge(const Editor& ed, int x0, int y0, int x1, int y1, int width, uint32_t seed) {
    BridgePlan plan;
    const int era = ed.era() & 7;
    const BridgeKitData* d = kitOf(era, false);
    const BridgeKitData* uk = kitOf(era, true);
    if (!d || !uk) return plan.problem = BridgePlan::Problem::NoKit, plan;
    plan.ground = d->ground, plan.water = d->water;
    if (!ed.canPaintTerrain()) return plan.problem = BridgePlan::Problem::NoIsom, plan;
    const int dx = x1 - x0, dy = y1 - y0;
    // The drag's diagonal: down-right (1, 1) or up-right (1, -1), and its way along it.
    const bool downRight = (dx >= 0) == (dy >= 0);
    const Diamond step{dx >= 0 ? 1 : -1, downRight == (dx >= 0) ? 1 : -1};
    // The kit: d if its deck runs along the drag's diagonal, else u (the mirror). deck: the d kit's direction of the
    // river's rows (mirrored for u); across: along the river.
    const bool dDownRight = (d->lengthStep[0] > 0) == (d->lengthStep[1] > 0);
    plan.u = dDownRight != downRight;
    const Diamond deck = plan.u ? Diamond{-d->lengthStep[0], d->lengthStep[1]} : Diamond{d->lengthStep[0], d->lengthStep[1]};
    const Diamond across = plan.u ? Diamond{-d->widthStep[0], d->widthStep[1]} : Diamond{d->widthStep[0], d->widthStep[1]};
    plan.width = std::max(0, width);

    const Terrains t{ed, d->ground, d->water};
    const Diamond s = nearestDiamond(x0, y0);
    const int steps = int((double(dx) * 64 * step.x + double(dy) * 32 * step.y) / (64.0 * 64 + 32.0 * 32));  // diamonds along the drag
    if (t.water4(s) || !t.hasGround(s)) return plan.problem = BridgePlan::Problem::NeedsTerrain, plan;
    const int far = std::max(ed.width(), ed.height());
    // The water: rows of whole water diamonds across the drag; or (half) a line of water corners, the edge between two
    // diamonds (the half-size brush's river: the shortest bridge where one row of diamonds is length 1).
    int k1 = 1;
    while (k1 <= steps && !t.water1(s + step * k1)) ++k1;
    if (k1 > steps) return plan.problem = BridgePlan::Problem::Short, plan;
    // (the bank's diamonds have water corners: the rows start at the first diamond that is all water, if there is one)
    int k4 = k1;
    while (k4 <= k1 + far && t.water1(s + step * k4) && !t.water4(s + step * k4)) ++k4;
    if (t.water4(s + step * k4)) k1 = k4;
    int rows = 0;
    while (rows < far && t.water4(s + step * (k1 + rows))) ++rows;
    // The ground: on a bank diamond next to the water, or the one after it (a wide river's banks are 2 diamonds deep).
    const auto groundNear = [&](int k, int way) { return t.hasGround(s + step * k) || t.hasGround(s + step * (k + way)); };
    if (!rows && d->rowsAtSize1 > 1 && t.water1(s + step * (k1 + 1))) return plan.problem = BridgePlan::Problem::TooNarrow, plan;
    const bool half = !rows && d->rowsAtSize1 == 1 && deck.y == 1 && t.water1(s + step * (k1 + 1));
    if (!rows && !half) return plan.problem = BridgePlan::Problem::NeedsTerrain, plan;
    const int span = half ? 2 : rows;  // the diamonds with water on the drag's line
    if (steps < k1 + span) return plan.problem = BridgePlan::Problem::ReachGround, plan;
    if (!groundNear(k1 + span, 1) || !groundNear(k1 - 1, -1) || (half && t.water1(s + step * (k1 + span))))
        return plan.problem = BridgePlan::Problem::NeedsTerrain, plan;
    plan.length = half ? 0 : rows - d->rowsAtSize1 + 1;
    if (plan.length < 0) return plan.problem = BridgePlan::Problem::TooNarrow, plan;

    // The river's first water diamond along the deck (on the drag's line); the half river's: the second of its two (the
    // one whose top corner is the water).
    const bool sameWay = step.x == deck.x && step.y == deck.y;
    const Diamond first = s + step * (sameWay ? k1 : k1 + span - 1) + (half ? deck : Diamond{0, 0});
    const std::pair<int, int> dOff{d->offset[0], d->offset[1]};
    const std::pair<int, int> off = plan.u ? uOffset(*d, *uk, plan.length, plan.width, dOff) : dOff;
    const auto tilesFrom = [&](Diamond r) { return bridgeTiles(era, plan.u, plan.length, plan.width, 2 * r.x + off.first, r.y + off.second, seed); };

    // Whether the river under the bridge whose first water diamond is r is straight: the same rows of water under the
    // whole bridge, and one diamond more at each side.
    const double vx = 64.0 * deck.x, vy = 32.0 * deck.y, rx = 64.0 * across.x, ry = 32.0 * across.y, det = vx * ry - vy * rx;
    const auto straight = [&](Diamond r, const std::vector<Editor::TileAt>& tiles) {
        double lo = 1e9, hi = -1e9;
        for (const Editor::TileAt& c : tiles) {
            const double ex = 32.0 * c.x + 16 - 64.0 * r.x, ey = 32.0 * c.y + 16 - 32.0 * r.y, b = (vx * ey - vy * ex) / det;
            lo = std::min(lo, b), hi = std::max(hi, b);
        }
        for (int j = int(std::floor(lo)) - 1; j <= int(std::ceil(hi)) + 1; ++j) {
            const Diamond line = r + across * j;
            if (half) {
                if (!t.water1(line) || !t.water1(line + deck * -1) || t.water1(line + deck) || t.water1(line + deck * -2)) return false;
                continue;
            }
            if (t.water4(line + deck * -1) || t.water4(line + deck * rows)) return false;
            for (int m = 0; m < rows; ++m)
                if (!t.water4(line + deck * m)) return false;
        }
        return true;
    };
    // The bridge lies across the drag's line: its middle moves along the river to the line (else it stays where it is).
    std::vector<Editor::TileAt> tiles = tilesFrom(first);
    if (tiles.empty()) return plan.problem = BridgePlan::Problem::NoKit, plan;
    double cx = 0, cy = 0;
    for (const Editor::TileAt& c : tiles) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
    cx /= double(tiles.size()), cy /= double(tiles.size());
    const int shift = int(std::lround(-((cx - x0) * vy - (cy - y0) * vx) / (rx * vy - ry * vx)));
    bool ok = false;
    for (const int j : {shift, 0}) {
        const Diamond r = first + across * j;
        std::vector<Editor::TileAt> at = j ? tilesFrom(r) : tiles;
        if (straight(r, at)) {
            tiles = std::move(at), ok = true;
            break;
        }
        if (!shift) break;
    }
    if (!ok) return plan.problem = BridgePlan::Problem::NotStraight, plan;
    for (const Editor::TileAt& c : tiles)
        if (c.x < 0 || c.y < 0 || c.x >= ed.width() || c.y >= ed.height()) return plan.problem = BridgePlan::Problem::OffMap, plan;
    plan.tiles = std::move(tiles);
    plan.problem = BridgePlan::Problem::None;
    return plan;
}

}  // namespace stardraft
