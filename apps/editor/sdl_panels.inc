// The Map workspace's side panels on this system (research/ports/MAC-BUILD-PLAN.md step 6): the palette's list (the
// mode's items, with the search box, the section box and the View button), the owner dropdown, the Locations filter and
// list, and the second palette (the other side's mode) with its panel tabs. Each control drives the core function that
// the Win32 control drives (pickUnit, setPlayer, rebuildList, setMode, selectLocation). The rows are the core's
// (paletteItems, core/palette_items.inc). Included by sdl_main_view.inc before MainView, which places the views.

// The panel tabs' sides (the core's PanelTabsView) and the modes last used on each side: main.cpp has them for Windows.
Mode gPanelTab[2] = {Mode::Terrain, Mode::Units};    // the tab each side shows
Mode gLastOnSide[2] = {Mode::Terrain, Mode::Units};  // the mode last used on each side (left, right)
bool gPal2Shown = false;                             // the second palette (the other side) was shown
Mode gPal2Mode = Mode::Units;                        // ... and the mode it showed
bool gPal3Shown = false;                             // the third palette (the active side's, while the active mode floats)
Mode gPal3Mode = Mode::Terrain;                      // ... and the mode it showed

// A pick in the second or the third palette goes to its mode first (the Win32 program's secondPick): that mode becomes the
// active one, its list is the main palette's, and the item is picked there. (The picks are the active mode's: the other
// lists show none.)
void pickLocationElsewhere(int v, bool add);
// A pick in another mode's list in the side bar: its panel (the side bar's of that mode) is the active one, and picks.
void pickInOtherMode(PanelState* panel, int v, bool add) {
    if (panel && panel->mode == Mode::Locations) return pickLocationElsewhere(v, add);
    if (panel) activatePanel(panel);
    pickUnit(gState, v, add);
}

std::vector<std::wstring> gListRows;  // the Locations list's rows (rebuildList fills them)
std::vector<int> gLocItems;           // ... and their locations (in every mode: shellListFill)
void locationListRows(const AppState& app, std::vector<int>& items, std::vector<std::wstring>& rows);
std::vector<std::wstring> gSectionNames;  // the section box's sections, after its "All ..." entry
Mode gSectionMode = Mode::Units;          // the mode the section box was filled for
std::wstring gPalSection;                 // the section the palette shows ("" is every one)

void invalidateMapView();  // (sdl_main_view.inc: the map view is drawn again)

// The palette and the second palette: the core's view (core/views_palette.inc: PaletteGridView, a list or a grid as the
// mode's view says). initPanels wires the picks, the pictures and the View menu.
using PalettePanel = PaletteGridView;

// The Locations list: a click on a row picks it and gives the keyboard to the map (the keys' own list moves keep it).
struct LocationListPanel final : kit::ListView {
    bool mouse(const draw::MouseEvent& e) override {
        const bool down = e.kind == draw::MouseKind::Down && e.button == 0;
        const int row = down ? rowAt(e.pt) : -1;
        const bool handled = kit::ListView::mouse(e);
        if (row >= 0) shellFocusView();
        return handled;
    }
};


