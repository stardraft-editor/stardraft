#include "stardraft/isom.h"
#include "stardraft/isom_core.h"

#include <algorithm>
#include <functional>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <tuple>

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

// By the tileset's CV5 (the terrain core's tables): could the ISOM give group g as this half of its rect (shapes left
// and right, the parity), under the group above (-1: none)? A group StarEdit picks for the rect, or a cliff piece that
// sits under the stack above (StarEdit keeps those). No group fits the shapes: nothing to compare with (true).
bool groupFromCv5(const IsomTiles& t, int g, int above, int parity, int half, uint16_t left, uint16_t right) {
    if (g < 0 || g >= 1024) return false;
    const auto& gs = t.groups(parity, left, right);
    if (gs.empty()) return true;
    if (std::find(gs.begin(), gs.end(), uint16_t(g - half)) != gs.end()) return true;
    return above >= 0 && above < 1024 && t.stackTop(uint16_t(g)) != 0 && t.stackTop(uint16_t(g)) == t.stackBottom(uint16_t(above));
}

}  // namespace

void IsomGrid::setTileset(const Tileset* tileset) {
    tileset_ = tileset;
    tiles_ = tileset ? IsomTiles::forTileset(*tileset) : nullptr;
}

// --- IsomTerrainTable ----------------------------------------------------------------------

IsomTerrainTable::IsomTerrainTable(uint16_t firstTransition, std::vector<Pair> pairs, std::vector<const char*> names)
    : firstTransition_(firstTransition), pairs_(std::move(pairs)), names_(std::move(names)) {
    indexShapes();
}

void IsomTerrainTable::indexShapes() {
    size_t end = 0;
    for (const auto& s : solids_) end = std::max<size_t>(end, size_t(s.shape) + 1);
    for (const auto& p : pairs_) end = std::max<size_t>(end, size_t(p.firstShape) + 14);
    shapeSolid_.assign(end, -1), shapePair_.assign(end, -1);
    for (size_t i = 0; i < solids_.size(); ++i)  // (the first one of a shape, as the scans found it)
        if (shapeSolid_[solids_[i].shape] < 0) shapeSolid_[solids_[i].shape] = int16_t(i);
    for (size_t i = 0; i < pairs_.size(); ++i)
        for (uint16_t k = 0; k < 14; ++k)
            if (shapePair_[size_t(pairs_[i].firstShape) + k] < 0) shapePair_[size_t(pairs_[i].firstShape) + k] = int16_t(i);
}

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

bool IsomTerrainTable::twinOnly(uint16_t a, uint16_t b) const {
    bool twin = false;
    for (const auto& p : pairs_)
        if ((p.a == a && p.b == b) || (p.a == b && p.b == a)) {
            if (!p.twin) return false;
            twin = twin || p.a == a;
        }
    return twin;
}

std::unique_ptr<IsomTerrainTable> IsomTerrainTable::withExtras(const IsomTerrainTable& game, const std::vector<Extra>& extras, uint16_t firstValue) {
    auto t = std::make_unique<IsomTerrainTable>(game.firstTransition_, game.pairs_, game.names_);
    t->game_ = &game;
    // The game terrains named as the mod terrains are, "X (Y)": each with the terrain it stands on, its parent in the
    // tileset's tree of borders from its main terrain (the one with the most borders: Dirt, Platform, Substructure); the
    // main terrain keeps its name.
    {
        const size_t n = t->names_.size();
        std::vector<std::vector<uint16_t>> next(n + 1);
        for (const auto& p : game.pairs_)
            if (p.a >= 1 && p.b >= 1 && p.a <= n && p.b <= n) next[p.a].push_back(p.b), next[p.b].push_back(p.a);
        uint16_t root = 1;
        for (uint16_t i = 1; i <= n; ++i)
            if (next[i].size() > next[root].size()) root = i;
        std::vector<uint16_t> parent(n + 1, 0), queue{root};
        parent[root] = root;
        for (size_t q = 0; q < queue.size(); ++q)
            for (uint16_t b : next[queue[q]])
                if (!parent[b]) parent[b] = queue[q], queue.push_back(b);
        t->gameNames_.resize(n);
        for (uint16_t i = 1; i <= n; ++i) {
            const char* own = game.names_[i - 1u];
            t->gameNames_[i - 1u] = own[0] && parent[i] && parent[i] != i ? std::string(own) + " (" + game.names_[parent[i] - 1u] + ")" : own;
        }
        for (uint16_t i = 1; i <= n; ++i) t->names_[i - 1u] = t->gameNames_[i - 1u].c_str();
    }
    t->ownNames_.reserve(extras.size());
    for (const Extra& e : extras) t->ownNames_.push_back(e.name);
    for (size_t i = 0; i < extras.size(); ++i) {
        const Extra& e = extras[i];
        // (its 15 shapes by its id: a terrain keeps them when others come before it; ids from 64, kExtraSlots of them)
        if (e.id < 64 || e.id >= 64 + kExtraSlots) continue;
        const uint16_t v = uint16_t(firstValue + 15 * (e.id - 64));
        // (its ground: a game terrain, or a mod terrain before it with a game terrain as its template ground)
        if (!e.id || game.isTerrain(e.id) || !game.isTerrain(e.templ) || !t->isTerrain(e.ground) || (e.groundTempl && !game.isTerrain(e.groundTempl)) ||
            (!game.isTerrain(e.ground) && !e.groundTempl))
            continue;
        if (t->names_.size() < e.id) t->names_.resize(e.id, "");
        t->names_[e.id - 1u] = t->ownNames_[i].c_str();
        t->solids_.push_back({e.id, v, e.templ, e.tag});
        t->pairs_.push_back({uint16_t(v + 1), e.id, e.ground, e.templ, e.groundTempl ? e.groundTempl : e.ground, e.tag});
        t->levels_ = t->levels_ || (e.groundTempl && e.groundTempl != e.ground);
    }
    // Its borders with its ground's twins: the same pieces, 14 more shapes each, from a place that adding terrains does
    // not move (saved maps keep these values): past kExtraSlots terrains' shapes, in the order of the newer of the two
    // terrains, then the ids (a new terrain, the last of its tileset, comes after all the twins before it).
    struct Twin {
        const Extra* e;
        uint16_t g;
    };
    std::vector<Twin> twins;
    for (const Extra& e : extras)
        if (t->isTerrain(e.id))
            for (uint16_t g : e.twins)
                if (t->isTerrain(g) && g != e.ground && !t->linked(e.id, g)) twins.push_back({&e, g});
    std::sort(twins.begin(), twins.end(), [](const Twin& x, const Twin& y) {
        return std::make_tuple(std::max(x.e->id, x.g), x.e->id, x.g) < std::make_tuple(std::max(y.e->id, y.g), y.e->id, y.g);
    });
    uint16_t w = uint16_t(firstValue + 15 * kExtraSlots);
    for (const Twin& tw : twins) {
        if (w + 15 > 4096) break;  // (an ISOM value holds a shape in 12 bits)
        Pair p{uint16_t(w + 1), tw.e->id, tw.g, tw.e->templ, tw.e->groundTempl ? tw.e->groundTempl : tw.e->ground, tw.e->tag};
        p.twin = true;
        t->pairs_.push_back(p);
        w = uint16_t(w + 15);
    }
    t->indexShapes();
    return t;
}

uint16_t IsomTerrainTable::shapeEnd() const {
    uint16_t end = firstTransition_;
    for (const auto& p : pairs_) end = std::max<uint16_t>(end, uint16_t(p.firstShape + 14));
    for (const auto& s : solids_) end = std::max<uint16_t>(end, uint16_t(s.shape + 1));
    return end;
}

bool IsomTerrainTable::levelGround(uint16_t left, uint16_t right, uint16_t& from, uint16_t& to, uint16_t& tag) const {
    const Pair *pl = pairOf(left), *pr = pairOf(right);  // (the pair first in the table of the two, as a scan of it)
    const Pair* p = pl && pr ? (pl < pr ? pl : pr) : pl ? pl : pr;
    for (const Pair* q : {p, p == pl ? pr : pl})
        if (q && q->ta && q->b != q->tb) {
            from = q->b, to = q->tb, tag = q->tag;
            return true;
        }
    return false;
}

uint16_t IsomTerrainTable::templateShape(uint16_t shape) const {
    if (!game_) return shape;
    if (const Solid* s = solidOf(shape)) return s->templ;
    if (const Pair* p = pairOf(shape); p && p->ta) {
        const auto& pat = kPairPatterns[shape - p->firstShape];
        std::array<uint16_t, 4> c;
        for (int i = 0; i < 4; ++i) c[i] = pat[i] ? p->tb : p->ta;
        return game_->shapeFor(c);
    }
    return shape;
}

