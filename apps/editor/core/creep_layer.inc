// View > Layers > Creep (core.inc): the creep that the map's Zerg buildings have at the start of a game (stardraft/
// creep.h: the creep makers' areas, the footprints of the buildings that require creep, the map's own creep tiles),
// drawn as the game draws it: each creep tile is a tile of the tileset's creep group, its variant drawn once a
// session. Made again only when the terrain or a building changes (not a selection); `creepRevision` changes when
// the creep does.

struct CreepLayer {
    const void* editor = nullptr;
    uint64_t terrain = ~0ull, buildings = 0;  // (the terrain's revision, the buildings' key)
    std::vector<uint8_t> creep, variants;
    uint64_t changes = 0;  // (the creep changed: the renderer's terrain again)
    uint32_t seed = 0;
} gCreep;

// The map's start creep (1: creep), one per tile; made again when the map changed. While the selected units are
// dragged (Units mode), it is made with them where the drag puts them (snapped as they will be dropped), so a
// Hatchery's creep goes with it.
const std::vector<uint8_t>& mapCreep(const Editor& ed) {
    CreepLayer& c = gCreep;
    const AppState& app = appState();
    std::vector<Unit> dragged;
    if (app.ed.get() == &ed && app.drag == Drag::MoveUnits && app.dragMoved) {
        float wx, wy, fx, fy;
        toWorld(app.mouseX, app.mouseY, wx, wy);
        dragMoveDelta(wx, wy, fx, fy);
        const int dx = int(std::lround(fx)), dy = int(std::lround(fy));
        if (dx || dy) {
            dragged = ed.units();
            for (int i : ed.selection())
                if (i >= 0 && size_t(i) < dragged.size()) {
                    Unit& u = dragged[size_t(i)];
                    u.x = uint16_t(std::clamp(int(u.x) + dx, 0, ed.width() * 32 - 1));
                    u.y = uint16_t(std::clamp(int(u.y) + dy, 0, ed.height() * 32 - 1));
                    if (u.type < ed.unitTypes().size()) snapToGrid(ed.unitTypes()[u.type], u.x, u.y);
                }
        }
    }
    const std::vector<Unit>& units = dragged.empty() ? ed.units() : dragged;
    uint64_t key = 1469598103934665603ull;  // (the buildings' types and places: FNV-1a)
    for (const Unit& u : units)
        if (u.type < ed.unitTypes().size() && ed.unitTypes()[u.type].is(UnitFlag::Building))
            for (uint64_t v : {uint64_t(u.type), uint64_t(u.x), uint64_t(u.y)}) key = (key ^ v) * 1099511628211ull;
    if (c.editor == &ed && c.terrain == ed.terrainRevision() && c.buildings == key) return c.creep;
    c.editor = &ed;
    c.terrain = ed.terrainRevision();
    c.buildings = key;
    const TerrainLayer& terrain = ed.terrain();
    std::vector<uint8_t> creep = startCreep(ed.tileset(), terrain, units, ed.unitTypes());
    const size_t n = creep.size();
    if (c.variants.size() != n) {  // (the variants: once a session, for a map of this size)
        if (!c.seed) c.seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
        c.variants.resize(n);
        for (uint8_t& v : c.variants) v = uint8_t(creepVariant(c.seed));
    }
    if (creep != c.creep) {
        c.creep = std::move(creep);
        ++c.changes;
    }
    return c.creep;
}

// A live game (Tools > Simulate) shows its own creep in place of the start creep: its creep per tile and a count of its
// changes (null: no game; sdl_simulation.inc).
const std::vector<uint8_t>* (*gLiveCreep)(uint64_t& changes) = nullptr;

// The number of creep changes: the renderer's terrain is uploaded again when it differs.
uint64_t creepRevision(const Editor& ed) {
    if (!gLayers.creep || !ed.isOpen()) return 0;
    uint64_t live = 0;
    if (gLiveCreep && gLiveCreep(live)) return (uint64_t(1) << 62) + live;  // (the game's creep: its own count)
    mapCreep(ed);
    return gCreep.changes;
}

// The terrain with the start creep's tiles (View > Layers > Creep on, and the terrain shows).
void addCreepTiles(const Editor& ed, TerrainLayer& terrain) {
    if (!gLayers.creep || !gLayers.terrain || !ed.isOpen()) return;
    uint64_t live = 0;
    const std::vector<uint8_t>* game = gLiveCreep ? gLiveCreep(live) : nullptr;
    const std::vector<uint8_t>& creep = game ? *game : mapCreep(ed);
    if (gCreep.variants.size() != creep.size()) mapCreep(ed);  // (the variants, once a session)
    if (creep.size() == terrain.tiles.size() && gCreep.variants.size() == creep.size()) applyCreep(terrain, creep, gCreep.variants);
}

// The number of creep tiles in the terrain the map shows (the snapshot tests' check).
int creepTilesShown(const Editor& ed) {
    if (!gLayers.creep || !gLayers.terrain) return 0;
    int n = 0;
    for (uint8_t c : mapCreep(ed)) n += c;
    return n;
}

// View > Layers > Creep.
void toggleCreep() {
    gLayers.creep = !gLayers.creep;
    setMessage(gLayers.creep ? std::wstring(tr(L"Creep shows"))
                             : std::wstring(tr(L"Creep is hidden (View > Layers shows it again)")));
    shellMenusChanged();
    shellState().dirty = true;
}

// --check creep (the snapshot test): a Hatchery of player 1 near the map's middle, then the creep tiles in the terrain
// that the map view draws (terrainWithLayers). False: not this check.
TerrainLayer terrainWithLayers(const Editor& ed);
bool creepCheck(const std::string& what, std::string& out) {
    if (what != "creep") return false;
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return out = "creep: no map open (WRONG)\n", true;
    Editor& ed = *app.ed;
    const int x = ed.width() * 16, y = ed.height() * 16;
    const PlaceResult placed = ed.placeUnit(131, 0, x, y);
    const TerrainLayer shown = terrainWithLayers(ed);
    int tiles = 0;
    for (uint16_t t : shown.tiles) tiles += (t >> 4) == kCreepGroup;
    const int made = creepTilesShown(ed);
    out = "creep: a Hatchery " + std::string(placed.placed ? "placed" : "not placed (WRONG)") + " at (" +
          std::to_string(placed.unit.x) + ", " + std::to_string(placed.unit.y) + "); " + std::to_string(tiles) +
          " creep tiles in the terrain the map shows, " + std::to_string(made) + " from the creep rules" +
          (tiles >= made && made >= 12 ? " (yes)" : " (WRONG)") + "\n";
    return true;
}
