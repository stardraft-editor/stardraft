// The map's right-click menu and a location's own menu (portable, core.inc): their items as a menu model
// (core/menu_model.inc) and what a pick does. main.cpp shows the model as its painted Win32 menu (toHmenu, popupMenu);
// the SDL program shows it as a kit Menu (sdl_map_dialogs.inc). The location's rename is here too (the text prompt).

void command(int id);
void showLocationEdit(int loc);
void showUnitProperties(int index);
void setListSel(int i);
// The text prompt (core/views_text_prompt.inc; each program's host): OK puts the typed text into `text` and returns true.
bool prompt(const wchar_t* title, std::wstring& text);

// F2 or the menu: the selected location gets the name typed in the prompt.
void renameSelectedLocation() {
    AppState& app = appState();
    Editor& ed = *app.ed;
    int i = ed.selectedLocation();
    if (!ed.locationUsed(i)) return;
    std::wstring name = widen(ed.locationName(i));
    if (prompt(L"Rename location", name) && !name.empty()) {
        ed.renameLocation(i, narrow(name));
        rebuildList(app);
        app.dirty = true;
    }
}

// A location's right-click menu (Locations tool): rename, delete, and the elevations that are in the location
// (MRGN elevation flags; triggers see units only at the elevations that are checked).
enum { ID_LOCMENU_RENAME = 1, ID_LOCMENU_DELETE, ID_LOCMENU_ALL, ID_LOCMENU_PROPS, ID_LOCMENU_ELEV = 10 /* + the bit */ };

MenuModel locationMenuModel(int loc) {
    MenuModel m{menuItem(ID_LOCMENU_PROPS, L"P&roperties\x2026", L"Enter"), menuItem(ID_LOCMENU_RENAME, L"Re&name", L"F2"),
                menuItem(ID_LOCMENU_DELETE, L"&Delete", L"Del"), menuSeparator(), menuItem(0, L"Elevations in the location", {}, true)};
    const uint16_t excluded = appState().ed->locationElevation(loc);
    for (int k = 0; k < 6; ++k) m.push_back(menuItem(ID_LOCMENU_ELEV + k, kElevationNames[k], {}, false, !(excluded & (1 << k))));
    m.push_back(menuItem(ID_LOCMENU_ALL, L"&All elevations", {}, !excluded));
    return m;
}

void locationMenuPick(int loc, int pick) {
    AppState& app = appState();
    Editor& ed = *app.ed;
    if (!ed.locationUsed(loc)) return;
    if (ed.locationSelected(loc)) ed.addLocationSelection(loc);  // (Delete: the selected locations, as Del)
    else ed.selectLocation(loc);
    if (pick == ID_LOCMENU_PROPS) showLocationEdit(loc);
    else if (pick == ID_LOCMENU_RENAME) renameSelectedLocation();
    else if (pick == ID_LOCMENU_DELETE) command(ID_EDIT_DELETE);
    else if (pick == ID_LOCMENU_ALL && ed.setLocationElevation(loc, 0)) setMessage(widen(ed.locationName(loc)) + L": all elevations are in the location");
    else if (pick >= ID_LOCMENU_ELEV && pick < ID_LOCMENU_ELEV + 6) {
        const uint16_t bit = uint16_t(1 << (pick - ID_LOCMENU_ELEV));
        ed.setLocationElevation(loc, ed.locationElevation(loc) ^ bit);
        const bool in = !(ed.locationElevation(loc) & bit);
        setMessage(widen(ed.locationName(loc)) + L": " + kElevationNames[pick - ID_LOCMENU_ELEV] + (in ? L" is in the location" : L" is not in the location"));
    }
    app.dirty = true;
}

// The map's right-click menu, for the active mode: what is under the mouse is selected first (when it is not
// already), then Cut, Copy, Paste, Duplicate, Delete and Select all act on the mode's selection and its own copy
// buffer. Units add Properties; a location adds its own menu (rename, elevations).
enum { ID_MAPMENU_PROPS = 9001, ID_MAPMENU_SELECT, ID_MAPMENU_REPLACE, ID_MAPMENU_SAMETYPE, ID_MAPMENU_SAMETYPE_PLAYER, ID_MAPMENU_TOTERRAIN,
       ID_MAPMENU_FILLSEL, ID_MAPMENU_STACK, ID_MAPMENU_SAVESTAMP, ID_MAPMENU_COPYTILE, ID_MAPMENU_FEEDBACKSEL };