// The palette's list and the second palette, the Locations list, the owner, the filter, the search and the section box,
// and the View button. The views are the MainView's children (sdl_main_view.inc places them).
PalettePanel gPalList, gPal2List, gPal3List;
LocationListPanel gLocList;
kit::Dropdown gOwnerBox;
SearchBox gFilterSdl;   // the Locations filter
SearchBox gSearchSdl;   // the palette's search
DropdownBox gSectionDropSdl;  // the section box (Units, Doodads, Sprites)
void paletteMenuSdl(PaletteGridView& pal, draw::View& from, draw::Point at, int row, PalettePanelView* panel = nullptr);
// A palette panel like a palette (sdl_palette_panels.inc): its mode, view and order; `nearView`: the window it opens by
// (null: by the side bar).
void palettePanelNew(Mode m, const PaletteView& v, PaletteSort sort, draw::View* nearView);
void palettePanelClose(PalettePanelView& p);
void palettePanelRebuild(PalettePanelView& p);
kit::Button gViewBtnSdl{L"View", kit::Tone::Normal, [] {  // the View menu under the button (as the Windows View button)
    paletteMenuSdl(gPalList, gViewBtnSdl, {0, gViewBtnSdl.height()}, -1);
}};
void paletteSortMenuSdl(Mode mode, draw::View& from, PalettePanelView* panel = nullptr);
// The Sort buttons: the search row's (the order of the active mode's palette: Units, Doodads, Sprites) and the Locations
// filter row's (the order of the locations; its floating window shows in every mode).
kit::IconButton gSortBtnSdl{kit::IconGlyph::ofGlyph(L'\xE8CB'), kit::IconStyle::Boxed, L"", [] { paletteSortMenuSdl(gState.mode, gSortBtnSdl); }};
kit::IconButton gLocSortBtnSdl{kit::IconGlyph::ofGlyph(L'\xE8CB'), kit::IconStyle::Boxed, L"", [] { paletteSortMenuSdl(Mode::Locations, gLocSortBtnSdl); }};
// The Sort buttons' tips: the orders now.
void sortTipsSdl() {
    gSortBtnSdl.setTip(paletteSortTip(gState.mode, gSortByMode[int(gState.mode)]));
    gLocSortBtnSdl.setTip(paletteSortTip(Mode::Locations, gSortByMode[int(Mode::Locations)]));
}
// The lists after a change of an order: Locations' everywhere (the list, the other palettes), else the palettes.
void sortChangedSdl(Mode mode) {
    if (mode == Mode::Locations) rebuildList(gState);  // (the Locations list: shellListFill, in any mode)
    shellPaletteRebuilt();
    sortTipsSdl();
}
PanelTabsView gPanelTabsSdl[2];



// The palette's search text, lower case (the search is not case sensitive).
std::wstring lowerPanelText(std::wstring t) {
    std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    return t;
}

// The items of a mode (paletteItems reads the mode from the app state: the mode is set for the call only).
std::vector<PaletteUnit> paletteUnitsOf(Mode m) {
    const Mode saved = gState.mode;
    gState.mode = m;
    std::vector<PaletteUnit> units = paletteItems(gState);
    gState.mode = saved;
    return units;
}

// The section box: "All units" (doodads, sprites), then the sections in their order. Filled again when the sections or the
// mode change; a section that is gone goes back to all.
void fillSectionBox(const std::vector<std::wstring>& names) {
    const Mode m = gState.mode;
    if (names == gSectionNames && m == gSectionMode) return;
    gSectionNames = names, gSectionMode = m;
    std::vector<kit::DropdownItem> items;
    items.push_back({m == Mode::Doodads ? shellTr(L"All doodads") : m == Mode::Sprites ? shellTr(L"All sprites") : shellTr(L"All units")});
    for (const auto& n : names) items.push_back({n});
    gSectionDropSdl.dropdown().setItems(std::move(items));
    const auto it = std::find(names.begin(), names.end(), gPalSection);
    if (it == names.end()) gPalSection.clear();
    gSectionDropSdl.dropdown().setValue(it == names.end() ? 0 : int(1 + (it - names.begin())));
}

// A palette item's picture (the palette's pictures, sdl_pictures.inc): false when it has none.
bool panelPicture(draw::Canvas& c, Mode what, int value, bool sprites, draw::Rect box) {
    if (what == Mode::Locations) return false;  // (a location has no picture)
    draw::Bitmap* b = paletteBitmap(what, value, gState.playerOwner, false, sprites);
    if (b) drawPaletteArt(c, *b, box);
    return b != nullptr;
}

