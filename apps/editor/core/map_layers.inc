// Map layers, a prototype (stardraft/layers.h; the panel: views_layers.inc). The active layer takes what the map's tools
// do: after each edit (the frame's sync), the ISOM corners, placed tiles and objects that changed go into it, and the map
// is flattened again, with the terrain fitted to the layers. The map's sections always are the flattened result. Erase
// on a layer takes corners out of it (NULL: see-through); the Layers tool moves contents. Each change is one undo step
// with the layers' files.

// The layers' terrain outside of the map (mapLayersOutside): its box in tiles (x0, y0 can be negative), its tiles
// (0xFFFF: none there), and a number that changes when it changes.
struct LayersOutside {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<uint16_t> tiles;
    bool any = false;
    uint64_t gen = 0;
};
const LayersOutside& mapLayersOutside(const Editor& ed);
// The layers that show see-through (Map layers' Opacity under 100 %, in the view only): the map's terrain without them
// (the view draws it, then the map's own terrain over it at each one's opacity, on its tiles: Renderer::setTerrainOverlay),
// those tiles as rects of the map (x1, y1 past them) with their opacity, and a number that changes when they change.
struct LayersTranslucency {
    bool any = false;
    TerrainLayer without;
    struct Rect {
        int x0, y0, x1, y1;
        float alpha;
        size_t layer;  // (its index in the stack: its opacity)
    };
    std::vector<Rect> rects;
    uint64_t gen = 0;
};
const LayersTranslucency& mapLayersTranslucency(const Editor& ed);
float mapLayersUnitAlpha(const Editor& ed, size_t unit);  // a map unit's opacity: its layer's (1: none)
int mapLayersOpacity(int row);  // % (100: not see-through); the Background: 100
void setMapLayersOpacity(int row, int percent);
bool& mapLayersShowOutside();
void setMapLayersShowOutside(bool show);
constexpr int kOutsideReach = 256;  // (tiles: how far outside of the map a layer's terrain shows; the grid stays small)

struct MapLayersState {
    const Editor* ed = nullptr;
    uint64_t loadMark = 0;  // Editor::loadMark when the map was last taken as it is
    bool filesPending = false;  // the layers' files are still to be read
    bool liveDirty = false;     // a stroke's flattening changed the layers: its files go with the stroke's undo step at its end
    bool on = false;
    LayerStack stack;
    Bytes base;
    int active = -1;       // -1: the Background; else the index in stack.layers
    bool staleCheck = false;  // the map is new here: compare its terrain with the last save's
    // A change of the map's size (mapLayersBeforeResize): the Background and the layers as they are after it.
    struct Resized {
        bool on = false;
        int w = 0, h = 0;
        Bytes base;
        LayerStack stack;
    } resized;
    const Editor* selEd = nullptr;
    uint32_t selLayer = 0;  // the layer that the terrain selection is of (0: the Background)
    std::set<int> picked;  // the rows selected (-1: the Background), with the active one
    std::map<fs::path, int> activeOf;  // the active row of each map, while the program runs
    uint64_t revision = 0;
    size_t undoSteps = 0;
    int width = 0, height = 0, era = 0;
    std::vector<uint16_t> corners;  // the flattened map's corners after the last sync
    std::vector<uint16_t> tiles;    // and its tiles
    std::vector<uint16_t> tileSection;  // and those under the doodads
    LayerObjectLists objects;
    LayerOwners owners;
    // A drag on the map.
    Editor* dragEd = nullptr;
    bool erasing = false, moving = false, dragChanged = false;
    int startX = 0, startY = 0, moveDX = 0, moveDY = 0, nowX = 0, nowY = 0;
    std::map<int, MapLayer> moveFrom;
    std::optional<LayerStack> dragStart;  // the stack before the drag (Esc)
    std::map<uint32_t, int> opacity;  // a layer's opacity in the view, %, by its id (not saved: a view setting of this run)
} gMapLayers;

void layersPanelChanged();    // (views_layers.inc)

static void layerCornerGrid(const Editor& ed, std::vector<uint16_t>& out) {
    out.clear();
    const IsomGrid& g = ed.isomGrid();
    for (int y = -1; y <= int(ed.height()) + 1; ++y)
        for (int x = -1; x <= int(ed.width()) / 2 + 1; ++x) out.push_back(g.terrainAt(x, y));
}

static LayerObjectLists layerObjectsOf(const Editor& ed) { return {ed.units(), ed.layerDoodads(), ed.layerSprites()}; }

// The map as it is now, to compare the next edit with.
static void layersRememberMap(Editor& ed) {
    MapLayersState& s = gMapLayers;
    layerCornerGrid(ed, s.corners);
    s.tiles = ed.terrain().tiles;
    s.tileSection = ed.layerTileSection().tiles;
    s.objects = layerObjectsOf(ed);
    s.revision = ed.revision();
    s.undoSteps = ed.undoSteps();
    s.width = ed.width(), s.height = ed.height(), s.era = ed.era();
}
static void layersRemember(Editor& ed) {
    layersRememberMap(ed);
    gMapLayers.owners = gMapLayers.stack.owners();
}

static void layersRead(Editor& ed) {
    MapLayersState& s = gMapLayers;
    const ArchiveFile* file = ed.archiveFile(kLayersFile);
    const ArchiveFile* base = ed.archiveFile(kLayersBaseFile);
    s.on = file && base && LayerStack::parse(file->data, s.stack);
    // (another editor saved the map after this program did: it keeps our files, not their meaning)
    if (const ArchiveFile* check = s.on && s.staleCheck && !ed.modified() ? ed.archiveFile(kLayersCheckFile) : nullptr)
        if (const std::string now = ed.terrainCheck(); std::string(check->data.begin(), check->data.end()) != now)
            setMessage(L"Another program changed this map's terrain after its last save here. The layers do not have those changes: the next edit makes the terrain from the layers again.");
    s.staleCheck = false;
    ed.noteAskedCorners(s.on);
    if (!s.on) {
        s.stack = {}, s.base.clear(), s.active = -1, s.picked.clear();
        return;
    }
    s.base = base->data;
    if (s.active >= int(s.stack.layers.size())) s.active = int(s.stack.layers.size()) - 1;
    std::erase_if(s.picked, [&](int i) { return i >= int(s.stack.layers.size()); });
    s.owners = s.stack.owners();
    std::set<std::pair<int, int>> placed;
    for (const MapLayer& l : s.stack.layers)
        for (const auto& [at, tile] : l.tiles) placed.insert(at);
    ed.noteLayerPlaced(std::move(placed));
    layersPanelChanged();
}

// The map from the Background and the layers.
static void layersCompose(Editor& ed) {
    MapLayersState& s = gMapLayers;
    syncLinkedLayers(s.stack);  // (the linked duplicates: their originals' contents, at their places)
    ed.composeLayers(s.base, s.stack, nullptr, false, s.ed == &ed ? &s.tileSection : nullptr, s.active);  // (the tiles as they were before this edit)
    shellState().terrainRev = 0;
    shellState().dirty = true;
}

static std::vector<ArchiveFile> layersFiles(bool withBase) {
    std::vector<ArchiveFile> files{{kLayersFile, gMapLayers.stack.serialize()}};
    if (withBase) files.push_back({kLayersBaseFile, gMapLayers.base});
    return files;
}

void mapLayersSync();

// The object lists change their order when the map is flattened: the selections go, not to point at other objects.
static void layersKeepSelections(Editor& ed, const LayerObjectLists& before) {
    AppState& app = shellState();
    const LayerObjectLists now = layerObjectsOf(ed);
    const auto sameSerials = [&] {
        if (now.units.size() != before.units.size()) return false;
        for (size_t i = 0; i < now.units.size(); ++i)
            if (now.units[i].serial != before.units[i].serial) return false;
        return true;
    };
    if (!sameSerials()) ed.clearSelection();
    const auto sameDoodads = [&] {
        if (now.doodads.size() != before.doodads.size()) return false;
        for (size_t i = 0; i < now.doodads.size(); ++i)
            if (now.doodads[i].x != before.doodads[i].x || now.doodads[i].y != before.doodads[i].y || now.doodads[i].id != before.doodads[i].id) return false;
        return true;
    };
    if (!sameDoodads()) app.selDoodads.clear();
    const auto sameSprites = [&] {
        if (now.sprites.size() != before.sprites.size()) return false;
        for (size_t i = 0; i < now.sprites.size(); ++i)
            if (now.sprites[i].x != before.sprites[i].x || now.sprites[i].y != before.sprites[i].y || now.sprites[i].id != before.sprites[i].id) return false;
        return true;
    };
    if (!sameSprites()) app.selSprites.clear();
}

// An edit of the stack (a button of the panel): one undo step. `edit` false: nothing changed.
static void layersEdit(const std::function<bool(Editor&)>& edit) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen() || app.ed->inStroke()) return;
    Editor& ed = *app.ed;
    mapLayersSync();  // (the last edit is in its layer first)
    if (gMapLayers.ed != &ed || gMapLayers.filesPending) return setMessage(L"The map's files are still being read");
    ed.beginLayerEdit();
    const LayerObjectLists before = layerObjectsOf(ed);
    const bool changed = edit(ed);
    if (changed) layersCompose(ed), layersKeepSelections(ed, before);
    ed.endLayerEdit(changed, changed ? layersFiles(true) : std::vector<ArchiveFile>{});
    if (changed) layersRemember(ed);
    ed.noteAskedCorners(gMapLayers.on);
    layersPanelChanged();
    shellMenusChanged();
    app.dirty = true;
}

// The map's size or tileset changed (Map properties, Resize): the layers cannot follow yet, so they flatten as they show.
// Undo brings them back.
static void layersFlattenForResize(Editor& ed) {
    MapLayersState& s = gMapLayers;
    // A new size: the layers stay, moved as the map's content moved (the resize's own undo step takes the files).
    if (s.resized.on && s.resized.w == ed.width() && s.resized.h == ed.height() && ed.era() == s.era) {
        s.resized.on = false;
        s.base = std::move(s.resized.base), s.stack = std::move(s.resized.stack);
        syncLinkedLayers(s.stack);
        ed.composeLayers(s.base, s.stack);  // (no tiles to keep: the last ones are of the old places)
        shellState().terrainRev = 0;
        shellState().dirty = true;
        ed.amendLastStep(layersFiles(true));
        layersRemember(ed);
        layersPanelChanged();
        return;
    }
    s.resized.on = false;
    ed.amendLastStep({}, {kLayersFile, kLayersBaseFile});
    s.active = -1;
    layersRead(ed);
    setMessage(L"The map's size or tileset changed: the layers are flattened. Undo brings them back.");
}

// What an edit on a linked duplicate says: edit its original, or unlink it.
static std::wstring linkedRefusal(const MapLayer& l) {
    std::wstring original;
    for (const MapLayer& o : gMapLayers.stack.layers)
        if (o.id == l.linkedTo) original = L"\"" + widen(o.name) + L"\"";
    return std::wstring(tr(L"A linked duplicate follows its original: edit ")) + (original.empty() ? std::wstring(tr(L"the original")) : original) +
           tr(L", or Unlink this layer (right-click it in Map layers)");
}

