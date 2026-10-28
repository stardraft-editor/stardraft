// What the mod's maps have beside its doodad brushes' tiles (the brush sets with doodad in their names, and its trees):
// for each such tile on a map, the tile group beside it in each direction (0 left, 1 right, 2 up, 3 down). Prints how
// many pairs there are, and how many of the brushes' tiles the maps use at all.
// usage: cmbw_doodad_fit <Cosmonarchy install> <maps folder>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <set>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/terrain_stamp.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

// Each place a map uses a brush whole (all its tiles as they are), and its edge score there; then the score of the
// same brushes at random places of the same maps: how well brushEdgeScore tells a real place from any place.
int calibrate(const char* install, const char* folder) {
    std::string err;
    GameData data;
    if (!cmbw::open(data, install, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::vector<int> real, random;
    std::mt19937 rng(7);
    std::error_code ec;
    std::vector<const TerrainStamp*> brushes[8];
    Tileset ts[8];
    for (int e = 0; e < 8; ++e) {
        ts[e].load(data, uint16_t(e), err);
        for (const TerrainStamp& s : cmbw::brushStamps(install, uint16_t(e))) {
            std::string n = s.name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if ((n.find("doodad") != std::string::npos || n.find("_trees") != std::string::npos) && s.count() > 1) brushes[e].push_back(&s);
        }
    }
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* era = chk.find("ERA ");
        if (!era || era->data.size() < 2) continue;
        const int e = (era->data[0] | era->data[1] << 8) & 7;
        const TerrainLayer t = readTerrain(chk);
        std::map<uint16_t, std::vector<size_t>> at;  // tile value -> its places
        for (size_t k = 0; k < t.tiles.size(); ++k) at[t.tiles[k]].push_back(k);
        for (const TerrainStamp* b : brushes[e]) {
            int fx = -1, fy = -1;
            for (int y = 0; y < b->height && fx < 0; ++y)
                for (int x = 0; x < b->width; ++x)
                    if (b->has(x, y)) { fx = x, fy = y; break; }
            auto f = at.find(b->tiles[size_t(fy) * b->width + fx]);
            if (f == at.end()) continue;
            for (size_t k : f->second) {
                const int x0 = int(k % t.width) - fx, y0 = int(k / t.width) - fy;
                bool whole = true;
                for (int y = 0; y < b->height && whole; ++y)
                    for (int x = 0; x < b->width && whole; ++x)
                        if (b->has(x, y)) {
                            const int mx = x0 + x, my = y0 + y;
                            whole = mx >= 0 && my >= 0 && mx < t.width && my < t.height && t.tiles[size_t(my) * t.width + mx] == b->tiles[size_t(y) * b->width + x];
                        }
                if (!whole) continue;
                if (const int s = cmbw::brushEdgeScore(ts[e], *b, t, x0, y0); s >= 0) real.push_back(s);
                const int rx = int(rng() % t.width), ry = int(rng() % t.height);
                if (const int s = cmbw::brushEdgeScore(ts[e], *b, t, rx, ry); s >= 0) random.push_back(s);
            }
        }
    }
    auto show = [](const char* what, std::vector<int> v) {
        std::sort(v.begin(), v.end());
        if (v.empty()) return void(std::printf("%s: none\n", what));
        auto q = [&](double f) { return v[std::min(v.size() - 1, size_t(f * v.size()))]; };
        std::printf("%s: %zu places; 10%% %d, 50%% %d, 90%% %d, 95%% %d, 99%% %d\n", what, v.size(), q(0.1), q(0.5), q(0.9), q(0.95), q(0.99));
    };
    show("real", real);
    show("random", random);
    for (int lim : {10, 15, 20, 25, 30, 40}) {
        size_t r = 0, x = 0;
        for (int s : real) r += s <= lim;
        for (int s : random) x += s <= lim;
        std::printf("limit %d: real %.1f%% pass, random %.1f%% pass\n", lim, 100.0 * r / std::max<size_t>(1, real.size()), 100.0 * x / std::max<size_t>(1, random.size()));
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[3]) == "--calibrate") return calibrate(argv[1], argv[2]);
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_doodad_fit <install> <maps folder>\n"), 2;
    std::set<uint16_t> tiles[8];
    for (int e = 0; e < 8; ++e)
        for (const TerrainStamp& s : cmbw::brushStamps(argv[1], uint16_t(e))) {
            std::string n = s.name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if (n.find("doodad") == std::string::npos && n.find("_trees") == std::string::npos) continue;
            for (size_t i = 0; i < s.tiles.size() && i < s.mask.size(); ++i)
                if (s.mask[i]) tiles[e].insert(s.tiles[i]);
        }
    std::set<std::array<uint16_t, 4>> pairs;
    std::set<std::pair<int, uint16_t>> used;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[2], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
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
        for (int y = 0; y < t.height; ++y)
            for (int x = 0; x < t.width; ++x) {
                const uint16_t v = t.tiles[size_t(y) * t.width + x];
                if (!tiles[e].count(v)) continue;
                used.insert({e, v});
                const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + dx[d], ny = y + dy[d];
                    if (nx < 0 || ny < 0 || nx >= t.width || ny >= t.height) continue;
                    pairs.insert({uint16_t(e), v, uint16_t(t.tiles[size_t(ny) * t.width + nx] >> 4), uint16_t(d)});
                }
            }
    }
    size_t all = 0;
    for (auto& s : tiles) all += s.size();
    std::printf("brush tiles: %zu; used on the maps: %zu; pairs (tile, neighbor group, direction): %zu\n", all, used.size(), pairs.size());
    return 0;
}
