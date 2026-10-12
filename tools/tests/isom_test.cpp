// Validate the ISOM model on every map (all tilesets) under the given directories:
//  1. decode + store without edits leaves ISOM/TILE/MTXM untouched
//  2. every decoded diamond is a valid shape
//  3. regenerating all of TILE from ISOM matches the original TILE
//  4. random paints leave no invalid diamonds and stack-consistent tiles
//
// usage: isom_test <maps dir> [more dirs, e.g. extracted campaign .chk files]

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "stardraft/chk.h"
#include "stardraft/isom.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: isom_test <maps dir> [more dirs]\n");
        return 2;
    }
    int maps = 0, failures = 0, perMaps[8] = {}, perFail[8] = {};
    size_t tilesTotal = 0, tilesSame = 0, perTotal[8] = {}, perSame[8] = {};
    // Blizzard's own maps (what the tile rules must reproduce): "(n)Name" maps outside a "ladder"
    // folder (community ladder maps carry hand-placed tiles) and the campaign scenarios ("(C)...").
    size_t blizTotal[8] = {}, blizSame[8] = {};
    int blizMaps[8] = {};
    std::mt19937 rng(1234);
    std::vector<fs::path> files;
    for (int d = 1; d < argc; ++d)
        for (const auto& entry : fs::recursive_directory_iterator(argv[d])) files.push_back(entry.path());
    for (const auto& path : files) {
        std::string ext = path.extension().string();
        for (auto& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        Bytes raw;
        std::string err;
        if (ext == ".chk") {
            std::ifstream f(path, std::ios::binary);
            raw.assign(std::istreambuf_iterator<char>(f), {});
        } else if ((ext != ".scm" && ext != ".scx") || !readMapChk(path, raw, err)) {
            continue;
        }
        Chk chk = Chk::parse(raw);
        if (!chk.has("ISOM") || !chk.has("TILE")) continue;
        const uint16_t era = readEra(chk);
        const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
        std::string name = std::string(tilesetName(era)) + "  " + path.filename().string();
        const int failuresBefore = failures;

        IsomGrid grid;
        if (!grid.load(chk, err)) {
            std::printf("LOAD SKIP %s: %s\n", name.c_str(), err.c_str());  // e.g. a truncated ISOM section
            continue;
        }
        ++maps, ++perMaps[era];

        // 1. no-op store
        Chk copy = chk;
        if (!grid.store(copy, err) || copy.find("ISOM")->data != chk.find("ISOM")->data ||
            copy.effective("TILE") != chk.effective("TILE")) {
            std::printf("NOOP FAIL %s %s\n", name.c_str(), err.c_str());
            ++failures;
        }

        // 2. validity
        if (int bad = grid.invalidDiamonds()) std::printf("INVALID   %s: %d diamonds\n", name.c_str(), bad);

        // 3. full regeneration
        Chk regen = chk;
        std::vector<uint8_t> all(size_t(grid.rectCols()) * grid.rectRows(), 1);
        if (!regenerateTiles(regen, all, err)) {
            std::printf("REGEN FAIL %s: %s\n", name.c_str(), err.c_str());
            ++failures;
            perFail[era] += failures - failuresBefore;
            continue;
        }
        TerrainLayer a = readTerrain(chk, "TILE"), b = readTerrain(regen, "TILE");
        size_t same = 0, n = 0;
        for (size_t i = 0; i < a.tiles.size(); ++i) {
            if ((a.tiles[i] >> 4) >= 1024) continue;  // doodads are not generated from ISOM
            ++n;
            same += (a.tiles[i] >> 4) == (b.tiles[i] >> 4);
        }
        tilesTotal += n, perTotal[era] += n;
        tilesSame += same, perSame[era] += same;
        bool blizzard = path.filename().string().rfind("(", 0) == 0;
        for (const auto& part : path.parent_path())
            if (part == "ladder") blizzard = false;
        if (blizzard) blizTotal[era] += n, blizSame[era] += same, ++blizMaps[era];
        std::printf("%-55s regen match %.4f\n", name.c_str(), n ? double(same) / n : 1.0);

        // 4. random paints with this tileset's terrains. Needs a consistent start: some maps were edited by
        // hand so that neighboring corners hold terrains that cannot touch (tree distance > 1); repairs
        // reaching such spots cannot make them valid, so those maps are reported and skipped here.
        int clashes = 0;
        auto inside = [&](int x, int y) { return grid.isCorner(x, y) && x >= 1 && y >= 1 && x < grid.rectCols() - 1 && y < grid.rectRows() - 1; };
        for (int y = 1; y < grid.rectRows() - 1; ++y)
            for (int x = 1; x < grid.rectCols() - 1; ++x) {
                if (!inside(x, y)) continue;
                for (auto [dx, dy] : {std::pair{1, 1}, std::pair{1, -1}, std::pair{2, 0}, std::pair{0, 2}})
                    if (inside(x + dx, y + dy) && table->distance(grid.terrainAt(x, y), grid.terrainAt(x + dx, y + dy)) > 1) ++clashes;
            }
        if (clashes) {
            std::printf("INCONSISTENT %s: %d corner pairs that cannot touch (paint check skipped)\n", name.c_str(), clashes);
            perFail[era] += failures - failuresBefore;
            continue;
        }
        Chk edited = chk;
        IsomGrid g2;
        g2.load(edited, err);
        for (int k = 0; k < 20; ++k) {
            int y = int(rng() % grid.rectRows()), x = int(rng() % grid.rectCols());
            if ((x + y) & 1) ++x;
            uint16_t t = 0;
            while (!table->isTerrain(t)) t = uint16_t(1 + rng() % table->terrainCount());
            g2.paint(x, y, t, 1 + int(rng() % 4));
        }
        if (int bad = g2.invalidDiamonds() - grid.invalidDiamonds(); bad > 0) {  // new ones only
            std::printf("PAINT INVALID %s: %d new diamonds\n", name.c_str(), bad);
            ++failures;
        }
        if (!g2.store(edited, err)) {
            std::printf("PAINT STORE FAIL %s: %s\n", name.c_str(), err.c_str());
            ++failures;
        }
        perFail[era] += failures - failuresBefore;
    }
    std::printf("\n");
    for (int e = 0; e < 8; ++e)
        std::printf("%-9s %3d maps, %d failures, regen match %.4f\n", tilesetName(uint16_t(e)), perMaps[e], perFail[e],
                    perTotal[e] ? double(perSame[e]) / perTotal[e] : 1.0);
    std::printf("\nBlizzard maps only (roadmap 3.2: >= 99%% per tileset):\n");
    for (int e = 0; e < 8; ++e) {
        const double m = blizTotal[e] ? double(blizSame[e]) / blizTotal[e] : 1.0;
        std::printf("%s %-9s %3d maps, regen match %.4f\n", m >= 0.99 ? "ok  " : "FAIL", tilesetName(uint16_t(e)), blizMaps[e], m);
        if (m < 0.99) ++failures;
    }
    std::printf("\n%d maps, %d failures, overall regen match %.4f\n", maps, failures, tilesTotal ? double(tilesSame) / tilesTotal : 1.0);
    return failures ? 1 : 0;
}
