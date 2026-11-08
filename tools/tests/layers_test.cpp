// Map layers (stardraft/layers.h): the file, moves, merges, and the Editor's flattening.
//
// usage: layers_test <install dir> <scratch dir>

#include <cstdio>
#include <filesystem>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"
#include "stardraft/isom.h"

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

    // The map's files: notes, and what File > Export keeps.
    {
        namespace fs = std::filesystem;
        Editor ed(data);
        check(ed.newMap(64, 64, 0, Dirt, err), "a new map");
        check(ed.notes().empty(), "no notes");
        ed.setNotes("Notes \xC3\xA5 here");
        check(ed.notes() == "Notes \xC3\xA5 here" && ed.archiveFile(kNotesFile), "the notes are a file of the map");
        ed.undo();
        check(ed.notes().empty(), "an undo takes them away");
        ed.redo();
        check(!ed.notes().empty(), "a redo brings them back");
        ed.setArchiveFiles({{"staredit\\wav\\a.wav", Bytes{1, 2, 3}}, {"est.pcx", Bytes{4}}, {kLayersFile, Bytes{5}}});
        check(ed.keepInExport("staredit\\wav\\a.wav") && ed.keepInExport("est.pcx"), "a file goes by default");
        check(!ed.keepInExport(kNotesFile), "the notes do not go by default");
        check(!ed.keepInExport(kLayersFile) && !Editor::canKeepInExport(kLayersFile), "the layers never go");
        ed.setKeepInExport("est.pcx", false);
        ed.setKeepInExport(kNotesFile, true);
        check(!ed.keepInExport("est.pcx") && ed.keepInExport(kNotesFile), "the settings hold");
        const fs::path out = fs::path(argv[2]) / "export-files.scx";
        std::vector<std::string> leftOut, inUse;
        check(ed.exportFlattened(out, err, &leftOut, &inUse), "the export");
        check(leftOut.size() == 1 && leftOut[0] == "est.pcx" && inUse.empty(), "it says what stays out");
        std::vector<ArchiveFile> back;
        check(readMapFiles(out, back, err), "the export reads");
        const auto has = [&](const std::string& n) { return std::any_of(back.begin(), back.end(), [&](const ArchiveFile& f) { return f.name == n; }); };
        check(has("staredit\\wav\\a.wav") && has(kNotesFile), "the export has what it keeps");
        check(!has("est.pcx") && !has(kLayersFile) && !has(kExportListFile), "and not the others");
        ed.renameArchiveFile("est.pcx", "est2.pcx");
        check(!ed.keepInExport("est2.pcx") && ed.keepInExport("est.pcx"), "the setting goes with the new name");
        ed.undo();
        check(!ed.keepInExport("est.pcx") && ed.archiveFile("est.pcx"), "an undo brings the name and the setting back");
        check(!archiveNameUsable("staredit\\scenario.chk") && !archiveNameUsable("(listfile)") && !archiveNameUsable("File00000001.pcx") && archiveNameUsable("est.pcx"), "names a map cannot keep");
        ed.setKeepInExport("est.pcx", true);
        ed.setKeepInExport(kNotesFile, false);
        check(!ed.archiveFile(kExportListFile), "all as by default: no list");
        ed.setQuietArchiveFile(kMapSettingsFile, Bytes{9});
        check(!ed.keepInExport(kMapSettingsFile) && ed.archiveFile(kMapSettingsFile), "the map's settings are a file that does not go");
    }
    // A save's check of the terrain; and a layered map whose terrain is not as at its last save (open it to see the message).
    {
        namespace fs = std::filesystem;
        Editor ed(data);
        check(ed.newMap(64, 64, 0, Dirt, err), "a new map");
        LayerStack s;
        s.add("One", 0).corners[{8, 15}] = HighDirt;
        ed.setArchiveFiles({{kLayersFile, s.serialize()}, {kLayersBaseFile, ed.layerBaseNow()}});
        const fs::path saved = fs::path(argv[2]) / "saved-layers.scx";
        check(ed.save(saved, err), "a layered map saves");
        Editor back(data);
        check(back.open(saved, err), "and opens");
        const ArchiveFile* mark = back.archiveFiles().empty() ? nullptr : back.archiveFile(kLayersCheckFile);
        check(mark && std::string(mark->data.begin(), mark->data.end()) == back.terrainCheck(), "a save's check is the terrain's");
        std::vector<ArchiveFile> other = back.archiveFiles();
        for (ArchiveFile& f : other)
            if (f.name == kLayersCheckFile) f.data = {'0', '0'};
        check(writeMap(fs::path(argv[2]) / "stale-layers.scx", back.savedChk(), other, err), "a map with a check that differs");
    }
    // An edit of the Background under a layer: the cliff on the layer keeps its tiles, and its stacks stay whole.
    {
        Editor ed(data);
        check(ed.newMap(64, 64, 0, Dirt, err), "a new map");
        LayerStack s;
        MapLayer& high = s.add("High", 0);
        for (int y = 10; y < 26; ++y)
            for (int x = 6; x < 16; ++x)
                if ((x + y) & 1) high.corners[{x, y}] = HighDirt;
        const Bytes base = ed.layerBaseNow();
        check(ed.composeLayers(base, s), "the cliff is on the map");
        const std::vector<uint16_t> before = ed.layerTileSection().tiles;
        uint16_t Grass = 0;
        const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
        for (uint16_t t = 1; table && t < 64 && !Grass; ++t)
            if (table->isTerrain(t) && table->terrainName(t) == std::string("Grass")) Grass = t;
        check(Grass != 0, "Badlands has Grass");
        Bytes painted = base;
        ed.noteAskedCorners(true);
        for (const uint16_t ground : {Grass, Water, Dirt, Grass, Grass, Dirt}) {
            std::vector<uint16_t> last = ed.layerTileSection().tiles;
            // (the Brush over the cliff's foot and past it)
            Brush brush;
            brush.size = 2;
            ed.beginTerrainStroke();
            for (int px = 256; px <= 1200; px += 64) ed.paintBrush(px, 860, ground, brush);
            ed.endTerrainStroke();
            std::map<std::pair<int, int>, uint16_t> asked;
            for (const auto& [x, y] : ed.takeAskedCorners()) asked[{x, y}] = ground;
            LayerStack one;
            one.add("", 0).corners = asked;
            ed.composeLayers(painted, one, nullptr, true);
            painted = ed.layerBaseNow();
            ed.composeLayers(painted, s, nullptr, false, &last, -1);
            std::vector<std::pair<int, int>> where;
            const int misfits = countTileMisfits(ed.chk(), ed.tileset(), &where);
            std::printf("  ground %d: %d tiles do not fit\n", int(ground), misfits);
            check(misfits == 0, "the stacks fit after an edit under the layer");
            int kept = 0, top = 0;
            const std::vector<uint16_t>& nowTiles = ed.layerTileSection().tiles;
            for (int y = 8; y < 16; ++y)
                for (int x = 8; x < 36; ++x, ++top) kept += nowTiles[size_t(y) * 64 + x] == before[size_t(y) * 64 + x];
            std::printf("  the top of the layer: %d of %d tiles kept\n", kept, top);
            check(kept == top, "the top of the layer does not change");
        }
    }
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

        // A null tile over a layer's terrain does not stay when the map is made again (a cut on a layer above it).
        {
            const size_t at = size_t(11) * 64 + 14;
            ed.composeLayers(base, s);
            std::vector<std::pair<int, int>> one{{14, 11}};
            check(ed.placeTileCells(one, 0) == 1 && ed.terrain().tiles[at] == 0, "a null tile is placed");
            ed.composeLayers(base, s);
            check(ed.terrain().tiles[at] != 0, "made again, the terrain shows there");
        }

        // The Background hidden: black where no layer has terrain; nothing showing: all black.
        {
            s.background.visible = false;
            ed.composeLayers(base, s);
            check(ed.terrain().tiles[size_t(60) * 64 + 60] == 0, "the Background hidden: black outside of the layers");
            s.layers[0].visible = false;
            ed.composeLayers(base, s);
            check(std::all_of(ed.terrain().tiles.begin(), ed.terrain().tiles.end(), [](uint16_t t) { return t == 0; }), "nothing showing: all black");
            s.background.visible = true, s.layers[0].visible = true;
            ed.composeLayers(base, s);
            check(ed.terrain().tiles[size_t(60) * 64 + 60] != 0, "shown again: the Background's tiles");
        }

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

    // A doodad's overlay sprite: it comes with the doodad, and one that was removed stays out.
    {
        Editor ed(data);
        check(ed.newMap(64, 64, 0, Dirt, err), "a map for the overlays");
        bool placed = false;
        for (const DoodadType& t : ed.doodadTypes().all())
            if (t.overlaySprite && ed.placeDoodad(t.id, 640, 640, false)) {
                placed = true;
                break;
            }
        check(placed && ed.sprites().size() == 1, "a doodad with its overlay");
        if (placed && ed.sprites().size() == 1) {
            const uint16_t overlay = ed.sprites()[0].id;
            check(!ed.layerDoodads()[0].noOverlay && ed.layerSprites().empty(), "the overlay is the doodad's");
            const Bytes base = ed.layerBaseNow();
            LayerStack s;
            s.background.doodads.push_back({ed.layerDoodads()[0], int(ed.doodads()[0].x), int(ed.doodads()[0].y), true});
            ed.composeLayers(base, s);
            check(ed.sprites().size() == 1 && ed.sprites()[0].id == overlay && !ed.layerDoodads()[0].noOverlay, "flattened: the overlay is back");
            // (off the grid: the overlay stays at the doodad's place, so it stays the doodad's)
            s.background.doodads[0].x += 5;
            ed.composeLayers(base, s);
            check(ed.sprites().size() == 1 && ed.sprites()[0].x == ed.doodads()[0].x && !ed.layerDoodads()[0].noOverlay && ed.layerSprites().empty(), "off the grid: the overlay with it");
            s.background.doodads[0].x -= 5;
            // (a Background doodad that does not fit keeps its overlay too)
            const int fitsX = s.background.doodads[0].x, fitsY = s.background.doodads[0].y;
            s.background.doodads[0].x = ed.width() * 32 - 1, s.background.doodads[0].y = ed.height() * 32 - 1;
            ed.composeLayers(base, s);
            check(ed.doodads().size() == 1 && ed.sprites().size() == 1 && !ed.layerDoodads()[0].noOverlay && ed.layerSprites().empty(), "one that does not fit: the overlay with it");
            s.background.doodads[0].x = fitsX, s.background.doodads[0].y = fitsY;
            ed.composeLayers(base, s);

            check(ed.deleteSprite(0) && ed.sprites().empty(), "the overlay is removed");
            const std::vector<Doodad> without = ed.layerDoodads();
            check(without.size() == 1 && without[0].noOverlay, "the doodad knows");
            s.background.doodads[0].item = without[0];
            ed.composeLayers(base, s);
            check(ed.doodads().size() == 1 && ed.sprites().empty() && ed.layerDoodads()[0].noOverlay, "flattened: it stays out");

            LayerStack back;
            check(LayerStack::parse(s.serialize(), back) && back.background.doodads[0].item.noOverlay && back.background.doodads[0].shown, "in the file too");

            // A move keeps it out.
            check(ed.moveDoodad(0, 960, 960, false) && ed.sprites().empty() && ed.layerDoodads()[0].noOverlay, "moved: it stays out");
            check(ed.moveDoodads({0}, 64, 64, false) && ed.sprites().empty() && ed.layerDoodads()[0].noOverlay, "moved by a drag: it stays out");
            s.background.doodads[0] = {ed.layerDoodads()[0], int(ed.doodads()[0].x), int(ed.doodads()[0].y), true};

            // Two at one spot, with one sprite: the stack says what the map says, so nothing looks changed at the next edit.
            MapLayer& over = s.add("over", 0);
            Doodad same = s.background.doodads[0].item;
            same.noOverlay = false;
            over.doodads.push_back({same, s.background.doodads[0].x, s.background.doodads[0].y, false});
            ed.composeLayers(base, s);
            const std::vector<Doodad> both = ed.layerDoodads();
            check(ed.sprites().size() == 1 && both.size() == 2 && !both[0].noOverlay && both[1].noOverlay, "two at one spot: the sprite is the first one's");
            check(s.background.doodads[0].item.noOverlay && !s.layers[0].doodads[0].item.noOverlay, "flattening does not change the stack");
            s.layers.clear();

            // A sprite of its own on a doodad with no overlay reads as that overlay: the lists stay in step, the stack keeps both.
            MapLayer& onto = s.add("onto", 0);
            onto.sprites.push_back({MapSprite{overlay, 0, 0, 0, 0, SpriteFlag::DrawAsSprite}, s.background.doodads[0].x, s.background.doodads[0].y, false});
            onto.sprites.push_back({MapSprite{overlay, 0, 0, 0, 0, SpriteFlag::DrawAsSprite}, 100, 100, false});
            ed.composeLayers(base, s);
            check(s.background.doodads[0].item.noOverlay && s.layers[0].sprites.size() == 2 && !s.layers[0].sprites[0].shown && s.layers[0].sprites[1].shown, "a sprite on the spot: the stack keeps both");
            check(ed.sprites().size() == 2 && ed.layerSprites().size() == 1 && s.owners().sprites.size() == 1, "the sprites' owners stay in step");
            s.layers[0].sprites[0].x = 200;
            ed.composeLayers(base, s);
            check(ed.layerDoodads()[0].noOverlay && ed.layerSprites().size() == 2 && s.owners().sprites.size() == 2 && s.layers[0].sprites[0].shown, "moved off: the sprite is its own again, and the doodad has no overlay");
        }
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
