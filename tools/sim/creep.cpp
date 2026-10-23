// The live game's creep (the creep rules): the start creep of the map's buildings (stardraft/creep.h), its growth
// from each working creep maker and its recession when a maker dies. Creep changes nothing in the play here; it draws
// its own random numbers.
#include <algorithm>
#include <cstdint>
#include <vector>

#include "stardraft/creep.h"
#include "world.h"

namespace sim {

namespace {

stardraft::CreepBuilding creepBuilding(const Tables& t, const Unit& u) {
    const UnitData& d = t.units[size_t(u.type)];
    stardraft::CreepBuilding b;
    b.x = u.x;
    b.y = u.y;
    b.placeW = d.placeW;
    b.placeH = d.placeH;
    b.maker = stardraft::makesCreep(uint16_t(u.type));
    b.requiresCreep = (d.flags & kRequiresCreep) != 0;
    return b;
}

}  // namespace

int World::creepRandom() {
    creepSeed = creepSeed * 22695477u + 1u;
    return int((creepSeed >> 16) & 0x7fff);
}

uint8_t World::creepNeighbours(int x, int y) const {
    return stardraft::creepNeighbours(creep, map.width, map.height, x, y);
}

// The start creep: the map's own creep tiles, each creep maker's area, the footprints of the buildings that require
// creep (all at once, as the game gives a map's buildings their creep).
void World::startCreep() {
    std::vector<stardraft::CreepBuilding> buildings;
    for (const Unit& u : units)
        if (u.alive && u.building) buildings.push_back(creepBuilding(t, u));
    creep = stardraft::startCreep(map.width, map.height, map.creepCarriers, buildings, map.paintedCreep);
    creepGrowth.assign(units.size(), 0);
    receding.clear();
    ++creepChanges;
}

// A creep maker that is finished in the game: its footprint and (with units.dat's flag 0x80000000) a margin of 2
// tiles left and right and 1 above and below without the four corners, then its growth.
void World::creepComplete(int unit) {
    const Unit& u = units[size_t(unit)];
    const stardraft::TileRect r = stardraft::footprintOf(creepBuilding(t, u));
    const bool margin = (t.units[size_t(u.type)].flags & kCreepMargin) != 0;
    for (int y = r.y - (margin ? 1 : 0); y < r.y + r.h + (margin ? 1 : 0); ++y)
        for (int x = r.x - (margin ? 2 : 0); x < r.x + r.w + (margin ? 2 : 0); ++x) {
            const bool corner = (y < r.y || y >= r.y + r.h) && (x < r.x - 1 || x >= r.x + r.w + 1);
            if (corner || x < 0 || y < 0 || x >= map.width || y >= map.height) continue;
            const bool own = x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
            if (own || map.creepCarriers[size_t(y) * size_t(map.width) + size_t(x)])
                creep[size_t(y) * size_t(map.width) + size_t(x)] = 1;
        }
    if (creepGrowth.size() < units.size()) creepGrowth.resize(units.size(), 0);
    creepGrowth[size_t(unit)] = 0;  // (the next frame adds its first tile)
    ++creepChanges;
}

// A frame of creep: each working maker adds a tile every `creepGrowFrames` (the candidate inside its area with the
// most creep neighbours; ties: a random one); receding creep loses a tile every few frames (the fewest neighbours).
void World::creepStep() {
    if (creep.empty()) return;
    const int w = map.width, h = map.height;
    if (creepGrowth.size() < units.size()) creepGrowth.resize(units.size(), 0);
    // (the tiles that buildings stand on: no growth there)
    std::vector<uint8_t> occupied;
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (!u.alive || !stardraft::makesCreep(uint16_t(u.type)) || --creepGrowth[i] > 0) continue;
        creepGrowth[i] = t.rules.creepGrowFrames;
        if (occupied.empty()) {
            occupied.assign(size_t(w) * size_t(h), 0);
            for (const Unit& o : units)
                if (o.alive && o.building) {
                    const stardraft::TileRect r = stardraft::footprintOf(creepBuilding(t, o));
                    for (int y = std::max(0, r.y); y < std::min(h, r.y + r.h); ++y)
                        for (int x = std::max(0, r.x); x < std::min(w, r.x + r.w); ++x) occupied[size_t(y) * w + x] = 1;
                }
        }
        const int cx = u.x / 32, cy = u.y / 32;
        const int rx = stardraft::kCreepHalfWidth / 32 + 1, ry = stardraft::kCreepHalfHeight / 32 + 1;
        int best = -1, count = 0;
        std::vector<int> ties;
        for (int y = std::max(0, cy - ry); y <= std::min(h - 1, cy + ry); ++y)
            for (int x = std::max(0, cx - rx); x <= std::min(w - 1, cx + rx); ++x) {
                const size_t k = size_t(y) * w + x;
                if (creep[k] || occupied[k] || !map.creepCarriers[k] || !stardraft::inCreepArea(u.x, u.y, x, y))
                    continue;
                const uint8_t nb = creepNeighbours(x, y);
                int n = 0;
                for (int b = 0; b < 8; ++b) n += (nb >> b) & 1;
                if (!n || n < count) continue;
                if (n > count) ties.clear(), count = n;
                ties.push_back(int(k));
            }
        if (ties.empty()) continue;  // (none: it tries again later)
        best = ties.size() == 1 ? ties[0] : ties[size_t(creepRandom()) % ties.size()];
        creep[size_t(best)] = 1;
        ++creepChanges;
    }
    // Recession: one tile in a step whose length depends on how many tiles recede.
    if (!receding.empty() && frame >= recedeFrom && --recedeWait <= 0) {
        int fewest = 9;
        std::vector<size_t> ties;
        for (size_t k = 0; k < receding.size(); ++k) {
            const int tile = receding[k];
            const uint8_t nb = creepNeighbours(tile % w, tile / w);
            int n = 0;
            for (int b = 0; b < 8; ++b) n += (nb >> b) & 1;
            if (n > fewest) continue;
            if (n < fewest) ties.clear(), fewest = n;
            ties.push_back(k);
        }
        const size_t pick = ties.size() == 1 ? ties[0] : ties[size_t(creepRandom()) % ties.size()];
        creep[size_t(receding[pick])] = 0;
        receding.erase(receding.begin() + std::ptrdiff_t(pick));
        ++creepChanges;
        recedeWait = recedeStep(int(receding.size()));
    }
}