// The contents of the layers that are locked, to compare them.
static Bytes layerLockedBytes(const LayerStack& stack) {
    LayerStack only;
    if (stack.background.locked) only.background = stack.background;
    for (const MapLayer& l : stack.layers)
        if (l.locked || l.linkedTo) only.layers.push_back(l);  // (a linked duplicate follows its original only)
    // (each object with its place in it, in one order: a move and a capture leave them differently)
    const auto even = [](auto& list, auto key) {
        for (auto& o : list) o.item.x = uint16_t(o.x), o.item.y = uint16_t(o.y), o.shown = false;
        std::sort(list.begin(), list.end(), [&](const auto& a, const auto& b) { return key(a) < key(b); });
    };
    const auto evenLayer = [&](MapLayer& l) {
        even(l.units, [](const auto& o) { return std::tuple(o.item.serial, o.x, o.y); });
        even(l.doodads, [](const auto& o) { return std::tuple(o.item.id, o.x, o.y); });
        even(l.sprites, [](const auto& o) { return std::tuple(o.item.id, o.x, o.y); });
    };
    evenLayer(only.background);
    for (MapLayer& l : only.layers) evenLayer(l);
    return only.serialize();
}

// These tiles of the layer become NULL: its placed tiles there, and the corners that are mostly in them. (A corner is
// between four rects, each a tile row of two tiles wide.)
static bool layerEraseTiles(MapLayer& layer, const std::vector<std::pair<int, int>>& tiles) {
    bool any = false;
    std::set<std::pair<int, int>> in(tiles.begin(), tiles.end());
    std::map<std::pair<int, int>, int> rectsOfCorner;
    std::set<std::pair<int, int>> rects;
    for (const auto& [tx, ty] : tiles) {
        any |= layer.tiles.erase({tx, ty}) > 0;
        rects.insert({tx / 2, ty});
    }
    for (const auto& [rx, ry] : rects)
        if (in.count({rx * 2, ry}) && in.count({rx * 2 + 1, ry}))
            for (const auto& corner : {std::pair{rx, ry}, {rx + 1, ry}, {rx, ry + 1}, {rx + 1, ry + 1}}) ++rectsOfCorner[corner];
    // (one rect: no corner is between two of its rects)
    const int least = rects.size() == 1 ? 1 : 2;
    for (const auto& [corner, n] : rectsOfCorner)
        if (n >= least) any |= layer.corners.erase(corner) > 0;
    return any;
}

static void layersCapture(Editor& ed, bool live);

// Terrain's Move follows the selection: its button shows if it can be used, and with no selection the tool is Select.
static void terrainMoveFollowsSelection(AppState& app) {
    void shellToolsChanged();  // (the shell)
    static bool had = false;
    const bool lost = !app.hasTerrainSel && app.toolByMode[int(Mode::Terrain)] == TMoveSel && app.drag != Drag::TerrainMove;
    if (lost) app.toolByMode[int(Mode::Terrain)] = TSelect, fillToolCombo(app);
    if (lost || had != app.hasTerrainSel) had = app.hasTerrainSel, shellToolsChanged();
}

// What the last edit changed goes into the active layer.
void mapLayersSync() {
    AppState& app = shellState();
    MapLayersState& s = gMapLayers;
    terrainMoveFollowsSelection(app);
    if (!app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    if (s.ed != &ed || ed.loadMark() != s.loadMark) {  // another map, or an undo or a redo: the map is as the step left it
        if (s.ed != &ed) {
            s.staleCheck = true;
            s.opacity.clear();  // (of the other map's layers: their ids start at 1 too)
            s.resized.on = false;
            s.active = -1, s.picked.clear();
            if (const auto it = s.activeOf.find(ed.path()); it != s.activeOf.end()) s.active = it->second, s.picked = {s.active};
        }
        if (s.ed == &ed && s.on) setTerrainSelTiles({});  // (an undo or a redo: the selection may be where nothing is now)
        s.ed = &ed, s.loadMark = ed.loadMark(), s.liveDirty = false;
        const bool was = s.on;
        s.on = false, s.stack = {}, s.filesPending = true;
        ed.noteAskedCorners(false);
        if (was) layersPanelChanged();
        if (!ed.filesRead() || ed.archiveFile(kLayersFile)) layersRememberMap(ed);  // (what is painted while the files are read is an edit)
    }
    if (s.filesPending) {
        if (!ed.filesRead()) return;
        s.filesPending = false;
        layersRead(ed);
    }
    // Another layer: the selection goes, as it was of the last one. (The copy stays.)
    if (const uint32_t layer = s.on && s.active >= 0 && s.active < int(s.stack.layers.size()) ? s.stack.layers[size_t(s.active)].id : 0; s.selEd != &ed || s.selLayer != layer) {
        if (s.selEd == &ed && app.hasTerrainSel) setTerrainSelTiles({}), app.dirty = true;
        s.selEd = &ed, s.selLayer = layer;
    }
    if (!s.on || ed.inStroke()) return;  // (no layers: nothing to compare)
    if (ed.revision() == s.revision) {
        if (s.liveDirty) s.liveDirty = false, ed.amendLastStep(layersFiles(true)), layersRemember(ed);  // (a stroke that ended as the last flattening left it)
        return;
    }
    // (a new size or tileset; or Crop or extend with the same size and another offset)
    if (ed.width() != s.width || ed.height() != s.height || ed.era() != s.era || s.resized.on) return layersFlattenForResize(ed);
    layersCapture(ed, false);
}

// An edit that belongs to the Background, whatever layer is active (the start locations that a save places: the game
// needs them, they are not a layer's work). The edits before it go to the active layer first.
void mapLayersOnBackground(const std::function<void()>& edit) {
    MapLayersState& s = gMapLayers;
    AppState& app = shellState();
    mapLayersSync();
    if (!s.on || !app.ed || s.ed != app.ed.get() || s.active < 0 || s.stack.background.locked || !s.stack.background.visible) return edit();
    const int was = s.active;
    const uint32_t selLayer = s.selLayer;  // (not another layer for the selection: it stays)
    s.active = -1, s.selLayer = 0;
    edit();
    mapLayersSync();
    s.active = was, s.selLayer = selLayer;
}

// While a terrain stroke goes on: the other layers stay as they are on the map, not only at its end.
void mapLayersLiveStroke(AppState& app) {
    MapLayersState& s = gMapLayers;
    if (!s.on || s.filesPending || !app.ed || s.ed != app.ed.get() || !app.ed->inStroke() || s.erasing || s.moving || app.mode != Mode::Terrain) return;
    if (app.drag != Drag::Paint && app.drag != Drag::Spray && app.drag != Drag::Jumble) return;
    Editor& ed = *app.ed;
    if (ed.revision() == s.revision) return;
    if (s.active >= int(s.stack.layers.size())) return;
    const MapLayer& active = s.active >= 0 ? s.stack.layers[size_t(s.active)] : s.stack.background;
    if (active.locked || !active.visible || active.linkedTo) return;  // (its end refuses it)
    bool others = false;  // (they stay as they are on the map)
    for (int r = 0; r < int(s.stack.layers.size()); ++r) others = others || (r != s.active && s.stack.layers[size_t(r)].visible && !s.stack.layers[size_t(r)].corners.empty());
    if (!others) return;
    layersCapture(ed, true);
}

static void layersCapture(Editor& ed, bool live) {
    AppState& app = shellState();
    MapLayersState& s = gMapLayers;
    std::vector<uint16_t> now;
    layerCornerGrid(ed, now);
    const LayerObjectLists objects = layerObjectsOf(ed);
    std::map<std::pair<int, int>, uint16_t> changed;
    if (now.size() == s.corners.size()) {
        size_t i = 0;
        for (int y = -1; y <= int(ed.height()) + 1; ++y)
            for (int x = -1; x <= int(ed.width()) / 2 + 1; ++x, ++i)
                if (now[i] != s.corners[i] && now[i]) changed[{x, y}] = now[i];
    }
    const auto sameUnits = [](const std::vector<Unit>& a, const std::vector<Unit>& b) {
        Bytes x, y;
        for (const Unit& u : a) u.encode(x);
        for (const Unit& u : b) u.encode(y);
        return x == y;
    };
    const auto sameDoodads = [](const std::vector<Doodad>& a, const std::vector<Doodad>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Doodad& p, const Doodad& q) {
                   return p.id == q.id && p.x == q.x && p.y == q.y && p.owner == q.owner && p.disabled == q.disabled && p.noOverlay == q.noOverlay;
               });
    };
    const auto sameSprites = [](const std::vector<MapSprite>& a, const std::vector<MapSprite>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const MapSprite& p, const MapSprite& q) {
                   return p.id == q.id && p.x == q.x && p.y == q.y && p.owner == q.owner && p.flags == q.flags;
               });
    };
    const bool doodadsChanged = !sameDoodads(objects.doodads, s.objects.doodads);
    const bool objectsChanged = !sameUnits(objects.units, s.objects.units) || doodadsChanged || !sameSprites(objects.sprites, s.objects.sprites);
    // Tiles that changed where the corners did not: placed tiles (not a doodad's: the doodad places them again).
    std::map<std::pair<int, int>, uint16_t> placed;
    // What a tool asked for goes into the layer, also over the same terrain, and not the terrain around it that was fitted
    // to the map as it shows: flattening fits that again, to what is under the layer. (A tool that notes nothing, such as a
    // shape's copy: what changed.) A tool that changed no tile takes no undo step.
    const std::map<std::pair<int, int>, uint16_t> fitted = changed;
    if (const auto askedCorners = ed.takeAskedCorners(); !askedCorners.empty()) {
        changed.clear();
        for (const auto& [x, y] : askedCorners)
            if (const uint16_t terrain = ed.isomGrid().terrainAt(x, y)) changed[{x, y}] = terrain;
    }
    const bool stepTaken = ed.undoSteps() != s.undoSteps;
    const bool kitTool = app.mode == Mode::Terrain && (currentTool(app) == TBridge || currentTool(app) == TRamp);
    const TerrainLayer section = ed.layerTileSection();
    const std::vector<uint16_t>& nowSection = section.tiles;
    // (a tile the ISOM does not give: asked only for the tiles that changed, not the whole map's at each stroke step)
    const int sectionW = int(section.width);
    const auto notTerrain = [&](size_t k) { return !ed.tileIsFromIsom(section, int(k) % sectionW, int(k) / sectionW); };
    if (ed.terrain().tiles.size() == s.tiles.size() && nowSection.size() == s.tileSection.size() && nowSection.size() == s.tiles.size()) {
        const int tw = int(ed.width());
        const std::vector<uint16_t>& nowTiles = ed.terrain().tiles;
        for (size_t k = 0; k < nowTiles.size(); ++k) {
            // (under the doodads: a tile placed where a doodad goes away too; a doodad's own tiles are not there)
            if (doodadsChanged ? nowSection[k] == s.tileSection[k] : nowTiles[k] == s.tiles[k]) continue;
            if (kitTool) {  // (a bridge's or a ramp's tiles are its own, also beside the corners it changes; not the terrain's)
                if (notTerrain(k)) placed[{int(k) % tw, int(k) / tw}] = nowTiles[k];
                continue;
            }
            const int tx = int(k) % tw, ty = int(k) / tw, rx = tx / 2;
            bool nearChange = false;  // (an edge's tiles depend on the corners around them too)
            for (int cy = ty - 3; !nearChange && cy <= ty + 4; ++cy)
                for (int cx = rx - 3; !nearChange && cx <= rx + 4; ++cx) nearChange = fitted.count({cx, cy}) > 0;
            if ((!nearChange && notTerrain(k)) || !nowTiles[k]) placed[{tx, ty}] = nowTiles[k];  // (not the terrain's other variant; a null tile: a cut)
        }
    }
    if (changed.empty() && placed.empty() && !objectsChanged) {
        if (!live && s.liveDirty) s.liveDirty = false, ed.amendLastStep(layersFiles(true));
        s.revision = ed.revision();
        return;
    }
    // A bridge or a ramp stands on its ground: on a layer, only where that layer has terrain.
    if (!live && kitTool && s.active >= 0 && s.active < int(s.stack.layers.size()) && stepTaken) {
        const auto& have = s.stack.layers[size_t(s.active)].corners;
        bool on = true;
        for (const auto& [at, tile] : placed) {
            const int rx = at.first / 2, ry = at.second;
            on = on && (have.count({rx, ry}) || have.count({rx + 1, ry}) || have.count({rx, ry + 1}) || have.count({rx + 1, ry + 1}));
        }
        for (const auto& [at, terrain] : changed) on = on && have.count(at);
        if (!on) {
            ed.refuseLastEdit();
            setMessage(L"A bridge or a ramp needs its ground in the same layer: paint the terrain in this layer, or select the layer that has it");
            return;
        }
    }
    // A linked duplicate follows its original: an edit on it goes back.
    if (s.active >= 0 && s.active < int(s.stack.layers.size()) && s.stack.layers[size_t(s.active)].linkedTo && !live &&
        (!changed.empty() || !placed.empty())) {  // (its objects: layerLockedBytes below)
        if (stepTaken) ed.refuseLastEdit();
        setMessage(linkedRefusal(s.stack.layers[size_t(s.active)]));
        return;
    }
    // A locked layer does not change: the edit goes back.
    if (const MapLayer& activeLayer = s.active >= 0 && s.active < int(s.stack.layers.size()) ? s.stack.layers[size_t(s.active)] : s.stack.background;
        !live && (activeLayer.locked || (s.active < 0 && !activeLayer.visible)) && (!changed.empty() || !placed.empty())) {
        if (stepTaken) ed.refuseLastEdit();
        setMessage(activeLayer.locked ? L"The layer is locked: nothing changed" : L"The Background is hidden: nothing changed");
        return;
    }
    if (objectsChanged && live) {
        captureLayerObjects(s.stack, s.objects, objects, s.owners, s.active);
    } else if (objectsChanged) {
        LayerStack trial = s.stack;
        captureLayerObjects(trial, s.objects, objects, s.owners, s.active);
        if (layerLockedBytes(trial) != layerLockedBytes(s.stack)) {
            ed.refuseLastEdit();
            setMessage(L"That belongs to a locked layer, or the active layer is locked: nothing changed");
            return;
        }
        s.stack = std::move(trial);
    }
    bool baseChanged = false;
    if (!changed.empty() || !placed.empty()) {
        if (s.active >= 0 && s.active < int(s.stack.layers.size())) {
            MapLayer& layer = s.stack.layers[size_t(s.active)];
            for (const auto& [at, terrain] : changed) layer.corners[at] = terrain;
            std::vector<std::pair<int, int>> nulls;
            for (const auto& [at, tile] : placed) {
                if (tile) layer.tiles[at] = tile;
                else nulls.push_back(at);  // (the null tile: a cut, or the tile placed)
            }
            layerEraseTiles(layer, nulls);
        } else {  // the Background: they go into its file
            LayerStack one;
            MapLayer& l = one.add("", 0);
            l.corners = changed;
            l.tiles = placed;
            ed.composeLayers(s.base, one, nullptr, true);
            s.base = ed.layerBaseNow();
            baseChanged = true;
        }
    }
    if (live) {  // (the stroke's undo step gets the files at its end)
        layersCompose(ed);
        const size_t steps = s.undoSteps;
        layersRemember(ed);
        s.undoSteps = steps;
        s.liveDirty = true;
        return;
    }
    if (!stepTaken) ed.beginLayerEdit();
    layersCompose(ed);
    layersKeepSelections(ed, objects);
    baseChanged = baseChanged || s.liveDirty;
    s.liveDirty = false;
    if (stepTaken) ed.amendLastStep(layersFiles(baseChanged));
    else ed.endLayerEdit(true, layersFiles(baseChanged));
    layersRemember(ed);
    layersPanelChanged();
}