int gMapMenuUnit = -1, gMapMenuLocation = -1, gMapMenuDoodad = -1, gMapMenuSprite = -1;
std::wstring tileValueText(const Editor& ed, float wx, float wy);  // (palette_tools.inc)
void shellCopyText(const std::wstring& text);                     // (shell_sdl.inc)
void shellReportFeedback(const std::wstring& on, const std::wstring& text);
std::vector<std::pair<int, int>> terrainSelTiles();  // (map_edit.inc)

// The selected terrain's tiles for a feedback report: each tile value once (as Copy tile value has it) with the places
// it is at; at most 40 values and 6 places each.
std::wstring selectedTilesText(const Editor& ed) {
    std::map<uint16_t, std::vector<std::pair<int, int>>> at;
    std::vector<uint16_t> order;
    for (auto [x, y] : terrainSelTiles()) {
        const uint16_t v = ed.terrain().tiles[size_t(y) * ed.width() + size_t(x)];
        auto& list = at[v];
        if (list.empty()) order.push_back(v);
        list.push_back({x, y});
    }
    std::wstring out = L"\n\n" + std::wstring(tr(L"The selected terrain (tile x, y):"));
    for (size_t i = 0; i < order.size() && i < 40; ++i) {
        const auto& list = at[order[i]];
        out += L"\n" + tileValueText(ed, list.front().first * 32.0f + 16, list.front().second * 32.0f + 16) + L"  at";
        for (size_t k = 0; k < list.size() && k < 6; ++k) out += L" " + std::to_wstring(list[k].first) + L"," + std::to_wstring(list[k].second);
        if (list.size() > 6) out += L" (+" + std::to_wstring(list.size() - 6) + L")";
    }
    if (order.size() > 40) out += L"\n(+" + std::to_wstring(order.size() - 40) + L")";
    return out;
}
float gMapMenuWX = 0, gMapMenuWY = 0;  // the map point that the menu opened on
size_t gMapMenuStack = 0;              // the units at that point (Units mode)

// --- A stack of units: the list of the units at a point -------------------------------------------------------------

// The host shows the list picker at the map view's point (sx, sy) (sdl_map_dialogs.inc): true with the picked value in
// `out` and the modifier keys held at the pick in `mods`.
bool shellPickStackUnit(std::vector<ListPickItem> items, const std::string& current, int sx, int sy, std::string& out, unsigned& mods);

// The rows of the list of a stack (Editor::unitsAt: the top unit first): each unit's icon, name, player and place; the
// value is the unit's index.
std::vector<ListPickItem> unitStackItems(const Editor& ed, const std::vector<int>& stack) {
    std::vector<ListPickItem> items;
    for (const int i : stack) {
        const Unit& u = ed.units()[size_t(i)];
        ListPickItem it;
        it.label = unitDisplayName(u.type) + L", Player " + std::to_wstring(u.owner + 1) + L", at " + std::to_wstring(u.x) + L", " + std::to_wstring(u.y);
        it.value = std::to_string(i);
        it.iconDat = "units", it.icon = u.type;
        items.push_back(std::move(it));
    }
    return items;
}

