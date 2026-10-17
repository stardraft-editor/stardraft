// Exercise every Editor operation headlessly on a real map and verify the results, including
// after save + reopen.
//
// usage: editor_test <install dir> <map.scm|.scx> <scratch dir>

#include <algorithm>
#include <cmath>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <random>
#include <set>
#include <string>

#include "stardraft/editor.h"
#include "stardraft/isom.h"
#include "stardraft/terrain.h"
#include "stardraft/terrain_gen.h"
#include "stardraft/game_data.h"
#include "stardraft/sprites.h"
#include "stardraft/map_file.h"
#include "stardraft/briefing.h"

using namespace stardraft;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond, msg)                                         \
    do {                                                         \
        bool ok_ = (cond);                                       \
        std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", msg);     \
        std::fflush(stdout); /* (a crash later keeps the lines) */ \
        if (!ok_) ++failures;                                    \
    } while (0)

// N.2: "Units anywhere" (illegal placement) and "Units may overlap" (stacking) hold for paste, move, duplicate and
// terrain paste with units, as for placing. Off: refused (no correction) or moved to a spot where they fit.
static void placementOptions(const GameData& data, const char* map) {
    int wx = -1, wy = -1;
    std::string err;
    Editor pe(data);
    if (!pe.open(map, err)) {
        CHECK(false, "placement options: the map opens");
        return;
    }
    const UnitTypes& types = pe.unitTypes();
    const uint16_t marine = *types.findByName("Terran Marine"), wraith = *types.findByName("Terran Wraith");
    auto fits = [&](size_t i) { return checkPlacement(pe.placementContext(), pe.units()[i], int(i)).empty(); };
    auto at = [&](size_t i, int x, int y) { return pe.units()[i].x == x && pe.units()[i].y == y; };
    // A marine on open ground, a wraith next to it and a second wraith to move onto.
    PlaceResult m = pe.placeUnit(marine, 0, pe.width() * 16, pe.height() * 16, true);
    PlaceResult w = pe.placeUnit(wraith, 0, m.unit.x + 96, m.unit.y, true);
    PlaceResult w2r = pe.placeUnit(wraith, 0, m.unit.x + 256, m.unit.y, true);
    CHECK(m.placed && w.placed && w2r.placed, "placement options: a marine and two wraiths placed");
    const size_t mi = size_t(m.index), wi = size_t(w.index);
    const Unit m0 = pe.units()[mi], w0 = pe.units()[wi], w2 = pe.units()[size_t(w2r.index)];
    const size_t n = pe.units().size();
    // The unit limit (GitHub feedback #103): at the limit, placing is refused; spraying adds nothing.
    pe.setUnitLimit(int(n));
    {
        const PlaceResult over = pe.placeUnit(wraith, 0, m.unit.x + 512, m.unit.y, true);
        const int sprayed = pe.sprayUnits({wraith}, 0, m.unit.x + 512, m.unit.y, 128, 5, 7);
        CHECK(!over.placed && !over.issues.empty() && over.issues[0] == PlacementIssue::UnitLimit && sprayed == 0 && pe.units().size() == n,
              "the unit limit: no unit is placed or sprayed when the map has that many");
    }
    pe.setUnitLimit(0);

    // Options off.
    pe.select(int(mi), false), pe.copySelection();
    // A spot where a marine cannot stand: the only issue is unwalkable ground, with open ground nearby.
    wx = wy = -1;
    for (int ty = 8; ty < pe.height() - 8 && wx < 0; ++ty)
        for (int tx = 8; tx < pe.width() - 8 && wx < 0; ++tx) {
            Unit probe = m0;
            probe.x = uint16_t(tx * 32 + 16), probe.y = uint16_t(ty * 32 + 16);
            const auto issues = checkPlacement(pe.placementContext(), probe);
            if (issues.size() == 1 && issues[0] == PlacementIssue::Unwalkable && pe.findValidPosition(probe)) wx = probe.x, wy = probe.y;
        }
    CHECK(wx >= 0, "off: a spot where a marine cannot stand");
    std::printf("      unwalkable spot %d,%d\n", wx, wy);
    CHECK(pe.paste(wx, wy, false) == 0 && pe.units().size() == n, "off: a marine pasted on unwalkable ground without correction is refused");
    CHECK(pe.paste(wx, wy, true) == 1 && pe.units().size() == n + 1 && fits(n) && !at(n, wx, wy), "off: with correction it goes to a spot where it fits");
    pe.undo();
    pe.select(int(wi), false), pe.copySelection();  // duplicate: copy, then paste on the same spot
    CHECK(pe.paste(w0.x, w0.y, false) == 0 && pe.units().size() == n, "off: a wraith duplicated on top of itself is refused");
    CHECK(pe.paste(w0.x, w0.y, true) == 1 && fits(n) && !pe.unitBox(pe.units()[n]).intersects(pe.unitBox(w0)), "off: with correction the duplicate goes next to it");
    pe.undo();
    pe.select(int(mi), false);
    CHECK(pe.moveSelection(wx - m0.x, wy - m0.y, false) == 0 && at(mi, m0.x, m0.y), "off: a marine moved onto unwalkable ground (drag or arrow keys) stays where it was");
    CHECK(pe.moveSelection(wx - m0.x, wy - m0.y, true) == 1 && fits(mi), "off: with correction it goes to a spot where it fits");
    pe.undo();
    pe.select(int(wi), false);
    CHECK(pe.moveSelection(w2.x - w0.x, w2.y - w0.y, false) == 0 && at(wi, w0.x, w0.y), "off: a wraith moved onto a wraith stays where it was");

    // Terrain paste with units: the copied area pasted on itself puts each copied unit on the unit it copied.
    // The copy starts on a corner (x + y odd in the 64 x 32 px grid), so the paste lines up exactly.
    const int ax = (m0.x / 64 - 4) | 1, ay = (m0.y / 32 - 4) & ~1;
    const int sx = ax * 64, sy = ay * 32;
    const Rect area{sx, sy, sx + 768, sy + 320};
    const bool terrainOk = pe.canPaintTerrain() && pe.copyTerrain(area, true, false);
    size_t inArea = 0;
    for (const Unit& u : pe.units()) inArea += area.contains(u.x, u.y);
    CHECK(terrainOk && inArea >= 3, "terrain with units copied");
    std::printf("      terrain clipboard: %zu units\n", inArea);
    if (terrainOk) {
        CHECK(pe.pasteTerrain(sx, sy, false) && pe.units().size() == n, "off: terrain pasted on itself without correction adds no unit");
        pe.undo();
        CHECK(pe.pasteTerrain(sx, sy, true) && pe.units().size() > n, "off: with correction the units go to spots where they fit");
        bool allFit = true;
        for (size_t i = n; i < pe.units().size(); ++i) allFit = allFit && fits(i);
        CHECK(allFit, "off: each of them fits");
        pe.undo();
    }

    // Units may overlap.
    pe.setAllowStacking(true);
    pe.select(int(wi), false), pe.copySelection();
    CHECK(pe.paste(w0.x, w0.y, false) == 1 && at(n, w0.x, w0.y), "overlap on: a wraith duplicated on top of itself");
    pe.undo();
    pe.select(int(wi), false);
    CHECK(pe.moveSelection(w2.x - w0.x, w2.y - w0.y, false) == 1 && at(wi, w2.x, w2.y), "overlap on: a wraith moved onto a wraith");
    pe.undo();
    pe.setAllowStacking(false);

    // Units anywhere.
    pe.setAllowIllegalUnits(true);
    pe.select(int(mi), false), pe.copySelection();
    CHECK(pe.paste(wx, wy, false) == 1 && at(n, wx, wy), "anywhere on: a marine pasted on unwalkable ground stays there");
    pe.undo();
    CHECK(pe.paste(wx, wy, true) == 1 && at(n, wx, wy), "anywhere on: also with correction on");
    pe.undo();
    CHECK(pe.paste(m0.x, m0.y, true) == 1 && at(n, m0.x, m0.y), "anywhere on: a marine duplicated on top of itself");
    pe.undo();
    pe.select(int(mi), false);
    CHECK(pe.moveSelection(wx - m0.x, wy - m0.y, true) == 1 && at(mi, wx, wy), "anywhere on: a marine moved onto unwalkable ground");
    pe.undo();
    pe.select(int(wi), false);
    CHECK(pe.moveSelection(w2.x - w0.x, w2.y - w0.y, false) == 1 && at(wi, w2.x, w2.y), "anywhere on: a wraith moved onto a wraith");
    pe.undo();
    if (terrainOk) {
        CHECK(pe.pasteTerrain(sx, sy, false) && pe.units().size() == n + inArea, "anywhere on: terrain pasted on itself adds every copied unit");
        bool same = true;
        for (size_t i = n; i < pe.units().size(); ++i) {
            bool found = false;
            for (size_t j = 0; j < n; ++j) found = found || (at(j, pe.units()[i].x, pe.units()[i].y) && pe.units()[j].type == pe.units()[i].type);
            same = same && found;
        }
        CHECK(same, "anywhere on: each at the place of the unit it copied");
        pe.undo();
    }
    pe.setAllowIllegalUnits(false);

    // Terrain paste: the units fit the pasted terrain. A marine copied on unwalkable ground (Units anywhere) is
    // pasted where the ground is open before the paste; the paste makes its spot unwalkable.
    if (wx >= 0 && pe.canPaintTerrain()) {
        pe.setAllowIllegalUnits(true);
        const PlaceResult wm = pe.placeUnit(marine, 0, wx, wy, false);
        const int cx = (wx / 64 - 4) | 1, cy = (wy / 32 - 4) & ~1;
        const Rect clip{cx * 64, cy * 32, cx * 64 + 768, cy * 32 + 320};
        const bool copied = wm.placed && pe.copyTerrain(clip, true, false);
        pe.undo();
        pe.setAllowIllegalUnits(false);
        // A corner where the marine's new spot is open ground and no unit is in the pasted area.
        int dx = -1, dy = -1;
        for (int y = 2; copied && y * 32 + 320 < pe.height() * 32 && dx < 0; y += 2)
            for (int x = 1; x * 64 + 768 < pe.width() * 32 && dx < 0; x += 2) {
                Unit probe = wm.unit;
                probe.x = uint16_t(x * 64 + (wm.unit.x - clip.left)), probe.y = uint16_t(y * 32 + (wm.unit.y - clip.top));
                const Rect dest{x * 64, y * 32, x * 64 + 768, y * 32 + 320};
                bool empty = true;
                for (const Unit& u : pe.units()) empty = empty && !dest.contains(u.x, u.y);
                if (empty && checkPlacement(pe.placementContext(), probe).empty()) dx = x, dy = y;
            }
        CHECK(copied && dx >= 0 && pe.units().size() == n, "terrain paste: a marine on unwalkable ground copied with its terrain, and open ground to paste it on");
        if (dx >= 0) {
            const int mx = dx * 64 + (wm.unit.x - clip.left), my = dy * 32 + (wm.unit.y - clip.top);
            auto addedFit = [&] {
                bool ok = true;
                for (size_t i = n; i < pe.units().size(); ++i) ok = ok && fits(i);
                return ok;
            };
            bool marineAdded = false;
            CHECK(pe.pasteTerrain(dx * 64, dy * 32, true), "terrain paste with correction: done");
            for (size_t i = n; i < pe.units().size(); ++i) marineAdded = marineAdded || pe.units()[i].type == marine;
            CHECK(marineAdded && addedFit(), "terrain paste with correction: the marine goes to a spot where it fits the pasted terrain");
            pe.undo();
            CHECK(pe.pasteTerrain(dx * 64, dy * 32, false), "terrain paste without correction: done");
            Unit probe = wm.unit;
            probe.x = uint16_t(mx), probe.y = uint16_t(my);
            const auto issues = checkPlacement(pe.placementContext(), probe);
            CHECK(std::find(issues.begin(), issues.end(), PlacementIssue::Unwalkable) != issues.end(), "terrain paste: the marine's spot is unwalkable after the paste");
            bool onSpot = false;
            for (size_t i = n; i < pe.units().size(); ++i) onSpot = onSpot || (pe.units()[i].type == marine && at(i, mx, my));
            CHECK(!onSpot && addedFit(), "terrain paste without correction: the marine is not put on the unwalkable spot");
            pe.undo();
        }
    }
}

// The Options menu's rules for each kind. Snap (units, sprites, locations): none, tiles, half and quarter tiles.
// Doodads: Place only where it fits, Move to the nearest place where it fits, Place anywhere (the terrain is not
// checked); never on top of another doodad. Sprites: anywhere on the map.
static void placementRules(const GameData& data, const char* map) {
    // Snap: the middle of the grid cell (a click), the nearest grid line (a move, a location edge).
    CHECK(snapToCell(39, 0) == 39 && snapRound(39, 0) == 39, "snap: Do not snap keeps the point");
    CHECK(snapToCell(39, 32) == 48 && snapToCell(39, 16) == 40 && snapToCell(39, 8) == 36, "snap: tiles, half and quarter tiles put a point in the middle of its cell");
    CHECK(snapRound(39, 32) == 32 && snapRound(39, 16) == 32 && snapRound(39, 8) == 40 && snapRound(-20, 16) == -16,
          "snap: a move or a location edge goes to the nearest grid line");
    std::string err;
    Editor pe(data);
    if (!pe.open(map, err)) {
        CHECK(false, "placement rules: the map opens");
        return;
    }
    // Sprites: anywhere on the map (any terrain), not off it.
    const size_t s0 = pe.sprites().size();
    CHECK(pe.placeSprite(131, 1, 1, 0) && pe.placeSprite(131, pe.width() * 32 - 1, pe.height() * 32 - 1, 0) && pe.sprites().size() == s0 + 2,
          "sprites: placed anywhere on the map");
    CHECK(!pe.placeSprite(131, pe.width() * 32, 5, 0), "sprites: not off the map");
    pe.undo(), pe.undo();
    // Doodads: one placed with Place anywhere, near the middle.
    const DoodadType* type = nullptr;
    int cx = 0, cy = 0;
    for (const auto& t : pe.doodadTypes().all()) {
        if (t.width < 1 || t.width > 4 || t.height < 1 || t.height > 4) continue;
        cx = pe.width() * 16 + t.width * 16, cy = pe.height() * 16 + t.height * 16;
        if (pe.placeDoodad(t.id, cx, cy, false)) {
            type = &t;
            break;
        }
    }
    CHECK(type != nullptr, "doodads: Place anywhere places one without the terrain check");
    if (!type) return;
    const Doodad a = pe.doodads().back();
    const size_t n = pe.doodads().size();
    CHECK(!pe.placeDoodad(type->id, a.x, a.y, false) && pe.doodads().size() == n, "doodads: never on top of another doodad (also with Place anywhere)");
    CHECK(pe.placeDoodads({{type->id, a.x, a.y, 0}}, false, false) == 0, "doodads: a paste on top of another doodad is refused");
    const auto spot = pe.nearestDoodadFit(type->id, a.x, a.y, false);
    CHECK(spot && (spot->first != a.x || spot->second != a.y), "doodads: the nearest place where it fits is found");
    if (spot) {
        const int dx = std::abs(spot->first - a.x) / 32, dy = std::abs(spot->second - a.y) / 32;
        CHECK((dx == type->width && dy == 0) || (dy == type->height && dx == 0), "doodads: the nearest place is next to the other doodad");
        std::printf("      %ux%u doodad at %u,%u, the nearest free place %d,%d\n", type->width, type->height, a.x, a.y, spot->first, spot->second);
    }
    CHECK(pe.placeDoodads({{type->id, a.x, a.y, 0}}, false, true) == 1 && spot && pe.doodads().back().x == spot->first && pe.doodads().back().y == spot->second,
          "doodads: with Move to the nearest place where it fits, a paste goes there");
    pe.undo();
    // Doodads overlap only where both have a tile: the empty corner of a ramp or cliff piece takes the next piece
    // (Discord 2026-10-14: the next segment of a ramp was refused).
    {
        const auto& groups = pe.tileset().groups;
        auto tile = [&](const DoodadType& k, int c, int r) {
            const size_t g = size_t(k.firstGroup) + size_t(r);
            return g < groups.size() && groups[g].megatiles[size_t(c)] != 0;
        };
        // A doodad with tiles and an empty cell, and an offset where a second one overlaps its rectangle but no tile.
        const DoodadType* holed = nullptr;
        int ox = 0, oy = 0;
        for (const auto& t : pe.doodadTypes().all()) {
            if (holed || t.width > 8 || t.height > 8) continue;
            bool any = false;
            for (int r = 0; r < t.height; ++r)
                for (int c = 0; c < t.width; ++c) any |= tile(t, c, r);
            if (!any) continue;
            for (int dy = -int(t.height) + 1; dy < t.height && !holed; ++dy)
                for (int dx = -int(t.width) + 1; dx < t.width && !holed; ++dx) {
                    if (!dx && !dy) continue;
                    bool meet = false;
                    for (int r = 0; r < t.height && !meet; ++r)
                        for (int c = 0; c < t.width && !meet; ++c) {
                            const int c2 = c - dx, r2 = r - dy;
                            meet = c2 >= 0 && r2 >= 0 && c2 < t.width && r2 < t.height && tile(t, c, r) && tile(t, c2, r2);
                        }
                    if (!meet) holed = &t, ox = dx, oy = dy;
                }
        }
        if (holed) {
            const int hx = pe.width() / 4, hy = pe.height() / 4;
            const size_t n0 = pe.doodads().size();
            CHECK(pe.placeDoodad(holed->id, hx * 32 + holed->width * 16, hy * 32 + holed->height * 16, false), "doodads: a doodad with an empty cell is placed");
            CHECK(!pe.doodadOverlaps(*holed, hx + ox, hy + oy) &&
                      pe.placeDoodad(holed->id, (hx + ox) * 32 + holed->width * 16, (hy + oy) * 32 + holed->height * 16, false) && pe.doodads().size() == n0 + 2,
                  "doodads: a doodad goes where the rectangles overlap but no tiles do (the next piece of a ramp)");
            CHECK(pe.doodadOverlaps(*holed, hx, hy), "doodads: a doodad on another doodad's tiles is refused");
            std::printf("      doodad %u (%ux%u): a second one at the offset %d,%d\n", holed->id, holed->width, holed->height, ox, oy);
            pe.undo(), pe.undo();
        } else std::printf("      no doodad with tiles and empty cells: the overlap check by cells is not tested\n");
    }
    // dddata.bin rows are the doodad's width long (GitHub feedback #93, #100): the map's own doodads fit their terrain.
    {
        int placed = 0, fit = 0;
        for (const Doodad& d : pe.doodads())
            if (const DoodadType* t = pe.doodadTypes().find(d.id)) {
                ++placed;
                fit += doodadFits(pe.chk(), *t, d.x / 32 - t->width / 2, d.y / 32 - t->height / 2, true);
            }
        std::printf("      %d of the map's %d doodads fit their terrain\n", fit, placed);
        CHECK(placed == 0 || fit * 10 >= placed * 9, "doodads: the map's own doodads fit the terrain check");
    }
    // Place only where it fits: the terrain check; the nearest place with the check fits the terrain.
    if (const auto fit = pe.nearestDoodadFit(type->id, a.x, a.y, true)) {
        int tx, ty;
        pe.doodadOrigin(*type, fit->first, fit->second, tx, ty);
        CHECK(doodadFits(pe.chk(), *type, tx, ty, true) && !pe.doodadOverlaps(*type, tx, ty), "doodads: the nearest place with the terrain check fits the terrain");
    } else {
        std::printf("      no place near it where the doodad fits the terrain\n");
    }
    const size_t before = pe.doodads().size();
    std::vector<uint16_t> ids{type->id};
    const int sprayed = pe.sprayDoodads(ids, cx + 400, cy, 160, 12, 7, false);
    CHECK(sprayed > 0 && pe.doodads().size() == before + size_t(sprayed), "doodads: spray with Place anywhere places doodads");
    if (sprayed) pe.undo();
}

// L.3: the classic settings (UNIx/UNIS, PUNI, UPGx/UPGS, PUPx/UPGR, TECx/TECS, PTEx/PTEC): each kind set, undone,
// saved and opened again; the sections a change does not touch stay byte-identical, and the pairs stay the same.
static bool sameExcept(const Chk& a, const Chk& b, std::initializer_list<const char*> except, std::string* which = nullptr) {
    auto skip = [&](const SectionName& n) {
        for (const char* e : except)
            if (n.str() == e) return true;
        return false;
    };
    std::vector<const Section*> x, y;
    for (const auto& s : a.sections)
        if (!skip(s.name)) x.push_back(&s);
    for (const auto& s : b.sections)
        if (!skip(s.name)) y.push_back(&s);
    if (x.size() != y.size()) {
        if (which) *which = "section count";
        return false;
    }
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i]->name != y[i]->name || x[i]->data != y[i]->data) {
            if (which) *which = x[i]->name.str();
            return false;
        }
    return true;
}

