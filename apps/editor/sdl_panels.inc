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
std::vector<int> gPal2Chosen;                        // the second palette's picks (its own list, as the Win32 program's)

// The second palette's pick runs in its mode with its own picks, and the left palette's mode and pick stay (the Win32
// program's SecondPaletteScope): the mode and the picks are swapped for the call.
struct SecondPaletteScope {
    int savedPalette;
    SecondPaletteScope() : savedPalette(gState.palette) {
        std::swap(gState.mode, gPal2Mode);
        std::swap(shellPaletteChosen(), gPal2Chosen);
        gState.palette = -1;
    }
    ~SecondPaletteScope() {
        std::swap(gState.mode, gPal2Mode);
        std::swap(shellPaletteChosen(), gPal2Chosen);
        gState.palette = savedPalette;
    }
};

std::vector<std::wstring> gListRows;  // the Locations list's rows (rebuildList fills them)
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
PalettePanel gPalList, gPal2List;
LocationListPanel gLocList;
kit::Dropdown gOwnerBox;
SearchBox gFilterSdl;   // the Locations filter
SearchBox gSearchSdl;   // the palette's search
DropdownBox gSectionDropSdl;  // the section box (Units, Doodads, Sprites)
void paletteMenuSdl(PaletteGridView& pal, draw::View& from, draw::Point at, int row);
kit::Button gViewBtnSdl{L"View", kit::Tone::Normal, [] {  // the View menu under the button (as the Windows View button)
    paletteMenuSdl(gPalList, gViewBtnSdl, {0, gViewBtnSdl.height()}, -1);
}};
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
    draw::Bitmap* b = paletteBitmap(what, value, gState.playerOwner, false, sprites);
    if (b) drawPaletteArt(c, *b, box);
    return b != nullptr;
}

// The View menu (core/palette_view.inc: paletteMenuModel) of the palette `pal` at `at` of `from`: on its item of `row`
// (-1: none), a doodad of a project gets Pin to the top or Unpin first. A view item changes the view of the palette's
// mode (saved in the settings), a section scrolls the list to it.
void paletteMenuSdl(PaletteGridView& pal, draw::View& from, draw::Point at, int row) {
    const int value = row >= 0 && row < int(pal.rows.size()) ? pal.rows[size_t(row)].value : -1;
    const Mode mode = pal.mode;
    std::vector<int> sections;  // the rows of the list's sections (units, doodads, sprites)
    if (mode == Mode::Units || mode == Mode::Doodads || mode == Mode::Sprites)
        for (size_t i = 0; i < pal.rows.size(); ++i)
            if (pal.rows[i].value < 0) sections.push_back(int(i));
    std::vector<std::wstring> names;
    for (int i : sections) names.push_back(pal.rows[size_t(i)].label);
    const MenuModel model = paletteMenuModel(mode, gViewByMode[int(mode)], names, palettePinState(mode, value));
    const std::wstring on = sdlFeedbackOn(from, at, L"the palette");
    PaletteGridView* p = &pal;
    sdlPopupMenu(from, at, sdlModelItems(model, [p, value, sections, mode](int id) {
        if (id == kPalPin) return doodadPinToggle(value);
        if (id == ID_VIEW_FLOATPAL) return shellFloatPalette(!gFloatPal.floating);
        if (id >= kPalSectionFirst && id < kPalSectionFirst + int(sections.size())) {
            p->scrollTo(p->rowRect(sections[size_t(id - kPalSectionFirst)]).top);
            return;
        }
        if (applyPaletteViewCommand(mode, id)) {
            gPalList.layout(), gPal2List.layout();  // (both palettes show the view of their mode)
            shellSettingsChanged();
        }
    }), on);
}

// The Locations list's pick (as the Win32 list's LBN_SELCHANGE): the location is selected and the camera goes to it;
// elsewhere the item is the palette's pick, and picking a terrain while selecting paints with it.
void locationListPicked() {
    const int sel = gLocList.cursor();
    const int v = sel >= 0 && sel < int(gState.items.size()) ? gState.items[size_t(sel)] : -1;
    if (gState.mode == Mode::Locations && gState.ed && v >= 0) {
        gState.ed->selectLocation(v);
        const auto r = gState.ed->locationRect(v);
        int w = 0, h = 0;
        shellViewSize(w, h);
        gState.camX = (r.left + r.right) / 2.0f - w / 2.0f / gState.zoom;
        gState.camY = (r.top + r.bottom) / 2.0f - h / 2.0f / gState.zoom;
    } else {
        gState.palette = v;
        if (gState.mode == Mode::Terrain && v > 0 && currentTool(gState) == TSelect && !gState.terrainPasting) {
            gState.toolByMode[int(Mode::Terrain)] = TBrush;
            fillToolCombo(gState);
        }
    }
    gState.dirty = true;
    gLocList.invalidate();
    invalidateMapView();
}

// The palette's rows for the current mode, the section box and the search (the Win32 rebuildUnitPalette).
void rebuildPaletteRows() {
    const std::vector<PaletteUnit> units = paletteUnitsOf(gState.mode);
    fillSectionBox(paletteSectionNames(units));
    int pinned = 0;
    gPalList.searchWords = lowerPanelText(gSearchSdl.text());
    std::vector<PaletteRow> rows = paletteRows(units, gState.mode, gPalList.searchWords, gPalSection, pinned);
    gPalList.setRows(std::move(rows), gState.mode, pinned);
}

// The second palette's rows: its mode's items, unfiltered (it has no search box).
void rebuildSecondRows() {
    int pinned = 0;
    std::vector<PaletteRow> rows = paletteRows(paletteUnitsOf(gPal2Mode), gPal2Mode, std::wstring(), std::wstring(), pinned);
    gPal2List.setRows(std::move(rows), gPal2Mode, pinned);
}

// The views' wiring, once (MainView's constructor): the rows, the look, the changes that rebuild them.
void initPanels() {
    static bool done = false;
    if (done) return;
    done = true;
    gLocList.setRowHeight(30);
    gPal2List.second = true;
    gPalList.chosen = &shellPaletteChosen();
    gPal2List.chosen = &gPal2Chosen;
    gPalList.picture = gPal2List.picture = panelPicture;
    // A click picks the item (Ctrl adds to the pick; Shift picks every item from the last pick, as on Windows); the second
    // palette picks in its own mode with its own picks (SecondPaletteScope). The keyboard goes to the map after it
    // (Windows: SetFocus on the view), so the map's keys and undo run.
    gPalList.picked = [](int row, unsigned mods) {
        const int v = gPalList.rows[size_t(row)].value;
        const bool ctrl = (mods & draw::kCtrl) != 0;
        if (!((mods & draw::kShift) && paletteShiftPick(gState, gPalList.rows, row, ctrl))) pickUnit(gState, v, ctrl);
        invalidateMapView();
        shellFocusView();
    };
    gPal2List.picked = [](int row, unsigned mods) {
        const int v = gPal2List.rows[size_t(row)].value;
        {
            SecondPaletteScope scope;
            pickUnit(gState, v, (mods & draw::kCtrl) != 0);
        }
        gPal2List.invalidate();
        invalidateMapView();
        shellFocusView();
    };
    gPalList.context = [](int row, draw::Point pt) { paletteMenuSdl(gPalList, gPalList, pt, row); };
    gPal2List.context = [](int row, draw::Point pt) { paletteMenuSdl(gPal2List, gPal2List, pt, row); };
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
