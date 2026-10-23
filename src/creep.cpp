// Zerg creep (creep.h): the tiles that can carry it, the start creep of a map's buildings, and its drawing values.
#include "stardraft/creep.h"

#include <algorithm>
#include <array>
#include <deque>
#include <string>

namespace stardraft {

namespace {

// Unit ids of the creep makers.
constexpr uint16_t kHatchery = 131, kLair = 132, kHive = 133, kCreepColony = 143, kSporeColony = 144,
                   kSunkenColony = 146;

// The edge frame by the neighbour bits: row = the high nibble (W, NE, N, NW), column = the low nibble (SE, S, SW, E).
constexpr std::array<std::array<uint8_t, 16>, 16> kEdgeFrames = {{
    {0, 0, 7, 18, 0, 0, 12, 22, 16, 16, 20, 20, 16, 16, 34, 34},
    {5, 5, 13, 32, 5, 5, 13, 32, 28, 28, 35, 35, 28, 28, 35, 35},
    {0, 0, 7, 18, 0, 0, 12, 22, 16, 16, 20, 20, 16, 16, 34, 34},
    {5, 5, 13, 32, 5, 5, 13, 32, 28, 28, 35, 35, 28, 28, 35, 35},
    {1, 1, 8, 19, 1, 1, 14, 23, 17, 17, 21, 21, 17, 17, 36, 36},
    {6, 6, 15, 33, 6, 6, 15, 33, 29, 29, 37, 37, 29, 29, 37, 37},
    {3, 3, 10, 21, 3, 3, 25, 36, 17, 17, 21, 21, 17, 17, 36, 36},
    {24, 24, 26, 37, 24, 24, 26, 37, 29, 29, 37, 37, 29, 29, 37, 37},
    {0, 0, 7, 18, 0, 0, 12, 22, 16, 16, 20, 20, 16, 16, 34, 34},
    {5, 5, 13, 32, 5, 5, 13, 32, 28, 28, 35, 35, 28, 28, 35, 35},
    {0, 0, 7, 18, 0, 0, 12, 22, 16, 16, 20, 20, 16, 16, 34, 34},
    {5, 5, 13, 32, 5, 5, 13, 32, 28, 28, 35, 35, 28, 28, 35, 35},
    {2, 2, 9, 30, 2, 2, 15, 33, 27, 27, 31, 31, 27, 27, 37, 37},
    {6, 6, 15, 33, 6, 6, 15, 33, 29, 29, 37, 37, 29, 29, 37, 37},
    {4, 4, 11, 31, 4, 4, 26, 37, 27, 27, 31, 31, 27, 27, 37, 37},
    {24, 24, 26, 37, 24, 24, 26, 37, 29, 29, 37, 37, 29, 29, 37, 37},
}};

}  // namespace

bool makesCreep(uint16_t type, bool complete) {
    switch (type) {
    case kHatchery:
    case kCreepColony: return complete;
    case kLair:
    case kHive:
    case kSunkenColony:
    case kSporeColony: return true;
    default: return false;
    }
}

bool inCreepArea(int centreX, int centreY, int tileX, int tileY) {
    const long long dx = tileX * 32 + 16 - centreX, dy = tileY * 32 + 16 - centreY;
    const long long a = kCreepHalfWidth, b = kCreepHalfHeight;
    return dx * dx * b * b + dy * dy * a * a <= a * a * b * b;
}

TileRect footprintOf(const CreepBuilding& b) {
    return {(b.x - b.placeW / 2) / 32, (b.y - b.placeH / 2) / 32, std::max(1, b.placeW / 32),
            std::max(1, b.placeH / 32)};
}

std::vector<uint8_t> creepCarriers(int width, int height, const std::vector<uint8_t>& unbuildable,
                                   const std::vector<uint8_t>& partlyWalkable) {
    std::vector<uint8_t> out(size_t(width) * size_t(height), 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = size_t(y) * size_t(width) + size_t(x);
            const bool southUnbuildable = y + 1 < height && unbuildable[i + size_t(width)];
            out[i] = !unbuildable[i] && !partlyWalkable[i] && !southUnbuildable;
        }
    return out;
}

void unbuildableMapEdges(int width, int height, std::vector<uint8_t>& unbuildable) {
    for (int x = 0; x < width && height > 0; ++x) {
        unbuildable[size_t(height - 1) * size_t(width) + size_t(x)] = 1;
        if (height > 1 && (x < 5 || x >= width - 5)) unbuildable[size_t(height - 2) * size_t(width) + size_t(x)] = 1;
    }
}

std::vector<uint8_t> creepCarriers(const Tileset& tileset, const TerrainLayer& terrain) {
    const size_t n = size_t(terrain.width) * size_t(terrain.height);
    std::vector<uint8_t> unbuildable(n, 1), partly(n, 0);
    for (size_t i = 0; i < n && i < terrain.tiles.size(); ++i) {
        const TileGroup* g = tileset.groupFor(terrain.tiles[i]);
        unbuildable[i] = !g || (g->buildability & Tileset::kUnbuildable);
        const int mega = tileset.megatileFor(terrain.tiles[i]);
        if (mega >= 0 && size_t(mega) < tileset.minitileFlags.size()) {
            int walkable = 0;
            for (uint16_t f : tileset.minitileFlags[size_t(mega)]) walkable += (f & Tileset::kWalkable) ? 1 : 0;
            partly[i] = walkable > 0 && walkable < 16;
        }
    }
    unbuildableMapEdges(terrain.width, terrain.height, unbuildable);
    return creepCarriers(terrain.width, terrain.height, unbuildable, partly);
}

std::vector<uint8_t> startCreep(int width, int height, const std::vector<uint8_t>& carriers,
                                const std::vector<CreepBuilding>& buildings, const std::vector<uint8_t>& painted) {
    const size_t n = size_t(width) * size_t(height);
    std::vector<uint8_t> creep(n, 0);
    if (painted.size() == n) creep = painted;
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < width && y < height; };
    auto at = [&](int x, int y) { return size_t(y) * size_t(width) + size_t(x); };
    // (the tiles that buildings stand on: by building, so a building's own footprint does not block its creep)
    std::vector<int> occupant(n, -1);
    for (size_t k = 0; k < buildings.size(); ++k) {
        const TileRect r = footprintOf(buildings[k]);
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x)
                if (inside(x, y)) occupant[at(x, y)] = int(k);
    }
    std::vector<uint8_t> seen(n, 0);
    std::vector<size_t> marked;
    for (size_t k = 0; k < buildings.size(); ++k) {
        const CreepBuilding& b = buildings[k];
        const TileRect r = footprintOf(b);
        if (!b.maker) {  // (a building that requires creep: its footprint, where creep can be)
            if (!b.requiresCreep) continue;
            for (int y = r.y; y < r.y + r.h; ++y)
                for (int x = r.x; x < r.x + r.w; ++x)
                    if (inside(x, y) && carriers[at(x, y)]) creep[at(x, y)] = 1;
            continue;
        }
        for (size_t i : marked) seen[i] = 0;  // (the last maker's marks only)
        marked.clear();
        std::deque<std::pair<int, int>> open;
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x)
                if (inside(x, y)) {
                    creep[at(x, y)] = 1;
                    seen[at(x, y)] = 1;
                    marked.push_back(at(x, y));
                    open.push_back({x, y});
                }
        while (!open.empty()) {
            const auto [x, y] = open.front();
            open.pop_front();
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if ((!dx && !dy) || !inside(nx, ny) || seen[at(nx, ny)]) continue;
                    seen[at(nx, ny)] = 1;
                    marked.push_back(at(nx, ny));
                    const int other = occupant[at(nx, ny)];
                    if (!inCreepArea(b.x, b.y, nx, ny) || !carriers[at(nx, ny)] || (other >= 0 && other != int(k)))
                        continue;
                    creep[at(nx, ny)] = 1;
                    open.push_back({nx, ny});
                }
        }
    }
    return creep;
}

