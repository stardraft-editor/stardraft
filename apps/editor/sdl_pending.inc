// The functions the core still needs that only the Win32 program defines (research/ports/MAC-BUILD-PLAN.md step 6).
// Each one is a temporary SDL version until it is in the core (another lane moves them there); the core declares them.
// Included by main_sdl.cpp after the core, next to sdl_main_view.inc.

// (until it is in the core) The state the core's functions reach through appState() (the core declares it in
// core/map_edit.inc): the SDL program's state is gState (sdl_main_view.inc).
AppState& appState() { return gState; }

// (until it is in the core) The palette and the tile panel are not on this system yet: their names and rows are empty.
void tilesRebuild() {}
void tilesSync() {}
int tilesViewH() { return 0; }
void tilesScrollTo(int) {}
RECT tilesCellRect(int) { return RECT{}; }
void tilesPanelShowAllFilter() {}
void tilesPanelRepaint() {}
void tilesPanelClearSearch() {}
bool trigUnitPickClick(float, float) { return false; }

// (until it is in the core) The camera stays near the map: at most half a view past each edge; a map smaller than the view
// keeps its middle in the view's middle on that axis (the Windows program's clamp; the view's size comes from the shell).
void clampCamera() {
    if (!gState.ed || !gState.ed->isOpen() || gState.zoom <= 0) return;
    int viewW = 0, viewH = 0;
    shellViewSize(viewW, viewH);
    const float vw = viewW / gState.zoom, vh = viewH / gState.zoom;
    const float mw = gState.ed->width() * 32.0f, mh = gState.ed->height() * 32.0f;
    auto clampAxis = [](float& cam, float view, float map) {
        if (map < view) {
            cam = std::clamp(cam, map / 2 - view, map / 2);
            return;
        }
        cam = std::clamp(cam, -view / 2, map - view / 2);
    };
    clampAxis(gState.camX, vw, mw);
    clampAxis(gState.camY, vh, mh);
}

// (until it is in the core) The right button paints with the second terrain (isometric terrain with a second terrain,
// as the Windows program's rightPaints); the other right clicks do nothing here (its map menu is not on this system yet).
bool rightPaints() {
    const int tool = currentTool();
    return gState.ed && gState.mode == Mode::Terrain && !gState.terrainRect && gState.palette2 > 0 &&
           (tool == TBrush || tool == TSpray || tool == TFill || tool == TReplace);
}

// (until it is in the core) The mode's own spray density is kept per mode (setMode swaps it in); the tool strip and the
// palette are not on this system yet.
void setMode(Mode m) {
    gState.densityByMode[int(gState.mode)] = gState.sprayDensity;
    gState.mode = m;
    gState.sprayDensity = gState.densityByMode[int(m)];
    shellToolsChanged();
    shellPaletteRebuilt();
}

// (until it is in the core) The palette's selection: the one entry that is picked.
std::vector<int> selectedPalette() {
    if (gState.palette >= 0) return {gState.palette};
    return {};
}

// (until it is in the core) A unit's name: the game's, with its subtitle (the project's custom names are not on this
// system yet).
std::wstring unitDisplayName(int u) {
    if (!gState.ed || u < 0 || u >= int(gState.ed->unitTypes().size())) return L"";
    const UnitType& t = gState.ed->unitTypes()[uint16_t(u)];
    return widen(t.name) + (t.subtitle.empty() ? L"" : L" (" + widen(t.subtitle) + L")");
}

// (until it is in the core) The workspaces: only the Map is on this system yet.
void setWorkspace(Workspace w) {
    if (w != Workspace::Map) setMessage(L"This is not on this system yet");
    gWorkspace = Workspace::Map;
    shellRelayout();
}
void pageSync() {}
void openStartPage() {}

// (until it is in the core) Closing a map with changes: Save is not on this system yet, so it is not closed (nothing
// is lost); the message says why.
bool confirmDiscard() {
    if (gState.ed && gState.ed->isOpen() && gState.ed->modified()) {
        setMessage(L"The map has changes, and saving is not on this system yet: the map stays open");
        return false;
    }
    return true;
}

// (until it is in the core) Projects are not on this system yet.
void forgetProject(const EudProject*) {}
void dropUnsavedProject(const std::shared_ptr<EudProject>&) {}

// (until it is in the core) A map that opened: one tab on this system; the map fits the view on the next frame.
void addDocument(std::unique_ptr<Editor> ed, bool) {
    gState.ed = std::move(ed);
    if (gDocs.empty()) gDocs.emplace_back();
    gActive = 0;
    gDocs[0].page = 0;
    gMapFitPending = true;
    shellRelayout();
    gState.dirty = true;
    updateTitle();
}
