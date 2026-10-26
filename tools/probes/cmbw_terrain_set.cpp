// The tiles a mod's maps use around a terrain of the mod's own (one placed in Rectangle mode, without isometric data):
// from the seed tiles (its fill), the rects (two tiles at an even column) right next to them, counted over the maps of
// one tileset, and a sheet of them (each rect: left and right tile) to see their shapes.
// usage: cmbw_terrain_set <maps folder> <Cosmonarchy install (with mpq\)> <tileset 0-7> <out .png> <seed group>...
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "stardraft/chk.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 6) return std::fprintf(stderr, "usage: cmbw_terrain_set <maps> <Cosmonarchy install> <tileset> <out .png> <seed group>...\n"), 2;
    const int era = std::atoi(argv[3]);
    std::set<uint16_t> seeds;
    for (int i = 5; i < argc; ++i) seeds.insert(uint16_t(std::atoi(argv[i])));
    std::map<uint32_t, int> rects;  // (left << 16 | right) -> rects next to the fill
    int maps = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(argv[1], ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path p = it->path();
        if (p.extension() != ".scx" && p.extension() != ".scm") continue;
        Bytes raw;
        std::string err;
        if (!readMapFile(p, raw, err)) continue;
        const Chk chk = Chk::parse(raw);
        const Section* e = chk.find("ERA ");
        if (!e || e->data.size() < 2 || (e->data[0] & 7) != era) continue;
        const TerrainLayer t = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
        const int w = t.width, h = t.height;
        auto fill = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && seeds.count(uint16_t(t.tiles[size_t(y) * w + x] >> 4)); };
        bool any = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x + 1 < w; x += 2) {
                if (fill(x, y) || fill(x + 1, y)) continue;
                bool near = false;
                for (int dy = -1; dy <= 1 && !near; ++dy)
                    for (int dx = -1; dx <= 2 && !near; ++dx) near = fill(x + dx, y + dy);
                if (!near) continue;
                ++rects[uint32_t(t.tiles[size_t(y) * w + x]) << 16 | t.tiles[size_t(y) * w + x + 1]], any = true;
            }
        maps += any;
    }
    std::vector<std::pair<int, uint32_t>> ranked;
    for (const auto& [lr, n] : rects)
        if (n >= 8) ranked.push_back({n, lr});
    std::sort(ranked.rbegin(), ranked.rend());
    std::printf("%d maps with the fill; %zu rects next to it 8+ times\n", maps, ranked.size());
    for (const auto& [n, lr] : ranked) std::printf("%4u.%-2u %4u.%-2u x%d\n", unsigned(lr >> 16 >> 4), unsigned(lr >> 16 & 15), unsigned(lr >> 4 & 0xfff),
                                                   unsigned(lr & 15), n);
    std::string err;
    GameData data;
    Tileset ts;
    if (!cmbw::open(data, argv[2], err) || !ts.load(data, uint16_t(era), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    // The sheet: 8 rects a row, a tile of space around each.
    const int cols = 8;
    TerrainLayer sheet;
    sheet.width = uint16_t(cols * 3), sheet.height = uint16_t((ranked.size() + cols - 1) / cols * 2);
    sheet.tiles.assign(size_t(sheet.width) * sheet.height, 0);
    for (size_t i = 0; i < ranked.size(); ++i) {
        const size_t x = (i % cols) * 3, y = (i / cols) * 2;
        sheet.tiles[y * sheet.width + x] = uint16_t(ranked[i].second >> 16), sheet.tiles[y * sheet.width + x + 1] = uint16_t(ranked[i].second);
    }
    return writePng(argv[4], renderTerrain(ts, sheet)) ? 0 : 1;
}
