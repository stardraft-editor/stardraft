// Which maps use tile groups of a set: for each map of the era, its tiles in each range of groups (first-last) and the
// place of the first one (tiles). For finding a set's use on the mod's maps.
// usage: cmbw_group_maps <maps folder> <era> <first-last> [first-last ...]
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_group_maps <maps folder> <era> <first-last> ...\n"), 2;
    const int want = std::atoi(argv[2]);
    std::vector<std::pair<unsigned, unsigned>> ranges;
    for (int i = 3; i < argc; ++i) {
        const std::string r = argv[i];
        const size_t d = r.find('-');
        ranges.push_back({unsigned(std::stoul(r.substr(0, d))), unsigned(std::stoul(d == std::string::npos ? r : r.substr(d + 1)))});
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
        if (!era || era->data.size() < 2 || ((era->data[0] | era->data[1] << 8) & 7) != want) continue;
        const TerrainLayer t = readTerrain(chk);
        std::string line;
        bool any = false;
        for (const auto& [a, b] : ranges) {
            int n = 0, fx = -1, fy = -1;
            for (size_t k = 0; k < t.tiles.size(); ++k)
                if (unsigned g = t.tiles[k] >> 4; g >= a && g <= b) {
                    if (!n) fx = int(k % t.width), fy = int(k / t.width);
                    ++n;
                }
            any |= n > 0;
            line += " " + std::to_string(n) + (n ? " (" + std::to_string(fx) + "," + std::to_string(fy) + ")" : "");
        }
        if (any) std::printf("%s:%s\n", p.filename().string().c_str(), line.c_str());
    }
    return 0;
}
