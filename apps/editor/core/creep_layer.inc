// View > Layers > Creep (core.inc): the creep that the map's Zerg buildings have at the start of a game (stardraft/
// creep.h: the creep makers' areas, the footprints of the buildings that require creep, the map's own creep tiles),
// drawn as the game draws it: each creep tile is a tile of the tileset's creep group, its variant drawn once a
// session. Made again only when the map changes (Editor::revision); `creepRevision` changes when the creep does.

struct CreepLayer {
    const void* editor = nullptr;
    uint64_t revision = ~0ull;
    std::vector<uint8_t> creep, variants;
    uint64_t changes = 0;  // (the creep changed: the renderer's terrain again)
    uint32_t seed = 0;
} gCreep;

// The map's start creep (1: creep), one per tile; made again when the map changed.
const std::vector<uint8_t>& mapCreep(const Editor& ed) {
    CreepLayer& c = gCreep;
    if (c.editor == &ed && c.revision == ed.revision()) return c.creep;
    c.editor = &ed;
    c.revision = ed.revision();
    const TerrainLayer& terrain = ed.terrain();
    std::vector<uint8_t> creep = startCreep(ed.tileset(), terrain, ed.units(), ed.unitTypes());
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

// The number of creep changes: the renderer's terrain is uploaded again when it differs.
uint64_t creepRevision(const Editor& ed) {
    if (!gLayers.creep || !ed.isOpen()) return 0;
    mapCreep(ed);
    return gCreep.changes;
}

// The terrain with the start creep's tiles (View > Layers > Creep on, and the terrain shows).
void addCreepTiles(const Editor& ed, TerrainLayer& terrain) {
    if (!gLayers.creep || !gLayers.terrain || !ed.isOpen()) return;
    const std::vector<uint8_t>& creep = mapCreep(ed);
    if (creep.size() == terrain.tiles.size()) applyCreep(terrain, creep, gCreep.variants);
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
