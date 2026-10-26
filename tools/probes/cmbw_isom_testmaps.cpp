// Two test maps for a terrain of the mod's own in the isometric terrain list (Ash World's asphalt on dirt, from the
// mod's brush set Asphalt-1-dirt), to see how other editors (SCMDraft) take them:
//  A (asphalt-lava-isom.scx): Lava painted on Dirt with the Isometric brush, then each Dirt/Lava rect and each Lava
//    rect made the mod's asphalt piece of the same place in its set (Lava-dirt1 is the game's own groups, in the same
//    order). The ISOM stays Lava's.
//  B (asphalt-new-isom.scx): the same tiles; the ISOM values of Lava and of its border with Dirt moved past the game's
//    shapes (as a terrain of its own would have).
// usage: cmbw_isom_testmaps <Cosmonarchy install> <Ash World map> <out folder>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "stardraft/chk.h"
#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"
#include "stardraft/isom_core.h"
#include "stardraft/map_file.h"
#include "stardraft/mods/cmbw.h"
#include "stardraft/terrain.h"
#include "stardraft/tileset.h"

namespace fs = std::filesystem;
using namespace stardraft;

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: cmbw_isom_testmaps <install> <Ash World map> <out folder>\n"), 2;
    std::string err;
    GameData data;
    if (!cmbw::open(data, argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    Editor ed(data);
    if (!ed.open(argv[2], err) || ed.era() != 3) return std::fprintf(stderr, "not an Ash World map: %s\n", err.c_str()), 1;
    const_cast<Tileset&>(ed.tileset()).groupPieces.clear();
    const int w = ed.width() * 32, h = ed.height() * 32;
    ed.paintBrush(w / 2, h / 2, 2, Brush{Brush::Diamond, 30.0f});  // Dirt
    ed.paintBrush(w / 2, h / 2, 3, Brush{Brush::Diamond, 6.0f});   // Lava (to be asphalt)
    ed.paintBrush(w / 2 + 400, h / 2 + 100, 3, Brush{Brush::Diamond, 3.0f});
    ed.paintBrush(w / 2 - 350, h / 2 + 150, 3, Brush{Brush::Diamond, 2.0f});
    Chk chk = ed.chk();
    TerrainLayer tile = readTerrain(chk, "TILE");
    const auto terrains = tileGroupTerrains(ed.tileset());
    // Lava-dirt1's blocks (the game's top-left groups) and Asphalt-1-dirt's (the mod's top groups), in the sets' order.
    const uint16_t lava[8] = {192, 196, 204, 200, 208, 212, 220, 216};
    const uint16_t asphalt[8] = {739, 741, 745, 743, 747, 749, 753, 751};
    int made = 0, left = 0;
    for (int y = 0; y < tile.height; ++y)
        for (int x = 0; x + 1 < tile.width; x += 2) {
            const size_t k = size_t(y) * tile.width + x;
            const uint16_t g = uint16_t(tile.tiles[k] >> 4);
            const uint16_t v = uint16_t((x * 7 + y * 13) % 5);  // the variant (0 to 4)
            if (g < terrains.size() && terrains[g][0] == 3 && terrains[g][1] == 0) {
                const uint16_t s = uint16_t((x + y * 3) % 6);  // (788/789 subtiles 0 to 5)
                tile.tiles[k] = uint16_t(788 << 4 | s), tile.tiles[k + 1] = uint16_t(789 << 4 | s), ++made;
                continue;
            }
            bool found = false;
            for (int b = 0; b < 8 && !found; ++b)
                for (int row = 0; row < 2 && !found; ++row)
                    if (g == lava[b] + 2 * row) {
                        const uint16_t a = uint16_t(asphalt[b] + row);
                        tile.tiles[k] = uint16_t(a << 4 | 2 * v), tile.tiles[k + 1] = uint16_t(a << 4 | (2 * v + 1));
                        found = true, ++made;
                    }
            if (!found && g >= 224 && g <= 239) ++left;
        }
    std::printf("asphalt rects made: %d; Dirt/Lava rects with no asphalt piece (left lava): %d\n", made, left);
    Bytes t;
    for (uint16_t value : tile.tiles) t.push_back(uint8_t(value)), t.push_back(uint8_t(value >> 8));
    chk.set("TILE", t), chk.set("MTXM", t);
    const fs::path out(argv[3]);
    if (!writeMap(out / "asphalt-lava-isom.scx", chk.serialize(), {}, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    // (the middle of A as a picture: asphalt-lava-isom.png)
    TerrainLayer part;
    part.width = 48, part.height = 32;
    for (int y = 0; y < part.height; ++y)
        for (int x = 0; x < part.width; ++x) part.tiles.push_back(tile.tiles[size_t(tile.height / 2 - 16 + y) * tile.width + tile.width / 2 - 24 + x]);
    writePng(out / "asphalt-lava-isom.png", renderTerrain(ed.tileset(), part));
    // B: Lava (3) and the Dirt/Lava shapes (55 to 68) moved past the game's shapes.
    const Section* isom = chk.find("ISOM");
    Bytes moved = isom->data;
    const uint16_t past = IsomTiles::forTileset(ed.tileset())->shapeCount();
    int changed = 0;
    for (size_t i = 0; i + 1 < moved.size(); i += 2) {
        const uint16_t word = uint16_t(moved[i] | moved[i + 1] << 8), shape = uint16_t(word >> 4);
        if (shape != 3 && (shape < 55 || shape > 68)) continue;
        const uint16_t to = uint16_t(past + (shape == 3 ? 0 : shape - 55 + 1));
        const uint16_t nw = uint16_t(to << 4 | (word & 15));
        moved[i] = uint8_t(nw), moved[i + 1] = uint8_t(nw >> 8), ++changed;
    }
    chk.set("ISOM", moved);
    std::printf("ISOM values moved past the game's shapes (%u and up): %d\n", unsigned(past), changed);
    if (!writeMap(out / "asphalt-new-isom.scx", chk.serialize(), {}, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    return 0;
}