// Alt+click on a stack, or the map menu's "Select a unit here...": the list of the units at the map point (wx, wy), at
// the map view's point (sx, sy). A pick selects that unit; with Shift (at the click or at the pick) it is added to the
// selection. The list opens after the event that asks for it (its loop does not run inside the map's mouse event).
void showUnitStack(float wx, float wy, int sx, int sy, bool add) {
    shellPost([wx, wy, sx, sy, add] {
        AppState& app = appState();
        if (!app.ed || !app.ed->isOpen()) return;
        Editor& ed = *app.ed;
        const std::vector<int> stack = ed.unitsAt(int(wx), int(wy));
        if (stack.empty()) return;
        std::string current;  // (the check mark: the selected unit of the stack)
        for (const int u : stack)
            if (current.empty() && ed.isSelected(u)) current = std::to_string(u);
        std::string out;
        unsigned mods = 0;
        if (!shellPickStackUnit(unitStackItems(ed, stack), current, sx, sy, out, mods)) return;
        const int u = std::atoi(out.c_str());
        const auto at = std::find(stack.begin(), stack.end(), u);
        if (out.empty() || at == stack.end() || u >= int(ed.units().size())) return;
        const bool plus = add || (mods & draw::kShift);
        ed.select(u, plus);
        if (!plus) gStackChosen = u;  // (a press on it keeps it: a drag moves it)
        const Unit& un = ed.units()[size_t(u)];
        setMessage((plus ? L"Added unit " : L"Selected unit ") + std::to_wstring(at - stack.begin() + 1) + L" of " + std::to_wstring(stack.size()) +
                   L" here: " + unitDisplayName(un.type) + L", Player " + std::to_wstring(un.owner + 1));
        app.dirty = true;
    });
}

// "Select all of this type": every unit (of `forPlayer`: only the same player's), doodad or sprite on the map of the
// same type as the one the menu was opened on, in place of the mode's selection. Returns how many are selected.
int selectSameType(bool forPlayer) {
    AppState& app = appState();
    if (!app.ed) return 0;
    Editor& ed = *app.ed;
    int n = 0;
    std::wstring what;
    if (gMapMenuUnit >= 0 && gMapMenuUnit < int(ed.units().size())) {
        const Unit u = ed.units()[size_t(gMapMenuUnit)];
        n = ed.selectUnitsOfType(u.type, forPlayer ? int(u.owner) : -1);
        what = (n == 1 ? L" unit (" : L" units (") + unitDisplayName(u.type) + (forPlayer ? L", Player " + std::to_wstring(u.owner + 1) : L"") + L")";
    } else if (gMapMenuDoodad >= 0 && gMapMenuDoodad < int(ed.doodads().size())) {
        const uint16_t id = ed.doodads()[size_t(gMapMenuDoodad)].id;
        app.selDoodads = ed.doodadsOfType(id);
        n = int(app.selDoodads.size());
        what = (n == 1 ? L" doodad (" : L" doodads (") + paletteItemName(id) + L")";
    } else if (gMapMenuSprite >= 0 && gMapMenuSprite < int(ed.sprites().size())) {
        const MapSprite s = ed.sprites()[size_t(gMapMenuSprite)];
        app.selSprites = ed.spritesOfType(gMapMenuSprite);
        n = int(app.selSprites.size());
        what = (n == 1 ? L" sprite (" : L" sprites (") + ((s.flags & SpriteFlag::DrawAsSprite) ? entryName("sprites", s.id) : unitDisplayName(s.id)) + L")";
    } else return 0;
    setMessage(L"Selected " + std::to_wstring(n) + what);
    app.dirty = true;
    return n;
}

