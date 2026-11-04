// Bridges of any size (include/stardraft/bridges.h). A kit builds one orientation of one tileset: each cell of the
// bridge (a pair of tiles, the first on an even column) has a class along each axis (the start cap, the far cap or the
// middle, with its phase), and the table gives the tile pairs that the approved example bridges have for that class.
// The river: the bridge's origin is the first water diamond of the river (along the d kit's deck) plus the kit's offset;
// the u kit is the d kit's mirror, so its offset is the mirror of d's (found from the two kits' outlines).
#include "stardraft/bridges.h"

#include "stardraft/passages.h"

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

// Where the approved bridges of a kit have each variant of each piece: its places along each axis (from the start and
// from the far end) and its depth there (the places to the nearer cap); the deepest place of the approved bridges along
// each axis (`deepL`, `deepW`: a place as deep or deeper in a larger bridge is as that one).
struct Seen {
    int a, f, c, g, dL, dW, length, width;  // (length, width: the approved bridge's)
};
struct SeenMap {
    std::map<std::tuple<const BridgeKeyData*, uint16_t, uint16_t>, std::vector<Seen>> at;
    int deepL = 0, deepW = 0, longest = 0, widest = 0;
};
const SeenMap& seenIn(const BridgeKitData& k) {
    static std::map<const BridgeKitData*, SeenMap> all;
    if (auto i = all.find(&k); i != all.end()) return i->second;
    SeenMap& m = all[&k];
    for (const BridgeExampleData& e : kBridgeExamples) {
        if (e.era != k.era || e.u != k.u) continue;
        const int extL = k.capStart + k.capFar + k.halfLength + 2 * e.length, extW = k.side0 + k.side1 + k.halfWidth + 2 * e.width;
        std::map<std::pair<int, int>, Cell> cells;
        for (const Cell& c : cellsOf(k, e.length, e.width)) cells[{c.x, c.y}] = c;
        for (int i = e.first; i < e.first + e.cells; ++i) {
            const BridgeExampleCellData& x = kBridgeExampleCells[i];
            const auto c = cells.find({x.x, x.y});
            if (c == cells.end()) continue;
            const int a = c->second.a, f = extL - 1 - a, w = c->second.c, g = extW - 1 - w;
            const int dL = std::min(a - k.capStart, f - k.capFar), dW = std::min(w - k.side0, g - k.side1);
            m.at[{c->second.key, x.a, x.b}].push_back({a, f, w, g, dL, dW, e.length, e.width});
            m.longest = std::max(m.longest, e.length), m.widest = std::max(m.widest, e.width);
            m.deepL = std::max(m.deepL, dL), m.deepW = std::max(m.deepW, dW);
        }
    }
    return m;
}

}  // namespace

bool bridgesAvailable(int era) { return kitOf(era, false) && kitOf(era, true); }

std::vector<uint16_t> bridgeKitTileValues(int era) {
    std::set<uint16_t> v;
    for (const bool u : {false, true})
        if (const BridgeKitData* k = kitOf(era, u))
            for (int i = 0; i < k->keys; ++i)
                for (int c = 0; c < kBridgeKeys[k->firstKey + i].count; ++c) {
                    const BridgeChoiceData& ch = kBridgeChoices[kBridgeKeys[k->firstKey + i].first + c];
                    v.insert(ch.a), v.insert(ch.b);
                }
    for (const BridgeExampleData& e : kBridgeExamples)
        if (e.era == era)
            for (int i = e.first; i < e.first + e.cells; ++i) v.insert(kBridgeExampleCells[i].a), v.insert(kBridgeExampleCells[i].b);
    return std::vector<uint16_t>(v.begin(), v.end());
}

