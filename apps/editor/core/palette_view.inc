// The palette's views (portable, core.inc; moved from main.cpp): a list or a grid (units: a grid of command icons or of
// sprites), normal or large, for each mode; the View menu (the View button and a right click on the palette) with the
// list's sections to go to and, for a doodad in a project, Pin to the top / Unpin; the rows with the pinned doodads
// first; and where each row or cell goes. Both programs use these: the Windows palette (main.cpp) and the SDL palette
// (core/views_palette.inc: PaletteGridView). The views of each mode are kept in the settings file (PaletteViews,
// core/app_settings.inc).

struct PaletteView {
    bool grid = false, sprites = false, large = false;
    bool xl = false;  // Extra large (with large): twice the normal size, to tell pictures such as ramps apart
};
// List or grid (units: icons or sprites) and size, per mode: terrain a large list (its swatches show more), doodads and
// sprites a large grid (their pictures say more than their names).
PaletteView gViewByMode[kModeCount] = {{false, false, true}, {}, {true, false, true}, {}, {true, false, true}};

// The order of each mode's palette (the View menu's Sort items): by category (the sections, each in the game's order:
// units by race and group), by name (A to Z, no sections), by number (the item's id, no sections) or by tier (a mod's
// units with tech tiers: a section each tier, in it the category order).
enum class PaletteSort { Category, Name, Number, Tier };
PaletteSort gSortByMode[kModeCount] = {};
// The orders as one number (2 bits a mode), as the settings file keeps them.
int paletteSortBits() {
    int bits = 0;
    for (int m = 0; m < kModeCount; ++m) bits |= int(gSortByMode[m]) << (m * 2);
    return bits;
}
void setPaletteSortBits(int v) {
    for (int m = 0; m < kModeCount; ++m) gSortByMode[m] = PaletteSort((v >> (m * 2)) & 3);
}

// The views of each mode as one number (3 bits a mode: grid, sprites, large; then a bit a mode from bit 15: extra
// large), as the settings file keeps them.
int paletteViewBits() {
    int bits = 0;
    for (int m = 0; m < kModeCount; ++m) {
        bits |= (int(gViewByMode[m].grid) | int(gViewByMode[m].sprites) << 1 | int(gViewByMode[m].large) << 2) << (m * 3);
        bits |= int(gViewByMode[m].xl) << (15 + m);
    }
    return bits;
}
void setPaletteViewBits(int v) {
    for (int m = 0; m < kModeCount; ++m) {
        const int b = v >> (m * 3);
        gViewByMode[m] = {(b & 1) != 0, (b & 2) != 0, (b & 4) != 0, (b & 4) != 0 && (v >> (15 + m) & 1) != 0};
    }
}

// A View menu item (ID_PAL_LIST, ID_PAL_ICONS, ID_PAL_SPRITES, ID_PAL_NORMAL, ID_PAL_LARGE) on the view of `mode`. Only
// units have sprites: a grid of another mode is a grid of pictures. False: not a view item.
bool applyPaletteViewCommand(Mode mode, int id, PaletteView& v, PaletteSort& sort) {
    if (id == ID_PAL_LIST) v.grid = v.sprites = false;
    else if (id == ID_PAL_ICONS) v.grid = true, v.sprites = false;
    else if (id == ID_PAL_SPRITES) v.grid = v.sprites = true;
    else if (id == ID_PAL_NORMAL || id == ID_PAL_LARGE || id == ID_PAL_XLARGE) v.large = id != ID_PAL_NORMAL, v.xl = id == ID_PAL_XLARGE;
    else if (id >= ID_PAL_SORT_CATEGORY && id <= ID_PAL_SORT_NUMBER) sort = PaletteSort(id - ID_PAL_SORT_CATEGORY);
    else if (id == ID_PAL_SORT_TIER) sort = PaletteSort::Tier;
    else return false;
    if (mode != Mode::Units) v.sprites = false;
    return true;
}
bool applyPaletteViewCommand(Mode mode, int id) { return applyPaletteViewCommand(mode, id, gViewByMode[int(mode)], gSortByMode[int(mode)]); }

constexpr int kPalPin = 0xDF00;  // the View menu's Pin / Unpin item

