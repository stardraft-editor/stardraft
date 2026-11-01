#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom_core.h"
#include <algorithm>
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace stardraft;
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "symmetry_editor_test <game installation>\n");
        return 2;
    }
    GameData game;
    std::string error;
    assert(game.open(argv[1], error));
    for (int era = 0; era < 8; ++era) {
        Editor ed(game);
        assert(ed.newMap(64, 64, uint16_t(era), 1, error));
        IsomCore core;
        assert(core.load(ed.chk(), ed.tileset(), error));
        const auto brushes = core.brushes();
        assert(brushes.size() >= 3);
        const auto before = ed.chk().serialize();
        const auto targetBefore = ed.terrainAt(1600, 512);
        ed.setMirrors(Mirror::LeftRight);
        const auto transforms = ed.availableCopies();
        assert(transforms.size() == 2);
        CopyOptions options;
        options.terrain = brushes[2];
        ed.setCopyOptions(options);
        const Brush brush{Brush::Diamond, 2};
        assert(!ed.brushAllTerrain(512, 512, brush, brushes[1]));
        assert(ed.paintBrush(512, 512, brushes[1], brush));
        assert(ed.terrainAt(448, 512) == brushes[1]);
        assert(ed.terrainAt(1600, 512) == brushes[2]);
        const auto painted = ed.chk().serialize();
        assert(ed.paintBrush(512, 512, brushes[0], brush, false));
        assert(ed.terrainAt(448, 512) == brushes[0] && ed.terrainAt(1600, 512) == brushes[0]);
        ed.undo();
        assert(ed.chk().serialize() == painted);
        ed.undo();
        assert(ed.chk().serialize() == before);
        ed.redo();
        assert(ed.chk().serialize() != before);
        ed.undo();
        // The spray and legacy entry points use the same per-copy choices as the brush.
        ed.sprayTerrain(512, 512, brushes[1], brush, 32, 42);
        bool sprayedCopy = false;
        for (int y = 256; y <= 768; y += 32)
            for (int x = 1280; x <= 1792; x += 64) sprayedCopy = sprayedCopy || ed.terrainAt(x, y) == brushes[2];
        assert(sprayedCopy);
        ed.undo();
        assert(ed.chk().serialize() == before);
        ed.paintTerrain(512, 512, brushes[1], 2);
        assert(ed.terrainAt(1600, 512) == brushes[2]);
        ed.undo();
        assert(ed.chk().serialize() == before);
        assert(ed.paintTerrainLine(448, 512, 576, 512, brushes[1], brush));
        assert(ed.terrainAt(1600, 512) == brushes[2]);
        ed.undo();
        assert(ed.chk().serialize() == before);
        assert(ed.fillTerrainPolygon({{320, 384}, {704, 384}, {704, 640}, {320, 640}}, brushes[1]));
        assert(ed.terrainAt(1600, 512) == brushes[2]);
        ed.undo();
        assert(ed.chk().serialize() == before);
        // Multi-terrain fill keeps its patch algorithm on the source island while
        // a separate mirrored island uses its explicit copy terrain.
        ed.setCopyOptions({});
        const auto island = *std::find_if(brushes.begin(), brushes.end(), [&](uint16_t t) { return t != targetBefore; });
        assert(ed.paintBrush(512, 512, island, Brush{Brush::Diamond, 4}));
        ed.setCopyOptions(options);
        assert(ed.fillTerrainMix(512, 512, {uint16_t(targetBefore), island}, 2, 77));
        assert(ed.terrainAt(1600, 512) == brushes[2]);
        ed.undo();
        ed.undo();
        assert(ed.chk().serialize() == before);
        options.choices.push_back({transforms[1].key(), false});
        ed.setCopyOptions(options);
        assert(ed.symmetricPoints(300, 400).size() == 1);
        assert(ed.paintBrush(512, 512, brushes[1], brush));
        assert(ed.terrainAt(1600, 512) == targetBefore);
        ed.undo();
        options.choices.front().enabled = true;
        options.terrain = -1;
        options.rawTile = 32;
        ed.setCopyOptions(options);
        const auto raw = ed.symmetricRawTiles(8, 8, 16);
        assert(raw.size() == 2 && raw[0].value == 16 && raw[1].value == 32);
        assert(ed.symmetricPoints(1024, 400).size() == 1);
        ed.setTurns(4);
        ed.setMirrors(3);
        options.direct = true;
        ed.setCopyOptions(options);
        assert(ed.availableCopies().size() == 6);
        options = {};
        options.opposite = true;
        ed.setMirrors(0), ed.setTurns(1), ed.setCopyOptions(options);
        assert(ed.symmetricPoints(512, 512).size() == 2);
        assert(ed.symmetry() == Symmetry::Rotate180);
        const auto opposite = CopyTransform{-1, 0, 0, -1}.key();
        options.choices.push_back({opposite, false});
        ed.setCopyOptions(options);
        assert(ed.symmetricPoints(512, 512).size() == 1);
        options.choices.front().enabled = true;
        options.choices.front().terrain = brushes[2];
        ed.setCopyOptions(options);
        assert(ed.paintBrush(512, 512, brushes[1], brush));
        assert(ed.terrainAt(1536, 1536) == brushes[2]);
        ed.undo();
        assert(ed.chk().serialize() == before);
        options = {};
        options.direct = options.mirroredRotations = true;
        ed.setCopyOptions(options), ed.setTurns(16);
        assert(ed.availableCopies().size() == 32 && ed.symmetricPoints(300, 400).size() == 32);
        ed.setSymmetry(Symmetry::None);
        assert(ed.symmetricPoints(300, 400).size() == 1);
        options = {};
        ed.setCopyOptions(options);
        ed.setTurns(1);
        ed.setMirrors(1);
        const auto start = std::chrono::steady_clock::now();
        ed.paintBrush(512, 512, brushes[2], brush);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ed.undo();
        assert(ed.chk().serialize() == before);
        std::printf("Tileset %d: distinct copy terrain in brush/spray/line/polygon/legacy, multi-terrain fill, erase, disabled copy, raw overrides, center overlap, "
                    "direct union, atomic undo/redo; "
                    "brush %.1f ms\n",
                    era, ms);
    }
}
