// Worker mining: the bases of a map, the melee start at each, the gather orders
// (MoveToMinerals, WaitForMinerals, MiningMinerals, ReturnMinerals and the gas ones), and the measures.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include "world.h"

namespace sim {

namespace {

bool isMinerals(const Tables& t, int type) { return t.roles[size_t(type)].mineralField; }
bool isGeyser(const Tables& t, int type) { return t.roles[size_t(type)].geyser; }
bool isStart(const Tables& t, int type) { return t.roles[size_t(type)].startLocation; }

double dist(int ax, int ay, int bx, int by) { return exactDistance(ax, ay, bx, by); }

// A depot on the tile grid at a place.
std::pair<int, int> depotSpot(const Tables& t, int type, int x, int y) {
    const UnitData& d = t.units[type];
    auto snap = [](int v, int half) { return (v - half) / 32 * 32 + half; };
    return {snap(x, d.placeW / 2), snap(y, d.placeH / 2)};
}

// The gap between two placement boxes (centre and size), along the farther axis.
int placementGap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    Box a{ax - aw / 2, ay - ah / 2, ax + aw / 2 - 1, ay + ah / 2 - 1};
    Box b{bx - bw / 2, by - bh / 2, bx + bw / 2 - 1, by + bh / 2 - 1};
    return axisGap(a, b);
}

// A Command Center spot for a group of mineral fields: on the tile grid, on walkable ground, not within 3 tiles of a
// resource, and the nearest to the fields (the sum of the distances).
bool clusterSpot(const Tables& t, const MapInfo& map, const std::vector<MapInfo::Placed>& group, int& outX,
                 int& outY) {
    const UnitData& cc = t.units[size_t(t.depotOf[1])];  // (a Command Center's place)
    double cx = 0, cy = 0;
    for (auto& f : group) cx += f.x, cy += f.y;
    cx /= double(group.size());
    cy /= double(group.size());
    double best = -1;
    const int span = t.rules.baseSearchTiles;
    for (int ty = int(cy) / 32 - span; ty <= int(cy) / 32 + span; ++ty)
        for (int tx = int(cx) / 32 - span; tx <= int(cx) / 32 + span; ++tx) {
            int x = tx * 32 + cc.placeW / 2, y = ty * 32 + cc.placeH / 2;
            if (tx < 0 || ty < 0 || x + cc.placeW / 2 > map.width * 32 || y + cc.placeH / 2 > map.height * 32) continue;
            if (!map.boxWalkable(x - cc.placeW / 2, y - cc.placeH / 2, x + cc.placeW / 2 - 1, y + cc.placeH / 2 - 1))
                continue;
            bool clear = true;
            for (auto& r : map.units)
                if ((isMinerals(t, r.type) || isGeyser(t, r.type)) &&
                    placementGap(x, y, cc.placeW, cc.placeH, r.x, r.y, t.units[r.type].placeW, t.units[r.type].placeH) <
                        t.rules.baseResourceGap)
                    clear = false;
            if (!clear) continue;
            double score = 0;
            for (auto& f : group) score += dist(x, y, f.x, f.y);
            if (best < 0 || score < best) {
                best = score;
                outX = x;
                outY = y;
            }
        }
    return best >= 0;
}

}  // namespace

