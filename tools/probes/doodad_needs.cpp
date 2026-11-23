// The doodads on maps whose ground (TILE, else MTXM) does not fit dddata.bin's cells below the top row, as
// doodadFits reads them: which doodads, how often, and the cells (the needed tile group and the one there).
//
// usage: doodad_needs <install dir> <folder or map>...   (DOODAD=<id>: also the first footprints of that doodad that fail)

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "stardraft/doodads.h"
#include "stardraft/game_data.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: doodad_needs <install dir> <folder or map>...\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    Tileset sets[8];
    DoodadTypes types[8];
    bool ok[8]{};
    for (uint16_t e = 0; e < 8; ++e) ok[e] = sets[e].load(data, e, err) && types[e].load(data, sets[e], e, err);
    std::vector<fs::path> maps;
    for (int i = 2; i < argc; ++i) {
        std::error_code ec;
        if (fs::is_directory(argv[i], ec)) {
            for (auto it = fs::recursive_directory_iterator(argv[i], fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                const std::string x = it->path().extension().string();
                if (it->is_regular_file(ec) && (x == ".scm" || x == ".scx")) maps.push_back(it->path());
            }
        } else maps.push_back(argv[i]);
    }
    struct Stat {
        int placed = 0, failed = 0;
        std::map<std::string, int> cells;
    };
    std::map<std::pair<int, int>, Stat> stats;
    int offsets = 0;  // (doodads whose tiles are a tile off from DD2's place)
    for (const fs::path& path : maps) {
        Bytes raw;
        if (!readMapChk(path, raw, err)) continue;
        Chk chk = Chk::parse(raw);
        const uint16_t era = readEra(chk) & 7;
        if (!ok[era]) continue;
        const TerrainLayer ground = readTerrain(chk, chk.has("TILE") ? "TILE" : "MTXM");
        const Tileset& ts = sets[era];
        const TerrainLayer mtxm = readTerrain(chk, "MTXM");
        for (const Doodad& d : readDoodads(chk)) {
            const DoodadType* t = types[era].find(d.id);
            if (!t || !t->inPalette || t->height < 2) continue;  // (one row: not checked)
            const int tx0 = (d.x - t->width * 16) / 32, ty0 = (d.y - t->height * 16) / 32;
            // Where the doodad's own tiles are in MTXM (a map's DD2 place can be a tile off from them): the place
            // within one tile of DD2's where the most of its tiles are. None there: not checked (removed or covered).
            int tx = tx0, ty = ty0, best = 0;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    int n = 0;
                    for (int r = 0; r < t->height && size_t(t->firstGroup) + r < ts.groups.size(); ++r)
                        for (int c = 0; c < t->width; ++c) {
                            const int x = tx0 + ox + c, y = ty0 + oy + r;
                            if (ts.groups[t->firstGroup + r].megatiles[c] && x >= 0 && y >= 0 && x < mtxm.width && y < mtxm.height &&
                                mtxm.tiles[size_t(y) * mtxm.width + x] == uint16_t((t->firstGroup + r) << 4 | c))
                                ++n;
                        }
                    if (n > best) best = n, tx = tx0 + ox, ty = ty0 + oy;
                }
            if (!best) continue;
            offsets += tx != tx0 || ty != ty0;
            Stat& s = stats[{era, d.id}];
            ++s.placed;
            bool bad = false;
            for (int r = t->height > 1 ? 1 : 0; r < t->height; ++r)
                for (int c = 0; c < t->width; ++c) {
                    const uint16_t need = t->required[size_t(r) * t->width + c];
                    const int x = tx + c, y = ty + r;
                    if (!need || x < 0 || y < 0 || x >= ground.width || y >= ground.height) continue;
                    const uint16_t g = ground.tiles[size_t(y) * ground.width + x] >> 4;
                    const auto& p = t->ground->piece;
                    if (g == need || (g < p.size() && need < p.size() && p[g] == p[need])) continue;
                    if (g < ts.groups.size() && ts.groups[g].type == 1) continue;  // (another doodad's tile: the map has doodads over doodads)
                    bad = true;
                    char b[160];
                    const uint16_t gt = g < ts.groups.size() ? ts.groups[g].type : 0xFFFF, nt = need < ts.groups.size() ? ts.groups[need].type : 0xFFFF;
                    std::snprintf(b, sizeof b, "r%d c%d need %u (type %u) got %u (type %u)", r, c, need, nt, g, gt);
                    ++s.cells[b];
                }
            s.failed += bad;
            if (bad && getenv("DOODAD") && atoi(getenv("DOODAD")) == d.id && s.failed <= 2) {  // (one doodad's footprint: MTXM's groups and the needs)
                const TerrainLayer m = readTerrain(chk, "MTXM");
                std::printf("%s era %d doodad %d %dx%d at %d,%d first %u\n", path.filename().string().c_str(), era, d.id, t->width, t->height, tx, ty, t->firstGroup);
                for (int r = -1; r <= t->height; ++r) {
                    std::printf("  r%2d:", r);
                    for (int c = 0; c < t->width; ++c) {
                        const int x = tx + c, y = ty + r;
                        const bool in = x >= 0 && y >= 0 && x < m.width && y < m.height;
                        const int mg = in ? m.tiles[size_t(y) * m.width + x] >> 4 : -1, gg = in ? ground.tiles[size_t(y) * ground.width + x] >> 4 : -1;
                        const int nd = r >= 0 && r < t->height ? t->required[size_t(r) * t->width + c] : -1;
                        const int mega = r >= 0 && r < t->height && size_t(t->firstGroup) + r < ts.groups.size() ? ts.groups[t->firstGroup + r].megatiles[c] : -1;
                        std::printf(" [m%d g%d n%d %s]", mg, gg, nd, mega > 0 ? "T" : ".");
                    }
                    std::printf("\n");
                }
            }
        }
    }
    int placed = 0, failed = 0;
    for (const auto& [k, s] : stats) {
        placed += s.placed, failed += s.failed;
        if (!s.failed) continue;
        std::printf("era %d doodad %d: %d of %d fail\n", k.first, k.second, s.failed, s.placed);
        int n = 0;
        for (const auto& [c, m] : s.cells)
            if (n++ < 4) std::printf("    %s  x%d\n", c.c_str(), m);
    }
    std::printf("%zu maps, %d of %d doodads fail (%d with their tiles a tile off from DD2)\n", maps.size(), failed, placed, offsets);
    return 0;
}
