// The simulation's core: movement, animation (iscript), placement, the images it draws, the frame loop, and the
// Simulator that the editor and the command line use.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <queue>
#include <tuple>
#include <string>
#include <utility>
#include <vector>

#include "world.h"

namespace sim {

// The long path searches of one frame; more wait for later frames (a fight on a big map stays smooth).
const int kSearchBudget = 6;
// The most minitiles one search expands (a goal it cannot reach: the search does not cover a whole big map at once).
const int kGridCap = 6000;
const int kExpandBudget = 10000;  // (and the most of all the searches of a frame together)

namespace {

struct DirTable {
    int x[256], y[256];
    DirTable() {
        for (int d = 0; d < 256; ++d) {
            double a = d * 3.14159265358979323846 / 128.0;
            x[d] = int(std::lround(std::sin(a) * 256));
            y[d] = int(std::lround(-std::cos(a) * 256));
        }
    }
};
const DirTable& dirs() {
    static const DirTable table;
    return table;
}

}  // namespace

int turnToward(int from, int to, int rate) {
    int diff = int(int8_t(uint8_t(to - from)));
    if (std::abs(diff) <= rate) return to & 255;
    return (from + (diff > 0 ? rate : -rate)) & 255;
}

int dirX(int dir) { return dirs().x[dir & 255]; }
int dirY(int dir) { return dirs().y[dir & 255]; }

int direction(int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    double a = std::atan2(double(dx), double(-dy));
    return int(std::lround(a * 128.0 / 3.14159265358979323846)) & 255;
}

int octagonalLength(int dx, int dy) {
    int a = std::abs(dx), b = std::abs(dy);
    if (a < b) std::swap(a, b);
    if (a / 4 < b) return a - a / 16 + b * 3 / 8 - a / 64 + b * 3 / 256;
    return a;
}

int World::random() {
    seed = seed * 22695477u + 1u;
    return int((seed >> 16) & 0x7fff);
}

int World::gap(const Unit& a, const Unit& b) const {
    const UnitData &da = t.units[a.type], &db = t.units[b.type];
    int al = a.x - da.left, ar = a.x + da.right + 1, au = a.y - da.up, ad = a.y + da.down + 1;
    int bl = b.x - db.left, br = b.x + db.right + 1, bu = b.y - db.up, bd = b.y + db.down + 1;
    int gx = std::max({0, al - br, bl - ar}), gy = std::max({0, au - bd, bu - ad});
    return octagonalLength(gx, gy);
}

// Whether a unit of a type at (x, y) and another unit overlap (inner boxes, touching counts).
// (footprint: a building's placement box, its footprint for movement)
bool World::overlaps(int type, int x, int y, const Unit& o, bool footprint) const {
    const UnitData &d = t.units[size_t(type)], &od = t.units[size_t(o.type)];
    int ol = od.left, orr = od.right, ou = od.up, odn = od.down;
    if (footprint && o.building && od.placeW > 0) {
        ol = od.placeW / 2;
        orr = od.placeW - ol - 1;
        ou = od.placeH / 2;
        odn = od.placeH - ou - 1;
    }
    return x - d.left <= o.x + orr && o.x - ol <= x + d.right && y - d.up <= o.y + odn && o.y - ou <= y + d.down;
}

// The gap from a point to a unit's box.
int World::pointGap(const Unit& v, int x, int y) const {
    const UnitData& d = t.units[size_t(v.type)];
    return octagonalLength(std::max({0, (v.x - d.left) - x, x - (v.x + d.right + 1)}),
                           std::max({0, (v.y - d.up) - y, y - (v.y + d.down + 1)}));
}

int World::add(int type, int owner, int x, int y) {
    Unit u;
    const UnitData& d = t.units[type];
    u.type = type;
    u.id = int(units.size());
    u.owner = owner;
    u.x = u.moveX = u.aimX = x;
    u.y = u.moveY = u.aimY = y;
    u.ex = x << 8;
    u.ey = y << 8;
    u.hp = d.hp;
    u.shields = d.hasShields ? d.shields << 8 : 0;
    u.building = (d.flags & (kBuilding | kResource)) != 0;
    u.anim.header = t.scriptHeader(t.flingyImage(d.flingy));
    startAnim(u.anim, kAnimInit);
    if (d.subunit != kNoUnit) {
        u.turret.header = t.scriptHeader(t.flingyImage(t.units[size_t(d.subunit)].flingy));
        startAnim(u.turret, kAnimInit);
    }
    if (role(type).launchesScarabs) u.scarabs = role(type).capacity;  // (a Reaver: its Scarabs)
    if (d.flags & kSpellcaster) u.energy = t.rules.startEnergy << 8;  // (spellcasters such as the Medic)
    units.push_back(u);
    return int(units.size()) - 1;
}

// The nearest free spot: the origin when it is free, else rings that grow by 16 pixels, each
// scanned along its bottom row (east), right column (north), top row (west) and left column (south) in 8-pixel steps,
// jumping past the units in the way. No spot within 128 pixels: x and y stay as they are.
bool World::freeSpotWithin(int type, int& x, int& y, const Unit* self, int limit) const {
    const UnitData& d = t.units[type];
    bool flyer = (d.flags & kFlyer) != 0;
    int mapR = map.width * 32 - 1, mapB = map.height * 32 - 1;
    int bl = std::max(0, x - limit), bt = std::max(0, y - limit);
    int br = std::min(mapR, x + limit - 1), bb = std::min(mapB, y + limit - 1);
    auto blockerAt = [&](int px, int py) -> const Unit* {
        for (const Unit& o : units) {
            if (&o == self || !o.alive || o.hidden || o.burrowed || ((t.units[o.type].flags & kFlyer) != 0) != flyer)
                continue;
            if (overlaps(type, px, py, o))
                return &o;
        }
        return nullptr;
    };
    auto inside = [&](int px, int py) {
        return px - d.left >= 0 && py - d.up >= 0 && px + d.right <= mapR && py + d.down <= mapB;
    };
    auto fits = [&](int px, int py) {
        return flyer || map.boxWalkable(px - d.left, py - d.up, px + d.right, py + d.down);
    };
    const Unit* first = blockerAt(x, y);
    if (!first && inside(x, y) && fits(x, y)) return true;
    int w = 8, h = 8;
    if (first) {
        const UnitData& fd = t.units[first->type];
        w = std::max(8, d.left + fd.right + 2);
        h = std::max(8, d.up + fd.down + 2);
    }
    auto floor8 = [](int v) { return v & ~7; };
    auto ceil8 = [](int v) { return (v + 7) & ~7; };
    // (the re-alignment after a jump: added going on, taken off going back)
    auto align = [](int v) { return (8 - (v & 7)) & 7; };
    for (;;) {
        int nx = x - w, ny = y - h;
        if (nx < bl && ny < bt && x + w > br && y + h > bb) return false;
        int x0 = std::max(floor8(nx), bl), y0 = std::max(floor8(ny), bt);
        int x1 = std::min(ceil8(nx + 2 * w), br), y1 = std::min(ceil8(ny + 2 * h), bb);
        int dl = d.left + d.right + 1;
        // One pass along an edge: a fixed coordinate, the other from `from` toward `to` (step +8 or -8).
        auto pass = [&](bool alongX, int fixed, int from, int to, int dir) -> bool {
            for (int v = from; dir > 0 ? v <= to : v >= to;) {
                int px = alongX ? v : fixed, py = alongX ? fixed : v;
                if (!inside(px, py)) {
                    v += 8 * dir;
                    continue;
                }
                if (const Unit* o = blockerAt(px, py)) {
                    const UnitData& od = t.units[o->type];
                    int jump = alongX ? (dir > 0 ? o->x + od.right + 1 - (px - d.left)
                                                 : (px + d.right) - (o->x - od.left - 1))
                                      : (dir > 0 ? o->y + od.down + 1 - (py - d.up)
                                                 : (py + d.down) - (o->y - od.up - 1));
                    int nv = v + dir * jump;
                    v = nv + dir * align(nv);
                    continue;
                }
                if (fits(px, py)) {
                    x = px;
                    y = py;
                    return true;
                }
                v += 8 * dir;
            }
            return false;
        };
        if (pass(true, y1, 2 * w > 2 * dl ? x0 + ceil8(dl) : x0, x1, 1) || pass(false, x1, y1, y0, -1) ||
            pass(true, y0, x1, x0, -1) || pass(false, x0, y0, y1, 1))
            return true;
        w += 16;
        h += 16;
    }
}

void World::freeSpot(int type, int& x, int& y, const Unit* self) const {
    int ox = x, oy = y;
    if (freeSpotWithin(type, x, y, self, 128)) return;
    x = ox;
    y = oy;
    if (!freeSpotWithin(type, x, y, self, 512)) {
        x = ox;
        y = oy;
    }
}

bool World::fits(int type, int x, int y) const {
    const UnitData& d = t.units[size_t(type)];
    return map.boxWalkable(x - d.left, y - d.up, x + d.right, y + d.down);
}

// Whether a unit's box stays on walkable minitiles along a straight walk (every 4 pixels).
bool World::lineClear(int type, int x0, int y0, int x1, int y1) const {
    int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) / 4 + 1;
    for (int i = 0; i <= steps; ++i)
        if (!fits(type, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)) return false;
    return true;
}