std::vector<MiningBase> miningBases(const Tables& t, const MapInfo& map, const MiningOptions& o) {
    std::vector<MiningBase> bases;
    auto baseNear = [&](int x, int y, double d) {
        for (auto& b : bases)
            if (dist(b.x, b.y, x, y) <= d) return true;
        return false;
    };
    for (auto& p : map.units)
        if (o.startLocations && isStart(t, p.type)) bases.push_back({p.x, p.y, true, {}, {}});
    for (auto& p : map.units)
        if (o.placedDepots && !isStart(t, p.type) && (t.units[p.type].flags & kDepot) &&
            !baseNear(p.x, p.y, t.rules.depotNear))
            bases.push_back({p.x, p.y, false, {}, {}});
    for (auto& [x, y] : o.bases) bases.push_back({x, y, false, {}, {}});
    std::vector<MapInfo::Placed> fields;
    for (auto& p : map.units)
        if (isMinerals(t, p.type)) fields.push_back(p);
    if (o.mineralClusters) {
        // Groups of fields (each within 6 tiles of another) with no base within 12 tiles.
        std::vector<int> group(fields.size(), -1);
        int groups = 0;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (group[i] >= 0 || baseNear(fields[i].x, fields[i].y, t.rules.fieldRange)) continue;
            std::vector<size_t> open = {i};
            group[i] = groups;
            while (!open.empty()) {
                size_t a = open.back();
                open.pop_back();
                for (size_t b = 0; b < fields.size(); ++b)
                    if (group[b] < 0 && !baseNear(fields[b].x, fields[b].y, t.rules.fieldRange) &&
                        dist(fields[a].x, fields[a].y, fields[b].x, fields[b].y) <= t.rules.clusterGap) {
                        group[b] = groups;
                        open.push_back(b);
                    }
            }
            ++groups;
        }
        for (int g = 0; g < groups; ++g) {
            std::vector<MapInfo::Placed> members;
            for (size_t i = 0; i < fields.size(); ++i)
                if (group[i] == g) members.push_back(fields[i]);
            int x = 0, y = 0;
            if (members.size() >= 3 && clusterSpot(t, map, members, x, y)) bases.push_back({x, y, false, {}, {}});
        }
    }
    // Each field goes to its nearest base (within 12 tiles), nearest first.
    for (auto& f : fields) {
        int best = -1;
        for (size_t b = 0; b < bases.size(); ++b)
            if (dist(bases[b].x, bases[b].y, f.x, f.y) <= t.rules.fieldRange &&
                (best < 0 || dist(bases[b].x, bases[b].y, f.x, f.y) < dist(bases[best].x, bases[best].y, f.x, f.y)))
                best = int(b);
        if (best >= 0) bases[best].fields.push_back({f.x, f.y});
    }
    for (auto& b : bases) {
        auto [dx, dy] = depotSpot(t, t.depotOf[1], b.x, b.y);
        std::stable_sort(b.fields.begin(), b.fields.end(), [&](auto& p, auto& q) {
            return dist(dx, dy, p.first, p.second) < dist(dx, dy, q.first, q.second);
        });
    }
    return bases;
}

// Each base's race (MiningOptions::mapRaces): a start location's base gets its player's race in the map (SIDE: 0 Zerg,
// 1 Terran, 2 Protoss; picked in the lobby or Random: a random race from the seed); the other bases, and all bases
// with mapRaces off, races[0].
std::vector<Race> baseRaces(const Tables& t, const MapInfo& map, const std::vector<MiningBase>& bases,
                             const MiningOptions& o) {
    Race first = o.races.empty() ? Terran : o.races[0];
    std::vector<Race> out(bases.size(), first);
    if (!o.mapRaces) return out;
    uint32_t state = o.seed;
    Race random[12];
    for (Race& r : random) {
        state = state * 22695477u + 1u;
        r = Race(((state >> 16) & 0x7fff) % 3);
    }
    for (size_t k = 0; k < bases.size(); ++k) {
        if (!bases[k].startLocation) continue;
        for (const MapInfo::Placed& p : map.units) {
            if (!isStart(t, p.type) || p.x != bases[k].x || p.y != bases[k].y || p.owner >= 12) continue;
            int side = size_t(p.owner) < map.sides.size() ? map.sides[size_t(p.owner)] : 5;
            out[k] = side <= 2 ? Race(side) : side == 5 || side == 6 ? random[p.owner] : first;
        }
    }
    return out;
}