bool mapLayersOn() { return gMapLayers.on; }

// The status bar's words on the current layer: with layers in the map, always; none: nothing.
std::wstring mapLayersStatusText() {
    const MapLayersState& s = gMapLayers;
    if (!s.on || s.stack.layers.empty() || s.ed != shellState().ed.get()) return {};
    const bool row = s.active >= 0 && s.active < int(s.stack.layers.size());
    const MapLayer& l = row ? s.stack.layers[size_t(s.active)] : s.stack.background;
    std::wstring t = L"   |   " + (row ? widen(l.name) : std::wstring(tr(L"Background")));
    if (!l.visible) t += std::wstring(L" (") + tr(L"hidden") + L")";
    if (l.locked) t += std::wstring(L" (") + tr(L"locked") + L")";
    return t;
}
bool mapLayersOnLayer() { return gMapLayers.on && gMapLayers.active >= 0; }

// Whether the active layer refuses an edit (locked or hidden); the message says so.
bool mapLayersActiveRefuses() {
    const MapLayersState& s = gMapLayers;
    if (s.on && s.active >= 0 && s.active < int(s.stack.layers.size()) && s.stack.layers[size_t(s.active)].linkedTo)
        return setMessage(linkedRefusal(s.stack.layers[size_t(s.active)])), true;
    if (mapLayersActiveLocked()) return setMessage(L"The layer is locked: nothing changed"), true;
    if (!s.on) return false;
    const MapLayer& active = s.active >= 0 && s.active < int(s.stack.layers.size()) ? s.stack.layers[size_t(s.active)] : s.stack.background;
    if (!active.visible) return setMessage(L"The layer is hidden: nothing changed"), true;
    return false;
}

// Terrain's Move with layers: the map's selection moves in the active layer only, and the rest of the stack stays. The
// release and the preview both go through this (mapLayersMoveTerrain, mapLayersMovePreview). False: the layer has nothing there.
static bool layersMoveSelection(Editor& copy, const Editor& ed, LayerStack& stack, Bytes& base, const std::vector<std::pair<int, int>>& cells, bool iso, int rx, int ry, int tileDx, int tileDy, bool& baseChanged) {
    const AppState& app = shellState();
    const MapLayersState& s = gMapLayers;
    const Rect& r = app.terrainSel;
    const bool masked = !app.terrainSelMask.empty();
    const auto inSelection = [&](int x, int y) { return masked ? terrainSelHas(x / 32, y / 32) : Editor::inSelectShape(r, selectShape(), x, y); };
    MapLayer& l = s.active >= 0 ? stack.layers[size_t(s.active)] : stack.background;
    bool any = false;
    if (iso) {  // (the objects copied with terrain go with it, also on the Background)
        MapLayer goes;
        const auto split = [&](auto& from, auto& to, bool with) {
            if (!with) return;
            for (size_t i = from.size(); i-- > 0;)
                if (inSelection(from[i].x, from[i].y)) to.push_back(from[i]), from.erase(from.begin() + i);
        };
        split(l.units, goes.units, app.copyUnitsWithTerrain);
        split(l.doodads, goes.doodads, app.copyDoodadsWithTerrain);
        any = !goes.units.empty() || !goes.doodads.empty();
        moveLayerContents(goes, rx, ry);
        l.units.insert(l.units.end(), goes.units.begin(), goes.units.end());
        l.doodads.insert(l.doodads.end(), goes.doodads.begin(), goes.doodads.end());
    }
    if (s.active < 0) {  // the Background: as on a map with no layers, on its terrain alone
        std::string error;
        LayerStack none;
        copy.composeLayers(base, none);
        if (iso ? copySelectedTerrainOf(copy, app, r) : copySelectedTiles(copy)) {
            terrainMoveApply(copy, cells, iso, rx, ry, r.left / 32 + tileDx, r.top / 32 + tileDy);
            base = copy.layerBaseNow(), baseChanged = true;
            any = true;
        }
        return any && copy.openChk(ed.chk(), error);
    }
    if (iso) {  // whole diamonds in the selection, as a copy takes them
        std::map<std::pair<int, int>, uint16_t> corners;
        for (const auto& c : ed.cornersInShape(r, masked ? Editor::SelectShape::Rect : selectShape())) {
            const auto it = l.corners.find(c);
            if (it == l.corners.end()) continue;
            bool whole = true;
            for (int y = c.second - 1; whole && masked && y <= c.second; ++y)
                for (int x = 2 * c.first - 2; whole && x <= 2 * c.first + 1; ++x)
                    whole = x < 0 || y < 0 || x >= int(ed.width()) || y >= int(ed.height()) || terrainSelHas(x, y);
            if (whole) corners[c] = it->second;
        }
        for (const auto& [c, terrain] : corners) l.corners.erase(c);
        for (const auto& [c, terrain] : corners) l.corners[{c.first + rx, c.second + ry}] = terrain;
        std::map<std::pair<int, int>, uint16_t> tiles;
        for (const auto& c : cells)
            if (const auto it = l.tiles.find(c); it != l.tiles.end()) tiles[c] = it->second, l.tiles.erase(it);
        for (const auto& [c, tile] : tiles) l.tiles[{c.first + tileDx, c.second + tileDy}] = tile;
        return any || !corners.empty() || !tiles.empty();
    }
    // (Rectangle and Tile: the tiles that the layer shows there, as they are; NULL where they were)
    std::map<std::pair<int, int>, uint16_t> tiles;
    const int w = int(ed.width());
    for (const auto& [x, y] : cells) {
        const int cx = x / 2;
        const bool layers = l.tiles.count({x, y}) || l.corners.count({cx, y}) || l.corners.count({cx + 1, y}) || l.corners.count({cx, y + 1}) || l.corners.count({cx + 1, y + 1});
        if (layers) tiles[{x, y}] = ed.terrain().tiles[size_t(y) * w + x];
    }
    if (tiles.empty()) return any;
    layerEraseTiles(l, cells);
    for (const auto& [c, tile] : tiles) l.tiles[{c.first + tileDx, c.second + tileDy}] = tile;
    return true;
}