// A walk over the terrain from one place to another: A* over the 8-pixel minitiles, each with the place in it nearest
// its centre where the unit's box fits (so a corridor just as wide as the box works, also round a bend), steps
// between neighbours where the box fits all the way; then only the waypoints that a straight walk cannot skip. A goal
// it cannot reach: the reachable place nearest to it (false).
bool World::gridPath(int type, int fx, int fy, int x, int y, std::vector<std::pair<int, int>>& out) const {
    int w = map.width * 4, h = map.height * 4;
    auto cell = [&](int px, int py) { return std::clamp(py / 8, 0, h - 1) * w + std::clamp(px / 8, 0, w - 1); };
    int start = cell(fx, fy), goal = cell(x, y);
    Search& b = search;
    size_t cells = size_t(w) * h;
    if (b.cells != cells) {
        b.cells = cells;
        b.cost.reset(new int[cells]);
        b.from.reset(new int[cells]);
        b.at.reset(new Search::Place[cells]);
        b.via.reset(new Search::Place[cells]);
        b.stamp.reset(new uint32_t[cells]());
        b.fit.reset(new uint8_t[cells]);
        b.tested.reset(new uint8_t[cells]);
        b.clear.reset(new uint8_t[cells]);
        b.now = 0;
    }
    ++b.now;
    auto touch = [&](int c) {  // (a cell first seen in this search)
        if (b.stamp[size_t(c)] == b.now) return;
        b.stamp[size_t(c)] = b.now;
        b.cost[size_t(c)] = b.from[size_t(c)] = -1;
        b.via[size_t(c)] = {-1, -1};
        b.fit[size_t(c)] = 2;  // (not known)
        b.tested[size_t(c)] = b.clear[size_t(c)] = 0;
    };
    int *cost = b.cost.get(), *from = b.from.get();
    Search::Place *at = b.at.get(), *via = b.via.get();  // (the place in it; a corner before it)
    static const std::pair<int, int>* order = [] {  // (the 64 places of a minitile, nearest its centre first)
        static std::pair<int, int> o[64];
        for (int i = 0; i < 64; ++i) o[i] = {i % 8, i / 8};
        std::sort(o, o + 64, [](auto a, auto b) {
            return (a.first - 4) * (a.first - 4) + (a.second - 4) * (a.second - 4) <
                   (b.first - 4) * (b.first - 4) + (b.second - 4) * (b.second - 4);
        });
        return o;
    }();
    auto ok = [&](int c) {
        touch(c);
        uint8_t* fit = b.fit.get();
        if (fit[size_t(c)] == 2) {
            fit[size_t(c)] = 0;
            for (int i = 0; i < 64 && !fit[size_t(c)] && map.walk[size_t(c)]; ++i) {
                int px = c % w * 8 + order[i].first, py = c / w * 8 + order[i].second;
                // (the start: its place when the unit can walk there, else where the unit stands)
                if (fits(type, px, py) && (c != start || lineClear(type, fx, fy, px, py))) {
                    fit[size_t(c)] = 1;
                    at[size_t(c)] = {px, py};
                }
            }
            if (c == start && !fit[size_t(c)]) {
                fit[size_t(c)] = 1;
                at[size_t(c)] = {fx, fy};
            }
        }
        return fit[size_t(c)] == 1;
    };
    auto estimate = [&](int c) {
        int dx = std::abs(c % w - goal % w), dy = std::abs(c / w - goal / w);
        return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
    };
    using Node = std::pair<int, int>;  // (cost + estimate, minitile)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    ok(start);  // (the start: where the unit stands)
    cost[size_t(start)] = 0;
    open.push({estimate(start), start});
    int best = start;
    b.capped = false;
    int expanded = 0;
    for (; !open.empty(); ++expanded) {
        if (expanded >= kGridCap) {  // (a cap: a goal it cannot reach, or far away; it goes on later)
            // (on from the place nearest the goal; when that is where it stands, at a wall across from the goal, on
            // from where the search would go on: the open place with the least cost and estimate)
            b.capped = true;
            if (best == start) best = open.top().second;
            break;
        }
        int c = open.top().second;
        open.pop();
        if (estimate(c) < estimate(best)) best = c;
        if (c == goal) break;
        int cx = c % w, cy = c / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = cx + dx, ny = cy + dy, n = ny * w + nx;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= w || ny >= h || !ok(n)) continue;
                int nc = cost[size_t(c)] + (dx && dy ? 14 : 10);
                if (cost[size_t(n)] >= 0 && cost[size_t(n)] <= nc) continue;
                // (straight, or round the corner of the two: a box that just fits a bend)
                const auto &pa = at[size_t(c)], &pb = at[size_t(n)];
                std::pair<int, int> corner = {-1, -1};
                bool stuckStart = c == start && !fits(type, fx, fy);  // (a box over unwalkable ground: it walks out)
                int dir = (dy + 1) * 3 + dx + 1;  // (0 to 8 without 4; the way back: 8 - dir)
                int bit = 1 << (dir > 4 ? dir - 1 : dir), back = 1 << (dir > 4 ? 8 - dir : 7 - dir);
                if (!(b.tested[size_t(c)] & bit)) {  // (each straight step is tested once, for both ways)
                    bool straight = lineClear(type, pa.first, pa.second, pb.first, pb.second);
                    b.tested[size_t(c)] |= uint8_t(bit);
                    b.tested[size_t(n)] |= uint8_t(back);
                    if (straight) {
                        b.clear[size_t(c)] |= uint8_t(bit);
                        b.clear[size_t(n)] |= uint8_t(back);
                    }
                }
                if (!stuckStart && !(b.clear[size_t(c)] & bit)) {
                    for (auto k : {std::make_pair(pb.first, pa.second), std::make_pair(pa.first, pb.second)})
                        if (corner.first < 0 && lineClear(type, pa.first, pa.second, k.first, k.second) &&
                            lineClear(type, k.first, k.second, pb.first, pb.second))
                            corner = k;
                    if (corner.first < 0) continue;
                }
                via[size_t(n)] = {corner.first, corner.second};
                cost[size_t(n)] = nc;
                from[size_t(n)] = c;
                open.push({nc + estimate(n), n});
            }
    }
    b.spent += expanded;
    bool reached = best == goal && fits(type, x, y);
    std::vector<std::pair<int, int>> points;
    auto add = [&](std::pair<int, int> p, int c) {
        points.push_back(p);
        if (via[size_t(c)].first >= 0) points.push_back(via[size_t(c)]);
    };
    if (reached) add({x, y}, goal);
    for (int c = reached ? from[size_t(goal)] : best; c >= 0 && c != start; c = from[size_t(c)]) add(at[size_t(c)], c);
    if (at[size_t(start)].first != fx || at[size_t(start)].second != fy) points.push_back(at[size_t(start)]);
    if (points.empty()) points.push_back({fx, fy});
    std::reverse(points.begin(), points.end());
    int px = fx, py = fy;
    for (size_t i = 0; i < points.size();) {  // (each look ahead at most 48 points)
        size_t last = i;
        while (last + 1 < points.size() && last < i + 48 &&
               lineClear(type, px, py, points[last + 1].first, points[last + 1].second))
            ++last;
        out.push_back(points[last]);
        px = points[last].first;
        py = points[last].second;
        i = last + 1;
    }
    return reached;
}

