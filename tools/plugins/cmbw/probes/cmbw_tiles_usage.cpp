// How much Cosmonarchy BW's maps use the tile groups its tilesets add (the groups past the game's: they have no isometric
// data, cmbw_tiles_probe), per tileset.
// With the mod's tileset folder: for each new group, the terrain types (.cv5) of the game's groups next to its tiles in
// the maps; a group whose neighbors are mostly of one type is likely a variant of that terrain.
// usage: cmbw_tiles_usage <maps folder> [mod tileset folder]
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 2) return std::fprintf(stderr, "usage: cmbw_tiles_usage <maps folder>\n"), 2;
    // The game's group counts (cmbw_tiles_probe): badlands, platform, install, ashworld, jungle, desert, ice, twilight.
    const std::array<unsigned, 8> stock = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};
    const char* names[8] = {"badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight"};
    struct Count {
        int maps = 0, mapsWithNew = 0;
        uint64_t tiles = 0, newTiles = 0;
    };
    std::map<int, Count> per;
    // Per tileset: the mod's .cv5 group types, and per new group the neighbor types' counts.
    std::array<std::vector<uint16_t>, 8> types;
    std::array<std::map<unsigned, std::map<uint16_t, int>>, 8> near;
    const char* files[8] = {"badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"};
    if (argc > 2)
        for (int e = 0; e < 8; ++e) {
            std::ifstream in(fs::path(argv[2]) / (std::string(files[e]) + ".cv5"), std::ios::binary);
            const Bytes cv5((std::istreambuf_iterator<char>(in)), {});
            for (size_t g = 0; g + 52 <= cv5.size(); g += 52) types[size_t(e)].push_back(uint16_t(cv5[g] | cv5[g + 1] << 8));
        }
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[1], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        std::string err;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* era = chk.find("ERA ");
        if (!era || era->data.size() < 2) continue;
        const int e = (era->data[0] | era->data[1] << 8) & 7;
        const TerrainLayer t = readTerrain(chk);
        Count& c = per[e];
        ++c.maps;
        uint64_t n = 0;
        for (uint16_t v : t.tiles) n += (v >> 4) >= stock[size_t(e)];
        const auto& ty = types[size_t(e)];
        for (int y = 0; y < t.height && !ty.empty(); ++y)
            for (int x = 0; x < t.width; ++x) {
                const unsigned g = t.tiles[size_t(y) * t.width + x] >> 4;
                if (g < stock[size_t(e)]) continue;
                const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= t.width || ny >= t.height) continue;
                    const unsigned h = t.tiles[size_t(ny) * t.width + nx] >> 4;
                    if (h < stock[size_t(e)] && h < ty.size() && ty[h]) ++near[size_t(e)][g][ty[h]];
                }
            }
        c.tiles += t.tiles.size(), c.newTiles += n, c.mapsWithNew += n > 0;
    }
    for (const auto& [e, c] : per)
        std::printf("%-9s %3d maps, %3d use new groups; tiles %llu, of the new groups %llu (%.1f %%)\n", names[e], c.maps, c.mapsWithNew,
                    (unsigned long long)c.tiles, (unsigned long long)c.newTiles, c.tiles ? 100.0 * c.newTiles / c.tiles : 0.0);
    for (int e = 0; e < 8; ++e) {
        if (near[size_t(e)].empty()) continue;
        int groups = 0, dominant = 0;
        for (const auto& [g, counts] : near[size_t(e)]) {
            int total = 0, best = 0;
            for (const auto& [ty, c] : counts) total += c, best = std::max(best, c);
            if (total < 8) continue;  // (too few samples)
            ++groups, dominant += best * 10 >= total * 8;
        }
        std::printf("%-9s new groups next to the game's tiles: %d with 8+ samples, %d (%.0f %%) mostly one terrain type (80 %%+)\n", names[e], groups,
                    dominant, groups ? 100.0 * dominant / groups : 0.0);
    }
    return 0;
}
