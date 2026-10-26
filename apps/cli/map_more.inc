// stardraft-cli: the switch names, the map strings, the sounds, the spray and decorate tools, the replace and map revealer
// edits, and the start locations, as JSON (included by main.cpp, inside its anonymous namespace; it uses main.cpp's
// helpers). The symmetry and the resource distance are apply options (cmdApply), not operations.

// --- Reading ---------------------------------------------------------------------------------------------------------

// `get` parts: switches (the names), strings (the map string table), sounds (the sound files the map names by path).
// known: false when `what` is not one of them.
// The sounds list the editor's own: its map archive holds the map's files (loadMapFiles for an open map), so a file of the
// map is listed as a file, not also as a game sound.
Json moreGet(const Editor& ed, const std::string& what, bool& known) {
    known = true;
    Json a = jarr();
    if (what == "switches") {
        for (int i = 0; i < 256; ++i) {
            const std::string name = ed.switchName(i);
            if (!name.empty()) a.items.push_back(Obj().add("switch", i + 1).add("name", name));
        }
    } else if (what == "strings") {
        for (uint32_t id = 1; id <= ed.strings().size(); ++id) {
            const std::string& text = ed.strings().get(id);
            if (!text.empty()) a.items.push_back(Obj().add("id", int(id)).add("text", text));
        }
    } else if (what == "sounds") {
        for (const std::string& path : ed.soundReferences()) a.items.push_back(Obj().add("path", path).add("kind", "game"));
        for (const ArchiveFile& f : ed.archiveFiles())
            if (lower(f.name).size() > 4 && lower(f.name).compare(lower(f.name).size() - 4, 4, ".wav") == 0)
                a.items.push_back(Obj().add("path", f.name).add("kind", "file").add("bytes", double(f.data.size())));
    } else {
        known = false;
    }
    return a;
}

// --- Editing ---------------------------------------------------------------------------------------------------------

// The spray and decorate levels: a few, some or many doodads (the GUI's Edit > Decorate: spacings of 9, 6 and 3 tiles).
int decorateSpacing(const Json& op) {
    auto tiles = [&](const char* k) {
        const double v = num(op, k);
        if (!(v >= 1 && v <= 64)) fail(std::string("\"") + k + "\" (the tiles between the doodads) must be 1-64");
        return int(v);
    };
    if (op.get("spacing")) return tiles("spacing");
    if (const Json* l = op.get("level"); l && l->type == Json::Type::Number) return tiles("level");  // (a number: the spacing)
    const std::string level = lower(op.str("level", "some"));
    if (level == "few") return 9;
    if (level == "some") return 6;
    if (level == "many") return 3;
    fail("level must be few, some or many (or give \"spacing\" in tiles)");
}

// The doodads that Decorate uses when none are given: the palette's doodads that take tiles (as the GUI's default).
std::vector<uint16_t> decorateDoodads(const Editor& ed) {
    std::vector<uint16_t> ids;
    for (const DoodadType& t : ed.doodadTypes().all())
        if (t.inPalette && t.needsTiles()) ids.push_back(t.id);
    return ids;
}

// A text field: it must be given and be text. (An empty text is a value only when it is written as "".)
std::string textOf(const Json& op, const char* k) {
    const Json& v = need(op, k);
    if (v.type != Json::Type::String) fail(std::string("\"") + k + "\" must be text");
    return v.s;
}
// A list field: it must be given and be a list.
const std::vector<Json>& arrayOf(const Json& op, const char* k) {
    const Json& v = need(op, k);
    if (v.type != Json::Type::Array) fail(std::string("\"") + k + "\" must be a list");
    return v.items;
}
// A sprite id (sprites.dat, the kinds of sprites the map can hold).
uint16_t spriteId(double v, const char* k) {
    const DatSchema* sc = DatSchema::find("sprites");
    if (v < 0 || !sc || v >= sc->entries) fail(std::string("\"") + k + "\": no sprite " + std::to_string(int(v)));
    return uint16_t(v);
}

std::vector<uint16_t> idList(const Editor& ed, const Json& op, const char* k) {
    std::vector<uint16_t> ids;
    for (const Json& v : arrayOf(op, k)) ids.push_back(unitTypeId(ed, v, k));
    return ids;
}