// A path over the terrain (compact): the long path over the regions (A* between their centres;
// the source and the destination at their own points), and the unit walks toward the centre of the region two steps
// ahead while more than 3 remain, then to the destination; each leg a walk over the minitiles. A goal it cannot reach
// (another group, or ground its box does not fit): the reachable place nearest to it, which becomes its move target.
void World::findPath(Unit& u, int x, int y) {
    ++pathSearches;
    if (searchFrame != frame) {
        searchFrame = frame;
        searchesNow = 0;
        search.spent = 0;
    }
    ++searchesNow;
    const int goalX = x, goalY = y;
    u.path.clear();
    u.cut = u.unreached = false;
    auto regionAt = [&](int px, int py) {
        int tx = std::clamp(px / 32, 0, map.width - 1), ty = std::clamp(py / 32, 0, map.height - 1);
        return map.tileRegion.empty() ? -1 : map.tileRegion[size_t(ty * map.width + tx)];
    };
    // (a goal where the unit's box does not fit, such as an enemy in a narrow place: the nearest place near it where
    // the box fits, so the search does not flood all the ground it can reach)
    if (map.walkable(x, y) && !fits(u.type, x, y)) {
        int bestDist = -1, bx = x, by = y;
        for (int r = 4; r <= 64 && bestDist < 0; r += 4)
            for (int dy = -r; dy <= r; dy += 4)
                for (int dx = -r; dx <= r; dx += 4) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    int d = dx * dx + dy * dy;
                    if ((bestDist < 0 || d < bestDist) && fits(u.type, x + dx, y + dy)) {
                        bestDist = d;
                        bx = x + dx;
                        by = y + dy;
                    }
                }
        x = bx;
        y = by;
        u.cut = true;
        u.moveX = u.aimX = x;
        u.moveY = u.aimY = y;
    }
    int src = regionAt(u.x, u.y), dst = regionAt(x, y);
    // A goal in another group (or on unwalkable ground): first the reachable place nearest to it, from the regions'
    // groups, so the search does not flood all the ground it can reach (groups).
    int group = src >= 0 ? map.regions[size_t(src)].group : 0;
    // (a walkable region of another group, or an unwalkable minitile; a walkable minitile of a tile with few walkable
    // minitiles is left to the minitile search)
    bool away = group && dst >= 0 &&
                ((map.regions[size_t(dst)].group && map.regions[size_t(dst)].group != group) || !map.walkable(x, y));
    if (away) {
        std::vector<std::pair<int, int>> tiles;  // (distance, tile) of the group's tiles
        for (int ty = 0; ty < map.height; ++ty)
            for (int tx = 0; tx < map.width; ++tx)
                if (map.regions[size_t(map.tileRegion[size_t(ty * map.width + tx)])].group == group) {
                    int dx = std::max({0, tx * 32 - x, x - (tx * 32 + 31)});
                    int dy = std::max({0, ty * 32 - y, y - (ty * 32 + 31)});
                    tiles.push_back({dx * dx + dy * dy, ty * map.width + tx});
                }
        std::partial_sort(tiles.begin(), tiles.begin() + std::min<size_t>(64, tiles.size()), tiles.end());
        int bestDist = -1, bx = x, by = y;
        for (size_t k = 0; k < tiles.size() && k < 64 && (bestDist < 0 || tiles[k].first <= bestDist); ++k)
            for (int my = 0; my < 32; my += 2)  // (the places of the tile where the box fits, every 2 pixels)
                for (int mx = 0; mx < 32; mx += 2) {
                    int px = tiles[k].second % map.width * 32 + mx, py = tiles[k].second / map.width * 32 + my;
                    int dd = (px - x) * (px - x) + (py - y) * (py - y);
                    if ((bestDist < 0 || dd < bestDist) && fits(u.type, px, py)) {
                        bestDist = dd;
                        bx = px;
                        by = py;
                    }
                }
        if (bestDist >= 0) {
            x = bx;
            y = by;
            dst = regionAt(x, y);
            u.cut = true;
            u.moveX = u.aimX = x;
            u.moveY = u.aimY = y;
        }
    }
    std::vector<int> chain;
    if (src >= 0 && dst >= 0 && src != dst && map.regions[size_t(src)].group &&
        map.regions[size_t(src)].group == map.regions[size_t(dst)].group) {
        size_t n = map.regions.size();
        std::vector<double> cost(n, -1);
        std::vector<int> from(n, -1);
        auto pos = [&](int g) {
            return g == src ? std::make_pair(u.x, u.y)
                            : g == dst ? std::make_pair(x, y)
                                       : std::make_pair(map.regions[size_t(g)].x, map.regions[size_t(g)].y);
        };
        auto dist = [&](int a, int b) {
            auto [ax, ay] = pos(a);
            auto [bx, by] = pos(b);
            return std::hypot(double(ax - bx), double(ay - by));
        };
        using Node = std::pair<double, int>;
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        cost[size_t(src)] = 0;
        open.push({dist(src, dst), src});
        while (!open.empty()) {
            int g = open.top().second;
            open.pop();
            if (g == dst) break;
            for (int nb : map.regions[size_t(g)].neighbours) {
                double c = cost[size_t(g)] + dist(g, nb);
                if (cost[size_t(nb)] >= 0 && cost[size_t(nb)] <= c) continue;
                cost[size_t(nb)] = c;
                from[size_t(nb)] = g;
                open.push({c + dist(nb, dst), nb});
            }
        }
        for (int g = dst; g >= 0; g = from[size_t(g)]) chain.push_back(g);
        std::reverse(chain.begin(), chain.end());
        if (chain.front() != src) chain.clear();
    }
    int fx = u.x, fy = u.y;
    u.partial = false;
    for (size_t i = 0; i + 3 < chain.size(); i += 2) {  // (the centre two regions ahead while more than 3 remain)
        if (i >= 12) {  // (6 legs now, the rest when they are walked: a long path costs no more in one frame)
            u.partial = true;
            break;
        }
        const MapInfo::Region& g = map.regions[size_t(chain[i + 2])];
        std::vector<std::pair<int, int>> leg;
        gridPath(u.type, fx, fy, g.x, g.y, leg);
        if (leg.empty() || leg.back() == std::make_pair(fx, fy)) continue;
        u.path.insert(u.path.end(), leg.begin(), leg.end());
        std::tie(fx, fy) = u.path.back();
    }
    if (u.partial) {
        while (!u.path.empty() && u.path.front() == std::make_pair(u.x, u.y)) u.path.erase(u.path.begin());
        if (!u.path.empty()) return;
        u.partial = false;  // (the legs get it nowhere: the whole way, or the nearest place)
    }
    // A goal this unit type could not reach from this region before: on to the place it reached then (the terrain does
    // not change, so the search need not cover all the ground again). (By region, not by group: a group can hold
    // places a big unit's box cannot get between.)
    int from = regionAt(fx, fy);
    long long key = (long long(u.type) << 52) | (long long(from & 0xfffff) << 32) |
                    (long long(std::clamp(y / 8, 0, map.height * 4 - 1)) * map.width * 4 +
                     std::clamp(x / 8, 0, map.width * 4 - 1));
    auto known = cutCache.find(key);
    bool cached = known != cutCache.end() && !away;
    int tx = cached ? known->second.first : x, ty = cached ? known->second.second : y;
    bool reached = gridPath(u.type, fx, fy, tx, ty, u.path);
    // (a search to the place it reached for a goal it could not reach: the wait for that goal stays as it is)
    bool toCutPlace = u.cutGoalX >= 0 && std::abs(goalX - u.cutX) <= 32 && std::abs(goalY - u.cutY) <= 32;
    bool same = std::abs(goalX - u.cutGoalX) <= 32 && std::abs(goalY - u.cutGoalY) <= 32;
    auto remember = [&](int px, int py, bool nearer) {  // (the wait: longer each time it gets no nearer the goal)
        u.fails = !same ? 1 : nearer ? 0 : std::min(u.fails + 1, 4);
        u.cutGoalX = goalX;
        u.cutGoalY = goalY;
        u.cutX = px;
        u.cutY = py;
        u.nextSearch = u.fails ? frame + (24 << (u.fails - 1)) : frame;
    };
    auto gap = [&](int px, int py) { return octagonalLength(px - goalX, py - goalY); };
    if (!reached && !cached && !away && search.capped && !toCutPlace) {
        // (the search stopped at its cap: the unit walks to the place where the search would go on, and searches on
        // from there; with no wait while each part gets it nearer)
        // (after 8 such parts with no search that got to its goal, it counts as a goal it cannot reach: a unit that
        // goes from one far enemy to another does not search on for ever)
        auto [px, py] = u.path.back();
        ++u.parts;
        if (u.path.back() != std::make_pair(u.x, u.y) && u.parts <= 8) {
            remember(px, py, same && gap(px, py) + 16 < gap(u.cutX, u.cutY));
            u.partial = true;
            while (u.path.front() == std::make_pair(u.x, u.y)) u.path.erase(u.path.begin());
            return;
        }
    }
    if (!reached && !away && !search.capped && !cached) cutCache[key] = u.path.back();
    if (reached) u.parts = 0;
    u.unreached = !reached || away || cached;
    if (u.unreached) {  // (it stops at the nearest place it can reach)
        u.cut = true;
        u.moveX = u.aimX = u.path.back().first;
        u.moveY = u.aimY = u.path.back().second;
    }
    // (a goal it could not reach: the same goal is searched again only after a wait, longer after each failure)
    if (toCutPlace) {
        u.cut = true;
    } else if (u.cut) {
        remember(u.moveX, u.moveY, false);
    } else {
        u.fails = u.nextSearch = 0;
        u.cutGoalX = u.cutGoalY = -1;
    }
    while (!u.path.empty() && u.path.front() == std::make_pair(u.x, u.y)) u.path.erase(u.path.begin());
}

bool World::searchBudgetLeft() const {
    return searchFrame != frame || (searchesNow < kSearchBudget && search.spent < kExpandBudget);
}

