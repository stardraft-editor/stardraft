// A map's tiles around a place as StarDraft opens it on a Cosmonarchy tab: each tile's group.subtile and the ISOM
// terrain at its rect's corners. For reading what the mod's maps put where.
// usage: cmbw_tile_at <Cosmonarchy install> <map> <tile x> <tile y> [radius]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 5) return std::fprintf(stderr, "usage: cmbw_tile_at <install> <map> <x> <y> [radius]\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor e(data);
    if (!e.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const int cx = std::atoi(argv[3]), cy = std::atoi(argv[4]), r = argc > 5 ? std::atoi(argv[5]) : 4;
    const TerrainLayer t = readTerrain(e.chk(), "MTXM");
    for (int y = cy - r; y <= cy + r; ++y) {
        for (int x = (cx - r) & ~1; x <= cx + r; ++x) {
            if (x < 0 || y < 0 || x >= t.width || y >= t.height) continue;
            const uint16_t v = t.tiles[size_t(y) * t.width + x];
            std::printf("%4u.%-2u ", unsigned(v >> 4), unsigned(v & 15));
        }
        std::printf("  |");
        for (int x = (cx - r) & ~1; x <= cx + r; x += 2) std::printf(" %u", unsigned(e.terrainAt(x * 32, y * 32)));
        std::printf("\n");
    }
    return 0;
}
