#include "stardraft/pathing.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include <queue>
#include <thread>

namespace stardraft {

namespace {

constexpr uint16_t kStartLocation = 214;

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }


struct Box {
    int left, top, right, bottom;  // pixels, right/bottom exclusive
};
Box collisionBox(const UnitType& t, int x, int y) { return {x - t.sizeLeft, y - t.sizeUp, x + t.sizeRight + 1, y + t.sizeDown + 1}; }
Box placementBox(const UnitType& t, int x, int y) {
    const int w = t.placementWidth, h = t.placementHeight;
    return {x - w / 2, y - h / 2, x - w / 2 + w, y - h / 2 + h};
}

struct Obstacle {
    uint16_t type;
    int x, y;
};

// Every static obstacle: the UNIT units and the THG2 entries that are units.
std::vector<Obstacle> obstacles(const PathingInput& in) {
    std::vector<Obstacle> out;
    const UnitTypes& types = *in.types;
    for (const Unit& u : *in.units) {
        if (u.type >= types.size()) continue;
        const UnitType& t = types[u.type];
        if (!isStaticObstacle(t, u.type)) continue;
        if (t.is(UnitFlag::FlyingBuilding) && (u.state & UnitState::InTransit)) continue;  // lifted off
        out.push_back({u.type, u.x, u.y});
    }
    if (in.sprites)
        for (const MapSprite& s : *in.sprites) {
            if ((s.flags & SpriteFlag::DrawAsSprite) || s.id >= types.size()) continue;
            if (isStaticObstacle(types[s.id], s.id)) out.push_back({s.id, s.x, s.y});
        }
    return out;
}

// Tiles (32 px) that are buildable (CV5) and that no obstacle's placement box covers.
std::vector<uint8_t> freeBuildableTiles(const PathingInput& in, const std::vector<Obstacle>& obs) {
    const TerrainLayer& ter = *in.terrain;
    const int w = ter.width, h = ter.height;
    std::vector<uint8_t> out(size_t(w) * h, 0);
    for (int i = 0; i < w * h; ++i) {
        const uint16_t v = ter.tiles[size_t(i)];
        const TileGroup* g = in.tileset->groupFor(v);
        out[size_t(i)] = g && in.tileset->megatileFor(v) >= 0 && !(g->buildability & Tileset::kUnbuildable);
    }
    for (const Obstacle& o : obs) {
        const Box b = placementBox((*in.types)[o.type], o.x, o.y);
        for (int ty = std::max(0, b.top / 32); ty < h && ty * 32 < b.bottom; ++ty)
            for (int tx = std::max(0, b.left / 32); tx < w && tx * 32 < b.right; ++tx) out[size_t(ty) * w + tx] = 0;
    }
    return out;
}

// Cells a unit of `cells` can stand on with the cell at its top-left, and the connected areas of those cells.
struct Standable {
    std::vector<int32_t> area;  // -1: the unit cannot stand there
};
Standable standableAreas(const WalkGrid& g, const std::vector<uint8_t>& clear, int cells) {
    Standable s;
    const int n = g.width * g.height;
    s.area.assign(size_t(n), -1);
    std::vector<int> stack;
    int next = 0;
    for (int start = 0; start < n; ++start) {
        if (s.area[size_t(start)] != -1 || clear[size_t(start)] < cells) continue;
        s.area[size_t(start)] = next;
        stack.push_back(start);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int x = c % g.width, y = c / g.width;
            const int nb[4] = {x > 0 ? c - 1 : -1, x + 1 < g.width ? c + 1 : -1, y > 0 ? c - g.width : -1, y + 1 < g.height ? c + g.width : -1};
            for (int m : nb)
                if (m >= 0 && s.area[size_t(m)] == -1 && clear[size_t(m)] >= cells) s.area[size_t(m)] = next, stack.push_back(m);
        }
        ++next;
    }
    return s;
}

// The cell nearest (cx, cy) where the unit can stand, at most `radius` cells away; -1 if none.
int nearestStandable(const WalkGrid& g, const std::vector<uint8_t>& clear, int cells, int cx, int cy, int radius) {
    int best = -1, bestD = std::numeric_limits<int>::max();
    for (int y = std::max(0, cy - radius); y <= std::min(g.height - 1, cy + radius); ++y)
        for (int x = std::max(0, cx - radius); x <= std::min(g.width - 1, cx + radius); ++x) {
            if (clear[size_t(y) * g.width + x] < cells) continue;
            const int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d < bestD) bestD = d, best = y * g.width + x;
        }
    return best;
}