void World::setMove(Unit& u, int x, int y) {
    x = std::clamp(x, 0, map.width * 32 - 1);
    y = std::clamp(y, 0, map.height * 32 - 1);
    if (x == u.frozenX && y == u.frozenY) return;  // (immovable)
    // (a goal it cannot reach, or one within a tile of it: it stays at the nearest place)
    // (a goal it could not reach a short time ago: on to the place it reached then, with no new search; after the wait,
    // a new search)
    bool known = u.cutGoalX >= 0 && std::abs(x - u.cutGoalX) <= 32 && std::abs(y - u.cutGoalY) <= 32;
    bool waiting = known && frame < u.nextSearch;
    if (u.cut && std::abs(x - u.pathX) <= 32 && std::abs(y - u.pathY) <= 32 && (waiting || !known)) return;
    if (known && u.cut && waiting) return;
    if (waiting && !(t.units[u.type].flags & kFlyer)) {
        int fails = u.fails, gx = u.cutGoalX, gy = u.cutGoalY, next = u.nextSearch, cx = u.cutX, cy = u.cutY;
        if (std::abs(cx - gx) > 32 || std::abs(cy - gy) > 32) {
            setMove(u, cx, cy);
        } else if (u.moveX != cx || u.moveY != cy) {  // (the place is near the goal: straight there)
            u.path.clear();
            u.partial = u.pending = false;
            u.moveX = u.aimX = cx;
            u.moveY = u.aimY = cy;
            u.pathFrame = frame;
        }
        u.fails = fails;
        u.cutGoalX = gx;
        u.cutGoalY = gy;
        u.nextSearch = next;
        u.cutX = cx;
        u.cutY = cy;
        u.cut = true;
        if (!u.pending) {  // (a search that waits keeps its goal: the reached place)
            u.pathX = x;
            u.pathY = y;
        }
        return;
    }
    u.frozenX = u.frozenY = -1;
    bool same = x == u.moveX && y == u.moveY;
    u.moveX = u.aimX = x;
    u.moveY = u.aimY = y;
    if (!same) u.pathFrame = frame;  // (a new move target: a new path)
    if (same || u.building || (t.units[u.type].flags & kFlyer)) return;
    // A new path only when the goal moved more than a tile from the old path's goal (a followed target).
    if (!u.path.empty() && !u.partial && std::abs(x - u.pathX) <= 32 && std::abs(y - u.pathY) <= 32) {
        u.path.back() = {x, y};
        return;
    }
    u.path.clear();
    u.cut = u.partial = u.pending = false;
    u.pathX = x;
    u.pathY = y;
    if (!lineClear(u.type, u.x, u.y, x, y)) {
        if (!searchBudgetLeft()) {
            u.pending = true;  // (the frame's searches are used: it waits, and searches on a later frame)
            return;
        }
        findPath(u, x, y);
    } else if (combat && std::max(std::abs(x - u.x), std::abs(y - u.y)) > t.rules.straightReach) {
        // (the short path's reach: at most 192 pixels along the main axis, then a new path from there)
        bool alongX = std::abs(x - u.x) >= std::abs(y - u.y);
        const int reach = t.rules.straightReach;
        int lx = alongX ? u.x + (x > u.x ? reach : -reach) : u.x, ly = alongX ? u.y : u.y + (y > u.y ? reach : -reach);
        if (lineClear(u.type, u.x, u.y, lx, ly) && lineClear(u.type, lx, ly, x, y)) u.path = {{lx, ly}, {x, y}};
    }
}

void World::stopMove(Unit& u) {
    u.moveX = u.x;
    u.moveY = u.y;
    u.path.clear();
    u.cut = u.partial = u.pending = false;
}

void World::setOrder(Unit& u, Order o) {
    u.order = o;
    u.state = 0;
    u.poll = 0;
}

// One frame of movement. Units with flingy control 2 move by the distance that
// their walking animation gives (the iscript "move" instruction), the others by the flingy speed and acceleration.
// In a fight, a ground unit whose next place would overlap another unit waits or steps aside.
// Flying units have no repulse: a simplification, so air groups bunch up.
bool World::move(Unit& u, bool last) {
    if (!u.alive || u.hidden || u.building || u.burrowed) return false;
    if (u.pending) {  // (a search that waits for the budget: it stands until then)
        if (!searchBudgetLeft()) return false;
        u.pending = false;
        findPath(u, u.pathX, u.pathY);
    }
    int savedHeading = u.heading, savedVelDir = u.velDir, savedSpeed = u.speed;
    const FlingyData& f = t.flingy[size_t(t.units[u.type].flingy)];
    bool scripted = f.control == kControlScript;
    const bool fast = speedUpgraded(u);
    const int top = fast ? upgradedSpeed(f.topSpeed, true) : f.topSpeed;
    const int accel = fast ? f.accel * t.rules.speedUpgradeAccel : f.accel;
    const int fullTurn = fast ? f.turn * t.rules.speedUpgradeTurn : f.turn;
    int turn = fullTurn / (scripted ? 1 : t.rules.velocityTurnDivisor);
    if (u.burrowing) {  // (it only turns)
        if (u.x != u.aimX || u.y != u.aimY)
            u.heading = u.velDir = turnToward(u.heading, direction(u.aimX - u.x, u.aimY - u.y), f.turn);
        return false;
    }
    if (u.retry) {  // (a failed path: the block counter goes from 10 to 20, then a new search; none: immovable)
        if (++u.stuck < t.rules.stuckRetryEnd) return false;
        u.retry = false;
        if (!shortPath(u, -1, false)) {
            u.frozenX = u.moveX;
            u.frozenY = u.moveY;
            stopMove(u);
        }
        return false;
    }
    // (a waypoint is passed at it; a braking unit passes one that is not its move target within 16 pixels when it can
    // walk straight on to the next)
    while (!u.path.empty() && ((u.x == u.path.front().first && u.y == u.path.front().second) ||
                               (!scripted && u.path.size() > 1 &&
                                octagonalLength(u.path.front().first - u.x, u.path.front().second - u.y) <=
                                    t.rules.waypointPass &&
                                lineClear(u.type, u.x, u.y, u.path[1].first, u.path[1].second))))
        u.path.erase(u.path.begin());
    if (u.path.empty() && u.partial) {  // (the next part of a long path; it waits for its turn)
        if (frame < u.nextSearch || !searchBudgetLeft()) return false;
        findPath(u, u.moveX, u.moveY);
    }
    if (!u.path.empty() && !u.partial && (u.path.back().first != u.moveX || u.path.back().second != u.moveY))
        u.path.clear();
    int tx = u.moveX, ty = u.moveY;
    if (!u.path.empty()) {
        tx = u.path.front().first;
        ty = u.path.front().second;
    }
    bool atTarget = u.x == tx && u.y == ty;
    int desired = u.heading;
    if (!atTarget)
        desired = direction(tx - u.x, ty - u.y);
    else if (u.x != u.aimX || u.y != u.aimY)
        desired = direction(u.aimX - u.x, u.aimY - u.y);
    u.velDir = turnToward(u.velDir, desired, f.turn ? turn : 256);  // (no turn rate, such as a Spider Mine: at once)
    u.heading = turnToward(u.heading, desired, f.turn ? fullTurn : 256);  // (the full rate)
    if (atTarget || u.repeatAttack) {  // (a braking unit only slows down, to 0)
        u.speed = scripted || u.speed - accel < 0 ? 0 : u.speed - accel;
        return false;
    }
    double fx = double((tx << 8) - u.ex), fy = double((ty << 8) - u.ey);
    int dist = int(std::sqrt(fx * fx + fy * fy));
    int speed;
    if (scripted) {
        speed = u.speed;
        u.speed = 0;
        if (std::abs(int(int8_t(uint8_t(desired - u.heading)))) >= 32) return false;
        if (u.startWait == 1) {
            u.startWait = 0;
            return false;
        }
    } else {
        int halt = haltDistance(u);
        bool last = tx == u.moveX && ty == u.moveY;  // (it brakes only for its move target)
        bool accelerate = (dist > halt || !last) && (u.velDir == desired || dist >= 32 * 256);
        u.speed = std::clamp(u.speed + (accelerate ? accel : -accel), 0, top);
        speed = u.speed;
    }
    if (speed <= 0) return false;
    int32_t ex = tx << 8, ey = ty << 8;
    if (speed < dist) {
        ex = u.ex + dirX(u.velDir) * speed / 256;
        ey = u.ey + dirY(u.velDir) * speed / 256;
    }
    int nx = (ex + 128) >> 8, ny = (ey + 128) >> 8;
    // The terrain: a ground unit's box never goes onto an unwalkable minitile. The full step,
    // else half of it, else a quarter; none fits: a new path from here (once a frame), else it stops.
    if (!(t.units[u.type].flags & kFlyer) && !fits(u.type, nx, ny) &&
        (fits(u.type, u.x, u.y) || !map.walkable(nx, ny))) {
        for (int part = 2; part <= 4 && !fits(u.type, nx, ny); part *= 2) {
            int32_t hx = u.ex + (ex - u.ex) / part, hy = u.ey + (ey - u.ey) / part;
            if (fits(u.type, (hx + 128) >> 8, (hy + 128) >> 8)) {
                ex = hx;
                ey = hy;
                nx = (hx + 128) >> 8;
                ny = (hy + 128) >> 8;
            }
        }
        if (!fits(u.type, nx, ny) || (nx == u.x && ny == u.y)) {
            u.speed = 0;
            if (++u.stuck > t.rules.stuckGiveUp) {
                stopMove(u);
            } else if (u.pathFrame != frame && searchBudgetLeft()) {
                u.pathFrame = frame;
                findPath(u, u.moveX, u.moveY);
            }
            return false;
        }
    }
    if (combat && !(t.units[u.type].flags & kFlyer)) {
        // The full step, else half of it, else a quarter. All blocked: the collision
        // reaction.
        int b = blocker(u, nx, ny);
        for (int part = 2; b >= 0 && part <= 4; part *= 2) {
            int32_t hx = u.ex + (ex - u.ex) / part, hy = u.ey + (ey - u.ey) / part;
            int px = (hx + 128) >> 8, py = (hy + 128) >> 8;
            if ((px != u.x || py != u.y) && blocker(u, px, py) < 0 && fits(u.type, px, py)) {
                ex = hx;
                ey = hy;
                nx = px;
                ny = py;
                b = -1;
            }
        }
        if (b >= 0) {
            const Unit& o = units[size_t(b)];
            bool moving = o.x != o.moveX || o.y != o.moveY;
            if (!last && moving && o.movedFrame != frame) {  // (it has not moved yet this frame: try again after)
                u.heading = savedHeading;
                u.velDir = savedVelDir;
                u.speed = savedSpeed;
                return true;
            }
            react(u, o, tx, ty);
            return false;
        }
    }
    if (speed >= dist && !scripted) u.speed = 0;
    u.stuck = 0;
    u.movedFrame = frame;
    u.ex = ex;
    u.ey = ey;
    u.x = nx;
    u.y = ny;
    return false;
}

