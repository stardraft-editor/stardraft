// Map layers, a prototype (stardraft/layers.h; the panel: views_layers.inc). The active layer takes what the map's tools
// do: after each edit (the frame's sync), the ISOM corners, placed tiles and objects that changed go into it, and the map
// is flattened again, with the terrain fitted to the layers. The map's sections always are the flattened result. Erase
// on a layer takes corners out of it (NULL: see-through); the Layers tool moves contents. Each change is one undo step
// with the layers' files.

struct MapLayersState {
    const Editor* ed = nullptr;
    uint64_t loadMark = 0;  // Editor::loadMark when the map was last taken as it is
    bool filesPending = false;  // the layers' files are still to be read
    bool on = false;
    LayerStack stack;
    Bytes base;
    int active = -1;       // -1: the Background; else the index in stack.layers
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
    std::vector<std::pair<int, int>> fitted;
    // A drag on the map.
    Editor* dragEd = nullptr;
    bool erasing = false, moving = false, dragChanged = false;
    int startX = 0, startY = 0, moveDX = 0, moveDY = 0, nowX = 0, nowY = 0;
    std::map<int, MapLayer> moveFrom;
    std::optional<LayerStack> dragStart;  // the stack before the drag (Esc)
} gMapLayers;

void layersPanelChanged();    // (views_layers.inc)

static void layerCornerGrid(const Editor& ed, std::vector<uint16_t>& out) {
    out.clear();
    const IsomGrid& g = ed.isomGrid();
    for (int y = -1; y <= int(ed.height()) + 1; ++y)
        for (int x = -1; x <= int(ed.width()) / 2 + 1; ++x) out.push_back(g.terrainAt(x, y));
}

static LayerObjectLists layerObjectsOf(const Editor& ed) { return {ed.units(), ed.doodads(), ed.layerSprites()}; }

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
    s.fitted.clear();
    ed.noteAskedCorners(s.on);
    if (!s.on) {
        s.stack = {}, s.base.clear(), s.active = -1, s.picked.clear();
        return;
    }
    s.base = base->data;
    if (s.active >= int(s.stack.layers.size())) s.active = int(s.stack.layers.size()) - 1;
    std::erase_if(s.picked, [&](int i) { return i >= int(s.stack.layers.size()); });
    std::set<std::pair<int, int>> asked;
    for (const MapLayer& l : s.stack.layers)
        if (l.visible)
            for (const auto& [at, terrain] : l.corners) asked.insert(at);
    const IsomGrid& grid = ed.isomGrid();
    for (const auto& [x, y] : asked)
        for (const auto& beside : {std::pair{x - 1, y - 1}, {x + 1, y - 1}, {x - 1, y + 1}, {x + 1, y + 1}})
            if (!asked.count(beside) && grid.isCorner(beside.first, beside.second)) s.fitted.push_back(beside);
    s.owners = s.stack.owners();
    std::set<std::pair<int, int>> placed;
    for (const MapLayer& l : s.stack.layers)
        for (const auto& [at, tile] : l.tiles) placed.insert(at);
    ed.noteLayerPlaced(std::move(placed));
    layersPanelChanged();
}

// The map from the Background and the layers; the edges show from it.
static void layersCompose(Editor& ed) {
    MapLayersState& s = gMapLayers;
    ed.composeLayers(s.base, s.stack, &s.fitted);
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
    ed.amendLastStep({}, {kLayersFile, kLayersBaseFile});
    s.active = -1;
    layersRead(ed);
    setMessage(L"The map's size or tileset changed: the layers are flattened. Undo brings them back");
}