// The View menu (core/palette_view.inc: paletteMenuModel) of the palette `pal` at `at` of `from`: on its item of `row`
// (-1: none), a doodad of a project gets Pin to the top or Unpin first. A view item changes the view of the palette's
// mode (saved in the settings).
void palettePanelsViewOf(Mode m);  // (sdl_palette_panels.inc)
void paletteMenuSdl(PaletteGridView& pal, draw::View& from, draw::Point at, int row, PalettePanelView* panel) {
    const int value = row >= 0 && row < int(pal.rows.size()) ? pal.rows[size_t(row)].value : -1;
    const Mode mode = pal.mode;
    const PaletteView& view = panel ? panel->view : gViewByMode[int(mode)];
    const PaletteSort sort = panel ? panel->sort : gSortByMode[int(mode)];
    const bool tiers = mode == Mode::Units && paletteHasTiers();  // (a mod's units with tech tiers: Sort by tier)
    const int pin = palettePinState(mode, value);
    const std::vector<uint16_t> shown = paletteShownDoodads(pal.rows);  // (Move up and Move down: among the pins shown)
    const MenuModel model = paletteMenuModel(mode, view, pin, sort, panel && !panel->floating, tiers, pin == 1 ? doodadPinMovesOff(value, shown) : 0);
    const std::wstring on = sdlFeedbackOn(from, at, panel ? L"a palette panel" : L"the palette");
    sdlPopupMenu(from, at, sdlModelItems(model, [value, mode, panel, view, sort, shown](int id) {
        if (id == kPalPin) return doodadPinToggle(value);
        if (id == kPalPinUp || id == kPalPinDown) return doodadPinMove(value, id == kPalPinUp ? -1 : 1, shown);
        if (id == ID_PAL_NEW_PANEL) return palettePanelNew(mode, view, sort, panel);
        if (id == ID_PAL_CLOSE_PANEL && panel) return shellPost([panel] { palettePanelClose(*panel); });
        if (id >= ID_VIEW_FLOATPAL && id < ID_VIEW_FLOATPAL + kModeCount) {
            const Mode fm = Mode(id - ID_VIEW_FLOATPAL);
            return shellPost([fm] { shellFloatPalette(fm, !paletteFloats(fm)); });
        }
        if (panel) {  // a panel's own view and order (each panel's own: panel_state.inc)
            if (applyPaletteViewCommand(mode, id, panel->view, panel->sort)) palettePanelRebuild(*panel);
            return;
        }
        if (id >= ID_VIEW_FLOATPAL && id < ID_VIEW_FLOATPAL + kModeCount) return shellFloatPalette(Mode(id - ID_VIEW_FLOATPAL), !paletteFloats(Mode(id - ID_VIEW_FLOATPAL)));
        if (applyPaletteViewCommand(mode, id)) {
            if (paletteSortCommand(id)) sortChangedSdl(mode);  // (the rows in the new order)
            gPalList.layout(), gPal2List.layout(), gPal3List.layout();  // (each palette shows the view of its mode)
            shellSettingsChanged();
        }
    }), on);
}

// The Sort button's menu (core/palette_view.inc: paletteSortMenuModel) under `from`: the order of the list of `mode`, or
// of a palette panel's own list. The side bar's order is saved in the settings.
void paletteSortMenuSdl(Mode mode, draw::View& from, PalettePanelView* panel) {
    const PaletteSort sort = panel ? panel->sort : gSortByMode[int(mode)];
    const bool tiers = mode == Mode::Units && paletteHasTiers();  // (a mod's units with tech tiers: Sort by tier)
    const draw::Point at{0, from.height()};
    const std::wstring on = sdlFeedbackOn(from, at, L"the Sort button");
    sdlPopupMenu(from, at, sdlModelItems(paletteSortMenuModel(mode, sort, tiers), [mode, panel](int id) {
        if (panel) {
            PaletteView v = panel->view;
            if (applyPaletteViewCommand(mode, id, v, panel->sort)) palettePanelRebuild(*panel);
            return;
        }
        if (!applyPaletteViewCommand(mode, id)) return;
        sortChangedSdl(mode);
        shellSettingsChanged();
    }), on);
}

