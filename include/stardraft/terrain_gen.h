#pragma once

// Procedural terrain for every tileset: layered simplex noise cut at quantiles into terrain shares,
// shaped the way StarEdit paints (whole diamonds, rims, separate strokes) and applied through the ISOM
// correction engine, so generated maps have valid isometric terrain, transitions and cliffs. Noise and
// quantile cuts after PUDForge's map generator (github.com/pudforge/pudforge, src/PUDForgeCore/noise.cpp
// and pf_map_generate).

#include <cstdint>
#include <string>
#include <vector>

#include "stardraft/chk.h"

namespace stardraft {

struct Tileset;

// 2D simplex noise in [-1, 1] from a seeded permutation (Ken Perlin's simplex grid).
class SimplexNoise {
public:
    explicit SimplexNoise(uint32_t seed);
    float at(float x, float y) const;

private:
    uint8_t perm_[512];
};

// Independent octaves summed and normalized to [0, 1].
struct NoiseLayer {
    float scale;  // features per tile; smaller is broader
    uint32_t seed;
    float weight;
};
class LayeredNoise {
public:
    explicit LayeredNoise(std::vector<NoiseLayer> layers);
    float at(float x, float y) const;

private:
    std::vector<NoiseLayer> layers_;
    std::vector<SimplexNoise> noises_;
    float total_ = 0;
};

// How the generator uses a tileset's terrains, all derived from data:
//  - the terrain tree (IsomTerrainTable) rooted at its most connected terrain, the base ground;
//  - walkability and elevation from the VF4 flags of the tile group Blizzard's maps put under each
//    solid terrain (solidTileGroup);
//  - default shares: the terrain's average share of Blizzard's maps of that tileset.
// The sea is the unwalkable terrain beside the base with the largest share, the high ground the
// raised one; every other terrain is a patch inside its parent.
struct GenTerrain {
    enum Role : uint8_t { Base, Sea, High, Patch };
    uint16_t id = 0, parent = 0;  // parent 0: the base
    int depth = 0;                // steps from the base
    Role role = Patch;
    bool walkable = true;
    int level = 0;                // 0 low, 1 mid, 2 high (VF4)
    float defaultShare = 0;       // of the map, this terrain alone
    // Built terrain (structure, plating, temples): Blizzard's maps give its border with its parent
    // mostly straight-edge shapes (55% or more, natural terrain 40-48%), so it is placed as blocks.
    bool blocky = false;
    const char* name = "";
};
std::vector<GenTerrain> generatorTerrains(uint16_t era, const Tileset& tileset);

enum class GenSymmetry { None, LeftRight, TopBottom, Both, Rotate180 };

struct TerrainGenParams {
    uint16_t width = 128, height = 128;  // tiles; width/2 and height must be even for exact symmetry
    uint16_t era = 0;                    // tileset
    uint32_t seed = 12345;
    // Share of the map per terrain id (index = id); negative or missing: the tileset's default.
    std::vector<float> shares;
    // Octave scales in thousandths of a feature per tile (as in PUDForge): broad land shape,
    // coastline roughness, fine detail.
    int landScale = 30, coastline = 75, detail = 180;
    // How strongly the coastline and detail octaves bend the outlines of sea and high ground (0..1).
    float roughness = 0.35f;
    // Smallest feature, in brush sizes: 1 builds terrain from whole diamonds as StarEdit's smallest
    // brush paints (rims a diamond wide, built terrain in 3x3 blocks); 0.5 allows single corners (thin
    // ridges and spurs, one-step rims, smaller blocks), like the half-size brush.
    float minFeature = 1.0f;
    GenSymmetry symmetry = GenSymmetry::None;
    // Rules (roadmap R49). `bases`: that many bases evenly round the map (with a symmetry, as many as it keeps the
    // same: an even number when it turns or mirrors both ways), each on open ground (no sea, cliffs or patches)
    // `generatorBaseRadius` tiles round. `paths`: ground joins each base to the next (no sea, no cliffs on it).
    // `basesHigh`: the bases and the paths are high ground; else they are low ground.
    int bases = 0;
    bool paths = true, basesHigh = false;
};

// Where the rules put the bases: the middle of each, in tiles (the generator's order: round the map).
struct GenBase {
    int x = 0, y = 0;
};
std::vector<GenBase> generatorBases(const TerrainGenParams& params);
int generatorBaseRadius(const TerrainGenParams& params);
constexpr int kGenPathHalfWidth = 4;  // tiles each side of a path's middle line (up and down; across, 1/0.6 times that)

// Generate terrain into `chk`, which must already hold DIM, ERA and ISOM for `tileset` (e.g. from
// IsomGrid::create or createNewScenario). TILE and MTXM are regenerated.
bool generateTerrain(Chk& chk, const Tileset& tileset, const TerrainGenParams& params, std::string& error);

}  // namespace stardraft