// The View menu of the palette in `mode` (the View button, or a right click on the palette): Pin to the top or Unpin
// first (`pin`: -1 none, 0 Pin, 1 Unpin: a doodad of a project), then List and Grid (units: icons or sprites), Normal and
// Large, the order, and the panel's items. (The palette's section dropdown goes to a section.) `sort`: the order shown
// checked; `panel`: the menu of a palette panel (Close panel in place of the floating palette's item); `tiers`: the
// units have tech tiers (a mod's), so Sort by tier is there.
MenuModel paletteMenuModel(Mode mode, const PaletteView& v, int pin, PaletteSort sort, bool panel, bool tiers = false) {
    MenuModel m;
    if (pin >= 0) {
        m.push_back(menuItem(kPalPin, pin ? L"Un&pin" : L"&Pin to the top"));
        m.push_back(menuSeparator());
    }
    m.push_back(menuItem(ID_PAL_LIST, L"&List", {}, false, !v.grid));
    if (mode == Mode::Units) {
        m.push_back(menuItem(ID_PAL_ICONS, L"&Grid: icons", {}, false, v.grid && !v.sprites));
        m.push_back(menuItem(ID_PAL_SPRITES, L"Grid: &sprites", {}, false, v.grid && v.sprites));
    } else {
        m.push_back(menuItem(ID_PAL_ICONS, L"&Grid", {}, false, v.grid));
    }
    m.push_back(menuSeparator());
    m.push_back(menuItem(ID_PAL_NORMAL, L"&Normal", {}, false, !v.large));
    m.push_back(menuItem(ID_PAL_LARGE, L"L&arge", {}, false, v.large && !v.xl));
    m.push_back(menuItem(ID_PAL_XLARGE, L"E&xtra large", {}, false, v.xl));
    m.push_back(menuSeparator());
    // (Tier with no tiers here: the rows are by category)
    const bool tierShown = mode == Mode::Units && tiers;
    m.push_back(menuItem(ID_PAL_SORT_CATEGORY, L"Sort by &category", {}, false,
                         sort == PaletteSort::Category || (sort == PaletteSort::Tier && !tierShown)));
    m.push_back(menuItem(ID_PAL_SORT_NAME, L"Sort by na&me", {}, false, sort == PaletteSort::Name));
    m.push_back(menuItem(ID_PAL_SORT_NUMBER, L"Sort by n&umber", {}, false, sort == PaletteSort::Number));
    if (tierShown) m.push_back(menuItem(ID_PAL_SORT_TIER, L"Sort by &tier", {}, false, sort == PaletteSort::Tier));
    // (View > Floating palettes: this mode's palette in a window of its own, or back in the side bar)
    m.push_back(menuSeparator());
    m.push_back(menuItem(ID_PAL_NEW_PANEL, L"&New panel like this"));
    if (panel) m.push_back(menuItem(ID_PAL_CLOSE_PANEL, L"&Close panel"));
    else m.push_back(menuItem(ID_VIEW_FLOATPAL + int(mode), floatPaletteMenuLabel(mode)));
    return m;
}

// A row of the palette: an item (value), or a section's heading (value -1).
struct PaletteRow {
    int value = -1;
    std::wstring label;
};