// The original section of a pair holds the same as the Brood War one for the entries it has.
static bool pairsAgree(const Chk& c) {
    const Section *ux = c.find("UNIx"), *us = c.find("UNIS"), *gx = c.find("UPGx"), *gs = c.find("UPGS"), *tx = c.find("TECx"), *ts = c.find("TECS");
    bool ok = true;
    if (ux && us && ux->data.size() >= 4168 && us->data.size() >= 4048)
        ok &= std::equal(us->data.begin(), us->data.begin() + 3648, ux->data.begin()) &&
              std::equal(us->data.begin() + 3648, us->data.begin() + 3848, ux->data.begin() + 3648) &&
              std::equal(us->data.begin() + 3848, us->data.begin() + 4048, ux->data.begin() + 3908);
    if (gx && gs && gx->data.size() >= 794 && gs->data.size() >= 598) {
        ok &= std::equal(gs->data.begin(), gs->data.begin() + 46, gx->data.begin());
        for (int k = 0; k < 6; ++k) ok &= std::equal(gs->data.begin() + 46 + k * 92, gs->data.begin() + 46 + k * 92 + 92, gx->data.begin() + 62 + k * 122);
    }
    if (tx && ts && tx->data.size() >= 396 && ts->data.size() >= 216) {
        ok &= std::equal(ts->data.begin(), ts->data.begin() + 24, tx->data.begin());
        for (int k = 0; k < 4; ++k) ok &= std::equal(ts->data.begin() + 24 + k * 48, ts->data.begin() + 24 + k * 48 + 48, tx->data.begin() + 44 + k * 88);
    }
    return ok;
}

// R10: the image player (SpriteSim) against OpenBW's iscript_execute and order code, for a few units of the game's own
// iscript.bin. The expected frames come from the scripts as OpenBW runs them: `wait n` stops for n ticks, `playfram f`
// shows f plus the facing (0..16; 17 frames per set), `move n` moves n pixels ahead, a target turns the unit by its
// flingy.dat Turn Radius per tick (update_unit_heading), and warpoverlay sets the warp texture's frame.
static void iscriptPlayerTest(Editor& ed) {
    UnitSprites& sp = ed.unitSprites();
    const int era = ed.tileset().era;
    std::printf("iscript player (OpenBW):\n");
    auto frames = [](SpriteSim& sim, int ticks) {
        std::vector<int> out{sim.mainFrame()};
        for (int t = 0; t < ticks; ++t) sim.tick(), out.push_back(sim.mainFrame());
        return out;
    };
    auto show = [](const std::vector<int>& v) {
        std::string s;
        for (int f : v) s += (s.empty() ? "" : " ") + std::to_string(f);
        return s;
    };
    {  // Marine (image 239) Ground attack, facing right (8): 0, 17, 34, then the shot (sound 69) and 51/34 four times.
        SpriteSim sim(sp, 239, 2, 8, 1, era);
        std::vector<int> f{sim.mainFrame()}, sounds, attackAt;
        for (int t = 1; t <= 10; ++t) {
            sim.tick();
            f.push_back(sim.mainFrame());
            for (int s : sim.takeSounds()) sounds.push_back(s);
            if (sim.takeAttack()) attackAt.push_back(t);
        }
        const std::vector<int> want{8, 25, 42, 59, 42, 59, 42, 59, 42, 42, 42};
        std::printf("      marine attack: %s\n", show(f).c_str());
        CHECK(f == want, "marine attack: frames as OpenBW plays GndAttkInit");
        CHECK(sounds == std::vector<int>{69} && attackAt == std::vector<int>{3}, "marine attack: sound 69 and the shot at tick 3");
        CHECK(sim.takeHandoff() == 2 && sim.running(), "marine attack: gotorepeatattk, then ignorerest waits for the order");
    }
    {  // Siege tank turret (image 251): Init sets the heading to 12 (96); a target down (128) turns it 13 per tick.
        SpriteSim sim(sp, 251, 0, 0, 1, era);
        sim.setTurnRate(13);
        sim.tick();  // wait 1, then setfldirect 12
        CHECK(sim.heading() == 96 && sim.mainFrame() == 12, "tank turret: Init faces direction 12");
        sim.setTarget(128);
        std::vector<int> headings;
        for (int t = 0; t < 4; ++t) sim.tick(), headings.push_back(sim.heading());
        std::printf("      tank turret to a target at 128: %s, frame %d\n", show(headings).c_str(), sim.mainFrame());
        CHECK((headings == std::vector<int>{109, 122, 128, 128}) && sim.mainFrame() == 16, "tank turret: turns 13 per tick to the target (frame 16)");
        sim.setTarget(-1);
        for (int t = 0; t < 30; ++t) sim.tick();
        CHECK(sim.heading() == 128, "tank turret: without a target it stays");
        SpriteSim fire(sp, 251, 2, 16, 1, era);
        std::vector<int> sounds;
        int attackAt = -1;
        for (int t = 1; t <= 4; ++t) {
            fire.tick();
            for (int s : fire.takeSounds()) sounds.push_back(s);
            if (fire.takeAttack() && attackAt < 0) attackAt = t;
        }
        CHECK(sounds == std::vector<int>{68} && attackAt == 3, "tank turret attack: sound 68 at tick 1, the shot at tick 3");
    }
    {  // Dragoon (image 122) Walking, facing right: frames 136, 153 ... 255 plus 8; move 4 6 8 8 2 2 6 6 pixels.
        SpriteSim sim(sp, 122, 11, 8, 1, era);
        std::vector<int> f{sim.mainFrame()}, xs{int(std::lround(sim.position().first))};
        for (int t = 0; t < 8; ++t) sim.tick(), f.push_back(sim.mainFrame()), xs.push_back(int(std::lround(sim.position().first)));
        std::printf("      dragoon walk: frames %s; x %s\n", show(f).c_str(), show(xs).c_str());
        CHECK((f == std::vector<int>{8, 144, 161, 178, 195, 212, 229, 246, 263}), "dragoon walk: frames as OpenBW plays Walking");
        CHECK((xs == std::vector<int>{4, 10, 18, 26, 28, 30, 36, 42, 46}) && std::lround(sim.position().second) == 0,
              "dragoon walk: the sprite moves right by the move steps");
        bool moved = false;
        for (const auto& d : sim.draws()) moved |= d.image == 122 && d.x == 46;
        CHECK(moved, "dragoon walk: the image draws where it moved");
    }
    {  // Zergling (image 54) Death: sound 896, frames 289..295 two ticks each, then the remnants (sprite 160).
        SpriteSim sim(sp, 54, 1, 8, 1, era);
        const std::vector<int> sounds = sim.takeSounds();
        const std::vector<int> f = frames(sim, 13);
        std::printf("      zergling death: %s\n", show(f).c_str());
        CHECK(sounds == std::vector<int>{896}, "zergling death: sound 896");
        CHECK((f == std::vector<int>{289, 289, 290, 290, 291, 291, 292, 292, 293, 293, 294, 294, 295, 295}), "zergling death: frames as OpenBW plays Death");
        sim.tick(), sim.tick();
        const int remnant = sp.spriteImage(160);
        bool remnants = false;
        for (const auto& d : sim.draws()) remnants |= d.image == remnant;
        CHECK(!sim.running() && sim.anyRunning() && remnants, "zergling death: the death ends, the remnants stay");
    }
    {  // The Nexus (image 179) warping in: the warp texture frames 0..19 (script 193), then the warp flash.
        SpriteSim sim(sp, 179, 0, 0, 1, era);
        sim.warpIn();
        std::vector<int> levels;
        for (int t = 0; t < 20 && sim.warpState() == 1; ++t) {
            for (const auto& d : sim.draws())
                if (d.image == 179 && d.drawFunction == 12) levels.push_back(d.level);
            sim.tick();
        }
        std::printf("      warp-in texture frames: %s\n", show(levels).c_str());
        std::vector<int> want(20);
        for (int k = 0; k < 20; ++k) want[size_t(k)] = k;
        CHECK(levels == want, "warp-in: the warp texture's frames 0 to 19, one per tick");
        for (int t = 0; t < 3 && sim.warpState() == 1; ++t) sim.tick();
        bool flash = false;
        for (const auto& d : sim.draws()) flash |= d.drawFunction == 17;
        CHECK(sim.warpState() == 2 && flash, "warp-in: then the Warp in animation with its warp flash");
        for (int t = 0; t < 80 && sim.warpState() == 2; ++t) sim.tick();
        CHECK(sim.warpState() == 3, "warp-in: the flash ends (63) and gives the order its signal");
    }
}

