// A snapshot's input lines (sdl_menus.inc: sdlInputLine): after each --key, --type, --click, --drag, --move or --wait,
// what the workspace shows holds now, as one line of text that tools/sdl_snapshots.py's CHECKS read (the Windows
// self-test reads the same parts of the model): the open map (its tileset and size; then, in the Map workspace, its
// parts as name=value: shotMapState); the Data workspace's open entry, its changed
// values and the undo steps; the selected trigger's blocks with their values; the Classic triggers page's triggers and
// the selected one's players; the Map data page's entry, rows and values; the Files page's search and how many game sounds it shows; the map's briefings; the New
// page's choices and its preview's maps. Included
// by main_sdl.cpp after the workspaces' files.

namespace {

// A trigger block as text: its id and its values, with the blocks it holds ("act.setresources(P1,1,50,0)").
std::string shotBlockText(const TriggerCall& b) {
    std::string s = b.def + "(";
    for (size_t i = 0; i < b.args.size(); ++i) s += (i ? "," : "") + b.args[i];
    s += ")";
    for (const auto& list : b.lists) {
        s += "{";
        for (size_t i = 0; i < list.size(); ++i) s += (i ? "; " : "") + shotBlockText(list[i]);
        s += "}";
    }
    return s;
}

std::string shotDataState() {
    DataModel& d = dataState();
    const DatTable* t = curTable();
    if (!t) return "data: no table";
    const int entry = d.datTab >= 0 && size_t(d.datTab) < d.tabEntry.size() ? d.tabEntry[size_t(d.datTab)] : 0;
    std::string s = "data: " + t->schema().name + " " + std::to_string(entry) + ":";
    int changed = 0;
    for (const DatField& f : t->schema().fields)
        if (f.covers(entry) && t->changed(f, entry))
            s += " " + f.name + "=" + std::to_string(t->value(f, entry)) + " (stock " + std::to_string(t->stock(f, entry)) + ")", ++changed;
    if (!changed) s += " no changed values";
    s += "; " + std::to_string(d.history.undo.size()) + " undo steps, " + std::to_string(d.history.redo.size()) + " redo steps";
    if (t->schema().name == "weapons") s += "; damage table armor " + std::to_string(datapics::gDamageArmor);
    if (t->schema().name == "sfxdata")  // (the list's selected entry, and the sounds asked for)
        s += "; list entry " + std::to_string(gSdlDataState.entry) + "; " + std::to_string(gSoundAsks) + " plays, last \"" + gSoundAsked + "\"";
    // The diagrams' handles (small squares) and how many of them the mouse cannot drag (no drag there: WRONG).
    int still = 0;
    for (const draw::Point& p : datapics::gHandles) {
        bool drags = false;
        for (const auto& a : gSdlDataFormHost.areas) drags = drags || (a.drag && a.r.contains(p));
        still += !drags;
    }
    if (!datapics::gHandles.empty()) s += "; " + std::to_string(datapics::gHandles.size()) + " handles, " + std::to_string(still) + " not dragged";
    return s;
}

std::string shotTriggersState() {
    const TriggerNode* n = trigSelectedNode();
    if (!n) return "triggers: nothing selected";
    std::string s = "triggers: \"" + n->name + "\"";
    if (n->kind != TriggerNode::Kind::Trigger) return s + " (" + std::to_string(n->children.size()) + " in it)";
    auto list = [&](const char* what, const std::vector<TriggerCall>& blocks) {
        s += std::string("; ") + std::to_string(blocks.size()) + " " + what + ":";
        for (const TriggerCall& b : blocks) s += " " + shotBlockText(b);
    };
    list("conditions", n->conditions);
    list("actions", n->actions);
    return s;
}

std::string shotClassicState() {
    if (!gSdlTrigPage) return "classic triggers: no page";
    const CtModel& m = gSdlTrigPage->model();
    auto players = [](const ClassicTrigger& t) {
        std::string p;
        for (size_t g = 0; g < t.players.size(); ++g)
            if (t.players[g]) p += " " + std::to_string(g < 8 ? g + 1 : g);  // (players 1-8; 17 all players, 18-21 forces)
        return p.empty() ? std::string(" none") : p;
    };
    // The page's triggers, the selected one, and the map's triggers (each one's players: the page's change is in the map).
    std::string s = "classic triggers: the page has " + std::to_string(m.trig.triggers.size()) + ", selected " + std::to_string(m.sel + 1);
    if (m.sel >= 0 && size_t(m.sel) < m.trig.triggers.size()) {
        const ClassicTrigger& t = m.trig.triggers[size_t(m.sel)];
        s += " (players" + players(t) + "; " + std::to_string(t.conditionCount()) + " conditions, " + std::to_string(t.actionCount()) + " actions" +
             (t.disabled() ? "; off)" : ")");
    }
    const ClassicTriggers inMap = gState.ed->classicTriggers();
    s += "; the map has " + std::to_string(inMap.triggers.size()) + ":";
    for (const ClassicTrigger& t : inMap.triggers) s += " [players" + players(t) + (t.disabled() ? "; off]" : "]");
    return s;
}

// The Map data page: the tab and its selected entry, the list's rows (how many, the pinned ones, the first ones, the
// Changed filter), the entry's values on the page (own: not the game's) and in the map, and the map's undo steps.
std::string shotMapDataState() {
    ClassicPageView* v = sdlClassicShownTab();
    if (!v) return "map data: no page";
    const int tab = gClassic->tab, e = v->entry();
    std::string s = "map data: " + narrow(kClassicTabNames[tab]) + " " + std::to_string(e) + "; " + std::to_string(v->shownCount()) + " rows, " +
                    std::to_string(v->pinnedShown()) + " pinned, changed filter " + (v->changedOnly() ? "on" : "off") + ", first";
    for (int i = 0; i < v->shownCount() && i < 4; ++i) s += " " + std::to_string(v->shownEntry(i));
    const bool own = !classicUseDefault(tab, e);
    s += std::string("; ") + (own ? "own" : "default") + " values";
    for (int f = 0; f < classicFieldCount(tab); ++f) s += " " + std::to_string(classicFieldValue(tab, e, f, !own));
    // The map's: its settings as the map has them now (the page's change is in the map).
    const Editor& ed = *gState.ed;
    std::string inMap;
    const size_t i = size_t(e);
    if (tab == kClassicUnits) {
        const UnitSettings::Unit& u = ed.unitSettings().units[i];
        inMap = (u.useDefault ? "default " : "own ") + std::to_string(u.hitPoints >> 8) + " " + std::to_string(u.shields) + " " + std::to_string(u.armor) +
                " " + std::to_string(u.buildTime) + " " + std::to_string(u.minerals) + " " + std::to_string(u.gas);
    } else if (tab == kClassicUpgrades) {
        const UpgradeSettings::Upgrade& g = ed.upgradeSettings().upgrades[i];
        inMap = (g.useDefault ? "default " : "own ") + std::to_string(g.minerals) + " " + std::to_string(g.gas) + " " + std::to_string(g.time);
    } else {
        const TechSettings::Tech& t = ed.techSettings().techs[i];
        inMap = (t.useDefault ? "default " : "own ") + std::to_string(t.minerals) + " " + std::to_string(t.gas) + " " + std::to_string(t.time);
    }
    return s + "; the map has " + inMap + "; undo " + std::to_string(ed.undoSteps());
}

std::string shotBriefingState() {
    const auto b = gState.ed->briefings();
    std::string s = "briefing: " + std::to_string(b.size()) + " briefings";
    for (const BriefingTrigger& t : b) {
        s += "; " + std::to_string(t.actions.size()) + " actions:";
        for (const BriefingAction& a : t.actions) s += " " + std::to_string(int(a.type));
    }
    return s;
}

// The open map (its tileset and size), then its parts that the self-test's log reads, as name=value (a value with
// spaces in quotes; "-" where there is none): the mode, the placement's player, the first selected unit's owner, the
// last unit's owner, the palette's pick and the tool, the sprites, the selected doodads and the first one's place, the
// locations and the selected one, the paste (units: how many of the ghost's units are red), the picked doodad's ghost,
// the terrain (a hash of its tiles, the tile under the mouse, the terrain selection), the tabs, the quick picker and the
// last unit's place.
std::string shotMapState() {
    const AppState& app = gState;
    Editor& ed = *app.ed;
    auto quoted = [](const std::string& s) { return "\"" + s + "\""; };
    auto box = [](const Rect& r) { return std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom); };
    std::string s = "map: " + narrow(kTilesetNames[ed.era() & 7]) + " " + std::to_string(ed.width()) + " x " + std::to_string(ed.height());
    s += "; mode=" + narrow(kModeNames[int(app.mode)]) + " player=" + std::to_string(app.playerOwner + 1);
    const auto& sel = ed.selection();
    s += " owner=" + (!sel.empty() && sel[0] >= 0 && size_t(sel[0]) < ed.units().size() ? std::to_string(int(ed.units()[size_t(sel[0])].owner) + 1) : std::string("-"));
    s += " lastowner=" + (!ed.units().empty() ? std::to_string(int(ed.units().back().owner) + 1) : std::string("-"));
    s += " palette=" + std::to_string(app.palette) + " tool=" + std::to_string(currentTool());
    s += " sprites=" + std::to_string(ed.sprites().size()) + " spritesel=" + std::to_string(app.selSprites.size());
    s += " doodadsel=" + std::to_string(app.selDoodads.size()) + " doodad=";
    if (!app.selDoodads.empty() && app.selDoodads[0] >= 0 && size_t(app.selDoodads[0]) < ed.doodads().size()) {
        const Doodad& d = ed.doodads()[size_t(app.selDoodads[0])];
        s += std::to_string(d.x) + "," + std::to_string(d.y);
    } else s += "-";
    int used = 0;
    for (int i = 0; i < ed.locationCount(); ++i) used += ed.locationUsed(i);
    const int loc = ed.selectedLocation();
    s += " locations=" + std::to_string(used) + " location=";
    s += loc >= 0 && ed.locationUsed(loc) ? std::to_string(loc) + ":" + box(ed.locationRect(loc)) + " name=" + quoted(ed.locationName(loc)) : "- name=-";
    // The selected location's edges as it has them (an inverted one: left more than right, or top more than bottom), and
    // Location properties' type line while it is open.
    s += " edges=" + (loc >= 0 && ed.locationUsed(loc) ? box(ed.locationEdges(loc)) : std::string("-"));
    // The selected locations (several: they move, copy and delete together) and the location list's selected rows.
    auto ints = [](const std::vector<int>& v) {
        std::string t;
        for (int i : v) t += (t.empty() ? "" : ",") + std::to_string(i);
        return t.empty() ? std::string("-") : t;
    };
    s += " locsel=" + ints(ed.selectedLocations()) + " listsel=" + ints(shellListSelection());
    if (gLocPropsShown) s += " locprops=" + quoted(narrow(gLocPropsShown->typeText())) + " locedges=" + narrow(gLocPropsShown->edgesText());
    float wx = 0, wy = 0;
    toWorld(app.mouseX, app.mouseY, wx, wy);
    // The units under the mouse (a stack: the status bar's count), and the places in it of the selected units (1: the top).
    {
        const std::vector<int> stack = ed.unitsAt(int(wx), int(wy));
        std::string at;
        for (size_t k = 0; k < stack.size(); ++k)
            if (ed.isSelected(stack[k])) at += (at.empty() ? "" : ",") + std::to_string(k + 1);
        s += " stack=" + std::to_string(stack.size()) + " stacksel=" + (at.empty() ? std::string("-") : at);
        s += " stackcount=" + std::to_string(unitStackCount(app));
    }
    s += " paste=";
    if (app.pasting && ed.canPaste()) {
        int red = 0;
        const auto ghost = unitPasteGhost(wx, wy);
        for (const PasteGhostUnit& g : ghost) red += g.red;
        s += "units:" + std::to_string(ghost.size()) + ":" + std::to_string(red) + "red";
    } else if (app.terrainPasting) s += "terrain";
    else if (app.pastingItems) s += "items";
    else s += "none";
    if (app.mode == Mode::Doodads && app.palette >= 0)  // the picked doodad's ghost at the mouse: green where a click places it
        if (const DoodadType* d = ed.doodadTypes().find(uint16_t(app.palette))) {
            int tx = 0, ty = 0;
            ed.doodadOrigin(*d, int(wx), int(wy), tx, ty);
            s += doodadFits(ed.chk(), *d, tx, ty, app.doodadCheck) && !ed.doodadOverlaps(*d, tx, ty) ? " ghost=fits" : " ghost=red";
        }
    std::uint32_t hash = 2166136261u;  // FNV-1a of the tiles
    for (uint16_t t : ed.terrain().tiles) hash = (hash ^ (t & 0xFF)) * 16777619u, hash = (hash ^ (t >> 8)) * 16777619u;
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08x", hash);
    const int tx = int(wx) / 32, ty = int(wy) / 32;
    const bool onMap = wx >= 0 && wy >= 0 && tx < ed.width() && ty < ed.height();
    s += std::string(" terrain=") + hex + " tile=" + (onMap ? std::to_string(ed.terrain().tiles[size_t(ty) * ed.width() + size_t(tx)]) : std::string("-"));
    s += " tsel=" + (app.hasTerrainSel ? box(app.terrainSel) : std::string("-")) + " tsellines=";  // (and its outline's lines)
    if (app.ed && app.ed->isOpen()) {
        render::Scene sel;
        s += std::to_string(terrainSelScene(sel, wx, wy));
    } else {
        s += "0";
    }
    s += " tabs=" + std::to_string(gActive + 1) + "/" + std::to_string(gDocs.size()) + " page=" + std::to_string(activePage()) +
         " tab=" + quoted(narrow(docTitle(gActive, false)));
    s += std::string(" quick=") + (gQuickHostSdl.open ? "open" : "-");  // the quick picker (Q): a pop-out over the map
    s += " quicktop=" + std::to_string(gQuickViewSdl.top()) + "/" + std::to_string(gQuickViewSdl.shown().size());  // its first row shown, of its rows
    s += " lastat=" + (!ed.units().empty() ? std::to_string(ed.units().back().x) + "," + std::to_string(ed.units().back().y) : std::string("-"));  // the last unit's place
    s += " brush=" + narrow(stripNumberText(kNumSize)) + "x" + narrow(stripNumberText(kNumHeight));  // (Terrain: the brush's size and height)
    static const char* const kShapes[3] = {"diamond", "square", "circle"};
    if (app.mode == Mode::Terrain && app.terrainRect && app.mouseIn) {  // the brush preview as the next paint draws it
        render::Scene preview;
        tilesScene(preview, wx, wy);
    }
    {  // the brush's tiles in Rectangle mode, the number in each row from the top (tilesFootprint)
        std::map<int, int> rows;
        for (auto [x, y] : tilesFootprint(0, 0)) ++rows[y];
        s += " brushrows=";
        for (auto it = rows.begin(); it != rows.end(); ++it) s += (it == rows.begin() ? "" : ",") + std::to_string(it->second);
    }
    s += std::string(" brushshape=") + kShapes[int(app.mode == Mode::Terrain && currentTool(app) == TSelect ? app.selectShape : app.brushShape) % 3] + " tilepreview=" + std::to_string(gTilesPreview.tiles) + "/" + std::to_string(gTilesPreview.lines);  // (Rectangle: the brush preview's tiles and lines)
    // The Tiles list (Rectangle): its scroll, its pictures kept, the tile picked; and the bitmaps that went while the
    // frame that drew them was not yet drawn by the GPU (none should: GitHub #148).
    s += " tilescroll=" + std::to_string(gTiles->scroll) + "/" + std::to_string(std::max(0, gTiles->contentH - tilesViewH())) +
         " tilepics=" + std::to_string(gTilesArt.pixels.size()) + " tilepicked=" + std::to_string(gTiles->picked) +
         " freedinframe=" + std::to_string(kit_sdl::bitmapsFreedInFrame());
    // The Tiles list's width and its columns' left edges (and the right end + 1): a tile is from an edge to the next
    // edge - 1 (views_tiles.inc: tilesEdge); and its slots (a row is 16).
    s += " tilegrid=" + std::to_string(gTilesGrid->width()) + ":";
    for (int i = 0; i <= 16; ++i) s += (i ? "," : "") + std::to_string(tilesEdge(i));
    s += " tileslots=" + std::to_string(gTiles->values.size());
    // Custom (core/stamps.inc): the choice of the strip, the tiles selected (Shift and Ctrl make any shape), the Brush's
    // custom terrain, the files of the tileset in the folder (names, | between them: read from the disk now), the list's
    // rows as it shows them (# a group, ## a sub-group, then the items), and whether the panel shows the list.
    static const char* const kKinds[3] = {"iso", "rect", "custom"};
    s += std::string(" kind=") + kKinds[terrainKind()] + " tselcount=" + std::to_string(terrainSelTiles().size());
    s += " custombrush=" + (gTiles->stampName.empty() ? std::string("-") : quoted(narrow(gTiles->stampName)));
    {
        std::string files, rows;
        for (const TerrainStamp& st : terrainStamps(shellStampsFolder(), uint16_t(ed.era() & 7)))
            files += (files.empty() ? "" : "|") + st.name + ":" + std::to_string(st.count());
        const auto& list = stampsList();
        for (const StampRow& r : stampsRows())
            rows += (rows.empty() ? "" : "|") + (r.heading ? std::string(size_t(r.depth + 1), '#') + narrow(r.label)
                                                           : narrow(r.label) + (r.index >= 0 && r.index < int(list.size()) ? "=" + list[size_t(r.index)].name : ""));
        s += " customfiles=" + quoted(files) + " customrows=" + quoted(rows);
        s += std::string(" customshown=") + (gTilesPanel->stamps().shown() ? "yes" : "no");
    }
    // The panels (panel_state.inc): the active one (the side bar's of a mode, or the n-th palette window's), and each one's
    // own Isometric or Rectangle, brush size, tool and pick: "side" the side bar's Terrain panel, then each window's.
    std::string active = "-";
    if (isDockedPanel(gActivePanel)) active = "side-" + narrow(kModeNames[int(gActivePanel->mode)]);
    for (size_t i = 0; i < gPanels.size(); ++i)
        for (PanelState& st : gPanels[i]->view->states)
            if (&st == gActivePanel) active = "window" + std::to_string(i + 1) + "-" + narrow(kModeNames[int(st.mode)]);
    auto one = [](PanelState& st) {
        panelFresh(st);
        char b[96];
        std::snprintf(b, sizeof b, "%s:%g:%d:%d:%d", st.terrainRect ? "rect" : "iso", st.brushSize, st.tool, st.palette, st.tiles ? st.tiles->picked : -1);
        return std::string(b);
    };
    s += " active=" + active + " sideT=" + one(dockedPanel(Mode::Terrain));
    for (size_t i = 0; i < gPanels.size(); ++i) s += " window" + std::to_string(i + 1) + "=" + one(gPanels[i]->view->state());
    return s;
}

}  // namespace