// The Locations list's pick (as the Win32 list's LBN_SELCHANGE): the list's selected rows are the selected locations
// (Ctrl+click and Shift+click: several); the row clicked is the selected one, and the camera goes to it. A pick in the
// location list's window while another mode is active makes Locations the mode.
void locationListPicked() {
    const int sel = gLocList.cursor();
    const int v = sel >= 0 && sel < int(gLocItems.size()) ? gLocItems[size_t(sel)] : -1;
    if (gState.ed) {
        std::vector<int> others;
        for (int row : gLocList.selection())
            if (row >= 0 && row < int(gLocItems.size()) && row != sel) others.push_back(gLocItems[size_t(row)]);
        const bool on = v >= 0 && gLocList.isSelected(sel);
        if (gState.mode != Mode::Locations) setMode(Mode::Locations);  // (its rows: the same locations)
        Editor& ed = *gState.ed;
        ed.selectLocation(-1);
        for (int l : others) ed.addLocationSelection(l);
        if (!on) {
            gState.dirty = true;
            gLocList.invalidate();
            invalidateMapView();
            return;
        }
        ed.addLocationSelection(v);
        const auto r = ed.locationRect(v);
        int w = 0, h = 0;
        shellViewSize(w, h);
        gState.camX = (r.left + r.right) / 2.0f - w / 2.0f / gState.zoom;
        gState.camY = (r.top + r.bottom) / 2.0f - h / 2.0f / gState.zoom;
    }
    gState.dirty = true;
    gLocList.invalidate();
    invalidateMapView();
}

// Each mode's palette keeps its place: the scroll, the search and the section it had when another mode took the palette
// come back with its mode (not saved in the settings).
struct PaletteMemory {
    int scroll = 0;
    std::wstring search, section;
};
std::map<int, std::array<PaletteMemory, kModeCount>> gPalMemoryByTab;  // (by the tab's key: Doc::key)
int gPalRowsMode = -1;         // the mode whose rows the palette has (-1: none yet)
int gPalRowsTab = -1;          // ... and the tab (its key)
int activeDocKey();
bool gPalRestoring = false;    // the search box's text is set back: its change does not scroll or rebuild

// The palette's rows for the current mode, the section box and the search (the Win32 rebuildUnitPalette), in the
// mode's order (gSortByMode). Another mode than the rows': the old mode's place is kept, the new one's comes back.
void rebuildPaletteRows() {
    const int m = int(gState.mode), tab = activeDocKey();
    const bool switched = m != gPalRowsMode || tab != gPalRowsTab;  // (each tab keeps each mode's place)
    if (switched) {
        if (gPalRowsMode >= 0 && gPalRowsMode < kModeCount)
            gPalMemoryByTab[gPalRowsTab][size_t(gPalRowsMode)] = {gPalList.scrollTop(), gSearchSdl.text(), gPalSection};
        const PaletteMemory back = m >= 0 && m < kModeCount ? gPalMemoryByTab[tab][size_t(m)] : PaletteMemory{};
        gPalSection = back.section;
        if (gSearchSdl.text() != back.search) {
            gPalRestoring = true;
            gSearchSdl.setText(back.search);
            gPalRestoring = false;
        }
        gPalRowsMode = m, gPalRowsTab = tab;
    }
    const std::vector<PaletteUnit> units = paletteUnitsOf(gState.mode);
    fillSectionBox(paletteSectionNames(units));
    int pinned = 0;
    gPalList.searchWords = lowerPanelText(gSearchSdl.text());
    std::vector<PaletteRow> rows = paletteRows(units, gState.mode, gPalList.searchWords, gPalSection, pinned, gSortByMode[m]);
    gPalList.setRows(std::move(rows), gState.mode, pinned);
    if (switched) gPalList.scrollTo(m >= 0 && m < kModeCount ? gPalMemoryByTab[tab][size_t(m)].scroll : 0);
}