// Before the map's size changes (Crop or extend, Map properties): the Background with the same change, and the layers'
// contents moved with the map's. (A new tileset has other terrains: its map is flat.)
void mapLayersBeforeResize(Editor& ed, int w, int h, int era, int shiftX, int shiftY, uint16_t fill) {
    MapLayersState& s = gMapLayers;
    (void)ed.archiveFiles();  // (the layers' files: this one wait, at the user's OK)
    mapLayersSync();
    s.resized.on = false;
    if (!s.on || s.ed != &ed || s.filesPending || era != int(ed.era())) return;
    Editor& copy = shapeCopy(ed.gameData());
    std::string error;
    LayerStack none;
    if (!copy.openChk(ed.chk(), error) || !copy.composeLayers(s.base, none) || !copy.resizeMap(uint16_t(w), uint16_t(h), uint16_t(era), shiftX, shiftY, fill, error)) {
        s.resized.on = true, s.resized.w = -1;  // (the Background cannot follow, as a damaged file: the layers flatten, also with the same size)
        return;
    }
    Editor::resizeShift(shiftX, shiftY);
    LayerStack stack = s.stack;
    const int dx = shiftX / 2, dy = shiftY;
    for (MapLayer& l : stack.layers) moveLayerContents(l, dx, dy);
    moveLayerContents(stack.background, dx, dy);
    // (the Background's objects off the new map go, as a resize takes them away)
    const auto gone = [&](const auto& o) { return o.x < 0 || o.y < 0 || o.x >= w * 32 || o.y >= h * 32; };
    std::erase_if(stack.background.units, gone);
    std::erase_if(stack.background.doodads, gone);
    std::erase_if(stack.background.sprites, gone);
    s.resized.base = copy.layerBaseNow(), s.resized.stack = std::move(stack), s.resized.w = w, s.resized.h = h, s.resized.on = true;
}

// The size did not change after all (mapLayersBeforeResize).
void mapLayersResizeFailed() { gMapLayers.resized.on = false; }

bool mapLayersHaveStack(const Editor& ed) { return gMapLayers.on && gMapLayers.ed == &ed && !gMapLayers.filesPending; }

// The preview: `copy` (a copy of the map) as the release would leave it.
void mapLayersMovePreview(Editor& copy, const Editor& ed, const std::vector<std::pair<int, int>>& cells, bool iso, int rx, int ry, int tileDx, int tileDy) {
    LayerStack stack = gMapLayers.stack;
    Bytes base = gMapLayers.base;
    bool baseChanged = false;
    if (layersMoveSelection(copy, ed, stack, base, cells, iso, rx, ry, tileDx, tileDy, baseChanged)) copy.composeLayers(base, stack);
}

// The release: one undo step. False: nothing to move.
bool mapLayersMoveTerrain(Editor& ed, const std::vector<std::pair<int, int>>& cells, bool iso, int rx, int ry, int tileDx, int tileDy) {
    MapLayersState& s = gMapLayers;
    Editor& copy = shapeCopy(ed.gameData());
    std::string error;
    if (!copy.openChk(ed.chk(), error)) return false;
    shapeCopySettings(copy, ed);
    copy.setMirrors(0), copy.setTurns(1);
    LayerStack stack = s.stack;
    Bytes base = s.base;
    bool baseChanged = false;
    if (!layersMoveSelection(copy, ed, stack, base, cells, iso, rx, ry, tileDx, tileDy, baseChanged)) return false;
    AppState& app = shellState();
    app.selDoodads.clear(), app.selSprites.clear(), ed.clearSelection();  // (the lists change)
    ed.beginLayerEdit();
    s.stack = std::move(stack), s.base = std::move(base);
    layersCompose(ed);
    ed.endLayerEdit(true, layersFiles(baseChanged));
    layersRemember(ed);
    layersPanelChanged();
    return true;
}

bool mapLayersActiveLocked() {
    const MapLayersState& s = gMapLayers;
    // (a linked duplicate too: it follows its original only)
    const MapLayer& l = s.active >= 0 && s.active < int(s.stack.layers.size()) ? s.stack.layers[size_t(s.active)] : s.stack.background;
    return s.on && (l.locked || l.linkedTo);
}

void mapLayersFrame() {
    AppState& app = shellState();
    mapLayersSync();
    MapLayersState& s = gMapLayers;
    if (app.ed && app.ed->isOpen() && s.on) s.activeOf[app.ed->path()] = s.active;
}

// --- The panel's commands ---------------------------------------------------------------------------------------------

// What to do with the layers to play the map. The panel's (?), and once by itself with the first new layer.
void mapLayersInfo() {
    shellInfoBox(L"Map layers are experimental. Keep a copy of your map, and report what does not work (Report feedback).\n\n"
                 L"Layers stay in this map file while you work on them.\n\nTo make the map that you play in StarCraft, use File > Export map. "
                 L"All layers that show become one map, and the layer data stays out of it.",
                 L"Map layers (experimental)");
}

// The map becomes the Background: its terrain in its file, its objects in the stack.
static void layersStart(Editor& ed) {
    MapLayersState& s = gMapLayers;
    if (s.on) return;
    s.base = ed.layerBaseNow(), s.on = true;
    s.stack = {};
    for (const Unit& u : ed.units()) s.stack.background.units.push_back({u, int(u.x), int(u.y), true});
    for (const Doodad& d : ed.layerDoodads()) s.stack.background.doodads.push_back({d, int(d.x), int(d.y), true});
    for (const MapSprite& m : ed.layerSprites()) s.stack.background.sprites.push_back({m, int(m.x), int(m.y), true});
}

void mapLayersNew() {
    const size_t layersBefore = gMapLayers.stack.layers.size();
    layersEdit([](Editor& ed) {
        MapLayersState& s = gMapLayers;
        layersStart(ed);
        int n = 1;
        const auto taken = [&](const std::string& name) {
            return std::any_of(s.stack.layers.begin(), s.stack.layers.end(), [&](const MapLayer& l) { return l.name == name; });
        };
        while (taken("Layer " + std::to_string(n))) ++n;
        const int at = s.active + 1;
        s.stack.add("Layer " + std::to_string(n), at);
        s.opacity.erase(s.stack.layers[size_t(at)].id);  // (an id again after an undo: opaque, as a new layer)
        s.active = at, s.picked = {at};
        setMessage(L"New layer: the tools now change this layer");
        return true;
    });
    if (gMapLayers.stack.layers.size() > layersBefore && !prefs::store().has("MapLayersInfoShown")) {
        prefs::store().set("MapLayersInfoShown", int64_t(1));
        mapLayersInfo();
    }
}

void mapLayersPick(int row, unsigned mods) {
    MapLayersState& s = gMapLayers;
    if (mods & draw::kCtrl) {
        if (!s.picked.erase(row)) s.picked.insert(row);
    } else if (mods & draw::kShift) {
        for (int i = std::min(s.active, row); i <= std::max(s.active, row); ++i) s.picked.insert(i);
    } else {
        s.picked = {row};
    }
    s.active = row;
    layersPanelChanged();
    shellState().dirty = true;
}

void mapLayersToggleVisible(int row) {
    layersEdit([row](Editor& ed) {
        MapLayersState& s = gMapLayers;
        if (row >= int(s.stack.layers.size())) return false;
        layersStart(ed);
        MapLayer& l = row >= 0 ? s.stack.layers[size_t(row)] : s.stack.background;
        l.visible = !l.visible;
        return true;
    });
}

void mapLayersToggleLock(int row) {
    // (a linked duplicate is always locked: it follows its original)
    if (row >= 0 && row < int(gMapLayers.stack.layers.size()) && gMapLayers.stack.layers[size_t(row)].linkedTo)
        return setMessage(linkedRefusal(gMapLayers.stack.layers[size_t(row)]));
    layersEdit([row](Editor& ed) {
        MapLayersState& s = gMapLayers;
        if (row >= int(s.stack.layers.size())) return false;
        layersStart(ed);
        MapLayer& l = row >= 0 ? s.stack.layers[size_t(row)] : s.stack.background;
        l.locked = !l.locked;
        return true;
    });
}

// A new layer's name: `base` (without the same word and number at its end: "Hills copy 2" copies as "Hills copy 3") and
// the word with the first number that no layer has ("Hills copy 1", "Hills copy 2"; "Hills link 1").
static std::string layerNumberedName(std::string base, const char* word) {
    const std::string w = std::string(" ") + word + " ";
    if (const size_t at = base.rfind(w); at != std::string::npos && at > 0 && at + w.size() < base.size() &&
                                         base.find_first_not_of("0123456789", at + w.size()) == std::string::npos)
        base.resize(at);
    std::set<std::string> names;
    for (const MapLayer& l : gMapLayers.stack.layers) names.insert(l.name);
    for (int n = 1;; ++n)
        if (std::string name = base + " " + word + " " + std::to_string(n); !names.count(name)) return name;
}

void mapLayersDuplicate(int row) {
    layersEdit([row](Editor&) {
        MapLayersState& s = gMapLayers;
        if (row < 0 || row >= int(s.stack.layers.size())) return false;
        MapLayer copy = s.stack.layers[size_t(row)];
        copy.id = s.stack.nextId++;
        copy.name = layerNumberedName(copy.name, "copy");
        copy.locked = false;
        copy.linkedTo = 0, copy.linkDx = copy.linkDy = 0;  // (of a linked duplicate: a layer of its own, with what it shows)
        // (new serials, as a paste's; its links only inside the copy)
        uint32_t next = 1;
        const auto seen = [&next](const MapLayer& l) {
            for (const auto& o : l.units) next = std::max(next, o.item.serial + 1);
        };
        seen(s.stack.background);
        for (const MapLayer& l : s.stack.layers) seen(l);
        std::map<uint32_t, uint32_t> serials;
        for (auto& o : copy.units) serials[o.item.serial] = next, o.item.serial = next++;
        for (auto& o : copy.units)
            if (o.item.relatedSerial) {
                const auto it = serials.find(o.item.relatedSerial);
                o.item.relatedSerial = it != serials.end() ? it->second : 0;
            }
        s.stack.layers.insert(s.stack.layers.begin() + row + 1, std::move(copy));
        s.active = row + 1, s.picked = {row + 1};
        return true;
    });
}