// The doodad ids of an op: "doodads" as ids (get doodad-types). Each must be one of the map's doodad types.
std::vector<uint16_t> doodadIds(const Editor& ed, const Json& op, const char* k) {
    std::vector<uint16_t> ids;
    for (double v : nums(need(op, k), k)) {
        if (!ed.doodadTypes().find(uint16_t(v))) fail(std::string("\"") + k + "\": no doodad type " + std::to_string(int(v)));
        ids.push_back(uint16_t(v));
    }
    return ids;
}

// Does the doodad have walkable ground of two levels (low, middle, high: the VF4 flags of its megatiles)? A ramp has;
// a cliff piece or a decoration on one level has not.
bool spansGroundLevels(const Tileset& ts, const DoodadType& t) {
    int levels = 0;
    for (int r = 0; r < t.height; ++r) {
        const size_t g = size_t(t.firstGroup) + size_t(r);
        if (g >= ts.groups.size()) continue;
        for (int c = 0; c < t.width && c < 16; ++c) {
            const int m = int(ts.groups[g].megatiles[size_t(c)]);
            if (!m || size_t(m) >= ts.minitileFlags.size()) continue;
            for (uint16_t f : ts.minitileFlags[size_t(m)])
                if (f & Tileset::kWalkable) levels |= 1 << ((f & 4) ? 2 : (f & 2) ? 1 : 0);
        }
    }
    return levels && (levels & (levels - 1));  // (more than one bit)
}

// Do walkable regions of two ground levels at the edge of `box` (tiles: x0, y0, x1, y1 not in it) reach each other
// through the regions that have ground in the box? (A ramp in the box joins them there; another ramp of the same
// cliff further away does not count.)
bool joinsLevelsIn(const PathingRegions& p, int x0, int y0, int x1, int y1) {
    std::set<int> inBox, edge;
    for (int my = y0 * 4; my < y1 * 4; ++my)
        for (int mx = x0 * 4; mx < x1 * 4; ++mx) {
            const int reg = p.walkable(mx, my) ? p.regionAt(mx, my) : -1;
            if (reg < 0 || size_t(reg) >= p.regions.size() || !p.regions[size_t(reg)].walkable()) continue;
            inBox.insert(reg);
            if (mx < x0 * 4 + 4 || my < y0 * 4 + 4 || mx >= x1 * 4 - 4 || my >= y1 * 4 - 4) edge.insert(reg);
        }
    for (int start : edge) {
        const int level = p.regions[size_t(start)].elevation();
        std::set<int> seen{start};
        std::vector<int> todo{start};
        while (!todo.empty()) {
            const int r = todo.back();
            todo.pop_back();
            if (edge.count(r) && p.regions[size_t(r)].elevation() != level) return true;
            for (int n : p.regions[size_t(r)].neighbors)
                if (inBox.count(n) && seen.insert(n).second) todo.push_back(n);
        }
    }
    return false;
}