// The menu at the map point (wx, wy): what is under it is selected first.
MenuModel mapMenuModel(float wx, float wy) {
    AppState& app = appState();
    MenuModel m;
    gMapMenuUnit = gMapMenuLocation = gMapMenuDoodad = gMapMenuSprite = -1;
    gMapMenuStack = 0;
    if (!app.ed || !app.ed->isOpen()) return m;
    Editor& ed = *app.ed;
    bool has = false, canPaste = false;
    switch (app.mode) {
    case Mode::Units: {
        // A stack: the unit that a click again or the list selected stays selected; else the top one.
        const size_t stack = ed.unitsAt(int(wx), int(wy)).size();
        const int u = stackUnitAt(ed, wx, wy);
        if (u >= 0 && !ed.isSelected(u)) ed.select(u, false);
        gMapMenuUnit = u, gMapMenuWX = wx, gMapMenuWY = wy, gMapMenuStack = stack;
        has = !ed.selection().empty(), canPaste = ed.canPaste();
        break;
    }
    case Mode::Doodads:
    case Mode::Sprites: {
        const bool dd = app.mode == Mode::Doodads;
        auto& sel = dd ? app.selDoodads : app.selSprites;
        const int hit = dd ? ed.doodadAt(int(wx), int(wy)) : mapSpriteAt(*app.ed, int(wx), int(wy));
        if (hit >= 0 && std::find(sel.begin(), sel.end(), hit) == sel.end()) sel = {hit};
        (dd ? gMapMenuDoodad : gMapMenuSprite) = hit;
        has = !sel.empty(), canPaste = !(dd ? app.doodadClip : app.spriteClip).empty();
        break;
    }
    case Mode::Locations: {
        const int loc = ed.locationAt(int(wx), int(wy));
        if (loc >= 0 && ed.locationSelected(loc)) ed.addLocationSelection(loc);  // (one of the selected: they stay selected)
        else if (loc >= 0) ed.selectLocation(loc);
        gMapMenuLocation = loc;
        has = ed.selectedLocation() >= 0, canPaste = !app.locationClip.empty();
        break;
    }
    case Mode::Terrain:
        has = app.hasTerrainSel, canPaste = ed.hasTileClipboard() || ed.hasTerrainClipboard();  // either copy (#83)
        gMapMenuWX = wx, gMapMenuWY = wy;  // (Copy tile value: the tile here)
        break;
    }
    auto item = [&](int id, const wchar_t* text, const wchar_t* keys, bool on) { m.push_back(menuItem(id, text, keys, !on)); };
    const bool terrain = app.mode == Mode::Terrain;
    item(ID_EDIT_CUT, L"Cu&t", L"Ctrl+X", has);  // terrain: null tiles in its place
    item(ID_EDIT_COPY, L"&Copy", L"Ctrl+C", has);
    item(ID_EDIT_PASTE, L"&Paste", L"Ctrl+V", canPaste);
    item(ID_EDIT_DUPLICATE, L"D&uplicate", L"Ctrl+D", has && !terrain);
    item(ID_EDIT_DELETE, L"&Delete", L"Del", has && !terrain);
    item(ID_MAPMENU_REPLACE, L"&Replace with\x2026", L"", has && (app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites));
    if (app.mode == Mode::Doodads) item(ID_MAPMENU_TOTERRAIN, L"Convert to t&errain", L"", has);
    if (terrain && app.terrainRect)  // (#114; Solid: the tiles inside its terrain)
        item(ID_MAPMENU_FILLSEL, gTiles->solid ? L"&Fill selection with the selected terrain" : L"&Fill selection with the selected tiles", L"", has && tilesHavePick());
    if (terrain) item(ID_MAPMENU_SAVESTAMP, L"Save as custom &terrain\x2026", L"", has);  // (core/stamps.inc)
    // (as the status bar shows it: tileValueText; with a selection, each of its tiles)
    if (terrain) item(ID_MAPMENU_COPYTILE, has ? L"Copy tile &values" : L"Copy tile &value", L"", true);
    m.push_back(menuSeparator());
    item(ID_EDIT_SELECTALL, L"Select &all", L"Ctrl+A", !terrain);
    // On a unit, doodad or sprite: all of its type on the map (units: also only its player's).
    if (gMapMenuUnit >= 0 || gMapMenuDoodad >= 0 || gMapMenuSprite >= 0) item(ID_MAPMENU_SAMETYPE, L"Select all of this t&ype", L"", true);
    if (gMapMenuUnit >= 0)
        m.push_back(menuItem(ID_MAPMENU_SAMETYPE_PLAYER, L"Select all of this type for Player " + std::to_wstring(ed.units()[size_t(gMapMenuUnit)].owner + 1)));
    if (gMapMenuStack > 1) item(ID_MAPMENU_STACK, L"Select a unit &here\x2026", L"Alt+click", true);  // (a stack: the list of its units)
    item(ID_MAPMENU_SELECT, L"&Select (no placing)", L"Esc", !inSelection());
    if (app.mode == Mode::Units) {  // the ranges of the selected units (as View > Unit vision and ranges)
        MenuModel ranges{menuItem(ID_VIEW_RANGES, L"&All ranges", {}, false, gRangeKinds == kRangeAll), menuSeparator()};
        for (int k = 0; k < kRangeKindCount; ++k) ranges.push_back(menuItem(ID_VIEW_RANGEKIND + k, kRangeKinds[k].menu, {}, false, rangeShown(k)));
        m.push_back(menuSeparator());
        m.push_back(menuPopup(L"&Unit vision and ranges", std::move(ranges)));
    }
    if (gMapMenuUnit >= 0) {
        m.push_back(menuSeparator());
        item(ID_MAPMENU_PROPS, L"P&roperties\x2026", L"", true);
    }
    if (gMapMenuLocation >= 0) {  // the location's own menu after these
        m.push_back(menuSeparator());
        for (MenuItem& it : locationMenuModel(gMapMenuLocation))
            if (it.id != ID_LOCMENU_DELETE) m.push_back(std::move(it));  // Delete is above
    }
    // A terrain selection: the menu's Report feedback is on it, its tiles in the box (in the place of "on the map": the
    // shell adds that one only when this is not here, mapRightClickSdl).
    if (terrain && has) {
        m.push_back(menuSeparator());
        item(ID_MAPMENU_FEEDBACKSEL, L"Report feedback on the selected terrain\x2026", L"", true);
    }
    return m;
}

