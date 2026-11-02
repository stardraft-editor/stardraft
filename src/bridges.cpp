// Bridges of any size (include/stardraft/bridges.h). A kit builds one orientation of one tileset: each cell of the
// bridge (a pair of tiles, the first on an even column) has a class along each axis (the start cap, the far cap or the
// middle, with its phase), and the table gives the tile pairs that the approved example bridges have for that class.
// The river: the bridge's origin is the first water diamond of the river (along the d kit's deck) plus the kit's offset;
// the u kit is the d kit's mirror, so its offset is the mirror of d's (found from the two kits' outlines).
#include "stardraft/bridges.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_set>
#include <random>
#include <set>

namespace stardraft {

namespace {

struct BridgeKitData {
    int era;
    bool u;
    int lengthStep[2], widthStep[2];  // pair cells (a pair cell: 2 tiles across, 1 down) for one step along each axis
    int capStart, capFar, side0, side1, periodLength, periodWidth, startLength, startWidth, halfLength, halfWidth;
    const char* ground;
    const char* water;
    const char* road;  // under the bridge's ends (Badlands: Asphalt); "": the ground
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

// A tile of a terrain group (groups below 1024; above: doodads' pieces) as all of its group's tiles: they are random
// variants of one piece (the corpus pairs are of groups there).
uint16_t variant(uint16_t v) { return (v >> 4) < 1024 ? uint16_t(v & ~15) : v; }

// Whether the map corpus has tile b next to tile a (right of it, or under it) in that tileset.
bool neighbors(int era, int way, uint16_t a, uint16_t b) {
    a = variant(a), b = variant(b);
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
    int a, c;  // its place along the length and along the width (from the start cap and side 0)
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
                    out.push_back({2 * (pn / det), yn / det, &kBridgeKeys[k.firstKey + i], a, c});
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

// The pairs of neighbors (side by side, one above the other) in the tiles that the corpus does not have.
int misfitCount(int era, const std::vector<Editor::TileAt>& tiles) {
    std::map<std::pair<int, int>, uint16_t> at;
    for (const Editor::TileAt& c : tiles) at[{c.x, c.y}] = c.value;
    int n = 0;
    for (const auto& [p, v] : at) {
        if (auto r = at.find({p.first + 1, p.second}); r != at.end()) n += !neighbors(era, 0, v, r->second);
        if (auto b = at.find({p.first, p.second + 1}); b != at.end()) n += !neighbors(era, 1, v, b->second);
    }
    return n;
}

// A place along one axis of a bridge of extent n, in an approved bridge of extent ne (n >= ne): the same place near the
// caps (the cap and 2 more), from the far end near the far cap, and in the middle the approved bridge's middle places
// after those again and again, q of them (q even: the same phase and parity). -1: the approved bridge is too short.
int exampleAxis(int a, int n, int ne, int capStart, int capFar, int q) {
    if (n == ne || a < capStart + 2) return a;
    if (n - 1 - a < capFar + 2) return ne - 1 - (n - 1 - a);
    if (capStart + 2 + q > ne - capFar - 2) return -1;
    return capStart + 2 + (a - capStart - 2) % q;
}

// The bridge made of the approved bridge of the kit that is as large as it in each way or the largest: its caps as they
// are, and its middle rows again as often as the size needs. Empty: no approved bridge of the kit.
std::vector<Editor::TileAt> fromExample(const BridgeKitData& k, int era, bool u, int length, int width, int ox, int oy) {
    int le = -1, we = -1;
    for (const BridgeExampleData& e : kBridgeExamples)
        if (e.era == era && e.u == u) le = std::max(le, std::min(length, e.length)), we = std::max(we, std::min(width, e.width));
    const BridgeExampleData* e = nullptr;
    for (const BridgeExampleData& x : kBridgeExamples)
        if (x.era == era && x.u == u && x.length == le && x.width == we) e = &x;
    if (!e) return {};
    std::map<std::pair<int, int>, std::pair<int, int>> axes;  // (the approved bridge's cells: their places)
    for (const Cell& c : cellsOf(k, le, we)) axes[{c.x, c.y}] = {c.a, c.c};
    std::map<std::pair<int, int>, const BridgeExampleCellData*> byAxes;
    for (int i = e->first; i < e->first + e->cells; ++i)
        if (auto p = axes.find({kBridgeExampleCells[i].x, kBridgeExampleCells[i].y}); p != axes.end()) byAxes[p->second] = &kBridgeExampleCells[i];
    const int extL = k.capStart + k.capFar + k.halfLength + 2 * length, extW = k.side0 + k.side1 + k.halfWidth + 2 * width;
    const int extLe = k.capStart + k.capFar + k.halfLength + 2 * le, extWe = k.side0 + k.side1 + k.halfWidth + 2 * we;
    // (the middle repeated every 2, 4 or 6 places each way: the one whose pieces fit their neighbors best)
    std::vector<Editor::TileAt> best;
    int fewest = INT_MAX;
    for (const int qL : {2, 4, 6})
        for (const int qW : {2, 4, 6}) {
            std::vector<Editor::TileAt> out;
            bool ok = true;
            for (const Cell& c : cellsOf(k, length, width)) {
                const auto p = byAxes.find({exampleAxis(c.a, extL, extLe, k.capStart, k.capFar, qL), exampleAxis(c.c, extW, extWe, k.side0, k.side1, qW)});
                if (p == byAxes.end()) {
                    ok = false;
                    break;
                }
                out.push_back({ox + c.x, oy + c.y, p->second->a});
                out.push_back({ox + c.x + 1, oy + c.y, p->second->b});
            }
            if (!ok) continue;
            if (const int n = misfitCount(era, out); n < fewest) fewest = n, best = std::move(out);
        }
    return best;
}

// How well two tiles meet (b right of a: way 0; under it: way 1): 0 where the game's maps have them so, else 1 and the
// mean difference of the pixels on the two sides of their edge (0 to 255; the tiles' graphics, `ts`; without them: 1).
class Seams {
public:
    Seams(int era, const Tileset* ts) : era_(era), ts_(ts) {}
    double cost(int way, uint16_t a, uint16_t b) {
        if (neighbors(era_, way, a, b)) return 0;
        if (!ts_) return 1;
        const Edges& ea = edges(a);
        const Edges& eb = edges(b);
        const std::array<Rgb, 32>& p = way ? ea.bottom : ea.right;
        const std::array<Rgb, 32>& q = way ? eb.top : eb.left;
        int sum = 0;
        for (size_t i = 0; i < 32; ++i) sum += std::abs(p[i].r - q[i].r) + std::abs(p[i].g - q[i].g) + std::abs(p[i].b - q[i].b);
        return 1 + sum / (32.0 * 3);
    }

private:
    struct Edges {
        std::array<Rgb, 32> left, right, top, bottom;
    };
    const Edges& edges(uint16_t v) {
        auto i = cache_.find(v);
        if (i != cache_.end()) return i->second;
        Edges e{};
        const int m = ts_->megatileFor(v);
        if (m >= 0 && size_t(m) < ts_->megatiles.size()) {
            const auto pixel = [&](int px, int py) {
                const uint32_t mini = ts_->megatiles[size_t(m)][size_t((py / 8) * 4 + px / 8)];
                const size_t n = mini >> 1;
                const int lx = (mini & 1) ? 7 - px % 8 : px % 8;
                return n < ts_->minitiles.size() ? ts_->palette[ts_->minitiles[n][size_t((py % 8) * 8 + lx)]] : Rgb{};
            };
            for (int i = 0; i < 32; ++i)
                e.left[size_t(i)] = pixel(0, i), e.right[size_t(i)] = pixel(31, i), e.top[size_t(i)] = pixel(i, 0), e.bottom[size_t(i)] = pixel(i, 31);
        }
        return cache_.emplace(v, e).first->second;
    }
    int era_;
    const Tileset* ts_;
    std::map<uint16_t, Edges> cache_;
};

// The seams of a bridge's tiles: of each two tiles side by side or one above the other in it, and (`map`) of each of its
// tiles and the map's tile next to it.
double seamCost(Seams& seams, const std::vector<Editor::TileAt>& tiles, const Editor* map) {
    std::map<std::pair<int, int>, uint16_t> at;
    for (const Editor::TileAt& c : tiles) at[{c.x, c.y}] = c.value;
    const auto tile = [&](int x, int y) -> int {
        if (auto i = at.find({x, y}); i != at.end()) return i->second;
        if (!map || x < 0 || y < 0 || x >= map->width() || y >= map->height()) return -1;
        return map->terrain().tiles[size_t(y) * map->width() + x];
    };
    double n = 0;
    for (const auto& [p, v] : at) {
        const auto [x, y] = p;
        if (const int r = tile(x + 1, y); r >= 0) n += seams.cost(0, v, uint16_t(r));
        if (const int b = tile(x, y + 1); b >= 0) n += seams.cost(1, v, uint16_t(b));
        if (!at.count({x - 1, y}))
            if (const int l = tile(x - 1, y); l >= 0) n += seams.cost(0, uint16_t(l), v);
        if (!at.count({x, y - 1}))
            if (const int t = tile(x, y - 1); t >= 0) n += seams.cost(1, uint16_t(t), v);
    }
    return n;
}

}  // namespace

bool bridgesAvailable(int era) { return kitOf(era, false) && kitOf(era, true); }

std::vector<Editor::TileAt> bridgeTiles(int era, bool u, int length, int width, int ox, int oy, uint32_t seed, const Editor* on, bool around) {
    std::vector<Editor::TileAt> out;
    const BridgeKitData* k = kitOf(era, u);
    if (!k) return out;
    // (the map's tiles round the bridge count only when asked: `around`)
    const Editor* map = around ? on : nullptr;
    Seams seams(era, on ? &on->tileset() : nullptr);
    std::vector<Editor::TileAt> example;  // (an approved bridge of this size: as it is)
    for (const BridgeExampleData& e : kBridgeExamples)
        if (e.era == era && e.u == u && e.length == length && e.width == width)
            for (int i = e.first; i < e.first + e.cells; ++i) {
                const BridgeExampleCellData& c = kBridgeExampleCells[i];
                example.push_back({ox + c.x, oy + c.y, c.a});
                example.push_back({ox + c.x + 1, oy + c.y, c.b});
            }
    if (!on && !example.empty()) return example;
    std::mt19937 rng(seed);
    // Row by row, left to right: each piece takes a variant whose tiles the corpus has next to the tiles already placed
    // left of it and above it (else the variant with the fewest pairs it does not have), by weight among them.
    std::vector<Cell> cells = cellsOf(*k, length, width);
    std::sort(cells.begin(), cells.end(), [](const Cell& p, const Cell& q) { return p.y != q.y ? p.y < q.y : p.x < q.x; });
    // Then a few passes in which each piece with variants takes the one that fits all its neighbors best (also those
    // right of it and under it), until no piece changes.
    std::map<std::pair<int, int>, uint16_t> placed;
    std::set<std::pair<int, int>> mine;  // (the bridge's tiles: the map's tiles there do not count)
    for (const Cell& c : cells) mine.insert({c.x, c.y}), mine.insert({c.x + 1, c.y});
    const auto misfits = [&](int x, int y, const BridgeChoiceData& v) {
        const auto at = [&](int px, int py) {
            if (auto i = placed.find({px, py}); i != placed.end()) return int(i->second);
            const int mx = ox + px, my = oy + py;
            if (!map || mine.count({px, py}) || mx < 0 || my < 0 || mx >= map->width() || my >= map->height()) return -1;
            return int(map->terrain().tiles[size_t(my) * map->width() + mx]);
        };
        const auto no = [&](int way, int a, int b) { return a >= 0 && b >= 0 ? seams.cost(way, uint16_t(a), uint16_t(b)) : 0.0; };
        return no(0, v.a, v.b) + no(0, at(x - 1, y), v.a) + no(0, v.b, at(x + 2, y)) + no(1, at(x, y - 1), v.a) + no(1, at(x + 1, y - 1), v.b) +
               no(1, v.a, at(x, y + 1)) + no(1, v.b, at(x + 1, y + 1));
    };
    std::vector<int> picks(cells.size(), 0);
    const auto choose = [&](size_t i) {
        const Cell& c = cells[i];
        const BridgeChoiceData* ch = &kBridgeChoices[c.key->first];
        placed.erase({c.x, c.y}), placed.erase({c.x + 1, c.y});  // (this piece's own tiles do not count)
        double best = 1e18;
        int total = 0;
        std::vector<double> score(size_t(c.key->count));
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
                const double now = misfits(cells[i].x, cells[i].y, ch[picks[size_t(i)]]);
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
    // The approved bridge made larger, where its pieces fit their neighbors better (fewer pairs that no map has).
    if (!on) {
        if (std::vector<Editor::TileAt> grown = fromExample(*k, era, u, length, width, ox, oy);
            !grown.empty() && grown.size() == out.size() && misfitCount(era, grown) < misfitCount(era, out))
            return grown;
        return out;
    }
    // With the tiles' graphics: of these, the approved bridge of this size and the approved bridge made larger, the one
    // with the least seams (the pairs the game's maps have: none; else how much the pixels differ at the edge).
    double least = seamCost(seams, out, map);
    if (example.size() == out.size())
        if (const double c = seamCost(seams, example, map); c < least) least = c, out = example;
    if (std::vector<Editor::TileAt> grown = fromExample(*k, era, u, length, width, ox, oy); grown.size() == out.size() && seamCost(seams, grown, map) < least)
        out = std::move(grown);
    return out;
}

double bridgeSeams(const Editor& ed, const std::vector<Editor::TileAt>& tiles, bool around) {
    Seams seams(ed.era() & 7, &ed.tileset());
    return seamCost(seams, tiles, around ? &ed : nullptr);
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
    std::string water;
    // (a game terrain's name without the ground it stands on, which a mod's tab adds: "Water (Dirt)")
    std::string at(int cx, int cy) const {
        const std::string n = ed.terrainName(ed.terrainAt(64 * cx, 32 * cy));
        return n.substr(0, n.find(" ("));
    }
    // Water: all four corners are the water.
    bool water4(Diamond d) const {
        return at(d.x - 1, d.y) == water && at(d.x + 1, d.y) == water && at(d.x, d.y - 1) == water && at(d.x, d.y + 1) == water;
    }
    // A corner is the water.
    bool water1(Diamond d) const {
        return at(d.x - 1, d.y) == water || at(d.x + 1, d.y) == water || at(d.x, d.y - 1) == water || at(d.x, d.y + 1) == water;
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

    const Terrains t{ed, d->water};
    const Diamond s = nearestDiamond(x0, y0);
    const int steps = int((double(dx) * 64 * step.x + double(dy) * 32 * step.y) / (64.0 * 64 + 32.0 * 32));  // diamonds along the drag
    // No bridge: why, and the tiles of a bridge as long as the drag with its middle on the drag's middle (the red preview).
    const auto fail = [&](BridgePlan::Problem why) {
        plan.problem = why;
        const int length = std::clamp(steps - d->rowsAtSize1 - 1, 0, 64);
        const std::pair<int, int> dOff{d->offset[0], d->offset[1]};
        const std::pair<int, int> o = plan.u ? uOffset(*d, *uk, length, plan.width, dOff) : dOff;
        const std::vector<Editor::TileAt> draft = bridgeTiles(era, plan.u, length, plan.width, o.first, o.second, seed);
        if (draft.empty()) return plan;
        double cx = 0, cy = 0;
        for (const Editor::TileAt& c : draft) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
        cx /= double(draft.size()), cy /= double(draft.size());
        const Diamond m = nearestDiamond(int(std::lround((x0 + x1) / 2.0 - cx)), int(std::lround((y0 + y1) / 2.0 - cy)));
        for (const Editor::TileAt& c : draft)
            if (const int x = c.x + 2 * m.x, y = c.y + m.y; x >= 0 && y >= 0 && x < ed.width() && y < ed.height()) plan.draft.push_back({x, y, c.value});
        return plan;
    };
    // (the drag starts on land, of any terrain: the shore makes the banks the kit's ground)
    if (t.water4(s)) return fail(BridgePlan::Problem::NeedsTerrain);
    const int far = std::max(ed.width(), ed.height());
    // The water: rows of whole water diamonds across the drag; or (half) a line of water corners, the edge between two
    // diamonds (the half-size brush's river: the shortest bridge where one row of diamonds is length 1).
    int k1 = 1;
    while (k1 <= steps && !t.water1(s + step * k1)) ++k1;
    if (k1 > steps) return fail(BridgePlan::Problem::Short);
    // (the bank's diamonds have water corners: the rows start at the first diamond that is all water, if there is one)
    int k4 = k1;
    while (k4 <= k1 + far && t.water1(s + step * k4) && !t.water4(s + step * k4)) ++k4;
    if (t.water4(s + step * k4)) k1 = k4;
    int rows = 0;
    while (rows < far && t.water4(s + step * (k1 + rows))) ++rows;
    if (!rows && d->rowsAtSize1 > 1 && t.water1(s + step * (k1 + 1))) return fail(BridgePlan::Problem::TooNarrow);
    const bool half = !rows && d->rowsAtSize1 == 1 && deck.y == 1 && t.water1(s + step * (k1 + 1));
    if (!rows && !half) return fail(BridgePlan::Problem::NeedsTerrain);
    const int span = half ? 2 : rows;  // the diamonds with water on the drag's line
    if (steps < k1 + span) return fail(BridgePlan::Problem::ReachGround);
    if (half && t.water1(s + step * (k1 + span))) return fail(BridgePlan::Problem::NeedsTerrain);
    plan.length = half ? 0 : rows - d->rowsAtSize1 + 1;
    if (plan.length < 0) return fail(BridgePlan::Problem::TooNarrow);

    // The river's first water diamond along the deck (on the drag's line); the half river's: the second of its two (the
    // one whose top corner is the water).
    const bool sameWay = step.x == deck.x && step.y == deck.y;
    const Diamond first = s + step * (sameWay ? k1 : k1 + span - 1) + (half ? deck : Diamond{0, 0});
    const std::pair<int, int> dOff{d->offset[0], d->offset[1]};
    const std::pair<int, int> off = plan.u ? uOffset(*d, *uk, plan.length, plan.width, dOff) : dOff;
    const auto tilesFrom = [&](Diamond r) {
        plan.ox = 2 * r.x + off.first, plan.oy = r.y + off.second, plan.seed = seed;
        return bridgeTiles(era, plan.u, plan.length, plan.width, plan.ox, plan.oy, seed, &ed);
    };

    // (the kit's shortest approved bridge: Badlands has none of length 0.5)
    int shortest = INT_MAX;
    for (const BridgeExampleData& e : kBridgeExamples)
        if (e.era == era && e.u == plan.u) shortest = std::min(shortest, e.length);
    if (shortest != INT_MAX && plan.length < shortest) return fail(BridgePlan::Problem::TooNarrow);

    // The bridge lies across the drag's line: its middle moves along the river to the line.
    const double vx = 64.0 * deck.x, vy = 32.0 * deck.y, rx = 64.0 * across.x, ry = 32.0 * across.y;
    std::vector<Editor::TileAt> tiles = tilesFrom(first);
    if (tiles.empty()) return plan.problem = BridgePlan::Problem::NoKit, plan;
    double cx = 0, cy = 0;
    for (const Editor::TileAt& c : tiles) cx += 32.0 * c.x + 16, cy += 32.0 * c.y + 16;
    cx /= double(tiles.size()), cy /= double(tiles.size());
    const int shift = int(std::lround(-((cx - x0) * vy - (cy - y0) * vx) / (rx * vy - ry * vx)));
    const Diamond r = first + across * shift;
    if (shift) tiles = tilesFrom(r);
    for (const Editor::TileAt& c : tiles)
        if (c.x < 0 || c.y < 0 || c.x >= ed.width() || c.y >= ed.height()) return fail(BridgePlan::Problem::OffMap);

    // The shore, in the ISOM corners' rows along the deck (a row: the corners on one line across the river, two apart;
    // row 0: the back corners of the first water diamond r): only what the bridge needs, the terrain round it stays. The
    // water: rows 0 to `rows` (the game's and the approved bridges: one row more than the water diamonds), from 1 corner
    // before the bridge's side to 1 after; a bank of ground on each side of it, as wide; under the bridge's ends ground
    // where there is water (other land stays), or the road (one diamond past each end).
    const auto along = [&](double X, double Y) { return X * deck.x + Y * deck.y; };   // (rect-grid points)
    const auto side = [&](double X, double Y) { return X * across.x + Y * across.y; };
    double s0 = INT_MAX, s1 = INT_MIN, t0 = INT_MAX, t1 = INT_MIN;
    for (const Editor::TileAt& c : tiles) {
        const double X = (c.x + 0.5) / 2, Y = c.y + 0.5;
        s0 = std::min(s0, along(X, Y)), s1 = std::max(s1, along(X, Y)), t0 = std::min(t0, side(X, Y)), t1 = std::max(t1, side(X, Y));
    }
    // (each end's width: the tiles of its last row; the road goes on from the end as wide as it)
    double n0 = INT_MAX, n1 = INT_MIN, f0 = INT_MAX, f1 = INT_MIN;
    for (const Editor::TileAt& c : tiles) {
        const double X = (c.x + 0.5) / 2, Y = c.y + 0.5, s = along(X, Y), tt = side(X, Y);
        if (s <= s0 + 1) n0 = std::min(n0, tt), n1 = std::max(n1, tt);
        if (s >= s1 - 1) f0 = std::min(f0, tt), f1 = std::max(f1, tt);
    }
    const int row0 = r.x * deck.x + r.y * deck.y - 1;
    const auto terrainId = [&](const char* name) {
        for (uint16_t id = 1; id < 256; ++id)
            if (ed.terrainName(id) == name) return id;
        for (uint16_t id = 1; id < 256; ++id)  // (in a mod's tab: "Water (Dirt)", the game terrains first)
            if (ed.terrainName(id).rfind(std::string(name) + " (", 0) == 0) return id;
        return uint16_t(0);
    };
    const uint16_t ground = terrainId(d->ground), water = terrainId(d->water), road = *d->road ? terrainId(d->road) : ground;
    std::vector<CornerValue> grounds, roads, waters;
    int bx0 = INT_MAX, by0 = INT_MAX, bx1 = INT_MIN, by1 = INT_MIN;  // (the bridge's rect-grid box)
    for (const Editor::TileAt& c : tiles) bx0 = std::min(bx0, c.x / 2), by0 = std::min(by0, c.y), bx1 = std::max(bx1, c.x / 2 + 1), by1 = std::max(by1, c.y + 1);
    const int margin = 6;
    std::set<std::pair<int, int>> roadCorners;
    for (int Y = std::max(-1, by0 - margin); Y <= std::min(int(ed.height()), by1 + margin); ++Y)
        for (int X = std::max(-1, bx0 - margin); X <= std::min(int(ed.width()) / 2, bx1 + margin); ++X) {
            const double s = along(X, Y), tt = side(X, Y);
            if (!((X + Y) & 1)) {
                // A diamond's middle: the road is of whole diamonds (as wide as the bridge's end, on 3 diamonds past each
                // end, as the game's bridges have it), clear of the banks.
                const double row = (s - row0) / 2;
                const bool nearEnd = s < (s0 + s1) / 2;
                if (*d->road && (row <= -2.5 || row >= rows + 2.5) && s >= s0 - 2 && s <= s1 + 2 && tt >= (nearEnd ? n0 : f0) + 1 &&
                    tt <= (nearEnd ? n1 : f1) - 1)
                    for (const auto& [cx, cy] : {std::pair{X - 1, Y}, {X + 1, Y}, {X, Y - 1}, {X, Y + 1}}) roadCorners.insert({cx, cy});
                continue;
            }
            const int row = int(std::lround((s - row0) / 2));
            if (row >= 0 && row <= rows && tt >= t0 - 1 && tt <= t1 + 1) waters.push_back({X, Y, water});
            else if ((row == -1 || row == rows + 1) && tt >= t0 - 1 && tt <= t1 + 1) grounds.push_back({X, Y, ground});
            else if ((row < -1 || row > rows + 1) && s >= s0 - 1 && s <= s1 + 1 && tt >= t0 - 1 && tt <= t1 + 1 && ed.terrainAt(64 * X, 32 * Y) == water)
                grounds.push_back({X, Y, ground});
        }
    // (the road's corners: not ground as well)
    std::erase_if(grounds, [&](const CornerValue& c) { return roadCorners.count({c.x, c.y}) != 0; });
    for (const auto& [cx, cy] : roadCorners) roads.push_back({cx, cy, road});
    if (!ground || !water || !road) return fail(BridgePlan::Problem::NoKit);
    for (auto* list : {&grounds, &roads, &waters})
        if (!list->empty()) plan.shore.push_back(std::move(*list));
    plan.tiles = std::move(tiles);
    plan.problem = BridgePlan::Problem::None;
    return plan;
}

bool applyBridge(Editor& ed, const BridgePlan& plan) {
    if (plan.problem != BridgePlan::Problem::None) return false;
    bool changed = false;
    for (const std::vector<CornerValue>& corners : plan.shore) changed |= ed.setTerrainCorners(corners);
    // (the pieces again, with the shore's tiles round them: the ones that meet them best)
    std::vector<Editor::TileAt> tiles = bridgeTiles(ed.era() & 7, plan.u, plan.length, plan.width, plan.ox, plan.oy, plan.seed, &ed, true);
    if (tiles.size() != plan.tiles.size()) tiles = plan.tiles;
    return ed.placeTileValues(tiles) > 0 || changed;
}

}  // namespace stardraft