// place_ramp: a doodad near (x, y) that fits the cliff there and joins the ground on its two sides (a ramp). The tilesets
// name ramps only as cliff pieces, so each doodad with walkable ground of two levels that fits (dddata.bin, no overlap)
// is put on a copy of the map, nearest first, and the game's regions tell if the low and the high ground beside it
// reach each other through it. Of those that join, the widest (the most walkable minitiles) within 64 px of the nearest
// is placed.
Json placeRamp(Editor& ed, const Json& op) {
    const int x = int(num(op, "x")), y = int(num(op, "y")), radius = std::clamp(int(num(op, "radius", 256)), 32, 1024);
    std::vector<const DoodadType*> types;
    if (op.get("doodads"))
        for (uint16_t id : doodadIds(ed, op, "doodads")) types.push_back(ed.doodadTypes().find(id));
    else
        for (const DoodadType& t : ed.doodadTypes().all())
            if (t.inPalette && t.needsTiles() && spansGroundLevels(ed.tileset(), t)) types.push_back(&t);
    struct Candidate {
        const DoodadType* type;
        int tx, ty, dist;
    };
    std::vector<Candidate> fits;
    const int tr = radius / 32 + 1;
    for (const DoodadType* t : types) {
        int ox, oy;
        ed.doodadOrigin(*t, x, y, ox, oy);
        for (int dy = -tr; dy <= tr; ++dy)
            for (int dx = -tr; dx <= tr; ++dx) {
                const int cx = (ox + dx) * 32 + t->width * 16, cy = (oy + dy) * 32 + t->height * 16;
                const int d = int(std::lround(std::hypot(cx - x, cy - y)));
                if (d > radius || !doodadFits(ed.chk(), *t, ox + dx, oy + dy, true) || ed.doodadOverlaps(*t, ox + dx, oy + dy)) continue;
                fits.push_back({t, ox + dx, oy + dy, d});
            }
    }
    std::stable_sort(fits.begin(), fits.end(), [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });
    const PathingRegions before = computePathingRegions(ed.terrain(), ed.tileset());
    const Candidate* best = nullptr;
    int bestWalk = -1, tried = 0, nearest = -1;
    for (const Candidate& c : fits) {
        if (nearest >= 0 && c.dist > nearest + 64) break;
        // The footprint and two tiles around it.
        const int x0 = c.tx - 2, y0 = c.ty - 2, x1 = c.tx + c.type->width + 2, y1 = c.ty + c.type->height + 2;
        if (joinsLevelsIn(before, x0, y0, x1, y1)) continue;  // (joined there already)
        if (++tried > 64) break;  // (each try computes the map's regions)
        Chk copy = ed.chk();
        if (!stardraft::placeDoodad(copy, *c.type, ed.tileset(), c.tx, c.ty, 0, true)) continue;
        const PathingRegions after = computePathingRegions(readTerrain(copy), ed.tileset());
        const bool joins = joinsLevelsIn(after, x0, y0, x1, y1);
        int walk = 0;
        for (int my = c.ty * 4; my < (c.ty + c.type->height) * 4; ++my)
            for (int mx = c.tx * 4; mx < (c.tx + c.type->width) * 4; ++mx) walk += after.walkable(mx, my);
        if (!joins) continue;
        if (nearest < 0) nearest = c.dist;
        if (walk > bestWalk) best = &c, bestWalk = walk;
    }
    if (!best)
        fail("no ramp fits within " + std::to_string(radius) + " px of (" + std::to_string(x) + ", " + std::to_string(y) +
             "): put the point on a cliff edge between high and low ground (" + std::to_string(fits.size()) + " doodads fit there, none joins the ground)");
    const int cx = best->tx * 32 + best->type->width * 16, cy = best->ty * 32 + best->type->height * 16;
    std::string why;
    const int index = int(ed.doodads().size());  // (mirror copies with symmetry on come after it)
    if (!ed.placeDoodad(best->type->id, cx, cy, true, &why)) fail("the ramp does not fit: " + why);
    return Obj().add("doodad", int(best->type->id)).add("x", cx).add("y", cy).add("index", index)
        .add("area", jrect(best->tx * 32, best->ty * 32, (best->tx + best->type->width) * 32, (best->ty + best->type->height) * 32));
}

// The places that decorate keeps clear (no doodad covers a tile of them): `keep_clear` (areas, px) and, unless
// `clear_bases` is false, the bases: 256 px around each start location, mineral field and geyser (the town hall's place
// and the mining area).
std::vector<Rect> decorateAvoid(const Editor& ed, const Json& op) {
    std::vector<Rect> areas;
    if (const Json* k = op.get("keep_clear")) {
        if (k->type != Json::Type::Array) fail("\"keep_clear\" must be a list of [left, top, right, bottom]");
        for (const Json& v : k->items) {
            const std::vector<double> e = nums(v, "keep_clear");
            if (e.size() != 4) fail("\"keep_clear\": each area is [left, top, right, bottom]");
            areas.push_back(Rect{int(e[0]), int(e[1]), int(e[2]), int(e[3])}.normalized());
        }
    }
    if (op.boolean("clear_bases", true))
        for (const Unit& u : ed.units())
            if ((u.type >= 176 && u.type <= 178) || u.type == 188 || u.type == 214)
                areas.push_back(Rect{u.x - 256, u.y - 256, u.x + 256, u.y + 256});
    return areas;
}