// What a pick of the map's menu does (the id of its item).
void mapMenuPick(int pick) {
    AppState& app = appState();
    if (pick == ID_MAPMENU_PROPS && gMapMenuUnit >= 0) showUnitProperties(gMapMenuUnit);
    else if (pick == ID_MAPMENU_STACK) showUnitStack(gMapMenuWX, gMapMenuWY, app.mouseX, app.mouseY, false);
    else if (pick == ID_MAPMENU_SELECT) enterSelection(), setMessage(std::wstring(kModeNames[int(app.mode)]) + L": select");
    else if (pick == ID_MAPMENU_REPLACE) shellShowQuickPicker(true, false);
    else if (pick == ID_MAPMENU_FILLSEL) tilesFillSelection();
    else if (pick == ID_MAPMENU_SAVESTAMP) stampsSaveSelection();
    else if (pick == ID_MAPMENU_FEEDBACKSEL && app.ed) shellReportFeedback(L"the selected terrain", selectedTilesText(*app.ed));
    else if (pick == ID_MAPMENU_COPYTILE && app.ed) {
        // A terrain selection: each of its tiles, a line each (its place, then its value), row by row; else the tile here.
        const std::vector<std::pair<int, int>> sel = terrainSelTiles();
        if (sel.empty()) {
            const std::wstring t = tileValueText(*app.ed, gMapMenuWX, gMapMenuWY);
            shellCopyText(t);
            setMessage(tr(L"Copied: ") + t);
        } else {
            std::wstring t;
            for (auto [x, y] : sel)
                t += (t.empty() ? L"" : L"\n") + std::to_wstring(x) + L", " + std::to_wstring(y) + L": " + tileValueText(*app.ed, x * 32.0f + 16, y * 32.0f + 16);
            shellCopyText(t);
            setMessage(L"Copied the values of " + std::to_wstring(sel.size()) + (sel.size() == 1 ? L" tile" : L" tiles"));
        }
    }
    else if (pick == ID_MAPMENU_SAMETYPE || pick == ID_MAPMENU_SAMETYPE_PLAYER) selectSameType(pick == ID_MAPMENU_SAMETYPE_PLAYER);
    else if (pick == ID_MAPMENU_TOTERRAIN && app.ed) {
        // The selected doodads (the one under the mouse is selected by the menu): their tiles stay as plain terrain.
        const int n = app.ed->convertDoodadsToTerrain(app.selDoodads);
        app.selDoodads.clear();
        if (n) setMessage(L"Converted " + std::to_wstring(n) + (n == 1 ? L" doodad" : L" doodads") + L" to terrain");
        app.dirty = true;
    }
    else if (pick < 100 && gMapMenuLocation >= 0) locationMenuPick(gMapMenuLocation, pick);
    else command(pick);
}
