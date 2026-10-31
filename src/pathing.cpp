#include "stardraft/pathing.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <queue>
#include <thread>

namespace stardraft {

namespace {

constexpr uint16_t kStartLocation = 214;
// A resource with less than this much in it is a wall of minerals, not a resource of a base (it is still an obstacle).
constexpr uint32_t kMinResource = 64;

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

GroundPath findGroundPath(const WalkGrid& g, const std::vector<uint8_t>& clear, int cells, float x0, float y0, float x1, float y1) {
    auto cellOf = [&](float px, float py, int& cx, int& cy) {  // the top-left cell of a unit with its middle at (px, py)
        cx = int(std::floor(px / 8 - cells / 2.0f + 0.5f)), cy = int(std::floor(py / 8 - cells / 2.0f + 0.5f));
    };
    int ax, ay, bx, by;
    cellOf(x0, y0, ax, ay), cellOf(x1, y1, bx, by);
    const int from = nearestStandable(g, clear, cells, ax, ay, 24), to = nearestStandable(g, clear, cells, bx, by, 24);
    if (from < 0 || to < 0) return {};
    return aStar(g, clear, cells, from, to);
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
        if (u.type == kStartLocation) {
            Base b;
            b.start = true, b.owner = u.owner, b.x = u.x, b.y = u.y;
            a.bases.push_back(b);
        }
    std::stable_sort(a.bases.begin(), a.bases.end(), [](const Base& l, const Base& r) { return l.owner < r.owner; });
    const size_t starts = a.bases.size();

    auto countsAsResource = [&](int i) {
        const Unit& u = units[size_t(i)];
        return u.type < types.size() && isResource(types[u.type]) && u.resources >= kMinResource;
    };
    auto addResource = [&](Base& b, int i) {
        const Unit& u = units[size_t(i)];
        b.resources.push_back(i);
        if (isGasResource(types[u.type])) ++b.geysers, b.gasAmount += u.resources;
        else ++b.minerals, b.mineralAmount += u.resources;
    };
    // Each resource near a start location goes to the nearest one.
    std::vector<int> loose;
    for (int i = 0; i < int(units.size()); ++i) {
        if (!countsAsResource(i)) continue;
        const Unit& u = units[size_t(i)];
        int best = -1;
        double bestD = double(opt.resourceRadius) * 32;
        for (size_t s = 0; s < starts; ++s) {
            const double d = std::hypot(u.x - a.bases[s].x, u.y - a.bases[s].y);
            if (d <= bestD) bestD = d, best = int(s);
        }
        if (best >= 0) addResource(a.bases[size_t(best)], i);
        else loose.push_back(i);
    }
    // Expansions: groups of the other resources (each within 7 tiles of another of its group) with at least 3 mineral
    // fields or a geyser. The town hall goes where the game lets one go (buildable, 3 tiles from the resources) nearest
    // to the resources.
    if (opt.expansions && !loose.empty()) {
        std::vector<int> group(loose.size(), -1);
        int groups = 0;
        for (size_t s = 0; s < loose.size(); ++s) {
            if (group[s] >= 0) continue;
            std::vector<size_t> stack{s};
            group[s] = groups;
            while (!stack.empty()) {
                const size_t c = stack.back();
                stack.pop_back();
                const Unit& cu = units[size_t(loose[c])];
                for (size_t o = 0; o < loose.size(); ++o) {
                    if (group[o] >= 0) continue;
                    const Unit& ou = units[size_t(loose[o])];
                    if (std::hypot(cu.x - ou.x, cu.y - ou.y) <= 7 * 32) group[o] = groups, stack.push_back(o);
                }
            }
            ++groups;
        }
        // Boxes that a town hall must keep 3 tiles from: every resource (any amount).
        std::vector<Box> keepOut;
        for (const Unit& u : units)
            if (u.type < types.size() && types[u.type].is(UnitFlag::ResourceContainer) && isResource(types[u.type])) {
                Box b = placementBox(types[u.type], u.x, u.y);
                keepOut.push_back({b.left - 96, b.top - 96, b.right + 96, b.bottom + 96});
            }
        // The town hall's place for the resources of b; false if there is none.
        auto findHall = [&](Base& b) {
            double cx = 0, cy = 0;
            for (int i : b.resources) cx += units[size_t(i)].x, cy += units[size_t(i)].y;
            cx /= double(b.resources.size()), cy /= double(b.resources.size());
            double bestScore = std::numeric_limits<double>::infinity();
            const int ctx = int(cx / 32), cty = int(cy / 32);
            for (int ty = std::max(0, cty - 11); ty <= std::min(mapH - 3, cty + 9); ++ty)
                for (int tx = std::max(0, ctx - 12); tx <= std::min(mapW - 4, ctx + 9); ++tx) {
                    bool fits = true;
                    for (int y = ty; y < ty + 3 && fits; ++y)
                        for (int x = tx; x < tx + 4 && fits; ++x) fits = freeTiles[size_t(y) * mapW + x] != 0;
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
        std::vector<Base> found;
        std::vector<bool> hasHall;
        for (int gi = 0; gi < groups; ++gi) {
            Base b;
            for (size_t s = 0; s < loose.size(); ++s)
                if (group[s] == gi) addResource(b, loose[s]);
            hasHall.push_back(findHall(b));
            found.push_back(std::move(b));
        }
        // A group with no mineral line (a geyser by itself) and a group with one: one base when they are at most 12 tiles
        // apart. Groups whose town halls would be near each other are one base too.
        auto gap = [&](const Base& p, const Base& q) {
            double d = std::numeric_limits<double>::infinity();
            for (int i : p.resources)
                for (int j : q.resources) d = std::min(d, std::hypot(double(units[size_t(i)].x) - units[size_t(j)].x, double(units[size_t(i)].y) - units[size_t(j)].y));
            return d;
        };
        for (bool merged = true; merged;) {
            merged = false;
            for (size_t i = 0; i < found.size() && !merged; ++i)
                for (size_t j = i + 1; j < found.size() && !merged; ++j) {
                    const bool lone = (found[i].minerals < 3) != (found[j].minerals < 3) && gap(found[i], found[j]) <= 12 * 32;
                    const bool nearHalls = hasHall[i] && hasHall[j] && std::hypot(found[i].x - found[j].x, found[i].y - found[j].y) <= 8 * 32;
                    if (!lone && !nearHalls) continue;
                    for (int r : found[j].resources) addResource(found[i], r);
                    found.erase(found.begin() + std::ptrdiff_t(j)), hasHall.erase(hasHall.begin() + std::ptrdiff_t(j));
                    hasHall[i] = findHall(found[i]);
                    merged = true;
                }
        }
        for (size_t i = 0; i < found.size(); ++i)
            if (hasHall[i] && (found[i].minerals >= 3 || found[i].geysers > 0)) a.bases.push_back(std::move(found[i]));
    }
    // Town areas: the buildable tiles joined to the town hall's tiles through buildable tiles, within the radius.
    for (Base& b : a.bases) {
        const int bx = int(b.x) / 32, by = int(b.y) / 32;
        const int cx = std::clamp(int(b.x) / 8, 0, a.grid.width - 1), cy = std::clamp(int(b.y) / 8, 0, a.grid.height - 1);
        b.region = a.regions.id[size_t(cy) * a.grid.width + cx];
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