// The frames between two removals with `count` tiles receding (rules.ini creepRecedeSteps: up to a count, its frames).
int World::recedeStep(int count) const {
    const std::vector<int>& steps = t.rules.creepRecedeSteps;
    for (size_t k = 0; k + 1 < steps.size(); k += 2)
        if (count <= steps[k]) return steps[k + 1];
    return steps.empty() ? 1 : steps.back();
}

// A creep maker died: its area's creep and its footprint's recede, but for the tiles that another living Zerg building
// keeps (a maker's area, another building's footprint).
void World::creepMakerDied(int unit) {
    if (creep.empty()) return;
    const Unit& dead = units[size_t(unit)];
    const stardraft::CreepBuilding b = creepBuilding(t, dead);
    const stardraft::TileRect r = stardraft::footprintOf(b);
    const int w = map.width, h = map.height;
    auto kept = [&](int x, int y) {
        for (size_t i = 0; i < units.size(); ++i) {
            const Unit& o = units[i];
            if (int(i) == unit || !o.alive || !o.building || !(t.units[size_t(o.type)].group & kGroupZerg)) continue;
            const stardraft::CreepBuilding ob = creepBuilding(t, o);
            if (ob.maker && stardraft::inCreepArea(ob.x, ob.y, x, y)) return true;
            const stardraft::TileRect orr = stardraft::footprintOf(ob);
            if (x >= orr.x && x < orr.x + orr.w && y >= orr.y && y < orr.y + orr.h) return true;
        }
        return false;
    };
    const int rx = stardraft::kCreepHalfWidth / 32 + 1, ry = stardraft::kCreepHalfHeight / 32 + 1;
    for (int y = std::max(0, dead.y / 32 - ry); y <= std::min(h - 1, dead.y / 32 + ry); ++y)
        for (int x = std::max(0, dead.x / 32 - rx); x <= std::min(w - 1, dead.x / 32 + rx); ++x) {
            const int k = y * w + x;
            const bool mine = stardraft::inCreepArea(b.x, b.y, x, y) ||
                              (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
            if (!mine || !creep[size_t(k)] || kept(x, y)) continue;
            if (std::find(receding.begin(), receding.end(), k) == receding.end()) receding.push_back(k);
        }
    if (recedeFrom <= frame) recedeFrom = frame + t.rules.creepRecedeDelay;
    recedeWait = 0;
}

}  // namespace sim
