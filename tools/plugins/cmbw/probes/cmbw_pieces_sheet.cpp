// A sheet of the mod's pieces for one terrain border of a Cosmonarchy tileset: one row for each game rect of the border
// (its game piece first), then the mod's pieces that pass the brush's look filter, a gap, and those that do not.
// usage: cmbw_pieces_sheet <Cosmonarchy install (the folder with mpq\)> <tileset 0-7> <terrain> <terrain> <out .png>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 6) return std::fprintf(stderr, "usage: cmbw_pieces_sheet <Cosmonarchy install> <tileset> <terrain> <terrain> <out .png>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "cannot open the Cosmonarchy data: %s\n", err.c_str()), 1;
    Tileset ts;
    if (!ts.load(data, uint16_t(std::atoi(argv[2])), err)) return std::fprintf(stderr, "no tileset: %s\n", err.c_str()), 1;
    const uint16_t a = uint16_t(std::atoi(argv[3])), b = uint16_t(std::atoi(argv[4]));
    const auto terrains = tileGroupTerrains(ts);
    // CMBW_GROUP=<group>: that group's terrains first.
    if (const char* g = std::getenv("CMBW_GROUP"); g && size_t(std::atoi(g)) < terrains.size())
        std::printf("group %d: terrains %u %u (%s / %s)\n", std::atoi(g), unsigned(terrains[size_t(std::atoi(g))][0]), unsigned(terrains[size_t(std::atoi(g))][1]),
                    IsomTerrainTable::forEra(ts.era)->terrainName(terrains[size_t(std::atoi(g))][0]),
                    IsomTerrainTable::forEra(ts.era)->terrainName(terrains[size_t(std::atoi(g))][1]));
    // The brush's look filter (Editor::sprinkleModTiles).
    auto look = [&](uint16_t value) {
        std::array<double, 5> s{};
        const int m = ts.megatileFor(value);
        int n = 0;
        if (m >= 0)
            for (uint32_t e : ts.megatiles[size_t(m)])
                if ((e >> 1) < ts.minitiles.size())
                    for (uint8_t p : ts.minitiles[e >> 1]) {
                        const Rgb& c = ts.palette[p];
                        const double l = 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
                        s[0] += c.r, s[1] += c.g, s[2] += c.b, s[3] += l, s[4] += l * l, ++n;
                    }
        for (double& v : s) v /= std::max(n, 1);
        return std::array<double, 4>{s[0], s[1], s[2], std::sqrt(std::max(0.0, s[4] - s[3] * s[3]))};
    };
    auto alike = [&](uint16_t x, uint16_t y) {
        const auto p = look(x), q = look(y);
        const double d = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
        return d < 144 && std::abs(p[3] - q[3]) < 0.25 * std::max(p[3], q[3]);
    };
    std::map<uint16_t, std::vector<std::pair<uint16_t, uint16_t>>> pieces;
    for (const auto& [l, r, g] : ts.groupPieces)
        if (g < terrains.size() && std::minmax(terrains[g][0], terrains[g][1]) == std::minmax(a, b)) pieces[g].push_back({l, r});
    std::vector<std::vector<uint16_t>> rows;
    size_t width = 0;
    int pass = 0, fail = 0;
    for (const auto& [g, list] : pieces) {
        std::vector<uint16_t> row = {uint16_t(g << 4), uint16_t((g + 1) << 4), 0, 0};
        std::vector<uint16_t> bad;
        for (const auto& [l, r] : list) {
            const bool ok = alike(l, uint16_t(g << 4)) && alike(r, uint16_t((g + 1) << 4));
            (ok ? row : bad).push_back(l), (ok ? row : bad).push_back(r);
            ok ? ++pass : ++fail;
        }
        row.push_back(0), row.push_back(0), row.push_back(0), row.push_back(0);
        row.insert(row.end(), bad.begin(), bad.end());
        std::printf("game rect %4u: %zu pieces of the mod's\n", unsigned(g), list.size());
        width = std::max(width, row.size());
        rows.push_back(std::move(row));
    }
    std::printf("%s / %s: %zu game rects, %d pieces pass the look filter, %d do not\n", IsomTerrainTable::forEra(ts.era)->terrainName(a),
                IsomTerrainTable::forEra(ts.era)->terrainName(b), rows.size(), pass, fail);
    TerrainLayer sheet;
    sheet.width = uint16_t(width), sheet.height = uint16_t(rows.size() * 2);
    sheet.tiles.assign(size_t(sheet.width) * sheet.height, 0);
    for (size_t y = 0; y < rows.size(); ++y)
        for (size_t x = 0; x < rows[y].size(); ++x) sheet.tiles[y * 2 * sheet.width + x] = rows[y][x];
    return writePng(argv[5], renderTerrain(ts, sheet)) ? 0 : 1;
}