// place_base: the resources of a base for a town hall centered at (x, y): `minerals` mineral fields (default 8) on
// `side` (left, right, top or bottom), as near as StarCraft lets them be (3 tiles from the town hall); `geysers` (0-2,
// default 1), 3 tiles from it; with `player`, a start location at (x, y). The rules of the staredit.net Melee Mapping
// Guide (topic 378): fields at the left or the right (above, the first trips take longer; below, new workers get stuck
// on the miners: a warning), and the geyser above the town hall (beside a row: at its left), in each mirror image too:
// a geyser above or at the left mines faster than one below or at the right ("balanced gas > symmetrical gas").
// At the left or the right, the fields make the arc of the ladder maps' mains (kSideFields), the same way up in each
// mirror image, as on the ladder maps. The town hall's place and the fields take the grid (their centers are 16 px off
// it in y), the fields the three looks of the field in turn. With symmetry, each mirror image is a base of its own, its
// side turned or mirrored with it (the editor's symmetry moves each unit, so a column of fields would become a row of
// fields on each other); its start location goes to the next player that has none.
// The fields at the right of a town hall (x: away from it, y: down), from its center; the first n of them make a base
// of n fields. The first 9 are the main of most ladder maps (2017-2024: 58 of the 101 mains with 9 fields; the first 7
// are their most common natural); then the 10th of their mains of 10, then places behind.
constexpr std::pair<int, int> kSideFields[] = {{160, 160}, {192, 128}, {192, 96}, {224, 64}, {192, 32}, {224, 0}, {192, -32},
                                               {224, -64}, {192, -96}, {192, -128}, {96, 160}, {160, -160}, {256, 32},
                                               {256, -32}, {256, 96}, {256, -96}};

Json placeBase(Editor& ed, const Json& op) {
    const int x = int(std::lround(num(op, "x") / 32)) * 32, y = int(std::floor(num(op, "y") / 32)) * 32 + 16;
    const int n = std::clamp(int(num(op, "minerals", 8)), 0, int(std::size(kSideFields)));
    const int geysers = std::clamp(int(num(op, "geysers", 1)), 0, 2);
    const std::string side = lower(op.str("side", "left"));
    if (side != "left" && side != "right" && side != "top" && side != "bottom") fail("\"side\" must be left, right, top or bottom");
    // The side, and the way along it (down a column, right along a row), as directions. Each mirror image takes the
    // images of points 32 px those ways (or the other way, when that is off the map: the symmetry leaves out points off
    // the map), the image nearest it: so a mirror image's fields are the mirror images of the first base's fields (an
    // even number of fields cannot be centered on the grid).
    const int sx = side == "left" ? -1 : side == "right" ? 1 : 0, sy = side == "top" ? -1 : side == "bottom" ? 1 : 0;
    const int ax = sx ? 0 : 1, ay = sx ? 1 : 0;
    const int W = ed.width() * 32, H = ed.height() * 32;
    const auto centers = ed.symmetricPoints(x, y);
    struct Probe {
        int sign;
        std::vector<std::pair<int, int>> images;
    };
    auto probe = [&](int vx, int vy) {
        const int px = x + vx * 32, py = y + vy * 32;
        const int sign = px < 0 || py < 0 || px > W || py > H ? -1 : 1;
        return Probe{sign, ed.symmetricPoints(x + sign * vx * 32, y + sign * vy * 32)};
    };
    const Probe sides = probe(sx, sy), alongs = probe(ax, ay);
    auto toward = [](const Probe& p, int cx, int cy) {  // the axis direction from (cx, cy) to the nearest image
        std::pair<int, int> best{0, 0};
        double bestD = 160;  // (a turn or a diagonal mirror scales 32 px by up to 4 on a 4:1 map)
        for (const auto& [ix, iy] : p.images) {
            const double d = std::hypot(ix - cx, iy - cy);
            if (d > 0 && d < bestD) {
                bestD = d;
                best = std::abs(ix - cx) >= std::abs(iy - cy) ? std::pair{ix > cx ? p.sign : -p.sign, 0} : std::pair{0, iy > cy ? p.sign : -p.sign};
            }
        }
        return best;
    };
    // The fields' places along a row (in x), from the town hall's center.
    auto rowAt = [&](int i) { return int(std::floor((32 * i - 16 * (n - 1)) / 32.0 + 0.5)) * 32; };
    std::vector<bool> has(8, false);
    for (const Unit& u : ed.units())
        if (u.type == 214 && u.owner < 8) has[u.owner] = true;
    const int first = op.get("player") ? player(op, "player", 8) : -1;
    if (first >= 0 && has[size_t(first)]) fail("player " + std::to_string(first + 1) + " has a start location already");
    if (first >= 0) has[size_t(first)] = true;

    struct Restore {  // (the symmetry is off while the bases are placed one by one)
        Editor& ed;
        int mirrors, turns;
        ~Restore() { ed.setMirrors(mirrors), ed.setTurns(turns); }
    } restore{ed, ed.mirrors(), ed.turns()};
    ed.setMirrors(0), ed.setTurns(1);
    const size_t before = ed.units().size();
    Json serials = jarr();
    std::set<std::string> warnings;
    auto put = [&](uint16_t type, int owner, int px, int py) {
        const PlaceResult p = ed.placeUnit(type, uint8_t(owner), px, py, true);
        if (!p.placed) fail("a unit of the base does not fit at (" + std::to_string(px) + ", " + std::to_string(py) + ")");
        const Unit& u = ed.units()[size_t(p.index)];
        serials.items.push_back(Obj().add("serial", double(u.serial)).add("type", int(u.type)).add("x", int(u.x)).add("y", int(u.y))
            .add("player", int(u.owner) + 1));
    };
    for (size_t k = 0; k < centers.size(); ++k) {
        const int cx = int(std::lround(centers[k].first / 32.0)) * 32, cy = int(std::floor(centers[k].second / 32.0)) * 32 + 16;
        const auto [dx, dy] = k ? toward(sides, centers[k].first, centers[k].second) : std::pair{sx, sy};
        const auto [gx, gy] = k ? toward(alongs, centers[k].first, centers[k].second) : std::pair{ax, ay};
        const int along = (dx ? gy : gx) < 0 ? -1 : 1;  // (-1: this image runs the other way)
        if (!dx && !dy) {
            warnings.insert("a mirror image of the base is not placed: its side is not clear (a center on a symmetry axis)");
            continue;
        }
        if (first >= 0) {
            int owner = first;
            if (k) {
                const auto free = std::find(has.begin(), has.end(), false);
                owner = free == has.end() ? -1 : int(free - has.begin());
                if (free != has.end()) *free = true;
            }
            if (owner >= 0) put(214, owner, cx, cy);
            else warnings.insert("no start location at a mirror image: each of the 8 players has one");
        }
        if (dy) warnings.insert("mineral fields above or below a town hall mine worse than at its left or right");
        for (int i = 0; i < n; ++i) {
            const uint16_t type = uint16_t(176 + i % 3);
            if (dx) {  // the arc (kSideFields), mirrored to the left
                put(type, 11, cx + dx * kSideFields[i].first, cy + kSideFields[i].second);
            } else {  // a row in two rows, 32 px apart each way (a field is 64 px wide): the near row 96 px away
                put(type, 11, cx + along * rowAt(i), cy + dy * (160 + 32 * (i % 2)));
            }
        }
        // The geysers (128 x 64), 96 px from the town hall: the first above it (beside a row: at its left), the second
        // below it (at its right). Above or below: on the town hall's middle, as on the ladder maps.
        for (int g = 0; g < geysers; ++g) {
            const int end = g == 0 ? -1 : 1;
            if (dx) put(188, 11, cx, cy + end * 176);
            else put(188, 11, cx + end * 224, cy + dy * 16);  // (a geyser's center is on the grid)
        }
    }
    Json w = jarr();
    for (const std::string& s : warnings) w.items.push_back(jstr(s));
    return Obj().add("units", serials).add("placed", int(ed.units().size() - before)).add("warnings", w);
}