// The contents of the layers that are locked, to compare them.
static Bytes layerLockedBytes(const LayerStack& stack) {
    LayerStack only;
    if (stack.background.locked) only.background = stack.background;
    for (const MapLayer& l : stack.layers)
        if (l.locked) only.layers.push_back(l);
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

// What the last edit changed goes into the active layer.
void mapLayersSync() {
    AppState& app = shellState();
    MapLayersState& s = gMapLayers;
    if (!app.ed || !app.ed->isOpen()) return;
    Editor& ed = *app.ed;
    if (s.ed != &ed || ed.loadMark() != s.loadMark) {  // another map, or an undo or a redo: the map is as the step left it
        if (s.ed != &ed) {
            s.active = -1, s.picked.clear();
            if (const auto it = s.activeOf.find(ed.path()); it != s.activeOf.end()) s.active = it->second, s.picked = {s.active};
        }
        s.ed = &ed, s.loadMark = ed.loadMark();
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
    if (!s.on || ed.inStroke() || ed.revision() == s.revision) return;  // (no layers: nothing to compare)
    if (ed.width() != s.width || ed.height() != s.height || ed.era() != s.era) return layersFlattenForResize(ed);
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
                   return p.id == q.id && p.x == q.x && p.y == q.y && p.owner == q.owner && p.disabled == q.disabled;
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
    // (a brush over the same terrain: the layer has it now; a tool that changed no tile takes no undo step)
    if (s.active >= 0)
        for (const auto& [x, y] : ed.takeAskedCorners())
            if (const uint16_t terrain = ed.isomGrid().terrainAt(x, y)) changed[{x, y}] = terrain;
    const bool stepTaken = ed.undoSteps() != s.undoSteps;
    const bool kitTool = app.mode == Mode::Terrain && (currentTool(app) == TBridge || currentTool(app) == TRamp);
    const std::vector<uint16_t> nowSection = ed.layerTileSection().tiles;
    const std::vector<uint8_t> notTerrain = ed.tilesNotFromIsom();
    if (ed.terrain().tiles.size() == s.tiles.size() && nowSection.size() == s.tileSection.size() && nowSection.size() == s.tiles.size()) {
        const int tw = int(ed.width());
        const std::vector<uint16_t>& nowTiles = ed.terrain().tiles;
        for (size_t k = 0; k < nowTiles.size(); ++k) {
            // (under the doodads: a tile placed where a doodad goes away too; a doodad's own tiles are not there)
            if (doodadsChanged ? nowSection[k] == s.tileSection[k] : nowTiles[k] == s.tiles[k]) continue;
            if (kitTool) {  // (a bridge's or a ramp's tiles are its own, also beside the corners it changes; not the terrain's)
                if (k < notTerrain.size() && notTerrain[k]) placed[{int(k) % tw, int(k) / tw}] = nowTiles[k];
                continue;
            }
            const int tx = int(k) % tw, ty = int(k) / tw, rx = tx / 2;
            bool nearChange = false;  // (an edge's tiles depend on the corners around them too)
            for (int cy = ty - 3; !nearChange && cy <= ty + 4; ++cy)
                for (int cx = rx - 3; !nearChange && cx <= rx + 4; ++cx) nearChange = changed.count({cx, cy}) > 0;
            if ((!nearChange && k < notTerrain.size() && notTerrain[k]) || !nowTiles[k]) placed[{tx, ty}] = nowTiles[k];  // (not the terrain's other variant; a null tile: a cut)
        }
    }
    if (changed.empty() && placed.empty() && !objectsChanged) {
        s.revision = ed.revision();
        return;
    }
    // A bridge or a ramp stands on its ground: on a layer, only where that layer has terrain.
    if (kitTool && s.active >= 0 && s.active < int(s.stack.layers.size()) && stepTaken) {
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
    // A locked layer does not change: the edit goes back.
    if (const MapLayer& activeLayer = s.active >= 0 && s.active < int(s.stack.layers.size()) ? s.stack.layers[size_t(s.active)] : s.stack.background;
        (activeLayer.locked || (s.active < 0 && !activeLayer.visible)) && (!changed.empty() || !placed.empty())) {
        if (stepTaken) ed.refuseLastEdit();
        setMessage(activeLayer.locked ? L"The layer is locked: nothing changed" : L"The Background is hidden: nothing changed");
        return;
    }
    if (objectsChanged) {
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
    if (!stepTaken) ed.beginLayerEdit();
    layersCompose(ed);
    layersKeepSelections(ed, objects);
    if (stepTaken) ed.amendLastStep(layersFiles(baseChanged));
    else ed.endLayerEdit(true, layersFiles(baseChanged));
    layersRemember(ed);
    layersPanelChanged();
}

bool mapLayersOn() { return gMapLayers.on; }
bool mapLayersOnLayer() { return gMapLayers.on && gMapLayers.active >= 0; }
bool mapLayersActiveLocked() {
    const MapLayersState& s = gMapLayers;
    return s.on && (s.active >= 0 && s.active < int(s.stack.layers.size()) ? s.stack.layers[size_t(s.active)] : s.stack.background).locked;
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
    shellInfoBox(L"Layers stay in this map file while you work on them.\n\nTo make the map that you play in StarCraft, use File > Export map. "
                 L"All layers that show become one map, and the layer data stays out of it.",
                 L"Map Layers");
}

// The map becomes the Background: its terrain in its file, its objects in the stack.
static void layersStart(Editor& ed) {
    MapLayersState& s = gMapLayers;
    if (s.on) return;
    s.base = ed.layerBaseNow(), s.on = true;
    s.stack = {};
    for (const Unit& u : ed.units()) s.stack.background.units.push_back({u, int(u.x), int(u.y), true});
    for (const Doodad& d : ed.doodads()) s.stack.background.doodads.push_back({d, int(d.x), int(d.y), true});
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
    layersEdit([row](Editor& ed) {
        MapLayersState& s = gMapLayers;
        if (row >= int(s.stack.layers.size())) return false;
        layersStart(ed);
        MapLayer& l = row >= 0 ? s.stack.layers[size_t(row)] : s.stack.background;
        l.locked = !l.locked;
        return true;
    });
}

void mapLayersDuplicate(int row) {
    layersEdit([row](Editor&) {
        MapLayersState& s = gMapLayers;
        if (row < 0 || row >= int(s.stack.layers.size())) return false;
        MapLayer copy = s.stack.layers[size_t(row)];
        copy.id = s.stack.nextId++;
        copy.name += " copy";
        copy.locked = false;
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

// The selection's tiles of the active layer become NULL (Delete in Terrain mode). False: not on a layer.
bool mapLayersDeleteSelection(const std::vector<std::pair<int, int>>& tiles) {
    MapLayersState& s = gMapLayers;
    if (!s.on || s.active < 0 || s.active >= int(s.stack.layers.size()) || tiles.empty()) return false;
    if (s.stack.layers[size_t(s.active)].locked) return setMessage(L"The layer is locked: nothing changed"), true;
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
void mapLayersCurrentHide() { mapLayersToggleVisible(gMapLayers.active); }
void mapLayersCurrentLock() { mapLayersToggleLock(gMapLayers.active); }
void mapLayersCurrentRename() {
    if (layersHaveCurrent()) mapLayersRename(gMapLayers.active);
}
void mapLayersCurrentDuplicate() {
    if (layersHaveCurrent()) mapLayersDuplicate(gMapLayers.active);
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

static void layersEraseAt(Editor& ed, const Brush& brush, int wx, int wy) {
    MapLayersState& s = gMapLayers;
    MapLayer& layer = s.stack.layers[size_t(s.active)];
    bool any = false;
    for (const auto& at : ed.isomGrid().brushCorners(wx, wy, brush)) {
        any |= layer.corners.erase(at) > 0;
        // (the placed tiles of the rects around the corner)
        for (int ty = at.second - 1; ty <= at.second; ++ty)
            for (int tx = (at.first - 1) * 2; tx <= at.first * 2 + 1; ++tx) any |= layer.tiles.erase({tx, ty}) > 0;
    }
    if (any) s.dragChanged = true, layersCompose(ed);
}

void mapLayersEraseDown(AppState& app, float wx, float wy) {
    MapLayersState& s = gMapLayers;
    mapLayersSync();
    if (s.active < 0 || s.active >= int(s.stack.layers.size())) return;  // (the sync read an undo: no layer now)
    if (s.stack.layers[size_t(s.active)].locked) return setMessage(L"The layer is locked");
    app.selDoodads.clear(), app.selSprites.clear(), app.ed->clearSelection();  // (the lists change in the drag)
    app.ed->beginLayerEdit();
    s.dragEd = app.ed.get();
    s.dragStart = s.stack;
    s.erasing = true, s.dragChanged = false;
    layersEraseAt(*app.ed, currentBrush(app), int(wx), int(wy));
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
    if (s.moveFrom.empty()) return setMessage(s.active >= 0 ? L"The layer is locked" : L"The Background does not move. Select a layer in the list");
    app.drag = Drag::LayerTool;
    app.selDoodads.clear(), app.selSprites.clear(), app.ed->clearSelection();  // (the lists change in the drag)
    app.ed->beginLayerEdit();
    s.dragEd = app.ed.get();
    s.dragStart = s.stack;
    s.moving = true, s.dragChanged = false;
}

void mapLayersDragMove(AppState& app, float wx, float wy) {
    MapLayersState& s = gMapLayers;
    if (s.erasing) {
        layersEraseAt(*app.ed, currentBrush(app), int(wx), int(wy));
    } else if (s.moving) {
        int dx, dy;
        snapLayerMove(int(wx) - s.startX, int(wy) - s.startY, dx, dy);
        if (dx == s.moveDX && dy == s.moveDY) return;
        s.moveDX = dx, s.moveDY = dy;
        for (const auto& [r, from] : s.moveFrom) {
            s.stack.layers[size_t(r)] = from;
            moveLayerContents(s.stack.layers[size_t(r)], dx, dy);
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

void addMapLayersOverlay(const Editor& ed, render::Scene& s) {
    const MapLayersState& m = gMapLayers;
    if (shellState().drag == Drag::TerrainMove && shellState().hasTerrainSel) {  // (Terrain's Move: where the selection goes)
        const Rect& r = shellState().terrainSel;
        int dx = gTerrainMoveDX, dy = gTerrainMoveDY;
        if (!shellState().terrainRect) {
            int rx, ry;
            snapLayerMove(dx, dy, rx, ry);
            dx = rx * 64, dy = ry * 32;
        } else {
            dx = int(std::lround(dx / 32.0)) * 32, dy = int(std::lround(dy / 32.0)) * 32;
        }
        layerBox(s, r.left + dx, r.top + dy, r.right + dx, r.bottom + dy, 0xFFFFFFE0u);
    }
    const AppState& app = shellState();
    const bool here = app.mode == Mode::Layers;
    if (!m.on || m.ed != &ed || (!here && !gFloatPal[int(Mode::Layers)].floating)) return;  // (in the mode, or with its window)
    // The edges: where the terrain is fitted to a layer or to what is under its NULL corners: a faint diamond at each.
    const auto line = [&s](float x0, float y0, float x1, float y1, uint32_t rgba, float thick) {
        render::RectDraw l;
        l.x0 = x0, l.y0 = y0, l.x1 = x1, l.y1 = y1, l.rgba = rgba, l.thickness = thick, l.line = true;
        s.rects.push_back(l);
    };
    for (const auto& [x, y] : m.fitted) {
        const float cx = x * 64.0f, cy = y * 32.0f, hw = 32, hh = 16;
        const uint32_t amber = 0xFFD04070u;
        line(cx - hw, cy, cx, cy - hh, amber, 1), line(cx, cy - hh, cx + hw, cy, amber, 1);
        line(cx + hw, cy, cx, cy + hh, amber, 1), line(cx, cy + hh, cx - hw, cy, amber, 1);
    }
    // The active layer's outline: round its diamonds and its placed tiles, as the layer's shape is. With only objects: a box.
    if (m.active >= 0 && m.active < int(m.stack.layers.size())) {
        const MapLayer& l = m.stack.layers[size_t(m.active)];
        const uint32_t edge = 0x60D0FFFFu;
        // (a corner's diamond: its four sides are shared with the corners diagonally beside it)
        for (const auto& [at, terrain] : l.corners) {
            const auto [x, y] = at;
            const float cx = x * 64.0f, cy = y * 32.0f, hw = 64, hh = 32;
            if (!l.corners.count({x - 1, y - 1})) line(cx - hw, cy, cx, cy - hh, edge, 2);
            if (!l.corners.count({x + 1, y - 1})) line(cx, cy - hh, cx + hw, cy, edge, 2);
            if (!l.corners.count({x + 1, y + 1})) line(cx + hw, cy, cx, cy + hh, edge, 2);
            if (!l.corners.count({x - 1, y + 1})) line(cx, cy + hh, cx - hw, cy, edge, 2);
        }
        for (const auto& [at, tile] : l.tiles) {
            const auto [x, y] = at;
            const float x0 = x * 32.0f, y0 = y * 32.0f, x1 = x0 + 32, y1 = y0 + 32;
            if (!l.tiles.count({x, y - 1})) line(x0, y0, x1, y0, edge, 2);
            if (!l.tiles.count({x, y + 1})) line(x0, y1, x1, y1, edge, 2);
            if (!l.tiles.count({x - 1, y})) line(x0, y0, x0, y1, edge, 2);
            if (!l.tiles.count({x + 1, y})) line(x1, y0, x1, y1, edge, 2);
        }
        if (l.corners.empty() && l.tiles.empty())
            if (const LayerBounds b = layerBounds(l); b.any) layerBox(s, b.left, b.top, b.right, b.bottom, edge);
    }
}
