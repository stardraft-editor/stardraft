// Map layers (stardraft/layers.h): the file, moves, merges, and the Editor's flattening.
//
// usage: layers_test <install dir> <scratch dir>

#include <cstdio>
#include <filesystem>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
static Bytes readTerrainBytes(const Editor& ed) {
    Bytes out;
    for (uint16_t v : ed.terrain().tiles) out.push_back(uint8_t(v)), out.push_back(uint8_t(v >> 8));
    return out;
}
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    if (argc < 3) return std::fprintf(stderr, "usage: layers_test <install dir> <scratch dir>\n"), 2;

    // The file.
    {
        LayerStack s;
        MapLayer& a = s.add("One", 0);
        a.corners[{-5, 2}] = 3, a.corners[{4, 5}] = 2;
        a.units.push_back({Unit{}, -100, 40, false});
        a.doodads.push_back({Doodad{7, 1, 2, 3, 0}, 5000, -3, false});
        a.sprites.push_back({MapSprite{9, 1, 2, 3, 0, 0x1000}, 1, 2, false});
        s.add("Two", 1).visible = false;
        s.layers[1].locked = true;
        LayerStack back;
        check(LayerStack::parse(s.serialize(), back), "the file reads");
        check(back.layers.size() == 2 && back.layers[0].name == "One" && back.layers[1].name == "Two" && !back.layers[1].visible && back.layers[1].locked && !back.layers[0].locked, "its layers");
        check(back.layers[0].corners == s.layers[0].corners && back.layers[0].units[0].x == -100 && back.layers[0].doodads[0].y == -3 &&
                  back.layers[0].sprites[0].item.flags == 0x1000,
              "its contents, also outside of the map");
        check(back.serialize() == s.serialize(), "the same bytes again");
        Bytes cut = s.serialize();
        cut.resize(cut.size() - 3);
        check(!LayerStack::parse(cut, back), "a short file does not read");
        check(!LayerStack::parse({1, 2, 3, 4, 5}, back), "another file does not read");
    }

    // Moves keep the corners' grid.
    {
        for (const auto& [px, py] : std::initializer_list<std::pair<int, int>>{{0, 0}, {64, 0}, {64, 32}, {32, 32}, {-70, 15}, {100, -200}, {17, 9}}) {
            int dx, dy;
            snapLayerMove(px, py, dx, dy);
            check(((dx + dy) & 1) == 0, "a move keeps the grid");
        }
        MapLayer l;
        l.corners[{3, 2}] = 2, l.corners[{10, 11}] = 4;
        l.units.push_back({Unit{}, 192, 64, false});
        moveLayerContents(l, -4, 2);
        check(l.corners.count({-1, 4}) && l.corners.count({6, 13}), "the corners move, also out of the map");
        check(l.units[0].x == 192 - 256 && l.units[0].y == 128, "the units move with them");
        moveLayerContents(l, 2, 0, LayerArea{0, 0, 1000, 1000});
        check(l.corners.count({-1, 4}) && l.corners.count({8, 13}) && l.corners.count({6, 13}) == 0, "an area moves only what is in it");
    }

    // A merge: the upper layer wins.
    {
        LayerStack s;
        s.add("a", 0).corners[{1, 2}] = 2;
        s.add("b", 1).corners[{5, 6}] = 3;
        MapLayer& c = s.add("c", 2);
        c.corners[{1, 2}] = 4;
        c.units.push_back({Unit{}, 1, 1, false});
        const int at = s.merge({2, 0});
        check(at == 0 && s.layers.size() == 2 && s.layers[0].name == "a" && s.layers[1].name == "b", "the lowest layer has the result");
        check(s.layers[0].corners[{1, 2}] == 4 && s.layers[0].units.size() == 1, "the upper layer's corners win");
    }

    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const uint16_t Dirt = 1, HighDirt = 2, Water = 3;

    // Flattening.
    {
        Editor ed(data);
        check(ed.newMap(64, 64, 0, Dirt, err), "a new map");
        const Bytes base = ed.layerBaseNow();
        const Bytes plain = ed.savedChk();
        LayerStack s;
        MapLayer& a = s.add("High", 0);
        // (a block of corners; also some outside of the map)
        for (int y = 10; y < 20; ++y)
            for (int x = 6; x < 12; ++x)
                if ((x + y) & 1) a.corners[{x, y}] = HighDirt;
        a.corners[{-40, 3}] = Water;
        Unit u;
        u.type = 0, u.owner = 0, u.x = 0, u.y = 0, u.serial = 77;
        a.units.push_back({u, 400, 400, false});
        a.units.push_back({u, -400, 400, false});
        std::vector<std::pair<int, int>> fitted;
        check(ed.composeLayers(base, s, &fitted), "the layers flatten");
        check(ed.isomGrid().terrainAt(8, 15) == HighDirt, "the layer's terrain is on the map");
        check(ed.units().size() == 1 && ed.units()[0].x == 400, "its unit on the map is there, the one outside is not");
        check(!fitted.empty(), "the edges show");
        const Bytes once = ed.savedChk();
        check(ed.composeLayers(base, s), "again");
        check(ed.savedChk() == once, "the same layers give the same map");
        if (ed.savedChk() != once) {
            Chk x, y;
            ParseReport rx, ry;
            (void)rx, (void)ry;
            x = Chk::parse(once), y = Chk::parse(ed.savedChk());
            for (const Section& sec : x.sections)
                if (const Section* o = y.find(sec.name); !o || o->data != sec.data) {
                    size_t n = 0;
                    if (o) for (size_t i = 0; i < std::min(sec.data.size(), o->data.size()); ++i) n += sec.data[i] != o->data[i];
                    std::printf("  differs: %.4s (%zu bytes)\n", sec.name.c.data(), n);
                }
        }

        s.layers[0].visible = false;
        ed.composeLayers(base, s);
        check(ed.isomGrid().terrainAt(8, 15) == Dirt && ed.units().empty(), "a hidden layer is not on the map");
        check(ed.savedChk() == plain, "the map is the Background again");
        s.layers[0].visible = true;

        // A move out of the map and back.
        moveLayerContents(s.layers[0], -40, 0);
        ed.composeLayers(base, s);
        check(ed.isomGrid().terrainAt(8, 15) == Dirt, "moved out: not on the map");
        check(!s.layers[0].corners.empty(), "moved out: the layer keeps it");
        moveLayerContents(s.layers[0], 40, 0);
        ed.composeLayers(base, s);
        check(ed.composeLayers(base, s), "moved back");
        check(ed.isomGrid().terrainAt(8, 15) == HighDirt && ed.invalidTerrain() == 0, "moved back: the terrain is there");

        // Placed tiles: on the map while the layer shows, and they move with it.
        s.layers[0].tiles[{40, 40}] = 1234;
        ed.composeLayers(base, s);
        check(ed.terrain().tiles[size_t(40) * 64 + 40] == 1234, "a layer's placed tile is on the map");
        s.layers[0].visible = false;
        ed.composeLayers(base, s);
        check(ed.terrain().tiles[size_t(40) * 64 + 40] != 1234, "not while it is hidden");
        s.layers[0].visible = true;
        s.layers[0].tiles.clear();
        ed.composeLayers(base, s);

        // NULL: a corner taken out shows what is under it.
        for (int y = 12; y < 18; ++y)
            for (int x = 8; x < 10; ++x) s.layers[0].corners.erase({x, y});
        ed.composeLayers(base, s);
        check(ed.isomGrid().terrainAt(9, 16) != Water, "what is under NULL shows");
        check(ed.invalidTerrain() == 0, "the terrain stays valid");

        // Undo: the files and the map go back together.
        ed.beginLayerEdit();
        s.add("Water", 1).corners[{20, 21}] = Water;
        ed.composeLayers(base, s);
        ed.endLayerEdit(true, {{kLayersFile, s.serialize()}, {kLayersBaseFile, base}});
        check(ed.archiveFile(kLayersFile) != nullptr, "the layers are in the map's files");
        const Bytes withWater = ed.savedChk();
        ed.undo();
        check(ed.archiveFile(kLayersFile) == nullptr, "undo takes the files back");
        check(ed.savedChk() != withWater, "undo takes the map back");
        ed.redo();
        check(ed.archiveFile(kLayersFile) != nullptr && ed.savedChk() == withWater, "redo gives both again");

        // Save keeps the layers; Export has none.
        const fs::path saved = fs::path(argv[2]) / "layers-saved.scx", exported = fs::path(argv[2]) / "layers-exported.scx";
        check(ed.save(saved, err), "a save");
        std::vector<ArchiveFile> files;
        check(readMapFiles(saved, files, err), "it reads");
        const auto has = [&](const char* name) {
            return std::any_of(files.begin(), files.end(), [&](const ArchiveFile& f) { return f.name == name; });
        };
        check(has(kLayersFile) && has(kLayersBaseFile), "a save keeps the layers");
        Bytes chk;
        check(ed.exportFlattened(exported, err), "an export");
        files.clear();
        check(readMapFiles(exported, files, err) && !has(kLayersFile) && !has(kLayersBaseFile), "an export has no layers");
        check(readMapFile(exported, chk, err) && chk == withWater, "an export is the flattened map");
        check(ed.path() == saved, "the open map stays the saved one");
    }

    // An edit's objects: each stays with its layer, new ones go to the active layer.
    {
        LayerStack s;
        MapLayer& a = s.add("a", 0);
        Unit u;
        u.x = 10, u.y = 10, u.serial = 1;
        a.units.push_back({u, 10, 10, true});
        Unit off = u;
        off.serial = 2;
        a.units.push_back({off, -50, 10, false});
        s.add("b", 1);
        Unit bg = u;
        bg.serial = 3, bg.x = 5;
        s.background.units.push_back({bg, 5, 10, true});
        const LayerObjectLists before{{bg, u}, {}, {}};
        check(s.owners().units == std::vector<int>({-1, 0}), "the owners: the Background first, only what is on the map");
        LayerObjectLists now = before;
        now.units[1].x = 30;
        captureLayerObjects(s, before, now, s.owners(), 1);
        check(s.background.units.size() == 1 && s.background.units[0].item.serial == 3, "the Background keeps its unit");
        check(s.layers[0].units.size() == 2, "the layer keeps its units, also the one outside of the map");
        check(std::any_of(s.layers[0].units.begin(), s.layers[0].units.end(), [](const auto& o) { return o.item.serial == 1 && o.x == 30; }),
              "its moved unit too");
        const LayerObjectLists moved = now;
        Unit fresh = u;
        fresh.serial = 4;
        now.units.push_back(fresh);
        captureLayerObjects(s, moved, now, s.owners(), 1);
        check(s.layers[1].units.size() == 1 && s.layers[1].units[0].item.serial == 4, "the new unit goes to the active layer");

        // A moved doodad goes to the end of the list: the others keep their layers.
        LayerStack d;
        d.add("a", 0).doodads.push_back({Doodad{1, 100, 100, 0, 0}, 100, 100, true});
        d.add("b", 1).doodads.push_back({Doodad{2, 200, 200, 0, 0}, 200, 200, true});
        const LayerObjectLists was{{}, {Doodad{1, 100, 100, 0, 0}, Doodad{2, 200, 200, 0, 0}}, {}};
        const LayerObjectLists rotated{{}, {Doodad{2, 200, 200, 0, 0}, Doodad{1, 132, 100, 0, 0}}, {}};
        captureLayerObjects(d, was, rotated, d.owners(), 1);
        check(d.layers[0].doodads.size() == 1 && d.layers[0].doodads[0].x == 132, "a moved doodad stays in its layer");
        check(d.layers[1].doodads.size() == 1 && d.layers[1].doodads[0].x == 200, "the other one stays in its layer");
    }

    // The Background's doodads: on the map from the stack, gone when they are out of it, with their tiles.
    {
        Editor ed(data);
        check(ed.newMap(64, 64, 0, Dirt, err), "a map for the doodads");
        const Bytes plain = readTerrainBytes(ed);
        int placedId = -1;
        for (int id = 0; id < 400 && placedId < 0; ++id)
            if (ed.placeDoodad(uint16_t(id), 640, 640, false)) placedId = id;
        check(placedId >= 0, "a doodad is on the map");
        if (placedId >= 0) {
            const Doodad d = ed.doodads()[0];
            const Bytes base = ed.layerBaseNow();
            LayerStack s;
            s.background.doodads.push_back({d, int(d.x), int(d.y), true});
            ed.composeLayers(base, s);
            check(ed.doodads().size() == 1 && s.background.doodads[0].shown, "it stays on the map");
            s.background.doodads.clear();
            ed.composeLayers(base, s);
            check(ed.doodads().empty() && readTerrainBytes(ed) == plain, "out of the stack: gone, with its tiles");
            // The Background's objects come back as they were, also outside of the map.
            Unit far;
            far.serial = 9, far.x = 60000, far.y = 60000;
            s.background.units.push_back({far, 60000, 60000, true});
            ed.composeLayers(base, s);
            check(ed.units().size() == 1 && ed.units()[0].x == 60000, "a Background unit outside of the map stays");
            s.background.units.clear();
            // A doodad that does not fit stays in its layer, not shown.
            MapLayer& l = s.add("a", 0);
            l.doodads.push_back({d, ed.width() * 32 - 1, ed.height() * 32 - 1, false});
            ed.composeLayers(base, s);
            check(!s.layers[0].doodads[0].shown && s.layers[0].doodads.size() == 1, "one at the map's edge: kept, not shown");
        }
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
