// The isometric terrain core (IsomCore: IsomTerrain, third_party/IsomTerrain) against StarEdit: IsomTerrain's own
// reference brush tests (isomterrain_cases.inc). Each paints its strokes on a source map and compares the ISOM, and
// the tile groups of TILE and MTXM, with the map ScmDraft (StarEdit's brush) made with the same strokes.
//
// usage: isomterrain_test <StarCraft install> <IsomTerrain's "Map Testing Pack" folder>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "IsomApi.h"  // (the brush ids of the cases: Sc::Isom::Brush)
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

namespace {

struct Op {
    size_t terrainType, x, y, extent;
};
struct Case {
    const char* source;
    const char* expected;
    std::vector<Op> ops;
};
const std::vector<Case> kCases = {
#include "isomterrain_cases.inc"
};

std::vector<uint16_t> u16s(const stardraft::Chk& chk, SectionName name) {
    const Bytes b = chk.effective(name);
    std::vector<uint16_t> v(b.size() / 2);
    for (size_t i = 0; i < v.size(); ++i) v[i] = uint16_t(b[2 * i] | b[2 * i + 1] << 8);
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: isomterrain_test <StarCraft install> <Map Testing Pack folder>\n");
        return 2;
    }
    GameData data;
    std::string err;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "game data: %s\n", err.c_str());
        return 2;
    }
    const fs::path pack = argv[2];
    Tileset tilesets[8];
    int pass = 0, fail = 0, oldPerfect = 0, oldImperfect = 0;
    double paintMs = 0;
    long strokes = 0;
    for (const Case& c : kCases) {
        Bytes raw, rawExpected;
        if (!readMapChk(pack / c.source, raw, err) || !readMapChk(pack / c.expected, rawExpected, err)) {
            std::printf("FAIL  %s: %s\n", c.expected, err.c_str());
            ++fail;
            continue;
        }
        stardraft::Chk chk = stardraft::Chk::parse(raw), expected = stardraft::Chk::parse(rawExpected);
        const uint16_t era = uint16_t(readEra(chk) & 7);
        if (tilesets[era].groups.empty() && !tilesets[era].load(data, era, err)) {
            std::printf("FAIL  %s: tileset: %s\n", c.expected, err.c_str());
            ++fail;
            continue;
        }
        IsomCore core;
        if (!core.load(chk, tilesets[era], err)) {
            std::printf("FAIL  %s: %s\n", c.expected, err.c_str());
            ++fail;
            continue;
        }
        bool painted = true;
        const auto start = std::chrono::steady_clock::now();
        for (const Op& op : c.ops) {
            const uint16_t isomValue = Sc::Isom::tilesetTerrainTypes[era][op.terrainType].isomValue;
            painted = core.paint(int(op.x), int(op.y), isomValue, int(op.extent)) && painted;
            core.updateTiles();
        }
        paintMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        strokes += long(c.ops.size());
        core.store(chk);
        const auto isom = u16s(chk, "ISOM"), isomExpected = u16s(expected, "ISOM");
        size_t isomDiff = isom.size() == isomExpected.size() ? 0 : std::max(isom.size(), isomExpected.size());
        for (size_t i = 0; i < std::min(isom.size(), isomExpected.size()); ++i) isomDiff += isom[i] != isomExpected[i];
        size_t groupDiff[2] = {};
        const char* names[2] = {"TILE", "MTXM"};
        for (int s = 0; s < 2; ++s) {
            const auto a = u16s(chk, names[s] == std::string("TILE") ? SectionName("TILE") : SectionName("MTXM"));
            const auto b = u16s(expected, names[s] == std::string("TILE") ? SectionName("TILE") : SectionName("MTXM"));
            groupDiff[s] = a.size() == b.size() ? 0 : std::max(a.size(), b.size());
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) groupDiff[s] += (a[i] >> 4) != (b[i] >> 4);
        }
        // The old engine (IsomGrid: corners and learned tile rules) on the same strokes, for comparison only.
        stardraft::Chk old = stardraft::Chk::parse(raw);
        IsomGrid grid;
        size_t oldIsomDiff = 0;
        if (grid.load(old, err)) {
            for (const Op& op : c.ops) {
                grid.paint(int(op.x), int(op.y), Sc::Isom::tilesetTerrainTypes[era][op.terrainType].isomValue, int(op.extent));
                grid.store(old, err);
            }
            const auto oldIsom = u16s(old, "ISOM");
            for (size_t i = 0; i < std::min(oldIsom.size(), isomExpected.size()); ++i) oldIsomDiff += (oldIsom[i] >> 4) != (isomExpected[i] >> 4);
            ++(oldIsomDiff ? oldImperfect : oldPerfect);
        }
        const bool ok = painted && !isomDiff && !groupDiff[0] && !groupDiff[1];
        std::printf("%s  %-40s %3zu strokes: ISOM %zu, TILE %zu, MTXM %zu differ%s (old engine: %zu ISOM terrains differ)\n",
                    ok ? "ok  " : "FAIL", c.expected, c.ops.size(), isomDiff, groupDiff[0], groupDiff[1], painted ? "" : " (a stroke was refused)",
                    oldIsomDiff);
        ok ? ++pass : ++fail;
    }
    std::printf("%d of %zu perfect; %.3f ms a stroke (the old engine: %d of %d with the same ISOM terrains)\n", pass, kCases.size(),
                strokes ? paintMs / double(strokes) : 0.0, oldPerfect, oldPerfect + oldImperfect);
    return fail ? 1 : 0;
}