// (tools.inc)
std::vector<float> groundDistances(const PathingRegions& p, int x, int y);
double distanceAt(const std::vector<float>& dist, const PathingRegions& p, int x, int y);

// Whether a doodad covers tile (tx, ty): its tiles as the editor counts them when it checks that a doodad still
// stands (Editor::doodadsIntact: the box's corner divided toward zero). (doodadAt misses a doodad at the map's top or
// left edge, whose box starts above or left of the map.)
bool doodadOnTile(const Editor& ed, int tx, int ty) {
    for (const Doodad& d : ed.doodads())
        if (const DoodadType* t = ed.doodadTypes().find(d.id)) {
            const int x0 = (int(d.x) - t->width * 16) / 32, y0 = (int(d.y) - t->height * 16) / 32;
            if (tx >= x0 && tx < x0 + t->width && ty >= y0 && ty < y0 + t->height) return true;
        }
    return false;
}

// The ground distance from each start location to each other one and to each resource (mineral field, geyser):
// -1 where there is none. By the terrain.
std::vector<double> startDistances(const Editor& ed) {
    const PathingRegions r = computePathingRegions(ed.terrain(), ed.tileset());
    std::vector<const Unit*> s, to;
    for (const Unit& u : ed.units()) {
        if (u.type == 214 && u.owner < 8) s.push_back(&u);
        if (u.type == 214 || (u.type >= 176 && u.type <= 178) || u.type == 188) to.push_back(&u);
    }
    std::vector<double> out;
    for (const Unit* a : s) {
        const std::vector<float> d = groundDistances(r, a->x, a->y);
        for (const Unit* b : to)
            if (b != a) out.push_back(distanceAt(d, r, b->x, b->y));
    }
    return out;
}