// --check names (after the inputs): the open map's file, its title and the units' own names, as the pages show them
// (the map's strings: UTF-8, else Windows-1252), with "utf-8" or "legacy" for their bytes.
void sdlNamesCheck() {
    if (!gState.ed || !gState.ed->isOpen()) return void(std::printf("names: no map open (WRONG)\n"));
    auto kind = [](const std::string& b) {
        std::wstring w;
        return b.empty() ? "empty" : stardraft::text::decodeUtf8(b, w) ? "utf-8" : "legacy";
    };
    const std::string title = gState.ed->scenarioName();
    std::printf("names: map \"%s\"\n", narrow(gState.ed->path().wstring()).c_str());
    std::printf("names: title \"%s\" (%s)\n", narrow(widen(title)).c_str(), kind(title));
    for (int u = 0; u < kSettingUnits; ++u)
        if (const std::string n = gState.ed->unitSettingName(u); !n.empty())
            std::printf("names: unit %d \"%s\" (%s)\n", u, narrow(gameTextPlain(n)).c_str(), kind(n));
}

// The New page: the choices it shows, the presses of its Randomize buttons, the preview's maps made and the choices of
// the last one ("the same" when it is made for the choices shown), and whether the Symmetry list is open.
std::string shotNewPageState() {
    const std::string shown = gNewView.choicesText();
    return "new page: " + shown + "; " + std::to_string(gNewView.randomized) + " randomize presses, " + std::to_string(gNewView.generations) +
           " maps made, the last for " + (gNewView.generatedFor == shown ? std::string("the same choices") : "other choices (" + gNewView.generatedFor + ")") +
           "; the symmetry list " + (gNewView.symmetryListOpen() ? "open" : "closed");
}
// The window's own parts after an input, on a line of their own ("  ui: ..."): what the Map workspace shows (the map,
// the start page, the New page or another workspace's view), the workspace, the side bars' widths (the dock settings,
// 96-dpi pixels), the cursor at the point where the last mouse input left the mouse, the document tabs and whether the
// active one is a project; with the New page open, its kind, the Start choice selected and the Start choices it shows.
const char* sdlCursorName(draw::Cursor c);
std::string sdlShotUi() {
    if (!gRoot) return "no window";
    const char* page = gNewView.shown() ? "new" : gStartView.shown() ? "start" : gRoot->map.shown() ? "map" : "other";
    const int ws = int(gWorkspace);
    std::string s = std::string("page=") + page + " ws=" + (ws >= 0 && ws < kWorkspaceNameCount ? kWorkspaceNames[ws] : "?");
    s += " left=" + std::to_string(gDock.leftW) + " right=" + std::to_string(gDock.rightW);
    std::string cur = "-";
    if (gShotMouse.x >= 0) {
        draw::Router router(*gRoot);
        cur = sdlCursorName(router.cursor(gShotMouse));
    }
    s += " cursor=" + cur;
    s += " tabs=" + std::to_string(gActive + 1) + "/" + std::to_string(gDocs.size()) + " project=" + (activeProject() ? "yes" : "no");
    if (gState.ed && gState.ed->isOpen()) {  // the map's parts (they stay when another workspace's commands run)
        int locs = 0;
        for (int i = 0; i < gState.ed->locationCount(); ++i) locs += gState.ed->locationUsed(i);
        s += " units=" + std::to_string(gState.ed->units().size()) + " doodads=" + std::to_string(gState.ed->doodads().size()) +
             " locations=" + std::to_string(locs) + " sel=" + std::to_string(gState.ed->selection().size()) + "," +
             std::to_string(gState.selDoodads.size()) + "," + std::to_string(gState.selSprites.size()) +
             " size=" + std::to_string(gState.ed->width()) + "x" + std::to_string(gState.ed->height());
    }
    if (const ResizeView* rv = gResizeShown) {  // Crop or extend shows: its values and its preview's
        const ResizeValues& v = rv->values();
        s += " resize=" + std::to_string(v.w) + "x" + std::to_string(v.h) + "@" + std::to_string(v.shiftX) + "," + std::to_string(v.shiftY) +
             " preview=" + (rv->preview().made ? std::to_string(rv->preview().w) + "x" + std::to_string(rv->preview().h) : std::string("none"));
    }
    // The Edit commands' state (the menu items and the top bar's buttons: 1 enabled, 0 gray), and the quick picker.
    static const std::pair<const char*, int> kEdit[] = {{"cut", ID_EDIT_CUT},       {"copy", ID_EDIT_COPY},     {"paste", ID_EDIT_PASTE},
                                                        {"dup", ID_EDIT_DUPLICATE}, {"del", ID_EDIT_DELETE},    {"all", ID_EDIT_SELECTALL},
                                                        {"rename", ID_EDIT_RENAME}, {"quick", ID_QUICKPICK}};
    s += " edit=";
    for (const auto& [n, id] : kEdit) s += std::string(n) + ":" + (sdlEnabled(id) ? "1" : "0") + (id == ID_QUICKPICK ? "" : ",");
    s += std::string(" quickopen=") + (!gQuickHostSdl.open                               ? "no"
                                       : gQuickViewSdl.kind() == QuickPickKind::Data ? "data"
                                       : gQuickViewSdl.kind() == QuickPickKind::Open ? "open"
                                                                                     : "map");
    if (gNewView.shown()) {
        const char* start = gNewView.copyMap ? "import" : gNewView.terrainOnly ? "terrain" : gNewView.generate ? "generate" : "blank";
        s += std::string(" new=") + (gNewView.project ? "eud" : "classic") + " start=" + start + " offered=" + gNewView.startOffered;
    }
    return s;
}

// The line's state part (empty: none for this workspace).
std::string sdlShotState() {
    const bool map = gState.ed && gState.ed->isOpen();
    if (gNewOpen && gWorkspace == Workspace::Map) return shotNewPageState();
    switch (gWorkspace) {
    case Workspace::Data: return activeProject() ? shotDataState() : "";
    case Workspace::Triggers: return activeProject() ? shotTriggersState() : "";
    case Workspace::ClassicTriggers: return map ? shotClassicState() : "";
    case Workspace::ClassicSettings: return map ? shotMapDataState() : "";
    case Workspace::Files:
        return "files: search \"" + narrow(gFilesPage.searchText()) + "\", " + std::to_string(gFilesPage.gameSoundsShown()) + " game sounds; selected \"" +
               gFilesPage.selected() + "\"; " + std::to_string(gSoundAsks) + " plays, last \"" + gSoundAsked + "\"";
    case Workspace::Briefing: return map ? shotBriefingState() : "";
    default: return map ? shotMapState() : "";
    }
}