void setupMining(World& w, const std::vector<MiningBase>& bases, const std::vector<Race>& races, int workers,
                 const MiningOptions& o) {
    const Tables& t = w.t;
    w.measureFrom = o.warmupFrames;
    for (auto& p : w.map.units) {
        if (!isMinerals(t, p.type) && !isGeyser(t, p.type)) continue;
        bool baseNear = false;
        for (auto& b : bases) baseNear = baseNear || dist(b.x, b.y, p.x, p.y) <= t.rules.resourceNearBase;
        if (baseNear) w.units[size_t(w.add(p.type, kNeutralPlayer, p.x, p.y))].resources = p.resources;
    }
    w.baseFields.assign(bases.size(), {});
    for (size_t k = 0; k < bases.size(); ++k) {
        const MiningBase& b = bases[k];
        int owner = int(k);
        Race race = k < races.size() ? races[k] : Terran;
        auto [dx, dy] = depotSpot(t, t.depotOf[race], b.x, b.y);
        w.add(t.depotOf[race], owner, dx, dy);
        for (auto& [fx, fy] : b.fields) {
            int idx = -1;
            for (size_t i = 0; i < w.units.size(); ++i)
                if (isMinerals(t, w.units[i].type) && w.units[i].x == fx && w.units[i].y == fy) idx = int(i);
            w.baseFields[k].push_back(idx);
        }
        int refinery = -1;
        if (o.gas) {
            for (size_t i = 0; i < w.units.size(); ++i)
                if (isGeyser(t, w.units[i].type) &&
                    dist(w.units[i].x, w.units[i].y, dx, dy) <= t.rules.resourceNearBase &&
                    (refinery < 0 || dist(w.units[i].x, w.units[i].y, dx, dy) <
                                         dist(w.units[refinery].x, w.units[refinery].y, dx, dy)))
                    refinery = int(i);
            if (refinery >= 0) {
                Unit& r = w.units[size_t(refinery)];
                r.type = t.refineryOf[race];
                r.owner = owner;
                r.anim.header = t.scriptHeader(t.flingyImage(t.units[r.type].flingy));
                w.startAnim(r.anim, kAnimInit);
            }
        }
        int gasWorkers = refinery >= 0 ? std::clamp(o.gasWorkers, 1, t.rules.gasWorkersMax) : 0;
        for (int n = 0; n < workers + gasWorkers; ++n) {
            int x = b.x, y = b.y;
            w.freeSpot(t.workerOf[race], x, y);
            int id = w.add(t.workerOf[race], owner, x, y);
            Unit& u = w.units[size_t(id)];
            u.base = owner;
            u.heading = u.velDir = direction(x - dx, y - dy);
            if (n >= workers) {
                u.field = refinery;
                w.setOrder(u, MoveToGas);
            } else {
                const std::vector<int>& fields = w.baseFields[k];
                u.field = fields.empty() ? -1 : fields[size_t(n) % fields.size()];
                w.setOrder(u, MoveToMinerals);
            }
        }
    }
    w.trips.assign(w.units.size(), {});
}

MiningRun miningRun(const World& w, int player, Race race, int workers, const MiningOptions& o) {
    MiningRun r;
    r.race = race;
    r.workers = workers;
    const World::Player& p = w.players[player];
    double minutes =
        (std::min(w.frame, o.warmupFrames + o.frames) - o.warmupFrames) * double(w.t.rules.frameMs) / 60000.0;
    if (p.measuredFrom >= 0 && minutes > 0) {
        r.mineralsPerMinute = (p.gathered - p.mineralsAtMeasure) / minutes;
        r.gasPerMinute = (p.gatheredGas - p.gasAtMeasure) / minutes;
    }
    if (size_t(player) < w.baseFields.size())
        for (int f : w.baseFields[size_t(player)]) {
            const World::Trip* trip = f >= 0 && size_t(f) < w.trips.size() ? &w.trips[size_t(f)] : nullptr;
            r.tripFrames.push_back(trip && trip->count ? trip->sum / trip->count : 0);
        }
    return r;
}

PlayerState playerState(const World& w, int player) {
    PlayerState s;
    const World::Player& p = w.players[player];
    s.player = player;
    s.minerals = p.minerals;
    s.gas = p.gas;
    s.gatheredMinerals = p.gathered;
    s.gatheredGas = p.gatheredGas;
    for (const Unit& u : w.units)
        if (u.alive && u.owner == player && (w.t.units[u.type].flags & kWorker)) ++s.workers;
    double minutes = (w.frame - w.measureFrom) * double(w.t.rules.frameMs) / 60000.0;
    if (p.measuredFrom >= 0 && minutes > 0) {
        s.mineralsPerMinute = (p.gathered - p.mineralsAtMeasure) / minutes;
        s.gasPerMinute = (p.gatheredGas - p.gasAtMeasure) / minutes;
    }
    for (const Unit& u : w.units)
        if (u.alive && u.owner == player) ++(u.building ? s.buildings : s.units);
    return s;
}

// The point where a worker touches a unit's box: the nearest point of that box grown by the worker's own size.
void World::depotBorder(const Unit& w, const Unit& b, int& x, int& y) const {
    const UnitData &dw = t.units[w.type], &db = t.units[b.type];
    x = std::clamp(w.x, b.x - db.left - dw.right - 1, b.x + db.right + dw.left + 1);
    y = std::clamp(w.y, b.y - db.up - dw.down - 1, b.y + db.down + dw.up + 1);
}

// Whether a resource is still one that the worker's order gathers (a refinery for gas, a mineral field).
bool World::resourceValid(const Unit& u, int f) const {
    if (f < 0 || !units[size_t(f)].alive) return false;
    int type = units[size_t(f)].type;
    return u.order >= MoveToGas ? t.refineryOf[0] == type || t.refineryOf[1] == type || t.refineryOf[2] == type
                                : isMinerals(t, type);
}