std::vector<uint8_t> startCreep(const Tileset& tileset, const TerrainLayer& terrain, const std::vector<Unit>& units,
                                const UnitTypes& types) {
    std::vector<CreepBuilding> buildings;
    for (const Unit& u : units) {
        if (u.type >= types.size()) continue;
        const UnitType& t = types[u.type];
        if (!t.is(UnitFlag::Building) && !t.is(UnitFlag::ResourceContainer)) continue;
        CreepBuilding b;
        b.x = u.x;
        b.y = u.y;
        b.placeW = t.placementWidth;
        b.placeH = t.placementHeight;
        b.maker = makesCreep(u.type);
        b.requiresCreep = t.is(UnitFlag::RequiresCreep);
        buildings.push_back(b);
    }
    const size_t n = size_t(terrain.width) * size_t(terrain.height);
    std::vector<uint8_t> painted(n, 0);
    for (size_t i = 0; i < n && i < terrain.tiles.size(); ++i) painted[i] = (terrain.tiles[i] >> 4) == kCreepGroup;
    return startCreep(terrain.width, terrain.height, creepCarriers(tileset, terrain), buildings, painted);
}

uint8_t creepNeighbours(const std::vector<uint8_t>& creep, int width, int height, int x, int y) {
    auto has = [&](int dx, int dy) {
        const int nx = x + dx, ny = y + dy;
        return nx >= 0 && ny >= 0 && nx < width && ny < height && creep[size_t(ny) * size_t(width) + size_t(nx)];
    };
    return uint8_t((has(1, 1) ? 1 : 0) | (has(0, 1) ? 2 : 0) | (has(-1, 1) ? 4 : 0) | (has(1, 0) ? 8 : 0) |
                   (has(-1, 0) ? 16 : 0) | (has(1, -1) ? 32 : 0) | (has(0, -1) ? 64 : 0) | (has(-1, -1) ? 128 : 0));
}

int creepEdgeFrame(uint8_t neighbours) { return kEdgeFrames[neighbours >> 4][neighbours & 15]; }

int creepVariant(uint32_t& seed) {
    auto next = [&] {
        seed = seed * 22695477u + 1u;
        return int((seed >> 16) & 0x7fff);
    };
    return next() % 100 < 4 ? 6 + next() % 7 : next() % 6;
}

void applyCreep(TerrainLayer& terrain, const std::vector<uint8_t>& creep, const std::vector<uint8_t>& variants) {
    for (size_t i = 0; i < terrain.tiles.size() && i < creep.size(); ++i)
        if (creep[i]) terrain.tiles[i] = uint16_t(kCreepGroup << 4 | (i < variants.size() ? variants[i] & 15 : 0));
}

std::string creepGrpPath(uint16_t era) { return std::string("tileset\\") + tilesetName(era) + ".grp"; }

}  // namespace stardraft