// The collision reaction (without the slide): settle,
// freeze, yield, or a new short path around the blocker C or around all moving units.
void World::react(Unit& u, const Unit& c, int tx, int ty) {
    ++u.stuck;
    enum { Settle = 1, Freeze, Repath, RepathAll, StopRepath, Yield };
    auto at = [&](int x, int y, const Unit& o, int ox, int oy) {
        Unit p = o;
        p.x = ox;
        p.y = oy;
        return overlaps(u.type, x, y, p, true);
    };
    const Unit* target = u.order == AttackUnit && u.target >= 0 ? &units[size_t(u.target)] : nullptr;
    bool cMoving = c.x != c.moveX || c.y != c.moveY;
    int outcome = 0;
    bool b1 = false;
    if (target && gap(u, *target) == 0) {
        outcome = Settle;  // (it touches its target)
    } else if (at(u.moveX, u.moveY, c, c.x, c.y)) {  // (C stands on its destination; else on to C)
        b1 = true;
        if (c.stuck >= t.rules.stuckGiveUp || at(u.moveX, u.moveY, c, c.moveX, c.moveY))
            outcome = target ? Repath : Freeze;
    } else if (!at(tx, ty, c, c.x, c.y)) {  // (C is not on its next step)
        const UnitData &d = t.units[u.type], &cd = t.units[c.type];
        int side = c.y + cd.down < u.y - d.up    ? 0
                   : c.x - cd.left > u.x + d.right ? 1
                   : c.y - cd.up > u.y + d.down    ? 2
                                                   : 3;
        int h = c.heading;  // (C moves away from it along the axis that joins them)
        bool away = side == 0   ? h <= 64 || h >= 192
                    : side == 1 ? h <= 128
                    : side == 2 ? h >= 64 && h <= 192
                                : h == 0 || h >= 128;
        outcome = cMoving && (away || c.stuck <= t.rules.stuckFresh) ? Yield : StopRepath;
    }
    if (!outcome && !b1 && (c.stuck >= t.rules.stuckGiveUp || at(tx, ty, c, c.moveX, c.moveY))) outcome = Repath;
    if (!outcome) {  // (C's heading snapped to a side, against the direction to C)
        int snapped = ((c.heading + 32) & 255) / 64 * 64;
        int diff = std::abs(int(int8_t(uint8_t(snapped - direction(c.x - u.x, c.y - u.y)))));
        outcome = diff > 64 ? RepathAll : Yield;
    }
    if (outcome == Yield && u.stuck >= t.rules.stuckYield) outcome = random() & 1 ? StopRepath : RepathAll;
    // (the new paths wait until the path is 7 frames old; 50: a cap, so that a unit whose target walks is not held
    // forever)
    if (outcome >= Repath && outcome <= StopRepath && frame - u.pathFrame < t.rules.pathAgeForRepath &&
        u.stuck < t.rules.stuckHold)
        return;
    switch (outcome) {
    case Settle:
    case Freeze:
        if (outcome == Freeze) {
            u.frozenX = u.moveX;
            u.frozenY = u.moveY;
        }
        stopMove(u);
        u.stuck = 0;
        return;
    case Yield:
        return;
    case RepathAll:
        if (shortPath(u, -1, true)) return;
        [[fallthrough]];
    default:
        if (u.x == u.moveX && u.y == u.moveY) return;
        if (!shortPath(u, int(&c - units.data()), false)) {
            u.retry = true;
            u.stuck = t.rules.stuckRetry;
        }
    }
}

// The short path around units (our own grid design): A* over 8-pixel steps within
// 128 pixels of the unit, where the unit's box must stay on walkable ground and off the units at rest (and the unit to
// consider, or all moving units). The goal not reached: the step nearest to it. False: no step away from here.
bool World::shortPath(Unit& u, int consider, bool movers) {
    ++shortSearches;
    const UnitData& d = t.units[u.type];
    // (the obstacles as the boxes where the unit's centre may not be: their boxes grown by its own)
    std::vector<std::array<int, 4>> obstacles;
    forNear(u.x - 224, u.y - 224, u.x + 224, u.y + 224, [&](int j) {
        const Unit& o = units[size_t(j)];
        if (&o == &u || !o.alive || o.hidden || o.burrowed || (t.units[o.type].flags & kFlyer)) return;
        if (std::abs(o.x - u.x) > 128 + 96 || std::abs(o.y - u.y) > 128 + 96) return;  // (outside the search's reach)
        bool rest = o.x == o.moveX && o.y == o.moveY;
        if (!(rest || movers || o.id == consider) || overlaps(u.type, u.x, u.y, o, true)) return;
        const UnitData& od = t.units[o.type];
        int ol = od.left, orr = od.right, ou = od.up, odn = od.down;
        if (o.building && od.placeW > 0) {
            ol = od.placeW / 2;
            orr = od.placeW - ol - 1;
            ou = od.placeH / 2;
            odn = od.placeH - ou - 1;
        }
        obstacles.push_back({o.x - ol - d.right, o.y - ou - d.down, o.x + orr + d.left, o.y + odn + d.up});
    });
    auto free = [&](int x, int y) {
        if (!map.boxWalkable(x - d.left, y - d.up, x + d.right, y + d.down)) return false;
        for (const auto& o : obstacles)
            if (x >= o[0] && y >= o[1] && x <= o[2] && y <= o[3]) return false;
        return true;
    };
    auto clear = [&](int x0, int y0, int x1, int y1) {
        int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) / 4 + 1;
        for (int i = 1; i <= steps; ++i)
            if (!free(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)) return false;
        return true;
    };
    const int R = 16, N = 2 * R + 1;
    int gx = u.moveX, gy = u.moveY;
    auto px = [&](int c) { return u.x + (c % N - R) * 8; };
    auto py = [&](int c) { return u.y + (c / N - R) * 8; };
    auto estimate = [&](int c) { return octagonalLength(gx - px(c), gy - py(c)) * 10 / 8; };
    std::vector<int> cost(N * N, -1), from(N * N, -1);
    std::vector<signed char> open(N * N, -1);  // (-1: not seen, 1: free, 0: blocked)
    using Node = std::pair<int, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    int start = R * N + R, best = start, bestEstimate = estimate(start);
    cost[size_t(start)] = 0;
    queue.push({bestEstimate, start});
    for (int expanded = 0; !queue.empty() && expanded < 250; ++expanded) {
        int c = queue.top().second;
        queue.pop();
        int e = estimate(c);
        if (e < bestEstimate) {
            best = c;
            bestEstimate = e;
        }
        if (std::abs(px(c) - gx) <= 4 && std::abs(py(c) - gy) <= 4) break;
        int cx = c % N, cy = c / N;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = cx + dx, ny = cy + dy, n = ny * N + nx;
                if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                auto isFree = [&](int k) {
                    if (open[size_t(k)] < 0) open[size_t(k)] = free(px(k), py(k)) ? 1 : 0;
                    return open[size_t(k)] == 1;
                };
                if (!isFree(n) || (dx && dy && (!isFree(cy * N + nx) || !isFree(ny * N + cx)))) continue;
                int nc = cost[size_t(c)] + (dx && dy ? 14 : 10);
                if (cost[size_t(n)] >= 0 && cost[size_t(n)] <= nc) continue;
                cost[size_t(n)] = nc;
                from[size_t(n)] = c;
                queue.push({nc + estimate(n), n});
            }
    }
    if (best == start) return false;
    std::vector<std::pair<int, int>> points;
    for (int c = best; c != start; c = from[size_t(c)]) points.push_back({px(c), py(c)});
    std::reverse(points.begin(), points.end());
    u.path.clear();
    int x = u.x, y = u.y;
    for (size_t i = 0; i < points.size();) {  // (only the waypoints that a straight walk cannot skip)
        size_t last = i;
        while (last + 1 < points.size() && clear(x, y, points[last + 1].first, points[last + 1].second)) ++last;
        u.path.push_back(points[last]);
        x = points[last].first;
        y = points[last].second;
        i = last + 1;
    }
    if (u.path.back() != std::make_pair(gx, gy)) u.path.push_back({gx, gy});
    u.pathX = gx;
    u.pathY = gy;
    u.pathFrame = frame;
    return true;
}

// The largest ground unit whose box the unit's box would overlap at (x, y), and does not overlap now; -1: none.
int World::blocker(const Unit& u, int x, int y) const {
    int best = -1, bestArea = 0;
    forNear(x - 192, y - 192, x + 192, y + 192, [&](int j) {  // (boxes, footprints and a frame's moves)
        const Unit& o = units[size_t(j)];
        if (&o == &u || !o.alive || o.hidden || o.burrowed) return;
        if (role(o.type).spiderMine && o.state >= 1 && o.state <= 3) return;  // (a Spider Mine burrowing: no collision)
        const UnitData& od = t.units[o.type];
        if (od.flags & kFlyer) return;
        int area = (od.left + od.right + 1) * (od.up + od.down + 1);
        if ((area > bestArea || (area == bestArea && j < best)) && overlaps(u.type, x, y, o, true) &&
            !overlaps(u.type, u.x, u.y, o, true)) {  // (the largest; of equal ones, the first in the list)
            best = j;
            bestArea = area;
        }
    });
    return best;
}