// A linked duplicate of the layer: over it in the list, at the same place (Move layer takes it elsewhere); it follows
// the original's changes. Of a linked duplicate: one more of its original.
void mapLayersDuplicateLinked(int row) {
    layersEdit([row](Editor&) {
        MapLayersState& s = gMapLayers;
        if (row < 0 || row >= int(s.stack.layers.size())) return false;
        const MapLayer& from = s.stack.layers[size_t(row)];
        MapLayer copy;
        copy.id = s.stack.nextId++;
        copy.linkedTo = from.linkedTo ? from.linkedTo : from.id;
        std::string original = from.name;  // ("<original> link 1", "<original> link 2": of a link, its original's)
        for (const MapLayer& o : s.stack.layers)
            if (o.id == copy.linkedTo) original = o.name;
        copy.name = layerNumberedName(original, "link");
        copy.linkDx = from.linkedTo ? from.linkDx : 0, copy.linkDy = from.linkedTo ? from.linkDy : 0;
        s.stack.layers.insert(s.stack.layers.begin() + row + 1, std::move(copy));
        s.active = row + 1, s.picked = {row + 1};
        s.opacity.erase(s.stack.layers[size_t(row + 1)].id);
        setMessage(L"Linked duplicate: it follows the changes of the original. Move it with Move layer.");
        return true;  // (layersEdit makes the map again: its contents come from the original then)
    });
}

// The linked duplicate becomes a layer of its own, with what it shows now.
void mapLayersUnlink(int row) {
    layersEdit([row](Editor&) {
        MapLayersState& s = gMapLayers;
        if (row < 0 || row >= int(s.stack.layers.size()) || !s.stack.layers[size_t(row)].linkedTo) return false;
        MapLayer& l = s.stack.layers[size_t(row)];
        l.linkedTo = 0, l.linkDx = l.linkDy = 0;
        setMessage(L"Unlinked: the layer is one of its own now");
        return true;
    });
}

// The selection's tiles of the active layer become NULL (Delete in Terrain mode). False: not on a layer.
bool mapLayersDeleteSelection(const std::vector<std::pair<int, int>>& tiles) {
    MapLayersState& s = gMapLayers;
    if (!s.on || s.active < 0 || s.active >= int(s.stack.layers.size()) || tiles.empty()) return false;
    if (s.stack.layers[size_t(s.active)].locked) return setMessage(L"The layer is locked: nothing changed"), true;
    if (s.stack.layers[size_t(s.active)].linkedTo) return setMessage(linkedRefusal(s.stack.layers[size_t(s.active)])), true;
    layersEdit([&tiles](Editor&) {
        MapLayersState& m = gMapLayers;
        if (m.active < 0 || m.active >= int(m.stack.layers.size())) return false;
        return layerEraseTiles(m.stack.layers[size_t(m.active)], tiles);
    });
    return true;
}

void mapLayersRename(int row) {
    MapLayersState& s = gMapLayers;
    if (row < 0 || row >= int(s.stack.layers.size())) return;
    std::wstring name = widen(s.stack.layers[size_t(row)].name);
    if (!shellPromptText(L"Rename the layer", name) || name.empty()) return;
    layersEdit([row, name](Editor&) {
        MapLayersState& m = gMapLayers;
        if (row >= int(m.stack.layers.size())) return false;
        m.stack.layers[size_t(row)].name = narrow(name);
        return true;
    });
}

// A layer goes to another place in the list (a higher index is over the lower ones). The Background stays at the bottom.
void mapLayersMoveRow(int from, int to) {
    layersEdit([from, to](Editor&) {
        MapLayersState& s = gMapLayers;
        const int n = int(s.stack.layers.size());
        if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
        MapLayer l = std::move(s.stack.layers[size_t(from)]);
        s.stack.layers.erase(s.stack.layers.begin() + from);
        s.stack.layers.insert(s.stack.layers.begin() + to, std::move(l));
        s.active = to, s.picked = {to};
        return true;
    });
}

void mapLayersSort() {
    layersEdit([](Editor&) {
        MapLayersState& s = gMapLayers;
        if (s.stack.layers.size() < 2) return false;
        const uint32_t id = s.active >= 0 ? s.stack.layers[size_t(s.active)].id : 0;
        s.stack.sortByName();
        for (size_t i = 0; i < s.stack.layers.size(); ++i)
            if (s.stack.layers[i].id == id) s.active = int(i);
        s.picked = {s.active};
        return true;
    });
}

void mapLayersDelete() {
    layersEdit([](Editor&) {
        MapLayersState& s = gMapLayers;
        std::vector<int> rows(s.picked.begin(), s.picked.end());
        std::erase_if(rows, [&](int r) { return r < 0 || r >= int(s.stack.layers.size()); });
        if (rows.empty()) return false;
        for (int r : rows)
            if (s.stack.layers[size_t(r)].locked) return setMessage(L"A locked layer cannot be deleted"), false;
        for (size_t k = rows.size(); k-- > 0;) s.stack.layers.erase(s.stack.layers.begin() + rows[k]);
        s.active = std::min(rows.front(), int(s.stack.layers.size()) - 1);
        s.picked = {s.active};
        return true;
    });
}

// The selected layers become one. With the Background selected: they go into it.
void mapLayersMerge() {
    layersEdit([](Editor& ed) {
        MapLayersState& s = gMapLayers;
        std::vector<int> rows(s.picked.begin(), s.picked.end());
        const bool intoBase = !rows.empty() && rows.front() < 0;
        std::erase_if(rows, [&](int r) { return r < 0 || r >= int(s.stack.layers.size()); });
        if (rows.empty() || (!intoBase && rows.size() < 2)) return false;
        for (int r : rows)
            if (s.stack.layers[size_t(r)].locked) return setMessage(L"A locked layer cannot be merged"), false;
        if ((intoBase && s.stack.background.locked)) return setMessage(L"A locked layer cannot be merged"), false;
        for (int r : rows) {
            s.stack.layers[size_t(r)].linkedTo = 0;  // (merged: a layer of its own, with what it showed)
            for (MapLayer& d : s.stack.layers)  // (the duplicates of a merged layer: on their own, as they are)
                if (d.linkedTo == s.stack.layers[size_t(r)].id) d.linkedTo = 0, d.linkDx = d.linkDy = 0;
        }
        if (intoBase) {
            LayerStack terrain;
            // (what shows: a hidden layer and what is outside of the map go, as Flatten's)
            const auto addShown = [](auto& to, const auto& from) {
                for (const auto& o : from)
                    if (o.shown) to.push_back(o);
            };
            for (int r : rows) {
                const MapLayer& from = s.stack.layers[size_t(r)];
                if (!from.visible) continue;
                MapLayer& to = terrain.add("", int(terrain.layers.size()));
                to.corners = from.corners, to.tiles = from.tiles;
                MapLayer& bg = s.stack.background;
                addShown(bg.units, from.units), addShown(bg.doodads, from.doodads), addShown(bg.sprites, from.sprites);
            }
            ed.composeLayers(s.base, terrain, nullptr, true);
            s.base = ed.layerBaseNow();
            for (size_t k = rows.size(); k-- > 0;) s.stack.layers.erase(s.stack.layers.begin() + rows[k]);
            s.active = -1, s.picked = {-1};
            setMessage(L"Merged the layers into the Background");
            return true;
        }
        s.active = s.stack.merge(rows);
        s.picked = {s.active};
        setMessage(L"Merged the layers");
        return true;
    });
}

// The commands of the current layer (the keys of Mode > Layer).
static bool layersHaveCurrent() {
    const MapLayersState& s = gMapLayers;
    if (s.on && s.active >= 0 && s.active < int(s.stack.layers.size())) return true;
    setMessage(L"Select a layer first");
    return false;
}
// All the layers are selected (not the Background, which does not move): Move Layer moves them, and Merge merges them.
void mapLayersSelectAll() {
    MapLayersState& s = gMapLayers;
    if (!s.on || s.stack.layers.empty()) return setMessage(L"The map has no layers: nothing to select");
    s.picked.clear();
    for (int i = 0; i < int(s.stack.layers.size()); ++i) s.picked.insert(i);
    if (s.active < 0) s.active = int(s.stack.layers.size()) - 1;
    setMessage(L"Selected all " + std::to_wstring(s.stack.layers.size()) + L" layers");
    layersPanelChanged();
    shellState().dirty = true;
}
void mapLayersCurrentHide() { mapLayersToggleVisible(gMapLayers.active); }
void mapLayersCurrentLock() { mapLayersToggleLock(gMapLayers.active); }
void mapLayersCurrentRename() {
    if (layersHaveCurrent()) mapLayersRename(gMapLayers.active);
}
void mapLayersCurrentDuplicate() {
    if (layersHaveCurrent()) mapLayersDuplicate(gMapLayers.active);
}
void mapLayersCurrentDuplicateLinked() {
    if (layersHaveCurrent()) mapLayersDuplicateLinked(gMapLayers.active);
}
void mapLayersMergeDown(int row);
void mapLayersCurrentMergeDown() {
    if (layersHaveCurrent()) mapLayersMergeDown(gMapLayers.active);
}
// The layer over (+1) or under (-1) the current one is the current one. Under the lowest: the Background.
void mapLayersSelectStep(int step) {
    MapLayersState& s = gMapLayers;
    if (!s.on) return;
    mapLayersPick(std::clamp(s.active + step, -1, int(s.stack.layers.size()) - 1), 0);
}
// The current layer goes up or down in the list.
void mapLayersMoveStep(int step) {
    if (layersHaveCurrent()) mapLayersMoveRow(gMapLayers.active, gMapLayers.active + step);
}

// The rows of the layers that show something at a map pixel, from the top down (the Background: not in it).
std::vector<int> mapLayersAt(int px, int py) {
    const MapLayersState& s = gMapLayers;
    std::vector<int> out;
    for (int r = int(s.stack.layers.size()) - 1; r >= 0; --r) {
        const MapLayer& l = s.stack.layers[size_t(r)];
        if (!l.visible) continue;
        bool here = l.tiles.count({px >= 0 ? px / 32 : -1 - (-px - 1) / 32, py >= 0 ? py / 32 : -1 - (-py - 1) / 32}) > 0;
        // (a corner's diamond: 64 px each way across, 32 px each way down)
        const int cx = px >= 0 ? px / 64 : -1 - (-px - 1) / 64, cy = py >= 0 ? py / 32 : -1 - (-py - 1) / 32;
        for (int y = cy - 1; !here && y <= cy + 2; ++y)
            for (int x = cx - 1; !here && x <= cx + 2; ++x)
                here = std::abs(px - x * 64) * 32 + std::abs(py - y * 32) * 64 <= 64 * 32 && l.corners.count({x, y}) > 0;
        const auto closeBy = [px, py](const auto& list) {
            return std::any_of(list.begin(), list.end(), [px, py](const auto& o) { return o.shown && std::abs(o.x - px) <= 16 && std::abs(o.y - py) <= 16; });
        };
        if (here || closeBy(l.units) || closeBy(l.doodads) || closeBy(l.sprites)) out.push_back(r);
    }
    return out;
}

