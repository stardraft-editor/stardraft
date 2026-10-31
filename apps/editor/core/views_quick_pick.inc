// The quick picker (Q, Quick open, the Data jump, Replace with...; portable, core.inc): a box in the middle of the map
// with a search field and the rows that match it. Type to filter the active mode's things (the palette's fuzzy search,
// best match first), Up and Down to choose, Enter to place it (a unit, terrain, doodad or sprite), or in Locations to
// select the location and show it; Escape or a click outside closes it.
// In the Data workspace the same box jumps to any entry of any .dat: type a name (and, if you like, the .dat's name:
// "weapon gauss"), each result shows its .dat; Enter opens it. Quick open lists the open tabs, then the recent projects
// and maps, then Browse.
// The view is a kit text field and painted rows in a Popup window (Host::openWindow); the host (main.cpp:
// showQuickPicker on Windows, sdl_map_dialogs.inc on SDL) places the window and draws the rows' pictures.

std::wstring datTitle(const std::string& name);
void dataGoTo(const std::string& dat, int e);  // (the Data workspace: main.cpp; the SDL program has no projects yet)
void replaceSelectionWith(int v);

// Quick open's rows (value): an open tab is its index; then these plus the index in the recent list; the last row browses.
constexpr int kRecentProjectRow = 1000, kRecentMapRow = 2000, kBrowseProjectRow = 3000;

// What the picker picks: the map mode's things (or, with replace, what the selection becomes), a .dat entry, a tab, or
// the rows that another view gives (Custom: gQuickCustom, as the trigger editors' Quick add and search).
enum class QuickPickKind { Map, Data, Open, Custom };

// Custom rows: the cue, the rows (made when the picker opens), their icon, and what a pick does with a row's value.
struct QuickPickCustom {
    std::wstring cue;
    std::function<std::vector<PaletteUnit>()> rows;
    UiIcon icon = UiIcon::Search;
    std::function<void(int)> run;
    // Each typed word is found as it is written (not its letters apart, as the names' fuzzy match): rows whose words
    // are long texts (the help's sections). The row that has it nearer the start of its words comes first.
    bool exact = false;
};
QuickPickCustom gQuickCustom;

// Replace with...: the selected units (each type of them), doodads or sprites become `v`, as one undo step.
void replaceSelectionWith(int v) {
    AppState& app = appState();
    Editor& ed = *app.ed;
    int n = 0;
    if (app.mode == Mode::Units) {
        std::set<uint16_t> types;
        for (int i : ed.selection())
            if (i >= 0 && i < int(ed.units().size())) types.insert(ed.units()[size_t(i)].type);
        ed.beginStroke();
        for (uint16_t t : types) n += ed.replaceUnits(t, uint16_t(v), true);
        ed.endStroke();
        setMessage(L"Replaced " + std::to_wstring(n) + L" units with " + paletteItemName(v));
    } else if (app.mode == Mode::Doodads) {
        const int asked = int(app.selDoodads.size());
        n = ed.replaceDoodads(app.selDoodads, uint16_t(v), app.doodadCheck);
        app.selDoodads.clear();
        setMessage(L"Replaced " + std::to_wstring(n) + L" of " + std::to_wstring(asked) + L" doodads (the others: the new one does not fit there)");
    } else if (app.mode == Mode::Sprites) {
        n = ed.replaceSprites(app.selSprites, uint16_t(v));
        setMessage(L"Replaced " + std::to_wstring(n) + L" sprites with " + paletteItemName(v));
    }
    app.dirty = true;
}

// Rectangle mode's rows for the quick picker (GitHub #141): each tile group that has a tile, found by the words of the
// Tiles list's search (its number, its terrain or its doodad); the row's value is the group's first tile.
std::vector<PaletteUnit> quickPickTileGroups() {
    std::vector<PaletteUnit> out;
    const Editor& ed = *appState().ed;
    for (const Editor::TileGroupEntry& e : ed.tileGroups()) {
        int first = -1;
        for (int s = 0; s < 16 && first < 0; ++s)
            if (ed.tileset().megatileFor(uint16_t(e.group << 4 | s)) > 0) first = s;
        if (first < 0) continue;
        PaletteUnit u;
        u.value = int(e.group) << 4 | first;
        u.label = L"Group " + std::to_wstring(e.group);
        u.section = tilesGroupNote(int(e.group));
        u.hay = L"group " + std::to_wstring(e.group) + L" " + u.section;
        std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
        out.push_back(std::move(u));
    }
    return out;
}

