// The Units list (View > Units list; Discord feedback #126; portable, core.inc): the list of the map's units in the left
// column of the Unit properties window (views_unit_props.inc), with a search by type or owner. A pick selects the unit on
// the map, the view goes to it and the window shows that unit. Delete deletes the selected units, Ctrl+Z and Ctrl+Y do
// the map's undo and redo, and the list follows the map. View > Units list opens the window (showUnitsList).

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

// Centers the map view on unit `unit` (a pick in the list).
void unitsListCenter(int unit) {
    AppState& app = shellState();
    if (!app.ed || unit < 0 || unit >= int(app.ed->units().size())) return;
    const Unit& u = app.ed->units()[size_t(unit)];
    int w = 0, h = 0;
    viewSize(w, h);
    app.camX = u.x - w / 2.0f / app.zoom, app.camY = u.y - h / 2.0f / app.zoom;
    app.dirty = true;
}

// The Unit properties window of unit `index` (the host: sdl_map_dialogs.inc; it selects the unit and returns when the
// window is closed for good).
void showUnitProperties(int index);

// View > Units list: the Unit properties window with the list (its first selected unit, else the first unit).
void showUnitsList() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    const Editor& ed = *app.ed;
    if (ed.units().empty()) return setMessage(std::wstring(tr(L"The map has no units")));
    showUnitProperties(ed.selection().empty() ? 0 : ed.selection().front());
}
