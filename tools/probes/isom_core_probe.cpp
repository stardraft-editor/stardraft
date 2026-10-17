// The terrain core (IsomCore) next to the old engine's tables (a probe).
//   isom_core_probe ids <era>: each tileset terrain: our id and name (IsomTerrainTable), the core's brush values
//   isom_core_probe same <map> <px> <py> [extent]: paint the terrain of the corner at (px, py) there with the core;
//     which ISOM slots change their terrain
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/terrain.h"

using namespace stardraft;

int main(int argc, char** argv) {
    GameData d;
    std::string e;
    const char* inst = std::getenv("STARDRAFT_INSTALL");
    if (!d.open(inst ? inst : "/Applications/StarCraft", e)) return std::fprintf(stderr, "%s\n", e.c_str()), 2;
    const std::string what = argc > 1 ? argv[1] : "";
    if (what == "ids" && argc > 2) {
        const uint16_t era = uint16_t(std::atoi(argv[2]));
        const IsomTerrainTable* t = IsomTerrainTable::forEra(era);
        for (uint16_t i = 1; t && i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) std::printf("ours %3u %s\n", i, t->terrainName(i));
        Tileset ts;
        if (!ts.load(d, era, e)) return 2;
        Chk chk;
        std::vector<uint8_t> dim{2, 0, 2, 0}, eraB{uint8_t(era), 0}, isom(2 * 3 * 8, 0), tile(8, 0);
        chk.set("DIM ", dim), chk.set("ERA ", eraB), chk.set("ISOM", isom), chk.set("TILE", tile), chk.set("MTXM", tile);
        IsomCore core;
        if (!core.load(chk, ts, e)) return std::fprintf(stderr, "%s\n", e.c_str()), 2;
        for (uint16_t v : core.brushes()) std::printf("core brush isom value %u\n", v);
        return 0;
    }
    if (what == "same" && argc > 4) {
        Bytes raw;
        if (!readMapChk(argv[2], raw, e)) return 2;
        Chk chk = Chk::parse(raw);
        const uint16_t era = uint16_t(readEra(chk) & 7);
        Tileset ts;
        ts.load(d, era, e);
        IsomGrid g;
        g.load(chk, e);
        int cx = 0, cy = 0;
        g.cornerAtPixel(std::atoi(argv[3]), std::atoi(argv[4]), cx, cy);
        const uint16_t terrain = g.terrainAt(cx, cy);
        int x = 0, y = 0;
        diamondAtPixel(std::atoi(argv[3]), std::atoi(argv[4]), x, y);
        const Bytes before = chk.effective("ISOM");
        IsomCore c;
        c.load(chk, ts, e);
        const bool ok = c.paint(x, y, terrain, argc > 5 ? std::atoi(argv[5]) : 2);
        c.updateTiles();
        c.store(chk);
        const Bytes after = chk.effective("ISOM");
        std::printf("diamond %d,%d terrain %u (%s): painted %d\n", x, y, terrain, IsomTerrainTable::forEra(era)->terrainName(terrain), ok);
        const int cols = g.rectCols();
        for (size_t i = 0; i < before.size() / 2; ++i) {
            const uint16_t a = uint16_t(before[2 * i] | before[2 * i + 1] << 8), b = uint16_t(after[2 * i] | after[2 * i + 1] << 8);
            if ((a >> 4) != (b >> 4))
                std::printf("rect %zu,%zu slot %zu: %u -> %u\n", (i / 4) % size_t(cols), (i / 4) / size_t(cols), i % 4, a >> 4, b >> 4);
        }
        return 0;
    }
    std::fprintf(stderr, "usage: isom_core_probe ids <era> | same <map> <px> <py> [extent]\n");
    return 2;
}
