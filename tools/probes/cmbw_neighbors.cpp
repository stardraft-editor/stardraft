// Which variants of a set's wall pieces stand beside each other on the mod's maps: for each map of the era, each rect
// whose left tile is in the groups first-last and its diagonal neighbors (one rect to the right, a row up or down) in
// those groups too; counts the pairs as "group.subtile -> group.subtile" (the left tiles), the most seen first.
// usage: cmbw_neighbors <maps folder> <era> <first group> <last group>
//        cmbw_neighbors <maps folder> --inc <out .inc> <Cosmonarchy install>   (all eras, the mod terrains' pieces only:
//        src/mods/cmbw_neighbors.inc,
//        {era, tile a, tile b, place}: place 0 b under a, 1 b a rect to the right and a row down, 2 to the right and a row up)
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/tileset.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"

namespace fs = std::filesystem;
using namespace stardraft;

int writeInc(const char* folder, const char* out, const char* install) {
    std::string err;
    GameData data;
    if (!cmbw::open(data, install, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    auto seen = cmbw::learnPieceNeighbors(data, folder);
    std::sort(seen.begin(), seen.end());
    // The packed form (src/mods/cmbw.cpp forEachPieceNeighbor reads it): numbers, each in base-64 characters of 5 bits
    // (the lowest first; 32 added: more characters follow). For each tileset: the tileset, its count of pieces; for each
    // piece: its left tile less the one before it (0 at first), the counts of its neighbors at places 0, 1 and 2, then
    // each place's neighbor tiles, smallest first, as the difference from the one before (the piece's tile at first),
    // zigzag (0, -1, 1, -2: 0, 1, 2, 3).
    std::vector<unsigned> nums;
    for (size_t i = 0; i < seen.size();) {
        const uint16_t era = seen[i][0];
        size_t end = i;
        std::vector<std::pair<uint16_t, std::array<std::vector<uint16_t>, 3>>> pieces;
        for (; end < seen.size() && seen[end][0] == era; ++end) {
            if (pieces.empty() || pieces.back().first != seen[end][1]) pieces.push_back({seen[end][1], {}});
            pieces.back().second[seen[end][3] % 3].push_back(seen[end][2]);
        }
        nums.push_back(era), nums.push_back(unsigned(pieces.size()));
        unsigned before = 0;
        for (auto& [a, places] : pieces) {
            nums.push_back(a - before), before = a;
            for (auto& l : places) std::sort(l.begin(), l.end()), nums.push_back(unsigned(l.size()));
            for (const auto& l : places) {
                int prev = a;
                for (uint16_t b : l) nums.push_back(b >= prev ? unsigned(b - prev) * 2 : unsigned(prev - b) * 2 - 1), prev = b;
            }
        }
        i = end;
    }
    static const char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    for (unsigned v : nums)
        do {
            const unsigned d = v & 31;
            v >>= 5;
            text += kDigits[d | (v ? 32 : 0)];
        } while (v);
    FILE* f = std::fopen(out, "wb");
    if (!f) return 1;
    std::fprintf(f, "// Made by tools/probes/cmbw_neighbors.cpp from the mod's maps (CMBW-maps): which of the mod terrains' pieces stand\n"
                    "// beside each other, packed (the probe's writeInc tells the form; lines, as one string literal may not be so\n"
                    "// long). Do not edit by hand.\n"
                    "// (Its probe builds with -Dcmbw_probes=ON.)\n");
    for (size_t at = 0; at < text.size(); at += 120) std::fprintf(f, "\"%s\",\n", text.substr(at, 120).c_str());
    std::fclose(f);
    std::printf("%zu pairs\n", seen.size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[2]) == "--inc") return writeInc(argv[1], argv[3], argv[4]);
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_neighbors <maps folder> <era> <first> <last>\n"), 2;
    const int want = std::atoi(argv[2]);
    const unsigned first = unsigned(std::atoi(argv[3])), last = unsigned(std::atoi(argv[4]));
    std::map<std::pair<uint16_t, uint16_t>, int> pairs;
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
        auto in = [&](int x, int y) -> int {
            if (x < 0 || y < 0 || x >= t.width || y >= t.height) return -1;
            const uint16_t v = t.tiles[size_t(y) * t.width + x];
            return (v >> 4) >= first && (v >> 4) <= last ? v : -1;
        };
        for (int y = 0; y < t.height; ++y)
            for (int x = 0; x + 2 < t.width; x += 2) {
                const int a = in(x, y);
                if (a < 0) continue;
                for (int dy : {-1, 1})
                    if (const int b = in(x + 2, y + dy); b >= 0) ++pairs[{uint16_t(a), uint16_t(b)}];
            }
    }
    std::vector<std::pair<int, std::pair<uint16_t, uint16_t>>> out;
    for (auto [k, n] : pairs) out.push_back({n, k});
    std::sort(out.rbegin(), out.rend());
    for (auto [n, k] : out)
        std::printf("%u.%u -> %u.%u: %d\n", unsigned(k.first >> 4), unsigned(k.first & 15), unsigned(k.second >> 4), unsigned(k.second & 15), n);
    return 0;
}
