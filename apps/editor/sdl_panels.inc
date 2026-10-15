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

// A row of a palette: an item (value), or a section's name (value -1: a header that picks nothing).
struct PanelRow {
    int value = -1;
    std::wstring label;
};
std::vector<PanelRow> gPalRows;   // the palette's rows (the mode's items, filtered and sectioned)
std::vector<PanelRow> gPal2Rows;  // the second palette's rows (its mode's items, unfiltered)
Mode gPalRowsMode = Mode::Units;   // the mode the rows were made for (their pictures are of it)
Mode gPal2RowsMode = Mode::Units;
std::vector<std::wstring> gListRows;  // the Locations list's rows (rebuildList fills them)
std::vector<std::wstring> gSectionNames;  // the section box's sections, after its "All ..." entry
Mode gSectionMode = Mode::Units;          // the mode the section box was filled for
std::wstring gPalSection;                 // the section the palette shows ("" is every one)

void invalidateMapView();  // (sdl_main_view.inc: the map view is drawn again)

// The palette's list: a click picks the item under it (Ctrl adds to the pick, as Win32), a header row picks nothing.
// The second palette's click picks in its own mode (SecondPaletteScope). A pick gives the keyboard to the map (as Win32's
// SetFocus on the view), so the map's keys and undo run after it.
struct PalettePanel final : kit::ListView {
    bool second = false;
    bool mouse(const draw::MouseEvent& e) override {
        const bool down = e.kind == draw::MouseKind::Down && e.button == 0;
        const int row = down ? rowAt(e.pt) : -1;
        const bool handled = kit::ListView::mouse(e);
        const std::vector<PanelRow>& rows = second ? gPal2Rows : gPalRows;
        if (row >= 0 && row < int(rows.size()) && rows[size_t(row)].value >= 0) {
            const int v = rows[size_t(row)].value;
            const bool add = (e.mods & draw::kCtrl) != 0;
            if (second) {
                SecondPaletteScope scope;
                pickUnit(gState, v, add);
            } else {
                pickUnit(gState, v, add);
            }
            invalidate();
            invalidateMapView();
            shellFocusView();
        }
        return handled;
    }
};

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
kit::Button gViewBtnSdl{L"View", kit::Tone::Normal, [] {
    setMessage(shellTr(L"The palette shows its list on this system; the other views are not here yet"));
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

// The rows of a palette over `units`: the ones the search and the section keep, with a header at each section's start
// (the Win32 palette's rule: the same section order, the search's words first, the fuzzy match when none has them).
std::vector<PanelRow> panelRows(const std::vector<PaletteUnit>& units, const std::wstring& search, const std::wstring& section) {
    const std::wstring filter = lowerPanelText(search);
    const bool literal = paletteLiteralSearch(units, filter);
    std::vector<PanelRow> rows;
    const PaletteUnit* prev = nullptr;
    for (const PaletteUnit& u : units) {
        if (!paletteUnitShown(u, literal, filter, section)) continue;
        if (!prev || prev->order != u.order) rows.push_back({-1, u.section});
        rows.push_back({u.value, u.label});
        prev = &u;
    }
    return rows;
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

// The row's look: a pick (the palette's choice) is the selection color; a header is dim. An item has its picture at the
// left (the Win32 palette's: a square, a terrain swatch twice as wide) and its name after it.
// `chosen` and `palette` are the palette's own picks (the second palette's are its own, not the active mode's).
void paintPanelRow(draw::Canvas& c, const std::vector<PanelRow>& rows, const std::vector<int>& chosen, int palette, int i,
                   draw::Rect r, kit::RowState s, Mode what) {
    if (i < 0 || i >= int(rows.size())) return;
    const PanelRow& row = rows[size_t(i)];
    const int pad = scale(8);
    if (row.value < 0) {
        c.text({r.left + pad, r.top, r.right - pad, r.bottom}, row.label, kDim, draw::kMiddle);
        return;
    }
    const bool picked = palette == row.value || std::find(chosen.begin(), chosen.end(), row.value) != chosen.end();
    if (picked) c.fill(r, kSel);
    else if (s.hot) c.fill(r, kRowHot);
    const int bw = (r.bottom - r.top) * (what == Mode::Terrain ? 2 : 1);
    const draw::Rect box{r.left + scale(4), r.top + scale(2), r.left + scale(4) + bw, r.bottom - scale(2)};
    if (draw::Bitmap* b = paletteBitmap(what, row.value, gState.playerOwner)) drawPaletteArt(c, *b, box);
    else if (what == Mode::Terrain) c.roundBox(box, draw::rgb(6, 6, 10), kBorder, float(scale(3)));  // nothing to show (Space): an empty swatch, as Win32
    c.text({box.right + scale(8), r.top, r.right - pad, r.bottom}, row.label, picked ? kAccent : kText, draw::kMiddle);
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
    gPalRows = panelRows(units, gSearchSdl.text(), gPalSection);
    gPalRowsMode = gState.mode;
    gPalList.setCount(int(gPalRows.size()));
    gPalList.invalidate();
}

// The second palette's rows: its mode's items, unfiltered (it has no search box).
void rebuildSecondRows() {
    gPal2Rows = panelRows(paletteUnitsOf(gPal2Mode), std::wstring(), std::wstring());
    gPal2RowsMode = gPal2Mode;
    gPal2List.setCount(int(gPal2Rows.size()));
    gPal2List.invalidate();
}

// The views' wiring, once (MainView's constructor): the rows, the look, the changes that rebuild them.
void initPanels() {
    static bool done = false;
    if (done) return;
    done = true;
    gPalList.setRowHeight(30);
    gPal2List.setRowHeight(30);
    gLocList.setRowHeight(30);
    gPal2List.second = true;
    gPalList.setEmptyHint(shellTr(L"Nothing to list"));
    gPal2List.setEmptyHint(shellTr(L"Nothing to list"));
    gPalList.paintRow = [](draw::Canvas& c, int i, draw::Rect r, kit::RowState s) {
        paintPanelRow(c, gPalRows, shellPaletteChosen(), gState.palette, i, r, s, gPalRowsMode);
    };
    gPal2List.paintRow = [](draw::Canvas& c, int i, draw::Rect r, kit::RowState s) {
        paintPanelRow(c, gPal2Rows, gPal2Chosen, -1, i, r, s, gPal2RowsMode);
    };
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
        gPalList.setScrollTop(0);
        rebuildPaletteRows();
    };
    gSectionDropSdl.dropdown().onChange = [](int i) {
        i = std::clamp(i, 0, int(gSectionNames.size()));
        gPalSection = i == 0 ? std::wstring() : gSectionNames[size_t(i - 1)];
        gPalList.setScrollTop(0);
        rebuildPaletteRows();
    };
    for (int s = 0; s < 2; ++s) gPanelTabsSdl[s].side = s;
}