// fix_pockets: the pockets of the pathing check (walkable ground that no unit can get to, or too small for a unit)
// become ground that is not walkable, tile by tile: each tile of a pocket takes a tile value with no walkable minitile
// there (findUnwalkableTile, as Scenario > Check pathing's "Apply the fix", which does one tile). A tile under a doodad
// stays (the new tile would take the doodad away), and so does a tile with no such value. Again, while a pass fixes
// tiles (a fix can make a new pocket): at most 4 passes.
// A pocket with a unit within 32 px stays: a unit there shows that it is on purpose (a turret's platform, a pen, a
// lone base). With "closed_passages": true, also the closed passages (walkable ground that the emulated regions do
// not join to the ground round it), only where it is safe: no unit within 32 px, the closed passages that touch one
// another 256 minitiles or less in all, and no way from a start location to another one or to a resource longer
// after it (else the op fails). The regions are emulated: a closed passage can be a real way (a walkway of split
// tiles), so it is not the default.
Json fixPockets(Editor& ed, const Json& op) {
    const bool closed = op.boolean("closed_passages", false);
    const std::vector<double> before = closed ? startDistances(ed) : std::vector<double>{};
    int changed = 0, skipped = 0, left = 0;
    for (int pass = 0; pass < 4; ++pass) {
        const PathingRegions regions = computePathingRegions(ed.terrain(), ed.tileset());
        const PathingCheck c = checkPathing(regions, ed.terrain(), ed.tileset(), ed.units());
        const int w = regions.walkW();
        auto nearUnit = [&](const std::vector<int>& cells) {  // a unit within 32 px of the cells
            for (const Unit& u : ed.units())
                for (int cell : cells)
                    if (std::abs((cell % w) * 8 + 4 - u.x) <= 32 && std::abs((cell / w) * 8 + 4 - u.y) <= 32) return true;
            return false;
        };
        std::map<std::pair<int, int>, std::vector<int>> byTile;  // (tile x, y) -> the minitiles to fix in it
        for (const PathingProblem& p : c.problems)
            if (p.kind == PathingProblemKind::Pocket && !nearUnit(p.cells))
                for (int cell : p.cells) byTile[{cell % w / 4, cell / w / 4}].push_back(cell);
        if (closed) {
            // The closed passages' cells; those that touch one another are one group, kept whole or left whole.
            std::set<int> all;
            for (const PathingProblem& p : c.problems)
                if (p.kind == PathingProblemKind::ClosedPassage) all.insert(p.cells.begin(), p.cells.end());
            std::set<int> seen;
            for (int start : all) {
                if (seen.count(start)) continue;
                std::vector<int> group{start}, todo{start};
                seen.insert(start);
                while (!todo.empty()) {
                    const int at = todo.back();
                    todo.pop_back();
                    for (int m : {at - 1, at + 1, at - w, at + w})
                        if (all.count(m) && !seen.count(m)) seen.insert(m), group.push_back(m), todo.push_back(m);
                }
                if (group.size() > 256 || nearUnit(group)) continue;
                for (int cell : group) byTile[{cell % w / 4, cell / w / 4}].push_back(cell);
            }
        }
        left = c.pockets + (closed ? c.closed : 0);
        std::vector<Editor::TileAt> tiles;
        skipped = 0;
        for (const auto& [t, cells] : byTile) {
            uint16_t v = 0;
            if (!doodadOnTile(ed, t.first, t.second) && findUnwalkableTile(ed.terrain(), ed.tileset(), t.first, t.second, cells, v))
                tiles.push_back({t.first, t.second, v});
            else
                ++skipped;
        }
        if (tiles.empty()) break;
        changed += ed.placeTileValues(tiles);
        const PathingCheck after = checkPathing(computePathingRegions(ed.terrain(), ed.tileset()), ed.terrain(), ed.tileset(), ed.units());
        left = after.pockets + (closed ? after.closed : 0);
        if (!left) break;
    }
    if (closed) {
        const std::vector<double> now = startDistances(ed);
        for (size_t i = 0; i < before.size() && i < now.size(); ++i)
            if (before[i] >= 0 && (now[i] < 0 || now[i] > before[i] + 1))
                fail("fix_pockets: fixing the closed passages makes a way from a start location longer or closes it; "
                     "fix them by hand (Scenario > Check pathing), or leave out \"closed_passages\"");
    }
    return Obj().add("changed", changed).add("left", left).add("tiles_left", skipped)
        .add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
}