namespace {

// A passage's bridge (stardraft/passages.h): its canvas's shape and grid (passages::bridgeShape), of one tileset.
struct CanvasBridge {
    passages::BridgeShape shape;
    int era = 0, grid = 0;
    std::vector<passages::BridgeTerrains> terrains;
};
// The passages' bridges whose canvas makes a shape (made again when the passages change).
const std::vector<CanvasBridge>& canvasBridges() {
    static std::vector<CanvasBridge> out;
    static int madeFor = -1;
    if (madeFor == passages::revision()) return out;
    madeFor = passages::revision(), out.clear();
    for (const passages::BridgeMapping& m : passages::inUse().bridges) {
        CanvasBridge c;
        c.shape = passages::bridgeShape(m);
        if (!c.shape.ok) continue;
        c.era = m.era & 7, c.grid = m.grid & 3, c.terrains = m.terrains;
        out.push_back(std::move(c));
    }
    return out;
}

// A kit planBridge uses: the game's kit of the map's tileset (index -1), or a passage's canvas (stardraft/passages.h),
// on its terrains (coast A under the start cap, coast B under the end cap).
struct KitUse {
    int index = -1;  // -1: the game's; else the passages' bridge kit (passageKits)
    int era = 0;     // the kit's tileset
    const BridgeKitData* d = nullptr;
    const BridgeKitData* u = nullptr;
    std::string coastA, water, coastB, road;
    const CanvasBridge* canvas = nullptr;  // (a passage's)
};
// The passages' bridge kits for a map's tileset: each canvas bridge of it, once for each of its terrains.
std::vector<KitUse> passageKits(int era) {
    std::vector<KitUse> out;
    for (const CanvasBridge& c : canvasBridges()) {
        if (c.era != era) continue;
        for (const passages::BridgeTerrains& t : c.terrains) {
            if (t.coastA.empty() || t.water.empty()) continue;
            KitUse k;
            k.index = int(out.size()), k.era = era, k.canvas = &c;
            k.coastA = t.coastA, k.water = t.water, k.coastB = t.coastB.empty() ? t.coastA : t.coastB;
            out.push_back(std::move(k));
        }
    }
    return out;
}
KitUse gameKit(int era) {
    KitUse k;
    k.era = era, k.d = kitOf(era, false), k.u = kitOf(era, true);
    if (k.d) k.coastA = k.coastB = k.d->ground, k.water = k.d->water, k.road = k.d->road;
    return k;
}
KitUse kitUse(int era, int index) {
    if (index < 0) return gameKit(era);
    std::vector<KitUse> p = passageKits(era);
    return index < int(p.size()) ? p[size_t(index)] : KitUse{};
}
// A game kit's tiles (a canvas bridge's are its plan's).
std::vector<Editor::TileAt> kitTiles(const KitUse& k, bool u, int length, int width, int ox, int oy, uint32_t seed, const Editor* on, bool around) {
    if (k.canvas) return {};
    return bridgeTiles(k.era, u, length, width, ox, oy, seed, on, around);
}

}  // namespace

bool bridgesAvailable(const Editor& ed) { return bridgesAvailable(ed.era() & 7) || !passageKits(ed.era() & 7).empty(); }