// The layer goes into the one under it.
void mapLayersMergeDown(int row) {
    MapLayersState& s = gMapLayers;
    if (row < 0 || row >= int(s.stack.layers.size())) return;
    const MapLayer& under = row > 0 ? s.stack.layers[size_t(row - 1)] : s.stack.background;
    if (s.stack.layers[size_t(row)].locked || under.locked) return setMessage(L"A locked layer cannot be merged");
    s.picked = {row, row - 1};
    mapLayersMerge();
}

// All layers that show go into the Background, the hidden ones go away: the map is as it shows, with no layers.
void mapLayersFlatten() {
    AppState& app = shellState();
    MapLayersState& s = gMapLayers;
    if (!app.ed || !s.on || app.ed->inStroke()) return;
    Editor& ed = *app.ed;
    mapLayersSync();
    ed.beginLayerEdit();
    ed.endLayerEdit(true, {}, {kLayersFile, kLayersBaseFile});
    s.active = -1;
    layersRead(ed);
    shellMenusChanged();
    app.dirty = true;
    setMessage(L"Flattened the layers into the Background");
}

// --- The map's mouse ------------------------------------------------------------------------------------------------

bool mapLayersEraseOn(AppState& app) {
    return gMapLayers.on && gMapLayers.active >= 0 && app.mode == Mode::Terrain && currentTool(app) == TErase;
}

static bool layersEraseAt(Editor& ed, const Brush& brush, int wx, int wy) {
    MapLayersState& s = gMapLayers;
    MapLayer& layer = s.stack.layers[size_t(s.active)];
    bool any = false;
    for (const auto& at : ed.isomGrid().brushCorners(wx, wy, brush)) {  // (on the map only: terrain is edited there)
        any |= layer.corners.erase(at) > 0;
        // (the placed tiles of the rects around the corner)
        for (int ty = at.second - 1; ty <= at.second; ++ty)
            for (int tx = (at.first - 1) * 2; tx <= at.first * 2 + 1; ++tx) any |= layer.tiles.erase({tx, ty}) > 0;
    }
    if (any) s.dragChanged = true;
    return any;
}

void mapLayersEraseDown(AppState& app, float wx, float wy) {
    MapLayersState& s = gMapLayers;
    mapLayersSync();
    if (s.active < 0 || s.active >= int(s.stack.layers.size())) return;  // (the sync read an undo: no layer now)
    if (s.stack.layers[size_t(s.active)].locked) return setMessage(L"The layer is locked");
    if (s.stack.layers[size_t(s.active)].linkedTo) return setMessage(linkedRefusal(s.stack.layers[size_t(s.active)]));
    app.selDoodads.clear(), app.selSprites.clear(), app.ed->clearSelection();  // (the lists change in the drag)
    app.ed->beginLayerEdit();
    s.dragEd = app.ed.get();
    s.dragStart = s.stack;
    s.erasing = true, s.dragChanged = false;
    s.nowX = int(wx), s.nowY = int(wy);
    if (layersEraseAt(*app.ed, currentBrush(app), int(wx), int(wy))) layersCompose(*app.ed);
    app.drag = Drag::LayerTool;
}

static void layersDragEnd(Editor& ed) {
    MapLayersState& s = gMapLayers;
    s.erasing = s.moving = false;
    s.dragEd = nullptr;
    s.moveFrom.clear();
    ed.endLayerEdit(s.dragChanged, s.dragChanged ? layersFiles(false) : std::vector<ArchiveFile>{});
    if (s.dragChanged && s.ed == &ed) layersRemember(ed);
    layersPanelChanged();
}

// Move Layer: a drag moves the selected layers.
// A move of layers by (dx, dy) rects (Move Layer): their terrain can go outside of the map (a place to try things out:
// mapLayersOutside), their units, doodads and sprites cannot. The move becomes the nearest one that keeps every object
// of the layers on the map (dx + dy stays even, as the rect grid).
void layersKeepObjectsOnMap(const Editor& ed, const std::map<int, MapLayer>& from, int& dx, int& dy) {
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    const int mw0 = ed.width() * 32, mh0 = ed.height() * 32;
    const auto take = [&](const auto& list) {  // (an object off the map already, as after a crop: it does not hold the layer)
        for (const auto& o : list)
            if (o.x >= 0 && o.y >= 0 && o.x < mw0 && o.y < mh0) x0 = std::min(x0, o.x), y0 = std::min(y0, o.y), x1 = std::max(x1, o.x), y1 = std::max(y1, o.y);
    };
    for (const auto& [r, l] : from) take(l.units), take(l.doodads), take(l.sprites);
    if (x0 > x1) return;  // (terrain only)
    const int mw = ed.width() * 32, mh = ed.height() * 32;
    const auto ceilDiv = [](int a, int b) { return a >= 0 ? (a + b - 1) / b : -(-a / b); };
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int dxLo = ceilDiv(-x0, 64), dxHi = floorDiv(mw - 1 - x1, 64), dyLo = ceilDiv(-y0, 32), dyHi = floorDiv(mh - 1 - y1, 32);
    const int wantX = dx, wantY = dy;
    dx = std::clamp(dx, std::min(dxLo, 0), std::max(dxHi, 0)), dy = std::clamp(dy, std::min(dyLo, 0), std::max(dyHi, 0));
    if ((dx + dy) & 1) {  // (back on the grid: one row toward the start)
        if (dy != 0 && dy - (dy > 0 ? 1 : -1) >= std::min(dyLo, 0) && dy - (dy > 0 ? 1 : -1) <= std::max(dyHi, 0)) dy -= dy > 0 ? 1 : -1;
        else dx -= dx > 0 ? 1 : dx < 0 ? -1 : 0, dy = (dx + dy) & 1 ? dy - (dy > 0 ? 1 : dy < 0 ? -1 : 0) : dy;
        if ((dx + dy) & 1) dx = dy = 0;
    }
    if (dx != wantX || dy != wantY) setMessage(L"Units, doodads and sprites stay on the map: only a layer's terrain goes outside of it");
}

void mapLayersMoveDown(AppState& app, float wx, float wy) {
    MapLayersState& s = gMapLayers;
    mapLayersSync();
    if (s.ed != app.ed.get() || s.filesPending) return setMessage(L"The map's files are still being read");
    s.startX = int(wx), s.startY = int(wy), s.moveDX = s.moveDY = 0;
    s.moveFrom.clear();
    for (int r : s.picked)
        if (r >= 0 && r < int(s.stack.layers.size()) && !s.stack.layers[size_t(r)].locked) s.moveFrom[r] = s.stack.layers[size_t(r)];
    if (s.moveFrom.empty() && s.on && s.active >= 0 && s.active < int(s.stack.layers.size()) && !s.stack.layers[size_t(s.active)].locked)
        s.moveFrom[s.active] = s.stack.layers[size_t(s.active)];
    if (s.moveFrom.empty()) return setMessage(s.active >= 0 ? L"The layer is locked" : L"The Background does not move. Select a layer in the list.");
    app.drag = Drag::LayerTool;
    app.selDoodads.clear(), app.selSprites.clear(), app.ed->clearSelection();  // (the lists change in the drag)
    app.ed->beginLayerEdit();
    s.dragEd = app.ed.get();
    s.dragStart = s.stack;
    s.moving = true, s.dragChanged = false;
}

void mapLayersDragMove(AppState& app, float wx, float wy) {
    MapLayersState& s = gMapLayers;
    if (s.erasing) {  // (along the way from the last point, a step at most each 16 px: no gaps in a fast drag)
        const int steps = std::max(1, int(std::hypot(wx - s.nowX, wy - s.nowY) / 16));
        bool any = false;
        for (int k = 1; k <= steps; ++k) {
            const int x = s.nowX + int((wx - s.nowX) * k / steps), y = s.nowY + int((wy - s.nowY) * k / steps);
            any |= layersEraseAt(*app.ed, currentBrush(app), x, y);
        }
        s.nowX = int(wx), s.nowY = int(wy);
        if (any) layersCompose(*app.ed);  // (once for all the steps)
    } else if (s.moving) {
        int dx, dy;
        snapLayerMove(int(wx) - s.startX, int(wy) - s.startY, dx, dy);
        layersKeepObjectsOnMap(*app.ed, s.moveFrom, dx, dy);
        if (dx == s.moveDX && dy == s.moveDY) return;
        s.moveDX = dx, s.moveDY = dy;
        for (const auto& [r, from] : s.moveFrom) {
            s.stack.layers[size_t(r)] = from;
            moveLayerContents(s.stack.layers[size_t(r)], dx, dy);
            bool withOriginal = false;  // (its original moves too: the link stays, the copy moves with it)
            for (const auto& [r2, from2] : s.moveFrom) withOriginal = withOriginal || (from.linkedTo && from2.id == from.linkedTo);
            if (from.linkedTo && !withOriginal) s.stack.layers[size_t(r)].linkDx = from.linkDx + dx, s.stack.layers[size_t(r)].linkDy = from.linkDy + dy;
        }
        // (a linked duplicate of a moved original, not moved itself: its place from the original goes back by the move)
        for (size_t i = 0; s.dragStart && i < s.stack.layers.size() && i < s.dragStart->layers.size(); ++i) {
            MapLayer& l = s.stack.layers[i];
            if (!l.linkedTo || s.moveFrom.count(int(i))) continue;
            for (const auto& [r, from] : s.moveFrom)
                if (from.id == l.linkedTo) l.linkDx = s.dragStart->layers[i].linkDx - dx, l.linkDy = s.dragStart->layers[i].linkDy - dy;
        }
        s.dragChanged = true;  // (also back at its start: the map was made again)
        layersCompose(*app.ed);
    }
}

void mapLayersDragUp(AppState& app) {
    MapLayersState& s = gMapLayers;
    app.drag = Drag::None;
    if ((s.erasing || s.moving) && s.dragEd) layersDragEnd(*s.dragEd);
    app.dirty = true;
}

// Esc in a drag: the layers as they were.
void mapLayersDragCancel(AppState& app) {
    MapLayersState& s = gMapLayers;
    app.drag = Drag::None;
    if ((s.erasing || s.moving) && s.dragEd && s.dragStart) {
        const bool composed = s.dragChanged;
        s.stack = *s.dragStart;
        if (composed) layersCompose(*s.dragEd);
        s.dragChanged = false;
        layersDragEnd(*s.dragEd);
        if (composed) layersRemember(*app.ed);
    }
    app.dirty = true;
}

// The tab shows another map or the mode changes while a drag is on: it ends where it is.
void mapLayersDragStop() {
    MapLayersState& s = gMapLayers;
    if ((s.erasing || s.moving) && s.dragEd) layersDragEnd(*s.dragEd);
}

// --- Over the map: the edges, the active layer's outline ----------------------------------------------------------

