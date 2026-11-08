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
bool paletteShiftPick(AppState& app, const std::vector<PaletteRow>& rows, int row, bool ctrl);  // (core/palette_view.inc)
// A click in the second or third palette with its keys: Ctrl adds to the pick, Shift picks a range (as the first palette).
void pickInOtherModeMods(PanelState* panel, std::vector<PaletteRow> rows, int row, int v, unsigned mods) {
    // (`rows`: a copy, the list's rows at the click; activatePanel's relayout fills the second and third lists again)
    const bool ctrl = (mods & draw::kCtrl) != 0;
    if (panel && panel->mode == Mode::Locations) return pickLocationElsewhere(v, ctrl);
    if (panel) activatePanel(panel);
    if (!((mods & draw::kShift) && paletteShiftPick(gState, rows, row, ctrl))) pickUnit(gState, v, ctrl);
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
kit::IconButton gSortBtnSdl{kit::IconGlyph::ofIcon(int(UiIcon::Sort)), kit::IconStyle::Boxed, L"", [] { paletteSortMenuSdl(gState.mode, gSortBtnSdl); }};
void sdlPieFromMenu();
// Terrain's search row: the pie palette's button in the Sort button's place (GitHub #257): a click shows the pie.
kit::IconButton gPieBtnSdl{kit::IconGlyph::ofIcon(int(UiIcon::Pie)), kit::IconStyle::Boxed, L"", [] { sdlPieFromMenu(); }};
kit::IconButton gLocSortBtnSdl{kit::IconGlyph::ofIcon(int(UiIcon::Sort)), kit::IconStyle::Boxed, L"", [] { paletteSortMenuSdl(Mode::Locations, gLocSortBtnSdl); }};
// The rows of the panels that are not active (the other side's list, the third list), in the place where the panel has
// them when it is active. A click in a box goes to that mode, in that box (the search box: its keyboard; the Owner and the
// section: its list open). The View and Sort menus are of that list, and leave the mode as it is.
struct OtherSearchCatch final : draw::View {
    enum What { Search, Owner, Section } what = Search;
    Mode mode = Mode::Terrain;
    bool mouse(const draw::MouseEvent& e) override {
        if (e.kind != draw::MouseKind::Down || e.button != 0) return true;
        void setMode(Mode m);
        setMode(mode);
        if (what == Owner) gOwnerBox.openList();
        else if (what == Section) gSectionDropSdl.dropdown().openList();
        else (mode == Mode::Locations ? gFilterSdl : gSearchSdl).field().focus();
        return true;
    }
};
// (Their boxes show what the panel has; the keys go to them in the active panel: no Tab stops here.)
struct OtherRowDropdown final : kit::Dropdown {
    bool focusable() const override { return false; }
};
struct OtherSearchRow {
    OtherRowDropdown owner, section;
    OtherSearchCatch ownerCatch, sectionCatch;
    SearchBox box;
    OtherSearchCatch catcher;
    kit::Button view;
    kit::IconButton sort;
    Mode mode = Mode::Terrain;
    explicit OtherSearchRow(PalettePanel& list)
        : view(L"View", kit::Tone::Normal, [this, &list] { paletteMenuSdl(list, view, {0, view.height()}, -1); }),
          sort(kit::IconGlyph::ofIcon(int(UiIcon::Sort)), kit::IconStyle::Boxed, L"", [this] {
              if (mode == Mode::Terrain) sdlPieFromMenu();  // (Terrain: the pie palette's button, as the active row's)
              else paletteSortMenuSdl(mode, sort);
          }) {
        ownerCatch.what = OtherSearchCatch::Owner, sectionCatch.what = OtherSearchCatch::Section;
        box.field().setTabStop(false);
    }
};
OtherSearchRow gSearchRow2{gPal2List}, gSearchRow3{gPal3List};
void otherRowsOwnerItems(const std::vector<kit::DropdownItem>& owners) {
    for (OtherSearchRow* r : {&gSearchRow2, &gSearchRow3}) {
        const int value = panelFresh(dockedPanel(r->mode)).owner;
        r->owner.setItems(owners);
        r->owner.setValue(value);
    }
}
// The Sort buttons' tips: the orders now.
void sortTipsSdl() {
    for (OtherSearchRow* r : {&gSearchRow2, &gSearchRow3}) {
        r->sort.setGlyph(kit::IconGlyph::ofIcon(int(r->mode == Mode::Terrain ? UiIcon::Pie : UiIcon::Sort)));
        r->sort.setTip(r->mode == Mode::Terrain ? std::wstring(tr(L"Terrain pie palette: the terrains in a circle; click one to paint with it (on the map: hold Space)"))
                                                : paletteSortTip(r->mode, gSortByMode[int(r->mode)]));
    }
    gSortBtnSdl.setTip(paletteSortTip(gState.mode, gSortByMode[int(gState.mode)]));
    gLocSortBtnSdl.setTip(paletteSortTip(Mode::Locations, gSortByMode[int(Mode::Locations)]));
    gPieBtnSdl.setTip(tr(L"Terrain pie palette: the terrains in a circle; click one to paint with it (on the map: hold Space)"));
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
    const int left = gPaletteArtLeft;
    draw::Bitmap* b = paletteBitmap(what, value, gState.playerOwner, false, sprites);
    if (b) drawPaletteArt(c, *b, box);
    return b != nullptr || gPaletteArtLeft != left;  // (a picture of a next frame: its place stays empty until then)
}
// A palette's paint: its time for new pictures starts (start), or ends; true: pictures are left for a next frame.
bool panelPictureBudget(bool start) {
    static int leftAtStart = 0;
    if (start) {
        gPaletteArtDeadline = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(kPaletteArtBudgetMs * 1000));
        leftAtStart = gPaletteArtLeft;
        return false;
    }
    gPaletteArtDeadline = {};
    return gPaletteArtLeft != leftAtStart;
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
    int mask = -1;  // (a terrain: in the mask or not)
    if (mode == Mode::Terrain && value >= 0 && gState.ed) {
        const auto& tm = gState.ed->terrainMask();
        mask = std::find(tm.begin(), tm.end(), uint16_t(value)) != tm.end() ? 1 : 0;
    }
    const MenuModel model = paletteMenuModel(mode, view, pin, sort, panel && !panel->floating, tiers, pin == 1 ? doodadPinMovesOff(value, shown) : 0, mask);
    const std::wstring on = sdlFeedbackOn(from, at, panel ? L"a palette panel" : L"the palette");
    sdlPopupMenu(from, at, sdlModelItems(model, [value, mode, panel, view, sort, shown](int id) {
        if (id == kPalPin) return doodadPinToggle(value);
        if (id == kPalMask) {
            paletteMaskToggle(gState, value);
            gPalList.invalidate(), gPal2List.invalidate(), gPal3List.invalidate();
            return invalidateMapView();
        }
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

// The Custom list's right click (core/views_stamps.inc): on an item Rename, Delete, Show in folder; then the View items
// as the other palettes have them (List, Grid; Normal, Large, Extra large), kept in the settings.
void shellStampsMenu(draw::View& from, draw::Point at, const TerrainStamp* s, const std::string& folder, bool onFolder) {
    StampsListView* list = stampsListOf(&from);  // (its view: the panel's own)
    const int vg = list ? list->viewGrid() : stampsGrid(), vs = list ? list->viewSize() : stampsSize();
    enum { kRename = 1, kDelete, kReveal, kCopyText, kImportText, kNewFolder, kRenameFolder, kDeleteFolder, kMask };
    MenuModel m;
    const bool own = s && !s->file.empty();  // (a mod's brush: not the user's file)
    const bool modFolder = folder == "Cosmonarchy" && modTab();  // (the mod's brushes: not a folder of the user's)
    if (own) {
        m.push_back(menuItem(kRename, L"Rename\x2026"));
        m.push_back(menuItem(kDelete, L"Delete\x2026"));
        m.push_back(menuItem(kReveal, L"Show in folder"));
        m.push_back(menuSeparator());
    }
    if (s && gState.ed) {  // (its tiles into the mask, or out of it: as Alt+click)
        m.push_back(menuItem(kMask, tilesMaskHas(stampTilesOf(*s)) ? L"Remove from the &mask" : L"Add to the &mask", L"Alt+click"));
        m.push_back(menuSeparator());
    }
    if (onFolder && !modFolder) {
        m.push_back(menuItem(kRenameFolder, L"Rename folder\x2026"));
        m.push_back(menuItem(kDeleteFolder, L"Delete folder"));
    }
    if (!modFolder) m.push_back(menuItem(kNewFolder, folder.empty() ? L"New folder\x2026" : L"New folder in this folder\x2026"));
    m.push_back(menuSeparator());
    if (s) m.push_back(menuItem(kCopyText, L"Copy as text"));
    m.push_back(menuItem(kImportText, L"Import from clipboard"));
    m.push_back(menuSeparator());
    m.push_back(menuItem(ID_PAL_LIST, L"&List", {}, false, !vg));
    m.push_back(menuItem(ID_PAL_ICONS, L"&Grid", {}, false, vg != 0));
    m.push_back(menuSeparator());
    m.push_back(menuItem(ID_PAL_NORMAL, L"&Normal", {}, false, vs == 0));
    m.push_back(menuItem(ID_PAL_LARGE, L"L&arge", {}, false, vs == 1));
    m.push_back(menuItem(ID_PAL_XLARGE, L"E&xtra large", {}, false, vs == 2));
    const TerrainStamp copy = s ? *s : TerrainStamp{};  // (a menu's run reads the list again)
    draw::Host* host = from.host();
    sdlPopupMenu(from, at, sdlModelItems(m, [copy, host, folder, list](int id) {
        if (id == kMask) return tilesMaskToggle(stampTilesOf(copy));
        if (id == kNewFolder) return shellPost([folder] { stampsNewFolder(folder); });
        if (id == kRenameFolder) return shellPost([folder] { stampsRenameFolder(folder); });
        if (id == kDeleteFolder) return shellPost([folder] { stampsDeleteFolder(folder); });
        if (id == kCopyText) return stampsCopyText(copy, host);
        if (id == kImportText) {
            const std::string text = host ? host->clipboardText() : std::string();
            return shellPost([text] { stampsImportText(text); });
        }
        if (id == kRename) return shellPost([copy] { stampsRename(copy); });
        if (id == kDelete) return shellPost([copy] { stampsDelete(copy); });
        if (id == kReveal) return shellRevealFile(copy.file);
        if (id == ID_PAL_LIST || id == ID_PAL_ICONS) return list ? list->setView(id == ID_PAL_ICONS, list->viewSize()) : void();
        if (id == ID_PAL_NORMAL || id == ID_PAL_LARGE || id == ID_PAL_XLARGE) return list ? list->setView(list->viewGrid(), id == ID_PAL_NORMAL ? 0 : id == ID_PAL_LARGE ? 1 : 2) : void();
    }), sdlFeedbackOn(from, at, L"the Custom list"));
}

// A right click's menu of a core view: `m` at the mouse in `from`, with Report feedback on `on`; `pick` runs with the
// item's id.
void shellMenuAt(draw::View& from, draw::Point at, const MenuModel& m, const std::wstring& on, std::function<void(int)> pick) {
    sdlPopupMenu(from, at, sdlModelItems(m, std::move(pick)), sdlFeedbackOn(from, at, on));
}
void shellMenu(draw::View& from, const MenuModel& m, const std::wstring& on, std::function<void(int)> pick) {
    shellMenuAt(from, gHost ? gHost->mouse(from) : draw::Point{}, m, on, std::move(pick));
}

// The Custom list's Sort and View buttons (core/views_stamps.inc): their menus under the button.
void shellStampsButtonMenu(draw::View& from, bool sort) {
    StampsListView* list = stampsListOf(&from);  // (its view: the panel's own)
    const int vg = list ? list->viewGrid() : stampsGrid(), vs = list ? list->viewSize() : stampsSize();
    MenuModel m;
    if (sort) {
        m.push_back(menuItem(ID_PAL_SORT_NAME, L"Name: &A to Z", {}, false, stampsSort() == 0));
        m.push_back(menuItem(ID_PAL_SORT_DATE, L"Date: &newest first", {}, false, stampsSort() == 1));
    } else {
        m.push_back(menuItem(ID_PAL_LIST, L"&List", {}, false, !vg));
        m.push_back(menuItem(ID_PAL_ICONS, L"&Grid", {}, false, vg != 0));
        m.push_back(menuSeparator());
        m.push_back(menuItem(ID_PAL_NORMAL, L"&Normal", {}, false, vs == 0));
        m.push_back(menuItem(ID_PAL_LARGE, L"L&arge", {}, false, vs == 1));
        m.push_back(menuItem(ID_PAL_XLARGE, L"E&xtra large", {}, false, vs == 2));
    }
    const draw::Point at{0, from.height()};
    sdlPopupMenu(from, at, sdlModelItems(m, [list](int id) {
        if (id == ID_PAL_SORT_NAME || id == ID_PAL_SORT_DATE) return setStampsSort(id == ID_PAL_SORT_DATE ? 1 : 0);
        if (id == ID_PAL_LIST || id == ID_PAL_ICONS) return list ? list->setView(id == ID_PAL_ICONS, list->viewSize()) : void();
        if (id == ID_PAL_NORMAL || id == ID_PAL_LARGE || id == ID_PAL_XLARGE) return list ? list->setView(list->viewGrid(), id == ID_PAL_NORMAL ? 0 : id == ID_PAL_LARGE ? 1 : 2) : void();
    }), sdlFeedbackOn(from, at, sort ? L"the Custom list's Sort button" : L"the Custom list's View button"));
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

// The second (or the third) palette's rows: its mode's items, with its mode's search (the box of its row shows it).
// Locations: the map's locations, in Locations' order (as the Locations list).
PaletteMemory dockedPaletteGet(int tab, Mode m);
void rebuildOtherRows(PalettePanel& list, Mode m) {
    int pinned = 0;
    OtherSearchRow& row = &list == &gPal2List ? gSearchRow2 : gSearchRow3;
    const std::wstring search = m == Mode::Locations || m == Mode::Layers ? std::wstring() : dockedPaletteGet(activeDocKey(), m).search;
    row.mode = row.catcher.mode = row.ownerCatch.mode = row.sectionCatch.mode = m;
    row.box.setPlaceholder(paletteSearchCue(m));
    if (row.box.text() != search) row.box.setText(search);
    sortTipsSdl();
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
    const std::vector<PaletteUnit> units = paletteUnitsOf(m);
    std::wstring section;
    if (m == Mode::Units || m == Mode::Doodads || m == Mode::Sprites) {
        const std::vector<std::wstring> names = paletteSectionNames(units);
        std::vector<kit::DropdownItem> items;
        items.push_back({m == Mode::Doodads ? shellTr(L"All doodads") : m == Mode::Sprites ? shellTr(L"All sprites") : shellTr(L"All units")});
        for (const auto& n : names) items.push_back({n});
        row.section.setItems(std::move(items));
        section = dockedPaletteGet(activeDocKey(), m).section;
        const auto it = std::find(names.begin(), names.end(), section);
        if (it == names.end()) section.clear();
        row.section.setValue(it == names.end() ? 0 : int(1 + (it - names.begin())));
    }
    list.searchWords = lowerPanelText(search);
    std::vector<PaletteRow> rows = paletteRows(units, m, list.searchWords, section, pinned, gSortByMode[int(m)]);
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
    gPalList.pictureBudget = gPal2List.pictureBudget = gPal3List.pictureBudget = panelPictureBudget;
    // A click picks the item (Ctrl adds to the pick; Shift picks every item from the last pick, as on Windows); the second
    // and the third palette pick in their own mode (pickInOtherMode). The keyboard goes to the map after a pick (Windows:
    // SetFocus on the view), so the map's keys and undo run.
    gPalList.picked = [](int row, unsigned mods) {
        const int v = gPalList.rows[size_t(row)].value;
        if ((mods & draw::kAlt) && gPalList.mode == Mode::Terrain) {  // (the mask; the pick stays)
            paletteMaskToggle(gState, v);
            gPalList.invalidate();
            return invalidateMapView();
        }
        const bool ctrl = (mods & draw::kCtrl) != 0;
        // (isometric terrain: Shift+click with no pick before adds it, as Ctrl+click)
        const bool second = (mods & draw::kShift) && gState.mode == Mode::Terrain && !gState.terrainRect;
        if (!((mods & draw::kShift) && paletteShiftPick(gState, gPalList.rows, row, ctrl))) pickUnit(gState, v, ctrl || second);
        invalidateMapView();
        shellFocusView();
    };
    gPal2List.picked = [](int row, unsigned mods) {
        const int v = gPal2List.rows[size_t(row)].value;
        if ((mods & draw::kAlt) && gPal2List.mode == Mode::Terrain) {  // (the mask)
            paletteMaskToggle(gState, v);
            gPal2List.invalidate();
            return invalidateMapView();
        }
        pickInOtherModeMods(gPal2List.state, gPal2List.rows, row, v, mods);
        gPal2List.invalidate();
        invalidateMapView();
        shellFocusView();
    };
    gPalList.context = [](int row, draw::Point pt) { paletteMenuSdl(gPalList, gPalList, pt, row); };
    gPal2List.context = [](int row, draw::Point pt) { paletteMenuSdl(gPal2List, gPal2List, pt, row); };
    gPal3List.picked = [](int row, unsigned mods) {
        const int v = gPal3List.rows[size_t(row)].value;
        if ((mods & draw::kAlt) && gPal3List.mode == Mode::Terrain) {  // (the mask)
            paletteMaskToggle(gState, v);
            gPal3List.invalidate();
            return invalidateMapView();
        }
        pickInOtherModeMods(gPal3List.state, gPal3List.rows, row, v, mods);
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

    gFilterSdl.setPlaceholder(paletteSearchCue(Mode::Locations));
    gFilterSdl.onChange = [] { rebuildList(gState); };
    gSearchSdl.setPlaceholder(paletteSearchCue(Mode::Units));  // (each mode sets its own: rebuildList)
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