// The palette's rows over `units` (the mode's items): the ones the search (`filter`, lower case) and the section keep,
// with a heading at each section's start; the search's words first, the fuzzy match when no item has them. Doodads in a
// project: the pinned ones first, under "Pinned", in the order they were pinned (each stays in its place below too);
// `pinned` is how many (they are rows 1 to pinned).
std::vector<PaletteRow> paletteRows(const std::vector<PaletteUnit>& units, Mode mode, const std::wstring& filter, const std::wstring& section,
                                    int& pinned, PaletteSort sort = PaletteSort::Category) {
    std::vector<PaletteRow> rows;
    const bool literal = paletteLiteralSearch(units, filter);
    auto shown = [&](const PaletteUnit& u) { return paletteUnitShown(u, literal, filter, section); };
    pinned = 0;
    if (mode == Mode::Doodads)
        for (uint16_t id : doodadPinsNow())
                for (const PaletteUnit& u : units)
                    if (u.value == id && shown(u)) {
                        if (!pinned) rows.push_back({-1, shellTr(L"Pinned")});
                        rows.push_back({u.value, u.label});
                        ++pinned;
                        break;
                    }
    const bool tiers = sort == PaletteSort::Tier && std::any_of(units.begin(), units.end(), [](const PaletteUnit& u) { return u.tier > 0; });
    if (tiers) {  // a section each tier, the units with no tier last; in each the category order (units' order)
        std::vector<const PaletteUnit*> list;
        for (const PaletteUnit& u : units)
            if (shown(u)) list.push_back(&u);
        auto key = [](const PaletteUnit* u) { return u->tier > 0 ? u->tier : INT_MAX; };
        std::stable_sort(list.begin(), list.end(), [&](const PaletteUnit* a, const PaletteUnit* b) { return key(a) < key(b); });
        for (size_t i = 0; i < list.size(); ++i) {
            if (!i || key(list[i - 1]) != key(list[i]))
                rows.push_back({-1, list[i]->tier > 0 ? shellTr(L"Tier") + L" " + std::to_wstring(list[i]->tier) : shellTr(L"No tier")});
            rows.push_back({list[i]->value, list[i]->label});
        }
        return rows;
    }
    if (sort == PaletteSort::Name || sort == PaletteSort::Number) {  // one list, no sections: by name (a doodad's: its group's, then its own) or number
        std::vector<const PaletteUnit*> list;
        for (const PaletteUnit& u : units)
            if (shown(u)) list.push_back(&u);
        auto name = [mode](const PaletteUnit& u) {
            std::wstring n = mode == Mode::Doodads ? u.section + L" " + u.label : u.label;
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            return n;
        };
        if (sort == PaletteSort::Name)
            std::stable_sort(list.begin(), list.end(), [&](const PaletteUnit* a, const PaletteUnit* b) { return name(*a) < name(*b); });
        else
            std::stable_sort(list.begin(), list.end(), [](const PaletteUnit* a, const PaletteUnit* b) { return a->value < b->value; });
        for (const PaletteUnit* u : list) rows.push_back({u->value, mode == Mode::Doodads && sort == PaletteSort::Name ? u->section + L"  " + u->label : u->label});
        return rows;
    }
    const PaletteUnit* prev = nullptr;
    for (const PaletteUnit& u : units) {
        if (!shown(u)) continue;
        if (!prev || prev->section != u.section) rows.push_back({-1, u.section});  // (a section's units can have orders of their own: a mod's)
        rows.push_back({u.value, u.label});
        prev = &u;
    }
    return rows;
}

// The View menu's pin state for the item `value` of `mode`: -1 no pin item (not a doodad, or no place to keep pins: a map
// not saved yet), 0 Pin, 1 Unpin.
int palettePinState(Mode mode, int value) {
    if (mode != Mode::Doodads || value < 0 || !canPinDoodads()) return -1;  // (the project's, else the map's file: map_prefs.inc)
    const std::vector<uint16_t> pins = doodadPinsNow();
    return std::find(pins.begin(), pins.end(), uint16_t(value)) != pins.end() ? 1 : 0;
}

// Pins a doodad to the top of the Doodads palette, or unpins it; kept at once in the project, else in the map's own file
// (map_prefs.inc); a map not saved yet has no pins.
void doodadPinToggle(int value) {
    if (value < 0 || !canPinDoodads()) return;
    std::vector<uint16_t> pins = doodadPinsNow();
    const auto it = std::find(pins.begin(), pins.end(), uint16_t(value));
    const bool was = it != pins.end();
    if (was) pins.erase(it);
    else pins.push_back(uint16_t(value));
    std::string err;
    if (!setDoodadPinsNow(pins, err)) shellErrorBox(widen(err), shellTr(activeProject() ? L"Could not save the project" : L"Could not save the map's settings"));
    setMessage(shellTr(was ? L"Unpinned " : L"Pinned to the top: ") + paletteItemName(value));
    shellPaletteRebuilt();
    shellPaletteChanged();
}