passages::BridgeMapping bridgeCanvasOfKit(int era, bool u) {
    passages::BridgeMapping m;
    const BridgeKitData* d = kitOf(era, false);
    const BridgeKitData* uk = kitOf(era, true);
    if (!d || !uk) return m;
    const BridgeKitData& k = u ? *uk : *d;
    m.name = "Bridge", m.era = era, m.gridAuto = false;
    m.terrains.push_back({k.ground, k.water, k.ground});
    const int length = 1, width = 1;
    std::map<std::pair<int, int>, uint16_t> at;
    for (const Editor::TileAt& c : bridgeTiles(era, u, length, width, 0, 0, 0)) at[{c.x, c.y}] = c.value;
    // (the river's first water diamond, from the origin; the deck's way along the river's rows)
    const std::pair<int, int> dOff{d->offset[0], d->offset[1]};
    const std::pair<int, int> off = u ? uOffset(*d, *uk, length, width, dOff) : dOff;
    const double rx = -off.first / 2.0, ry = -off.second;
    const int deckX = u ? -d->lengthStep[0] : d->lengthStep[0], deckY = d->lengthStep[1];
    const int rows = d->rowsAtSize1 + length - 1;
    const auto along = [&](double X, double Y) { return (X * deckX + Y * deckY) / 2; };
    const double a0 = along(rx, ry) - 0.5, a1 = along(rx + deckX * (rows - 1), ry + deckY * (rows - 1)) + 0.5;
    int x0 = INT_MAX, y0 = INT_MAX;
    for (const auto& [p, v] : at) x0 = std::min(x0, p.first), y0 = std::min(y0, p.second);
    for (const Cell& c : cellsOf(k, length, width)) {
        const int extL = k.capStart + k.capFar + k.halfLength + 2 * length;
        const char cls = axisClass(c.a, extL, k.capStart, k.capFar, k.periodLength)[0];
        // (the middle: a length's 2 places a deck; its half past them (halfLength) goes with the end cap)
        const uint8_t role = cls == 'S' ? passages::kStart : cls == 'F' || c.a - k.capStart >= 2 * length ? passages::kEnd : passages::kDeck;
        for (int h = 0; h < 2; ++h) {
            const auto i = at.find({c.x + h, c.y});
            if (i == at.end()) continue;
            passages::Cell pc;
            pc.x = c.x + h - x0, pc.y = c.y - y0, pc.tile = i->second, pc.role = role;
            // (the deck over the water; the caps and the cells of the river's own rows (a deck of length 1) as the river's)
            const double a = along((c.x + h + 0.5) / 2, c.y + 0.5);
            pc.mark = role == passages::kDeck || (std::min(a0, a1) < a && a < std::max(a0, a1)) ? passages::kHigh : passages::kLow;
            m.cells.push_back(pc);
        }
    }
    for (int g = 0; g < 4; ++g) {  // (the grid of the map's corners: the origin is the river's first water diamond and the offset)
        bool same = true;
        for (int y = 0; y < 4 && same; ++y)
            for (int x = 0; x < 8 && same; ++x) same = passages::cornerOnGrid(g, x, y) == passages::cornerOnGrid(0, x + x0 + off.first, y + y0 + off.second);
        if (same) {
            m.grid = g;
            break;
        }
    }
    return m;
}

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
    // (a variant goes only where the approved bridges have it: as far from the start or from the far end; a place deeper
    // than the approved bridges' deepest is as the deepest. A piece of the far cap's row in the middle rows does not
    // join them, though the table has both there)
    const SeenMap& seen = seenIn(*k);
    const int extL = k->capStart + k->capFar + k->halfLength + 2 * length, extW = k->side0 + k->side1 + k->halfWidth + 2 * width;
    const auto placeFits = [&](const Cell& c, const BridgeChoiceData& v) {
        const auto s = seen.at.find({c.key, v.a, v.b});
        if (s == seen.at.end()) return true;
        const int f = extL - 1 - c.a, g = extW - 1 - c.c;
        const int dL = std::min(c.a - k->capStart, f - k->capFar), dW = std::min(c.c - k->side0, g - k->side1);
        // (along one axis: in an approved bridge of this extent, at the same place; in a larger bridge, as far from the
        // nearer end as there, or both as deep as the deepest)
        const auto axis = [](int oa, int of, int od, int on, int ca, int cf, int cd, int n, int most, int deep) {
            if (n <= most) return on == n && oa == ca;
            // (the cells of a row of one depth are on one parity: the deepest two rows of the approved bridges are as
            // deep, each for its parity)
            if (cd >= deep - 1 && od >= deep - 1 && !((cd - od) & 1)) return true;
            return ca <= cf ? oa == ca && oa <= of : of == cf && of <= oa;
        };
        for (const Seen& o : s->second)
            if (axis(o.a, o.f, o.dL, o.length, c.a, f, dL, length, seen.longest, seen.deepL) &&
                axis(o.c, o.g, o.dW, o.width, c.c, g, dW, width, seen.widest, seen.deepW))
                return true;
        return false;
    };
    // (the deck inside the caps and the sides is walkable: there, where some variants have only walkable minitiles, the
    // others do not go)
    const Tileset* ts = on ? &on->tileset() : nullptr;
    const auto walkable = [&](uint16_t v) {
        const int m = ts->megatileFor(v);
        if (m < 0 || size_t(m) >= ts->minitileFlags.size()) return false;
        for (int i = 0; i < 16; ++i)
            if (!(ts->minitileFlags[size_t(m)][size_t(i)] & Tileset::kWalkable)) return false;
        return true;
    };
    const auto deck = [&](const Cell& c) {
        return c.a >= k->capStart && extL - 1 - c.a >= k->capFar && c.c >= k->side0 && extW - 1 - c.c >= k->side1;
    };
    std::vector<int> picks(cells.size(), 0);
    const auto choose = [&](size_t i) {
        const Cell& c = cells[i];
        const BridgeChoiceData* ch = &kBridgeChoices[c.key->first];
        placed.erase({c.x, c.y}), placed.erase({c.x + 1, c.y});  // (this piece's own tiles do not count)
        double best = 1e18;
        int total = 0;
        std::vector<double> score(size_t(c.key->count));
        std::vector<char> ok(size_t(c.key->count));
        bool any = false, walk = false;
        for (int j = 0; j < c.key->count; ++j) any |= ok[size_t(j)] = placeFits(c, ch[j]);
        for (int j = 0; j < c.key->count; ++j) ok[size_t(j)] = !any || ok[size_t(j)];  // (none fits: all may go)
        if (ts && deck(c))
            for (int j = 0; j < c.key->count; ++j) walk |= ok[size_t(j)] && walkable(ch[j].a) && walkable(ch[j].b);
        for (int j = 0; j < c.key->count; ++j)
            if (walk && !(walkable(ch[j].a) && walkable(ch[j].b))) ok[size_t(j)] = false;
        for (int j = 0; j < c.key->count; ++j) best = std::min(best, score[size_t(j)] = ok[size_t(j)] ? misfits(c.x, c.y, ch[j]) : 1e17);
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
    uint16_t waterId = 0;  // (a passage's water: that terrain, by its whole name)
    // (a game terrain's name without the ground it stands on, which a mod's tab adds: "Water (Dirt)")
    std::string at(int cx, int cy) const {
        const std::string n = ed.terrainName(ed.terrainAt(64 * cx, 32 * cy));
        return n.substr(0, n.find(" ("));
    }
    bool wet(int cx, int cy) const { return waterId ? ed.terrainAt(64 * cx, 32 * cy) == waterId : at(cx, cy) == water; }
    // Water: all four corners are the water.
    bool water4(Diamond d) const { return wet(d.x - 1, d.y) && wet(d.x + 1, d.y) && wet(d.x, d.y - 1) && wet(d.x, d.y + 1); }
    // A corner is the water.
    bool water1(Diamond d) const { return wet(d.x - 1, d.y) || wet(d.x + 1, d.y) || wet(d.x, d.y - 1) || wet(d.x, d.y + 1); }
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

namespace {

// A map terrain's id by its name (a mod's tab has the ground in the game terrains' names, "Water (Dirt)").
uint16_t terrainIdOf(const Editor& ed, const std::string& name) {
    if (name.empty()) return 0;
    for (uint16_t id = 1; id < 256; ++id)
        if (ed.terrainName(id) == name) return id;
    for (uint16_t id = 1; id < 256; ++id)
        if (ed.terrainName(id).rfind(name + " (", 0) == 0) return id;
    return 0;
}

// A canvas bridge (a passage's): the drag goes along its deck's diagonal over a river (as planWith finds it); the deck
// repeats so that the canvas's water corners are as long as the river's (its water diamonds on the drag's line, and
// the corner row past them); the bridge lies on the river's first water corner row and its middle on the drag's line.
// Its shore: each corner of the canvas on the map (water; coast A by the start cap, coast B by the end cap).
BridgePlan planCanvas(const Editor& ed, const KitUse& use, int x0, int y0, int x1, int y1, uint32_t seed) {
    BridgePlan plan;
    const CanvasBridge& cb = *use.canvas;
    const passages::BridgeShape& sh = cb.shape;
    plan.kit = use.index, plan.ground = use.coastA, plan.water = use.water, plan.seed = seed;
    if (!ed.canPaintTerrain()) return plan.problem = BridgePlan::Problem::NoIsom, plan;
    const int dx = x1 - x0, dy = y1 - y0;
    const bool downRight = (dx >= 0) == (dy >= 0);
    if (downRight != (sh.deck == 0)) return plan.problem = BridgePlan::Problem::NoKit, plan;  // (another diagonal: this canvas's deck does not go that way)
    plan.u = sh.deck == 1;
    const Diamond step{dx >= 0 ? 1 : -1, downRight == (dx >= 0) ? 1 : -1};
    const Diamond D{sh.stepX > 0 ? 1 : -1, sh.stepY > 0 ? 1 : -1};  // (the deck's way: from the start cap to the end cap)
    const Diamond A{D.x, -D.y};                                       // (across it)
    const int kd = std::max(1, std::abs(sh.stepX) / 2);              // (diamonds from one deck to the next)
    Terrains t{ed, use.water};
    if (!(t.waterId = terrainIdOf(ed, use.water))) return plan.problem = BridgePlan::Problem::NoKit, plan;
    const Diamond s = nearestDiamond(x0, y0);
    const int steps = int((double(dx) * 64 * step.x + double(dy) * 32 * step.y) / (64.0 * 64 + 32.0 * 32));
    if (t.water4(s)) return plan.problem = BridgePlan::Problem::NeedsTerrain, plan;
    const int far = std::max(ed.width(), ed.height());
    int k1 = 1;
    while (k1 <= steps && !t.water1(s + step * k1)) ++k1;
    if (k1 > steps) return plan.problem = BridgePlan::Problem::Short, plan;
    int k4 = k1;
    while (k4 <= k1 + far && t.water1(s + step * k4) && !t.water4(s + step * k4)) ++k4;
    if (t.water4(s + step * k4)) k1 = k4;
    int rows = 0;
    while (rows < far && t.water4(s + step * (k1 + rows))) ++rows;
    if (!rows) return plan.problem = BridgePlan::Problem::NeedsTerrain, plan;
    if (steps < k1 + rows) return plan.problem = BridgePlan::Problem::ReachGround, plan;
    // (rect-grid points along the deck and across it, in diamonds)
    const auto along = [&](double X, double Y) { return (X * D.x + Y * D.y) / 2; };
    const auto side = [&](double X, double Y) { return (X * A.x + Y * A.y) / 2; };
    const Diamond F = s + step * k1, L = s + step * (k1 + rows - 1);
    const double ra0 = std::min(along(F.x, F.y), along(L.x, L.y)) - 0.5;  // (the river's first water corner row along the deck)
    // The canvas with n decks, its cell (0, 0) on the map's tile (gx + 2 X, gy + Y) for a diamond (X, Y): its pieces'
    // tiles and its corners (map tile points).
    const int gx = cb.grid & 1, gy = cb.grid >> 1;
    using Placed = std::pair<passages::CanvasCorner, std::pair<int, int>>;
    using Marked = std::tuple<int, int, uint8_t>;  // (a piece's cell on the map and its mark)
    const auto build = [&](int n, int X, int Y, std::vector<Editor::TileAt>* tiles, std::vector<Placed>* corners, std::vector<Marked>* marks = nullptr) {
        for (int piece = 0; piece < 3; ++piece)
            for (int i = 0; i < (piece == 1 ? n : 1); ++i) {
                const int sx = piece == 1 ? i * sh.stepX : piece == 2 ? (n - 1) * sh.stepX : 0;
                const int sy = piece == 1 ? i * sh.stepY : piece == 2 ? (n - 1) * sh.stepY : 0;
                if (tiles)
                    for (const passages::Cell& c : sh.pieces[size_t(piece)]) tiles->push_back({gx + 2 * X + c.x + sx, gy + Y + c.y + sy, uint16_t(c.tile)});
                if (marks)
                    for (const passages::Cell& c : sh.pieces[size_t(piece)])
                        if (c.mark != passages::kUnmarked) marks->push_back({gx + 2 * X + c.x + sx, gy + Y + c.y + sy, c.mark});
                if (corners)
                    for (const passages::CanvasCorner& c : sh.corners)
                        if (c.piece == piece) corners->push_back({c, {gx + 2 * X + c.x + sx, gy + Y + c.y + sy}});
            }
    };
    // (one deck's cells along the deck, at diamond 0: a first guess of the decks that cover the river's water rows)
    double d0 = 1e18, d1 = -1e18;
    for (const passages::Cell& c : sh.pieces[1])
        for (const auto& [ex, ey] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
            const double v = along((gx + c.x + ex) / 2.0, gy + c.y + ey);
            d0 = std::min(d0, v), d1 = std::max(d1, v);
        }
    // (the water's cells of one deck along it, their middles: as long as the river's water rows that one deck covers)
    double w0 = 1e18, w1 = -1e18;
    {
        std::vector<Marked> ms;
        build(1, 0, 0, nullptr, nullptr, &ms);
        for (const auto& [x, y, mark] : ms)
            if (mark == passages::kHigh) w0 = std::min(w0, along((x + 0.5) / 2, y + 0.5)), w1 = std::max(w1, along((x + 0.5) / 2, y + 0.5));
    }
    // (no water marks: the deck's cells are the water's)
    if (w0 > w1) w0 = d0 + 0.25, w1 = d1 - 0.25;
    // The decks: the water's cells of n decks span the river's water rows (a cell's middle is a quarter row in from its
    // edge: one deck of the game's kits has rowsAtSize1 - 0.25 between its water cells' middles).
    const int n = std::clamp(int(std::lround((rows - (w1 - w0) - 0.25) / kd)) + 1, 1, 64);
    plan.length = n, plan.width = 0;
    // (across: the bridge's middle on the drag's line; along: its water's middle on the river's)
    std::vector<Editor::TileAt> tiles;
    build(n, 0, 0, &tiles, nullptr);
    if (tiles.empty()) return plan.problem = BridgePlan::Problem::NoKit, plan;
    double c0 = 0;
    for (const Editor::TileAt& c : tiles) c0 += side((c.x + 0.5) / 2, c.y + 0.5);
    c0 /= double(tiles.size());
    const double waterMiddle = (w0 + w1) / 2 + (n - 1) * kd / 2.0, riverMiddle = ra0 + rows / 2.0;
    const int a = int(std::lround(riverMiddle - waterMiddle)), b = int(std::lround(side(s.x, s.y) - c0));
    const int X = a * D.x + b * A.x, Y = a * D.y + b * A.y;
    std::vector<Placed> cs;
    tiles.clear();
    build(n, X, Y, &tiles, &cs);
    for (const Editor::TileAt& c : tiles)
        if (c.x < 0 || c.y < 0 || c.x >= ed.width() || c.y >= ed.height()) return plan.problem = BridgePlan::Problem::OffMap, plan;
    const uint16_t water = t.waterId, coastA = terrainIdOf(ed, use.coastA), coastB = terrainIdOf(ed, use.coastB);
    if (!coastA || !coastB) return plan.problem = BridgePlan::Problem::NoKit, plan;
    // (the shore: the canvas's corners; a deck's coast corner is coast A on the start cap's half)
    const double mid = ra0 + rows / 2.0;
    std::vector<CornerValue> grounds, groundsB, waters;
    std::set<std::pair<int, int>> seen;
    for (const auto& [c, at] : cs) {
        if (at.first % 2 || !seen.insert(at).second) continue;
        const int cx = at.first / 2, cy = at.second;
        if (cx < 0 || cy < 0 || cx > ed.width() / 2 || cy > ed.height()) continue;
        if (c.high) waters.push_back({cx, cy, water});
        else if (c.piece == 0 || (c.piece == 1 && along(cx, cy) < mid)) grounds.push_back({cx, cy, coastA});
        else groundsB.push_back({cx, cy, coastB});
    }
    for (auto* list : {&grounds, &groundsB, &waters})
        if (!list->empty()) plan.shore.push_back(std::move(*list));
    plan.ox = gx + 2 * X, plan.oy = gy + Y;
    plan.tiles = std::move(tiles);
    plan.problem = BridgePlan::Problem::None;
    return plan;
}

BridgePlan planWith(const Editor& ed, const KitUse& use, int x0, int y0, int x1, int y1, int width, uint32_t seed) {
    if (use.canvas) return planCanvas(ed, use, x0, y0, x1, y1, seed);
    BridgePlan plan;
    const int era = use.era;
    const BridgeKitData* d = use.d;
    const BridgeKitData* uk = use.u;
    if (!d || !uk) return plan.problem = BridgePlan::Problem::NoKit, plan;
    plan.kit = use.index;
    plan.ground = use.coastA, plan.water = use.water;
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

    // (the map's terrain ids by name: a mod's tab has the ground in the game terrains' names, "Water (Dirt)")
    const auto terrainId = [&](const std::string& name) {
        for (uint16_t id = 1; id < 256; ++id)
            if (ed.terrainName(id) == name) return id;
        for (uint16_t id = 1; id < 256; ++id)
            if (ed.terrainName(id).rfind(name + " (", 0) == 0) return id;
        return uint16_t(0);
    };
    Terrains t{ed, use.water};
    const Diamond s = nearestDiamond(x0, y0);
    const int steps = int((double(dx) * 64 * step.x + double(dy) * 32 * step.y) / (64.0 * 64 + 32.0 * 32));  // diamonds along the drag
    // No bridge: why, and the tiles of a bridge as long as the drag with its middle on the drag's middle (the red preview).
    const auto fail = [&](BridgePlan::Problem why) {
        plan.problem = why;
        const int length = std::clamp(steps - d->rowsAtSize1 - 1, 0, 64);
        const std::pair<int, int> dOff{d->offset[0], d->offset[1]};
        const std::pair<int, int> o = plan.u ? uOffset(*d, *uk, length, plan.width, dOff) : dOff;
        const std::vector<Editor::TileAt> draft = kitTiles(use, plan.u, length, plan.width, o.first, o.second, seed, nullptr, false);
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
        return kitTiles(use, plan.u, plan.length, plan.width, plan.ox, plan.oy, seed, &ed, false);
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
    // (coast A on the start cap's side of the river, coast B on the far cap's: the game's kits have one ground)
    const uint16_t groundA = terrainId(use.coastA), groundB = terrainId(use.coastB), water = terrainId(use.water),
                   road = !use.road.empty() ? terrainId(use.road) : groundA;
    std::vector<CornerValue> grounds, groundsB, roads, waters;
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
                if (!use.road.empty() && (row <= -2.5 || row >= rows + 2.5) && s >= s0 - 2 && s <= s1 + 2 && tt >= (nearEnd ? n0 : f0) + 1 &&
                    tt <= (nearEnd ? n1 : f1) - 1)
                    for (const auto& [cx, cy] : {std::pair{X - 1, Y}, {X + 1, Y}, {X, Y - 1}, {X, Y + 1}}) roadCorners.insert({cx, cy});
                continue;
            }
            const int row = int(std::lround((s - row0) / 2));
            std::vector<CornerValue>& bank = row < 0 ? grounds : groundsB;
            const uint16_t ground = row < 0 ? groundA : groundB;
            if (row >= 0 && row <= rows && tt >= t0 - 1 && tt <= t1 + 1) waters.push_back({X, Y, water});
            else if ((row == -1 || row == rows + 1) && tt >= t0 - 1 && tt <= t1 + 1) bank.push_back({X, Y, ground});
            else if ((row < -1 || row > rows + 1) && s >= s0 - 1 && s <= s1 + 1 && tt >= t0 - 1 && tt <= t1 + 1 && ed.terrainAt(64 * X, 32 * Y) == water)
                bank.push_back({X, Y, ground});
        }
    // (a third terrain near the bridge's tiles, as Snow past an Ice river's Dirt shore: the kit's ground there, two
    // diamonds round them, as the game's maps have round their bridges (Spring Thaw), so that the bridge's edge meets
    // that ground; the rest stays)
    {
        std::set<std::pair<int, int>> asked;
        for (const std::vector<CornerValue>* v : {&grounds, &groundsB, &waters})
            for (const CornerValue& c : *v) asked.insert({c.x, c.y});
        for (const Editor::TileAt& c : tiles)
            for (int Y = c.y - 2; Y <= c.y + 3; ++Y)
                for (int X = std::max(0, 32 * c.x - 128) / 64; X <= (32 * c.x + 160) / 64; ++X) {
                    if (!((X + Y) & 1) || X < 0 || Y < 0 || X > ed.width() / 2 || Y > ed.height() || !asked.insert({X, Y}).second) continue;
                    const uint16_t t = ed.terrainAt(64 * X, 32 * Y);
                    const bool a = along(X, Y) - row0 < rows;  // (the start cap's side)
                    if (t && t != groundA && t != groundB && t != water && t != road) (a ? grounds : groundsB).push_back({X, Y, a ? groundA : groundB});
                }
    }
    // (the road's corners: not ground as well)
    for (auto* list : {&grounds, &groundsB}) std::erase_if(*list, [&](const CornerValue& c) { return roadCorners.count({c.x, c.y}) != 0; });
    for (const auto& [cx, cy] : roadCorners) roads.push_back({cx, cy, road});
    if (!groundA || !groundB || !water || !road) return fail(BridgePlan::Problem::NoKit);
    for (auto* list : {&grounds, &groundsB, &roads, &waters})
        if (!list->empty()) plan.shore.push_back(std::move(*list));
    plan.tiles = std::move(tiles);
    plan.problem = BridgePlan::Problem::None;
    return plan;
}

}  // namespace