void World::animate(int unit, int anim) {
    startAnim(units[size_t(unit)].anim, anim);
    for (Effect& e : effects)
        if (e.follow == unit && t.animOffset(e.anim.header, anim)) startAnim(e.anim, anim);
}

void World::startAnim(Anim& a, int anim) {
    int pc = t.animOffset(a.header, anim);
    if (!pc) return;
    a.pc = pc;
    a.wait = 0;
    a.paused = false;
}

// One frame of an image's iscript: the instructions up to the next wait. Instructions that only draw (overlays,
// sounds) are passed over, but those that draw random numbers draw them.
void World::runAnim(Anim& a, int unit, int bullet) {
    if (a.paused || a.header < 0) return;
    if (a.wait > 0) {
        --a.wait;
        return;
    }
    const std::vector<uint8_t>& s = t.iscript;
    auto u16 = [&](int o) { return o + 1 < int(s.size()) ? s[o] | s[o + 1] << 8 : 0; };
    for (int guard = 0; guard < 256; ++guard) {
        int pc = a.pc, len = t.opLength(pc);
        if (len < 0) {
            a.paused = true;
            return;
        }
        a.pc = pc + len;
        int arg = len > 1 ? s[pc + 1] : 0;
        Unit* u = unit >= 0 ? &units[unit] : nullptr;
        switch (s[pc]) {
        case 0x00: a.frame = u16(pc + 1); break;  // playfram
        case 0x05: a.wait = arg - 1; return;       // wait
        case 0x06: {                               // waitrand
            int lo = s[pc + 1], hi = s[pc + 2];
            a.wait = lo + ((random() * (hi - lo + 1)) >> 15) - 1;
            return;
        }
        case 0x07: a.pc = u16(pc + 1); break;  // goto
        case 0x16:                              // end
            if (bullet >= 0) bullets[size_t(bullet)].alive = false;
            a.paused = true;
            return;
        case 0x18: case 0x19: case 0x1A: {  // playsnd, playsndrand, playsndbtwn (the game's draws as before)
            int op = s[size_t(pc)], r = op == 0x18 ? 0 : random();
            int sfx = op == 0x18 ? u16(pc + 1)
                      : op == 0x19 ? u16(pc + 2 + 2 * (arg ? r % arg : 0))
                                   : u16(pc + 1) + r % std::max(1, u16(pc + 3) - u16(pc + 1) + 1);
            if (u) sound(sfx, u->x, u->y, u->owner);
            else if (bullet >= 0) sound(sfx, bullets[size_t(bullet)].x, bullets[size_t(bullet)].y,
                                        bullets[size_t(bullet)].owner);
            break;
        }
        case 0x22: random(); break;  // turnrand
        case 0x1B: case 0x44:                              // domissiledmg, dogrddamage
            if (bullet >= 0) bulletHit(bullets[size_t(bullet)]);
            break;
        case 0x1C:  // attackmelee
            if (u) fire(unit, t.units[u->type].groundWeapon, true);
            break;
        case 0x1E:  // randcondjmp
            if ((random() & 0xff) <= arg) a.pc = u16(pc + 2);
            break;
        case 0x1F: if (u) u->heading = (u->heading - arg * 8) & 255; break;  // turnccwise
        case 0x20: if (u) u->heading = (u->heading + arg * 8) & 255; break;  // turncwise
        case 0x21: if (u) u->heading = (u->heading + 8) & 255; break;        // turn1cwise
        case 0x25: case 0x31:                                                // attackwith, attkshiftproj
            if (u) {
                const UnitData& d = weaponUnit(u->type);
                // (attkshiftproj n: the ground weapon from n pixels ahead)
                fire(unit, s[pc] == 0x25 && arg == 2 ? d.airWeapon : d.groundWeapon, false, s[pc] == 0x31 ? arg : -1);
            }
            break;
        case 0x26:  // attack
            if (u && u->target >= 0) fire(unit, weaponFor(*u, units[size_t(u->target)]), false);
            break;
        case 0x28: if (u) fire(unit, arg, false); break;   // useweapon
        case 0x08: case 0x09: case 0x0D: case 0x0E: case 0x3D:  // imgol, imgul, imgoluselo, imguluselo, imgulnextid
        case 0x0F: case 0x10: case 0x11: case 0x14: case 0x42:  // sprol, highsprol, lowsprul, sprul, grdsprol
            if (bullet >= 0 && s[size_t(pc)] >= 0x0F && s[size_t(pc)] != 0x3D) {  // (a projectile's sprite: the spines)
                int id = u16(pc + 1);
                const Bullet& b = bullets[size_t(bullet)];
                bool shown = id < int(t.spriteImage.size()) && t.spriteVisible[size_t(id)];
                bool under = s[size_t(pc)] == 0x11 || s[size_t(pc)] == 0x14 || s[size_t(pc)] == 0x42;
                if (shown)
                    addEffect(t.spriteImage[size_t(id)], b.x + int8_t(s[size_t(pc) + 3]),
                              b.y + int8_t(s[size_t(pc) + 4]), b.owner, b.heading,
                              spriteElevation(s[size_t(pc)], bulletElevation(b)), under, 0, -1, 0, 0);
            }
            if (u && &a == &u->anim) {  // (the main image's: overlays stay with the unit, sprites go their own way)
                int op = s[size_t(pc)];
                int id = op == 0x3D ? t.flingyImage(t.units[size_t(u->type)].flingy) + 1 : u16(pc + 1);
                bool lo = op == 0x0D || op == 0x0E;  // (offsets from a .lo file: not read, 0)
                int dx = op == 0x3D ? int8_t(s[size_t(pc) + 1]) : lo ? 0 : int8_t(s[size_t(pc) + 3]);
                int dy = op == 0x3D ? int8_t(s[size_t(pc) + 2]) : lo ? 0 : int8_t(s[size_t(pc) + 4]);
                bool sprite = op >= 0x0F && op != 0x3D;
                // (a sprite the game never draws: none)
                bool shown = !sprite || (id < int(t.spriteImage.size()) && t.spriteVisible[size_t(id)]);
                int image = !shown ? -1 : sprite ? t.spriteImage[size_t(id)] : id;
                bool under = op == 0x09 || op == 0x0E || op == 0x3D || op == 0x11 || op == 0x14 || op == 0x42;
                int level = t.units[size_t(u->type)].elevation;
                addEffect(image, u->x + dx, u->y + dy, u->owner, u->heading,
                          sprite ? spriteElevation(op, level) : level, under, 0, sprite ? -1 : unit, dx, dy);
            }
            break;
        case 0x24:                                         // sigorder: a projectile's launch
            if (bullet >= 0 && (arg & 1)) bullets[size_t(bullet)].launched = true;
            if (u && (arg & 4) && u->burrowing) {  // (the end of burrowing)
                u->burrowing = false;
                u->burrowed = true;
            }
            break;
        case 0x29:  // move (control 2 units)
            if (u) u->speed = speedUpgraded(*u) ? upgradedSpeed(arg << 8, false) : arg << 8;  // (no minimum)
            break;
        case 0x2A: if (u) u->repeatAttack = false; break;  // gotorepeatattk
        case 0x30:  // ignorerest (an attack script that has no gotorepeatattk, the Firebat's, ends its attack here)
            if (u) u->repeatAttack = false;
            a.paused = true;
            return;
        case 0x35: a.ret = a.pc; a.pc = u16(pc + 1); break;  // call
        case 0x36: a.pc = a.ret; break;                      // return
        default: break;
        }
    }
    a.paused = true;  // (a loop with no wait)
}


// The distance a unit needs to stop from its speed now (8 fractional bits), with its speed upgrade; 0 for units
// moved by their script.
int World::haltDistance(const Unit& u) const {
    const FlingyData& f = t.flingy[size_t(t.units[u.type].flingy)];
    if (f.control == kControlScript) return 0;
    bool fast = speedUpgraded(u);
    int top = fast ? upgradedSpeed(f.topSpeed, true) : f.topSpeed;
    int accel = fast ? f.accel * t.rules.speedUpgradeAccel : f.accel;
    return u.speed == top && accel == f.accel ? f.halt : u.speed * u.speed / std::max(1, 2 * accel);
}

// Whether a unit has its speed upgrade (rules.ini speedUpgrade; speedAlways: without the upgrade).
bool World::speedUpgraded(const Unit& u) const {
    const UnitRole& r = role(u.type);
    return r.speedAlways || (r.speedUpgrade >= 0 && level(u.owner, r.speedUpgrade) > 0);
}

// A speed (8 fractional bits) with the speed upgrade: rules.ini's factor (a top speed: at least its minimum).
int World::upgradedSpeed(int speed, bool top) const {
    const std::vector<int>& f = t.rules.speedUpgradeFactor;
    return top ? std::max(speed * f[0] / f[1], t.rules.speedUpgradeMin) : speed * f[0] / f[1];
}

void World::buildGrid() {
    int gw = (map.width * 32 + 63) / 64, gh = (map.height * 32 + 63) / 64;
    grid.assign(size_t(gw) * gh, {});
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (!u.alive) continue;
        int cx = std::clamp(u.x / 64, 0, gw - 1), cy = std::clamp(u.y / 64, 0, gh - 1);
        grid[size_t(cy * gw + cx)].push_back(int(i));
    }
    gridUnits = units.size();
}