GroundPath aStar(const WalkGrid& g, const std::vector<uint8_t>& clear, int cells, int from, int to) {
    GroundPath p;
    const int w = g.width, n = g.width * g.height;
    constexpr float kDiag = 1.41421356f;
    std::vector<float> cost(size_t(n), std::numeric_limits<float>::infinity());
    std::vector<int32_t> parent(size_t(n), -1);
    std::vector<uint8_t> closed(size_t(n), 0);
    const int tx = to % w, ty = to / w;
    auto h = [&](int c) {
        const int dx = std::abs(c % w - tx), dy = std::abs(c / w - ty);
        return (std::max(dx, dy) + (kDiag - 1) * std::min(dx, dy)) * 1.0001f;  // a little over: fewer ties
    };
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    cost[size_t(from)] = 0;
    open.push({h(from), from});
    auto ok = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < g.height && clear[size_t(y) * w + x] >= cells; };
    while (!open.empty()) {
        const int c = open.top().second;
        open.pop();
        if (closed[size_t(c)]) continue;
        closed[size_t(c)] = 1;
        if (c == to) break;
        const int x = c % w, y = c / w;
        const float gc = cost[size_t(c)];
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const int nx = x + dx, ny = y + dy;
                if (!ok(nx, ny)) continue;
                if (dx && dy && (!ok(x + dx, y) || !ok(x, y + dy))) continue;  // no corner cutting
                const int m = ny * w + nx;
                if (closed[size_t(m)]) continue;
                const float ng = gc + (dx && dy ? kDiag : 1.0f);
                if (ng < cost[size_t(m)]) cost[size_t(m)] = ng, parent[size_t(m)] = c, open.push({ng + h(m), m});
            }
    }
    if (!closed[size_t(to)]) return p;
    p.found = true;
    p.lengthTiles = cost[size_t(to)] / 4.0;
    std::vector<int> chain;
    for (int c = to; c != -1; c = parent[size_t(c)]) chain.push_back(c);
    std::reverse(chain.begin(), chain.end());
    const float half = cells * 4.0f;  // from the top-left cell to the unit's middle, in pixels
    int lastDir = 0x7fffffff;
    for (size_t i = 0; i < chain.size(); ++i) {
        const int c = chain[i];
        const int dir = i + 1 < chain.size() ? chain[i + 1] - c : 0;
        if (dir != lastDir || i + 1 == chain.size()) p.points.push_back({(c % w) * 8.0f + half, (c / w) * 8.0f + half});
        lastDir = dir;
    }
    return p;
}

}  // namespace

int pathUnitCells(PathUnitSize size) { return size == PathUnitSize::Small ? 2 : size == PathUnitSize::Large ? 5 : 4; }

const char* pathUnitSizeName(PathUnitSize size) {
    return size == PathUnitSize::Small ? "small unit" : size == PathUnitSize::Large ? "large unit" : "medium unit";
}

bool isStaticObstacle(const UnitType& type, uint16_t typeId) {
    if (typeId == kStartLocation) return false;
    if (type.is(UnitFlag::Flyer) && !type.is(UnitFlag::Building)) return false;
    return type.is(UnitFlag::Building) || type.is(UnitFlag::ResourceContainer);
}

bool isResource(const UnitType& type) { return type.editorGroup == "Resources"; }
bool isGasResource(const UnitType& t) { return t.name.find("Geyser") != std::string::npos || t.name.find("Vespene") != std::string::npos; }

WalkGrid buildWalkGrid(const PathingInput& in) {
    WalkGrid g;
    const TerrainLayer& ter = *in.terrain;
    const Tileset& ts = *in.tileset;
    g.width = ter.width * 4, g.height = ter.height * 4;
    g.cells.assign(size_t(g.width) * g.height, 0);
    for (int ty = 0; ty < ter.height; ++ty)
        for (int tx = 0; tx < ter.width; ++tx) {
            const int mega = ts.megatileFor(ter.tiles[size_t(ty) * ter.width + tx]);
            if (mega < 0 || size_t(mega) >= ts.minitileFlags.size()) continue;  // no megatile: not walkable
            const auto& flags = ts.minitileFlags[size_t(mega)];
            for (int my = 0; my < 4; ++my) {
                uint8_t* row = &g.cells[size_t(ty * 4 + my) * g.width + tx * 4];
                for (int mx = 0; mx < 4; ++mx) {
                    const uint16_t f = flags[size_t(my * 4 + mx)];
                    const int level = (f & 4) ? 2 : (f & 2) ? 1 : 0;
                    row[mx] = uint8_t((f & Tileset::kWalkable ? WalkGrid::kTerrainWalkable : 0) | (level << 2));
                }
            }
        }
    // Obstacles: the cells whose middle is in a collision box.
    for (const Obstacle& o : obstacles(in)) {
        const Box b = collisionBox((*in.types)[o.type], o.x, o.y);
        // Cell x has its middle at 8x + 4: inside when left <= 8x + 4 < right.
        const int x0 = std::max(0, (b.left - 4 + 7) / 8), x1 = std::min(g.width - 1, (b.right - 5) / 8);
        const int y0 = std::max(0, (b.top - 4 + 7) / 8), y1 = std::min(g.height - 1, (b.bottom - 5) / 8);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) g.cells[size_t(y) * g.width + x] |= WalkGrid::kBlocked;
    }
    return g;
}