std::optional<Json> applyMoreOp(Editor& ed, const std::string& name, const Json& op) {
    Obj r;
    if (name == "place_ramp") return placeRamp(ed, op);
    if (name == "fix_pockets") return fixPockets(ed, op);
    if (name == "place_base") return placeBase(ed, op);
    if (name == "set_switch_name") {
        const int s = int(num(op, "switch"));
        if (s < 1 || s > 256) fail("\"switch\" must be 1-256");
        return r.add("changed", ed.setSwitchName(s - 1, textOf(op, "name")));
    }
    if (name == "set_string") {
        const uint32_t id = uint32_t(num(op, "string"));
        if (id < 1 || id > ed.strings().size()) fail("no map string " + std::to_string(id) + " (see `get strings`)");
        return r.add("changed", ed.setMapString(id, textOf(op, "text")));
    }
    if (name == "add_sound") {
        const std::string path = textOf(op, "path");
        if (path.empty()) fail("missing \"path\"");
        const size_t before = ed.soundReferences().size();
        ed.addSoundReference(path);
        return r.add("added", ed.soundReferences().size() != before);  // (a file of the map is not added: it is there already)
    }
    if (name == "remove_sound") {
        const std::string path = textOf(op, "path");
        // A file of the map is not a game sound: its WAV entry goes with the file (add-file --remove).
        if (ed.archiveFile(path)) fail("\"" + path + "\" is a file of the map: take it out with add-file --remove");
        return r.add("removed", ed.removeSoundReference(path));
    }
    if (name == "decorate") {
        // Doodads where they fit the terrain, over the area (default: the map), with the given doodads (default: the palette's).
        const std::vector<uint16_t> ids = op.get("doodads") ? doodadIds(ed, op, "doodads") : decorateDoodads(ed);
        const Rect a = op.get("area") ? area(op, "area").normalized() : Rect{0, 0, ed.width() * 32, ed.height() * 32};
        const int n = ed.decorate(ids, a, decorateSpacing(op), uint32_t(num(op, "seed", 1)), decorateAvoid(ed, op));
        return r.add("placed", n).add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "spray_units") {
        const std::vector<uint16_t> types = idList(ed, op, "types");
        const int n = ed.sprayUnits(types, uint8_t(player(op, "player", 12)), int(num(op, "x")), int(num(op, "y")), int(num(op, "radius")),
                                    int(num(op, "count")), uint32_t(num(op, "seed", 1)));
        return r.add("placed", n);
    }
    if (name == "spray_doodads") {
        const int n = ed.sprayDoodads(doodadIds(ed, op, "doodads"), int(num(op, "x")), int(num(op, "y")), int(num(op, "radius")), int(num(op, "count")),
                                      uint32_t(num(op, "seed", 1)), op.boolean("check_terrain", true));
        return r.add("placed", n);
    }
    if (name == "spray_sprites") {
        std::vector<uint16_t> ids;
        for (double v : nums(need(op, "sprites"), "sprites")) ids.push_back(spriteId(v, "sprites"));
        const int n = ed.spraySprites(ids, uint8_t(player(op, "player", 12)), float(num(op, "x")), float(num(op, "y")), int(num(op, "radius")),
                                      int(num(op, "count")), uint32_t(num(op, "seed", 1)), int(num(op, "grid", 0)));
        return r.add("placed", n);
    }
    if (name == "spray_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const uint16_t t = terrainId(ed, need(op, "terrain"), "terrain");
        ed.sprayTerrain(int(num(op, "x")), int(num(op, "y")), t, brush(op), int(num(op, "dabs", 1)), uint32_t(num(op, "seed", 1)));
        return r.add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "reroll_terrain") {
        if (!ed.canPaintTerrain()) fail("the terrain of this map cannot be painted (it has no valid ISOM)");
        const int n = ed.rerollTerrain(int(num(op, "x")), int(num(op, "y")), brush(op), uint32_t(num(op, "seed", 1)));
        return r.add("changed", n).add("units_removed", ed.takeUnitsRemovedByTerrain()).add("doodads_removed", ed.takeDoodadsRemovedByTerrain());
    }
    if (name == "replace_doodads") {
        const std::vector<int> d = indices(op, "doodads", ed.doodads().size());
        const uint16_t to = uint16_t(num(op, "to"));
        if (!ed.doodadTypes().find(to)) fail("no doodad type " + std::to_string(int(to)));
        return r.add("replaced", ed.replaceDoodads(d, to, op.boolean("check_terrain", true)));
    }
    if (name == "replace_sprites") {
        const std::vector<int> s = indices(op, "sprites", ed.sprites().size());
        return r.add("replaced", ed.replaceSprites(s, spriteId(num(op, "to"), "to")));
    }
    if (name == "place_map_revealers") {
        std::vector<uint8_t> owners;
        for (double p : nums(need(op, "players"), "players")) {
            if (p < 1 || p > 8) fail("\"players\" must be 1-8");
            owners.push_back(uint8_t(int(p) - 1));
        }
        return r.add("placed", ed.placeMapRevealers(owners, int(num(op, "spacing", 8))));
    }
    if (name == "remove_map_revealers") return r.add("removed", ed.removeMapRevealers());
    if (name == "place_start_locations") return r.add("placed", ed.placeStartLocations());
    // Copy, cut, paste and duplicate. The clipboard is the editor's own, kept for the rest of this apply: a copy op
    // and the paste op that follows it in the same list use the same copy. Units: copy_units (units), cut_units (units:
    // copied, then deleted), paste_units (x, y: the group's center; player: the owner of the pasted units; correct).
    // duplicate_units: a copy pasted at (x, y). Terrain: copy_terrain (area, shape, units, doodads: the clipboard's
    // ground and the units and doodads in it), paste_terrain (x, y, correct).
    if (name == "copy_units" || name == "cut_units") {
        const std::vector<int> u = unitIndices(ed, op);
        if (u.empty()) fail("no units to copy");
        selectUnits(ed, u);
        ed.copySelection();
        if (name == "cut_units") {
            const int n = int(ed.selection().size());
            ed.deleteSelection();
            return r.add("cut", n);
        }
        return r.add("copied", int(u.size()));
    }
    if (name == "paste_units" || name == "duplicate_units") {
        if (name == "duplicate_units" && op.get("units")) {  // (the units given are copied first)
            selectUnits(ed, unitIndices(ed, op));
            ed.copySelection();
        }
        if (!ed.canPaste()) fail("nothing is copied: copy_units or cut_units first");
        if (op.get("player")) ed.setClipboardOwner(uint8_t(player(op, "player", 12)));
        const int n = ed.paste(int(num(op, "x")), int(num(op, "y")), op.boolean("correct", true));
        return r.add("pasted", n);
    }
    if (name == "copy_terrain") {
        const Rect a = area(op, "area").normalized();
        const bool ok = ed.copyTerrain(a, op.boolean("units", false), op.boolean("doodads", false), selectShape(op));
        return r.add("copied", ok);
    }
    if (name == "paste_terrain") {
        if (!ed.hasTerrainClipboard()) fail("no terrain is copied: copy_terrain first");
        return r.add("pasted", ed.pasteTerrain(int(num(op, "x")), int(num(op, "y")), op.boolean("correct", true)));
    }
    if (name == "copy_players") {  // the Players page's copy: the players and forces of another map (one undo step)
        const std::string from = textOf(op, "from");
        Editor other(ed.gameData());
        std::string err;
        if (!other.open(upath(from), err)) fail(err);
        std::vector<Editor::Player> players;
        std::vector<Editor::Force> forces;
        for (int p = 0; p < 8; ++p) players.push_back(other.player(p));
        for (int f = 0; f < 4; ++f) forces.push_back(other.force(f));
        ed.setPlayersAndForces(players, forces);
        return r.add("players", 8).add("forces", 4);
    }
    if (name == "link_units") {
        const std::vector<int> u = unitIndices(ed, op);
        if (u.size() != 2) fail("link_units needs two units");
        if (!ed.linkUnits(u[0], u[1])) fail("these units cannot be linked (they are linked already, or not an add-on pair)");
        return r;
    }
    return std::nullopt;
}
