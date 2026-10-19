// The Units list (View > Units list; Discord feedback #126; portable, core.inc): every unit of the map with its number,
// its type, its owner and its place (x and y in pixels), and a search by type or owner. While it is on, it is in the side
// bar of the Units mode in place of the palette (the program's panel: sdl_panels.inc). A click selects the unit and the
// view goes to it; Delete deletes the selected units (the map's command); the rows follow the map's changes. The units
// that cannot be picked (a map revealer while its layer is hidden) are not listed, as on the map.

bool gUnitsListOn = false;  // View > Units list: the Units mode's side bar shows the list
bool unitsListOn() { return gUnitsListOn; }

// The owner's name in the list: Player 1 to Player 12, the twelfth being the neutral player (owner 11: Neutral).
std::wstring unitsListOwner(int owner) {
    if (owner == 11) return std::wstring(tr(L"Neutral"));
    return std::wstring(tr(L"Player ")) + std::to_wstring(owner + 1);
}

// The units the list shows, in the map's order (row i is unit rows[i]): each unit that can be picked whose type or owner
// has every word of the search (the words are separated by spaces; the case does not count).
std::vector<int> unitsListRows(const Editor& ed, const std::wstring& search) {
    std::vector<std::wstring> words;
    std::wstring word;
    for (wchar_t c : search + L" ") {
        if (c == L' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word.push_back(static_cast<wchar_t>(std::towlower(c)));
        }
    }
    std::vector<int> rows;
    const auto& units = ed.units();
    for (int i = 0; i < int(units.size()); ++i) {
        if (!ed.pickable(i)) continue;
        std::wstring text = unitDisplayName(units[size_t(i)].type) + L" " + unitsListOwner(units[size_t(i)].owner);
        for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
        bool all = true;
        for (const std::wstring& w : words) all = all && text.find(w) != std::wstring::npos;
        if (all) rows.push_back(i);
    }
    return rows;
}

// The text of a list cell: the number (the unit's place in the map's units, from 1), the type, the owner, x and y.
std::wstring unitsListCell(const Editor& ed, int unit, size_t column) {
    const Unit& u = ed.units()[size_t(unit)];
    switch (column) {
    case 0: return std::to_wstring(unit + 1);
    case 1: return unitDisplayName(u.type);
    case 2: return unitsListOwner(u.owner);
    case 3: return std::to_wstring(u.x);
    default: return std::to_wstring(u.y);
    }
}

// Centers the map view on unit `unit` (a click in the list).
void unitsListCenter(int unit) {
    AppState& app = shellState();
    if (!app.ed || unit < 0 || unit >= int(app.ed->units().size())) return;
    const Unit& u = app.ed->units()[size_t(unit)];
    int w = 0, h = 0;
    viewSize(w, h);
    app.camX = u.x - w / 2.0f / app.zoom, app.camY = u.y - h / 2.0f / app.zoom;
    app.dirty = true;
}

// View > Units list: on, the Units mode is the active one (its side bar has the list); off, the palette shows again.
void toggleUnitsList() {
    gUnitsListOn = !gUnitsListOn;
    if (gUnitsListOn) setMode(Mode::Units);
    setMessage(gUnitsListOn ? std::wstring(tr(L"The units list shows in the side bar. A click selects a unit; Delete deletes the selected units."))
                            : std::wstring(tr(L"The palette shows in the side bar again")));
    shellRelayout();  // (the side bar takes the list's place, or the palette's)
    shellMenusChanged();
    shellState().dirty = true;
}