GroundRegions findRegions(const WalkGrid& g) {
    GroundRegions r;
    const int n = g.width * g.height;
    r.id.assign(size_t(n), -1);
    std::vector<int> stack;
    for (int start = 0; start < n; ++start) {
        if (r.id[size_t(start)] != -1 || !g.walkable(start % g.width, start / g.width)) continue;
        const int id = int(r.size.size());
        int count = 0;
        r.id[size_t(start)] = id;
        stack.push_back(start);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            ++count;
            const int x = c % g.width, y = c / g.width;
            const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
            for (auto [nx, ny] : nb) {
                if (!g.walkable(nx, ny)) continue;
                const int m = ny * g.width + nx;
                if (r.id[size_t(m)] == -1) r.id[size_t(m)] = id, stack.push_back(m);
            }
        }
        r.size.push_back(count);
    }
    // Largest first.
    std::vector<int> order(r.size.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return r.size[size_t(a)] > r.size[size_t(b)]; });
    std::vector<int> rank(order.size());
    std::vector<int> sizes(order.size());
    for (size_t i = 0; i < order.size(); ++i) rank[size_t(order[i])] = int(i), sizes[i] = r.size[size_t(order[i])];
    for (int32_t& v : r.id)
        if (v >= 0) v = rank[size_t(v)];
    r.size = std::move(sizes);
    return r;
}

std::vector<uint8_t> clearance(const WalkGrid& g) {
    std::vector<uint8_t> c(size_t(g.width) * g.height, 0);
    for (int y = g.height - 1; y >= 0; --y)
        for (int x = g.width - 1; x >= 0; --x) {
            if (!g.walkable(x, y)) continue;
            const int right = x + 1 < g.width ? c[size_t(y) * g.width + x + 1] : 0;
            const int down = y + 1 < g.height ? c[size_t(y + 1) * g.width + x] : 0;
            const int diag = x + 1 < g.width && y + 1 < g.height ? c[size_t(y + 1) * g.width + x + 1] : 0;
            c[size_t(y) * g.width + x] = uint8_t(std::min(15, 1 + std::min({right, down, diag})));
        }
    return c;
}