// The active mode's things for the quick picker: the palette's, the tile groups (Rectangle), or the locations.
std::vector<PaletteUnit> quickPickItems() {
    AppState& app = appState();
    if (app.mode == Mode::Units) return paletteUnits(app);
    if (tilesOn() && gTiles->stamps) return quickPickStamps();  // Custom: the custom terrain of the tileset (core/stamps.inc)
    if (tilesOn()) return quickPickTileGroups();
    if (app.mode != Mode::Locations) return paletteItems(app);
    std::vector<PaletteUnit> out;
    for (int i = 0; app.ed && i < app.ed->locationCount(); ++i) {
        if (!app.ed->locationUsed(i)) continue;
        PaletteUnit u;
        u.value = i, u.label = widen(app.ed->locationName(i)), u.section = L"Location " + std::to_wstring(i + 1);
        u.hay = u.label;
        std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
        out.push_back(std::move(u));
    }
    return out;
}

// Quick open's rows: the open tabs (not the start and New pages), then the recent projects and maps that no tab has.
// A row is found by its name only (the project's or the map file's), not by its folder, which shows under it.
std::vector<PaletteUnit> quickOpenItems() {
    std::vector<PaletteUnit> out;
    auto add = [&](int value, const std::wstring& label, const std::wstring& section) {
        PaletteUnit u;
        u.value = value, u.label = label, u.section = section, u.hay = label;
        std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
        out.push_back(std::move(u));
    };
    const auto same = samePath;
    std::vector<fs::path> openProjects, openMaps;
    for (int i = 0; i < int(gDocs.size()); ++i) {
        const Editor* ed = docEditor(i);
        if (!ed || gDocs[size_t(i)].page) continue;
        const auto& pr = gDocs[size_t(i)].project;
        if (pr) openProjects.push_back(pr->folder());
        else openMaps.push_back(ed->path());
        add(i, docTitle(i, false), std::wstring(tr(pr ? L"Open tab: project" : L"Open tab: map")) +
                                       (pr ? L"  \x00B7  " + pr->folder().wstring() : ed->path().empty() ? L"" : L"  \x00B7  " + ed->path().parent_path().wstring()));
    }
    for (size_t k = 0; k < gRecentProjects.size() && k < size_t(kRecentMax); ++k) {
        const fs::path f = gRecentProjects[k];
        if (std::any_of(openProjects.begin(), openProjects.end(), [&](const fs::path& o) { return same(o, f); })) continue;
        add(kRecentProjectRow + int(k), f.filename().wstring(), std::wstring(tr(L"Recent project")) + L"  \x00B7  " + f.parent_path().wstring());
    }
    for (size_t k = 0; k < gRecentMaps.size() && k < size_t(kRecentMax); ++k) {
        const fs::path f = gRecentMaps[k];
        if (std::any_of(openMaps.begin(), openMaps.end(), [&](const fs::path& o) { return same(o, f); })) continue;
        add(kRecentMapRow + int(k), f.filename().wstring(), std::wstring(tr(L"Recent map")) + L"  \x00B7  " + f.parent_path().wstring());
    }
    return out;
}

// Every entry of every .dat, for the Data workspace's jump box.
std::vector<PaletteUnit> dataJumpItems() {
    std::vector<PaletteUnit> out;
    const auto& all = DatSchema::all();
    for (size_t tab = 0; tab < all.size(); ++tab) {
        const std::wstring title = datTitle(all[tab].name);
        for (int e = 0; e < all[tab].entries; ++e) {
            PaletteUnit u;
            u.value = int(tab) * 10000 + e;
            u.label = entryName(all[tab].name, e);
            if (u.label.empty()) u.label = L"(unnamed)";
            u.label += L"  #" + std::to_wstring(e);
            u.section = title;
            u.hay = u.label + L" " + title;
            std::transform(u.hay.begin(), u.hay.end(), u.hay.begin(), ::towlower);
            out.push_back(std::move(u));
        }
    }
    return out;
}

// The words of the typed text (lower case): each word matches on its own, in any order ("weap gauss" and "gauss weap"
// both find the Gauss Rifle).
std::vector<std::wstring> quickPickWords(const std::wstring& typed) {
    std::wstring filter = typed;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);
    std::vector<std::wstring> words;
    std::wistringstream in(filter);
    for (std::wstring w; in >> w;) words.push_back(w);
    return words;
}