std::vector<int> World::nearUnits(int x0, int y0, int x1, int y1) const {
    std::vector<int> out;
    if (gridUnits != units.size() || grid.empty()) {  // (no grid yet: all)
        out.resize(units.size());
        for (size_t i = 0; i < units.size(); ++i) out[i] = int(i);
        return out;
    }
    int gw = (map.width * 32 + 63) / 64, gh = (map.height * 32 + 63) / 64;
    for (int cy = std::clamp(y0 / 64, 0, gh - 1); cy <= std::clamp(y1 / 64, 0, gh - 1); ++cy)
        for (int cx = std::clamp(x0 / 64, 0, gw - 1); cx <= std::clamp(x1 / 64, 0, gw - 1); ++cx)
            out.insert(out.end(), grid[size_t(cy * gw + cx)].begin(), grid[size_t(cy * gw + cx)].end());
    std::sort(out.begin(), out.end());
    return out;
}

void World::step() {
    ++frame;
    if (frame == std::max(measureFrom, 1))  // (mining: the end of the warm-up)
        for (Player& p : players) {
            p.measuredFrom = frame;
            p.mineralsAtMeasure = p.gathered;
            p.gasAtMeasure = p.gatheredGas;
        }
    // Movement as if at the same time: a unit held by one that has not moved yet this frame tries again after all.
    if (combat) buildGrid();
    std::vector<Unit*> later;
    for (Unit& u : units)
        if (move(u, false)) later.push_back(&u);
    for (Unit* u : later) move(*u, true);
    if (combat) buildGrid();
    for (size_t i = 0; i < units.size(); ++i) {
        Unit& u = units[i];
        if (!u.alive) continue;
        if (u.dieAt >= 0 && frame >= u.dieAt) {
            kill(int(i), false);
            continue;
        }
        if (u.timer > 0) --u.timer;
        if (u.cooldown > 0) --u.cooldown;
        const UnitData& d = t.units[u.type];
        if ((d.flags & kRegenerates) && u.hp < d.hp) u.hp = std::min(d.hp, u.hp + t.rules.regenHp);
        if (d.hasShields && u.shields < d.shields << 8)
            u.shields = std::min(d.shields << 8, u.shields + t.rules.regenShields);
        const UnitRole& r = role(u.type);
        bool more = r.energyUpgrade >= 0 && level(u.owner, r.energyUpgrade);
        const int maxEnergy = (more ? t.rules.maxEnergyUpgraded : t.rules.maxEnergy) << 8;
        if ((d.flags & kSpellcaster) && u.energy < maxEnergy)
            u.energy = std::min(maxEnergy, u.energy + t.rules.regenEnergy);
        if (u.order >= MoveToMinerals && u.order <= ReturnGas)
            miningOrder(int(i));
        else if (combat)
            combatOrder(int(i));
        Unit& v = units[i];
        if (v.alive && !combat && role(v.type).mineralField) runAnim(v.anim, int(i), -1);  // (a mineral field)
        if (v.alive && combat && v.building && !v.neutral) runAnim(v.anim, int(i), -1);  // (a defence: it fires)
        if (!v.alive || v.building) continue;
        bool moving = v.x != v.moveX || v.y != v.moveY;
        // A script-moved unit that starts from rest: its walk script starts a frame later, and its first movement
        // frame is lost (as the reference's movement traces show).
        bool scripted = t.flingy[size_t(t.units[v.type].flingy)].control == kControlScript;
        if (moving && !v.repeatAttack && v.walkingAnim != 1 && scripted && v.startWait == 0) {
            v.startWait = 2;
        } else if (moving && !v.repeatAttack && v.walkingAnim != 1) {
            if (v.startWait == 2) v.startWait = 1;
            animate(int(i), kAnimWalking);
            v.walkingAnim = 1;
        } else if (!moving && v.startWait) {
            v.startWait = 0;
        }
        if (!moving && v.walkingAnim == 1) {
            animate(int(i), kAnimWalkingToIdle);
            v.walkingAnim = 0;
        }
        runAnim(v.anim, int(i), -1);
        runAnim(units[i].turret, int(i), -1);
    }
    for (size_t b = 0; b < bullets.size(); ++b)
        if (bullets[b].alive) bulletStep(int(b));
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.alive; }),
                  bullets.end());
    if (combat && allTeams) trackProgress();
    for (size_t k = 0; k < effects.size(); ++k) effectStep(k);
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.done; }),
                  effects.end());
}

// A projectile's elevation: its unit's, and rules.ini's levels above it.
int World::bulletElevation(const Bullet& b) const {
    int source = b.source >= 0 ? t.units[size_t(units[size_t(b.source)].type)].elevation : 0;
    return source + t.rules.bulletElevationAbove;
}

// The elevation of a sprite that a script makes, by its opcode: sprol and grdsprol one level above its parent,
// sprul one below, highsprol the highest level, lowsprul the lowest (rules.ini).
int World::spriteElevation(int op, int parent) const {
    switch (op) {
    case 0x10: return t.rules.elevationHighest;
    case 0x11: return t.rules.elevationLowest;
    case 0x14: return std::max(t.rules.elevationLowest, parent - 1);
    default: return std::min(t.rules.elevationHighest, parent + 1);
    }
}

void World::addEffect(int image, int x, int y, int owner, int heading, int elevation, bool under, int anim, int follow,
                      int dx, int dy) {
    // (no shadows, and no image at all)
    if (image < 0 || (image < int(t.imageDraw.size()) && t.imageDraw[size_t(image)] == kDrawShadow)) return;
    Effect e;
    e.follow = follow;
    e.dx = dx;
    e.dy = dy;
    e.image = image;
    e.x = x;
    e.y = y;
    e.owner = owner;
    e.heading = heading;
    e.elevation = elevation;
    e.under = under;
    e.anim.header = t.scriptHeader(image);
    if (image < 0 || !t.animOffset(e.anim.header, anim)) return;
    startAnim(e.anim, anim);
    effects.push_back(e);
}

// One frame of an effect's script (the public iscript opcodes): frames, waits, jumps, and the images and sprites it
// makes (imgol, imgul, sprol, highsprol, lowsprul, sprul, grdsprol); the rest has no effect on a picture here.
void World::effectStep(size_t k) {
    if (effects[k].done) return;
    if (effects[k].follow >= 0) {  // (an overlay: with its unit)
        const Unit& u = units[size_t(effects[k].follow)];
        effects[k].x = u.x + effects[k].dx;
        effects[k].y = u.y + effects[k].dy;
        effects[k].heading = u.heading;
    }
    Anim& a0 = effects[k].anim;
    if (a0.wait > 0) {
        --a0.wait;
        return;
    }
    const std::vector<uint8_t>& s = t.iscript;
    auto u16 = [&](int o) { return o + 1 < int(s.size()) ? s[size_t(o)] | s[size_t(o + 1)] << 8 : 0; };
    auto draw = [&]() {
        effectSeed = effectSeed * 22695477u + 1u;
        return int((effectSeed >> 16) & 0x7fff);
    };
    for (int guard = 0; guard < 256; ++guard) {
        Effect& e = effects[k];
        int pc = e.anim.pc, len = t.opLength(pc);
        if (len < 0) break;
        e.anim.pc = pc + len;
        int op = s[size_t(pc)], arg = len > 1 ? s[size_t(pc) + 1] : 0;
        switch (op) {
        case 0x00: e.anim.frame = u16(pc + 1); break;
        case 0x05: e.anim.wait = arg - 1; return;
        case 0x06: e.anim.wait = arg + draw() % std::max(1, s[size_t(pc) + 2] - arg + 1) - 1; return;
        case 0x07: e.anim.pc = u16(pc + 1); break;
        case 0x16: e.done = true; return;
        case 0x1E: if ((draw() & 0xff) <= arg) e.anim.pc = u16(pc + 2); break;
        case 0x18: sound(u16(pc + 1), e.x, e.y, e.owner); break;  // (sounds: their own random numbers)
        case 0x19: sound(u16(pc + 2 + 2 * (arg ? draw() % arg : 0)), e.x, e.y, e.owner); break;
        case 0x1A: sound(u16(pc + 1) + draw() % std::max(1, u16(pc + 3) - u16(pc + 1) + 1), e.x, e.y, e.owner); break;
        case 0x35: e.anim.ret = e.anim.pc; e.anim.pc = u16(pc + 1); break;
        case 0x36: e.anim.pc = e.anim.ret; break;
        case 0x08: case 0x09: case 0x0F: case 0x10: case 0x11: case 0x14: case 0x42: {
            int id = u16(pc + 1), dx = int8_t(s[size_t(pc) + 3]), dy = int8_t(s[size_t(pc) + 4]);
            // (a sprite the game never draws: none)
            bool shown = op <= 0x09 || (id < int(t.spriteImage.size()) && t.spriteVisible[size_t(id)]);
            int image = !shown ? -1 : op <= 0x09 ? id : t.spriteImage[size_t(id)];
            bool under = op == 0x09 || op == 0x11 || op == 0x14 || op == 0x42 || e.under;
            Effect copy = e;  // (the vector may grow)
            bool attached = copy.follow >= 0 && op <= 0x09;  // (an image of the same sprite: it stays with the unit)
            int level = op <= 0x09 ? copy.elevation : spriteElevation(op, copy.elevation);  // (an image: its sprite's)
            addEffect(image, copy.x + dx, copy.y + dy, copy.owner, copy.heading, level, under, 0,
                      attached ? copy.follow : -1,
                      copy.dx + dx, copy.dy + dy);
            break;
        }
        default: break;
        }
    }
    effects[k].done = true;  // (a loop with no wait, or an unknown instruction)
}