static void layerBox(render::Scene& s, int x0, int y0, int x1, int y1, uint32_t rgba) {
    const float t = 2;
    const float l = float(x0), r = float(x1), u = float(y0), d = float(y1);
    for (const auto& b : {std::array<float, 4>{l, u, r, u + t}, {l, d - t, r, d}, {l, u, l + t, d}, {r - t, u, r, d}}) {
        render::RectDraw k;
        k.x0 = b[0], k.y0 = b[1], k.x1 = b[2], k.y1 = b[3], k.rgba = rgba, k.fill = true;
        s.rects.push_back(k);
    }
}

// The tiles of a layer: those of the rects round its corners, and its placed tiles.
static std::set<std::pair<int, int>> layerTileCells(const MapLayer& l) {
    std::set<std::pair<int, int>> tiles;
    for (const auto& [at, terrain] : l.corners)
        for (int ry = at.second - 1; ry <= at.second; ++ry)
            for (int rx = at.first - 1; rx <= at.first; ++rx) tiles.insert({rx * 2, ry}), tiles.insert({rx * 2 + 1, ry});
    for (const auto& [at, tile] : l.tiles) tiles.insert(at);
    return tiles;
}

// The outline of a layer's tiles (layerTileCells), in tile corners: the sides between a tile of the layer and the
// outside. The holes in it have none.
static std::vector<std::array<int, 4>> layerOutline(const MapLayer& l) {
    const std::set<std::pair<int, int>> tiles = layerTileCells(l);
    std::vector<std::array<int, 4>> out;
    if (tiles.empty()) return out;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& [x, y] : tiles) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    // The outside: all that the border of the box (a tile wider each way) reaches without going through the layer.
    --x0, --y0, ++x1, ++y1;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    std::vector<uint8_t> outside(size_t(w) * h, 0);
    std::vector<std::pair<int, int>> open{{x0, y0}};
    outside[0] = 1;
    while (!open.empty()) {
        const auto [x, y] = open.back();
        open.pop_back();
        for (const auto& n : {std::pair{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}}) {
            if (n.first < x0 || n.second < y0 || n.first > x1 || n.second > y1 || tiles.count(n)) continue;
            uint8_t& seen = outside[size_t(n.second - y0) * w + size_t(n.first - x0)];
            if (!seen) seen = 1, open.push_back(n);
        }
    }
    const auto isOut = [&](int x, int y) { return outside[size_t(y - y0) * w + size_t(x - x0)] != 0; };
    for (const auto& [x, y] : tiles) {
        if (isOut(x, y - 1)) out.push_back({x, y, x + 1, y});
        if (isOut(x, y + 1)) out.push_back({x, y + 1, x + 1, y + 1});
        if (isOut(x - 1, y)) out.push_back({x, y, x, y + 1});
        if (isOut(x + 1, y)) out.push_back({x + 1, y, x + 1, y + 1});
    }
    return out;
}

// --- The layers' terrain outside of the map --------------------------------------------------------------------------
// A layer's terrain can be outside of the map, up to 256 tiles from it: a place to try things out (Move Layer takes it
// there; units, doodads and sprites stay on the map). The map's file has none of it. The map view shows it on a grid larger than the map
// (sdl_main_view.inc), with tiles that a scratch map makes from the layers as the map makes them, and the view scrolls to
// it (clampCamera). Map layers panel > Settings > Show terrain outside of map boundaries (default value: off). Where no
// layer has terrain, the grid shows the view's background color (addMapLayersOverlay).
bool& mapLayersShowOutside() {
    static bool show = [] {
        bool v = false;
        if (!gSelfTest && !gSettingsOff) prefs::flag(L"LayersShowOutside", v);
        return v;
    }();
    return show;
}
void setMapLayersShowOutside(bool show) {
    mapLayersShowOutside() = show;
    if (!gSelfTest && !gSettingsOff) prefs::setNumber(L"LayersShowOutside", show ? 1 : 0);
    setMessage(show ? L"Map layers: terrain outside of map boundaries shows" : L"Map layers: terrain outside of map boundaries is hidden");
    shellState().dirty = true;
}

const LayersOutside& mapLayersOutside(const Editor& ed) {
    static LayersOutside out;
    static const Editor* forEd = nullptr;
    static uint64_t forRevision = ~0ull, forHash = 0, gen = 0;
    static bool forHide = false, forOn = false;
    const MapLayersState& m = gMapLayers;
    const bool hide = !mapLayersShowOutside(), on = m.on && m.ed == &ed;
    if (forEd == &ed && forRevision == ed.revision() && forHide == hide && forOn == on) return out;
    forEd = &ed, forRevision = ed.revision(), forHide = hide, forOn = on;
    const int W = ed.width(), H = ed.height();
    constexpr int kReach = kOutsideReach;
    // The visible layers with terrain outside of the map: a scan of their corners and tiles (no sets: this runs at each
    // edit), and a hash of their terrain (the scratch maps' work only when it changes).
    std::vector<const MapLayer*> layers;
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&](uint64_t v) { hash = (hash ^ v) * 1099511628211ull; };
    for (size_t i = 0; on && !hide && i < m.stack.layers.size(); ++i) {
        const MapLayer& l = m.stack.layers[i];
        if (!l.visible) continue;
        bool any = false;
        for (const auto& [at, t] : l.corners)  // (a corner's rects: tiles 2rx-2 to 2rx+1, rows ry-1 to ry)
            if (any = 2 * at.first - 2 < 0 || at.second - 1 < 0 || 2 * at.first + 1 >= W || at.second >= H; any) break;
        for (auto it = l.tiles.begin(); !any && it != l.tiles.end(); ++it)
            any = it->first.first < 0 || it->first.second < 0 || it->first.first >= W || it->first.second >= H;
        if (!any) continue;
        layers.push_back(&l);
        mix(l.id);
        for (const auto& [at, t] : l.corners) mix(uint64_t(uint32_t(at.first)) << 32 | uint32_t(at.second)), mix(t);
        for (const auto& [at, t] : l.tiles) mix(uint64_t(uint32_t(at.first)) << 32 | uint32_t(at.second)), mix(t | 0x10000);
    }
    mix(uint64_t(W) << 32 | uint32_t(H)), mix(ed.era() & 7);
    if (hash == forHash && out.any == !layers.empty()) return out;
    forHash = hash;
    LayersOutside next;
    if (!layers.empty()) {
        // The tiles outside of the map (as far as kReach), and their box.
        std::set<std::pair<int, int>> outside;
        const auto add = [&](int x, int y) {
            if ((x < 0 || y < 0 || x >= W || y >= H) && x >= -kReach && y >= -kReach && x < W + kReach && y < H + kReach) outside.insert({x, y});
        };
        for (const MapLayer* l : layers) {
            for (const auto& [at, t] : l->corners)
                for (int ry = at.second - 1; ry <= at.second; ++ry)
                    for (int x = 2 * at.first - 2; x <= 2 * at.first + 1; ++x) add(x, ry);
            for (const auto& [at, t] : l->tiles) add(at.first, at.second);
        }
        int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
        for (const auto& [x, y] : outside) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        if (!outside.empty()) next.x0 = x0, next.y0 = y0, next.w = x1 - x0 + 1, next.h = y1 - y0 + 1, next.tiles.assign(size_t(next.w) * next.h, 0xFFFF);
        // The tiles: in scratch maps of 256 x 256 tiles, each a part of the box with a margin round it (the edges there are
        // made as on the map), one for each ground terrain (made once). Placed tiles need no scratch map.
        constexpr int kSide = 256, kMargin = 4;  // (tiles; the margin: 2 rects)
        struct Scratch {
            std::unique_ptr<Editor> ed;
            Bytes base;
        };
        static std::map<std::pair<const GameData*, uint32_t>, Scratch> scratches;  // (by the game data and era << 16 | ground)
        static const bool released = (mapCacheReleases().push_back(+[] { scratches.clear(); }), true);
        (void)released;
        const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
        uint16_t mapGround = ed.terrainAt(16, 16);  // (no ISOM: the era's first terrain that has tiles, for placed tiles only)
        for (uint16_t t = 1; !mapGround && t < 64; ++t)
            if (solidTileGroup(ed.tileset(), t) >= 0) mapGround = t;
        constexpr int kStep = kSide - 2 * kMargin - 4;  // (a part's inside: its margin each way, a rect more for the grid's parity)
        for (int cy = y0; !outside.empty() && cy <= y1; cy += kStep)
            for (int cx = x0; cx <= x1; cx += kStep) {
                // (the part's scratch map: rects from rx0, rows from ry0, rx0 + ry0 even)
                int rx0 = floorDiv(cx - kMargin, 2), ry0 = cy - kMargin;
                if ((rx0 + ry0) & 1) --rx0;
                const int ix0 = cx, iy0 = cy, ix1 = std::min(x1, cx + kStep - 1), iy1 = std::min(y1, cy + kStep - 1);
                bool here = false;
                for (auto it = outside.lower_bound({ix0, INT_MIN}); !here && it != outside.end() && it->first <= ix1; ++it) here = it->second >= iy0 && it->second <= iy1;
                if (!here) continue;
                LayerStack stack;
                std::map<uint16_t, int> count;
                for (const MapLayer* l : layers) {
                    MapLayer c;
                    c.id = l->id;
                    for (const auto& [at, t] : l->corners)
                        if (at.first >= rx0 && at.first <= rx0 + kSide / 2 && at.second >= ry0 && at.second <= ry0 + kSide) c.corners[at] = t, ++count[t];
                    for (const auto& [at, t] : l->tiles)
                        if (at.first >= 2 * rx0 && at.first < 2 * rx0 + kSide && at.second >= ry0 && at.second < ry0 + kSide) c.tiles[at] = t;
                    if (c.corners.empty() && c.tiles.empty()) continue;
                    moveLayerContents(c, -rx0, -ry0);
                    stack.layers.push_back(std::move(c));
                }
                if (stack.layers.empty()) continue;
                uint16_t ground = mapGround;  // (the terrain the layers have most here; only placed tiles: the map's)
                int most = 0;
                for (const auto& [t, n] : count)
                    if (n > most) ground = t, most = n;
                // (a terrain that cannot fill a new map, as Solar Array, which lies on Platform: the map's ground)
                Scratch* scp = nullptr;
                for (const uint16_t g : {ground, mapGround}) {
                    if (!g) continue;
                    Scratch& sc = scratches[{&ed.gameData(), uint32_t(ed.era() & 7) << 16 | g}];
                    if (!sc.ed) {
                        sc.ed = std::make_unique<Editor>(ed.gameData());
                        Chk chk;
                        std::string err;
                        if (createNewScenario(chk, ed.gameData(), kSide, kSide, uint16_t(ed.era() & 7), g, err) && sc.ed->openChk(chk, err)) sc.base = sc.ed->layerBaseNow();
                    }
                    if (!sc.base.empty()) {  // (one that failed stays empty: not made again)
                        scp = &sc;
                        break;
                    }
                }
                if (!scp || !scp->ed->composeLayers(scp->base, stack)) continue;
                Scratch& sc = *scp;
                const TerrainLayer& t = sc.ed->terrain();
                for (auto it = outside.lower_bound({ix0, INT_MIN}); it != outside.end() && it->first <= ix1; ++it) {
                    const auto [x, y] = *it;
                    const int sx = x - 2 * rx0, sy = y - ry0;
                    if (y < iy0 || y > iy1 || sx < 0 || sy < 0 || sx >= t.width || sy >= t.height) continue;
                    next.tiles[size_t(y - y0) * size_t(next.w) + size_t(x - x0)] = t.tiles[size_t(sy) * t.width + size_t(sx)];
                    next.any = true;
                }
            }
    }
    if (next.any != out.any || next.x0 != out.x0 || next.y0 != out.y0 || next.w != out.w || next.h != out.h || next.tiles != out.tiles) {
        next.gen = ++gen;
        out = std::move(next);
    }
    return out;
}