static void classicSettings(const GameData& data, const char* map, const fs::path& scratch) {
    std::string err;
    Editor ed(data);
    if (!ed.open(map, err) || !ed.settingDefaults()) {
        CHECK(false, "classic settings: the map opens and the game's values load");
        return;
    }
    const Chk original = ed.chk();
    const std::string ext = fs::path(map).extension().string();
    std::printf("      classic settings: %s map; UNIx %d UNIS %d PUNI %d UPGx %d UPGS %d PUPx %d UPGR %d TECx %d TECS %d PTEx %d PTEC %d\n",
                settingsBroodWar(original) ? "Brood War" : "original", original.has("UNIx"), original.has("UNIS"), original.has("PUNI"),
                original.has("UPGx"), original.has("UPGS"), original.has("PUPx"), original.has("UPGR"), original.has("TECx"), original.has("TECS"),
                original.has("PTEx"), original.has("PTEC"));
    const SettingDefaults& def = *ed.settingDefaults();
    CHECK(def.units.units[0].hitPoints == 40 * 256 && def.units.weapons[0].damage == 6 && def.upgrades.levels[0].defaultMax == 3 &&
              def.techs.techs[0].minerals == 100,
          "classic settings: the game's values (marine 40 hit points, Gauss Rifle 6 damage, 3 armor levels, Stim Packs 100 minerals)");

    // No change: no undo step, and saved the map is the same.
    CHECK(!ed.setUnitSettings(ed.unitSettings()) && !ed.setUpgradeSettings(ed.upgradeSettings()) && !ed.setTechSettings(ed.techSettings()) &&
              !ed.canUndo(),
          "classic settings: setting the same values is no change (no undo step)");
    std::string which;
    {
        const fs::path p = scratch / ("editor_test_classic_same" + ext);
        Editor back(data);
        const bool ok = ed.save(p, err) && back.open(p, err) && sameExcept(original, back.chk(), {}, &which);
        CHECK(ok, ("classic settings: a map with no changes saves with every section byte-identical " + which).c_str());
    }

    // Units: the marine's values, its weapon, a name and who can build it.
    {
        UnitSettings s = ed.unitSettings();
        const uint8_t gauss = def.unitWeapons[0][0];
        s.units[0].useDefault = false;
        s.units[0].hitPoints = 55 * 256, s.units[0].shields = 5, s.units[0].armor = 2, s.units[0].buildTime = 200, s.units[0].minerals = 77;
        s.units[0].gas = 3;
        s.weapons[gauss] = {9, 2};
        s.availability[0].usesDefault[1] = false, s.availability[0].player[1] = false;
        s.availability[7].global = false;
        CHECK(ed.setUnitSettings(s, {{0, "Test Marine"}}) && ed.canUndo(), "units: set the marine (one undo step)");
        const UnitSettings r = ed.unitSettings();
        CHECK(!r.units[0].useDefault && r.units[0].hitPoints == 55 * 256 && r.units[0].minerals == 77 && r.weapons[gauss].damage == 9 &&
                  r.weapons[gauss].bonus == 2 && !r.availability[0].usesDefault[1] && !r.availability[0].player[1] && !r.availability[7].global &&
                  ed.unitSettingName(0) == "Test Marine",
              "units: read back as set");
        CHECK(pairsAgree(ed.chk()), "units: UNIx and UNIS hold the same");
        which.clear();
        CHECK(sameExcept(original, ed.chk(), {"UNIx", "UNIS", "PUNI", "STR ", "STRx"}, &which), ("units: no other section changes " + which).c_str());
        ed.undo();
        CHECK(sameExcept(original, ed.chk(), {}, &which), ("units: undo gives the map back byte-identical " + which).c_str());
        ed.redo();
        const fs::path p = scratch / ("editor_test_classic_units" + ext);
        Editor back(data);
        const bool saved = ed.save(p, err) && back.open(p, err);
        CHECK(saved && back.unitSettings() == r && back.unitSettingName(0) == "Test Marine", "units: saved and opened again, the same");
        CHECK(saved && sameExcept(original, back.chk(), {"UNIx", "UNIS", "PUNI", "STR ", "STRx"}, &which),
              ("units: saved, no other section changes " + which).c_str());
        // Back to the default, and the game's name.
        s = ed.unitSettings();
        s.units[0].useDefault = true;
        CHECK(ed.setUnitSettings(s, {{0, ""}}) && ed.unitSettings().units[0].useDefault && ed.unitSettings().units[0].nameString == 0,
              "units: use default again, the game's name");
    }

    // Upgrades: Terran Infantry Armor's costs and levels.
    {
        Editor e2(data);
        e2.open(map, err);
        UpgradeSettings s = e2.upgradeSettings();
        s.upgrades[0].useDefault = false, s.upgrades[0].minerals = 123, s.upgrades[0].gasPerLevel = 11, s.upgrades[0].time = 999;
        s.levels[0].defaultMax = 2, s.levels[0].usesDefault[1] = false, s.levels[0].max[1] = 1, s.levels[0].start[1] = 1;
        CHECK(e2.setUpgradeSettings(s) && e2.canUndo(), "upgrades: set Terran Infantry Armor (one undo step)");
        const UpgradeSettings r = e2.upgradeSettings();
        CHECK(r == s, "upgrades: read back as set");
        CHECK(pairsAgree(e2.chk()), "upgrades: UPGx and UPGS hold the same");
        which.clear();
        CHECK(sameExcept(original, e2.chk(), {"UPGx", "UPGS", "PUPx", "UPGR"}, &which), ("upgrades: no other section changes " + which).c_str());
        e2.undo();
        CHECK(sameExcept(original, e2.chk(), {}, &which), ("upgrades: undo gives the map back byte-identical " + which).c_str());
        e2.redo();
        const fs::path p = scratch / ("editor_test_classic_upgrades" + ext);
        Editor back(data);
        const bool saved = e2.save(p, err) && back.open(p, err);
        CHECK(saved && back.upgradeSettings() == r, "upgrades: saved and opened again, the same");
        CHECK(saved && sameExcept(original, back.chk(), {"UPGx", "UPGS", "PUPx", "UPGR"}, &which),
              ("upgrades: saved, no other section changes " + which).c_str());
    }

    // Techs: Stim Packs' costs, researched for player 3, and Lockdown not available by default.
    {
        Editor e3(data);
        e3.open(map, err);
        TechSettings s = e3.techSettings();
        s.techs[0].useDefault = false, s.techs[0].minerals = 1, s.techs[0].gas = 2, s.techs[0].time = 3, s.techs[0].energy = 7;
        s.players[0].usesDefault[2] = false, s.players[0].available[2] = true, s.players[0].researched[2] = true;
        s.players[1].defaultAvailable = false;
        CHECK(e3.setTechSettings(s) && e3.canUndo(), "techs: set Stim Packs (one undo step)");
        const TechSettings r = e3.techSettings();
        CHECK(r == s, "techs: read back as set");
        CHECK(pairsAgree(e3.chk()), "techs: TECx and TECS hold the same");
        which.clear();
        CHECK(sameExcept(original, e3.chk(), {"TECx", "TECS", "PTEx", "PTEC"}, &which), ("techs: no other section changes " + which).c_str());
        e3.undo();
        CHECK(sameExcept(original, e3.chk(), {}, &which), ("techs: undo gives the map back byte-identical " + which).c_str());
        e3.redo();
        const fs::path p = scratch / ("editor_test_classic_techs" + ext);
        Editor back(data);
        const bool saved = e3.save(p, err) && back.open(p, err);
        CHECK(saved && back.techSettings() == r, "techs: saved and opened again, the same");
        CHECK(saved && sameExcept(original, back.chk(), {"TECx", "TECS", "PTEx", "PTEC"}, &which),
              ("techs: saved, no other section changes " + which).c_str());
    }
}


// L.2: the classic triggers (TRIG): a trigger and an action added, changed, moved and removed, each one undo step;
// undo and redo; saved and opened again; the sections a change does not touch stay byte-identical.
static void classicTriggersTest(const GameData& data, const char* map, const fs::path& scratch) {
    std::string err, which;
    Editor ed(data);
    if (!ed.open(map, err)) {
        CHECK(false, "classic triggers: the map opens");
        return;
    }
    const Chk original = ed.chk();
    const std::string ext = fs::path(map).extension().string();
    const ClassicTriggers t0 = ed.classicTriggers();
    std::printf("      classic triggers: %zu in the map\n", t0.triggers.size());
    CHECK(encodeClassicTriggers(t0) == original.effective("TRIG"), "classic triggers: TRIG reads and encodes back byte-identical");
    CHECK(!ed.setClassicTriggers(t0) && !ed.canUndo(), "classic triggers: setting the same triggers is no change (no undo step)");

    // Add: for player 1, Bring 1 men to Anywhere; Create unit; Display text (a new string).
    ClassicTrigger t = ClassicTrigger::make();
    t.players[0] = 1;
    t.conditions[0] = makeClassicCondition(3);
    t.conditions[0].unit = 230;
    ClassicAction create = makeClassicAction(44);
    create.unit = 37, create.player = 0;
    t.addAction(create);
    StringTable strings = ed.strings();
    ClassicAction text = makeClassicAction(9);
    text.text = classicStringId(strings, "editor_test text");
    t.addAction(text);
    CHECK(ed.addClassicTrigger(t, 0, &strings), "classic triggers: add a trigger (first)");
    ClassicTriggers r = ed.classicTriggers();
    CHECK(r.triggers.size() == t0.triggers.size() + 1 && r.triggers[0] == t && ed.strings().get(r.triggers[0].actions[1].text) == "editor_test text",
          "classic triggers: the new trigger and its text read back");
    which.clear();
    CHECK(sameExcept(original, ed.chk(), {"TRIG", "STR ", "STRx"}, &which), ("classic triggers: no other section changes " + which).c_str());

    // Edit: another unit; an action added, moved first, then removed; the trigger moved last.
    ClassicTrigger e = r.triggers[0];
    e.actions[0].unit = 0;
    CHECK(ed.setClassicTrigger(0, e), "classic triggers: change an action's unit");
    e.addAction(makeClassicAction(4));
    e.moveAction(2, 0);
    CHECK(ed.setClassicTrigger(0, e) && ed.classicTriggers().triggers[0].actions[0].type == 4 && ed.classicTriggers().triggers[0].actionCount() == 3,
          "classic triggers: add an action and move it first");
    e.removeAction(0);
    CHECK(ed.setClassicTrigger(0, e) && ed.classicTriggers().triggers[0].actionCount() == 2 && ed.classicTriggers().triggers[0].actions[0].type == 44,
          "classic triggers: remove the action");
    const int last = int(ed.classicTriggers().triggers.size()) - 1;
    CHECK(ed.moveClassicTrigger(0, last) && ed.classicTriggers().triggers[size_t(last)] == e, "classic triggers: move the trigger last");
    // Undo: each step one at a time, back to the map.
    ed.undo();
    CHECK(ed.classicTriggers().triggers[0] == e, "classic triggers: undo the move");
    ed.undo();
    CHECK(ed.classicTriggers().triggers[0].actionCount() == 3, "classic triggers: undo the remove");
    ed.undo(), ed.undo(), ed.undo();
    CHECK(sameExcept(original, ed.chk(), {}, &which), ("classic triggers: undo gives the map back byte-identical " + which).c_str());
    for (int i = 0; i < 5; ++i) ed.redo();
    CHECK(ed.classicTriggers().triggers[size_t(last)] == e, "classic triggers: redo gives the edits again");
    // Remove a trigger of the map (the first after the new one went last).
    ClassicTriggers before = ed.classicTriggers();
    if (before.triggers.size() > 1) {
        CHECK(ed.removeClassicTrigger(0) && ed.classicTriggers().triggers.size() == before.triggers.size() - 1, "classic triggers: remove a trigger");
        before.triggers.erase(before.triggers.begin());
    }
    // Saved and opened again: the same triggers and text; untouched triggers keep their bytes.
    const fs::path p = scratch / ("editor_test_classic_triggers" + ext);
    Editor back(data);
    const bool saved = ed.save(p, err) && back.open(p, err);
    CHECK(saved && back.classicTriggers().triggers == before.triggers &&
              back.strings().get(back.classicTriggers().triggers.back().actions[1].text) == "editor_test text",
          "classic triggers: saved and opened again, the same");
    CHECK(saved && sameExcept(original, back.chk(), {"TRIG", "STR ", "STRx"}, &which), ("classic triggers: saved, no other section changes " + which).c_str());
    bool kept = saved;
    const ClassicTriggers b = back.classicTriggers();
    for (size_t i = 1; kept && i < t0.triggers.size(); ++i) kept = b.triggers[i - 1] == t0.triggers[i];
    CHECK(kept, "classic triggers: every trigger not edited keeps its bytes");
}

// The isometric brush over tiles placed in Rectangle mode (GitHub #143): where it paints, they give way to the
// terrain's tiles, also when the brush's terrain is the terrain under them (the ISOM does not change).
static void brushOverPlacedTiles(const GameData& data, const char* map) {
    std::string err;
    Editor e(data);
    if (!e.open(map, err)) {
        CHECK(false, "brush over placed tiles: the map opens");
        return;
    }
    const int px = e.width() * 16 + 16, py = e.height() * 16 + 16;  // (the middle of a tile)
    const size_t k = size_t(py / 32) * e.width() + size_t(px / 32);
    const uint16_t terrain = e.terrainAt(px, py);
    const Brush brush{Brush::Diamond, 1.0f};
    e.paintBrush(px, py, terrain, brush);  // the brush's own tiles there first
    const uint16_t painted = e.terrain().tiles[k];
    uint16_t other = 0;  // a tile of another terrain's group
    for (const auto& g : e.tileGroups())
        if (g.doodad < 0 && g.terrains[0] && g.terrains[0] != terrain && g.terrains[1] == 0 && (g.group << 4) != (painted & 0xFFF0)) {
            other = uint16_t(g.group << 4);
            break;
        }
    CHECK(other != 0 && e.placeTiles(px / 32, py / 32, px / 32, py / 32, other) == 1 && e.terrain().tiles[k] == other,
          "brush over placed tiles: a tile of another terrain placed as it is");
    e.paintBrush(px, py, terrain, brush);
    const TerrainLayer tile = readTerrain(e.chk(), "TILE");
    std::printf("      tile %zu: %u painted, %u placed, %u after the brush with the same terrain (%u)\n", k, painted, other, e.terrain().tiles[k], terrain);
    CHECK(e.terrain().tiles[k] == painted && k < tile.tiles.size() && tile.tiles[k] == painted,
          "brush over placed tiles: the brush with the terrain under them gives the terrain's tile again (#143)");
}