// The second (or the third) palette's rows: its mode's items, unfiltered (it has no search box). Locations: the map's
// locations, in Locations' order (as the Locations list).
void rebuildOtherRows(PalettePanel& list, Mode m) {
    int pinned = 0;
    if (m == Mode::Locations) {
        std::vector<PaletteRow> rows;
        if (gState.ed && gState.ed->isOpen()) {
            std::vector<int> all;
            for (int i = 0; i < gState.ed->locationCount(); ++i)
                if (gState.ed->locationUsed(i)) all.push_back(i);
            sortLocationItems(gState, all);
            for (int i : all) rows.push_back({i, widen(gState.ed->locationName(i))});
        }
        list.setRows(std::move(rows), m, 0);
        return;
    }
    std::vector<PaletteRow> rows = paletteRows(paletteUnitsOf(m), m, std::wstring(), std::wstring(), pinned, gSortByMode[int(m)]);
    list.setRows(std::move(rows), m, pinned);
}
void rebuildSecondRows() { rebuildOtherRows(gPal2List, gPal2Mode); }

// A mode's palette state in the side bar (its scroll, search and section) of tab `tab`: the side bar's boxes while its
// palette has that mode's rows, else the mode's memory. A mode's floating palette is that palette in a window: it shows
// and sets the same state (sdl_palette_panels.inc), so it is the same in the window and in the side bar.
PaletteMemory dockedPaletteGet(int tab, Mode m) {
    if (gPalRowsMode == int(m) && gPalRowsTab == tab) return {gPalList.scrollTop(), gSearchSdl.text(), gPalSection};
    return gPalMemoryByTab[tab][size_t(m)];
}
void dockedPaletteSet(int tab, Mode m, const PaletteMemory& s) {
    if (!(gPalRowsMode == int(m) && gPalRowsTab == tab)) {
        gPalMemoryByTab[tab][size_t(m)] = s;
        return;
    }
    gPalSection = s.section;
    if (gSearchSdl.text() != s.search) {
        gPalRestoring = true;
        gSearchSdl.setText(s.search);
        gPalRestoring = false;
    }
    const auto it = std::find(gSectionNames.begin(), gSectionNames.end(), gPalSection);
    if (gSectionMode == m) gSectionDropSdl.dropdown().setValue(it == gSectionNames.end() ? 0 : int(1 + (it - gSectionNames.begin())));
    if (gState.mode == m && activeDocKey() == tab) rebuildPaletteRows();
    gPalList.scrollTo(s.scroll);
}
void rebuildThirdRows() { rebuildOtherRows(gPal3List, gPal3Mode); }
// The map's locations changed while another mode is active: a side's list of the locations shows them.
void shellOtherLocationRows() {
    if (gState.mode != Mode::Locations) shellListFill({});  // (the location list's window: the locations)
    if (gPal2Shown && gPal2Mode == Mode::Locations) rebuildSecondRows();
    if (gPal3Shown && gPal3Mode == Mode::Locations) rebuildThirdRows();
}
// A location picked in a side's list of the locations while another mode is active: Locations becomes the mode and the
// location is selected (Ctrl+click: in or out of the selection), the camera going to it.
void pickLocationElsewhere(int v, bool add) {
    if (!gState.ed || !gState.ed->locationUsed(v)) return;
    if (gState.mode != Mode::Locations) setMode(Mode::Locations);
    Editor& ed = *gState.ed;
    if (add && ed.locationSelected(v)) return ed.removeLocationSelection(v);
    if (add) ed.addLocationSelection(v);
    else ed.selectLocation(v);
    const auto r = ed.locationRect(v);
    int w = 0, h = 0;
    shellViewSize(w, h);
    gState.camX = (r.left + r.right) / 2.0f - w / 2.0f / gState.zoom;
    gState.camY = (r.top + r.bottom) / 2.0f - h / 2.0f / gState.zoom;
    gState.dirty = true;
}