// The rows that match `words`, best match first (at most 400: enough to scroll; keeps the list quick). Quick open's
// last row, whatever is typed, is Browse (the Open project window).
std::vector<PaletteUnit> quickPickFilter(std::vector<PaletteUnit> items, const std::vector<std::wstring>& words, bool open, bool exact = false) {
    std::vector<std::pair<int, PaletteUnit>> scored;
    for (auto& u : items) {
        int sc = 0;
        for (const auto& w : words) {
            const size_t at = exact ? u.hay.find(w) : 0;
            const int ws = !exact ? fuzzyScore(u.hay, w) : at == std::wstring::npos ? -1 : std::max(1, 2000 - int(std::min<size_t>(at, 1999)));
            if (ws < 0) {
                sc = -1;
                break;
            }
            sc += ws;
        }
        if (sc >= 0) scored.push_back({sc, std::move(u)});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (scored.size() > 400) scored.erase(scored.begin() + 400, scored.end());
    if (open) {
        PaletteUnit u;
        u.value = kBrowseProjectRow, u.label = tr(L"Browse for a project\x2026"), u.section = tr(L"File > Project > Open project");
        scored.push_back({0, std::move(u)});
    }
    std::vector<PaletteUnit> out;
    out.reserve(scored.size());
    for (auto& [sc, u] : scored) out.push_back(std::move(u));
    return out;
}

// What a pick does (value `v` of the row; -1: none): open the tab or the recent file, go to the .dat entry, replace the
// selection, show the location, or place the thing of the active mode.
void quickPickRun(QuickPickKind kind, bool replace, int v) {
    AppState& app = appState();
    if (kind == QuickPickKind::Open) {
        if (v >= 0 && v < int(gDocs.size())) activateDoc(v);
        else if (v >= kRecentProjectRow && v < kRecentProjectRow + kRecentMax) command(ID_RECENT_PROJECT + (v - kRecentProjectRow));
        else if (v >= kRecentMapRow && v < kRecentMapRow + kRecentMax) command(ID_RECENT_MAP + (v - kRecentMapRow));
        else if (v == kBrowseProjectRow) command(ID_PROJ_OPEN);
        return;
    }
    if (kind == QuickPickKind::Data) {
        if (v >= 0) dataGoTo(DatSchema::all()[size_t(v / 10000)].name, v % 10000);
        return;
    }
    if (kind == QuickPickKind::Custom) {
        if (v >= 0 && gQuickCustom.run) gQuickCustom.run(v);
        return;
    }
    if (v < 0 || !app.ed) return;
    if (replace) return replaceSelectionWith(v);
    if (tilesOn() && gTiles->stamps) {  // Custom: it is the Brush's stamp, as a click in the Custom list makes it
        if (v < int(stampsList().size())) stampsUse(stampsList()[size_t(v)]);
        return;
    }
    if (tilesOn()) {  // Rectangle: the group's tile is picked, as a click in the Tiles list picks it, and the list shows it
        tilesPick(v, false);
        tilesReveal(v);
        return;
    }
    if (app.mode == Mode::Locations) {  // select it, and show it in the middle of the view
        app.ed->selectLocation(v);
        setListSel(int(std::find(app.items.begin(), app.items.end(), v) - app.items.begin()));
        const Rect r = app.ed->locationRect(v);
        int vw = 0, vh = 0;
        shellViewSize(vw, vh);
        app.camX = (r.left + r.right) / 2.0f - vw / 2.0f / app.zoom, app.camY = (r.top + r.bottom) / 2.0f - vh / 2.0f / app.zoom;
        setMessage(L"Location " + widen(app.ed->locationName(v)));
        app.dirty = true;
        return;
    }
    pickUnit(app, v, false);  // a unit, terrain, doodad or sprite of the active mode: place (paint) it
}

// The search field's cue for the kind (the mode's, or Replace with's).
std::wstring quickPickCue(QuickPickKind kind, bool replace, Mode mode) {
    static const wchar_t* cues[kModeCount] = {L"Paint a terrain", L"Place a unit", L"Place a doodad", L"Go to a location", L"Place a sprite"};
    static const wchar_t* replaceCues[kModeCount] = {L"", L"Replace with a unit", L"Replace with a doodad", L"", L"Replace with a sprite"};
    if (kind == QuickPickKind::Open) return tr(L"Go to an open tab, or open a recent project or map");
    if (kind == QuickPickKind::Data) return L"Go to a .dat entry";
    if (kind == QuickPickKind::Custom) return gQuickCustom.cue;
    if (!replace && mode == Mode::Terrain && tilesOn() && gTiles->stamps) return tr(L"Place custom terrain");
    if (!replace && mode == Mode::Terrain && tilesOn()) return tr(L"Place a tile");
    return replace ? replaceCues[int(mode)] : cues[int(mode)];
}

// The window's layout at 100 % (the old Win32 window's): 620 x 600 with a 1 px frame; the search row 14 px in (18 at the
// sides) and 32 high, its field 32 px after the row's left and 84 px before its right, the line under it 8 px below,
// the rows 6 px below the line, 6 px from the sides and the bottom, 48 high.
namespace quickPickLayout {
constexpr int kW = 620, kH = 600, kPad = 14, kRowH = 32, kItemH = 48, kIcon = 20, kPic = 36;
constexpr int kListSide = 6, kScrollW = 17;  // the rows' space for the bar: the native list's bar was 17 px wide
}  // namespace quickPickLayout

// The rows' pictures are the host's (the palette's bitmaps of each program).
class QuickPickHost {
public:
    virtual ~QuickPickHost() = default;
    // Draws the picture of row `u` into `box`; false when it has none (the view draws a dim icon in its place).
    virtual bool drawPicture(draw::Canvas& c, QuickPickKind kind, Mode mode, const PaletteUnit& u, draw::Rect box) = 0;
    // Before a paint draws the rows' pictures (none of this paint is drawn yet): the host may let go of pictures that
    // the last paints did not draw.
    virtual void paintStarts(QuickPickKind, Mode) {}
    // The picker closed (a pick, Escape, a click outside): the keys go back where they were. `v` is the picked row's
    // value (-1: none); the host runs quickPickRun after it.
    virtual void closed(int v) = 0;
};

class QuickPickView final : public draw::View {
public:
    QuickPickView() : bar_(stardraft::kit::Axis::Vertical) {
        field_.setPlain(true);
        field_.onChange = [this] { fill(); };
        field_.onEnter = [this] { choose(); };
        field_.onEscape = [this] { finish(-1); };
        bar_.onScroll = [this](int pos) {
            const int ih = std::max(1, itemH());
            top_ = (pos + ih / 2) / ih * cols();  // (whole rows, as the list box scrolled; a grid: whole lines)
            invalidate();
        };
        add(field_, {0, 0, 0, 0});
        add(bar_, {0, 0, 0, 0});
    }

    // Starts the picker for `kind` (the map's `mode`; replace: Replace with...). `framed`: the host's window has a 1 px
    // frame outside the view (Windows); else the view draws it. `focusText`: the field has the keyboard (not in a
    // background run).
    void start(QuickPickHost& host, QuickPickKind kind, Mode mode, bool replace, bool framed, bool focusText) {
        host_ = &host, kind_ = kind, mode_ = mode, replace_ = replace && kind == QuickPickKind::Map, framed_ = framed;
        focusText_ = focusText, focused_ = false, done_ = false;
        openRows_ = kind == QuickPickKind::Open ? quickOpenItems()
                    : kind == QuickPickKind::Custom && gQuickCustom.rows ? gQuickCustom.rows()
                                                                         : std::vector<PaletteUnit>{};
        field_.setText(L"");
        field_.setPlaceholder(quickPickCue(kind_, replace_, mode_));
        field_.setFont({draw::Face::Ui, quickFontPx()});
        top_ = 0;
        grid_ = gridSetting(), size_ = sizeSetting();
        fill();
    }
    QuickPickKind kind() const { return kind_; }
    bool replacing() const { return replace_; }
    // The rows shown, best first, and the one chosen (-1: none).
    const std::vector<PaletteUnit>& shown() const { return shown_; }
    int selected() const { return sel_; }
    int top() const { return top_; }  // the first row shown
    // The search field (the self-tests type into it).
    stardraft::kit::TextField& field() { return field_; }
    // Types `text` in place of the field's text (the rows follow), as a user's typing does.
    void setTyped(const std::wstring& text) {
        field_.setText(text);
        fill();
    }
    // A key as the user presses it in the field: Up, Down, Page Up, Page Down, Enter, Escape.
    void press(draw::Key k) {
        if (k == draw::Key::Enter) return choose();
        if (k == draw::Key::Escape) return finish(-1);
        key(draw::KeyEvent{k, true, false, 0});
    }
    // How the map's things show (the settings QuickPickGrid and QuickPickSize): a list or a grid of pictures, small,
    // medium (the default) or large. The Data jump, Quick open and the other lists are lists.
    static bool gridSetting() {
        int v = 0;
        return prefs::number(L"QuickPickGrid", v, 0, 1) && v == 1;
    }
    static int sizeSetting() {
        int v = 1;
        return prefs::number(L"QuickPickSize", v, 0, 2) ? v : 1;
    }
    void setView(bool grid, int size) {
        grid_ = grid, size_ = std::clamp(size, 0, 2);
        if (!gSettingsOff) prefs::setNumber(L"QuickPickGrid", grid_ ? 1 : 0), prefs::setNumber(L"QuickPickSize", size_);
        top_ = 0;
        if (sel_ >= 0) select(sel_);
        layoutField(), place(), invalidate();
    }
    // The window's size at the display scale (the view is this less the frame on each side when the host draws it).
    static draw::Point windowSize() { return {scale(quickPickLayout::kW), scale(quickPickLayout::kH)}; }

    // Up and Down move the choice by one, Page Up and Page Down by 8 (the field does not take them).
    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        if (pictures() && e.mods == draw::kCtrl) {  // (the view: Ctrl+L list, Ctrl+G grid; the size: Ctrl+wheel or the S / M / L button)
            if (e.key == static_cast<draw::Key>('L')) return setView(false, size_), true;
            if (e.key == static_cast<draw::Key>('G')) return setView(true, size_), true;
        }
        const int n = int(shown_.size());
        int step = 0;
        switch (e.key) {
        case draw::Key::Down: step = cols(); break;
        case draw::Key::Up: step = -cols(); break;
        case draw::Key::PageDown: step = std::max(cols(), visibleRows() - cols()); break;
        case draw::Key::PageUp: step = -std::max(cols(), visibleRows() - cols()); break;
        case draw::Key::Enter: return choose(), true;
        case draw::Key::Escape: return finish(-1), true;
        default: return false;
        }
        if (n) select(std::clamp(sel_ + step, 0, n - 1));
        return true;
    }

    bool mouse(const draw::MouseEvent& e) override {
        const draw::Rect L = listRect();
        if (e.kind == draw::MouseKind::Wheel) {  // three rows a notch (the list box's; a grid: a line of pictures)
            if (e.wheel == 0 || e.horizontal) return false;
            if (pictures() && (e.mods & draw::kCtrl)) return setView(grid_, size_ + (e.wheel > 0 ? 1 : -1)), true;  // (Ctrl+wheel: the size)
            const int lines = e.exact ? draw::exactSteps(e, float(itemH()), wheelCarry_) : int(std::lround(e.wheel * (grid_ && pictures() ? 1 : 3)));
            setTop(top_ - lines * cols());
            return true;
        }
        if (pictures() && (e.kind == draw::MouseKind::Down || e.kind == draw::MouseKind::DoubleClick) && e.button == 0)
            for (int b = 0; b < 3; ++b)
                if (viewButton(b).contains(e.pt)) {
                    if (b == 2) setView(grid_, (size_ + 1) % 3);
                    else setView(b == 1, size_);
                    return true;
                }
        if (pictures() && e.kind == draw::MouseKind::Move) {
            int over = -1;
            for (int b = 0; b < 3; ++b)
                if (viewButton(b).contains(e.pt)) over = b;
            if (over != overButton_) overButton_ = over, invalidate();
        }
        if (e.kind != draw::MouseKind::Move && e.kind != draw::MouseKind::Down && e.kind != draw::MouseKind::DoubleClick) return false;
        const int i = rowAt(e.pt);
        if (i < 0 || !L.contains(e.pt) || (scrolls() && e.pt.x >= L.right - scale(quickPickLayout::kScrollW))) return false;
        if (i != sel_) select(i);  // the row under the mouse is the choice (a row cut at the bottom scrolls in, as the list box's)
        if (e.kind != draw::MouseKind::Move && e.button == 0) choose();  // one click picks it
        return true;
    }

    std::wstring feedbackName(draw::Point) override { return L"the quick picker"; }

    void resized() override {
        using namespace quickPickLayout;
        // The old window's client area is inside its 1 px frame; its layout counted from the window's outer width.
        const int o = inset(), w = outerW();
        sr_ = {o + scale(kPad) + scale(4), o + scale(kPad), o + w - scale(kPad) - scale(4), o + scale(kPad) + scale(kRowH)};
        layoutField();
        place();
    }

    void paint(draw::Canvas& c) override {
        using namespace quickPickLayout;
        if (focusText_ && !focused_ && host()) focused_ = true, field_.focus();
        if (host_) host_->paintStarts(kind_, mode_);
        c.fill({0, 0, width(), height()}, kBg);
        if (!framed_) c.frame({0, 0, width(), height()}, kBorderHot);
        // The search row: the icon, the count of results at the right, the line under it.
        const int is = scale(kIcon);
        c.icon({sr_.left, (sr_.top + sr_.bottom - is) / 2, sr_.left + is, (sr_.top + sr_.bottom + is) / 2}, int(UiIcon::Search), kDim, is);
        const draw::Font of = c.setFont(smallFont(c));
        const std::wstring count = std::to_wstring(shown_.size()) + (shown_.size() >= 400 ? L"+" : L"");
        c.text({sr_.right - scale(80), sr_.top, sr_.right, sr_.bottom}, count, kDim, draw::kRight | draw::kMiddle);
        if (pictures()) {  // (the view: List, Grid, the size)
            static const wchar_t* const kSizes[3] = {L"S", L"M", L"L"};
            for (int b = 0; b < 3; ++b) {
                const draw::Rect r = viewButton(b);
                const bool on = b == 2 ? false : (b == 1) == grid_;
                if (on) c.roundBox(r, kSel, kAccentLine, float(scale(4)));
                else if (b == overButton_) c.roundBox(r, kCtlHot, kCtlHot, float(scale(4)));
                const draw::Color fg = on ? kAccentBright : b == overButton_ ? kText : kDim;
                if (b == 2) c.text(r, kSizes[size_], fg, draw::kCenter | draw::kMiddle);
                else c.icon({r.left + scale(4), r.top + scale(4), r.right - scale(4), r.bottom - scale(4)}, int(b == 1 ? UiIcon::Grid : UiIcon::AlignLeft), fg, scale(16));
            }
        }
        c.setFont(of);
        c.fill({inset(), sr_.bottom + scale(8), width() - inset(), sr_.bottom + scale(9)}, kBorder);
        // The rows.
        const draw::Rect L = listRect();
        c.pushClip(L);
        const int ih = itemH(), nc = cols(), cw = cellW();
        const int rowsRight = L.right - (scrolls() ? scale(kScrollW) : 0);
        for (int i = top_; i < int(shown_.size()); ++i) {
            const int y = L.top + (i - top_) / nc * ih;
            if (y >= L.bottom) break;
            if (nc > 1 || (grid_ && pictures())) {
                const int x = L.left + (i - top_) % nc * cw;
                paintCell(c, shown_[size_t(i)], i == sel_, {x, y, x + cw, y + ih});
            } else {
                paintRow(c, shown_[size_t(i)], i == sel_, {L.left, y, rowsRight, y + ih});
            }
        }
        c.popClip();
    }

private:
    static draw::Font smallFont(draw::Canvas& c) { return c.font({draw::Face::Ui, -(shellUiFontLfHeight() * 5 / 6)}); }
    // The rows' and the field's font: the UI font, a quarter larger.
    static int quickFontPx() { return -(shellUiFontLfHeight() * 5 / 4); }

    // Where the old client area starts in the view (the view draws the frame itself: 1) and the window's outer size.
    int inset() const { return framed_ ? 0 : 1; }
    int outerW() const { return framed_ ? width() + 2 : width(); }
    int outerH() const { return framed_ ? height() + 2 : height(); }
    // The rows' list: 6 px in from the sides and the bottom (of the outer size, as the old list box), under the line.
    draw::Rect listRect() const {
        using namespace quickPickLayout;
        const int o = inset();
        const int top = sr_.bottom + scale(9) + scale(6);  // (in the view; sr_ has the inset)
        const int left = o + scale(kListSide);
        return {left, top, left + outerW() - 2 * scale(kListSide), top + outerH() - (top - o) - scale(kListSide)};
    }
    // The map's things have pictures: they take the view (list or grid, the size); the other lists are lists.
    bool pictures() const { return kind_ == QuickPickKind::Map; }
    // A row's height (a list) or a cell's (a grid: its picture and its name under it); a cell's width; the columns.
    int itemH() const {
        static const int kListH[3] = {36, 48, 64}, kCell[3] = {64, 88, 120};
        if (!pictures()) return scale(quickPickLayout::kItemH);
        return grid_ ? scale(kCell[size_] + 18) : scale(kListH[size_]);
    }
    int cellW() const {
        static const int kCell[3] = {64, 88, 120};
        return pictures() && grid_ ? scale(kCell[size_]) : listRect().width();
    }
    int cols() const { return pictures() && grid_ ? std::max(1, (listRect().width() - scale(quickPickLayout::kScrollW)) / std::max(1, cellW())) : 1; }
    int lines(int n) const { return (n + cols() - 1) / cols(); }
    int visibleLines() const { return std::max(1, listRect().height() / std::max(1, itemH())); }
    int visibleRows() const { return visibleLines() * cols(); }  // (the things that show whole: a grid's lines of them)
    bool scrolls() const { return lines(int(shown_.size())) * itemH() > listRect().height(); }
    int rowAt(draw::Point pt) const {
        const draw::Rect L = listRect();
        if (pt.y < L.top || pt.x < L.left) return -1;
        const int col = (pt.x - L.left) / std::max(1, cellW());
        if (col >= cols()) return -1;
        const int i = top_ + (pt.y - L.top) / std::max(1, itemH()) * cols() + col;
        return i < int(shown_.size()) ? i : -1;
    }
    // The view's buttons in the search row, left of the count: List, Grid, the size.
    draw::Rect viewButton(int b) const {
        const int bw = scale(24), right = sr_.right - scale(56) - (2 - b) * (bw + scale(2));  // (the count's room at the right: 56)
        return {right - bw, sr_.top + scale(4), right, sr_.bottom - scale(4)};
    }
    // A grid's cell: the picture, as large as the cell takes, the name under it (cut to the cell).
    void paintCell(draw::Canvas& c, const PaletteUnit& u, bool sel, draw::Rect r) {
        c.fill(r, kBg);
        if (sel) c.roundBox({r.left + scale(2), r.top + scale(2), r.right - scale(2), r.bottom - scale(2)}, kSel, kSel, float(scale(6)));
        const int pic = r.width() - scale(12);
        const draw::Rect box{r.left + scale(6), r.top + scale(4), r.left + scale(6) + pic, r.top + scale(4) + pic};
        if (!(mode_ != Mode::Locations && host_->drawPicture(c, kind_, mode_, u, box)))
            c.icon(box, int(mode_ == Mode::Locations ? UiIcon::Locations : UiIcon::Units), kFaint, scale(20));
        const draw::Font of = c.setFont(smallFont(c));
        c.text({r.left + scale(3), box.bottom + scale(2), r.right - scale(3), r.bottom - scale(2)}, u.label, u.mark & kQuickMarkGray ? kFaint : kText,
               draw::kCenter | draw::kMiddle | draw::kEllipsis);
        c.setFont(of);
    }

    void layoutField() {
        using namespace quickPickLayout;
        // The old edit box: the font's line and 4 px high, in the middle of the row, its text at its top and 4 px in. The
        // kit field puts its line in the middle and 2 px in: the same text place is 2 px right and 2 px up of the box.
        const int lineH = lineHeight_ > 0 ? lineHeight_ : scale(20);
        const int editH = lineH + scale(4), editX = sr_.left + scale(32);
        const int y = sr_.top + (scale(kRowH) - editH) / 2;
        field_.setFrame({editX + scale(2), y - scale(2), sr_.right - (pictures() ? scale(56 + 3 * 26 + 8) : scale(84)), y - scale(2) + editH});
    }
    void place() {
        const draw::Rect L = listRect();
        const int ih = itemH();
        // The kit bar where the old list's bar had its track: in the 17 px column at the right, between the 17 px of its
        // arrows; its thin thumb on the column's middle. The page is the whole rows that show (the list box's), so the
        // thumb has the old one's length and place.
        const int col = L.right - scale(quickPickLayout::kScrollW), arrow = scale(quickPickLayout::kScrollW);
        bar_.setFrame({col + scale(1), L.top + arrow, col + scale(1) + scale(stardraft::kit::kScrollBarW), L.bottom - arrow});
        bar_.setRange(lines(int(shown_.size())) * ih, visibleLines() * ih);
        bar_.setPos(top_ / cols() * ih);
        bar_.setVisible(scrolls());
    }

    void fill() {
        words_ = quickPickWords(field_.text());
        std::vector<PaletteUnit> items = kind_ == QuickPickKind::Data                                    ? dataJumpItems()
                                         : kind_ == QuickPickKind::Open || kind_ == QuickPickKind::Custom ? openRows_
                                                                                                          : quickPickItems();
        shown_ = quickPickFilter(std::move(items), words_, kind_ == QuickPickKind::Open, kind_ == QuickPickKind::Custom && gQuickCustom.exact);
        sel_ = shown_.empty() ? -1 : 0;
        top_ = 0;
        place();
        invalidate();
    }
    void setTop(int t) {
        const int nc = cols(), maxTop = std::max(0, lines(int(shown_.size())) - visibleLines()) * nc;
        t = std::clamp(t / nc * nc, 0, maxTop);
        if (t != top_) top_ = t, place(), invalidate();
    }
    // The choice, scrolled into view (the list box's LB_SETCURSEL; a grid: its line).
    void select(int i) {
        sel_ = i;
        const int nc = cols();
        if (i < top_) setTop(i);
        else if (i >= top_ + visibleRows()) setTop((i / nc - visibleLines() + 1) * nc);
        invalidate();
    }
    void choose() { finish(sel_ >= 0 && sel_ < int(shown_.size()) ? shown_[size_t(sel_)].value : -1); }
    void finish(int v) {
        if (done_ || !host_) return;
        done_ = true;
        host_->closed(v);
    }

    // A row: the picture, the name (the typed words in the accent) and under it its group, small and dim; the chosen
    // row is a rounded pill in the selection color.
    void paintRow(draw::Canvas& c, const PaletteUnit& u, bool sel, draw::Rect r) {
        using namespace quickPickLayout;
        c.fill(r, kBg);
        if (sel) c.roundBox({r.left + scale(2), r.top + scale(2), r.right - scale(2), r.bottom - scale(2)}, kSel, kSel, float(scale(scale(8))));  // (ui::box scales its radius)
        const int pic = scale(kPic);
        const draw::Rect box{r.left + scale(10), (r.top + r.bottom - pic) / 2, r.left + scale(10) + pic, (r.top + r.bottom - pic) / 2 + pic};
        if (kind_ == QuickPickKind::Open) {  // a tab, a recent project or map, or Browse
            const bool project = u.value < kRecentProjectRow ? u.value < int(gDocs.size()) && gDocs[size_t(u.value)].project != nullptr : u.value < kRecentMapRow;
            c.icon(box, int(u.value == kBrowseProjectRow ? UiIcon::Open : project ? UiIcon::Project : UiIcon::Map), u.value < kRecentProjectRow ? kText : kFaint,
                   scale(20));
        } else if (kind_ == QuickPickKind::Custom && (u.mark & kQuickMarkOption)) {  // an option: its check box, on or off
            const int s = scale(16);
            const draw::Rect b{(box.left + box.right - s) / 2, (box.top + box.bottom - s) / 2, (box.left + box.right + s) / 2, (box.top + box.bottom + s) / 2};
            kitpaint::drawCheckMark(c, b, (u.mark & kQuickMarkOn) != 0, false, !(u.mark & kQuickMarkGray));
        } else if (kind_ == QuickPickKind::Custom) {
            c.icon(box, int(gQuickCustom.icon), u.mark & kQuickMarkGray ? kFaint : kText, scale(20));
        } else if (!((kind_ == QuickPickKind::Data || mode_ != Mode::Locations) && host_->drawPicture(c, kind_, mode_, u, box))) {
            c.icon(box, int(mode_ == Mode::Locations && kind_ != QuickPickKind::Data ? UiIcon::Locations : UiIcon::Units), kFaint, scale(20));
        }
        const int x = box.right + scale(12), right = r.right - scale(12), mid = (r.top + r.bottom) / 2;
        c.pushClip({x, r.top, right, r.bottom});
        // The name: the parts that the typed words found in the accent color.
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, quickFontPx()}));
        const int nameH = c.metrics().height;
        if (nameH != lineHeight_) lineHeight_ = nameH, layoutField();  // (the field's height follows the font)
        textRuns(c, u.label, x, mid - nameH + scale(2), u.mark & kQuickMarkGray ? kFaint : kText);  // (gray: it cannot run now)
        // The group, under the name (the typed words found in it in the accent too: "zerg spec").
        c.setFont(smallFont(c));
        textRuns(c, u.section, x, mid + scale(2), kDim);
        c.setFont(of);
        c.popClip();
    }
    // `s` at (x, y), the parts that the typed words found in the accent, the rest in `rest`.
    void textRuns(draw::Canvas& c, const std::wstring& s, int x, int y, draw::Color rest) {
        std::wstring low = s;
        std::transform(low.begin(), low.end(), low.begin(), ::towlower);
        std::vector<uint8_t> hit(s.size(), 0);
        for (const auto& w : words_)
            if (const size_t at = w.empty() ? std::wstring::npos : low.find(w); at != std::wstring::npos)
                std::fill(hit.begin() + long(at), hit.begin() + long(at + w.size()), uint8_t(1));
        const int h = c.metrics().height;
        for (size_t i = 0; i < s.size();) {
            size_t j = i;
            while (j < s.size() && hit[j] == hit[i]) ++j;
            const std::wstring_view run = std::wstring_view(s).substr(i, j - i);
            // Each letter where the run's measure puts it (no kerning between letters, as the old rows' ExtTextOut): a
            // pair of surrogates stays together. A letter is centered in a box wider by `room` on each side, so the parts
            // of it that reach past its advance (a backslash, an italic) are not cut off.
            const int room = scale(8);
            for (size_t k = 0; k < run.size();) {
                const size_t n = k + 1 < run.size() && run[k] >= 0xD800 && run[k] <= 0xDBFF ? 2 : 1;
                const int at = x + (k ? c.measure(run.substr(0, k)) : 0), w = c.measure(run.substr(k, n));
                c.text({at - room, y, at + w + room, y + h}, run.substr(k, n), hit[i] ? kAccent : rest, draw::kCenter);
                k += n;
            }
            x += c.measure(run), i = j;
        }
    }

    QuickPickHost* host_ = nullptr;
    QuickPickKind kind_ = QuickPickKind::Map;
    Mode mode_ = Mode::Units;
    bool replace_ = false, framed_ = true, focusText_ = true, focused_ = false, done_ = false;
    bool grid_ = false;  // (the view of the map's things: a grid of pictures, else a list)
    int size_ = 1;       // (their size: 0 small, 1 medium, 2 large)
    int overButton_ = -1;  // (the view button under the mouse)
    std::vector<PaletteUnit> openRows_;  // Quick open's rows, made once when it opens
    std::vector<PaletteUnit> shown_;
    std::vector<std::wstring> words_;  // what is typed, word by word (the rows show where each word is in the name)
    int sel_ = -1, top_ = 0;
    float wheelCarry_ = 0;  // an exact scroll's part of a row (draw::exactSteps)
    int lineHeight_ = 0;  // the rows' font's line height, from the last paint
    draw::Rect sr_;       // the search row: the icon, the field, the count
    stardraft::kit::TextField field_;
    stardraft::kit::ScrollBar bar_;
};