// To the point where the worker touches the resource, facing it.
void World::goToResource(Unit& u, int f) {
    int x, y;
    depotBorder(u, units[size_t(f)], x, y);
    setMove(u, x, y);
    u.aimX = units[size_t(f)].x;
    u.aimY = units[size_t(f)].y;
}

// The nearest mineral field within the range, not the worker's own (freeOnly: one that no worker mines); -1: none.
int World::findField(const Unit& u, int range, bool freeOnly) const {
    int best = -1;
    double bestDist = 0;
    for (size_t j = 0; j < units.size(); ++j) {
        const Unit& f = units[j];
        if (!f.alive || !isMinerals(t, f.type) || int(j) == u.field || (freeOnly && f.holder >= 0)) continue;
        double d = exactDistance(u.x, u.y, f.x, f.y);
        if (d <= range && (best < 0 || d < bestDist)) best = int(j), bestDist = d;
    }
    return best;
}

// The claim: the free resource is the worker's; else it waits in the resource's queue.
void World::claimResource(int w) {
    Unit& c = units[size_t(w)];
    Unit& f = units[size_t(c.field)];
    c.state = 2;
    if (f.holder < 0) {
        f.holder = w;
        setOrder(c, c.order == WaitForGas ? HarvestGas : MiningMinerals);
    } else if (std::find(f.queue.begin(), f.queue.end(), w) == f.queue.end()) {
        f.queue.push_back(w);
    }
}

// The resource is free again: the first worker of its queue that still waits for it claims it.
void World::releaseResource(int fi) {
    Unit& f = units[size_t(fi)];
    f.holder = -1;
    while (!f.queue.empty()) {
        int next = f.queue.front();
        f.queue.erase(f.queue.begin());
        Unit& n = units[size_t(next)];
        if (n.alive && n.field == fi && (n.order == WaitForMinerals || n.order == WaitForGas)) {
            claimResource(next);
            break;
        }
    }
}

// The worker takes its load from the resource; a mineral field shows the amount left, and an empty one is gone.
void World::extract(Unit& u, int fi) {
    Unit& f = units[size_t(fi)];
    int take = std::min(t.rules.mineralLoad, f.resources);
    f.resources -= take;
    // (its picture by the amount left: Working to Idle, Almost Built, Special State 2, 1)
    if (isMinerals(t, f.type))
        startAnim(f.anim, f.resources >= t.rules.mineralFull   ? kAnimWorkingToIdle
                          : f.resources >= t.rules.mineralHalf ? kAnimAlmostBuilt
                          : f.resources >= t.rules.mineralLow  ? kAnimSpecial2
                                                                : kAnimSpecial1);
    u.carry = take;
    u.carryGas = !isMinerals(t, f.type);
    if (isMinerals(t, f.type) && f.resources == 0) {
        f.alive = false;
        regrid(fi);
    }
}

// The worker's owner's nearest depot; -1: none.
int World::nearestDepot(const Unit& u) const {
    int best = -1;
    for (size_t j = 0; j < units.size(); ++j)
        if (units[j].alive && units[j].owner == u.owner && (t.units[units[j].type].flags & kDepot) &&
            (best < 0 || exactDistance(u.x, u.y, units[j].x, units[j].y) <
                             exactDistance(u.x, u.y, units[size_t(best)].x, units[size_t(best)].y)))
            best = int(j);
    return best;
}

void World::miningOrder(int i) {
    Unit& u = units[size_t(i)];
    if (u.order != HarvestGas) {  // (the 9-frame poll; the hidden gas order runs every frame)
        if (!tick(u)) return;
    }
    switch (u.order) {
    case MoveToMinerals:
    case MoveToGas: moveToResource(i); return;
    case WaitForMinerals:
    case WaitForGas:
        if (!resourceValid(u, u.field)) {
            setOrder(u, u.order >= MoveToGas ? MoveToGas : MoveToMinerals);
            return;
        }
        if (u.state == 0) claimResource(i);
        return;
    case MiningMinerals: mineMinerals(i); return;
    case HarvestGas: harvestGas(i); return;
    case ReturnMinerals:
    case ReturnGas: returnCargo(i); return;
    default: return;
    }
}

