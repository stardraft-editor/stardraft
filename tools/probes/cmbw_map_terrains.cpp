// What terrains a map's ISOM holds (each corner's terrain, counted; mod terrains by name) and how many of its tiles are
// mod terrains' pieces, as StarDraft opens it on a Cosmonarchy tab; a picture of its tiles. For checking a map that
// another editor saved (SCMDraft) after StarDraft painted mod terrains in it.
// usage: cmbw_map_terrains <Cosmonarchy install> <map> [out .png]
#include <cstdio>
#include <map>
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
    if (argc < 3) return std::fprintf(stderr, "usage: cmbw_map_terrains <install> <map> [out .png]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "open: %s\n", err.c_str()), 1;
    std::printf("opened: %d x %d, ISOM %s\n", e.width(), e.height(), e.canPaintTerrain() ? "loaded" : "not loaded");
    const IsomTerrainTable* game = IsomTerrainTable::forEra(e.era());
    std::map<uint16_t, int> corners;
    for (int y = 0; y <= e.height(); ++y)
        for (int x = (y + 1) & 1; x * 2 <= e.width(); x += 2) ++corners[e.terrainAt(x * 64, y * 32)];
    for (auto [t, n] : corners) {
        const Tileset::ModTerrain* m = e.tileset().modTerrain(t);
        std::printf("terrain %u %s: %d corners\n", unsigned(t), m ? m->name.c_str() : game && game->isTerrain(t) ? game->terrainName(t) : "?", n);
    }
    std::set<uint16_t> mine;
    for (const auto& m : e.tileset().modTerrains)
        for (const auto& p : m.pieces) mine.insert(p[1]), mine.insert(p[2]);
    int pieces = 0;
    const TerrainLayer t = readTerrain(e.chk(), "TILE");
    for (uint16_t v : t.tiles) pieces += mine.count(v) != 0;
    std::printf("tiles that are mod pieces: %d\n", pieces);
    if (argc > 3) return writePng(argv[3], renderTerrain(e.tileset(), t)) ? 0 : 1;
    return 0;
}
