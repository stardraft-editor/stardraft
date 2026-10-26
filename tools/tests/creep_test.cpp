// The creep rules (src/creep.cpp) on synthetic maps: the start shapes of a Hatchery and the colonies against the
// rules' tables, a Spawning Pool's footprint, unbuildable tiles, two makers that overlap, the edge frames and the
// variants.
#include <cstdio>
#include <string>
#include <vector>

#include "stardraft/creep.h"

using namespace stardraft;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", what.c_str(), ok ? "OK" : "FAIL");
    if (!ok) ++failures;
}

constexpr int W = 64, H = 64;

int count(const std::vector<uint8_t>& creep) {
    int n = 0;
    for (uint8_t c : creep) n += c;
    return n;
}

// A building with its footprint's top-left tile at (tx, ty) and its size in tiles.
CreepBuilding building(int tx, int ty, int w, int h, bool maker, bool needsCreep = false) {
    CreepBuilding b;
    b.placeW = w * 32;
    b.placeH = h * 32;
    b.x = tx * 32 + b.placeW / 2;
    b.y = ty * 32 + b.placeH / 2;
    b.maker = maker;
    b.requiresCreep = needsCreep;
    return b;
}

// The creep round a footprint against a shape of the rules ('#', 'F': creep; '.': none), its top-left at
// (fx - left, fy - top); and no creep outside the shape.
bool matches(const std::vector<uint8_t>& creep, int fx, int fy, int left, int top, const std::vector<std::string>& shape) {
    int inShape = 0;
    for (size_t r = 0; r < shape.size(); ++r)
        for (size_t c = 0; c < shape[r].size(); ++c) {
            const int x = fx - left + int(c), y = fy - top + int(r);
            const bool want = shape[r][c] != '.';
            if (bool(creep[size_t(y) * W + size_t(x)]) != want) return false;
            inShape += want;
        }
    return count(creep) == inShape;
}

}  // namespace

int main() {
    const std::vector<uint8_t> open(size_t(W) * H, 1);
    // Hatchery (4x3): 200 tiles, the rules' shape.
    {
        const std::vector<std::string> shape = {
            "..........######..........", ".......############.......", ".....################.....",
            "....##################....", "....##################....", "...########FFFF########...",
            "...########FFFF########...", "...########FFFF########...", "....##################....",
            "....##################....", ".....################.....", ".......############.......",
            "..........######..........",
        };
        std::vector<uint8_t> creep = startCreep(W, H, open, {building(20, 20, 4, 3, true)});
        check(count(creep) == 200 && matches(creep, 20, 20, 11, 5, shape), "Hatchery: 200 tiles in the rules' shape");
    }
    // Creep Colony, Sunken Colony, Spore Colony (2x2): 196 tiles.
    {
        const std::vector<std::string> shape = {
            ".......##########.......", ".....##############.....", "....################....",
            "...##################...", "..####################..", "..#########FF#########..",
            "..#########FF#########..", "..####################..", "...##################...",
            "....################....", ".....##############.....", ".......##########.......",
        };
        std::vector<uint8_t> creep = startCreep(W, H, open, {building(30, 30, 2, 2, true)});
        check(count(creep) == 196 && matches(creep, 30, 30, 11, 5, shape), "Colony: 196 tiles in the rules' shape");
        check(makesCreep(143) && makesCreep(146) && makesCreep(144) && makesCreep(131) && !makesCreep(142) &&
                  !makesCreep(143, false) && makesCreep(146, false),
              "Makers by type (Creep Colony and Hatchery only complete)");
    }
    // Spawning Pool (3x2, requires creep): its footprint only.
    {
        std::vector<uint8_t> creep = startCreep(W, H, open, {building(10, 10, 3, 2, false, true)});
        check(count(creep) == 6, "Spawning Pool: 6 footprint tiles");
        check(count(startCreep(W, H, open, {building(10, 10, 4, 3, false, false)})) == 0,
              "Infested Command Center (no creep required): none");
    }
    // Unbuildable tiles: no creep on them, nor north of them; partly walkable tiles: none.
    {
        std::vector<uint8_t> unbuildable(size_t(W) * H, 0), partly(size_t(W) * H, 0);
        unbuildable[size_t(25) * W + 27] = 1;  // (east of the Hatchery, inside its area)
        partly[size_t(19) * W + 22] = 1;       // (north of it)
        std::vector<uint8_t> carriers = creepCarriers(W, H, unbuildable, partly);
        std::vector<uint8_t> creep = startCreep(W, H, carriers, {building(20, 20, 4, 3, true)});
        check(!creep[size_t(25) * W + 27] && !creep[size_t(24) * W + 27] && creep[size_t(26) * W + 27] &&
                  !creep[size_t(19) * W + 22] && count(creep) == 197,
              "Unbuildable, north of unbuildable and partly walkable tiles: no creep");
        check(carriers[size_t(H - 1) * W + 5] == 1, "The bottom row passes the south test");
    }
    // Two makers that overlap: the union; a building in the way blocks the spread, not its own footprint.
    {
        std::vector<uint8_t> one = startCreep(W, H, open, {building(20, 20, 4, 3, true)});
        std::vector<uint8_t> two = startCreep(W, H, open, {building(26, 20, 4, 3, true)});
        std::vector<uint8_t> both = startCreep(W, H, open, {building(20, 20, 4, 3, true), building(26, 20, 4, 3, true)});
        int expected = 0;
        for (size_t i = 0; i < both.size(); ++i) expected += one[i] || two[i];
        check(count(both) == expected, "Two Hatcheries 6 tiles apart: the union (" + std::to_string(expected) + ")");
    }
    // Edge frames and variants.
    {
        check(creepEdgeFrame(0x02) == 7 && creepEdgeFrame(0x01) == 0 && creepEdgeFrame(0xFF) == 37 &&
                  creepEdgeFrame(0x40) == 1 && creepEdgeFrame(0x10) == 5,
              "Edge frames: S 7, SE alone 0, all 37, N 1, W 5");
        std::vector<uint8_t> creep(size_t(W) * H, 0);
        creep[size_t(10) * W + 10] = 1;
        check(creepNeighbours(creep, W, H, 10, 9) == 0x02 && creepNeighbours(creep, W, H, 11, 11) == 0x80,
              "Neighbour bits: S below, NW above-left");
        uint32_t seed = 42;
        int rare = 0, bad = 0;
        for (int k = 0; k < 10000; ++k) {
            int v = creepVariant(seed);
            rare += v >= 6;
            bad += v < 0 || v > 12;
        }
        check(bad == 0 && rare > 200 && rare < 600, "Variants 0 to 12, about 4% from 6 to 12 (" + std::to_string(rare) +
                                                      " of 10000)");
    }
    std::printf(failures ? "FAILED\n" : "All creep checks passed.\n");
    return failures ? 1 : 0;
}
