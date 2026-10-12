// Render every palette unit type on a blank badlands map, owners cycling through players 1-8.
//
// usage: unit_gallery <install dir> <out.png> [unit ids]

#include <cstdio>
#include <cstdlib>

#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/sprites.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"
#include "stardraft/units.h"

using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: unit_gallery <install dir> <out.png>\n");
        return 2;
    }
    std::string err;
    GameData data;
    Tileset ts;
    UnitTypes types;
    UnitSprites sprites;
    if (!data.open(argv[1], err) || !ts.load(data, 0, err) || !types.load(data, err) || !sprites.load(data, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::vector<uint16_t> list;
    for (uint16_t i = 0; i < types.size(); ++i)
        if ((types[i].availability & UnitAvail::Palette) && !types[i].is(UnitFlag::Subunit)) list.push_back(i);
    // Optional unit ids after the output: only those (for close-ups).
    if (argc > 3) {
        list.clear();
        for (int a = 3; a < argc; ++a) list.push_back(uint16_t(std::atoi(argv[a])));
    }

    // 16 columns of 160 px cells.
    const int cols = 16, cell = 160;
    int rows = int((list.size() + cols - 1) / cols);
    uint16_t w = uint16_t(cols * cell / 32), h = uint16_t(rows * cell / 32);
    Chk chk;
    IsomGrid grid;
    if (!grid.create(chk, w + (w & 1), h, 0, 1, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::vector<Unit> units;
    for (size_t i = 0; i < list.size(); ++i) {
        uint16_t x = uint16_t((i % cols) * cell + cell / 2), y = uint16_t((i / cols) * cell + cell / 2);
        units.push_back(makeUnit(types[list[i]], uint8_t(i % 8), x, y));
    }
    int missing = 0;
    for (uint16_t t : list)
        if (!sprites.unitSprite(t, 0, ts)) ++missing, std::printf("no sprite: %s\n", types[t].name.c_str());

    Image img = renderTerrain(ts, readTerrain(chk), 1);
    drawUnits(img, units, sprites, ts, 1);
    if (!writePng(argv[2], img)) return 1;
    std::printf("%zu unit types, %d without sprites -> %s\n", list.size(), missing, argv[2]);
    return 0;
}