bool IsomTerrainTable::templateRect(uint16_t left, uint16_t right, uint16_t& gameLeft, uint16_t& gameRight, uint16_t& tag) const {
    gameLeft = left, gameRight = right, tag = 0;
    if (!game_) return true;
    std::array<std::pair<uint16_t, uint16_t>, 8> map{};  // a terrain of the rect -> the game's terrain in its template
    int n = 0;
    auto mapped = [&](uint16_t from) {
        for (int i = 0; i < n; ++i)
            if (map[size_t(i)].first == from) return i;
        return -1;
    };
    auto add = [&](uint16_t from, uint16_t to) {
        const int i = mapped(from);
        if (i >= 0) return map[size_t(i)].second == to;
        map[size_t(n++)] = {from, to};
        return true;
    };
    // Each diamond: a mod terrain's border (its template names both its terrains), a mod terrain's solid shape (its own
    // template), or the game's (its terrains as themselves). The borders decide first: the ground beside a mod terrain a
    // level higher takes the border's template terrain (High Dirt under Higher Dirt's cliff is that cliff's Dirt).
    const Pair* pairs[2] = {nullptr, nullptr};
    const Solid* solid[2] = {nullptr, nullptr};
    const uint16_t shapes[2] = {left, right};
    for (int d = 0; d < 2; ++d) {
        solid[d] = solidOf(shapes[d]);
        if (const Pair* p = pairOf(shapes[d]); !solid[d] && p && p->ta) pairs[d] = p;
    }
    if (!pairs[0] && !pairs[1] && !solid[0] && !solid[1]) return true;
    for (int d = 0; d < 2; ++d)
        if (const Pair* p = pairs[d]) {
            if (!add(p->a, p->ta) || !add(p->b, p->tb) || (tag && tag != p->tag)) return false;
            tag = p->tag;
        }
    for (int d = 0; d < 2; ++d)
        if (const Solid* sd = solid[d]) {
            if (mapped(sd->id) < 0) add(sd->id, sd->templ);
            if (!tag) tag = sd->tag;
            else if (!pairs[0] && !pairs[1] && tag != sd->tag) return false;
        }
    for (int d = 0; d < 2; ++d)
        if (!pairs[d] && !solid[d])
            for (uint16_t t : corners(shapes[d]))
                if (t && mapped(t) < 0) add(t, t);
    for (int i = 0; i < n; ++i)  // (two terrains of the rect as one template terrain: the pieces would show one of them)
        for (int j = i + 1; j < n; ++j)
            if (map[size_t(i)].second == map[size_t(j)].second) return false;
    auto game = [&](uint16_t shape) {
        auto c = corners(shape);
        for (uint16_t& t : c)
            if (const int i = mapped(t); i >= 0) t = map[size_t(i)].second;
        return game_->shapeFor(c);
    };
    gameLeft = game(left), gameRight = game(right);
    return gameLeft && gameRight;
}

std::array<uint16_t, 4> IsomTerrainTable::corners(uint16_t shape) const {
    if (const Solid* s = solidOf(shape)) return {s->id, s->id, s->id, s->id};
    if (game_ ? game_->isTerrain(shape) : isTerrain(shape)) return {shape, shape, shape, shape};
    if (const Pair* p = pairOf(shape)) {
        const auto& pat = kPairPatterns[shape - p->firstShape];
        std::array<uint16_t, 4> c;
        for (int i = 0; i < 4; ++i) c[i] = pat[i] ? p->b : p->a;
        return c;
    }
    return {0, 0, 0, 0};
}

