// A snapshot's input lines (sdl_menus.inc: sdlInputLine): after each --key, --type, --click, --drag, --move or --wait,
// what the workspace shows holds now, as one line of text that tools/sdl_snapshots.py's CHECKS read (the Windows
// self-test reads the same parts of the model): the open map (its tileset and size; then, in the Map workspace, its
// parts as name=value: shotMapState); the Data workspace's open entry, its changed
// values and the undo steps; the selected trigger's blocks with their values; the Classic triggers page's triggers and
// the selected one's players; the Files page's search and how many game sounds it shows; the map's briefings. Included
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
        s += " (players" + players(t) + "; " + std::to_string(t.conditionCount()) + " conditions, " + std::to_string(t.actionCount()) + " actions)";
    }
    const ClassicTriggers inMap = gState.ed->classicTriggers();
    s += "; the map has " + std::to_string(inMap.triggers.size()) + ":";
    for (const ClassicTrigger& t : inMap.triggers) s += " [players" + players(t) + "]";
    return s;
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
// the terrain (a hash of its tiles, the tile under the mouse, the terrain selection), the tabs and the quick picker.
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
    if (gLocPropsShown) s += " locprops=" + quoted(narrow(gLocPropsShown->typeText()));
    float wx = 0, wy = 0;
    toWorld(app.mouseX, app.mouseY, wx, wy);
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
    s += " tsel=" + (app.hasTerrainSel ? box(app.terrainSel) : std::string("-"));
    s += " tabs=" + std::to_string(gActive + 1) + "/" + std::to_string(gDocs.size()) + " page=" + std::to_string(activePage()) +
         " tab=" + quoted(narrow(docTitle(gActive, false)));
    s += std::string(" quick=") + (gQuickHostSdl.open ? "open" : "-");
    s += " brush=" + narrow(stripNumberText(kNumSize)) + "x" + narrow(stripNumberText(kNumHeight));  // (Terrain: the brush's size and height)  // the quick picker (Q): a pop-out over the map
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

// The line's state part (empty: none for this workspace).
std::string sdlShotState() {
    const bool map = gState.ed && gState.ed->isOpen();
    switch (gWorkspace) {
    case Workspace::Data: return activeProject() ? shotDataState() : "";
    case Workspace::Triggers: return activeProject() ? shotTriggersState() : "";
    case Workspace::ClassicTriggers: return map ? shotClassicState() : "";
    case Workspace::Files: return "files: search \"" + narrow(gFilesPage.searchText()) + "\", " + std::to_string(gFilesPage.gameSoundsShown()) + " game sounds";
    case Workspace::Briefing: return map ? shotBriefingState() : "";
    default: return map ? shotMapState() : "";
    }
}