// Ramps (and other doodads) stay when terrain is painted near them, as in StarEdit: a doodad goes only when the terrain
// under its tiles no longer fits it (the owner's note: ramps went too often). A Blizzard map with ramps on cliffs:
// (4)Lost Civilization (jungle), doodad 1 (a ramp of the Cliff group, 4 x 4 tiles).
static void rampsStay(const GameData& data, const fs::path& install) {
    const fs::path map = install / "Maps" / "(4)Lost Civilization.scm";
    std::error_code ec;
    if (!fs::exists(map, ec)) {
        std::printf("      (no %s: the ramp checks are skipped)\n", map.string().c_str());
        return;
    }
    std::string err;
    Editor e(data);
    if (!e.open(map, err) || e.doodads().size() < 2) {
        CHECK(false, "ramps: Lost Civilization opens, with its doodads");
        return;
    }
    const Doodad ramp = e.doodads()[1];
    const DoodadType* t = e.doodadTypes().find(ramp.id);
    CHECK(t && t->width == 4 && t->height == 4, "ramps: doodad 1 of Lost Civilization is a 4 x 4 ramp");
    if (!t) return;
    auto has = [&] {
        for (const Doodad& d : e.doodads())
            if (d.id == ramp.id && d.x == ramp.x && d.y == ramp.y) return true;
        return false;
    };
    auto shown = [&] {  // its tiles are on the map (MTXM)
        const int tx = (ramp.x - t->width * 16) / 32, ty = (ramp.y - t->height * 16) / 32;
        for (int r = 0; r < t->height; ++r)
            for (int c = 0; c < t->width; ++c) {
                const size_t g = size_t(t->firstGroup) + r;
                if (g < e.tileset().groups.size() && e.tileset().groups[g].megatiles[size_t(c)] &&
                    (e.terrain().tiles[size_t(ty + r) * e.width() + size_t(tx + c)] >> 4) != g)
                    return false;
            }
        return true;
    };
    const int right = ramp.x + t->width * 16;
    const uint16_t center = e.terrainAt(ramp.x, ramp.y), side = e.terrainAt(right + 3 * 32, ramp.y);
    const Brush brush{Brush::Diamond, 1.0f};
    // Next to it (3 tiles to its right), the terrain of its middle: its tiles stay as they are.
    const size_t before = e.doodads().size();
    e.paintBrush(right + 3 * 32, ramp.y, center, brush);
    std::printf("      ramp %u at %u,%u: painted terrain %u 3 tiles to its right: %zu of %zu doodads gone\n", ramp.id, ramp.x, ramp.y, center,
                before - e.doodads().size(), before);
    CHECK(has() && shown(), "ramps: terrain painted next to a ramp (not under it) leaves the ramp and its tiles");
    e.undo();
    // Every doodad, with the terrain of the next tile 3 tiles to each side of it: a doodad goes only when its terrain
    // changed. Its terrain here is the ISOM, not the tile groups that the editor compares (this check does not ask the
    // editor's own rule again): a doodad that goes had the ISOM of one of its tiles' rects changed, and a doodad that
    // stays has the same terrain type at each corner of its tiles.
    auto footprint = [&](const Doodad& o) {  // its tiles (x, y)
        std::vector<std::pair<int, int>> out;
        const DoodadType* ot = e.doodadTypes().find(o.id);
        if (!ot) return out;
        const int tx = (int(o.x) - ot->width * 16) / 32, ty = (int(o.y) - ot->height * 16) / 32;
        for (int rr = 0; rr < ot->height; ++rr) {
            const size_t g = size_t(ot->firstGroup) + rr;
            if (g >= e.tileset().groups.size()) break;
            for (int cc = 0; cc < ot->width; ++cc) {
                const int cx = tx + cc, cy = ty + rr;
                if (e.tileset().groups[g].megatiles[size_t(cc)] && cx >= 0 && cy >= 0 && cx < int(e.width()) && cy < int(e.height())) out.push_back({cx, cy});
            }
        }
        return out;
    };
    auto isomOf = [&](const Doodad& o) {  // the ISOM entries of its tiles' rects
        std::vector<uint8_t> out;
        const Section* isom = e.chk().find("ISOM");
        const size_t cols = e.width() / 2 + 1;
        for (auto [cx, cy] : footprint(o))
            for (size_t k = 0; k < 8; ++k)
                if (const size_t at = (size_t(cy) * cols + size_t(cx / 2)) * 8 + k; isom && at < isom->data.size()) out.push_back(isom->data[at]);
        return out;
    };
    auto corners = [&](const Doodad& o) {  // the terrain type at each corner of its tiles
        std::vector<uint16_t> out;
        for (auto [cx, cy] : footprint(o))
            for (const int px : {cx * 32 + 2, cx * 32 + 29})
                for (const int py : {cy * 32 + 2, cy * 32 + 29}) out.push_back(e.terrainAt(px, py));
        return out;
    };
    int tries = 0, went = 0, wentSame = 0, stayed = 0, stayedChanged = 0;
    for (size_t i = 0; i < e.doodads().size(); ++i) {
        const Doodad d = e.doodads()[i];
        const DoodadType* dt = e.doodadTypes().find(d.id);
        if (!dt) continue;
        const int l = d.x - dt->width * 16, r = l + dt->width * 32;
        for (const int x : {l - 3 * 32, r + 3 * 32}) {
            if (x < 0 || x >= e.width() * 32) continue;
            struct Near {
                Doodad d;
                std::vector<uint8_t> isom;
                std::vector<uint16_t> corners;
            };
            std::vector<Near> near;  // the doodads that the stroke can reach
            for (const Doodad& o : e.doodads())
                if (std::abs(int(o.x) - x) < 16 * 32 && std::abs(int(o.y) - int(d.y)) < 16 * 32) near.push_back({o, isomOf(o), corners(o)});
            e.paintBrush(x, d.y, e.terrainAt(x, d.y), brush);
            ++tries;
            for (const Near& n : near) {
                const bool goes = std::none_of(e.doodads().begin(), e.doodads().end(), [&](const Doodad& o) { return o.id == n.d.id && o.x == n.d.x && o.y == n.d.y; });
                if (goes) ++went, wentSame += isomOf(n.d) == n.isom;
                else ++stayed, stayedChanged += corners(n.d) != n.corners;
            }
            if (e.canUndo()) e.undo();
        }
    }
    std::printf("      %d strokes next to doodads: %d doodads went (%d with the ISOM of their tiles as before), %d stayed (%d "
                "with another terrain at their tiles' corners)\n", tries, went, wentSame, stayed, stayedChanged);
    CHECK(tries > 100 && went > 0 && wentSame == 0, "ramps: a stroke near doodads removes only those whose terrain it changed");
    CHECK(stayedChanged == 0, "ramps: a stroke that changes the terrain type at a doodad's tiles removes it");
    // Over it, with another terrain: the ramp goes, its tiles too.
    e.paintBrush(ramp.x, ramp.y, side != center ? side : uint16_t(center == 1 ? 2 : 1), Brush{Brush::Diamond, 2.0f});
    CHECK(!has(), "ramps: another terrain painted over a ramp removes it");
}

