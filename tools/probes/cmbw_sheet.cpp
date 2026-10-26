// Draws columns of rects side by side, to compare the mod's pieces with the game's: each column a stack of rects from
// the top, a rect either a game group ("g52": tiles 52.0 and 53.0) or the mod's left tile ("2402.0": it and the next
// subtile, 2402.1). Columns are split by ';', rects by ','.
// usage: cmbw_sheet <Cosmonarchy install> <tileset> <out .png> <columns>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_sheet <install> <tileset> <out .png> <columns>\n"), 2;
    std::string err;
    GameData data;
    Tileset ts;
    if (!cmbw::open(data, argv[1], err) || !ts.load(data, uint16_t(std::atoi(argv[2])), err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::vector<std::vector<std::pair<uint16_t, uint16_t>>> cols;
    std::stringstream all(argv[4]);
    for (std::string col; std::getline(all, col, ';');) {
        cols.emplace_back();
        std::stringstream cs(col);
        for (std::string r; std::getline(cs, r, ',');) {
            if (r.empty()) continue;
            if (r[0] == 'g') {
                const uint16_t g = uint16_t(std::atoi(r.c_str() + 1));
                cols.back().push_back({uint16_t(g << 4), uint16_t((g + 1) << 4)});
            } else {
                const size_t dot = r.find('.');
                const uint16_t v = uint16_t(std::atoi(r.c_str()) << 4 | (dot == std::string::npos ? 0 : std::atoi(r.c_str() + dot + 1)));
                cols.back().push_back({v, uint16_t(v + 1)});
            }
        }
    }
    size_t rows = 0;
    for (const auto& c : cols) rows = std::max(rows, c.size());
    TerrainLayer sheet;
    sheet.width = uint16_t(cols.size() * 3), sheet.height = uint16_t(rows);
    sheet.tiles.assign(size_t(sheet.width) * sheet.height, 0);
    for (size_t x = 0; x < cols.size(); ++x)
        for (size_t y = 0; y < cols[x].size(); ++y)
            sheet.tiles[y * sheet.width + x * 3] = cols[x][y].first, sheet.tiles[y * sheet.width + x * 3 + 1] = cols[x][y].second;
    return writePng(argv[3], renderTerrain(ts, sheet)) ? 0 : 1;
}
