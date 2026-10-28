// Lists the mod's terrain brushes of a tileset (its SCMDraft brush file), and draws those whose name has a text.
// usage: cmbw_brushes <Cosmonarchy install (with mpq\)> <tileset 0-7> [name text] [out .png]
// CMBW_SHAPE=<fill terrain>,<ground terrain>[,<fill group>...]: each drawn brush's minitiles as F (the fill's), G (the
// ground's) or . (neither), and the same for the game's rects of the border of the two terrains: their shapes, exactly.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/terrain_stamp.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_brushes <Cosmonarchy install> <tileset> [name text] [out .png]\n"), 2;
    const uint16_t era = uint16_t(std::atoi(argv[2]));
    const auto& stamps = cmbw::brushStamps(argv[1], era);
    const std::string text = argc > 3 ? argv[3] : "";
    int width = 0, height = 0;
    for (const TerrainStamp& s : stamps) {
        std::printf("%-60s %3dx%-3d\n", s.name.c_str(), s.width, s.height);
        if (std::getenv("BRUSH_DUMP") && !text.empty() && s.name.find(text) != std::string::npos)  // (its tiles, group.subtile)
            for (int y = 0; y < s.height; ++y) {
                std::printf("   ");
                for (int x = 0; x < s.width; ++x) {
                    const size_t i = size_t(y) * s.width + x;
                    if (s.mask[i]) std::printf(" %5u.%-2u", unsigned(s.tiles[i] >> 4), unsigned(s.tiles[i] & 15));
                    else std::printf("    -   ");
                }
                std::printf("\n");
            }
        if (!text.empty() && s.name.find(text) != std::string::npos) width = std::max(width, s.width), height += s.height + 1;
    }
    if (argc < 5 || !height) return 0;
    std::string err;
    GameData data;
    Tileset ts;
    if (!cmbw::open(data, argv[1], err) || !ts.load(data, era, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    // The minitiles (without flips) of the fill and of the ground, for CMBW_SHAPE.
    std::set<uint32_t> fill, ground;
    const char* shape = std::getenv("CMBW_SHAPE");
    std::vector<int> v;
    const auto terrains = tileGroupTerrains(ts);
    auto mini = [&](uint16_t value, int k) -> uint32_t {
        const int m = ts.megatileFor(value);
        return m >= 0 ? ts.megatiles[size_t(m)][size_t(k)] >> 1 : 0;
    };
    // A block of tiles (rows of values) as its minitiles' classes.
    auto printShape = [&](const std::vector<std::vector<uint16_t>>& rows) {
        for (size_t r = 0; r < rows.size() * 4; ++r) {
            std::printf("    ");
            for (size_t c = 0; c < rows[r / 4].size() * 4; ++c) {
                const uint32_t e = mini(rows[r / 4][c / 4], int((r % 4) * 4 + c % 4));
                std::printf("%c", fill.count(e) ? 'F' : ground.count(e) ? 'G' : '.');
            }
            std::printf("\n");
        }
    };
    if (shape) {
        std::stringstream ss(shape);
        for (std::string item; std::getline(ss, item, ',');) v.push_back(std::atoi(item.c_str()));
        auto add = [&](std::set<uint32_t>& to, size_t g) {
            for (int k = 0; k < 16; ++k)
                if (ts.groups[g].megatiles[size_t(k)])
                    for (int i = 0; i < 16; ++i) to.insert(mini(uint16_t(g << 4 | k), i));
        };
        for (size_t g = 0; g < terrains.size() && g < ts.groups.size(); ++g) {
            if (terrains[g][1] == 0 && terrains[g][0] == v[0]) add(fill, g);
            if (terrains[g][1] == 0 && terrains[g][0] == v[1]) add(ground, g);
        }
        for (size_t i = 2; i < v.size(); ++i) add(fill, size_t(v[i]));
        for (uint32_t m : fill) ground.erase(m);  // (shared: neither)
        std::printf("fill minitiles %zu, ground %zu\n", fill.size(), ground.size());
        // The game's rects of the border (each left group: its left and right tile, subtile 0).
        for (size_t g = 0; g + 1 < terrains.size(); ++g)
            if (std::minmax(terrains[g][0], terrains[g][1]) == std::minmax(uint16_t(v[0]), uint16_t(v[1])) && terrains[g][1] &&
                IsomTiles::forTileset(ts) && !IsomTiles::forTileset(ts)->rectsOf(uint16_t(g)).empty()) {
                std::printf("game rect %zu:\n", g);
                printShape({{uint16_t(g << 4), uint16_t((g + 1) << 4)}});
            }
    }
    TerrainLayer sheet;
    sheet.width = uint16_t(width), sheet.height = uint16_t(height);
    sheet.tiles.assign(size_t(width) * height, 0);
    int y = 0;
    for (const TerrainStamp& s : stamps) {
        if (s.name.find(text) == std::string::npos) continue;
        for (int r = 0; r < s.height; ++r)
            for (int c = 0; c < s.width; ++c)
                if (s.mask.empty() || s.mask[size_t(r) * s.width + c]) {
                    const uint16_t v = s.tiles[size_t(r) * s.width + c];
                    sheet.tiles[size_t(y + r) * width + c] = v;
                    std::printf("%s%4u.%-2u", c ? " " : "\n", unsigned(v >> 4), unsigned(v & 15));
                } else std::printf("%s   .   ", c ? " " : "\n");
        std::printf("\n  (%s)\n", s.name.c_str());
        if (shape) {
            std::vector<std::vector<uint16_t>> rows(size_t(s.height), std::vector<uint16_t>(size_t(s.width)));
            for (int r = 0; r < s.height; ++r)
                for (int c = 0; c < s.width; ++c) rows[size_t(r)][size_t(c)] = s.tiles[size_t(r) * s.width + c];
            printShape(rows);
        }
        y += s.height + 1;
    }
    return writePng(argv[4], renderTerrain(ts, sheet)) ? 0 : 1;
}
