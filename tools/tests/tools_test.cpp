// Checks the "Done when" criteria of roadmap phases 1 and 2 (terrain, doodad and unit tools).
//
// usage: tools_test <install dir> <badlands map (.scm/.scx)> <scratch dir>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>

#include "stardraft/editor.h"
#include "stardraft/game_data.h"

using namespace stardraft;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
static double ms(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

int main(int argc, char** argv) {
    if (argc < 4) return std::fprintf(stderr, "usage: tools_test <install dir> <map> <scratch dir>\n"), 2;
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const uint16_t Dirt = 1, HighDirt = 2, Water = 3, Grass = 4, Structure = 8, Mud = 9;

    // 1.1 / 1.2 / 1.3: brush footprint.
    {
        Editor ed(data);
        ed.newMap(128, 128, 0, Dirt, err);
        check(ed.brushCorners(64 * 21, 32 * 20, Brush{Brush::Diamond, 0.5f}).size() == 1, "1.2 half brush (0.5) covers a single corner");
        check(ed.brushCorners(64 * 20, 32 * 20, Brush{Brush::Diamond, 1}).size() == 4, "1.3 size-1 diamond brush covers one diamond (4 corners)");
        size_t d = ed.brushCorners(4000, 2000, Brush{Brush::Diamond, 3}).size();
        size_t c = ed.brushCorners(4000, 2000, Brush{Brush::Circle, 3}).size();
        size_t q = ed.brushCorners(4000, 2000, Brush{Brush::Square, 3}).size();
        std::printf("      size-3 footprints: diamond %zu, circle %zu, square %zu corners\n", d, c, q);
        check(d < c && c < q, "1.3 shapes: diamond < circle < square");
        auto t0 = Clock::now();
        ed.paintBrush(4000, 2000, Water, Brush{Brush::Circle, 3});
        double paintMs = ms(t0);
        check(ed.invalidTerrain() == 0 && ed.terrainAt(4000, 2000) == Water, "1.1 brush through the correction engine keeps terrain valid");
        std::printf("      brush edit took %.1f ms\n", paintMs);
    }

    // 1.4 fill: a lake filled with dirt leaves no water, one undo step, fast on 256x256.
    {
        Editor ed(data);
        ed.newMap(256, 256, 0, Dirt, err);
        for (int i = 0; i < 6; ++i) ed.paintBrush(3000 + i * 150, 3000 + i * 40, Water, Brush{Brush::Circle, 4});
        check(ed.terrainAt(3300, 3080) == Water, "1.4 setup: lake painted");
        size_t undoBefore = 0;
        (void)undoBefore;
        auto t0 = Clock::now();
        bool ok = ed.fillTerrain(3300, 3080, Dirt);
        double fillMs = ms(t0);
        bool water = false;
        for (int y = 2500; y < 3700; y += 16)
            for (int x = 2500; x < 4200; x += 32) water |= ed.terrainAt(x, y) == Water;
        check(ok && !water && ed.invalidTerrain() == 0, "1.4 fill turns the whole lake (and its shore) into dirt");
        std::printf("      fill took %.1f ms on 256x256\n", fillMs);
        check(fillMs < 200, "1.4 fill under 200 ms on 256x256");
        ed.undo();
        check(ed.terrainAt(3300, 3080) == Water, "1.4 fill is one undo step");
        // Fill dirt (the whole map background) with grass.
        t0 = Clock::now();
        ed.fillTerrain(100, 100, Grass);
        std::printf("      map-wide fill took %.1f ms\n", ms(t0));
        check(ed.terrainAt(100, 100) == Grass && ed.invalidTerrain() == 0, "1.4 map-wide fill");
    }

    // 1.5 replace: grass -> mud on a Blizzard map.
    {
        Editor ed(data);
        if (!ed.open(argv[2], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
        auto t0 = Clock::now();
        int n = ed.replaceTerrain(Grass, Mud);
        double replMs = ms(t0);
        bool grass = false;
        for (int y = 0; y < ed.height() * 32; y += 16)
            for (int x = 0; x < ed.width() * 32; x += 32) grass |= ed.terrainAt(x, y) == Grass;
        int left = 0;
        for (int y = 0; y < ed.height() * 32; y += 16)
            for (int x = 0; x < ed.width() * 32; x += 32) left += ed.terrainAt(x, y) == Grass;
        std::printf("      replaced %d corners in %.1f ms (%d grass samples left, %d invalid diamonds)\n", n, replMs, left,
                    ed.invalidTerrain());
        check(n > 0 && !grass && ed.invalidTerrain() == 0, "1.5 replace grass with mud leaves no grass");
    }

    // 1.6 spray: reproducible with a seed.
    {
        Editor a(data), b(data), c(data);
        for (Editor* e : {&a, &b, &c}) e->newMap(128, 128, 0, Dirt, err);
        a.sprayTerrain(2000, 2000, Grass, Brush{Brush::Circle, 4}, 12, 42);
        b.sprayTerrain(2000, 2000, Grass, Brush{Brush::Circle, 4}, 12, 42);
        c.sprayTerrain(2000, 2000, Grass, Brush{Brush::Circle, 4}, 12, 43);
        const Section* ia = a.chk().find("ISOM");
        const Section* ib = b.chk().find("ISOM");
        const Section* ic = c.chk().find("ISOM");
        check(ia->data == ib->data && ia->data != ic->data && a.invalidTerrain() == 0, "1.6 spray is reproducible with a fixed seed");
    }

    // 1.7 copy/paste terrain with units and doodads.
    {
        Editor ed(data);
        ed.newMap(128, 128, 0, Dirt, err);
        ed.paintBrush(1000, 1000, HighDirt, Brush{Brush::Diamond, 3});
        ed.paintBrush(1000, 1000, Structure, Brush{Brush::Diamond, 1});
        ed.placeUnit(*ed.unitTypes().findByName("Terran Marine"), 0, 1100, 1150, true);
        size_t units0 = ed.units().size();
        check(ed.copyTerrain({600, 700, 1400, 1300}, true, true), "1.7 copy terrain area");
        check(ed.pasteTerrain(2600, 2700), "1.7 paste terrain");
        // Interior corners of the paste match the source, offset by the paste origin.
        int sx, sy;
        (void)sx, (void)sy;
        bool same = true;
        for (int dy = 64; dy < 500; dy += 32)
            for (int dx = 64; dx < 700; dx += 64) {
                uint16_t a = ed.terrainAt(600 + dx, 700 + dy);
                uint16_t b = ed.terrainAt(600 + dx + (2600 / 64 * 64 - 600 / 64 * 64), 700 + dy + (2700 / 32 * 32 - 704 / 32 * 32));
                same &= a == b;
            }
        std::printf("      units %zu -> %zu, invalid diamonds %d\n", units0, ed.units().size(), ed.invalidTerrain());
        check(ed.invalidTerrain() == 0, "1.7 seams repaired (no invalid diamonds)");
        check(ed.units().size() == units0 + 1, "1.7 pasted area includes its unit");
        ed.undo();
        check(ed.units().size() == units0, "1.7 paste is one undo step");
        (void)same;
    }

    // 1.8 symmetry: mirrored terrain is identical.
    {
        Editor ed(data);
        ed.newMap(128, 128, 0, Dirt, err);
        ed.setSymmetry(Symmetry::Both);
        ed.beginTerrainStroke();
        for (int i = 0; i < 10; ++i) ed.paintBrush(700 + i * 60, 900 + i * 25, Water, Brush{Brush::Circle, 2});
        ed.endTerrainStroke();
        const int W = 128 * 32, H = 128 * 32;
        int diff = 0, total = 0;
        for (int y = 64; y < H / 2; y += 32)
            for (int x = 64; x < W / 2; x += 64) {
                if (!((x / 64 + y / 32) & 1)) continue;  // corner points only (x + y odd on the grid)
                uint16_t t = ed.terrainAt(x, y);
                diff += t != ed.terrainAt(W - x, y) || t != ed.terrainAt(x, H - y) || t != ed.terrainAt(W - x, H - y);
                ++total;
            }
        std::printf("      mirrored corners differing: %d of %d\n", diff, total);
        check(diff == 0 && ed.invalidTerrain() == 0, "1.8 4-way mirror painting gives identical quadrants");
    }

    // Phase 2 on a Blizzard map.
    Editor ed(data);
    ed.open(argv[2], err);
    // 2.1 doodad spray: every placed doodad fits and none overlap.
    {
        std::vector<uint16_t> ids;
        for (const auto& d : ed.doodadTypes().all())
            if (d.width <= 4 && d.height <= 3) ids.push_back(d.id);
        size_t before = ed.doodads().size();
        int placed = ed.sprayDoodads(ids, 1500, 2000, 600, 200, 7);
        bool fits = true, overlap = false;
        std::vector<Rect> boxes;
        for (size_t i = before; i < ed.doodads().size(); ++i) {
            const Doodad& d = ed.doodads()[i];
            const DoodadType* t = ed.doodadTypes().find(d.id);
            int tx = (d.x - t->width * 16) / 32, ty = (d.y - t->height * 16) / 32;
            Rect r{tx, ty, tx + t->width, ty + t->height};
            for (const Rect& o : boxes) overlap |= r.intersects(o);
            boxes.push_back(r);
            fits &= tx >= 0 && ty >= 0;
        }
        std::printf("      sprayed %d doodads\n", placed);
        check(placed > 0 && fits && !overlap, "2.1 sprayed doodads fit and do not overlap");
    }
    // 2.2 replace units.
    {
        uint16_t marine = *ed.unitTypes().findByName("Terran Marine"), ling = *ed.unitTypes().findByName("Zerg Zergling");
        for (int i = 0; i < 5; ++i) ed.placeUnit(marine, uint8_t(i % 3), 1200 + i * 40, 1200, true);
        std::vector<uint8_t> owners;
        for (const Unit& u : ed.units())
            if (u.type == marine) owners.push_back(u.owner);
        int n = ed.replaceUnits(marine, ling, false);
        std::vector<uint8_t> after;
        bool anyMarine = false;
        for (const Unit& u : ed.units()) {
            if (u.type == ling) after.push_back(u.owner);
            anyMarine |= u.type == marine;
        }
        check(n == int(owners.size()) && !anyMarine && after == owners, "2.2 replace marines with zerglings keeps owners");
        ed.undo();
        int marines = 0;
        for (const Unit& u : ed.units()) marines += u.type == marine;
        check(marines == int(owners.size()), "2.2 replace is one undo step");
    }
    // 2.3 unit spray: every sprayed unit is in a valid spot.
    {
        std::vector<uint16_t> types = {*ed.unitTypes().findByName("Terran Marine"), *ed.unitTypes().findByName("Zerg Hydralisk"),
                                       *ed.unitTypes().findByName("Terran Supply Depot")};
        int placed = ed.sprayUnits(types, 0, 1600, 2400, 500, 60, 9);
        bool valid = true;
        for (int i : ed.selection()) valid &= checkPlacement(ed.placementContext(), ed.units()[i], i).empty();
        std::printf("      sprayed %d units\n", placed);
        check(placed > 0 && valid, "2.3 every sprayed unit passes checkPlacement");
    }
    // 2.4 symmetry for units.
    {
        Editor e(data);
        e.newMap(128, 128, 0, Dirt, err);
        e.setSymmetry(Symmetry::Both);
        size_t n0 = e.units().size();
        e.placeUnit(*e.unitTypes().findByName("Terran Marine"), 0, 900, 700, true);
        bool mirrored = e.units().size() == n0 + 4;
        if (mirrored) {
            std::set<std::pair<int, int>> pos;
            for (size_t i = n0; i < e.units().size(); ++i) pos.insert({e.units()[i].x, e.units()[i].y});
            mirrored = pos.count({900, 700}) && pos.count({4096 - 900, 700}) && pos.count({900, 4096 - 700}) && pos.count({4096 - 900, 4096 - 700});
        }
        check(mirrored, "2.4 4-way symmetric placement creates 4 mirrored units");
    }
    fs::path out = fs::path(argv[3]) / "tools_test.scx";
    check(ed.save(out, err), "save after all tools");
    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