// The views' wiring, once (MainView's constructor): the rows, the look, the changes that rebuild them.
void initPanels() {
    static bool done = false;
    if (done) return;
    done = true;
    gLocList.setRowHeight(30);
    gPal2List.second = gPal3List.second = true;
    gPalList.chosen = &shellPaletteChosen();
    gPalList.picture = gPal2List.picture = gPal3List.picture = panelPicture;
    // A click picks the item (Ctrl adds to the pick; Shift picks every item from the last pick, as on Windows); the second
    // and the third palette pick in their own mode (pickInOtherMode). The keyboard goes to the map after a pick (Windows:
    // SetFocus on the view), so the map's keys and undo run.
    gPalList.picked = [](int row, unsigned mods) {
        const int v = gPalList.rows[size_t(row)].value;
        const bool ctrl = (mods & draw::kCtrl) != 0;
        // (isometric terrain: Shift+click picks the second terrain, the one Shift+click paints on the map; as Ctrl+click)
        const bool second = (mods & draw::kShift) && gState.mode == Mode::Terrain && !gState.terrainRect;
        if (!((mods & draw::kShift) && paletteShiftPick(gState, gPalList.rows, row, ctrl))) pickUnit(gState, v, ctrl || second);
        invalidateMapView();
        shellFocusView();
    };
    gPal2List.picked = [](int row, unsigned mods) {
        const int v = gPal2List.rows[size_t(row)].value;
        pickInOtherMode(gPal2List.state, v, (mods & draw::kCtrl) != 0);
        gPal2List.invalidate();
        invalidateMapView();
        shellFocusView();
    };
    gPalList.context = [](int row, draw::Point pt) { paletteMenuSdl(gPalList, gPalList, pt, row); };
    gPal2List.context = [](int row, draw::Point pt) { paletteMenuSdl(gPal2List, gPal2List, pt, row); };
    gPal3List.picked = [](int row, unsigned mods) {
        const int v = gPal3List.rows[size_t(row)].value;
        pickInOtherMode(gPal3List.state, v, (mods & draw::kCtrl) != 0);
        gPal3List.invalidate();
        invalidateMapView();
        shellFocusView();
    };
    gPal3List.context = [](int row, draw::Point pt) { paletteMenuSdl(gPal3List, gPal3List, pt, row); };
    gLocList.label = [](int i) { return i >= 0 && i < int(gListRows.size()) ? gListRows[size_t(i)] : std::wstring(); };
    gLocList.setMode(kit::SelectMode::Multiple);
    gLocList.onSelect = [] { locationListPicked(); };

    std::vector<kit::DropdownItem> owners;
    for (int i = 0; i < 12; ++i) owners.push_back({L"Player " + std::to_wstring(i + 1) + (i == 11 ? L" (neutral)" : L"")});
    gOwnerBox.setItems(std::move(owners));
    gOwnerBox.setValue(0);
    gOwnerBox.onChange = [](int i) {
        setPlayer(gState, i);
        shellFocusView();  // (the keyboard goes to the map after a pick, as Win32's SetFocus on the view)
    };

    gFilterSdl.setPlaceholder(L"Filter");
    gFilterSdl.onChange = [] { rebuildList(gState); };
    gSearchSdl.setPlaceholder(L"Search");
    gSearchSdl.onChange = [] {
        if (gPalRestoring) return;  // (a mode's search set back: rebuildPaletteRows goes on)
        gPalList.scrollTo(0);
        rebuildPaletteRows();
    };
    gSectionDropSdl.dropdown().onChange = [](int i) {
        i = std::clamp(i, 0, int(gSectionNames.size()));
        gPalSection = i == 0 ? std::wstring() : gSectionNames[size_t(i - 1)];
        gPalList.scrollTo(0);
        rebuildPaletteRows();
    };
    for (int s = 0; s < 2; ++s) gPanelTabsSdl[s].side = s;
}