// Where the rows go in a palette `width` px wide (content coordinates, before scrolling): a heading is a full row; an item
// is a row in a list, or a cell in a grid (as many columns as fit; the cells share the width, so a resize moves them at
// once, not a column at a time). Returns the content's height.
int paletteLayout(const std::vector<PaletteRow>& rows, const PaletteView& v, int width, std::vector<stardraft::draw::Rect>& out) {
    const int w = width, pad = scale(4), headerH = scale(26), rowH = v.xl ? scale(62) : v.large ? scale(46) : scale(30);
    const int cell = scale((v.sprites ? 56 : 44) * (v.xl ? 4 : v.large ? 3 : 2) / 2), cols = std::max(1, (w - 2 * pad) / cell);
    const int cw = std::max(cell, (w - 2 * pad) / cols);
    out.assign(rows.size(), {});
    int y = 0, col = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].value < 0) {
            if (col) y += cell, col = 0;
            out[i] = {0, y, w, y + headerH};
            y += headerH;
        } else if (v.grid) {
            out[i] = {pad + col * cw, y, pad + (col + 1) * cw, y + cell};
            if (++col == cols) col = 0, y += cell;
        } else {
            out[i] = {0, y, w, y + rowH};
            y += rowH;
        }
    }
    if (col) y += cell;
    return y + pad;
}
// The grid's footer (the hovered or picked item's name): its height, 0 for a list.
int paletteFooterHeight(const PaletteView& v) { return v.grid ? scale(26) : 0; }

// Shift+click in the palette (doodads, sprites, units): every item from the last pick to `row`, in the palette's order, is
// picked (for a spray, #79); Ctrl keeps the picks before. False: no last pick in the list (a plain pick then).
bool paletteShiftPick(AppState& app, const std::vector<PaletteRow>& rows, int row, bool ctrl) {
    if (row < 0 || row >= int(rows.size()) || app.palette < 0) return false;
    if (app.mode != Mode::Doodads && app.mode != Mode::Sprites && app.mode != Mode::Units) return false;
    int from = -1;
    for (size_t k = 0; k < rows.size(); ++k)
        if (rows[k].value == app.palette) from = int(k);
    if (from < 0) return false;
    std::vector<int>& chosen = shellPaletteChosen();
    if (!ctrl) chosen.clear();
    for (int k = std::min(from, row); k <= std::max(from, row); ++k)
        if (const int v = rows[size_t(k)].value; v >= 0 && std::find(chosen.begin(), chosen.end(), v) == chosen.end()) chosen.push_back(v);
    const wchar_t* what = app.mode == Mode::Units ? L" units" : app.mode == Mode::Sprites ? L" sprites" : L" doodads";
    setMessage(L"Selected " + std::to_wstring(chosen.size()) + what + L": each click or spray places one of them, at random");
    app.dirty = true;
    shellPaletteChanged();
    return true;
}

// --- The pie palette (roadmap 1.9) ---------------------------------------------------------------------------------
// Terrain mode: hold Space and a ring of the tileset's terrains comes up in the middle of the map; move the mouse toward
// one (its slice lights up) and let go of Space: the brush paints with it. Let go in the middle: no change. The ring's
// view is core/views_palette.inc (PieView); the Windows program draws the same ring in a layered window (main.cpp).
int pieRadius() { return scale(190); }
int pieHole() { return scale(46); }
int pieSize() { return 2 * pieRadius() + scale(8); }

// The slice in the direction (dx, dy) from the middle, of `n` slices (-1: inside the hole). The first slice is centered on
// the top; they go clockwise.
int pieSliceAt(double dx, double dy, int n) {
    if (n <= 0 || dx * dx + dy * dy < double(pieHole()) * pieHole()) return -1;
    double a = std::atan2(dx, -dy);  // 0 at the top, clockwise
    if (a < 0) a += 2 * 3.14159265358979323846;
    return int(a / (2 * 3.14159265358979323846 / n) + 0.5) % n;
}

// The terrains of the ring: the tileset's terrains that paint (empty: no ring, as when the map has no isometric terrain).
std::vector<uint16_t> pieTerrains(const AppState& app) {
    std::vector<uint16_t> items;
    if (!app.ed || !app.ed->isOpen() || app.mode != Mode::Terrain || !app.ed->canPaintTerrain()) return items;
    if (const IsomTerrainTable* t = IsomTerrainTable::forEra(app.ed->era()))
        for (uint16_t i = 1; i <= t->terrainCount(); ++i)
            if (t->isTerrain(i)) items.push_back(i);
    for (const Tileset::ModTerrain& m : app.ed->tileset().modTerrains) items.push_back(m.id);  // (a mod's own: Cosmonarchy tabs)
    return items;
}