uint16_t IsomTerrainTable::shapeFor(const std::array<uint16_t, 4>& c) const {
    if (c[0] == c[1] && c[1] == c[2] && c[2] == c[3]) {
        for (const auto& s : solids_)
            if (s.id == c[0]) return s.shape;
        return (game_ ? game_->isTerrain(c[0]) : isTerrain(c[0])) ? c[0] : 0;
    }
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
    const int n = idCount();
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

int IsomTerrainTable::diameter() const {
    int d = 0;
    for (uint16_t a = 1; a <= terrainCount(); ++a)
        for (uint16_t b = uint16_t(a + 1); b <= terrainCount(); ++b)
            if (isTerrain(a) && isTerrain(b))
                if (const int k = distance(a, b); k < 1000) d = std::max(d, k);
    return d;
}

std::vector<uint16_t> IsomTerrainTable::groundsOf(uint16_t terrain) const {
    std::vector<uint16_t> out;
    bool twins = false;
    for (const auto& p : pairs_)
        if (p.ta && p.a == terrain && std::find(out.begin(), out.end(), p.b) == out.end()) out.push_back(p.b), twins = twins || p.twin;
    if (!twins) out.clear();
    return out;
}

std::vector<uint16_t> IsomTerrainTable::path(uint16_t from, uint16_t to, const std::vector<uint16_t>* avoid) const {
    if (!isTerrain(from) || !isTerrain(to)) return {};
    std::vector<int> prev(size_t(idCount()), -1);
    std::deque<uint16_t> queue{from};
    prev[from] = from;
    while (!queue.empty()) {
        uint16_t t = queue.front();
        queue.pop_front();
        if (t == to) break;
        for (const auto& p : pairs_) {
            if (p.twin && p.a != from && p.a != to) continue;  // (a twin's border: not a way between the twins)
            uint16_t n = p.a == t ? p.b : p.b == t ? p.a : 0;
            if (n && avoid && n != to && std::find(avoid->begin(), avoid->end(), n) != avoid->end()) continue;
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
    table_ = ownTable_ ? ownTable_ : IsomTerrainTable::forEra(era_);
    if (!table_) {
        error = std::string("no ISOM terrain table for tileset ") + tilesetName(era_);
        return false;
    }
    pieces_.clear(), gameTile_.clear();
    if (table_->hasExtras() && tileset_)
        for (const auto& m : tileset_->modTerrains) {
            auto& by = pieces_.emplace_back();
            for (size_t i = 0; i < m.pieces.size(); ++i) {
                const auto& [g, l, r] = m.pieces[i];
                by[g].push_back({l, r});
                if (i + m.gapPieces < m.pieces.size()) {  // (a gap's null tile stands for no one game tile; so does a NULL half)
                    if (l > 1) gameTile_.emplace(l, uint16_t(g << 4));
                    if (r > 1) gameTile_.emplace(r, uint16_t((g + 1) << 4));
                }
            }
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
    if (!table) return error = std::string("no ISOM terrain table for tileset ") + tilesetName(era), false;
    if (!tiles_) return error = "the tileset is not loaded", false;
    const TerrainLayer tiles = chk.find("TILE") ? readTerrain(chk, "TILE") : readTerrain(chk, "MTXM");
    if (tiles.tiles.size() < size_t(w) * h) {
        error = "missing tiles (TILE, MTXM)";
        return false;
    }
    const int cols = w / 2 + 1, rows = h + 1;

    // Which ISOM combinations (parity, half, left and right shape) give each tile group: the rects StarEdit picks it for
    // (the tileset's CV5; a solid one weighs more: most ground is).
    struct Use {
        uint8_t parity, half;
        uint16_t left, right;
        double weight;
    };
    std::vector<std::vector<Use>> uses(1024);
    for (uint16_t g = 0; g + 1 < 1024; g += 2)
        for (const IsomTiles::Rect& r : tiles_->rectsOf(g))
            for (uint8_t half = 0; half < 2; ++half) uses[g + half].push_back({r.parity, half, r.left, r.right, r.left == r.right ? 2.0 : 1.0});

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

bool IsomGrid::create(Chk& chk, uint16_t width, uint16_t height, uint16_t era, uint16_t terrain, const Tileset& tileset, std::string& error) {
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
    if (!regenerateTiles(chk, std::vector<uint8_t>(rects, 1), tileset, error)) return false;  // (StarEdit's new map)
    setTileset(&tileset);
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
    fixFailed_.clear();
    for (const auto& c : locked)
        if (isCorner(c.x, c.y)) locked_[size_t(c.y + 1) * (rectCols_ + 3) + (c.x + 1)] = 1;
    // (a terrain with twin grounds: the one most around the stroke's first edit is its ground for the whole stroke; the
    // settle does not take the others, so the stroke's ring is one terrain: Caution dragged from Substructure on Jungle
    // out onto Dirt keeps that Substructure, with Jungle around it, and not a seam of the two Substructures that cannot be)
    avoid_.clear();
    const uint16_t terrain = locked.front().terrain;
    const std::vector<uint16_t> grounds = table_->groundsOf(terrain);
    if (grounds.size() > 1 && strokeTerrain_ == terrain && strokeGround_) {
        for (uint16_t g : grounds)
            if (g != strokeGround_) avoid_.push_back(g);
    } else if (grounds.size() > 1) {
        // (none of them there: the one nearest to the terrains there, so a stroke on Dirt makes the way up to it)
        std::vector<int> count(grounds.size(), 0), far(grounds.size(), 0);
        static const int nb[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}};
        for (const auto& c : locked)
            for (const auto& d : nb)
                if (const int x = c.x + d[0], y = c.y + d[1]; isCorner(x, y))
                    for (size_t k = 0; k < grounds.size(); ++k)
                        count[k] += corner(x, y) == grounds[k], far[k] += table_->distance(grounds[k], corner(x, y));
        size_t best = size_t(std::max_element(count.begin(), count.end()) - count.begin());
        if (!count[best]) best = size_t(std::min_element(far.begin(), far.end()) - far.begin());
        for (size_t k = 0; k < grounds.size(); ++k)
            if (k != best) avoid_.push_back(grounds[k]);
        strokeTerrain_ = terrain, strokeGround_ = grounds[best];
    }
    painted_ = terrain;
    settleFrom(locked, true);
    painted_ = 0;
    lastTerrain_ = locked.front().terrain;
    repairUntileable();
    avoid_.clear();
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
    // How far the wave may go from the seeds: as far as the longest chain of borders can need (each step of it a diamond:
    // 2 corners across), and a margin. A corner past it keeps its terrain: a seam there, not the whole map rebuilt (a mod's
    // terrains can ask for chains the space between two of them cannot hold, and then the wave never ended).
    int sx0 = 1 << 30, sy0 = 1 << 30, sx1 = -(1 << 30), sy1 = -(1 << 30);
    for (const auto& c : seeds) sx0 = std::min(sx0, c.x), sy0 = std::min(sy0, c.y), sx1 = std::max(sx1, c.x), sy1 = std::max(sy1, c.y);
    if (diameterOf_ != table_) diameter_ = std::max(1, table_->diameter()), diameterOf_ = table_;
    const int reach = 2 * diameter_ + 4;
    // (the game's terrains: no limit, as StarEdit's settle; a mod's only)
    const bool limited = table_->hasExtras();
    auto inReach = [&](int x, int y) { return !limited || (x >= sx0 - reach && x <= sx1 + reach && y >= sy0 - reach && y <= sy1 + reach); };
    if (markSource) {
        if (limited) reachX0_ = sx0 - reach, reachY0_ = sy0 - reach, reachX1_ = sx1 + reach, reachY1_ = sy1 + reach;
        else reachX0_ = 0, reachX1_ = -1;  // (no reach: repairUntileable runs where the edit made rects dirty)
    }
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
            if (!isCorner(cx, cy) || processed[idx(cx, cy)] || !inReach(cx, cy)) continue;
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
                if (!table_->isTerrain(t) || std::find(avoid_.begin(), avoid_.end(), t) != avoid_.end()) continue;
                // (a terrain beside a twin of its ground: only the painted one; Outpost on Outpost does not ring a High
                // Outpost painted on Snow)
                bool twinBeside = false;
                for (int i = 0; i < ns && t != painted_; ++i) twinBeside = twinBeside || table_->twinOnly(t, settled[i]);
                if (twinBeside) continue;
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
    if (!tiles_ || ry >= rectRows_ - 1) return true;  // last ISOM row has no tiles
    int par = (rx + ry) & 1;
    uint16_t left = par ? shapeAt(rx, ry + 1) : shapeAt(rx, ry);
    uint16_t right = par ? shapeAt(rx + 1, ry) : shapeAt(rx + 1, ry + 1);
    if (table_ && table_->hasExtras()) {  // (a mod's terrain: the tiles of its template)
        uint16_t gl = 0, gr = 0, tag = 0;
        if (!table_->templateRect(left, right, gl, gr, tag)) return false;
        const auto& gs = tiles_->groups(par, gl, gr);
        // piecesOnly_: a rect of a mod's terrain must be one its set has a piece for (not a rect where its border meets
        // another terrain's), or the template's solid rect (a terrain of edges only: the template's own tiles inside it).
        if (piecesOnly_ && tag && !gs.empty() && tag <= pieces_.size() && gl != gr)
            return std::any_of(gs.begin(), gs.end(), [&](uint16_t g) { return pieces_[tag - 1u].count(g) != 0; });
        return !gs.empty();
    }
    return !tiles_->groups(par, left, right).empty();  // (a group StarEdit picks for it: the tileset's CV5)
}

bool IsomGrid::stacksWithAbove(int rx, int ry) const {
    if (!tiles_ || ry <= 0 || ry >= rectRows_ - 1 || rx < 0 || rx >= rectCols_) return true;
    auto slots = [&](int x, int y, uint16_t& l, uint16_t& r) {
        int par = (x + y) & 1;
        std::tie(l, r) = templateSlots(x, y);
        return par;
    };
    uint16_t al, ar, bl, br;
    int pa = slots(rx, ry - 1, al, ar), pb = slots(rx, ry, bl, br);
    // Some group for each rect that stacks (CV5's stack connections), for both halves.
    const auto &above = tiles_->groups(pa, al, ar), &below = tiles_->groups(pb, bl, br);
    for (int half = 0; half < 2; ++half) {
        bool any = false;
        for (uint16_t a : above)
            for (uint16_t b : below) any = any || tiles_->stackBottom(uint16_t(a + half)) == tiles_->stackTop(uint16_t(b + half));
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
    // (a call like one that found nothing, with the same corners everywhere its checks look: nothing again)
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
    mix(uint64_t(rx) << 32 | uint32_t(ry)), mix(uint64_t(prefer) << 3 | uint64_t(stacks) << 2 | uint64_t(grow) << 1 | uint64_t(piecesOnly_));
    // (the rects 3 around, the ones above and below them that stacking reads, and the ones 2 rows on and a column aside
    // that a level table's rectTemplate reads for those; their corners, and the diamonds' shapes: an invalid diamond keeps
    // its last shape)
    for (int y = ry - 7; y <= ry + 8; ++y)
        for (int x = rx - 5; x <= rx + 6; ++x)
            mix(isCorner(x, y) ? corner(x, y) : uint64_t(shapeAt(x, y)) << 16);
    const bool remember = table_->hasExtras();  // (a mod's terrains only: the game's keep StarEdit's search as it was)
    if (remember && fixFailed_.count(key)) return false;
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
    // (a value at a corner that leaves a diamond there with no shape, where none of that diamond's other corners can
    // move: no trial with it can do; left out before the trials)
    std::vector<std::vector<uint8_t>> allowed(movable.size(), std::vector<uint8_t>(values.size(), 1));
    auto isMovable = [&](int x, int y) { return std::find(movable.begin(), movable.end(), std::pair{x, y}) != movable.end(); };
    for (size_t m = 0; m < movable.size(); ++m)
        for (size_t v = 0; v < values.size(); ++v) {
            const auto [mx, my] = movable[m];
            for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                const int x = mx + dx, y = my + dy;  // a diamond touching this corner
                if (!inGrid(x, y)) continue;
                const std::pair<int, int> cs[4] = {{x - 1, y}, {x, y - 1}, {x + 1, y}, {x, y + 1}};
                std::array<uint16_t, 4> c{};
                bool other = false;
                for (int i = 0; i < 4; ++i) {
                    const bool self = cs[i].first == mx && cs[i].second == my;
                    c[size_t(i)] = self ? values[v] : corner(cs[i].first, cs[i].second);
                    other = other || (!self && isMovable(cs[i].first, cs[i].second));
                }
                if (!other && !table_->shapeFor(c)) allowed[m][v] = 0;
            }
        }
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
                    if (!allowed[size_t(pick[size_t(i)])][size_t(val[size_t(i)])]) noop = true;
                    trial.push_back({x, y, v});
                }
                if (!noop) {
                    saved.clear();
                    std::vector<std::pair<int, int>> changed;
                    for (const auto& t : trial) saved.push_back(corner(t.x, t.y)), setCorner(t.x, t.y, t.terrain), changed.push_back({t.x, t.y});
                    // (only the rects with a diamond at a changed corner can change: the others keep what they were)
                    auto touched = [&](int x, int y) {
                        const int par = (x + y) & 1, ax = x, ay = par ? y + 1 : y, bx = x + 1, by = par ? y : y + 1;
                        for (auto [cx, cy] : changed)
                            if (std::abs(ax - cx) + std::abs(ay - cy) == 1 || std::abs(bx - cx) + std::abs(by - cy) == 1) return true;
                        return false;
                    };
                    bool ok = diamondsValid(changed) && good(rx, ry);
                    for (size_t r = 0; ok && r < nearRects.size(); ++r)
                        if (touched(nearRects[r].first, nearRects[r].second)) ok = rectTileable(nearRects[r].first, nearRects[r].second);
                    // (stacking reads the rects above and below; with levels, their templates read rects 2 rows on and a
                    // column aside too)
                    const int sx = table_->hasLevels() ? 1 : 0, sy = table_->hasLevels() ? 3 : 1;
                    for (size_t r = 0; ok && r < nearStacking.size(); ++r) {
                        const auto [x, y] = nearStacking[r];
                        bool near = false;
                        for (int dy = -sy; dy <= sy && !near; ++dy)
                            for (int dx = -sx; dx <= sx && !near; ++dx) near = touched(x + dx, y + dy);
                        if (near) ok = stacking(x, y);
                    }
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
    if (best.set.empty()) {
        if (remember) fixFailed_[key] = 1;
        return false;
    }
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
    // (the stroke's avoided grounds: Caution pushed out from Substructure on Jungle goes on through Jungle, not through
    // Substructure on Dirt; apply)
    const std::vector<uint16_t>* avoid = avoid_.empty() ? nullptr : &avoid_;
    for (const auto& p : pts) far = std::max(far, table_->path(terrain, corner(p[0], p[1]), avoid).size());
    if (far <= 1) return false;
    std::vector<CornerValue> moved;
    for (const auto& p : pts) {
        auto path = table_->path(terrain, corner(p[0], p[1]), avoid);
        if (path.size() == far) moved.push_back({p[0], p[1], path[path.size() - 2]});
    }
    // Neighbors outside this rect may not accept the new values; settle them too, or a push next to
    // unusual terrain can leave an invalid diamond behind.
    if (!moved.empty()) settleFrom(moved, false);
    return !moved.empty();
}

void IsomGrid::repairUntileable() {
    size_t lastBad = SIZE_MAX;  // (a pass that leaves no fewer untileable rects than the one before: the end)
    for (int pass = 0; pass < 16; ++pass) {
        bool changed = false;
        size_t bad = 0;
        for (int ry = 0; ry < rectRows_; ++ry)
            for (int rx = 0; rx < rectCols_; ++rx) {
                int par = (rx + ry) & 1;
                int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
                auto dirtyAt = [&](int x, int y) { return inGrid(x, y) && diamondDirty_[size_t(y) * (rectCols_ + 1) + x]; };
                if (!dirtyAt(ax, ay) && !dirtyAt(bx, by)) continue;
                // (within the edit's reach only: past it a rect keeps its terrain; the fixes do not run on across the map)
                if (reachX1_ >= reachX0_ && (rx < reachX0_ || rx > reachX1_ || ry < reachY0_ || ry > reachY1_)) continue;
                if (rectTileable(rx, ry)) {
                    // A mod's terrain where its set has no piece for the rect: the smallest change that gives it one,
                    // without the painted terrain growing (no such change: the game's tiles there).
                    if (!pieces_.empty()) {
                        piecesOnly_ = true;
                        if (!rectTileable(rx, ry)) changed |= fixRectMinimal(rx, ry, sourceNear(rx, ry), false, false);
                        piecesOnly_ = false;
                    }
                    continue;
                }
                // The smallest fix first (keeps thin walls thin), the old push toward the painted
                // terrain only when none exists.
                ++bad;
                changed |= fixRectMinimal(rx, ry, sourceNear(rx, ry)) || pushRect(rx, ry);
            }
        // (a mod's terrains: a pass that leaves no fewer untileable rects is the end; only the pieces-only fixes left: they
        // run on while they change)
        if (!changed || (table_->hasExtras() && bad && bad >= lastBad)) break;
        lastBad = bad;
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

std::pair<int, int> starEditCenter(int px, int py, int extent) {
    extent = std::max(extent, 1);
    // The nearest grid point (64 px across, 32 down; with the diamond distance, rounding each axis is nearest).
    int x = int(std::lround(px / 64.0)), y = int(std::lround(py / 32.0));
    if (extent % 2 == 1) {  // centered on a diamond: the nearest one
        diamondAtPixel(px, py, x, y);
        return {x, y};
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
    return {x, y};
}

std::vector<std::pair<int, int>> starEditBrushAt(int px, int py, int extent) {
    extent = std::max(extent, 1);
    const auto [x, y] = starEditCenter(px, py, extent);
    if (extent % 2 == 1) return starEditBrushDiamonds(x, y, extent);
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
    // A mod's terrains (IsomTerrainTable::withExtras): the terrain core makes the tiles of their templates, on a copy of
    // the map with the game's shapes and tiles in place of the mod's; then their rects take the mod's pieces for those.
    const bool own = table_ && table_->hasExtras();
    Chk copy;
    if (own) {
        // (a piece as the game's tile it stands for: only in a rect of a mod's terrain, by the ISOM before this store; the
        // same tiles placed elsewhere stay as they are)
        std::vector<uint8_t> modRect(size_t(rectCols_) * rectRows_, 0);
        for (int ry = 0; ry < rectRows_; ++ry)
            for (int rx = 0; rx < rectCols_; ++rx) {
                const uint16_t* q = &isom_[(size_t(ry) * rectCols_ + rx) * 4];
                uint16_t gl = 0, gr = 0, tag = 0;
                modRect[size_t(ry) * rectCols_ + rx] = table_->templateRect(uint16_t(q[0] >> 4), uint16_t(q[2] >> 4), gl, gr, tag) && tag;
            }
        auto take = [&](SectionName name, bool tiles) {
            const Section* s = chk.find(name);
            if (!s) return;
            Bytes d = s->data;
            for (size_t i = 0; tiles && i + 1 < d.size(); i += 2) {
                const size_t t = i / 2, x = mapWidth_ ? t % size_t(mapWidth_) : 0, y = mapWidth_ ? t / size_t(mapWidth_) : 0;
                if (y >= size_t(rectRows_) || !modRect[y * rectCols_ + x / 2]) continue;
                if (auto it = gameTile_.find(uint16_t(d[i] | d[i + 1] << 8)); it != gameTile_.end()) d[i] = uint8_t(it->second), d[i + 1] = uint8_t(it->second >> 8);
            }
            copy.set(name, std::move(d));
        };
        take("DIM ", false), take("ERA ", false), take("TILE", true), take("MTXM", true);
    }
    Chk& work = own ? copy : chk;
    size_t lastBreaks = 0;
    for (int pass = 0;; ++pass) {
        encodeDirty();
        for (size_t i = 0; i < touched.size(); ++i) touched[i] |= dirtyRects_[i];
        writeIsom(work, own);
        const TerrainLayer before = readTerrain(work, "TILE");
        if (!tileset_) return error = "the map's tileset is not loaded", false;
        if (!regenerateTiles(work, dirtyRects_, *tileset_, error)) return false;
        {  // tiles that changed beyond the edited rects (down a column, along a row) are part of the edit too
            const TerrainLayer after = readTerrain(work, "TILE");
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
        const auto breaks = stackBreaks(work, touched);
        // (no fewer breaks than the pass before: the changes do not help, more would only spread the terrain)
        if (own && pass > 0 && breaks.size() >= lastBreaks) break;
        lastBreaks = breaks.size();
        // (a mod's terrains: no growing; its terrains on one ground read that ground as other game terrains in their
        // templates (Floor and Plating on Substructure: Asphalt and Dirt), so beside each other their pieces may never
        // stack, and growing to make them stack spreads one through the other)
        const bool mayGrow = !own;
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
            else if (!mayGrow) continue;
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
    if (own) {
        writeIsom(chk);
        putPieces(chk, work, touched);
    }
    return true;
}

bool IsomGrid::rectTemplate(int rx, int ry, uint16_t& gl, uint16_t& gr, uint16_t& tag, uint16_t* ground) const {
    auto shapesOf = [&](int x, int y, uint16_t& l, uint16_t& r) {
        const int par = (x + y) & 1;
        l = par ? shapeAt(x, y + 1) : shapeAt(x, y), r = par ? shapeAt(x + 1, y) : shapeAt(x + 1, y + 1);
    };
    uint16_t l = 0, r = 0, from = 0, to = 0, t = 0;
    shapesOf(rx, ry, l, r);
    if (ground) *ground = 0;
    if (!table_ || !table_->templateRect(l, r, gl, gr, tag)) return false;
    if (!table_->hasExtras()) return true;
    // A rect of one terrain beside the border of a mod terrain whose ground is another terrain in the template (High Dirt
    // as Dirt beside Higher Dirt; the mod's Asphalt as Dirt beside Structure on Asphalt): the game's walls go down over the
    // ground below a cliff, its top edges stack under the ground above and its rects meet the ground beside, so the rects
    // around such a border (two above and below, one beside) are that template terrain too; also a mod terrain's own.
    const auto cl = table_->corners(l), cr = table_->corners(r);
    bool oneTerrain = table_->hasLevels();
    for (uint16_t c : {cl[1], cl[2], cl[3], cr[0], cr[1], cr[2], cr[3]}) oneTerrain = oneTerrain && c == cl[0];
    if (tag && !oneTerrain) {  // (a border: the ground's own tiles where its set has no piece)
        if (ground && table_->levelGround(l, r, from, to, t)) *ground = from;
        return true;
    }
    if (!oneTerrain) return true;
    if (!levelMask_.empty()) {  // (made for this pass: levelMask)
        const LevelRect& m = levelMask_[size_t(ry) * rectCols_ + rx];
        if (m.from && m.from == cl[0]) {
            gl = gr = m.to, tag = m.tag;
            if (ground) *ground = m.from;
        }
        return true;
    }
    for (int dy : {0, 1, -1, 2, -2})
        for (int dx : {0, -1, 1}) {
            const int x = rx + dx, y = ry - dy;
            if ((!dx && !dy) || x < 0 || y < 0 || x >= rectCols_ || y >= rectRows_) continue;
            uint16_t al = 0, ar = 0;
            shapesOf(x, y, al, ar);
            if (table_->levelGround(al, ar, from, to, t) && from == cl[0]) {
                gl = gr = to, tag = t;  // (a game terrain's solid shape is its id)
                if (ground) *ground = from;
                return true;
            }
        }
    return true;
}

IsomGrid::LevelMask::LevelMask(const IsomGrid& g) : grid(g) {
    if (!g.table_ || !g.table_->hasLevels() || !g.levelMask_.empty()) return;
    own = true;
    std::vector<LevelRect> mask(size_t(g.rectCols_) * g.rectRows_);
    for (int ry = 0; ry < g.rectRows_; ++ry)  // (from each rect with a border of a terrain a level higher: the rects around it)
        for (int rx = 0; rx < g.rectCols_; ++rx) {
            const int par = (rx + ry) & 1;
            uint16_t from = 0, to = 0, tag = 0;
            if (!g.table_->levelGround(par ? g.shapeAt(rx, ry + 1) : g.shapeAt(rx, ry), par ? g.shapeAt(rx + 1, ry) : g.shapeAt(rx + 1, ry + 1), from, to, tag)) continue;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = rx + dx, y = ry + dy;
                    if (x < 0 || y < 0 || x >= g.rectCols_ || y >= g.rectRows_) continue;
                    LevelRect& m = mask[size_t(y) * g.rectCols_ + x];
                    if (!m.from) m = {from, to, tag};
                }
        }
    g.levelMask_ = std::move(mask);
}

IsomGrid::LevelMask::~LevelMask() {
    if (own) grid.levelMask_.clear();
}

std::pair<uint16_t, uint16_t> IsomGrid::templateSlots(int rx, int ry) const {
    uint16_t gl = 0, gr = 0, tag = 0;
    if (rectTemplate(rx, ry, gl, gr, tag)) return {gl, gr};
    const int par = (rx + ry) & 1;
    return templatePair(par ? shapeAt(rx, ry + 1) : shapeAt(rx, ry), par ? shapeAt(rx + 1, ry) : shapeAt(rx + 1, ry + 1));
}

std::pair<uint16_t, uint16_t> IsomGrid::templatePair(uint16_t l, uint16_t r) const {
    uint16_t gl = l, gr = r, tag = 0;
    if (table_ && table_->hasExtras() && !table_->templateRect(l, r, gl, gr, tag)) gl = table_->templateShape(l), gr = table_->templateShape(r);
    return {gl, gr};
}

std::pair<uint16_t, uint16_t> IsomGrid::pickPiece(const std::vector<std::pair<uint16_t, uint16_t>>& pieces, const TerrainLayer& tile, int rx,
                                                  int ry, uint32_t h) const {
    return pickModPiece(*tileset_, pieces, tile, rx, ry, h);
}

std::pair<uint16_t, uint16_t> pickModPiece(const Tileset& tileset, const std::vector<std::pair<uint16_t, uint16_t>>& pieces, const TerrainLayer& tile,
                                           int rx, int ry, uint32_t h) {
    // (a set's first two variants of a rect are its plain ones, the others carry cracks, gates or marks: those 4 times as
    // often, as the fill's first variants; the maps put the others in here and there)
    auto pick = [&](const std::vector<size_t>& of) {
        uint32_t total = 0;
        for (size_t i : of) total += i < 2 ? 4 : 1;
        uint32_t k = h % total;
        for (size_t i : of) {
            const uint32_t w = i < 2 ? 4 : 1;
            if (k < w) return pieces[i];
            k -= w;
        }
        return pieces[of.back()];
    };
    std::vector<size_t> all(pieces.size());
    for (size_t i = 0; i < all.size(); ++i) all[i] = i;
    const auto& rules = tileset.pieceNeighbors;
    const auto& ruled = tileset.pieceNeighborTiles;
    if (rules.empty() || pieces.size() < 2) return pick(all);
    auto at = [&](int x, int y) -> int {  // a rect's left tile, when it has rules
        if (x < 0 || y < 0 || 2 * x + 1 >= tile.width || y >= tile.height) return -1;
        const uint16_t v = tile.tiles[size_t(y) * tile.width + 2 * x];
        return ruled.count(v) ? v : -1;
    };
    const int above = at(rx, ry - 1), upLeft = at(rx - 1, ry - 1), upRight = at(rx + 1, ry - 1);
    auto fits = [&](uint16_t c, bool sides) {
        if (above >= 0 && !rules.count(Tileset::pieceNeighborKey(uint16_t(above), c, 0))) return false;
        if (!sides) return true;
        if (upLeft >= 0 && !rules.count(Tileset::pieceNeighborKey(uint16_t(upLeft), c, 1))) return false;
        if (upRight >= 0 && !rules.count(Tileset::pieceNeighborKey(c, uint16_t(upRight), 2))) return false;
        return true;
    };
    std::vector<size_t> ok;
    for (bool sides : {true, false}) {
        for (size_t i = 0; i < pieces.size(); ++i)
            if (fits(pieces[i].first, sides)) ok.push_back(i);
        if (!ok.empty()) return pick(ok);
    }
    return pick(all);
}

void IsomGrid::putPieces(Chk& chk, const Chk& work, const std::vector<uint8_t>& rects) const {
    const LevelMask mask(*this);
    TerrainLayer tile = readTerrain(chk, "TILE"), mtxm = readTerrain(chk, "MTXM");
    const TerrainLayer wt = readTerrain(work, "TILE"), wm = readTerrain(work, "MTXM");
    const size_t n = size_t(mapWidth_) * mapHeight_;
    if (tile.tiles.size() < n || mtxm.tiles.size() < n || wt.tiles.size() < n || wm.tiles.size() < n) return;
    std::vector<uint16_t> tagOf(size_t(rectCols_) * rectRows_, 0);  // (each rect's mod terrain, for the seam pass below)
    for (int ry = 0; ry < mapHeight_; ++ry)
        for (int rx = 0; 2 * rx + 1 < mapWidth_; ++rx) {
            if (size_t(ry) * rectCols_ + rx >= rects.size() || !rects[size_t(ry) * rectCols_ + rx]) continue;
            const size_t k = size_t(ry) * mapWidth_ + 2 * rx;
            uint16_t l = wt.tiles[k], r = wt.tiles[k + 1], ml = wm.tiles[k], mr = wm.tiles[k + 1];
            uint16_t gl = 0, gr = 0, tag = 0, ground = 0;
            if (rectTemplate(rx, ry, gl, gr, tag, &ground) && tag && tag <= pieces_.size()) {
                tagOf[size_t(ry) * rectCols_ + rx] = tag;
                const auto& by = pieces_[tag - 1u];
                // (the rows of a block stand one under the other: a rect under one with a piece of the same terrain
                // takes the variant of the top one, so that a wall's gate or door keeps its parts together)
                int top = ry;
                for (int up = 1; up <= 2 && ry - up >= 0; ++up) {
                    uint16_t ul = 0, ur = 0, utag = 0;
                    if (!rectTemplate(rx, ry - up, ul, ur, utag) || utag != tag) break;
                    const uint16_t ug = uint16_t(wt.tiles[size_t(ry - up) * mapWidth_ + 2 * rx] >> 4);
                    if (auto u = by.find(ug); u == by.end() || u->second.empty()) break;
                    top = ry - up;
                }
                uint32_t h = uint32_t(2 * rx) * 73856093u ^ uint32_t(top) * 19349663u;
                h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
                // (no piece for the game's rect: its tiles, as inside a terrain of edges only; under a cliff a level
                // higher: the ground's own tiles, not its template's)
                if (auto it = by.find(uint16_t(l >> 4)); it != by.end() && !it->second.empty()) {
                    // (under a piece of the same terrain: the next row of its block, one group down at the same subtiles,
                    // when this rect has it: the rows of a wall stay one variant also where the maps give no rule)
                    const uint16_t ul = top < ry ? tile.tiles[k - size_t(mapWidth_)] : 0, ur = top < ry ? tile.tiles[k + 1 - size_t(mapWidth_)] : 0;
                    const auto next = std::find(it->second.begin(), it->second.end(), std::pair<uint16_t, uint16_t>{uint16_t(ul + 16), uint16_t(ur + 16)});
                    const auto [pl, pr] = top < ry && next != it->second.end() ? *next : pickPiece(it->second, tile, rx, ry, h);
                    l = ml = pl, r = mr = pr;
                } else if (uint16_t gt = 0, gtag = 0; ground && table_->modTerrain(ground, gt, gtag)) {  // (a mod terrain: its fill)
                    const int par = (rx + ry) & 1;
                    const auto& gs = tiles_->groups(par, gt, gt);
                    for (size_t n = 0; n < gs.size() && gtag && gtag <= pieces_.size(); ++n)
                        if (auto f = pieces_[gtag - 1u].find(gs[(h + n) % gs.size()]); f != pieces_[gtag - 1u].end() && !f->second.empty()) {
                            const auto [pl, pr] = f->second[(h >> 8) % uint32_t(f->second.size())];
                            l = ml = pl, r = mr = pr;
                            break;
                        }
                } else if (ground) {
                    const int par = (rx + ry) & 1;
                    const auto& gs = tiles_->groups(par, ground, ground);  // (a game terrain's solid shape is its id)
                    if (!gs.empty() && size_t(gs[0]) + 1 < tileset_->groups.size()) {
                        const uint16_t g = gs[h % gs.size()], k = uint16_t((h >> 8) % 4);  // (one of its first variants)
                        const uint16_t sub = tileset_->groups[g].megatiles[k] && tileset_->groups[g + 1u].megatiles[k] ? k : 0;
                        l = ml = uint16_t(g << 4 | sub), r = mr = uint16_t((g + 1) << 4 | sub);
                    }
                }
            }
            tile.tiles[k] = l, tile.tiles[k + 1] = r, mtxm.tiles[k] = ml, mtxm.tiles[k + 1] = mr;
        }
    // Seams: a mod terrain's piece whose edges do not meet the tiles around it (a thin slice or an inner corner its set has
    // no piece for: a block's lower row under the ground) takes the piece of that terrain whose edges meet them best, when
    // it is much better (from the top down, so a rect's upper neighbor is final).
    if (tileset_) {
        auto pixel = [&](uint16_t value, int px, int py) -> const Rgb* {
            const int mg = tileset_->megatileFor(value);
            if (mg < 0 || size_t(mg) >= tileset_->megatiles.size()) return nullptr;
            const uint32_t e = tileset_->megatiles[size_t(mg)][size_t(py / 8 * 4 + px / 8)];
            if ((e >> 1) >= tileset_->minitiles.size()) return nullptr;
            return &tileset_->palette[tileset_->minitiles[e >> 1][size_t(py % 8 * 8 + ((e & 1) ? 7 - px % 8 : px % 8))]];
        };
        auto at = [&](int x, int y) -> int { return x >= 0 && y >= 0 && x < mapWidth_ && y < mapHeight_ ? tile.tiles[size_t(y) * mapWidth_ + x] : -1; };
        // A tile's four edges (left, right, top, bottom: 32 pixels each, RGB), read once per tile value and tileset.
        struct Edges {
            bool ok = false;
            uint8_t px[4][32][3];
        };
        thread_local std::map<std::pair<const Tileset*, uint16_t>, Edges> kept;  // (each thread its own)
        // (another tileset's graphics, or the same tileset loaded again: its edges read again)
        thread_local std::tuple<const void*, const void*, size_t, size_t> keptFor{};
        const auto now = std::make_tuple(static_cast<const void*>(tileset_->megatiles.data()), static_cast<const void*>(tileset_->minitiles.data()),
                                         tileset_->megatiles.size(), tileset_->minitiles.size());
        if (keptFor != now) kept.clear(), keptFor = now;
        auto edgesOf = [&](uint16_t v) -> const Edges& {
            auto [it, fresh] = kept.try_emplace({tileset_, v});
            if (fresh) {
                it->second.ok = tileset_->megatileFor(v) >= 0;
                for (int d = 0; d < 4 && it->second.ok; ++d)
                    for (int i = 0; i < 32; ++i) {
                        const int x = d == 0 ? 0 : d == 1 ? 31 : i, y = d == 2 ? 0 : d == 3 ? 31 : i;
                        const Rgb* p = pixel(v, x, y);
                        const uint8_t c[3] = {uint8_t(p ? p->r : 0), uint8_t(p ? p->g : 0), uint8_t(p ? p->b : 0)};
                        std::copy(c, c + 3, it->second.px[d][i]);
                    }
            }
            return it->second;
        };
        // The mean difference (RGB) along the edges of the pair (l, r) at (tx, ty) with the tiles around it.
        auto seam = [&](uint16_t l, uint16_t r, int tx, int ty) {
            int64_t sum = 0, cnt = 0;
            auto edge = [&](uint16_t v, int d, int w) {  // d: 0 left, 1 right, 2 top, 3 bottom; w the neighbor there
                if (w < 0) return;
                const Edges &a = edgesOf(v), &b = edgesOf(uint16_t(w));
                if (!a.ok || !b.ok) return;
                const int o = d ^ 1;  // (the neighbor's facing edge)
                for (int i = 0; i < 32; ++i)
                    for (int k = 0; k < 3; ++k) sum += std::abs(int(a.px[d][i][k]) - int(b.px[o][i][k]));
                cnt += 96;
            };
            edge(l, 0, at(tx - 1, ty)), edge(r, 1, at(tx + 2, ty));
            edge(l, 2, at(tx, ty - 1)), edge(r, 2, at(tx + 1, ty - 1));
            edge(l, 3, at(tx, ty + 1)), edge(r, 3, at(tx + 1, ty + 1));
            return cnt ? double(sum) / double(cnt) : 0.0;
        };
        std::map<uint32_t, std::vector<std::pair<uint16_t, uint16_t>>> all;  // (the candidates of a terrain and a parity, made once a call)
        for (int ry = 0; ry < mapHeight_; ++ry)
            for (int rx = 0; 2 * rx + 1 < mapWidth_; ++rx) {
                const uint16_t tag = size_t(ry) * rectCols_ + rx < tagOf.size() ? tagOf[size_t(ry) * rectCols_ + rx] : 0;
                if (!tag) continue;
                const int par = (rx + ry) & 1;
                {  // (edges only: a rect whose two diamonds have one terrain at all their corners keeps its fill)
                    const int ax = rx, ay = par ? ry + 1 : ry, bx = rx + 1, by = par ? ry : ry + 1;
                    const int pts[6][2] = {{ax - 1, ay}, {ax, ay - 1}, {ax, ay + 1}, {bx + 1, by}, {bx, by - 1}, {bx, by + 1}};
                    bool one = true;
                    for (const auto& q : pts) one = one && terrainAt(q[0], q[1]) == terrainAt(pts[0][0], pts[0][1]);
                    if (one) continue;
                }
                const size_t k = size_t(ry) * mapWidth_ + 2 * rx;
                if (tile.tiles[k] == 0 && tile.tiles[k + 1] == 1) continue;  // (the null tile: a gap in the set, kept in sight)
                // (a row of a block's column, under its row above or over its row below: the wall stays one piece)
                auto continues = [&](size_t upper, size_t lower) {
                    return tile.tiles[lower] == uint16_t(tile.tiles[upper] + 16) && tile.tiles[lower + 1] == uint16_t(tile.tiles[upper + 1] + 16) &&
                           tile.tiles[upper] >= 16;
                };
                if ((ry > 0 && continues(k - size_t(mapWidth_), k)) || (ry + 1 < mapHeight_ && continues(k, k + size_t(mapWidth_)))) continue;
                const double now = seam(tile.tiles[k], tile.tiles[k + 1], 2 * rx, ry);
                if (now < 28) continue;
                // (the candidates: the terrain's pieces for game rects of this parity, so that a diagonal keeps its way; of
                // other shapes too: the set may have no fitting piece of the game's shapes here)
                auto [it, fresh] = all.try_emplace(uint32_t(tag) << 1 | uint32_t(par));
                if (fresh) {
                    std::set<std::pair<uint16_t, uint16_t>> seen;
                    for (const auto& [g, list] : pieces_[tag - 1u]) {
                        const auto& rs = tiles_->rectsOf(uint16_t(g & ~1u));
                        if (std::none_of(rs.begin(), rs.end(), [&](const IsomTiles::Rect& r) { return r.parity == par; })) continue;
                        for (const auto& p : list)
                            if (p != std::pair<uint16_t, uint16_t>{0, 1} && seen.insert(p).second) it->second.push_back(p);
                    }
                }
                std::pair<uint16_t, uint16_t> best{tile.tiles[k], tile.tiles[k + 1]};
                double bestScore = now;
                for (const auto& p : it->second)
                    if (const double sc = seam(p.first, p.second, 2 * rx, ry); sc < bestScore) bestScore = sc, best = p;
                if (bestScore < now * 0.6)
                    tile.tiles[k] = mtxm.tiles[k] = best.first, tile.tiles[k + 1] = mtxm.tiles[k + 1] = best.second;
            }
    }
    Bytes t, m;
    for (uint16_t v : tile.tiles) appendU16(t, v);
    for (uint16_t v : mtxm.tiles) appendU16(m, v);
    chk.set("TILE", std::move(t)), chk.set("MTXM", std::move(m));
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

void IsomGrid::writeIsom(Chk& chk, bool templates) const {
    Bytes data;
    data.reserve(isom_.size() * 2);
    const uint16_t known = templates && tiles_ ? tiles_->shapeCount() : 0;  // (the terrain core reads its tables by the value: none past them)
    const LevelMask mask(*this);
    if (!templates)
        for (uint16_t v : isom_) appendU16(data, v);
    else  // (each rect's slots as the game's rect of its template: the core reads a rect's own four slots)
        for (int ry = 0; ry < rectRows_; ++ry)
            for (int rx = 0; rx < rectCols_; ++rx) {
                const uint16_t* q = &isom_[(size_t(ry) * rectCols_ + rx) * 4];
                const auto [gl, gr] = templateSlots(rx, ry);
                for (int k = 0; k < 4; ++k) {
                    const uint16_t s = rightSlot(rx, ry, k) ? gr : gl;
                    appendU16(data, s < known ? uint16_t(s << 4 | (q[k] & 15)) : uint16_t(0));
                }
            }
    // Keep any bytes beyond the expected size (unusual, but preserves the original).
    if (const Section* s = chk.find("ISOM"); s && s->data.size() > data.size())
        data.insert(data.end(), s->data.begin() + data.size(), s->data.end());
    chk.set("ISOM", std::move(data));
}

std::vector<std::pair<int, int>> IsomGrid::stackBreaks(const Chk& chk, const std::vector<uint8_t>& area) const {
    std::vector<std::pair<int, int>> out;
    if (!tiles_) return out;
    {  // by the tileset's CV5 (the terrain core's tables)
        TerrainLayer tile = readTerrain(chk, "TILE");
        auto group = [&](int tx, int ty) { return int(tile.tiles[size_t(ty) * mapWidth_ + tx] >> 4); };
        const LevelMask mask(*this);
        auto placed = [&](int tx, int ty) {  // a tile placed as it is: not the terrain's fault
            const int rx = tx / 2;
            uint16_t gl = uint16_t(isom_[(size_t(ty) * rectCols_ + rx) * 4] >> 4), gr = uint16_t(isom_[(size_t(ty) * rectCols_ + rx) * 4 + 2] >> 4);
            if (table_ && table_->hasExtras()) std::tie(gl, gr) = templateSlots(rx, ty);  // (a mod's terrains: the rect's template)
            return !groupFromCv5(*tiles_, group(tx, ty), ty > 0 ? group(tx, ty - 1) : -1, (rx + ty) & 1, tx & 1, gl, gr);
        };
        // (a mod terrain a level higher: the ground beside its cliff is its template's lower terrain, the ground farther
        // away its own; where they meet the game has no tiles, and the ground's own tiles go there: no break)
        const bool levels = table_ && table_->hasExtras();
        auto lowered = [&](int rx, int ry) {
            uint16_t gl = 0, gr = 0, tag = 0, ground = 0;
            return levels && rectTemplate(rx, ry, gl, gr, tag, &ground) && ground;
        };
        for (int ty = 1; ty < mapHeight_; ++ty)
            for (int tx = 0; tx < mapWidth_; ++tx) {
                const int rx = tx / 2;
                if (!area[size_t(ty) * rectCols_ + rx] && !area[size_t(ty - 1) * rectCols_ + rx]) continue;
                const int a = group(tx, ty - 1), b = group(tx, ty);
                if (a >= 1024 || b >= 1024 || placed(tx, ty - 1) || placed(tx, ty) || (levels && lowered(rx, ty - 1) != lowered(rx, ty))) continue;
                if (tiles_->stackBottom(uint16_t(a)) != tiles_->stackTop(uint16_t(b))) out.push_back({rx, ty});
            }
        for (int ty = 0; ty < mapHeight_; ++ty)  // rects side by side that share no diamond shape
            for (int tx = 1; tx + 1 < mapWidth_; tx += 2) {
                if (!area[size_t(ty) * rectCols_ + tx / 2] && !area[size_t(ty) * rectCols_ + (tx + 1) / 2]) continue;
                const int l = group(tx, ty), r = group(tx + 1, ty);
                if (l >= 1024 || r >= 1024 || placed(tx, ty) || placed(tx + 1, ty) || (levels && lowered(tx / 2, ty) != lowered((tx + 1) / 2, ty))) continue;
                if (!tiles_->beside(uint16_t(l - 1), uint16_t(r))) out.push_back({tx / 2, ty + 1}), out.push_back({(tx + 1) / 2, ty + 1});
            }
        return out;
    }
    return out;
}

// --- TILE regeneration -------------------------------------------------------------------------



int countTileMisfits(const Chk& chk, const Tileset& tileset, std::vector<std::pair<int, int>>* where) {
    const IsomTiles* tiles = IsomTiles::forTileset(tileset);
    if (!tiles) return 0;
    const TerrainLayer t = readTerrain(chk, "TILE");
    int n = 0;
    for (int y = 0; y < t.height; ++y)
        for (int x = 0; x < t.width; ++x) {
            const uint16_t g = uint16_t(t.tiles[size_t(y) * t.width + x] >> 4);
            if (g >= 1024 || tiles->rectsOf(uint16_t(g & ~1u)).empty()) continue;  // doodads, groups the ISOM never gives: no rule
            const int above = y > 0 ? t.tiles[size_t(y - 1) * t.width + x] >> 4 : 1024, left = x > 0 ? t.tiles[size_t(y) * t.width + x - 1] >> 4 : 1024;
            const bool stacks = above >= 1024 || tiles->stackBottom(uint16_t(above)) == tiles->stackTop(g);
            // (a rect's left tile and the right tile of the rect before it; a rect's own two tiles always meet)
            const bool meets = (x & 1) || left >= 1024 || tiles->beside(uint16_t(left - 1), g);
            if (stacks && meets) continue;
            ++n;
            if (where) where->push_back({x, y});
        }
    return n;
}


bool regenerateTiles(Chk& chk, const std::vector<uint8_t>& dirtyRects, const Tileset& tileset, std::string& error) {
    if (tileset.groups.empty() || (tileset.era & 7) != (readEra(chk) & 7)) return error = "the map's tileset is not loaded", false;
    IsomCore core;
    if (!core.load(chk, tileset, error)) return false;
    core.retile(dirtyRects);  // (a rect no tile group fits keeps its tiles)
    core.store(chk);
    return true;
}




bool tileFromIsom(const Chk& chk, const TerrainLayer& tile, int tx, int ty, const Tileset& tileset) {
    const int w = tile.width, h = tile.height;
    if (tx < 0 || ty < 0 || tx >= w || ty >= h || tile.tiles.size() < size_t(w) * h) return true;
    const IsomTiles* tiles = IsomTiles::forTileset(tileset);
    const Section* isomSec = chk.find("ISOM");
    const int rectCols = w / 2 + 1, rx = tx / 2;
    if (!tiles || !isomSec || isomSec->data.size() < size_t(rectCols) * (h + 1) * 8) return true;
    auto shape = [&](int k) { return uint16_t(readU16(isomSec->data, ((size_t(ty) * rectCols + rx) * 4 + k) * 2) >> 4); };
    const size_t i = size_t(ty) * w + tx;
    return groupFromCv5(*tiles, tile.tiles[i] >> 4, ty > 0 ? tile.tiles[i - w] >> 4 : -1, (rx + ty) & 1, tx & 1, shape(0), shape(2));
}

bool IsomGrid::tileFromIsom(const Chk& chk, const TerrainLayer& tile, int tx, int ty) const {
    if (!tileset_) return true;
    if (!table_ || !table_->hasExtras() || !tiles_) return stardraft::tileFromIsom(chk, tile, tx, ty, *tileset_);
    const int w = tile.width, h = tile.height;
    if (tx < 0 || ty < 0 || tx >= w || ty >= h || tile.tiles.size() < size_t(w) * h || ty >= rectRows_ - 1 || tx / 2 >= rectCols_) return true;
    const int rx = tx / 2, par = (rx + ty) & 1, half = tx & 1;
    uint16_t gl = 0, gr = 0, tag = 0, ground = 0;  // (as putPieces gives the rect its tiles)
    if (!rectTemplate(rx, ty, gl, gr, tag, &ground)) return true;  // (no rect of the game's: nothing to compare with)
    const size_t i = size_t(ty) * w + tx;
    const uint16_t v = tile.tiles[i];
    if (tag && tag <= pieces_.size())
        for (uint16_t g : tiles_->groups(par, gl, gr))
            if (auto it = pieces_[tag - 1u].find(g); it != pieces_[tag - 1u].end())
                for (const auto& [l, r] : it->second)
                    if ((half ? r : l) == v) return true;
    if (uint16_t gt = 0, gtag = 0; ground && table_->modTerrain(ground, gt, gtag)) {  // (the ground's own tiles where the set has no piece)
        for (uint16_t g : tiles_->groups(par, gt, gt)) {
            if (!gtag || gtag > pieces_.size()) break;
            if (auto f = pieces_[gtag - 1u].find(g); f != pieces_[gtag - 1u].end())
                for (const auto& [l, r] : f->second)
                    if ((half ? r : l) == v) return true;
        }
    } else if (ground)
        for (uint16_t g : tiles_->groups(par, ground, ground))
            if ((v >> 4) == uint16_t(g + half)) return true;
    auto game = [&](uint16_t value) {  // (a piece as the game's tile it is for: only in a rect of the mod's terrain)
        const auto it = tag ? gameTile_.find(value) : gameTile_.end();
        return int((it == gameTile_.end() ? value : it->second) >> 4);
    };
    return groupFromCv5(*tiles_, game(v), ty > 0 ? game(tile.tiles[i - w]) : -1, par, half, gl, gr);
}

int solidTileGroup(const Tileset& tileset, uint16_t terrain, int half) {
    const IsomTiles* tiles = IsomTiles::forTileset(tileset);
    if (!tiles) return -1;
    const auto& gs = tiles->groups(0, terrain, terrain);
    return gs.empty() ? -1 : int(gs[0]) + (half & 1);
}

bool solidRectTiles(const Tileset& tileset, uint16_t terrain, const TerrainLayer& tile, int rx, int ry, uint32_t rnd, uint16_t& left, uint16_t& right) {
    const IsomTiles* tiles = IsomTiles::forTileset(tileset);
    if (!tiles) return false;
    // (the next random number of the rect: each choice takes its own)
    rnd = rnd * 0x9E3779B1u | 1u;
    auto next = [&] {
        rnd ^= rnd << 13, rnd ^= rnd >> 17, rnd ^= rnd << 5;
        return rnd;
    };
    const int parity = (rx + ry) & 1;
    const size_t w = tile.width, above = ry > 0 && size_t(2 * rx) < w && tile.tiles.size() >= size_t(ry) * w ? size_t(ry - 1) * w + size_t(2 * rx) : SIZE_MAX;
    // A mod terrain: its pieces for the stand-in's solid groups (its fill), picked as the brush picks them.
    const Tileset::ModTerrain* mod = tileset.modTerrain(terrain);
    if (mod) {
        std::vector<std::pair<uint16_t, uint16_t>> fill;
        for (uint16_t g : tiles->groups(parity, mod->standIn, mod->standIn)) {
            for (size_t i = 0; i + mod->gapPieces < mod->pieces.size(); ++i)
                if (mod->pieces[i][0] == g) fill.push_back({mod->pieces[i][1], mod->pieces[i][2]});
            if (!fill.empty()) break;
        }
        if (!fill.empty()) {
            // (under a piece of its fill: the next row of that piece's block, one group down at the same subtiles, when the
            // fill has it, as the brush keeps a block's rows together; else a piece as the brush picks it)
            if (above != SIZE_MAX && size_t(2 * rx + 1) < w) {  // (the rect's both tiles on the map)
                const std::pair<uint16_t, uint16_t> down{uint16_t(tile.tiles[above] + 16), uint16_t(tile.tiles[above + 1] + 16)};
                if (std::find(fill.begin(), fill.end(), down) != fill.end()) {
                    std::tie(left, right) = down;
                    return true;
                }
            }
            std::tie(left, right) = pickModPiece(tileset, fill, tile, rx, ry, next());
            return true;
        }
        terrain = mod->standIn;  // (no fill of its own: the stand-in's tiles inside it)
    }
    // A game terrain: its solid groups, the one that stacks under the tile above (as StarEdit picks), else the first.
    const auto& gs = tiles->groups(parity, terrain, terrain);
    if (gs.empty()) return false;
    uint16_t g = gs[0];
    if (above != SIZE_MAX)
        for (uint16_t c : gs)
            if (tiles->stackTop(c) == tiles->stackBottom(uint16_t(tile.tiles[above] >> 4))) {
                g = c;
                break;
            }
    if (size_t(g) + 1 >= tileset.groups.size()) return false;
    // Its variant, by IsomTerrain's rule (getRandomSubtile): the common variants (before the first empty megatile), and
    // 1 time in 20 a rare one (the variants after that gap).
    const auto& mega = tileset.groups[g].megatiles;
    int common = 0, rare = 0;
    while (common < 16 && mega[size_t(common)]) ++common;
    while (common + rare + 1 < 16 && mega[size_t(common + rare + 1)]) ++rare;
    uint16_t sub = 0;
    // (under the group above in its stack: the stack's variant, as StarEdit gives a whole stack one variant)
    if (above != SIZE_MAX && tiles->stackTop(g) && tiles->stackTop(g) == tiles->stackBottom(uint16_t(tile.tiles[above] >> 4)) && mega[tile.tiles[above] & 15])
        sub = uint16_t(tile.tiles[above] & 15);
    else if (rare && next() % 20 == 0) sub = uint16_t(common + 1 + int(next() % uint32_t(rare)));
    else if (common) sub = uint16_t(next() % uint32_t(common));
    left = uint16_t(g << 4 | sub), right = uint16_t((g + 1) << 4 | sub);
    return true;
}

std::vector<std::array<uint16_t, 2>> tileGroupTerrains(const Tileset& tileset) {
    const size_t groupCount = tileset.groups.size();
    std::vector<std::array<uint16_t, 2>> out(groupCount, {0, 0});
    const IsomTiles* tiles = IsomTiles::forTileset(tileset);
    const IsomTerrainTable* table = IsomTerrainTable::forEra(tileset.era);
    if (!tiles || !table) return out;
    for (size_t g = 0; g + 1 < groupCount && g < 1024; g += 2) {
        // The terrains at the corners of the rects StarEdit picks this group for: one when it is a rect of one
        // terrain; else the two most common (a transition).
        std::vector<std::pair<uint16_t, int>> counts;
        bool solid = false;
        for (const IsomTiles::Rect& r : tiles->rectsOf(uint16_t(g))) {
            std::vector<uint16_t> seen;
            for (uint16_t s : {r.left, r.right})
                for (uint16_t t : table->corners(s))
                    if (t && std::find(seen.begin(), seen.end(), t) == seen.end()) seen.push_back(t);
            if (seen.size() == 1) solid = true, counts = {{seen[0], 1}};
            if (solid) break;
            for (uint16_t t : seen) {
                auto it = std::find_if(counts.begin(), counts.end(), [t](const auto& p) { return p.first == t; });
                if (it == counts.end()) counts.push_back({t, 1});
                else ++it->second;
            }
        }
        if (counts.empty()) continue;
        std::sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        out[g][0] = out[g + 1][0] = counts[0].first;
        if (!solid && counts.size() > 1) out[g][1] = out[g + 1][1] = counts[1].first;
    }
    for (const auto& [g, t, other] : tileset.groupTerrains)
        if (g < groupCount) out[g] = {t, other};
    return out;
}


}  // namespace stardraft