void World::images(std::vector<LiveImage>& out) const {
    auto add = [&](int image, const Anim& a, int heading, int x, int y, int owner, bool air, int elevation) {
        if (image < 0) return false;
        LiveImage li;
        li.image = image;
        li.elevation = elevation;
        li.frame = a.frame;
        // A turning image's frame set holds 17 directions; a set that the GRP has no room for (a death animation's
        // frames after the last full set) is one frame.
        int frames = size_t(image) < t.imageFrames.size() ? t.imageFrames[size_t(image)] : 0;
        if (t.imageTurns[size_t(image)] && (frames == 0 || a.frame + 17 <= frames)) {
            int d = ((heading + 4) >> 3) & 31;
            li.flip = d > 16;
            li.frame += li.flip ? 32 - d : d;
        }
        li.x = x;
        li.y = y;
        li.owner = owner;
        li.sortY = y;
        li.air = air;
        out.push_back(li);
        return true;
    };
    for (const Unit& u : units) {
        if (!u.alive || u.hidden) continue;
        const UnitData& d = t.units[u.type];
        int image = t.flingyImage(d.flingy);
        int self = int(&u - units.data()), order = 0;
        const int level = d.elevation;  // (its overlays, load and turret: the same sprite's level)
        for (const Effect& e : effects)  // (its overlays under it, then it, then those above: in their order)
            if (e.follow == self && e.under && add(e.image, e.anim, e.heading, e.x, e.y, e.owner, false, level))
                out.back().order = order++;
        if (add(image, u.anim, u.heading, u.x, u.y, u.owner, (d.flags & kFlyer) != 0, level))
            out.back().order = order++;
        for (const Effect& e : effects)
            if (e.follow == self && !e.under && add(e.image, e.anim, e.heading, e.x, e.y, e.owner, false, level))
                out.back().order = order++;
        if (u.carry > 0 && image >= 0) {  // A worker's load: above it, at its overlay offset.
            int gas = role(u.type).gasImage;  // (by worker: the full gas image)
            int carried = u.carryGas && gas >= 0 ? gas : t.rules.mineralImage;
            LiveImage li = out.back();
            li.image = carried + (u.carry < t.rules.mineralLoad ? 1 : 0);
            if (u.carryGas) li.frame = 0;  // (gas: a fixed frame; minerals: the worker's)
            for (const auto& [img, frames] : t.carryOffsets)
                if (img == image && out.back().frame < int(frames.size())) {
                    li.x += (li.flip ? -1 : 1) * frames[size_t(out.back().frame)].first;
                    li.y += frames[size_t(out.back().frame)].second;
                }
            li.order = 1;
            out.push_back(li);
        }
        if (d.subunit != kNoUnit) {
            if (add(t.flingyImage(t.units[size_t(d.subunit)].flingy), u.turret, u.heading, u.x, u.y, u.owner, false,
                    level))
                out.back().order = 1;
        }
    }
    for (const Bullet& b : bullets) {  // (not a projectile whose sprite the game never draws: flame, spines, Scourge)
        const int level = bulletElevation(b);
        if (b.body >= 0 && !b.arrived) add(b.body, b.anim, b.heading, b.x, b.y, b.owner, false, level);  // (a Scarab)
        else if (b.body < 0 && t.flingyVisible(t.weapons[size_t(b.weapon)].flingy))
            add(t.flingyImage(t.weapons[size_t(b.weapon)].flingy), b.anim, b.heading, b.x, b.y, b.owner, false, level);
    }
    for (const Effect& e : effects)  // (remnants under the living units: sortY far up)
        if (e.follow < 0 && add(e.image, e.anim, e.heading, e.x, e.y, e.owner, false, e.elevation) && e.under)
            out.back().sortY = e.y - 65536;
}

namespace {

class Engine : public Simulator {
public:
    bool init(const FileReader& read, std::string& error) override {
        ready_ = tables_.load(read, error);
        return ready_;
    }
    bool setMap(std::vector<uint8_t> chk, std::string& error) override {
        stop();
        if (!ready_) {
            error = "The game data is not loaded";
            return false;
        }
        hasMap_ = map_.load(chk, tables_, error);
        return hasMap_;
    }

    bool mining(const MiningOptions& o, std::vector<MiningBase>& out, std::string& error) override {
        if (!check(error)) return false;
        out = miningBases(tables_, map_, o);
        if (out.empty()) {
            error = "The map has no base to mine at";
            return false;
        }
        std::vector<Race> races = baseRaces(tables_, map_, out, o);
        for (size_t k = 0; k < out.size(); ++k)
            for (Race race : o.mapRaces ? std::vector<Race>{races[k]} : o.races)
                for (int workers : o.workers) {
                    MiningBase& base = out[k];
                    World w(tables_, map_);
                    setupMining(w, {base}, {race}, workers, o);
                    while (w.frame < o.warmupFrames + o.frames) w.step();
                    base.runs.push_back(miningRun(w, 0, race, workers, o));
                }
        return true;
    }

    bool combat(const CombatOptions& o, CombatResult& out, std::string& error) override {
        if (!startCombat(o, error)) return false;
        while (step(1)) {
        }
        out = combatResult();
        stop();
        return error_.empty();
    }

    bool startCombat(const CombatOptions& o, std::string& error) override {
        if (!check(error)) return false;
        stop();
        world_ = std::make_unique<World>(tables_, map_);
        world_->recordSounds = true;
        if (!setupCombat(*world_, o, error)) {
            world_.reset();
            return false;
        }
        combat_ = true;
        last_ = o.frames;
        return true;
    }

    bool startMining(const MiningOptions& o, std::string& error) override {
        if (!check(error)) return false;
        stop();
        liveBases_ = miningBases(tables_, map_, o);
        if (liveBases_.size() > 8) liveBases_.resize(8);
        if (liveBases_.empty()) {
            error = "The map has no base to mine at";
            return false;
        }
        miningOptions_ = o;
        world_ = std::make_unique<World>(tables_, map_);
        world_->recordSounds = true;
        liveRaces_ = baseRaces(tables_, map_, liveBases_, o);
        setupMining(*world_, liveBases_, liveRaces_, o.workers.empty() ? 9 : o.workers[0], o);
        combat_ = false;
        last_ = o.warmupFrames + o.frames;
        return true;
    }

    bool startMap(const MapRunOptions&, MapRunInfo&, std::string& error) override {
        error = "Run the map is not in the simulation yet";
        return false;
    }

    bool live() const override { return world_ != nullptr; }
    bool step(int frames) override {
        if (!world_ || done_) return false;
        for (int i = 0; i < frames && !done_; ++i) {
            world_->step();
            if (combat_) {
                done_ = combatOver(*world_, last_);
            } else {
                done_ = world_->frame >= last_;
            }
        }
        return !done_;
    }
    int frame() const override { return world_ ? world_->frame : 0; }
    int lastFrame() const override { return last_; }
    bool done() const override { return done_; }
    const std::string& liveError() const override { return error_; }
    void images(std::vector<LiveImage>& out) const override {
        out.clear();
        if (world_) world_->images(out);
    }
    std::vector<LiveSound> takeSounds() override {
        std::vector<LiveSound> out;
        if (world_) out.swap(world_->sounds);
        return out;
    }
    CombatResult combatResult() const override {
        return world_ && combat_ ? combatResultOf(*world_) : CombatResult{};
    }
    std::vector<MiningBase> miningResult() const override {
        std::vector<MiningBase> out;
        if (!world_ || combat_) return out;
        const MiningOptions& o = miningOptions_;
        for (size_t i = 0; i < liveBases_.size(); ++i) {
            MiningBase b = liveBases_[i];
            b.runs.push_back(miningRun(*world_, int(i), liveRaces_[i], o.workers.empty() ? 9 : o.workers[0], o));
            out.push_back(b);
        }
        return out;
    }
    std::vector<PlayerState> players() const override {
        std::vector<PlayerState> out;
        if (!world_ || combat_) return out;
        for (size_t i = 0; i < liveBases_.size(); ++i) out.push_back(playerState(*world_, int(i)));
        return out;
    }
    void stop() override {
        world_.reset();
        done_ = false;
        error_.clear();
        last_ = 0;
    }

private:
    bool check(std::string& error) const {
        if (!ready_ || !hasMap_) error = ready_ ? "No map is open" : "The game data is not loaded";
        return ready_ && hasMap_;
    }

    Tables tables_;
    MapInfo map_;
    bool ready_ = false, hasMap_ = false, combat_ = false, done_ = false;
    std::unique_ptr<World> world_;
    std::vector<MiningBase> liveBases_;
    std::vector<Race> liveRaces_;
    MiningOptions miningOptions_;
    int last_ = 0;
    std::string error_;
};

}  // namespace

std::unique_ptr<Simulator> createSimulator() { return std::make_unique<Engine>(); }

}  // namespace sim