PathingAnalysis analyzePathing(const PathingInput& in, const PathingOptions& opt) {
    PathingAnalysis a;
    const auto t0 = Clock::now();
    if (!in.terrain || !in.tileset || !in.types || !in.units || in.terrain->width == 0) return a;
    const UnitTypes& types = *in.types;
    const int mapW = in.terrain->width, mapH = in.terrain->height;
    const std::vector<Unit>& units = *in.units;

    auto t = Clock::now();
    a.grid = buildWalkGrid(in);
    a.gridMs = msSince(t);
    t = Clock::now();
    a.regions = findRegions(a.grid);
    a.regionsMs = msSince(t);
    t = Clock::now();
    const std::vector<uint8_t> clear = clearance(a.grid);
    a.unitCells = pathUnitCells(opt.unitSize);
    const Standable standable = standableAreas(a.grid, clear, a.unitCells);
    a.clearanceMs = msSince(t);

    // --- Bases ---
    t = Clock::now();
    const std::vector<Obstacle> obs = obstacles(in);
    const std::vector<uint8_t> freeTiles = freeBuildableTiles(in, obs);
    for (const Unit& u : units)
        if (u.type == kStartLocation && !(in.mapAreas && u.owner > 7)) {  // (a mod's map: the position of an area, below)
            Base b;
            b.start = true, b.owner = u.owner, b.x = u.x, b.y = u.y;
            a.bases.push_back(b);
        }
    std::stable_sort(a.bases.begin(), a.bases.end(), [](const Base& l, const Base& r) { return l.owner < r.owner; });
    const size_t starts = a.bases.size();

    // --- Resource areas ---
    // The game's rule for the areas that a computer player makes its towns at. Each resource (a unit with the resource
    // flag) goes to one area, and each area has a number.
    //   - Areas 1 and up: the start locations of players 1 to 8, in the players' order. A resource goes to the first of
    //     them that is near: at most 640 px away (a geyser: 896 px), on the same ground level, and a walk of at most 20
    //     tiles (a geyser: 28) on the ground.
    //   - Then the other geysers, then the other mineral fields, make areas: each of them counts the resources with no
    //     area near it (by their amounts); the one with the most makes an area round the mineral field nearest the
    //     middle of those resources, and the area takes all the resources with no area near that field. The area's
    //     position is the middle of the box round its resources.
    //   - A mod's map (in.mapAreas) then sets areas itself. A resource of Player 12 with the Burrowed state has its
    //     area's number in its hit points (0: no area; a number of 249 or more does nothing). There are as many
    //     areas as the largest of these numbers (at most 249): the ones that the rule above did not make have no
    //     resources yet. A start location of player 9 and up gives its position to the area with the player's number
    //     (one that no start location of players 1 to 8 made). Each such resource then goes to its area, and the
    //     last areas that have no resources (and no start location) are not areas. All this only when the rule above
    //     made an area.
    // The walk on the ground is by this library's walk grid (the terrain only), not by the game's path regions: a
    // resource at the limit of the walk can go to another area than in the game. The order of the units is the map's.
    const int gw = a.grid.width, gh = a.grid.height, unitCount = int(units.size());
    constexpr int kAreaLimit = 249, kNear = 960, kNoWalk = 9999;
    // The game's distance between two points (pixels): an estimate with no square root.
    auto gameDistance = [](int x0, int y0, int x1, int y1) {
        uint32_t dx = uint32_t(std::abs(x0 - x1)), dy = uint32_t(std::abs(y0 - y1));
        if (dx < dy) std::swap(dx, dy);
        return (dx >> 2) < dy ? dx - (dx >> 4) + ((3 * dy) >> 3) - (dx >> 6) + ((3 * dy) >> 8) : dx;
    };
    auto levelAt = [&](int px, int py) { return a.grid.elevation(std::clamp(px / 8, 0, gw - 1), std::clamp(py / 8, 0, gh - 1)); };
    auto terrainWalkable = [&](int cx, int cy) { return a.grid.inside(cx, cy) && (a.grid.cells[size_t(cy) * gw + cx] & WalkGrid::kTerrainWalkable) != 0; };
    // The walkable cell of a point: its own, or the nearest one at most 4 cells away; -1 if none.
    auto walkCell = [&](int px, int py) {
        const int cx = std::clamp(px / 8, 0, gw - 1), cy = std::clamp(py / 8, 0, gh - 1);
        if (terrainWalkable(cx, cy)) return cy * gw + cx;
        for (int r = 1; r <= 4; ++r)
            for (int y = cy - r; y <= cy + r; ++y)
                for (int x = cx - r; x <= cx + r; ++x)
                    if (std::max(std::abs(x - cx), std::abs(y - cy)) == r && terrainWalkable(x, y)) return y * gw + x;
        return -1;
    };
    // The walks from one cell to the cells round it (tenths of a pixel; kFar: none of at most 29 tiles). The last 192
    // start cells are kept: a resource is the start of some walks at different times.
    constexpr int kSpan = 29 * 4, kSide = 2 * kSpan + 1, kReach = 29 * 320;
    constexpr uint16_t kFar = 0xFFFF;
    struct WalkField {
        int cell = -1, x0 = 0, y0 = 0;
        uint64_t used = 0;
        std::vector<uint16_t> d;
    };
    std::deque<WalkField> fields;
    uint64_t fieldClock = 0;
    std::vector<std::vector<int>> open(size_t(kReach) + 1);  // the cells to go on from, by their walk
    auto fieldFrom = [&](int cell) -> const WalkField& {
        for (WalkField& c : fields)
            if (c.cell == cell) {
                c.used = ++fieldClock;
                return c;
            }
        if (fields.size() < 192) fields.emplace_back();
        WalkField& f = fields.size() < 192 ? fields.back() : *std::min_element(fields.begin(), fields.end(), [](const WalkField& l, const WalkField& r) { return l.used < r.used; });
        f.cell = cell, f.used = ++fieldClock, f.x0 = cell % gw - kSpan, f.y0 = cell / gw - kSpan;
        f.d.assign(size_t(kSide) * kSide, kFar);
        const int first = kSpan * kSide + kSpan;
        f.d[size_t(first)] = 0;
        open[0].push_back(first);
        for (int d = 0; d <= kReach; ++d) {
            for (size_t k = 0; k < open[size_t(d)].size(); ++k) {
                const int c = open[size_t(d)][k];
                if (d != f.d[size_t(c)]) continue;
                const int lx = c % kSide, ly = c / kSide;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = lx + dx, ny = ly + dy;
                        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= kSide || ny >= kSide) continue;
                        const int gx = f.x0 + nx, gy = f.y0 + ny;
                        if (!terrainWalkable(gx, gy)) continue;
                        if (dx && dy && (!terrainWalkable(gx - dx, gy) || !terrainWalkable(gx, gy - dy))) continue;  // (no cut of a corner)
                        const int nd = d + (dx && dy ? 113 : 80);
                        const size_t n = size_t(ny) * kSide + nx;
                        if (nd > kReach || nd >= f.d[n]) continue;
                        f.d[n] = uint16_t(nd);
                        open[size_t(nd)].push_back(int(n));
                    }
            }
            open[size_t(d)].clear();
        }
        return f;
    };
    // The walk between two points in tiles: the game's distance when nothing is in the way, else the walk's length;
    // kNoWalk when there is none.
    auto walkTiles = [&](int sx, int sy, int tx, int ty) {
        const int from = walkCell(sx, sy), to = walkCell(tx, ty);
        if (from < 0 || to < 0) return kNoWalk;
        const WalkField& f = fieldFrom(from);
        const int lx = to % gw - f.x0, ly = to / gw - f.y0;
        if (lx < 0 || ly < 0 || lx >= kSide || ly >= kSide) return kNoWalk;
        const int d = f.d[size_t(ly) * kSide + lx];
        if (d == kFar) return kNoWalk;
        const int dx = std::abs(lx - kSpan), dy = std::abs(ly - kSpan);
        if (d <= 80 * std::max(dx, dy) + 33 * std::min(dx, dy)) return int(gameDistance(sx, sy, tx, ty) / 32);
        return d / 320;
    };

    // What each unit is: no resource, a mineral field or a geyser with no area, or a resource that has its area.
    enum : uint8_t { kNoResource, kMineral, kGeyser, kTaken };
    std::vector<uint8_t> kind(units.size(), kNoResource), gas(units.size(), 0);
    std::vector<int> areaOf(units.size(), 0);
    for (int i = 0; i < unitCount; ++i) {
        const Unit& u = units[size_t(i)];
        if (u.type >= types.size() || !types[u.type].is(UnitFlag::ResourceContainer)) continue;
        gas[size_t(i)] = !isResource(types[u.type]) || isGasResource(types[u.type]);  // (a building on a geyser is gas too)
        kind[size_t(i)] = gas[size_t(i)] ? kGeyser : kMineral;
    }
    struct Area {
        int x = 0, y = 0;
        int base = -1;  // the start location's base, -1 for an area that the resources made
        std::vector<int> resources;
        bool placed = false;  // a mod's map: a start location of player 9 and up gave the position
    };
    std::vector<Area> areas(1);  // (the game does not use area 0)
    for (size_t s = 0; s < starts; ++s) {
        const Base& b = a.bases[s];
        if (b.owner < 0 || b.owner > 7 || (b.x == 0 && b.y == 0)) continue;
        if (s + 1 < starts && a.bases[s + 1].owner == b.owner) continue;  // (a player's last start location is its place)
        areas.push_back({int(b.x), int(b.y), int(s), {}});
    }
    auto take = [&](int i, int id) {
        kind[size_t(i)] = kTaken, areaOf[size_t(i)] = id;
        areas[size_t(id)].resources.push_back(i);
    };
    bool looseKind[3] = {};
    for (int i = 0; i < unitCount; ++i) {
        if (kind[size_t(i)] == kNoResource) continue;
        const Unit& u = units[size_t(i)];
        const bool g = gas[size_t(i)] != 0;
        int found = 0;
        for (size_t id = 1; id < areas.size() && !found; ++id) {
            const Area& ar = areas[id];
            if (gameDistance(u.x, u.y, ar.x, ar.y) > uint32_t(g ? 896 : 640) || levelAt(u.x, u.y) != levelAt(ar.x, ar.y)) continue;
            if (walkTiles(ar.x, ar.y, u.x, u.y) <= (g ? 28 : 20)) found = int(id);
        }
        if (found) take(i, found);
        else looseKind[kind[size_t(i)]] = true;
    }
    // A resource with no area is near a point: in the box of 960 px round it, on its ground level, at most 640 px
    // (a geyser: 896 px) away and a walk of less than 20 tiles (a geyser: 28).
    auto nearPoint = [&](int i, int px, int py) {
        if (kind[size_t(i)] == kNoResource || areaOf[size_t(i)]) return false;
        const Unit& u = units[size_t(i)];
        if (u.x < px - kNear || u.x > px + kNear || u.y < py - kNear || u.y > py + kNear) return false;
        const bool g = gas[size_t(i)] != 0;
        if (levelAt(u.x, u.y) != levelAt(px, py) || gameDistance(u.x, u.y, px, py) > uint32_t(g ? 896 : 640)) return false;
        return walkTiles(px, py, u.x, u.y) < (g ? 28 : 20);
    };
    auto makeAreas = [&](uint8_t type) {
        struct Seed {
            int unit;
            uint32_t value;
        };
        std::vector<Seed> seeds;
        for (int i = 0; i < unitCount; ++i) {
            if (kind[size_t(i)] != type) continue;
            Seed s{i, 1};
            for (int j = 0; j < unitCount; ++j)
                if (nearPoint(j, units[size_t(i)].x, units[size_t(i)].y)) s.value += std::min<uint32_t>(4, units[size_t(j)].resources / 500 + 1);
            seeds.push_back(s);
        }
        for (size_t n = 0; n < seeds.size(); ++n) {
            Seed* seed = &seeds[0];
            for (size_t k = 1; k < seeds.size(); ++k)
                if (seeds[k].value > seed->value) seed = &seeds[k];
            seed->value = 0;
            const Unit& su = units[size_t(seed->unit)];
            int64_t sumX = 0, sumY = 0;
            int count = 0;
            for (int j = 0; j < unitCount; ++j)
                if (nearPoint(j, su.x, su.y)) sumX += units[size_t(j)].x, sumY += units[size_t(j)].y, ++count;
            if (!count) continue;
            if (int(areas.size()) - 1 >= kAreaLimit) break;
            // The mineral field with no area nearest the middle (a walk of at most 20 tiles; of two as near, the later).
            const int mx = int(sumX / count), my = int(sumY / count);
            int nearest = seed->unit, best = 99999999;
            for (int j = 0; j < unitCount; ++j) {
                if (kind[size_t(j)] != kMineral) continue;
                // (a walk of 20 tiles does not go out of the box of 960 px)
                if (std::abs(units[size_t(j)].x - mx) > kNear || std::abs(units[size_t(j)].y - my) > kNear) continue;
                const int d = walkTiles(mx, my, units[size_t(j)].x, units[size_t(j)].y);
                if (d <= best && d <= 20) best = d, nearest = j;
            }
            seed->unit = nearest;
            const Unit& nu = units[size_t(nearest)];
            const int id = int(areas.size());
            areas.push_back({nu.x, nu.y, -1, {}});
            int l = nu.x, t = nu.y, r = nu.x, b = nu.y;
            for (int j = 0; j < unitCount; ++j) {
                if ((kind[size_t(j)] != kMineral && kind[size_t(j)] != kGeyser) || !nearPoint(j, nu.x, nu.y)) continue;
                const Unit& u = units[size_t(j)];
                take(j, id);
                l = std::min(l, int(u.x)), r = std::max(r, int(u.x)), t = std::min(t, int(u.y)), b = std::max(b, int(u.y));
            }
            areas[size_t(id)].x = (l + r) / 2, areas[size_t(id)].y = (t + b) / 2;
        }
    };
    if (looseKind[kGeyser]) makeAreas(kGeyser);
    if (looseKind[kMineral]) makeAreas(kMineral);
    if (in.mapAreas && areas.size() > 1) {
        const size_t made = areas.size() - 1;
        size_t prefix = 0;  // the areas of the start locations of players 1 to 8
        while (prefix < made && areas[prefix + 1].base >= 0) ++prefix;
        // Each resource's number from the map (-1: none), and the largest of them.
        std::vector<int> mapArea(units.size(), -1);
        int largest = -1;
        for (int i = 0; i < unitCount; ++i) {
            const Unit& u = units[size_t(i)];
            if (kind[size_t(i)] == kNoResource || u.owner != 11 || !(u.state & u.validState & UnitState::Burrowed)) continue;
            mapArea[size_t(i)] = u.hitPoints, largest = std::max(largest, int(u.hitPoints));
        }
        if (largest > int(made)) areas.resize(size_t(std::min(largest, kAreaLimit)) + 1);
        for (const Unit& u : units)
            if (u.type == kStartLocation && u.owner > 7 && (u.x || u.y) && size_t(u.owner) >= prefix && size_t(u.owner) + 1 < areas.size())
                areas[size_t(u.owner) + 1].x = u.x, areas[size_t(u.owner) + 1].y = u.y, areas[size_t(u.owner) + 1].placed = true;
        if (largest >= 0) {
            for (int i = 0; i < unitCount; ++i) {
                const int to = mapArea[size_t(i)], from = areaOf[size_t(i)];
                if (to < 0 || to >= kAreaLimit || to == from || to >= int(areas.size())) continue;
                if (from) std::erase(areas[size_t(from)].resources, i);
                areaOf[size_t(i)] = to;
                if (to) areas[size_t(to)].resources.push_back(i);
            }
            while (areas.size() > 1 && areas.back().resources.empty() && areas.back().base < 0 && !areas.back().placed) areas.pop_back();
            // An area that only the numbers made has no position in the game. Here it shows at the middle of its
            // resources.
            for (size_t id = made + 1; id < areas.size(); ++id) {
                Area& ar = areas[id];
                if (ar.placed || ar.resources.empty()) continue;
                int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
                for (int i : ar.resources) {
                    const Unit& u = units[size_t(i)];
                    x0 = std::min(x0, int(u.x)), y0 = std::min(y0, int(u.y)), x1 = std::max(x1, int(u.x)), y1 = std::max(y1, int(u.y));
                }
                ar.x = (x0 + x1) / 2, ar.y = (y0 + y1) / 2;
            }
        }
    }

    auto addResource = [&](Base& b, int i) {
        const Unit& u = units[size_t(i)];
        b.resources.push_back(i);
        if (gas[size_t(i)]) ++b.geysers, b.gasAmount += u.resources;
        else ++b.minerals, b.mineralAmount += u.resources;
    };
    // The bases: a start location's base has its area's resources; each other area is a base (an expansion). Its town
    // hall goes where the game lets one go (buildable, 3 tiles from the resources) nearest to the resources.
    {
        // Boxes that a town hall must keep 3 tiles from: every resource (any amount).
        std::vector<Box> keepOut;
        for (const Unit& u : units)
            if (u.type < types.size() && types[u.type].is(UnitFlag::ResourceContainer) && isResource(types[u.type])) {
                Box b = placementBox(types[u.type], u.x, u.y);
                keepOut.push_back({b.left - 96, b.top - 96, b.right + 96, b.bottom + 96});
            }
        // The ground level of a tile: the level of most of its walkable minitiles (-1: none is walkable).
        auto tileLevel = [&](int tx, int ty) {
            int n[3] = {};
            for (int my = ty * 4; my < ty * 4 + 4; ++my)
                for (int mx = tx * 4; mx < tx * 4 + 4; ++mx)
                    if (a.grid.inside(mx, my) && (a.grid.cells[size_t(my) * a.grid.width + mx] & WalkGrid::kTerrainWalkable)) ++n[a.grid.elevation(mx, my)];
            if (!n[0] && !n[1] && !n[2]) return -1;
            return n[2] > n[1] && n[2] > n[0] ? 2 : n[1] > n[0] ? 1 : 0;
        };
        // The level of most of the resources of b (each by the tiles under its box).
        auto resourceLevel = [&](const Base& b) {
            int n[3] = {};
            for (int i : b.resources) {
                const Box box = placementBox(types[units[size_t(i)].type], units[size_t(i)].x, units[size_t(i)].y);
                int m[3] = {};
                for (int y = std::max(0, box.top / 32); y < std::min(mapH, (box.bottom + 31) / 32); ++y)
                    for (int x = std::max(0, box.left / 32); x < std::min(mapW, (box.right + 31) / 32); ++x)
                        if (const int l = tileLevel(x, y); l >= 0) ++m[l];
                if (m[0] || m[1] || m[2]) ++n[m[2] > m[1] && m[2] > m[0] ? 2 : m[1] > m[0] ? 1 : 0];
            }
            return n[2] > n[1] && n[2] > n[0] ? 2 : n[1] > n[0] ? 1 : 0;
        };
        // The town hall's place for the resources of b; false if there is none. The place is on the resources' level
        // when one is there (a town hall below or above a cliff does not mine them).
        auto findHall = [&](Base& b) {
            double cx = 0, cy = 0;
            for (int i : b.resources) cx += units[size_t(i)].x, cy += units[size_t(i)].y;
            cx /= double(b.resources.size()), cy /= double(b.resources.size());
            double bestScore = std::numeric_limits<double>::infinity();
            const int ctx = int(cx / 32), cty = int(cy / 32);
            const int level = resourceLevel(b);
            for (int pass = 0; pass < 2 && !std::isfinite(bestScore); ++pass)
            for (int ty = std::max(0, cty - 11); ty <= std::min(mapH - 3, cty + 9); ++ty)
                for (int tx = std::max(0, ctx - 12); tx <= std::min(mapW - 4, ctx + 9); ++tx) {
                    bool fits = true;
                    for (int y = ty; y < ty + 3 && fits; ++y)
                        for (int x = tx; x < tx + 4 && fits; ++x) fits = freeTiles[size_t(y) * mapW + x] != 0 && (pass || tileLevel(x, y) == level);
                    if (!fits) continue;
                    const Box hall{tx * 32, ty * 32, tx * 32 + 128, ty * 32 + 96};
                    for (const Box& k : keepOut)
                        if (hall.left < k.right && k.left < hall.right && hall.top < k.bottom && k.top < hall.bottom) {
                            fits = false;
                            break;
                        }
                    if (!fits) continue;
                    const double hx = tx * 32 + 64.0, hy = ty * 32 + 48.0;
                    double score = 0;
                    for (int i : b.resources) score += std::hypot(units[size_t(i)].x - hx, units[size_t(i)].y - hy);
                    if (score < bestScore) bestScore = score, b.x = float(hx), b.y = float(hy);
                }
            return std::isfinite(bestScore);
        };
        for (size_t id = 1; id < areas.size(); ++id) {
            const Area& ar = areas[id];
            if (ar.base >= 0) {
                Base& b = a.bases[size_t(ar.base)];
                b.area = int(id), b.areaX = float(ar.x), b.areaY = float(ar.y);
                for (int i : ar.resources) addResource(b, i);
                continue;
            }
            if (!opt.expansions) continue;
            if (ar.resources.empty() && !ar.x && !ar.y) continue;  // (a mod's map: an area with nothing to show)
            Base b;
            b.area = int(id), b.areaX = float(ar.x), b.areaY = float(ar.y);
            for (int i : ar.resources) addResource(b, i);
            b.hall = !b.resources.empty() && findHall(b);
            if (!b.hall) b.x = b.areaX, b.y = b.areaY;
            a.bases.push_back(std::move(b));
        }
    }
    // Town areas: the buildable tiles joined to the town hall's tiles through buildable tiles, within the radius.
    for (Base& b : a.bases) {
        const int bx = int(b.x) / 32, by = int(b.y) / 32;
        const int cx = std::clamp(int(b.x) / 8, 0, a.grid.width - 1), cy = std::clamp(int(b.y) / 8, 0, a.grid.height - 1);
        b.region = a.regions.id[size_t(cy) * a.grid.width + cx];
        if (!b.hall) continue;  // (no town hall: no town area)
        const double r2 = double(opt.townRadius) * opt.townRadius;
        auto within = [&](int x, int y) {
            const double dx = x + 0.5 - b.x / 32.0, dy = y + 0.5 - b.y / 32.0;
            return x >= 0 && y >= 0 && x < mapW && y < mapH && dx * dx + dy * dy <= r2;
        };
        std::vector<uint8_t> seen(size_t(mapW) * mapH, 0);
        std::vector<int> stack;
        // The seeds: the free buildable tiles of the town hall's box (4x3 tiles around its middle).
        for (int y = by - 2; y <= by + 1; ++y)
            for (int x = bx - 2; x <= bx + 2; ++x)
                if (within(x, y) && freeTiles[size_t(y) * mapW + x] && !seen[size_t(y) * mapW + x])
                    seen[size_t(y) * mapW + x] = 1, stack.push_back(y * mapW + x);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            b.buildable.push_back(c);
            const int x = c % mapW, y = c / mapW;
            const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
            for (auto [nx, ny] : nb) {
                if (!within(nx, ny)) continue;
                const size_t m = size_t(ny) * mapW + nx;
                if (!seen[m] && freeTiles[m]) seen[m] = 1, stack.push_back(int(m));
            }
        }
        std::sort(b.buildable.begin(), b.buildable.end());
    }
    a.basesMs = msSince(t);

    // --- Paths between the start locations ---
    t = Clock::now();
    std::vector<std::pair<int, int>> pairs;
    for (size_t i = 0; i < starts; ++i)
        for (size_t j = i + 1; j < starts; ++j) pairs.push_back({int(i), int(j)});
    a.paths.resize(pairs.size());
    auto solve = [&](size_t k) {
        const Base &p = a.bases[size_t(pairs[k].first)], &q = a.bases[size_t(pairs[k].second)];
        auto endCell = [&](const Base& b) {
            const int cx = int(std::floor(b.x / 8 - a.unitCells / 2.0f + 0.5f)), cy = int(std::floor(b.y / 8 - a.unitCells / 2.0f + 0.5f));
            return nearestStandable(a.grid, clear, a.unitCells, cx, cy, 24);
        };
        const int from = endCell(p), to = endCell(q);
        GroundPath path;
        // No search where the ends are in different areas (an island): no path.
        if (from >= 0 && to >= 0 && standable.area[size_t(from)] == standable.area[size_t(to)]) path = aStar(a.grid, clear, a.unitCells, from, to);
        path.from = pairs[k].first, path.to = pairs[k].second;
        a.paths[k] = std::move(path);
    };
    if (pairs.size() <= 1) {
        for (size_t k = 0; k < pairs.size(); ++k) solve(k);
    } else {
        // The pairs on a few threads (each takes the next pair).
        std::atomic<size_t> next{0};
        const size_t threads = std::min<size_t>(pairs.size(), std::max(2u, std::thread::hardware_concurrency()));
        std::vector<std::future<void>> jobs;
        for (size_t k = 0; k < threads; ++k)
            jobs.push_back(std::async(std::launch::async, [&] {
                for (size_t i; (i = next++) < pairs.size();) solve(i);
            }));
        for (auto& j : jobs) j.get();
    }
    a.pathsMs = msSince(t);
    a.totalMs = msSince(t0);
    return a;
}

}  // namespace stardraft