// --- See-through layers (the view only) ------------------------------------------------------------------------------

int mapLayersOpacity(int row) {
    const MapLayersState& s = gMapLayers;
    if (row < 0 || row >= int(s.stack.layers.size())) return 100;
    const auto it = s.opacity.find(s.stack.layers[size_t(row)].id);
    return it == s.opacity.end() ? 100 : it->second;
}
void setMapLayersOpacity(int row, int percent) {
    MapLayersState& s = gMapLayers;
    if (row < 0 || row >= int(s.stack.layers.size())) return;
    percent = std::clamp(percent, 0, 100);
    if (percent == 100) s.opacity.erase(s.stack.layers[size_t(row)].id);
    else s.opacity[s.stack.layers[size_t(row)].id] = percent;
    shellState().dirty = true;
}

float mapLayersUnitAlpha(const Editor& ed, size_t unit) {
    const MapLayersState& s = gMapLayers;
    if (s.opacity.empty() || !s.on || s.ed != &ed || s.owners.units.size() != ed.units().size() || unit >= s.owners.units.size()) return 1;
    return mapLayersOpacity(s.owners.units[unit]) / 100.0f;
}

const LayersTranslucency& mapLayersTranslucency(const Editor& ed) {
    static LayersTranslucency out;
    static const Editor* forEd = nullptr;
    static uint64_t forTerrain = ~0ull, gen = 0;
    static std::vector<uint32_t> forClear;  // (the see-through layers' ids)
    const MapLayersState& s = gMapLayers;
    const bool on = s.on && s.ed == &ed && !s.filesPending;
    // (the visible layers that are see-through)
    std::vector<size_t> clear;
    std::vector<uint32_t> clearIds;
    for (size_t i = 0; on && i < s.stack.layers.size(); ++i)
        if (s.stack.layers[i].visible && mapLayersOpacity(int(i)) < 100) clear.push_back(i), clearIds.push_back(s.stack.layers[i].id);
    // The same terrain and the same see-through layers: only their opacity can have changed (the rects' alpha). A
    // selection, a slider's step: nothing is made again.
    if (forEd == &ed && forTerrain == ed.terrainRevision() && forClear == clearIds) {
        bool changed = false;
        for (auto& r : out.rects)
            if (const float a = mapLayersOpacity(int(r.layer)) / 100.0f; a != r.alpha) r.alpha = a, changed = true;
        if (changed) out.gen = ++gen;
        return out;
    }
    forEd = &ed, forTerrain = ed.terrainRevision(), forClear = clearIds;
    LayersTranslucency next;
    if (clear.empty()) {  // (none: the same as before, no new upload)
        if (out.any) out = LayersTranslucency{}, out.gen = ++gen;
        return out;
    }
    next.gen = ++gen;
    if (!clear.empty()) {
        // The map without them: the layers made again into a copy of the map (one for each map of a size and tileset).
        static std::unique_ptr<Editor> scratch;
        static std::tuple<const Editor*, const GameData*, uint64_t, int, int, int> scratchFor{};
        static const bool released = (mapCacheReleases().push_back(+[] { scratch.reset(), scratchFor = {}; }), true);
        (void)released;
        // (the map: its editor, its game data (a mod's tab has its own) and its load; its size and tileset)
        const std::tuple<const Editor*, const GameData*, uint64_t, int, int, int> key{&ed, &ed.gameData(), ed.loadMark(), ed.width(), ed.height(), ed.era()};
        bool ready = scratch && scratchFor == key;
        if (!ready) {
            scratch = std::make_unique<Editor>(ed.gameData());
            Chk chk = ed.chk();
            std::string err;
            ready = scratch->openChk(chk, err);
            scratchFor = ready ? key : decltype(scratchFor){};
        }
        LayerStack without = s.stack;
        for (size_t i : clear) without.layers[i].visible = false;
        if (ready && scratch->composeLayers(s.base, without)) {
            next.any = true;
            next.without = scratch->terrain();
            // Each see-through layer's tiles on the map, as rects: the runs of a row, joined with the same runs below.
            const int W = ed.width(), H = ed.height();
            for (size_t i : clear) {
                const float alpha = mapLayersOpacity(int(i)) / 100.0f;
                std::map<int, std::vector<int>> rows;
                for (const auto& [x, y] : layerTileCells(s.stack.layers[i]))
                    if (x >= 0 && y >= 0 && x < W && y < H) rows[y].push_back(x);
                std::map<std::pair<int, int>, size_t> open;  // (x0, x1) -> the rect that ends at the row above
                for (auto& [y, xs] : rows) {
                    std::sort(xs.begin(), xs.end());
                    std::map<std::pair<int, int>, size_t> now;
                    for (size_t k = 0; k < xs.size();) {
                        size_t e = k + 1;
                        while (e < xs.size() && xs[e] == xs[e - 1] + 1) ++e;
                        const std::pair<int, int> run{xs[k], xs[e - 1] + 1};
                        if (const auto it = open.find(run); it != open.end() && next.rects[it->second].y1 == y) {
                            next.rects[it->second].y1 = y + 1, now[run] = it->second;
                        } else {
                            next.rects.push_back({run.first, y, run.second, y + 1, alpha, i}), now[run] = next.rects.size() - 1;
                        }
                        k = e;
                    }
                    open = std::move(now);
                }
            }
        }
    }
    out = std::move(next);
    return out;
}

// The box of the layers' terrain outside of the map, in tiles (x1, y1 past it); false: none (or it is hidden).
bool mapLayersOutsideBox(const Editor& ed, int& x0, int& y0, int& x1, int& y1) {
    const LayersOutside& o = mapLayersOutside(ed);
    if (!o.any) return false;
    x0 = o.x0, y0 = o.y0, x1 = o.x0 + o.w, y1 = o.y0 + o.h;
    return true;
}

void addMapLayersOverlay(const Editor& ed, render::Scene& s) {
    const MapLayersState& m = gMapLayers;
    if (shellState().drag == Drag::TerrainLasso && gLassoPoints.size() > 1) {  // (Lasso: the line drawn so far, closed)
        for (size_t i = 0; i < gLassoPoints.size(); ++i) {
            const auto& a = gLassoPoints[i];
            const auto& b = gLassoPoints[(i + 1) % gLassoPoints.size()];
            render::RectDraw d;
            d.x0 = a.first, d.y0 = a.second, d.x1 = b.first, d.y1 = b.second, d.thickness = 2, d.line = true;
            d.rgba = i + 1 == gLassoPoints.size() ? 0xFFFFFF80u : 0xFFFFFFFFu;
            s.rects.push_back(d);
        }
    }
    if (const LayersOutside& o = mapLayersOutside(ed); o.any) {
        // The larger grid's tiles with no terrain (not the map's, no layer's): the view's background color (the terrain
        // shader's, past the grid), in runs of a row.
        const int W = ed.width(), H = ed.height();
        static std::vector<render::RectDraw> runs;  // (made again when the terrain outside or the map's size changes)
        static uint64_t runsGen = 0;
        static int runsW = 0, runsH = 0;
        if (runsGen != o.gen || runsW != W || runsH != H) {
            runsGen = o.gen, runsW = W, runsH = H, runs.clear();
            const int gx0 = std::min(0, o.x0), gy0 = std::min(0, o.y0), gx1 = std::max(W, o.x0 + o.w), gy1 = std::max(H, o.y0 + o.h);
            const auto empty = [&](int x, int y) {
                if (x >= 0 && y >= 0 && x < W && y < H) return false;
                const int ox = x - o.x0, oy = y - o.y0;
                return ox < 0 || oy < 0 || ox >= o.w || oy >= o.h || o.tiles[size_t(oy) * size_t(o.w) + size_t(ox)] == 0xFFFF;
            };
            for (int y = gy0; y < gy1; ++y)
                for (int x = gx0; x < gx1;) {
                    if (y >= 0 && y < H && x >= 0 && x < W) {  // (the map's row: past it at once)
                        x = W;
                        continue;
                    }
                    if (!empty(x, y)) {
                        ++x;
                        continue;
                    }
                    int e = x + 1;
                    while (e < gx1 && empty(e, y) && !(y >= 0 && y < H && e == 0)) ++e;
                    render::RectDraw k;
                    k.x0 = x * 32.0f, k.y0 = y * 32.0f, k.x1 = e * 32.0f, k.y1 = (y + 1) * 32.0f, k.rgba = 0x121214FFu, k.fill = true;
                    runs.push_back(k);
                    x = e;
                }
        }
        s.underRects.insert(s.underRects.end(), runs.begin(), runs.end());  // (under the sprites: art over the edge shows)
        layerBox(s, 0, 0, W * 32, H * 32, 0xFFFFFF50u);  // (terrain outside: the map's edge)
    }
    const AppState& app = shellState();
    const bool here = app.mode == Mode::Layers;
    if (!m.on || m.ed != &ed || (!here && !gFloatPal[int(Mode::Layers)].floating)) return;  // (in the mode, or with its window)
    // The active layer's outline: round its outermost tiles, only. With only objects: a box.
    if (m.active >= 0 && m.active < int(m.stack.layers.size())) {
        const MapLayer& l = m.stack.layers[size_t(m.active)];
        const uint32_t edge = 0x60D0FFFFu;
        static const Editor* forEd = nullptr;
        static uint64_t forRevision = 0;
        static uint32_t forLayer = 0;
        static std::vector<std::array<int, 4>> lines;  // tile corners (made again when the layer or the map changes)
        if (forEd != &ed || forRevision != ed.revision() || forLayer != l.id) {
            forEd = &ed, forRevision = ed.revision(), forLayer = l.id;
            lines = layerOutline(l);
        }
        for (const auto& [x0, y0, x1, y1] : lines) {
            render::RectDraw d;
            d.x0 = x0 * 32.0f, d.y0 = y0 * 32.0f, d.x1 = x1 * 32.0f, d.y1 = y1 * 32.0f, d.rgba = edge, d.thickness = 2, d.line = true;
            s.rects.push_back(d);
        }
        if (l.corners.empty() && l.tiles.empty())
            if (const LayerBounds b = layerBounds(l); b.any) layerBox(s, b.left, b.top, b.right, b.bottom, edge);
}
}
