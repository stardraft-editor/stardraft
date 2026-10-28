// Game cliffs painted on a Cosmonarchy tab: the map's middle gets <low> (a diamond of 20), then <high> (14) and <higher> (8)
// on it, and the probe writes the tiles there and lists the tiles that are the mod's (its learned pieces and single
// tiles) and where. For looking at the game's cliffs with the mod's tiles in them.
// usage: cmbw_cliffs <Cosmonarchy install> <map> <low> <high> <higher> <out .png>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 7) return std::fprintf(stderr, "usage: cmbw_cliffs <install> <map> <low> <high> <higher> <out .png>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const IsomTerrainTable* game = IsomTerrainTable::forEra(e.era());
    auto named = [&](const char* n) -> uint16_t {
        for (uint16_t t = 1; game && t <= game->terrainCount(); ++t)
            if (game->isTerrain(t) && std::strcmp(game->terrainName(t), n) == 0) return t;
        std::fprintf(stderr, "no terrain %s\n", n);
        return 0;
    };
    const uint16_t low = named(argv[3]), high = named(argv[4]), higher = named(argv[5]);
    if (!low || !high || !higher) return 1;
    const int cx = e.width() * 16, cy = e.height() * 16;
    const TerrainLayer before = readTerrain(e.chk(), "TILE");
    e.paintBrush(cx, cy, low, Brush{Brush::Diamond, 20.0f});
    e.paintBrush(cx, cy, high, Brush{Brush::Diamond, 14.0f});
    e.paintBrush(cx, cy, higher, Brush{Brush::Diamond, 8.0f});
    const Tileset& ts = e.tileset();
    std::set<uint16_t> pieceGroups, aloneGroups;
    for (const auto& [l, r, g] : ts.groupPieces) pieceGroups.insert(uint16_t(l >> 4)), pieceGroups.insert(uint16_t(r >> 4));
    for (const auto& [g, t, other] : ts.groupTerrains) aloneGroups.insert(g);
    const TerrainLayer t = readTerrain(e.chk(), "TILE");
    TerrainLayer part;
    const int x0 = std::max(0, cx / 32 - 22), y0 = std::max(0, cy / 32 - 22);
    part.width = uint16_t(std::min<int>(t.width - x0, 44)), part.height = uint16_t(std::min<int>(t.height - y0, 44));
    static constexpr uint16_t kGameGroups[8] = {1665, 1513, 1265, 1262, 1578, 1520, 1415, 1494};  // (the 1.16.1 CV5s)
    int pieces = 0, alone = 0, past = 0;
    for (int y = 0; y < part.height; ++y)
        for (int x = 0; x < part.width; ++x) {
            const uint16_t v = t.tiles[size_t(y0 + y) * t.width + x0 + x];
            part.tiles.push_back(v);
            if (v == before.tiles[size_t(y0 + y) * t.width + x0 + x]) continue;
            past += (v >> 4) >= kGameGroups[e.era() & 7];
            const bool p = pieceGroups.count(uint16_t(v >> 4)) != 0;
            if (p || aloneGroups.count(uint16_t(v >> 4))) {
                p ? ++pieces : ++alone;
                std::printf("%s %d,%d group %u\n", p ? "piece" : "alone", x, y, unsigned(v >> 4));
            }
        }
    std::printf("changed tiles that are mod piece tiles: %d, mod single tiles: %d; of the mod's new groups: %d\n", pieces, alone, past);
    return writePng(argv[6], renderTerrain(ts, part)) ? 0 : 1;
}