// Sound files of the map: import, rename and remove are each one undo step, and undo puts the archive and the
// briefings back together (no briefing names a sound that the file list does not have).
static void soundFileSteps(const GameData& data, const char* map) {
    std::string err;
    Editor e(data);
    if (!e.open(map, err)) {
        CHECK(false, "sound files: the map opens");
        return;
    }
    const std::string a = "staredit\\wav\\undo-a.wav", b = "staredit\\wav\\undo-b.wav";
    const Bytes bytes{'R', 'I', 'F', 'F', 1, 2, 3, 4};
    auto has = [&](const std::string& n) { return e.archiveFile(n) != nullptr; };
    auto plays = [&](const std::string& n) {
        for (const auto& t : e.briefings())
            for (const auto& x : t.actions)
                if (x.wav == n) return true;
        return false;
    };
    const size_t files0 = e.archiveFiles().size();
    e.setArchiveFiles({{a, bytes}});
    CHECK(has(a), "sound files: imported");
    e.undo();
    CHECK(!has(a) && e.archiveFiles().size() == files0, "sound files: undo of an import removes the file");
    e.redo();
    CHECK(has(a) && e.archiveFile(a)->data == bytes, "sound files: redo of an import adds it again");
    // A briefing that plays it.
    std::vector<BriefingTrigger> br = e.briefings();
    BriefingTrigger t;
    t.players[0] = true;
    BriefingAction play;
    play.type = BriefingAction::PlayWav, play.wav = a;
    t.actions.push_back(play);
    br.push_back(t);
    e.setBriefings(br);
    CHECK(plays(a), "sound files: a briefing plays it");
    CHECK(e.renameArchiveFile(a, b) && has(b) && !has(a) && plays(b) && !plays(a), "sound files: rename changes the file and the briefing");
    CHECK(!e.renameArchiveFile(b, "STAREDIT\\WAV\\UNDO-B.WAV"), "sound files: rename to a path in the map (in another case) is refused");
    e.undo();
    CHECK(has(a) && !has(b) && plays(a) && !plays(b), "sound files: one undo of a rename puts back the file and the briefing");
    e.redo();
    CHECK(has(b) && !has(a) && plays(b) && !plays(a), "sound files: redo of a rename");
    CHECK(e.removeArchiveFile(b) && !has(b), "sound files: removed");
    e.undo();
    CHECK(has(b) && e.archiveFile(b)->data == bytes && plays(b), "sound files: undo of a remove brings the file back");
    e.undo();
    e.undo();
    e.undo();
    CHECK(!has(a) && !has(b) && !plays(a) && e.archiveFiles().size() == files0, "sound files: undo to the start");
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: editor_test <install dir> <map> <scratch dir>\n");
        return 2;
    }
    std::string err;
    GameData data;
    if (!data.open(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (argc > 4 && std::string(argv[4]) == "sounds") {  // only the sound file checks
        soundFileSteps(data, argv[2]);
        return failures ? 1 : 0;
    }
    if (argc > 4 && std::string(argv[4]) == "terrain") {  // only the brush and doodad checks (GitHub #143, ramps)
        brushOverPlacedTiles(data, argv[2]);
        rampsStay(data, argv[1]);
        std::printf("\n%d failures\n", failures);
        return failures ? 1 : 0;
    }
    Editor ed(data);
    if (!ed.open(argv[2], err)) {
        std::fprintf(stderr, "open: %s\n", err.c_str());
        return 1;
    }
    const UnitTypes& types = ed.unitTypes();
    uint16_t marine = *types.findByName("Terran Marine"), cc = *types.findByName("Terran Command Center");
    size_t units0 = ed.units().size();
    std::printf("%s: %ux%u, %zu units, %d locations slots\n", fs::path(argv[2]).filename().string().c_str(), ed.width(),
                ed.height(), units0, ed.locationCount());

    // Find a water tile (unbuildable + unwalkable) for correction tests.
    int wx = -1, wy = -1;
    for (int ty = 4; ty < ed.height() - 4 && wx < 0; ++ty)
        for (int tx = 4; tx < ed.width() - 4 && wx < 0; ++tx) {
            const TileGroup* g = ed.tileset().groupFor(ed.terrain().tiles[size_t(ty) * ed.width() + tx]);
            if (g && g->type == 5) wx = tx * 32 + 16, wy = ty * 32 + 16;
        }

    // Placement + correction.
    PlaceResult r = ed.placeUnit(cc, 0, wx, wy, false);
    CHECK(!r.placed, "command center on water is refused without correction");
    r = ed.placeUnit(cc, 0, wx, wy, true);
    CHECK(r.placed && checkPlacement(ed.placementContext(), r.unit, r.index).empty(),
          "command center on water is corrected to a valid spot");
    std::printf("      corrected from %d,%d to %u,%u\n", wx, wy, r.unit.x, r.unit.y);
    int ccIndex = r.index;
    r = ed.placeUnit(marine, 1, r.unit.x, r.unit.y, true);
    CHECK(r.placed && !ed.unitBox(r.unit).intersects(ed.unitBox(ed.units()[ccIndex])),
          "marine dropped on the command center is moved out of it");
    int marineIndex = r.index;

    // A Comsat Station dropped near a command center goes on its add-on spot, and both are linked (GitHub #129).
    {
        Editor ae(data);
        std::string aerr;
        if (ae.newMap(64, 64, 0, 1, aerr)) {  // badlands dirt: room to build
            const uint16_t comsat = 107;
            PlaceResult c = ae.placeUnit(cc, 0, 20 * 32, 20 * 32, false);
            const auto [sx, sy] = addonSpot(types[cc], c.unit, types[comsat]);
            CHECK(c.placed && sx > c.unit.x && std::abs(sy - c.unit.y) < 64, "add-on: the Comsat Station's spot is at the right of the command center");
            PlaceResult other = ae.placeUnit(comsat, 1, sx, sy, false);  // another player's: not on this building
            CHECK(other.placed && other.unit.relatedSerial == 0 && other.unit.links == 0, "add-on: not linked to another player's building");
            ae.undo();
            PlaceResult a = ae.placeUnit(comsat, 0, sx + 20, sy - 10, false);
            const Unit& b = ae.units()[size_t(c.index)];
            CHECK(a.placed && a.unit.x == sx && a.unit.y == sy, "add-on: snapped to its building's add-on spot");
            CHECK(a.placed && (a.unit.links & UnitLink::Addon) && a.unit.relatedSerial == b.serial && (b.links & UnitLink::Addon) &&
                      b.relatedSerial == a.unit.serial,
                  "add-on: the add-on and its building are linked to each other");
            PlaceResult second = ae.placeUnit(comsat, 0, sx, sy + 96, false);  // the building has its add-on
            CHECK(!second.placed || second.unit.relatedSerial == 0, "add-on: a building takes one add-on");
        } else CHECK(false, "add-on: a new map");
    }

    // Selection.
    ed.select(marineIndex, false);
    CHECK(ed.selection().size() == 1 && ed.selection()[0] == marineIndex, "click select");
    ed.select(ccIndex, true);
    CHECK(ed.selection().size() == 2, "add to selection");
    ed.toggleSelected(ccIndex);
    CHECK(ed.selection().size() == 1, "toggle removes from selection");
    Rect box = ed.unitBox(ed.units()[ccIndex]);
    ed.selectRect({box.left - 4, box.top - 4, box.right + 4, box.bottom + 4}, false);
    CHECK(ed.isSelected(ccIndex), "box select");
    CHECK(ed.unitAt(ed.units()[marineIndex].x, ed.units()[marineIndex].y) == marineIndex, "hit test finds the marine");

    // Move.
    ed.select(marineIndex, false);
    Unit before = ed.units()[marineIndex];
    ed.moveSelection(64, 32);
    Unit after = ed.units()[marineIndex];
    CHECK(after.x != before.x || after.y != before.y, "drag-move moves the marine");
    CHECK(checkPlacement(ed.placementContext(), after, marineIndex).empty(), "moved marine is in a valid spot");

    // Copy/paste.
    ed.select(marineIndex, false);
    ed.select(ccIndex, true);
    ed.copySelection();
    size_t n = ed.units().size();
    ed.paste(ed.units()[ccIndex].x + 400, ed.units()[ccIndex].y);
    CHECK(ed.units().size() == n + 2 && ed.selection().size() == 2, "paste adds the two copied units and selects them");
    bool pastedValid = true;
    for (int i : ed.selection()) pastedValid &= checkPlacement(ed.placementContext(), ed.units()[i], i).empty();
    CHECK(pastedValid, "pasted units are in valid spots");
    CHECK(ed.units()[ed.selection()[0]].serial != ed.units()[ccIndex].serial, "pasted units get new serials");
    ed.deleteSelection();
    CHECK(ed.units().size() == n, "delete removes the pasted units");

    // Undo / redo.
    ed.undo();
    CHECK(ed.units().size() == n + 2, "undo restores deleted units");
    ed.redo();
    CHECK(ed.units().size() == n, "redo deletes them again");

    // Doodads.
    size_t dd0 = ed.doodads().size(), sp0 = ed.sprites().size();
    bool placed = false;
    std::string why;
    for (const auto& t : ed.doodadTypes().all()) {
        if (!t.overlaySprite) continue;
        if (ed.placeDoodad(t.id, ed.width() * 16, ed.height() * 16, false, &why)) {
            placed = true;
            int tx, ty;
            ed.doodadOrigin(t, ed.width() * 16, ed.height() * 16, tx, ty);
            uint16_t v = ed.terrain().tiles[size_t(ty) * ed.width() + tx];
            CHECK((v >> 4) >= 1024, "doodad tiles written into MTXM");
            break;
        }
    }
    CHECK(placed && ed.doodads().size() == dd0 + 1 && ed.sprites().size() == sp0 + 1, "doodad + overlay sprite placed");
    std::printf("      %zu doodad types for %s\n", ed.doodadTypes().all().size(), tilesetName(ed.era()));

    // Doodads: found under the cursor, deleted (tiles back to the terrain beneath, overlay gone), undone;
    // painting terrain over one removes it.
    if (placed) {
        const size_t dd = ed.doodads().size(), sp = ed.sprites().size();
        const Doodad d = ed.doodads().back();
        const DoodadType* t = ed.doodadTypes().find(d.id);
        int hit = -1, hx = 0, hy = 0;
        for (int y = d.y - t->height * 16 + 16; y < d.y + t->height * 16 && hit < 0; y += 32)
            for (int x = d.x - t->width * 16 + 16; x < d.x + t->width * 16 && hit < 0; x += 32)
                if ((hit = ed.doodadAt(x, y)) >= 0) hx = x, hy = y;
        CHECK(hit == int(dd) - 1, "doodadAt finds the placed doodad");
        const TerrainLayer under = readTerrain(ed.chk(), "TILE");
        CHECK(ed.deleteDoodad(hit) && ed.doodads().size() == dd - 1 && ed.sprites().size() == sp - 1, "delete removes the doodad and its overlay");
        const size_t k = size_t(hy / 32) * ed.width() + hx / 32;
        CHECK(ed.terrain().tiles[k] == under.tiles[k], "its tiles give way to the terrain beneath");
        ed.undo();
        CHECK(ed.doodads().size() == dd && ed.sprites().size() == sp && ed.doodadAt(hx, hy) == int(dd) - 1, "undo brings it back");
        if (ed.canPaintTerrain()) {
            const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
            uint16_t other = 1;
            while (other < 64 && (!table->isTerrain(other) || other == ed.terrainAt(hx, hy))) ++other;
            ed.takeDoodadsRemovedByTerrain();
            ed.paintTerrain(hx, hy, other, 3);
            // (The brush may cover the map's own doodads nearby too: each one painted over goes.)
            const int removed = ed.takeDoodadsRemovedByTerrain();
            CHECK(removed >= 1 && ed.doodads().size() == dd - size_t(removed) && ed.doodadAt(hx, hy) < 0, "painting terrain over a doodad removes it");
            ed.undo();
            CHECK(ed.doodads().size() == dd, "and undo restores it");
        }
    }

    // Locations.
    int loc = ed.createLocation({100, 100, 300, 260});
    CHECK(loc >= 0 && ed.locationUsed(loc), "create location");
    ed.renameLocation(loc, "Test Spot");
    CHECK(ed.locationName(loc) == "Test Spot", "rename location");
    ed.setLocationRect(loc, {200, 200, 456, 456});
    CHECK(ed.locationRect(loc).left == 200 && ed.locationRect(loc).right == 456, "move/resize location");
    CHECK(ed.locationAt(300, 300) == loc, "location hit test");

    // Location elevation flags (MRGN; a set bit excludes the elevation): each one set and cleared, one undo step each.
    {
        namespace LE = LocationElevation;
        CHECK(ed.locationElevation(loc) == 0, "a new location has all elevations");
        bool each = true;
        for (int k = 0; k < 6; ++k) {
            const uint16_t bit = uint16_t(1 << k);
            each = each && ed.setLocationElevation(loc, bit) && ed.locationElevation(loc) == bit;
            each = each && ed.setLocationElevation(loc, 0) && ed.locationElevation(loc) == 0;
        }
        CHECK(each, "each elevation flag can be set and cleared");
        const uint16_t two = LE::LowGround | LE::HighAir;
        CHECK(ed.setLocationElevation(loc, two) && ed.locationElevation(loc) == two, "two flags set");
        CHECK(!ed.setLocationElevation(loc, two), "the same flags again change nothing");
        ed.undo();
        CHECK(ed.locationElevation(loc) == 0, "undo clears them (one undo step)");
        ed.redo();
        CHECK(ed.locationElevation(loc) == two, "redo sets them again");
        ed.setLocationRect(loc, {232, 200, 488, 456});
        ed.setLocationRect(loc, {200, 200, 456, 456});
        CHECK(ed.locationElevation(loc) == two, "the flags stay when the location moves and is resized");
        ed.renameLocation(loc, "Test Spot");
        CHECK(ed.locationElevation(loc) == two, "and when it is renamed");
        CHECK(readLocations(ed.chk())[size_t(loc)].elevation == two, "MRGN has the flags");
    }

    // A map without ISOM: the ISOM made from the tiles is close to the real one, and the tiles stay as they are.
    {
        Chk real = ed.chk();
        IsomGrid orig;
        std::string e;
        const bool hadIsom = orig.load(real, e);
        Chk noIsom = real;
        noIsom.remove("ISOM");
        IsomGrid rebuilt;
        const bool ok = rebuilt.rebuildFromTiles(noIsom, e);
        int same = 0, total = 0;
        for (int y = 0; ok && hadIsom && y <= orig.rectRows(); ++y)
            for (int x = 0; x <= orig.rectCols(); ++x)
                if (orig.isCorner(x, y)) ++total, same += orig.terrainAt(x, y) == rebuilt.terrainAt(x, y);
        const auto tilesOf = [](const Chk& c) { return c.find("MTXM") ? c.find("MTXM")->data : Bytes{}; };
        CHECK(ok && tilesOf(noIsom) == tilesOf(real), ("a map without ISOM: the ISOM is made from the tiles, and the tiles stay (" + e + ")").c_str());
        CHECK(total > 0 && same * 100 >= total * 90,
              ("the ISOM made from the tiles has the real corner terrains (" + std::to_string(same) + " of " + std::to_string(total) + ")").c_str());
    }

    // An inverted location (feedback #56): made inverted in one step, it stays inverted when moved; undo removes it.
    {
        const int inv = ed.createLocation({200, 200, 264, 296}, true);
        CHECK(inv >= 0 && ed.locationInverted(inv), "an inverted location is made inverted");
        const Rect r = ed.locationRect(inv);
        CHECK(r.left == 200 && r.top == 200 && r.right == 264 && r.bottom == 296, "an inverted location's rect is the normal one");
        ed.setLocationRect(inv, {232, 232, 296, 328});
        CHECK(ed.locationInverted(inv) && ed.locationRect(inv).left == 232, "an inverted location stays inverted when moved");
        {  // Location properties: Inverted sets the axes (feedback #77), the edges stay; one undo step
            Editor::LocationEdit e;
            e.rect = ed.locationRect(inv), e.excluded = ed.locationElevation(inv), e.invertedAxes = 1;
            const bool changed = ed.editLocation(inv, e);
            const Rect after = ed.locationRect(inv);
            CHECK(changed && ed.locationInvertedAxes(inv) == 1 && after.left == 232 && after.right == 296 && after.top == 232,
                  "Inverted in the location properties: left and right only, the same edges");
            e.invertedAxes = 0;
            CHECK(ed.editLocation(inv, e) && !ed.locationInverted(inv), "Inverted: No makes it a normal location");
            ed.undo(), ed.undo();
            CHECK(ed.locationInvertedAxes(inv) == 3, "two undo steps make it inverted on both axes again");
        }
        {  // Location properties: the edges as typed (left more than right inverts left and right); one undo step
            Editor::LocationEdit e;
            e.rect = {400, 100, 300, 200}, e.excluded = ed.locationElevation(inv), e.asTyped = true;
            CHECK(ed.editLocation(inv, e) && ed.locationInvertedAxes(inv) == 1 && ed.locationEdges(inv).left == 400 && ed.locationEdges(inv).right == 300,
                  "edges as typed: left more than right inverts it left and right, as typed");
            e.rect = {400, 100, 300, 1 << 20};
            CHECK(ed.editLocation(inv, e) && ed.locationEdges(inv).bottom == (1 << 20), "edges as typed: no limit (an edge out of the map)");
            ed.undo(), ed.undo();
            CHECK(ed.locationInvertedAxes(inv) == 3, "one undo step gives the old edges back");
        }
        ed.undo(), ed.undo();
        CHECK(!ed.locationUsed(inv), "two undo steps remove it");
    }

    // The location edit (name, edges and elevations together): one undo step; wrong edges change nothing; the edges
    // are clamped to the map; the Anywhere location keeps its edges.
    const int loc2 = ed.createLocation({64, 64, 128, 128});
    {
        namespace LE = LocationElevation;
        CHECK(loc2 >= 0, "a second location");
        const std::string name0 = ed.locationName(loc2);
        const Editor::LocationEdit e{"Edit Spot", {96, 160, 352, 288}, LE::MediumAir};
        auto edited = [&] {
            const Rect r = ed.locationRect(loc2);
            return ed.locationName(loc2) == "Edit Spot" && r.left == 96 && r.top == 160 && r.right == 352 && r.bottom == 288 &&
                   ed.locationElevation(loc2) == LE::MediumAir;
        };
        CHECK(ed.editLocation(loc2, e) && edited(), "location edit sets the name, the edges and the elevations");
        CHECK(!ed.editLocation(loc2, e), "the same edit again changes nothing");
        ed.undo();
        const Rect r0 = ed.locationRect(loc2);
        CHECK(ed.locationName(loc2) == name0 && r0.left == 64 && r0.right == 128 && ed.locationElevation(loc2) == 0, "one undo step undoes the location edit");
        ed.redo();
        CHECK(edited(), "redo does it again");
        std::string why;
        CHECK(!ed.editLocation(loc2, {"", {400, 160, 300, 288}, LE::MediumAir}, &why) && !why.empty() && edited(), "right less than left: refused");
        CHECK(!ed.editLocation(loc2, {"", {96, 288, 352, 288}, LE::MediumAir}, &why) && !why.empty() && edited(), "bottom equal to top: refused");
        CHECK(ed.editLocation(loc2, {"", {-50, 160, 1000000, 288}, LE::MediumAir}), "edges off the map");
        const Rect rc = ed.locationRect(loc2);
        CHECK(rc.left == 0 && rc.right == ed.width() * 32 && rc.top == 160, "are clamped to the map");
        ed.undo();
        CHECK(edited(), "and undo puts them back");
        const Location any = ed.locations()[size_t(kAnywhereLocation)];
        ed.editLocation(kAnywhereLocation, {"", {32, 32, 64, 64}, 0});
        const Location any2 = ed.locations()[size_t(kAnywhereLocation)];
        CHECK(any2.left == any.left && any2.top == any.top && any2.right == any.right && any2.bottom == any.bottom, "the Anywhere location keeps its edges");
    }

    // Terrain still paintable.
    if (ed.canPaintTerrain()) {
        uint64_t rev = ed.terrainRevision();
        // A terrain this tileset has (ids differ per tileset; platform has no id 3).
        const IsomTerrainTable* table = IsomTerrainTable::forEra(ed.era());
        uint16_t t = 2;
        while (!table->isTerrain(t)) ++t;
        ed.paintTerrain(ed.width() * 16, ed.height() * 8, t, 2);
        CHECK(ed.terrainRevision() != rev, "terrain paint updates MTXM");
    }

    // Sprites (N.4): place one, find it, delete it, undo; it is saved with the map.
    {
        const size_t before = ed.sprites().size();
        CHECK(ed.placeSprite(130, 640, 640, 0), "sprite placed");
        CHECK(ed.sprites().size() == before + 1 && (ed.sprites().back().flags & SpriteFlag::DrawAsSprite), "sprite in THG2, drawn as a sprite");
        const int at = ed.spriteAt(645, 638);
        CHECK(at == int(before), "spriteAt finds it");
        CHECK(ed.deleteSprite(at) && ed.sprites().size() == before, "sprite deleted");
        ed.undo();
        CHECK(ed.sprites().size() == before + 1, "undo brings the sprite back");
    }

    // Select all of this type: units (and of one player), doodads, sprites (not the doodads' overlays).
    if (!ed.units().empty()) {
        const Unit u0 = ed.units()[0];
        const auto n = std::count_if(ed.units().begin(), ed.units().end(), [&](const Unit& u) { return u.type == u0.type; });
        const auto np = std::count_if(ed.units().begin(), ed.units().end(), [&](const Unit& u) { return u.type == u0.type && u.owner == u0.owner; });
        CHECK(ed.selectUnitsOfType(u0.type) == int(n) && int(ed.selection().size()) == int(n) &&
                  std::all_of(ed.selection().begin(), ed.selection().end(), [&](int i) { return ed.units()[size_t(i)].type == u0.type; }),
              "select all units of a type");
        CHECK(ed.selectUnitsOfType(u0.type, u0.owner) == int(np) &&
                  std::all_of(ed.selection().begin(), ed.selection().end(), [&](int i) { return ed.units()[size_t(i)].owner == u0.owner; }),
              "select all units of a type for one player");
        ed.clearSelection();
    }
    if (!ed.doodads().empty()) {
        const uint16_t id = ed.doodads()[0].id;
        const auto n = std::count_if(ed.doodads().begin(), ed.doodads().end(), [&](const Doodad& d) { return d.id == id; });
        const std::vector<int> all = ed.doodadsOfType(id);
        CHECK(int(all.size()) == int(n) && n >= 1 && std::all_of(all.begin(), all.end(), [&](int i) { return ed.doodads()[size_t(i)].id == id; }),
              "select all doodads of a type");
    }
    {
        const int before = int(ed.sprites().size());
        CHECK(ed.placeSprites({{131, 700, 700, 0}, {132, 760, 700, 0}, {131, 820, 700, 0}}) == 3, "three sprites placed");
        const std::vector<int> same = ed.spritesOfType(before);
        CHECK(std::find(same.begin(), same.end(), before) != same.end() && std::find(same.begin(), same.end(), before + 2) != same.end() &&
                  std::find(same.begin(), same.end(), before + 1) == same.end() &&
                  std::all_of(same.begin(), same.end(), [&](int i) { return ed.sprites()[size_t(i)].id == 131 && !ed.spriteIsDoodadOverlay(i); }),
              "select all sprites of a type (not other sprites, not overlays)");
        ed.undo();
        CHECK(int(ed.sprites().size()) == before, "undo takes the three sprites away");
    }

    // Several sprites at once: place, move, delete (each one undo step).
    {
        const size_t before = ed.sprites().size();
        CHECK(ed.placeSprites({{131, 700, 700, 0}, {132, 760, 700, 0}}) == 2, "two sprites placed at once");
        const int a0 = int(before), b0 = int(before) + 1;
        CHECK(ed.moveSprites({a0, b0}, 32, 0) && ed.sprites()[size_t(a0)].x == 732 && ed.sprites()[size_t(b0)].x == 792, "both sprites move");
        CHECK(ed.deleteSprites({a0, b0}) && ed.sprites().size() == before, "both sprites deleted");
        ed.undo();
        CHECK(ed.sprites().size() == before + 2, "undo brings both back");
        ed.undo(), ed.undo();
        CHECK(ed.sprites().size() == before, "undo of the place takes both away");
    }
    // Replace with...: sprites take the new id where they are; a doodad that fits is replaced, one that does not stays.
    {
        const size_t before = ed.sprites().size();
        ed.placeSprites({{131, 700, 700, 0}, {132, 760, 700, 0}});
        CHECK(ed.replaceSprites({int(before), int(before) + 1}, 140) == 2 && ed.sprites()[before].id == 140 && ed.sprites()[before + 1].x == 760,
              "replace with: both sprites become the new one where they were");
        ed.undo(), ed.undo();
    }
    if (ed.doodads().size() > 1) {
        const size_t n0 = ed.doodads().size();
        const uint16_t other = ed.doodads()[1].id != ed.doodads()[0].id ? ed.doodads()[1].id : uint16_t(ed.doodads()[0].id + 1);
        const int n = ed.replaceDoodads({0}, other, false);
        CHECK(ed.doodads().size() == n0 && (n == 0 || std::any_of(ed.doodads().begin(), ed.doodads().end(), [&](const Doodad& d) { return d.id == other; })),
              "replace with: a doodad is replaced, or stays when the new one does not fit");
        ed.undo();
        CHECK(ed.doodads().size() == n0, "replace with undoes");
    }
    // Convert to terrain: the doodad's tiles stay (MTXM, and TILE takes them), its entry and overlay sprite go; one undo
    // step; save and open again keep the tiles and no doodad there.
    int convertedAt = -1;
    std::vector<uint16_t> convertedTiles;
    Doodad converted;
    if (!ed.doodads().empty()) {
        converted = ed.doodads()[0];
        const DoodadType* t = ed.doodadTypes().find(converted.id);
        const size_t n0 = ed.doodads().size(), s0 = ed.sprites().size();
        const bool overlay = t && t->overlaySprite && std::any_of(ed.sprites().begin(), ed.sprites().end(), [&](const MapSprite& s) {
            return s.id == t->overlaySprite && s.x == converted.x && s.y == converted.y;
        });
        const std::vector<uint16_t> before = ed.terrain().tiles;
        convertedAt = t ? int((int(converted.y) - t->height * 16) / 32 * ed.width() + (int(converted.x) - t->width * 16) / 32) : -1;
        CHECK(ed.convertDoodadsToTerrain({0}) == 1, "convert a doodad to terrain");
        CHECK(ed.terrain().tiles == before, "its tiles stay");
        CHECK(ed.doodads().size() == n0 - 1 && ed.sprites().size() == s0, "its doodad entry goes; its overlay sprite stays, a sprite of its own (#53)");
        (void)overlay;
        CHECK(convertedAt < 0 || readTerrain(ed.chk(), "TILE").tiles[size_t(convertedAt)] == before[size_t(convertedAt)], "TILE has its tiles (placed tiles)");
        ed.undo();
        CHECK(ed.doodads().size() == n0 && ed.sprites().size() == s0 && ed.terrain().tiles == before, "one undo step brings the doodad back");
        ed.redo();
        CHECK(ed.doodads().size() == n0 - 1 && ed.terrain().tiles == before, "redo converts it again");
        convertedTiles = before;
    }

    // Several doodads at once: a move that does not fit moves none.
    if (!ed.doodads().empty()) {
        const Doodad d0 = ed.doodads()[0];
        std::string why;
        const bool moved = ed.moveDoodads({0}, 100000, 0, true, &why);
        CHECK(!moved && ed.doodads()[0].x == d0.x, "a doodad move off the map changes nothing");
    }

    // Save + reopen.
    fs::path out = fs::path(argv[3]) / ("editor_test" + fs::path(argv[2]).extension().string());
    CHECK(ed.save(out, err), "save");
    if (!err.empty()) std::printf("      %s\n", err.c_str());
    Editor re(data);
    CHECK(re.open(out, err), "reopen");
    CHECK(re.units().size() == ed.units().size(), "units persisted");
    CHECK(re.locationName(loc) == "Test Spot" && re.locationRect(loc).left == 200, "location persisted");
    CHECK(re.locationElevation(loc) == (LocationElevation::LowGround | LocationElevation::HighAir), "location elevation flags persisted");
    {
        const Rect r = re.locationRect(loc2);
        CHECK(re.locationName(loc2) == "Edit Spot" && r.left == 96 && r.top == 160 && r.right == 352 && r.bottom == 288 &&
                  re.locationElevation(loc2) == LocationElevation::MediumAir,
              "the location edit persisted (name, edges, elevations)");
    }
    CHECK(re.doodads().size() == ed.doodads().size() && re.sprites().size() == ed.sprites().size(), "doodads persisted");
    CHECK(re.terrain().tiles == ed.terrain().tiles, "terrain persisted");
    if (convertedAt >= 0) {
        CHECK(re.terrain().tiles[size_t(convertedAt)] == convertedTiles[size_t(convertedAt)] &&
                  std::none_of(re.doodads().begin(), re.doodads().end(), [&](const Doodad& d) { return d.x == converted.x && d.y == converted.y && d.id == converted.id; }),
              "a doodad converted to terrain stays tiles after save and open");
    }
    CHECK(!re.sprites().empty() && re.sprites().back().id == 130 && re.sprites().back().x == 640, "sprite persisted");

    // Re-rolling variants: tiles change, terrain (ISOM) does not, one undo restores them.
    {
        const Bytes isom = re.chk().effective("ISOM"), tiles = re.chk().effective("TILE");
        int n = re.rerollTerrain(re.width() * 16, re.height() * 16, Brush{Brush::Diamond, 3}, 7);
        CHECK(n > 0 && re.chk().effective("ISOM") == isom && re.chk().effective("TILE") != tiles, "re-roll changes variants, not terrain");
        re.undo();
        CHECK(re.chk().effective("TILE") == tiles, "re-roll undoes");
        // Painting a terrain on itself over a doodad (a re-roll there): the doodad's tiles stay.
        if (!re.doodads().empty()) {
            const Doodad d = re.doodads().front();
            const Bytes mtxm0 = re.chk().effective("MTXM");
            const size_t doodads0 = re.doodads().size();
            re.rerollTerrain(d.x, d.y, Brush{Brush::Square, 4}, 3);
            const Bytes mtxm1 = re.chk().effective("MTXM");
            bool kept = re.doodads().size() == doodads0;
            if (const DoodadType* t = re.doodadTypes().find(d.id)) {
                const int tx0 = (int(d.x) - t->width * 16) / 32, ty0 = (int(d.y) - t->height * 16) / 32, w = re.width();
                for (int r = 0; r < t->height; ++r)
                    for (int c = 0; c < t->width; ++c) {
                        const size_t k = (size_t(ty0 + r) * w + tx0 + c) * 2;
                        auto u16 = [](const Bytes& b, size_t at) { return uint16_t(b[at] | b[at + 1] << 8); };
                        if (k + 1 < mtxm0.size() && (u16(mtxm0, k) >> 4) >= 1024) kept &= u16(mtxm1, k) == u16(mtxm0, k);
                    }
            }
            CHECK(kept, "painting the same terrain over a doodad keeps the doodad");
        }
    }

    // Terrain edits remove the units they make unplaceable, unless illegal placement is allowed.
    {
        const IsomTerrainTable* table = IsomTerrainTable::forEra(re.era());
        uint16_t water = 0;
        for (uint16_t t = 1; t < 64 && !water; ++t)
            if (table->isTerrain(t) && table->terrainName(t) == std::string("Water")) water = t;
        // Water, or on tilesets without it the first unwalkable terrain (installation: Substructure).
        for (const GenTerrain& g : generatorTerrains(re.era(), re.tileset()))
            if (!water && !g.walkable) water = g.id;
        PlaceResult m = re.placeUnit(marine, 0, re.width() * 16, re.height() * 16, true);
        CHECK(water && m.placed, "marine placed for the terrain test");
        const size_t n0 = re.units().size();
        re.takeUnitsRemovedByTerrain();
        re.paintTerrain(m.unit.x, m.unit.y, water, 3);
        CHECK(re.units().size() < n0 && re.takeUnitsRemovedByTerrain() > 0, "painting water under a marine removes it");
        re.undo();
        CHECK(re.units().size() == n0, "and undo brings it back");
        re.setAllowIllegalUnits(true);
        re.paintTerrain(m.unit.x, m.unit.y, water, 3);
        CHECK(re.units().size() == n0, "with illegal placement allowed the marine stays");
        PlaceResult w = re.placeUnit(marine, 0, m.unit.x, m.unit.y, false);
        CHECK(w.placed && w.unit.x == m.unit.x && w.unit.y == m.unit.y, "and a marine can be placed on the water");
        re.setAllowIllegalUnits(false);
        re.undo(), re.undo(), re.undo();  // the water marine, the paint and the first marine
    }

    // Stacking: air units keep out of each other unless stacking is allowed.
    {
        const uint16_t wraith = *types.findByName("Terran Wraith");
        const int x = re.width() * 16 + 96, y = re.height() * 16 + 96;
        PlaceResult a = re.placeUnit(wraith, 0, x, y, true);
        PlaceResult b = re.placeUnit(wraith, 0, a.unit.x, a.unit.y, false);
        CHECK(a.placed && !b.placed, "a wraith cannot go on top of another wraith");
        re.setAllowStacking(true);
        PlaceResult c = re.placeUnit(wraith, 0, a.unit.x, a.unit.y, false);
        CHECK(c.placed && c.unit.x == a.unit.x && c.unit.y == a.unit.y, "with stacking allowed it can");
        re.setAllowStacking(false);
        re.undo(), re.undo();
    }

    // Air and ground units do not block each other (issues #20, #26, #27); ground units still block ground units.
    {
        const uint16_t wraith = *types.findByName("Terran Wraith");
        const int x = re.width() * 16 + 96, y = re.height() * 16 + 96;
        PlaceResult m = re.placeUnit(marine, 0, x, y, true);
        PlaceResult w = re.placeUnit(wraith, 0, m.unit.x, m.unit.y, false);
        CHECK(m.placed && w.placed && w.unit.x == m.unit.x && w.unit.y == m.unit.y, "a wraith can go on top of a marine");
        re.undo(), re.undo();
        PlaceResult w2 = re.placeUnit(wraith, 0, m.unit.x, m.unit.y, false);  // where a marine can stand
        PlaceResult m2 = re.placeUnit(marine, 0, w2.unit.x, w2.unit.y, false);
        CHECK(w2.placed && m2.placed && m2.unit.x == w2.unit.x && m2.unit.y == w2.unit.y, "a marine can go under a wraith");
        PlaceResult m3 = re.placeUnit(marine, 0, m2.unit.x, m2.unit.y, false);
        CHECK(!m3.placed, "a marine cannot go on top of a marine");
        re.setAllowStacking(true);
        PlaceResult m4 = re.placeUnit(marine, 0, m2.unit.x, m2.unit.y, false);
        CHECK(m4.placed, "with stacking allowed it can");
        re.setAllowStacking(false);
        re.undo(), re.undo(), re.undo();
    }

    // A placement that fails leaves no undo step (issue #29): a refused placeUnit, a stroke that places nothing. A
    // stroke that places something is one step; redo stays.
    {
        const int x = re.width() * 16 + 96, y = re.height() * 16 + 96;
        const size_t n0 = re.units().size();
        PlaceResult a = re.placeUnit(marine, 0, x, y, true);
        const bool could = re.canUndo();
        PlaceResult b = re.placeUnit(marine, 0, a.unit.x, a.unit.y, false);
        CHECK(a.placed && !b.placed && re.canUndo() == could, "a refused placeUnit leaves canUndo as it was");
        re.undo();
        CHECK(re.units().size() == n0 && re.canRedo(), "and no undo step (one undo takes the placed marine away)");
        re.beginStroke();
        re.endStroke();
        CHECK(re.canRedo(), "a stroke that places nothing keeps the redo step");
        re.redo();
        CHECK(re.units().size() == n0 + 1, "and redo brings the marine back");
        re.beginStroke();
        re.placeUnit(marine, 0, a.unit.x, a.unit.y, false);
        re.endStroke();
        re.undo();
        CHECK(re.units().size() == n0, "a stroke that places nothing leaves no undo step");
        re.redo();
        re.beginStroke();
        const bool p1 = re.placeUnit(marine, 0, a.unit.x + 64, a.unit.y, true).placed;
        const bool p2 = re.placeUnit(marine, 0, a.unit.x + 128, a.unit.y, true).placed;
        re.endStroke();
        CHECK(p1 && p2 && re.units().size() == n0 + 3, "a stroke places two marines");
        re.undo();
        CHECK(re.units().size() == n0 + 1, "a stroke that places something is one undo step");
        re.undo();
        CHECK(re.units().size() == n0, "undo the first marine");
    }

    // Terrain paste reproduces the copied terrain away from the seams. Copy and paste start on corners
    // (x + y odd in the 64 x 32 px grid), so source and target line up exactly.
    if (re.canPaintTerrain()) {
        const int ax = re.width() / 8 | 1, ay = re.height() / 4 & ~1;              // a corner: odd + even
        const int bx = re.width() * 5 / 16 | 1, by = re.height() * 5 / 8 & ~1;
        const int sx = ax * 64, sy = ay * 32, dx = bx * 64, dy = by * 32, w = 1280, h = 640;
        CHECK(re.copyTerrain({sx, sy, sx + w, sy + h}, false, false), "copy terrain");
        auto sample = [&](int& differ, int& total) {
            differ = total = 0;
            for (int y = 128; y <= h - 128; y += 32)
                for (int x = 128; x <= w - 128; x += 64)
                    if (((x / 64) + (y / 32)) % 2 == 0)  // corners only (offsets keep the parity)
                        differ += re.terrainAt(dx + x, dy + y) != re.terrainAt(sx + x, sy + y), ++total;
        };
        int before, after, total;
        sample(before, total);
        CHECK(re.pasteTerrain(dx, dy), "paste terrain");
        sample(after, total);
        std::printf("      paste: %d of %d inner corners differed before, %d after\n", before, total, after);
        CHECK(before > 0 && after == 0, "the pasted area matches the copied one away from the seams");
        re.undo();
    }

    // Turning the terrain clipboard: four turns give it back, one turn pastes valid ISOM.
    if (re.canPaintTerrain()) {
        const int cx = re.width() * 16, cy = re.height() * 16;
        CHECK(re.copyTerrain({cx - 300, cy - 200, cx + 200, cy + 100}, false, false), "copy terrain to turn");
        auto before = re.terrainPasteCorners(cx, cy);
        const Rect box = re.terrainClipboardBox();
        re.rotateTerrainClipboard();
        const Rect turned = re.terrainClipboardBox();
        for (int k = 0; k < 3; ++k) re.rotateTerrainClipboard();
        auto after = re.terrainPasteCorners(cx, cy);
        std::sort(before.begin(), before.end()), std::sort(after.begin(), after.end());
        CHECK(before == after && turned.right == box.bottom * 2 && turned.bottom == box.right / 2, "four turns give the clipboard back");
        re.rotateTerrainClipboard();
        CHECK(re.pasteTerrain(cx - 600, cy - 300) && re.invalidTerrain() == 0, "a turned paste is valid ISOM");
        re.undo();
    }

    // Init animations: units and buildings whose idle script shows more than one frame.
    {
        UnitSprites& sp = re.unitSprites();
        int animated = 0;
        std::string names;
        for (uint16_t u = 0; u < re.unitTypes().size(); ++u) {
            if (!(re.unitTypes()[u].availability & UnitAvail::Palette)) continue;
            const int image = sp.imageFor(u);
            const bool built = re.unitTypes()[u].is(UnitFlag::Building) && sp.animOffset(image, 16);
            ImageAnim a(sp, image, u, built ? 16 : 0);
            std::set<int> frames{a.frame()};
            for (int t = 0; t < 24 * 20; ++t) a.tick(), frames.insert(a.frame());
            if (frames.size() > 1) {
                ++animated;
                if (animated <= 12) names += (names.empty() ? "" : ", ") + re.unitTypes()[u].name;
            }
        }
        std::printf("      %d palette units animate, e.g. %s\n", animated, names.c_str());
        const uint16_t hatchery = *types.findByName("Zerg Hatchery");
        ImageAnim h(sp, sp.imageFor(hatchery), 1, 16);
        std::set<int> hf{h.frame()};
        for (int t = 0; t < 24 * 10; ++t) h.tick(), hf.insert(h.frame());
        CHECK(hf.size() > 1, "the hatchery's idle animation runs");
    }

    // Long history: 500 edits, all undone, all redone.
    {
        const Bytes startUnits = re.chk().effective("UNIT");
        int placed = 0;
        for (int i = 0; i < 500; ++i)
            placed += re.placeUnit(0, 0, 200 + (i % 25) * 40, 200 + (i / 25) * 40, true).placed;
        const Bytes endUnits = re.chk().effective("UNIT");
        int undone = 0;
        while (re.canUndo() && undone < 600) re.undo(), ++undone;
        CHECK(re.chk().effective("UNIT") == startUnits && undone >= placed, "500 edits undo back to the start");
        while (re.canRedo()) re.redo();
        CHECK(re.chk().effective("UNIT") == endUnits, "and redo forward to the end");
    }

    // Map properties: title and description; a new size and tileset, each one undo step.
    {
        re.setScenarioText("StarDraft test", "A map for the test.\nSecond line.");
        CHECK(re.scenarioName() == "StarDraft test" && re.scenarioDescription() == "A map for the test.\nSecond line.", "title and description set");
        re.undo();
        CHECK(re.scenarioName() != "StarDraft test", "and undone");
        const uint16_t w0 = re.width(), h0 = re.height(), era0 = re.era();
        const size_t units0 = re.units().size(), doodads0 = re.doodads().size();
        const Bytes isom0 = re.chk().effective("ISOM");
        std::string err;
        // Bigger by 64 x 32, the old map moved right 32 and down 16 tiles: everything stays.
        const bool grew = re.resizeMap(uint16_t(w0 + 64), uint16_t(h0 + 32), era0, 32, 16, 1, err);
        CHECK(grew && re.width() == w0 + 64 && re.height() == h0 + 32, "the map grows");
        CHECK(re.units().size() == units0 && re.doodads().size() == doodads0, "units and doodads stay when the map grows");
        CHECK(re.invalidTerrain() == 0, "the grown terrain is valid");
        bool moved = true;
        for (size_t i = 0; i < std::min<size_t>(units0, 5); ++i) moved &= re.units()[i].x >= 32 * 32;
        CHECK(moved, "units moved with the terrain");
        CHECK(re.locationElevation(loc) == (LocationElevation::LowGround | LocationElevation::HighAir), "location elevation flags stay when the map grows");
        re.undo();
        CHECK(re.width() == w0 && re.height() == h0 && re.chk().effective("ISOM") == isom0 && re.units().size() == units0, "the resize is one undo step");
        // Smaller: what is outside goes.
        CHECK(re.resizeMap(64, 64, era0, 0, 0, 1, err) && re.width() == 64 && re.units().size() <= units0 && re.invalidTerrain() == 0, "the map shrinks");
        re.undo();
        // Another tileset: terrain of the same names, the doodads it does not have go.
        const uint16_t other = era0 == 0 ? 4 : 0;  // jungle or badlands
        CHECK(re.resizeMap(w0, h0, other, 0, 0, 1, err) && re.era() == other && re.invalidTerrain() == 0, "another tileset");
        CHECK(re.doodads().empty(), "a change of tileset leaves no doodads (a doodad id is another doodad in another tileset)");
        re.undo();
        CHECK(re.era() == era0 && re.chk().effective("ISOM") == isom0, "and undone");
    }
    // Player colors (COLR, CRGB): unchanged players keep the map's bytes; a color id, an id only Remastered has, and a
    // custom RGB read back, save and open again; undo gives the map back.
    // Selection shapes (feedback #31, #33, #52): a diamond or an ellipse copies the tiles inside it; paste leaves the rest.
    {
        Editor sh(data);
        std::string e1;
        if (sh.open(argv[2], e1)) {
            CHECK(sh.copyTiles(10, 10, 19, 19, Editor::SelectShape::Ellipse), "an ellipse tile copy");
            const auto& v = sh.tileClipboard();
            const bool corners = v[0] == 0xFFFF && v[9] == 0xFFFF && v[90] == 0xFFFF && v[99] == 0xFFFF, middle = v[55] != 0xFFFF;
            CHECK(corners && middle, "the ellipse leaves out the box's corners and keeps its middle");
            const std::vector<uint16_t> before = sh.terrain().tiles;
            sh.pasteTiles(40, 40);
            CHECK(sh.terrain().tiles[size_t(40) * sh.width() + 40] == before[size_t(40) * sh.width() + 40], "paste leaves the tiles outside the shape");
            CHECK(Editor::inSelectShape({0, 0, 100, 100}, Editor::SelectShape::Diamond, 50, 5) && !Editor::inSelectShape({0, 0, 100, 100}, Editor::SelectShape::Diamond, 10, 10),
                  "a diamond holds its top point, not the box's corner");
            // A location inverted on one axis stays so (only that axis) when moved.
            const int one = sh.createLocation({100, 100, 200, 200}, true, 1);
            sh.setLocationRect(one, {132, 132, 232, 232});
            CHECK(one >= 0 && sh.locationInvertedAxes(one) == 1, "a location inverted on one axis stays inverted on that axis only");
        }
    }

    // A new map with the terrain of another (feedback #48): its size, tileset, tiles and doodads; no units or locations.
    {
        Editor src(data), nt(data);
        std::string e1, e2;
        if (src.open(argv[2], e1) && nt.newMapFromTerrain(argv[2], e2)) {
            CHECK(nt.width() == src.width() && nt.height() == src.height() && nt.era() == src.era(), "the terrain map has the map's size and tileset");
            CHECK(nt.terrain().tiles == src.terrain().tiles, "the terrain map has the map's tiles");
            CHECK(nt.doodads().size() == src.doodads().size(), "the terrain map has the map's doodads");
            CHECK(nt.units().empty(), "the terrain map has no units");
        } else {
            CHECK(false, ("the terrain map: " + e1 + e2).c_str());
        }
    }

    {
        Editor pc(data);
        if (pc.open(argv[2], err)) {
            const Chk original = pc.chk();
            std::vector<Editor::Player> players;
            std::vector<Editor::Force> forces;
            for (int p = 0; p < 8; ++p) players.push_back(pc.player(p));
            for (int f = 0; f < 4; ++f) forces.push_back(pc.force(f));
            pc.setPlayersAndForces(players, forces);
            std::string which;
            CHECK(sameExcept(original, pc.chk(), {"IOWN", "OWNR", "SIDE", "FORC", "STR ", "STRx"}, &which) &&
                      pc.chk().effective("COLR") == original.effective("COLR") && pc.chk().effective("CRGB") == original.effective("CRGB"),
                  ("player colors: unchanged players keep COLR and CRGB " + which).c_str());
            pc.undo();
            players[1].color = {Editor::PlayerColor::Id, 9, 0};        // pale yellow (COLR has it)
            players[2].color = {Editor::PlayerColor::Id, 14, 0};       // cyan (only CRGB)
            players[3].color = {Editor::PlayerColor::Custom, 0, 0x123456};
            pc.setPlayersAndForces(players, forces);
            const bool read = pc.player(1).color == players[1].color && pc.player(2).color == players[2].color &&
                              pc.player(3).color == players[3].color && pc.player(0).color.kind == Editor::PlayerColor::Id;
            CHECK(read, "player colors: an id, a Remastered id and an RGB read back as set");
            CHECK(pc.playerRgb(1) == 0xFCFC7C && pc.playerRgb(2) == 0x00E4FC && pc.playerRgb(3) == 0x123456, "player colors: the map view's colors");
            CHECK(pc.chk().effective("COLR").size() == 8 && pc.chk().effective("COLR")[1] == 9 && pc.chk().effective("CRGB").size() == 32,
                  "player colors: COLR has the id, CRGB is there");
            const fs::path p = fs::path(argv[3]) / ("editor_test_colors" + fs::path(argv[2]).extension().string());
            Editor back(data);
            const bool saved = pc.save(p, err) && back.open(p, err);
            CHECK(saved && back.player(2).color == players[2].color && back.player(3).color == players[3].color, "player colors: saved and opened again");
            pc.undo();
            CHECK(sameExcept(original, pc.chk(), {}, &which) && pc.chk().effective("CRGB") == original.effective("CRGB"),
                  ("player colors: undo gives the map back " + which).c_str());
        }
    }
    // Terrain by kind through a change of tileset (1.10): water and high ground painted on a new badlands map stay
    // water (space, magma, tar) and high ground in every other tileset.
    {
        Editor tm(data);
        std::string err;
        if (tm.newMap(64, 64, 0, 1, err)) {
            tm.paintBrush(20 * 32, 20 * 32, 3, Brush{Brush::Circle, 4});   // water
            tm.paintBrush(44 * 32, 44 * 32, 2, Brush{Brush::Circle, 4});   // high dirt
            bool kinds = true;
            for (uint16_t era = 1; era < 8; ++era) {
                if (!tm.resizeMap(64, 64, era, 0, 0, 1, err)) {
                    kinds = false;
                    std::printf("      to %s: %s\n", tilesetName(era), err.c_str());
                    continue;
                }
                const IsomTerrainTable* t = IsomTerrainTable::forEra(era);
                const std::string wet = t->terrainName(tm.terrainAt(20 * 32, 20 * 32)), high = t->terrainName(tm.terrainAt(44 * 32, 44 * 32));
                const auto m = Editor::tilesetTerrainMap(0, era, 0);
                const bool ok = tm.terrainAt(20 * 32, 20 * 32) == m[3] && tm.terrainAt(44 * 32, 44 * 32) == m[2];
                std::printf("      badlands -> %s: water is %s, high dirt is %s%s\n", tilesetName(era), wet.c_str(), high.c_str(), ok ? "" : " (NOT as mapped)");
                kinds &= ok;
                tm.undo();
            }
            CHECK(kinds, "water and high ground keep their kind in every tileset");
        }
    }

    // R2: the tiles the ISOM gives at Temple edges and small walls (jungle). Strokes of Temple, High Temple, High
    // Jungle, Raised Jungle and Jungle on Jungle Rumble (as tools/paint_stress with seed 7): every cliff or wall
    // piece stacks on the piece above it (CV5 stack connections), also where Blizzard's maps never put those two
    // pieces together. Before the fix, 4 pieces here did not stack.
    {
        Editor tm(data);
        std::string err;
        const fs::path jungleMap = fs::path(argv[1]) / "Maps" / "(8)Jungle Rumble.scm";
        if (fs::exists(jungleMap) && tm.open(jungleMap.string(), err) && tm.canPaintTerrain()) {
            std::mt19937 rng(7);
            const uint16_t terrains[] = {8, 12, 9, 5, 4};
            for (int s = 0; s < 80; ++s) {
                const uint16_t t = terrains[rng() % 5];
                const int brush = 1 + int(rng() % 4);
                const float x = float(rng() % (tm.width() * 32)), y = float(rng() % (tm.height() * 32));
                const float dx = float(int(rng() % 41) - 20), dy = float(int(rng() % 41) - 20);
                tm.beginTerrainStroke();
                for (int i = 0; i < 40; ++i) tm.paintTerrain(int(x + dx * i), int(y + dy * i), t, brush);
                tm.endTerrainStroke();
            }
            const TerrainLayer tiles = readTerrain(tm.chk(), "TILE");
            int breaks = 0, pieces = 0;
            for (int ty = 1; ty < tiles.height; ++ty)
                for (int tx = 0; tx < tiles.width; ++tx) {
                    const uint16_t va = tiles.tiles[size_t(ty - 1) * tiles.width + tx], vb = tiles.tiles[size_t(ty) * tiles.width + tx];
                    const TileGroup* a = tm.tileset().groupFor(va);
                    const TileGroup* b = tm.tileset().groupFor(vb);
                    if (!a || !b || (va >> 4) >= 1024 || (vb >> 4) >= 1024) continue;
                    pieces += b->extra[1] != 0;
                    breaks += a->extra[3] != b->extra[1];
                }
            std::printf("      jungle Temple strokes: %d stacked pieces, %d that do not stack\n", pieces, breaks);
            CHECK(pieces > 50 && breaks == 0, "Temple edges and small walls stack (jungle)");
        } else {
            std::printf("      skipped the jungle Temple strokes: no %s\n", jungleMap.string().c_str());
        }
    }

    placementOptions(data, argv[2]);
    soundFileSteps(data, argv[2]);
    placementRules(data, argv[2]);
    // A small brush (size 1) painted in many quick strokes on each tileset: the tiles fit their neighbors as in
    // Blizzard's maps (they have none that do not). A few spots can stay where no piece and no corner change fits.
    {
        int misfits = 0, tilesets = 0;
        for (uint16_t era = 0; era < 8; ++era) {
            Editor t(data);
            std::string err2;
            if (!t.newMap(128, 128, era, 1, err2) || !t.canPaintTerrain()) continue;
            ++tilesets;
            const IsomTerrainTable* table = IsomTerrainTable::forEra(t.era());
            std::mt19937 rng(7);
            for (int s = 0; s < 120; ++s) {
                uint16_t terrain = 0;
                while (!table->isTerrain(terrain)) terrain = uint16_t(1 + rng() % table->terrainCount());
                float x = float(rng() % (128 * 32)), y = float(rng() % (128 * 32));
                const float dx = float(int(rng() % 41) - 20), dy = float(int(rng() % 41) - 20);
                t.beginTerrainStroke();
                for (int i = 0; i < 40; ++i) t.paintBrush(int(x + dx * i), int(y + dy * i), terrain, Brush{Brush::Diamond, 1.0f});
                t.endTerrainStroke();
            }
            misfits += countTileMisfits(t.chk());
        }
        const std::string what = "size 1 brush strokes on every tileset leave few tiles that do not fit (" + std::to_string(misfits) + " in " +
                                 std::to_string(tilesets) + " maps, at most 12)";
        CHECK(tilesets == 8 && misfits <= 12, what.c_str());
    }
    // Rectangular terrain (N.7): tiles placed as they are, kept by later terrain edits that do not touch their rects,
    // saved and opened again as placed; the ISOM stays valid.
    if (Editor te(data); te.open(argv[2], err) && te.canPaintTerrain()) {
        const int w = te.width(), cx = w / 2, cy = te.height() / 2, rectCols = w / 2 + 1;
        const auto& groups = te.tileGroups();
        size_t doodadGroups = 0, withTerrain = 0;
        for (const auto& g : groups) doodadGroups += g.doodad >= 0, withTerrain += g.terrains[0] != 0;
        std::printf("      %zu tile groups in %s (%zu of doodads, %zu with a terrain)\n", groups.size(), tilesetName(te.era()), doodadGroups, withTerrain);
        CHECK(groups.size() > 100 && doodadGroups > 0 && withTerrain > 0, "the Tiles palette lists the tileset's groups");
        // A tile of another terrain than the one at the spot: the ISOM could not give it there.
        const uint16_t here = te.terrainAt(cx * 32, cy * 32);
        uint16_t value = 0, value2 = 0;
        for (const auto& g : groups)
            if (g.doodad < 0 && g.terrains[0] && g.terrains[0] != here && g.terrains[1] != here && !g.terrains[1]) {
                const uint16_t v = uint16_t(g.group << 4 | std::countr_zero(unsigned(g.subtiles)));
                if (!value) value = v;
                else if ((v >> 4) != (value >> 4) && !value2) value2 = v;
            }
        CHECK(value && value2, "two tiles of other terrains found");
        const Bytes isom0 = te.chk().effective("ISOM");
        const int invalid0 = te.invalidTerrain();
        // One tile.
        CHECK(te.placeTiles(cx, cy, cx, cy, value) == 1, "a click places one tile");
        CHECK(te.terrain().tiles[size_t(cy) * w + cx] == value && readTerrain(te.chk(), "TILE").tiles[size_t(cy) * w + cx] == value,
              "the tile is in MTXM and TILE");
        CHECK(te.chk().effective("ISOM") == isom0, "the ISOM does not change");
        CHECK(te.placeTiles(cx, cy, cx, cy, value) == 0, "placing the same tile again changes nothing");
        // A rectangle (dragged from its bottom right to its top left).
        const int x0 = cx - 12, y0 = cy - 1, x1 = cx - 2, y1 = cy + 2;
        // (Tiles that have this value already do not count as changed.)
        const int area = (x1 - x0 + 1) * (y1 - y0 + 1), placedN = te.placeTiles(x1, y1, x0, y0, value2);
        std::printf("      rectangle: %d of %d tiles changed\n", placedN, area);
        CHECK(placedN > area / 2 && placedN <= area, "a drag places a rectangle of the tile");
        auto rectHas = [&](const Editor& e, uint16_t v) {
            const TerrainLayer t = readTerrain(e.chk(), "TILE");
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if (e.terrain().tiles[size_t(y) * w + x] != v || t.tiles[size_t(y) * w + x] != v) return false;
            return true;
        };
        CHECK(rectHas(te, value2) && te.terrain().tiles[size_t(cy) * w + cx] == value, "every tile of the rectangle is placed");
        CHECK(te.invalidTerrain() == invalid0 && te.chk().effective("ISOM") == isom0, "the ISOM stays valid and the same");
        te.undo();
        CHECK(!rectHas(te, value2) && te.terrain().tiles[size_t(cy) * w + cx] == value, "undo takes the rectangle back (one step)");
        te.redo();
        CHECK(rectHas(te, value2), "redo places it again");
        // A tile placed on a doodad removes the doodad (as painting does); TILE and MTXM then agree there.
        if (!te.doodads().empty()) {
            const Doodad d = te.doodads()[0];
            const size_t dd = te.doodads().size();
            int hit = te.doodadAt(d.x, d.y), hx = d.x, hy = d.y;
            if (hit < 0)
                if (const DoodadType* t = te.doodadTypes().find(d.id))
                    for (int y = d.y - t->height * 16 + 16; y < d.y + t->height * 16 && hit < 0; y += 32)
                        for (int x = d.x - t->width * 16 + 16; x < d.x + t->width * 16 && hit < 0; x += 32)
                            if ((hit = te.doodadAt(x, y)) >= 0) hx = x, hy = y;
            if (hit >= 0) {
                te.takeDoodadsRemovedByTerrain();
                te.placeTiles(hx / 32, hy / 32, hx / 32, hy / 32, value);
                const size_t k = size_t(hy / 32) * w + hx / 32;
                CHECK(te.doodads().size() == dd - 1 && te.takeDoodadsRemovedByTerrain() == 1 && te.terrain().tiles[k] == value &&
                          readTerrain(te.chk(), "TILE").tiles[k] == value,
                      "a tile placed on a doodad removes the doodad");
                te.undo();
                CHECK(te.doodads().size() == dd, "and undo brings it back");
            }
        }
        // A brush next to the placed tiles: its correction may rewrite the tiles of the rects whose ISOM it changes;
        // every other placed tile stays. (A placed tile that the ISOM could have given there is a terrain tile like
        // the others: the correction may pick another for it.)
        {
            const Bytes before = te.chk().effective("ISOM");
            std::vector<uint8_t> foreign(size_t(w) * te.height(), 0);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) foreign[size_t(y) * w + x] = !tileFromIsom(te.chk(), x, y);
            uint16_t other = 1;
            const IsomTerrainTable* table = IsomTerrainTable::forEra(te.era());
            const uint16_t at = te.terrainAt(x0 * 32 - 128, cy * 32);  // a terrain next to it in the terrain tree: a small change
            while (other < 64 && (!table->isTerrain(other) || other == at || !table->linked(at, other))) ++other;
            te.paintTerrain(x0 * 32 - 128, cy * 32, other, 1);
            const Bytes after = te.chk().effective("ISOM");
            int kept = 0, touchedRewritten = 0, touchedKept = 0, lost = 0, regained = 0;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const size_t r = (size_t(y) * rectCols + x / 2) * 8;
                    const bool touched = !std::equal(before.begin() + long(r), before.begin() + long(r) + 8, after.begin() + long(r));
                    const bool same = te.terrain().tiles[size_t(y) * w + x] == value2;
                    if (touched) (same ? touchedKept : touchedRewritten)++;
                    else if (same) ++kept;
                    else if (foreign[size_t(y) * w + x]) ++lost;
                    else ++regained;
                }
            std::printf("      brush beside the rectangle: %d placed tiles in untouched rects kept, %d lost, %d that the ISOM could give "
                        "re-picked; %d in changed rects rewritten, %d kept\n",
                        kept, lost, regained, touchedRewritten, touchedKept);
            CHECK(lost == 0 && kept > 0, "a brush keeps the placed tiles whose rects it does not change");
            CHECK(te.invalidTerrain() == 0 || te.invalidTerrain() == invalid0, "the ISOM is still valid after the brush");
            te.undo();
            CHECK(rectHas(te, value2), "undo of the brush gives the rectangle back");
        }
        // Save and open again: the tiles are there, TILE agrees with MTXM, the ISOM is the same and valid.
        const fs::path tilesOut = fs::path(argv[3]) / ("editor_test_tiles" + fs::path(argv[2]).extension().string());
        CHECK(te.save(tilesOut, err), "save with placed tiles");
        Editor tr(data);
        CHECK(tr.open(tilesOut, err) && rectHas(tr, value2) && tr.terrain().tiles[size_t(cy) * w + cx] == value, "the placed tiles open again as placed");
        CHECK(tr.chk().effective("ISOM") == isom0 && tr.invalidTerrain() == invalid0, "the ISOM opens again the same and valid");
        CHECK(tr.terrain().tiles == te.terrain().tiles && readTerrain(tr.chk(), "TILE").tiles == readTerrain(te.chk(), "TILE").tiles,
              "MTXM and TILE open again the same");
        // Re-rolling variants under the brush leaves the placed tiles as they are.
        tr.rerollTerrain(cx * 32 - 128, cy * 32, Brush{Brush::Square, 3}, 11);
        CHECK(rectHas(tr, value2), "re-rolling variants keeps the placed tiles");

        // The Terrain tool strip's Rectangle, then Isometric: tiles painted, copied and pasted as they are (a null tile
        // too: megatile 0, the empty tile), then an ISOM brush away from them. Both kinds of edit save and open again.
        Editor rm(data);
        if (rm.open(argv[2], err) && rm.canPaintTerrain()) {
            const int W = rm.width();
            CHECK(rm.placeTiles(4, 4, 7, 6, value) > 0, "Rectangle: a drag paints a rectangle of tiles");
            CHECK(rm.copyTiles(7, 6, 4, 4) && rm.tileClipboardWidth() == 4 && rm.tileClipboardHeight() == 3, "Rectangle: copy a rectangle of tiles");
            const std::vector<uint16_t> beforePaste = rm.terrain().tiles;
            CHECK(rm.pasteTiles(12, 4) > 0, "Rectangle: paste the copied tiles");
            rm.undo();
            CHECK(rm.terrain().tiles == beforePaste, "Rectangle: undo takes the paste back (one step)");
            rm.redo();
            uint16_t nullTile = 0xFFFF;
            for (size_t g = 1; g < rm.tileset().groups.size() && nullTile == 0xFFFF; ++g)
                for (int s = 0; s < 16; ++s)
                    if (rm.tileset().groups[g].megatiles[size_t(s)] == 0) {
                        nullTile = uint16_t(g << 4 | s);
                        break;
                    }
            CHECK(nullTile != 0xFFFF && rm.placeTiles(20, 4, 20, 4, nullTile) == 1, "Rectangle: a null tile places");
            // Isometric: a brush in the middle of the map, far from the tiles.
            const IsomTerrainTable* table = IsomTerrainTable::forEra(rm.era());
            const int mx = W * 16, my = rm.height() * 16;
            const uint16_t at = rm.terrainAt(mx, my);
            uint16_t other = 1;
            while (other < 64 && (!table->isTerrain(other) || other == at || !table->linked(at, other))) ++other;
            rm.beginTerrainStroke();
            rm.paintBrush(mx, my, other, Brush{Brush::Diamond, 2});
            rm.endTerrainStroke();
            CHECK(rm.terrainAt(mx, my) == other, "Isometric: the brush paints after Rectangle edits");
            auto tilesKept = [&](const Editor& e) {
                for (int y = 4; y <= 6; ++y)
                    for (int x = 4; x <= 7; ++x)
                        if (e.terrain().tiles[size_t(y) * W + x] != value || e.terrain().tiles[size_t(y) * W + x + 8] != value) return false;
                return e.terrain().tiles[size_t(4) * W + 20] == nullTile && readTerrain(e.chk(), "TILE").tiles[size_t(4) * W + 20] == nullTile;
            };
            CHECK(tilesKept(rm), "Isometric: the brush keeps the tiles placed in Rectangle mode");
            const fs::path both = fs::path(argv[3]) / ("editor_test_rect_iso" + fs::path(argv[2]).extension().string());
            Editor rb(data);
            CHECK(rm.save(both, err) && rb.open(both, err), "save and open the map with Rectangle and Isometric edits");
            CHECK(tilesKept(rb) && rb.terrain().tiles == rm.terrain().tiles, "the Rectangle tiles (the null tile too) open again as placed");
            CHECK(rb.chk().effective("ISOM") == rm.chk().effective("ISOM") && rb.terrainAt(mx, my) == other && rb.invalidTerrain() == rm.invalidTerrain(),
                  "the Isometric brush opens again as painted");
        }

        // The brush over its own terrain (a click and a drag, as the Terrain brush does it): new tile variants, the
        // same ISOM, and the stroke is one undo step.
        Editor same(data);
        if (same.open(argv[2], err) && same.canPaintTerrain()) {
            int px = same.width() * 16, py = same.height() * 16;
            const Brush b0{Brush::Diamond, 2};
            bool found = false;  // a spot of one terrain, from the middle out
            for (int r = 0; r < same.width() * 16 && !found; r += 64)
                for (int y = py - r; y <= py + r && !found; y += 64)
                    for (int x = px - r; x <= px + r && !found; x += 64)
                        if (x > 256 && y > 256 && x < same.width() * 32 - 512 && y < same.height() * 32 - 256 &&
                            same.brushAllTerrain(x, y, b0, same.terrainAt(x, y)) && same.brushAllTerrain(x + 64, y, b0, same.terrainAt(x, y)))
                            px = x, py = y, found = true;
            const uint16_t ground = same.terrainAt(px, py);
            const Bytes isomBefore = same.chk().effective("ISOM");
            const std::vector<uint16_t> tilesBefore = same.terrain().tiles;
            const Brush b{Brush::Diamond, 2};
            same.beginTerrainStroke();
            int rerolled = 0, spots = 0;
            for (int k = 0; k < 4; ++k)  // along a drag, where the ground is all the brush's terrain
                if (same.brushAllTerrain(px + k * 64, py, b, ground)) {
                    ++spots;
                    rerolled += same.rerollTerrain(px + k * 64, py, b, uint32_t(7 + k));
                    same.paintBrush(px + k * 64, py, ground, b);
                }
            CHECK(spots > 0, "the brush is over its own terrain");
            same.endTerrainStroke();
            int changed = 0;
            for (size_t i = 0; i < tilesBefore.size(); ++i) changed += same.terrain().tiles[i] != tilesBefore[i];
            std::printf("      brush over its own terrain: %d tiles redrawn (%d changed)\n", rerolled, changed);
            CHECK(changed > 0 && same.chk().effective("ISOM") == isomBefore, "a brush over its own terrain redraws tiles (new variants), the ISOM the same");
            same.undo();
            CHECK(same.terrain().tiles == tilesBefore && same.chk().effective("ISOM") == isomBefore, "one undo gives the map back");
        }
    }

    // Every tile of every tileset can be placed, and opens again as placed (on a new 64 x 64 map per tileset).
    {
        int tilesets = 0, values = 0, wrong = 0;
        for (uint16_t era = 0; era < 8; ++era) {
            Editor nm(data);
            const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
            uint16_t t = 1;
            while (table && t < 64 && !table->isTerrain(t)) ++t;
            std::string e2;
            if (!nm.newMap(64, 64, era, t, e2)) {
                std::printf("      %s: no new map (%s)\n", tilesetName(era), e2.c_str());
                ++wrong;
                continue;
            }
            std::vector<uint16_t> all;
            for (const auto& g : nm.tileGroups())
                for (int s = 0; s < 16; ++s)
                    if ((g.subtiles >> s) & 1) all.push_back(uint16_t(g.group << 4 | s));
            ++tilesets;
            const size_t cells = 64 * 64;
            for (size_t start = 0; start < all.size(); start += cells) {
                Editor batch(data);
                batch.newMap(64, 64, era, t, e2);
                const size_t n = std::min(cells, all.size() - start);
                for (size_t k = 0; k < n; ++k) batch.placeTiles(int(k % 64), int(k / 64), int(k % 64), int(k / 64), all[start + k]);
                const fs::path p = fs::path(argv[3]) / ("editor_test_alltiles_" + std::to_string(era) + "_" + std::to_string(start / cells) + ".chk");
                Editor back(data);
                if (!batch.save(p, e2) || !back.open(p, e2)) {
                    ++wrong;
                    continue;
                }
                const TerrainLayer tile = readTerrain(back.chk(), "TILE");
                for (size_t k = 0; k < n; ++k) {
                    ++values;
                    if (back.terrain().tiles[k] != all[start + k] || tile.tiles[k] != all[start + k]) ++wrong;
                }
                if (back.invalidTerrain() != 0) ++wrong;
            }
            std::printf("      %s: %zu tile values\n", tilesetName(era), all.size());
        }
        std::printf("      %d tilesets, %d tile values placed, saved and opened again, %d wrong\n", tilesets, values, wrong);
        CHECK(tilesets == 8 && values > 0 && wrong == 0, "every tile of every tileset places, saves and opens again as placed");
    }

    iscriptPlayerTest(ed);
    brushOverPlacedTiles(data, argv[2]);
    rampsStay(data, argv[1]);
    classicSettings(data, argv[2], fs::path(argv[3]));
    classicTriggersTest(data, argv[2], fs::path(argv[3]));

    std::printf("\n%d failures\n", failures);
    return failures ? 1 : 0;
}