BridgePlan planBridge(const Editor& ed, int x0, int y0, int x1, int y1, int width, uint32_t seed) {
    // The passages' kits (a mod's bridges: a mapping is the user's choice for its water), then the game's kit: the first
    // that makes a bridge; else the one that found its water on the drag (its reason), else the first's.
    std::vector<KitUse> kits = passageKits(ed.era() & 7);
    kits.push_back(gameKit(ed.era() & 7));
    BridgePlan first, found;
    bool haveFound = false;
    for (size_t i = 0; i < kits.size(); ++i) {
        BridgePlan p = planWith(ed, kits[i], x0, y0, x1, y1, width, seed);
        if (p.problem == BridgePlan::Problem::None) return p;
        if (!haveFound && p.problem != BridgePlan::Problem::NoKit && p.problem != BridgePlan::Problem::Short && p.problem != BridgePlan::Problem::NeedsTerrain)
            found = p, haveFound = true;
        if (!i || first.problem == BridgePlan::Problem::NoKit) first = std::move(p);
    }
    return haveFound ? found : first;
}

bool applyBridge(Editor& ed, const BridgePlan& plan) {
    if (plan.problem != BridgePlan::Problem::None) return false;
    bool changed = false;
    for (const std::vector<CornerValue>& corners : plan.shore) changed |= ed.setTerrainCorners(corners);
    // (the pieces again, with the shore's tiles round them: the ones that meet them best)
    std::vector<Editor::TileAt> tiles = kitTiles(kitUse(ed.era() & 7, plan.kit), plan.u, plan.length, plan.width, plan.ox, plan.oy, plan.seed, &ed, true);
    if (tiles.size() != plan.tiles.size()) tiles = plan.tiles;
    return ed.placeTileValues(tiles) > 0 || changed;
}

}  // namespace stardraft
