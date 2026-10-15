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
    ed.selectLocation(loc);
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
       ID_MAPMENU_FILLSEL };
int gMapMenuUnit = -1, gMapMenuLocation = -1, gMapMenuDoodad = -1, gMapMenuSprite = -1;

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
    if (!app.ed || !app.ed->isOpen()) return m;
    Editor& ed = *app.ed;
    bool has = false, canPaste = false;
    switch (app.mode) {
    case Mode::Units: {
        const int u = ed.unitAt(int(wx), int(wy));
        if (u >= 0 && !ed.isSelected(u)) ed.select(u, false);
        gMapMenuUnit = u;
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
        if (loc >= 0) {
            ed.selectLocation(loc);
            setListSel(int(std::find(app.items.begin(), app.items.end(), loc) - app.items.begin()));
        }
        gMapMenuLocation = loc;
        has = ed.selectedLocation() >= 0, canPaste = app.hasLocationClip;
        break;
    }
    case Mode::Terrain: has = app.hasTerrainSel, canPaste = ed.hasTileClipboard() || ed.hasTerrainClipboard(); break;  // either copy (#83)
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
    if (terrain && app.terrainRect) item(ID_MAPMENU_FILLSEL, L"&Fill selection with the selected tiles", L"", has && gTiles.picked >= 0);  // (#114)
    m.push_back(menuSeparator());
    item(ID_EDIT_SELECTALL, L"Select &all", L"Ctrl+A", !terrain && app.mode != Mode::Locations);
    // On a unit, doodad or sprite: all of its type on the map (units: also only its player's).
    if (gMapMenuUnit >= 0 || gMapMenuDoodad >= 0 || gMapMenuSprite >= 0) item(ID_MAPMENU_SAMETYPE, L"Select all of this t&ype", L"", true);
    if (gMapMenuUnit >= 0)
        m.push_back(menuItem(ID_MAPMENU_SAMETYPE_PLAYER, L"Select all of this type for Player " + std::to_wstring(ed.units()[size_t(gMapMenuUnit)].owner + 1)));
    item(ID_MAPMENU_SELECT, L"&Select (no placing)", L"Esc", !inSelection());
    if (gMapMenuUnit >= 0) {
        m.push_back(menuSeparator());
        item(ID_MAPMENU_PROPS, L"P&roperties\x2026", L"", true);
    }
    if (gMapMenuLocation >= 0) {  // the location's own menu after these
        m.push_back(menuSeparator());
        for (MenuItem& it : locationMenuModel(gMapMenuLocation))
            if (it.id != ID_LOCMENU_DELETE) m.push_back(std::move(it));  // Delete is above
    }
    return m;
}

// What a pick of the map's menu does (the id of its item).
void mapMenuPick(int pick) {
    AppState& app = appState();
    if (pick == ID_MAPMENU_PROPS && gMapMenuUnit >= 0) showUnitProperties(gMapMenuUnit);
    else if (pick == ID_MAPMENU_SELECT) enterSelection(), setMessage(std::wstring(kModeNames[int(app.mode)]) + L": select");
    else if (pick == ID_MAPMENU_REPLACE) shellShowQuickPicker(true, false);
    else if (pick == ID_MAPMENU_FILLSEL) tilesFillSelection();
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