// Move to Minerals, Move to Gas: to the resource (another field when its own is held), then wait for it there.
void World::moveToResource(int i) {
    Unit& u = units[size_t(i)];
    bool gasOrder = u.order >= MoveToGas;
    if (u.order == MoveToMinerals && u.carry && u.carryGas) {
        setOrder(u, Idle);
        return;
    }
    if (u.state == 0) {
        if (!resourceValid(u, u.field)) u.field = gasOrder ? -1 : findField(u, t.rules.fieldRange, false);
        if (u.field < 0) {
            stopMove(u);
            setOrder(u, Idle);
            return;
        }
        goToResource(u, u.field);
        u.state = 1;
        return;
    }
    if (!resourceValid(u, u.field)) {
        u.state = 0;
        return;
    }
    if (u.state == 1) {
        if (gap(u, units[size_t(u.field)]) > t.rules.miningReach && !(u.x == u.moveX && u.y == u.moveY)) return;
        u.state = 3;  // (the arrival is its own state, on the next poll)
        return;
    }
    if (!gasOrder && units[size_t(u.field)].holder >= 0 && units[size_t(u.field)].holder != i) {
        int other = findField(u, t.rules.otherFieldRange, true);
        if (other >= 0) {
            u.field = other;
            goToResource(u, other);
            u.state = 1;
            return;
        }
    }
    if (u.carry) setOrder(u, gasOrder ? ReturnGas : ReturnMinerals);
    else setOrder(u, gasOrder ? WaitForGas : WaitForMinerals);
}

// Mining Minerals: it turns to the field, mines, takes its load and frees the field.
void World::mineMinerals(int i) {
    Unit& u = units[size_t(i)];
    if (!resourceValid(u, u.field)) {
        if (u.field >= 0) releaseResource(u.field);
        setOrder(u, MoveToMinerals);
        return;
    }
    if (u.state == 0) {
        if ((u.x == u.aimX && u.y == u.aimY) || u.heading == direction(u.aimX - u.x, u.aimY - u.y)) {
            animate(i, kAnimAlmostBuilt);  // (the mining pose)
            u.walkingAnim = 2;
            u.timer = t.rules.miningFrames;
            u.state = 5;
        }
        return;
    }
    if (u.timer > 0) return;
    animate(i, kAnimGroundAttackToIdle);
    extract(u, u.field);
    releaseResource(u.field);
    setOrder(u, ReturnMinerals);
}

// Harvest Gas: inside the refinery a while, then out on the side of the nearest depot with its load.
void World::harvestGas(int i) {
    Unit& u = units[size_t(i)];
    if (u.state == 0) {
        u.hidden = true;
        u.timer = t.rules.gasFrames;
        u.state = 5;
        return;
    }
    if (u.timer > 0) return;
    const Unit& r = units[size_t(u.field)];
    int depot = nearestDepot(u);
    int dir = depot >= 0 ? direction(units[size_t(depot)].x - r.x, units[size_t(depot)].y - r.y) : 128;
    Unit probe = u;
    probe.x = r.x + dirX(dir) * 4;
    probe.y = r.y + dirY(dir) * 4;
    depotBorder(probe, r, u.x, u.y);
    regrid(i);
    u.ex = u.x << 8;
    u.ey = u.y << 8;
    stopMove(u);
    u.hidden = false;
    extract(u, u.field);
    releaseResource(u.field);
    setOrder(u, ReturnGas);
}

// Return Minerals, Return Gas: to the nearest depot, where the load counts for its owner.
void World::returnCargo(int i) {
    Unit& u = units[size_t(i)];
    bool gasOrder = u.order >= MoveToGas;
    if (!u.carry) {
        setOrder(u, gasOrder ? MoveToGas : MoveToMinerals);
        return;
    }
    if (u.state == 0) {
        int depot = nearestDepot(u);
        if (depot < 0) {
            u.poll = t.rules.miningFrames;  // (no depot: it waits)
            return;
        }
        goToResource(u, depot);
        u.state = 1;
        return;
    }
    if (!(u.x == u.moveX && u.y == u.moveY)) return;
    if (u.state == 1) {  // (blocked by the depot it touches, it takes the depot again: one poll more)
        u.state = 2;
        return;
    }
    Player& p = players[u.owner];
    if (u.carryGas) {
        p.gas += u.carry;
        p.gatheredGas += u.carry;
    } else {
        p.minerals += u.carry;
        p.gathered += u.carry;
        if (u.lastDelivery >= measureFrom && u.field >= 0 && size_t(u.field) < trips.size()) {
            trips[size_t(u.field)].sum += frame - u.lastDelivery;
            ++trips[size_t(u.field)].count;
        }
        u.lastDelivery = frame;
    }
    u.carry = 0;
    setOrder(u, gasOrder ? MoveToGas : MoveToMinerals);
}

}  // namespace sim
