// The tool strip (portable, core.inc): the active mode's tools as icon buttons, the Terrain brush field (shape, width,
// height, link) or the Size and Density number boxes, the Terrain Isometric | Rectangle choice, and the mirrors. Moved
// from main.cpp as it was: the same boxes, the same tips and keys. Its window is a kit view (ToolStripView) that the
// host shows (the Win32 window of kViewClass in main.cpp, the SDL window's slot in sdl_main_view.inc). The number
// edit is a kit text field (apps/kit/text_field.h): Enter, Escape and leaving the field end it as before.

// (The kit's headers come before core.inc, outside its namespace: main.cpp and core_check.cpp include text_field.h.)
constexpr int kStripRadius = 4;         // every box (the kit's kRadius)
constexpr double kStripPi = 3.14159265358979323846;

void setTerrainRect(bool rect);
bool objectSprayOn();
void shellMenuAt(draw::View& from, draw::Point at, const MenuModel& m, const std::wstring& on, std::function<void(int)> pick);  // (sdl_panels.inc)

// The brush sizes (diamonds; the sprays' radius too): 0.5 (half a diamond: one corner), 1 to 16, then every other to 32.
const std::vector<float>& brushSizes() {
    static const std::vector<float> v = [] {
        std::vector<float> o{0.5f};
        for (int i = 1; i <= 16; ++i) o.push_back(float(i));
        for (int i = 18; i <= 32; i += 2) o.push_back(float(i));
        return o;
    }();
    static const std::vector<float> tiles(v.begin() + 1, v.end());  // (the Rectangle tools: a tile is the least, no 0.5)
    if (appState().mode == Mode::Terrain && appState().terrainRect) return tiles;
    return v;
}
int brushSizeIndex(float size) {  // the nearest size in the list
    const auto& v = brushSizes();
    int best = 0;
    for (int i = 0; i < int(v.size()); ++i)
        if (std::abs(v[size_t(i)] - size) < std::abs(v[size_t(best)] - size)) best = i;
    return best;
}

// The tool strip's number boxes (Size, Density, the Terrain height and the brush shape, for the wheel).
enum StripNumber { kNumSize, kNumDensity, kNumHeight, kNumShape, kNumTurns, kNumMix, kNumDetail };

// A click area of the strip, as painted: what it does, its tip, its cursor.
struct StripHit {
    draw::Rect r;
    std::function<void()> click;
    std::wstring tip;
    draw::Cursor cursor = draw::Cursor::Hand;
};

// The number box's value field: Up and Down step the number (as they did in the native edit box).
class StripNumberField final : public stardraft::kit::TextField {
public:
    std::function<void(int)> onStep;  // +1 or -1
    // Space pressed the value part (Tab's focus, item_focus.inc) and the field took the keyboard: the space that the
    // same key types next is not typed into the number.
    bool dropSpace = false;
    using TextField::text;  // (the field's text())
    bool text(const draw::TextEvent& e) override {
        if (std::exchange(dropSpace, false) && e.utf8 == " ") return true;
        return TextField::text(e);
    }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key != draw::Key::Space) dropSpace = false;
        if (e.down && (e.key == draw::Key::Up || e.key == draw::Key::Down) && onStep) {
            onStep(e.key == draw::Key::Up ? 1 : -1);
            return true;
        }
        return TextField::key(e);
    }
};

// The shape list's entries: the names as the menu shows them.
static const wchar_t* const kStripShapeNames[3] = {L"&Diamond", L"&Square", L"&Circle"};

// --- Rules of the strip (no drawing) ------------------------------------------------------------------------------------
bool objectSprayOn() {
    const AppState& app = appState();
    return (app.mode == Mode::Units && currentTool() == USpray) || (app.mode == Mode::Doodads && currentTool() == DSpray) ||
           (app.mode == Mode::Sprites && currentTool() == SSpray);
}
// The brush size: Terrain, and the sprays and Erase of Units, Doodads, Sprites and Locations (their radius).
bool stripHasSize() { return appState().mode == Mode::Terrain || objectSprayOn() || objectEraseOn(); }
// Density: the sprays (Terrain, Units, Doodads, Sprites) and the Fill of Units and Doodads (how many).
bool stripHasDensity() {
    const AppState& app = appState();
    return (app.mode == Mode::Terrain && currentTool() == TSpray) || objectSprayOn() || (app.mode == Mode::Units && currentTool() == UFill) ||
           (app.mode == Mode::Doodads && currentTool() == DFill);
}
// Mix: Terrain's Fill (several picks in patches).
bool stripHasMix() { return appState().mode == Mode::Terrain && currentTool() == TFill && terrainKind() != 2; }  // (not Custom)
// More: the terrain tools' less used values, under a fold at the strip's end (Isometric and Rectangle: Detailed tiles,
// GitHub #257). Its fold, open or closed, is kept in the settings (ToolStripMore).
bool stripHasMore() { return appState().mode == Mode::Terrain && (terrainKind() == 0 || terrainKind() == 3); }
bool& stripMoreOpen() {
    static bool open = [] {
        bool v = false;
        if (!gSelfTest && !gSettingsOff) prefs::flag(L"ToolStripMore", v);
        return v;
    }();
    return open;
}
// The mirrors: the modes whose edits repeat with them (Rectangle too: its tiles at the mirror images; sprites as units do).
bool stripHasMirrors() {
    const AppState& app = appState();
    return app.mode == Mode::Terrain || app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites;
}

// The Terrain tool strip's Isometric | Tile: what the terrain tools work on (ISOM terrains, or tiles as they are; Tile
// was "Rectangle", and the code's terrainRect is it). Each map's own: its file keeps it (a new map starts in Isometric).
// Custom: Tile's tools on the map, and the panel shows the user's custom terrain (core/stamps.inc) in place of the
// Tiles grid (TilesView::stamps). Rectangle (the code's Solid): Tile's tools put the tiles inside the terrain selected
// in the terrain list, which the panel shows (TilesView::solid, docs/dev/solid-terrain-brush.md).
// 0 Isometric, 1 Tile, 2 Custom, 3 Rectangle; the strip shows them in kTerrainKindOrder (Ctrl+Alt+T goes in that order).
// (kTerrainKinds and kTerrainKindOrder: modes.inc)
int nextTerrainKind(int kind) {
    for (int i = 0; i < kTerrainKinds; ++i)
        if (kTerrainKindOrder[i] == kind) return kTerrainKindOrder[(i + 1) % kTerrainKinds];
    return 0;
}
int terrainKind() { return !appState().terrainRect ? 0 : gTiles->stamps ? 2 : gTiles->solid ? 3 : 1; }
void stampsPanelSync();  // (views_stamps.inc: the active panel shows its list or its grid)
void setTerrainKind(int kind) {
    AppState& app = appState();
    const int was = terrainKind();
    if (was == kind) return;
    const bool rect = kind != 0;
    if ((was == 2) != (kind == 2)) {  // to Custom or from it: the tile or the custom terrain picked before goes
        if (kind == 2 || !gTiles->stampName.empty()) {
            gTiles->stamp.clear(), gTiles->stampW = gTiles->stampH = 0, gTiles->stampName.clear();
            gTiles->chosen.clear(), gTiles->picked = gTiles->anchor = -1;
        }
        gTiles->stamps = kind == 2;
    }
    gTiles->solid = kind == 3;
    if (app.terrainRect != rect) {
        app.terrainRect = rect;
        app.hasTerrainSel = false, app.terrainSelMask.clear();  // (each has its own Select shape: the selection would change)
        std::swap(app.brushSize, app.otherBrushSize), std::swap(app.brushHeight, app.otherBrushHeight);  // (each its own size)
        std::swap(app.brushShape, app.otherBrushShape);  // (and shape: a Diamond for Isometric, a Square for Rectangle, GitHub #236)
        if (rect && app.brushSize < 1) app.brushSize = 1;  // (a tile is the least: no half)
    }
    if (rect && app.toolByMode[int(Mode::Terrain)] == TJumble) app.toolByMode[int(Mode::Terrain)] = TBrush;  // (Isometric's only)
    app.terrainPasting = false;
    stampsPanelSync();
    if (shellViewShown()) shellRelayout();  // the strip's height (no mirrors in Rectangle)
    fillToolCombo(app);  // tilesSync: the Tiles grid or the terrain palette
    if (kind == 3 || was == 3) rebuildList(app);  // (Solid: the terrain list with its pick)
    updateStatus();
    setMessage(kind == 3 ? L"Rectangle: the tools place the tiles inside a terrain. Select a terrain in the list."
               : kind == 2 ? L"Custom: the tools place your custom terrain. Select custom terrain in the list."
               : rect    ? L"Tile: the tools place tiles as they are. Select a tile in the Tiles list."
                         : L"Isometric: the tools paint terrains and correct the edges between them");
    shellToolsChanged();
    app.dirty = true;
}
// Isometric or Rectangle (from Custom, Rectangle's tools stay: Custom).
void setTerrainRect(bool rect) { setTerrainKind(!rect ? 0 : appState().terrainRect ? terrainKind() : 1); }

std::wstring stripNumberText(int which) {
    const AppState& app = appState();
    wchar_t t[24];
    if (which == kNumSize) std::swprintf(t, 24, L"%g", app.brushSize);
    else if (which == kNumHeight) std::swprintf(t, 24, L"%g", brushHeightOf(app));  // linked: the size; the Diamond: its own rule
    else if (which == kNumTurns) std::swprintf(t, 24, L"%d", app.ed ? app.ed->turns() : 1);
    else if (which == kNumMix) std::swprintf(t, 24, L"%d", app.fillMix);
    else if (which == kNumDetail) std::swprintf(t, 24, L"%d", stardraft::detailedTilePercent());
    else std::swprintf(t, 24, L"%d", app.sprayDensity);
    return t;
}
void stripNumberChanged() {
    shellToolsChanged();
    appState().dirty = true;
}
// One step up (dir 1) or down (-1).
void stripNumberStep(int which, int dir) {
    AppState& app = appState();
    if (which == kNumShape) {  // diamond, square, circle, round and round
        Brush::Shape& shape = shapeOf(app);  // (Select's own, or the brushes')
        shape = Brush::Shape((int(shape) + (dir > 0 ? 1 : 2)) % 3);
        shellSettingsChanged();
    } else if (which == kNumSize) {
        const auto& v = brushSizes();
        float next = app.brushSize;
        if (dir > 0) {
            for (float sz : v)
                if (sz > app.brushSize + 0.01f) {
                    next = sz;
                    break;
                }
        } else {
            for (auto it = v.rbegin(); it != v.rend(); ++it)
                if (*it < app.brushSize - 0.01f) {
                    next = *it;
                    break;
                }
        }
        app.brushSize = next;
    } else if (which == kNumHeight) {  // linked: one step from the height it shows, and no longer linked
        app.brushHeight = std::clamp(std::round(brushHeightOf(app)) + float(dir), 1.0f, 32.0f);
    } else if (which == kNumTurns) {
        if (app.ed) app.ed->setTurns(app.ed->turns() + dir);
    } else if (which == kNumMix) {
        app.fillMix = std::clamp(app.fillMix + dir, 1, 10);
    } else if (which == kNumDetail) {  // (as Density: 0, 1, 5, 10, ... 100)
        const int d = stardraft::detailedTilePercent();
        stardraft::setDetailedTilePercent(std::clamp(dir > 0 ? (d == 0 ? 1 : (d / 5 + 1) * 5) : d <= 1 ? 0 : d <= 5 ? 1 : (d - 1) / 5 * 5, 0, 100));
        shellSettingsChanged();
    } else {
        // (in fives: 1, 5, 10, ... 100; typed: any number)
        const int d = app.sprayDensity;
        app.sprayDensity = std::clamp(dir > 0 ? (d / 5 + 1) * 5 : d <= 5 ? 1 : (d - 1) / 5 * 5, 1, kDensityMax);
    }
    stripNumberChanged();
}
// A typed number: into the range; Size to a whole number, or 0.5 below 1 (half a diamond: one corner).
bool stripNumberSet(int which, const std::wstring& text) {
    AppState& app = appState();
    if (which == kNumHeight && text.empty()) {  // linked: the same as the size
        app.brushHeight = 0;
        stripNumberChanged();
        return true;
    }
    wchar_t* end = nullptr;
    const double v = std::wcstod(text.c_str(), &end);
    while (end && *end == L' ') ++end;
    if ((which == kNumDensity || which == kNumDetail) && end && *end == L'%') ++end;  // (as it shows)
    if (text.empty() || !end || end == text.c_str() || *end || !std::isfinite(v)) {
        setMessage(L"Not a number: " + text);
        return false;
    }
    if (which == kNumSize) {
        app.brushSize = v < 1 && !(app.mode == Mode::Terrain && app.terrainRect) ? 0.5f : float(std::clamp(std::round(v), 1.0, 32.0));
    } else if (which == kNumHeight) {
        app.brushHeight = float(std::clamp(std::round(v), 1.0, 32.0));  // a value of its own (the link turns off)
    } else if (which == kNumTurns) {
        if (app.ed) app.ed->setTurns(int(std::lround(std::clamp(v, -1000.0, 1000.0))));
    } else if (which == kNumMix) {
        app.fillMix = std::clamp(int(std::lround(std::clamp(v, -1000.0, 1000.0))), 1, 10);
    } else if (which == kNumDetail) {
        stardraft::setDetailedTilePercent(int(std::lround(std::clamp(v, 0.0, 100.0))));
        shellSettingsChanged();
    } else {
        app.sprayDensity = std::clamp(int(std::lround(std::clamp(v, -1000.0, 1000.0))), 1, kDensityMax);
    }
    stripNumberChanged();
    return true;
}

// A tool of the strip: as its button does.
void pickStripTool(const StripItem& it) {
    AppState& app = appState();
    if (it.select) enterSelection(), setMessage(std::wstring(kModeNames[int(app.mode)]) + L": select");
    else {
        app.toolByMode[int(app.mode)] = it.tool;
        if (app.mode == Mode::Terrain)  // (its group's button shows it from now on)
            for (int g : {1, 2, 3, 4, 5})
                for (const StripItem& s : stripTools(app.mode))
                    for (const StripItem& m : s.members)
                        if (s.group == g && m.tool == it.tool) app.lastInGroup[g] = it.tool;
        app.terrainPasting = false;
        fillToolCombo(app);  // the strip's rows (size, density) follow the tool
        if (app.palette < 0 && app.mode != Mode::Terrain) setMessage(L"Select what to place in the list");
        if (app.mode == Mode::Terrain && isShapeTool(it.tool)) setMessage(it.tip);  // (Line, Contour, Polygon: how they work)
        updateStatus();
    }
    shellToolsChanged();
    tilesSync();
    shellFocusView();
}
// The keys V, B, A, F: the active mode's tool of that key (the Map workspace only).
void stripToolByKey(int key) {
    if (gWorkspace != Workspace::Map) return;
    for (const StripItem& it : stripTools(appState().mode)) {
        if (it.disabled && stripToolKey(it) == key) return setMessage(L"Select terrain first, with Select, the Lasso or the Magic wand");
        if (it.members.empty() && stripToolKey(it) == key) return pickStripTool(it);
        for (const StripItem& m : it.members)  // (a group: each of its tools by its own key)
            if (stripToolKey(m) == key) return pickStripTool(m);
    }
}
// A group's tools in a row of icon buttons under its button `button` (in `from`'s coordinates): its arrow, a right
// click on it. A click on one picks it and closes the row; Esc or a click outside closes it. (`it` a copy: the strip
// paints its buttons again while the row is open.)
void stripGroupPopout(StripItem it, draw::View& from, draw::Rect button);

// Terrain with a mask on (the terrain mask or the tile mask): the map shows a chip with what is in it, at its top left
// (sdl_main_view.inc: MapView; on the map, so that the lists do not move when a mask starts).
bool stripMaskShown() {
    const AppState& app = appState();
    return app.mode == Mode::Terrain && app.ed && app.ed->isOpen() && (!app.ed->terrainMask().empty() || !app.ed->tileMask().empty());
}
// The chip's text: "Mask: Dirt, Mud" (the terrains), "3 tiles" (the tiles), or both.
std::wstring stripMaskText() {
    const Editor& ed = *appState().ed;
    std::wstring t;
    const auto& tm = ed.terrainMask();
    for (size_t i = 0; i < tm.size() && i < 3; ++i) t += (i ? L", " : L"") + widen(ed.terrainName(tm[i]));
    if (tm.size() > 3) t += L" +" + std::to_wstring(tm.size() - 3);
    if (const size_t n = ed.tileMask().size()) t += (t.empty() ? L"" : L"  \x00b7  ") + std::to_wstring(n) + (n == 1 ? tr(L" tile") : tr(L" tiles"));
    return tr(L"Mask: ") + t;
}

int toolStripHeight(int width, bool titled) {
    const AppState& app = appState();
    const int b = scale(30), gap = scale(4);
    const int perRow = std::max(1, (width + gap) / (b + gap));
    const int tools = int(stripTools(app.mode).size());
    const int rows = (tools + perRow - 1) / perRow;  // (Terrain: the brush's shape is in its size field)
    // (the mirrors' row: Turns on it where it fits, as paint puts it; else a row of its own below)
    const bool turnsBeside = true;  // (the symmetry button and the Brushes field are one control on one row)
    const int extra = (stripHasSize() ? scale(26) + gap : 0) + (stripHasMirrors() ? scale(28) + gap + (turnsBeside ? 0 : scale(26) + gap) : 0) +
                      (stripHasDensity() ? scale(26) + gap : 0) +
                      (stripHasMix() ? scale(26) + gap : 0) + (app.mode == Mode::Terrain ? scale(26) + gap : 0) +  // Terrain: Isometric | Rectangle
                      (stripHasMore() ? scale(22) + gap + (stripMoreOpen() ? scale(26) + gap : 0) : 0);  // (More, and its rows)
    return (titled ? scale(24) : 0) + rows * b + (rows - 1) * gap + extra;
}
// The strip's height for a panel (its tools, its sizes, its options: panel_state.inc).
int toolStripHeight(int width, PanelState* panel) {
    PanelScope scope(panel);
    return toolStripHeight(width, false);
}

// --- Drawing helpers (the kit's look, on the canvas) -------------------------------------------------------------------
namespace stripLook {
using draw::Canvas;
using draw::Color;
using draw::Rect;
constexpr Color kNoLine = draw::kNone;

// A rounded box (the kit's box: the radius is in 96-dpi pixels).
void box(Canvas& c, Rect r, Color fill, Color line, int radius = kStripRadius) { c.roundBox(r, fill, line, float(scale(radius))); }
Rect square(Rect r) {
    const int w = r.width(), h = r.height(), s = std::min(w, h);
    const int x = r.left + (w - s) / 2, y = r.top + (h - s) / 2;
    return {x, y, x + s, y + s};
}
// A small UI icon (a chevron, the link), centered: the 16 px icon (`size`: kept for the callers).
void glyph(Canvas& c, Rect r, UiIcon g, Color col, int size) {
    (void)size;
    c.icon(r, int(g), col, scale(16));
}
// A UI icon, centered, at the size of 16 (the strip's icon buttons) or `px`.
void icon(Canvas& c, Rect r, UiIcon i, Color col, int px = 16) { c.icon(r, int(i), col, scale(px)); }

// The button face (ui::buttonFace): Normal, Primary (the chosen one of a pair) and Hot.
void buttonFace(Canvas& c, Rect r, const std::wstring& text, bool primary, bool hot) {
    Color fill = kCtl, line = kBorder;
    if (primary) fill = kSel, line = kAccentLine;
    if (hot) fill = primary ? kSelHot : kCtlHot, line = primary ? kAccent : kBorderHot;
    box(c, r, fill, line);
    c.text(r, text, primary ? kAccent : kText, draw::kMiddle | draw::kCenter | draw::kEllipsis);
}
// A flat icon button (ui::iconButton, Boxed): a fill when on (the accent) or under the mouse.
void iconButtonFace(Canvas& c, Rect r, UiIcon g, bool on, bool over, bool disabled = false) {
    r = square(r);
    if (disabled) return icon(c, r, g, kFaint, 16);
    if (on) box(c, r, kSel, kAccentLine);
    else if (over) box(c, r, kCtlHot, kNoLine);
    icon(c, r, g, on ? kAccentBright : over ? kText : kDim, 16);  // (a UI icon is always at 16, as the Win32 glyph was)
}
// A mirror's picture: a square and its axis (`kind`: Mirror flags; several: their axes together, as the symmetry
// button shows the mirrors in use). Smooth, in the button's color (the square's line at 110 of 255).
void mirrorIcon(Canvas& c, Rect r, int kind, Color col, Color /*ground*/) {
    using P = Canvas::PointF;
    const float s = float(scale(14)), x0 = (r.left + r.right - s) / 2.0f, y0 = (r.top + r.bottom - s) / 2.0f;
    const float aw = float(std::max(1, scale(2))) * 0.75f;  // the axis's width
    c.smoothFrame(P{x0, y0}, s, s, col, 1.0f, 110);
    if (kind & Mirror::LeftRight) c.smoothLine(P{x0 + s / 2, y0 - 1}, P{x0 + s / 2, y0 + s + 1}, col, aw);
    if (kind & Mirror::TopBottom) c.smoothLine(P{x0 - 1, y0 + s / 2}, P{x0 + s + 1, y0 + s / 2}, col, aw);
    if (kind & Mirror::Diagonal) c.smoothLine(P{x0, y0}, P{x0 + s, y0 + s}, col, aw);
    if (kind & Mirror::AntiDiagonal) c.smoothLine(P{x0 + s, y0}, P{x0, y0 + s}, col, aw);
}
// The brushes' number as dots (Brushes: the brushes go round the symmetry center): n dots in a ring round the
// middle of `r` (1: one dot in the middle), the first at the top.
void turnDots(Canvas& c, Rect r, int n, Color col) {
    using P = Canvas::PointF;
    const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f, d = float(std::max(2, scale(4)));
    const float ring = n <= 1 ? 0 : std::min((r.bottom - r.top) / 2.0f - d, d * (0.9f + 0.32f * float(n)));
    for (int k = 0; k < std::max(1, n); ++k) {
        const double a = 6.283185307179586 * k / std::max(1, n);
        const float x = cx + ring * float(std::sin(a)), y = cy - ring * float(std::cos(a));
        c.smoothEllipse(P{x - d / 2, y - d / 2}, d, d, col);
    }
}
}  // namespace stripLook

// The pop-out of a group's tools (stripGroupPopout): one row of icon buttons, the tool in use on.
struct ToolPopoutView final : draw::View {
    StripItem group{};
    int hot = -1;
    bool open = false;
    static int cell() { return scale(30); }
    static int pad() { return scale(3); }
    int at(draw::Point p) const {
        if (p.y < pad() || p.y >= pad() + cell() || p.x < pad()) return -1;
        const int i = (p.x - pad()) / cell();
        return i < int(group.members.size()) ? i : -1;
    }
    void paint(draw::Canvas& c) override {
        using namespace stripLook;
        c.fill({0, 0, width(), height()}, kPanel);
        c.frame({0, 0, width(), height()}, kBorderHot, 1);
        const int now = appState().toolByMode[int(appState().mode)];
        for (size_t i = 0; i < group.members.size(); ++i) {
            const draw::Rect r{pad() + int(i) * cell(), pad(), pad() + int(i + 1) * cell(), pad() + cell()};
            iconButtonFace(c, r, group.members[i].icon, group.members[i].tool == now, int(i) == hot);
        }
    }
    bool hoverTip() const override { return true; }
    std::wstring tip(draw::Point p) override {
        const int i = at(p);
        if (i < 0) return {};
        const StripItem& m = group.members[size_t(i)];
        const int key = stripToolKey(m);
        return std::wstring(tr(m.tip)) + (key >= 0 ? std::wstring(L" (") + stripKeyName(key) + L")" : L"");
    }
    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move:
            if (const int i = at(e.pt); i != hot) hot = i, invalidate();
            return true;
        case draw::MouseKind::Leave:
            if (hot >= 0) hot = -1, invalidate();
            return true;
        case draw::MouseKind::Down: {
            if (e.button != 0) return true;
            const int i = at(e.pt);
            if (i < 0) return true;
            const StripItem pick = group.members[size_t(i)];
            shellPost([this, pick] {  // (after this event: the row's window closes, then the tool; the row lives as long as the program)
                close();
                pickStripTool(pick);
            });
            return true;
        }
        default: return false;
        }
    }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape) return close(), true;
        return false;
    }
    void close() {
        if (!open) return;
        open = false, hot = -1;
        if (draw::Host* h = host()) h->closeWindow(*this);
    }
};
ToolPopoutView gToolPopout;
// --dialog toolpopout (a snapshot, --mode terrain): the row of the Terrain strip's shapes button, as it opens (--size 222x36).
draw::View* toolPopoutShot() {
    for (const StripItem& it : stripTools(Mode::Terrain))
        if (it.group == 2) gToolPopout.group = it;
    gToolPopout.hot = 3;  // (the mouse over Rectangle)
    return &gToolPopout;
}

void stripGroupPopout(StripItem it, draw::View& from, draw::Rect button) {
    draw::Host* host = from.host();
    if (!host || it.members.empty()) return;
    gToolPopout.close();
    gToolPopout.group = std::move(it);
    const int w = int(gToolPopout.group.members.size()) * ToolPopoutView::cell() + 2 * ToolPopoutView::pad(),
              h = ToolPopoutView::cell() + 2 * ToolPopoutView::pad();
    const draw::Rect at{button.left - ToolPopoutView::pad(), button.bottom + scale(2), button.left - ToolPopoutView::pad() + w, button.bottom + scale(2) + h};
    gToolPopout.open = true;
    host->openWindow(from, gToolPopout, draw::WindowSpec{draw::WindowKind::Popup, at, L""}, [] { gToolPopout.open = false, gToolPopout.hot = -1; });
}

// The symmetry button's pop-out (stripMirrorPopout): No symmetry, then the four mirrors. A mirror is on or off at a
// click (they combine) and the row stays open; No symmetry turns them all off (and the brushes to 1) and closes it.
struct MirrorKind {
    int flag;
    const wchar_t* tip;
};
constexpr MirrorKind kMirrorKinds[] = {{0, L"No symmetry"},
                                       {Mirror::LeftRight, L"Mirror left and right"},
                                       {Mirror::TopBottom, L"Mirror top and bottom"},
                                       {Mirror::Diagonal, L"Mirror across the line from top left to bottom right"},
                                       {Mirror::AntiDiagonal, L"Mirror across the line from top right to bottom left"}};
void setMirrorKind(int flag) {  // (a click on a mirror, or on No symmetry: 0)
    AppState& a = appState();
    if (!a.ed) return;
    a.ed->setMirrors(flag ? a.ed->mirrors() ^ flag : 0);
    if (!flag) a.ed->setTurns(1);  // (No symmetry turns the brushes off too)
    shellToolsChanged();
    a.dirty = true;
}
struct MirrorPopoutView final : draw::View {
    int hot = -1;
    bool open = false;
    static int cell() { return scale(30); }
    static int pad() { return scale(3); }
    int at(draw::Point p) const {
        if (p.y < pad() || p.y >= pad() + cell() || p.x < pad()) return -1;
        const int i = (p.x - pad()) / cell();
        return i < int(std::size(kMirrorKinds)) ? i : -1;
    }
    void paint(draw::Canvas& c) override {
        using namespace stripLook;
        c.fill({0, 0, width(), height()}, kPanel);
        c.frame({0, 0, width(), height()}, kBorderHot, 1);
        const AppState& a = appState();
        const int mask = a.ed ? a.ed->mirrors() : 0;
        for (size_t i = 0; i < std::size(kMirrorKinds); ++i) {
            const draw::Rect r{pad() + int(i) * cell(), pad(), pad() + int(i + 1) * cell(), pad() + cell()};
            const int flag = kMirrorKinds[i].flag;
            const bool on = flag ? (mask & flag) != 0 : mask == 0 && (!a.ed || a.ed->turns() == 1), over = int(i) == hot;
            const draw::Rect sq = square(r);
            if (on) box(c, sq, kSel, kAccentLine);
            else if (over) box(c, sq, kCtlHot, kNoLine);
            mirrorIcon(c, sq, flag, on ? kAccentBright : over ? kText : kDim, on ? kSel : kPanel);
            if (i == 0) c.fill({r.right - 1, r.top + scale(4), r.right, r.bottom - scale(4)}, kBorder);  // (a line after No symmetry: the mirrors combine)
        }
    }
    bool hoverTip() const override { return true; }
    std::wstring tip(draw::Point p) override {
        const int i = at(p);
        return i < 0 ? std::wstring() : std::wstring(tr(kMirrorKinds[i].tip)) + L" (Alt+" + std::to_wstring(i) + L")";
    }
    bool mouse(const draw::MouseEvent& e) override {
        switch (e.kind) {
        case draw::MouseKind::Move:
            if (const int i = at(e.pt); i != hot) hot = i, invalidate();
            return true;
        case draw::MouseKind::Leave:
            if (hot >= 0) hot = -1, invalidate();
            return true;
        case draw::MouseKind::Down: {
            if (e.button != 0) return true;
            const int i = at(e.pt);
            if (i < 0) return true;
            const int flag = kMirrorKinds[i].flag;
            setMirrorKind(flag);
            if (!flag) shellPost([this] { close(); });  // (No symmetry: done; a mirror: the row stays for the next)
            invalidate();
            return true;
        }
        default: return false;
        }
    }
    bool key(const draw::KeyEvent& e) override {
        if (e.down && e.key == draw::Key::Escape) return close(), true;
        return false;
    }
    void close() {
        if (!open) return;
        open = false, hot = -1;
        if (draw::Host* h = host()) h->closeWindow(*this);
    }
};
MirrorPopoutView gMirrorPopout;
std::chrono::steady_clock::time_point gMirrorPopoutClosedAt{};  // (a press outside closes it before the button's click comes)
void stripMirrorPopout(draw::View& from, draw::Rect button) {
    draw::Host* host = from.host();
    if (!host) return;
    if (gMirrorPopout.open) return gMirrorPopout.close();  // (a second click on the button: it closes)
    // (the press on the button closed it already, as a press outside the pop-out does: the click only closes it)
    if (std::chrono::steady_clock::now() - gMirrorPopoutClosedAt < std::chrono::milliseconds(400)) return;
    const int w = int(std::size(kMirrorKinds)) * MirrorPopoutView::cell() + 2 * MirrorPopoutView::pad(),
              h = MirrorPopoutView::cell() + 2 * MirrorPopoutView::pad();
    const draw::Rect at{button.left - MirrorPopoutView::pad(), button.bottom + scale(2), button.left - MirrorPopoutView::pad() + w, button.bottom + scale(2) + h};
    gMirrorPopout.open = true;
    host->openWindow(from, gMirrorPopout, draw::WindowSpec{draw::WindowKind::Popup, at, L""}, [] { gMirrorPopout.open = false, gMirrorPopout.hot = -1, gMirrorPopoutClosedAt = std::chrono::steady_clock::now(); });
}

// --- The view -----------------------------------------------------------------------------------------------------
class ToolStripView;
extern ToolStripView* gStripInUse;  // the strip of the last mouse event (a dropdown of it opens there)

class ToolStripView final : public draw::View {
public:
    // Its panel's tools (panel_state.inc), whichever panel is active; a press on it makes the panel active first. Empty:
    // the active panel's.
    PanelState* state = nullptr;

    ToolStripView() {
        field_.onStep = [this](int dir) {
            const int which = editing_;
            if (which < 0) return;
            stripNumberSet(which, field_.text());
            stripNumberStep(which, dir);
            field_.setText(stripNumberText(which));
            field_.model().selectAll();
        };
        field_.onEnter = [this] {
            endEdit(true);
            shellFocusView();
        };
        field_.onEscape = [this] {
            endEdit(false);
            shellFocusView();
        };
        field_.onFocusLost = [this] { endEdit(true); };
        field_.setPlain(true);      // the strip's box is the field's box (as the old native edit: no border)
        field_.setAlignRight(true);  // the number sits at the right, as it did (ES_RIGHT)
        add(field_, {0, 0, 0, 0});
        field_.setVisible(false);
    }

    // The click areas of the last paint (for the self-test, and the mouse).
    const std::vector<StripHit>& hits() const { return hits_; }
    // The number box `which` is being typed into (its edit field is shown).
    bool editing(int which) const { return editing_ == which && field_.visible(); }
    // A number is being typed: the field has the keyboard (the message loop leaves its keys to it).
    bool typing() const { return editing_ >= 0; }

    void paint(draw::Canvas& c) override {
        const MutedAccent muted(state && state != gActivePanel);  // (no focus: gray)
        using namespace stripLook;
        PanelScope scope(state);
        AppState& app = appState();
        const draw::Font prevFont = c.setFont(c.font({draw::Face::Ui, 0}));  // the UI font
        const int W = width(), H = height();
        c.fill({0, 0, W, H}, kBg);
        hits_.clear();
        numbers_.clear();
        groups_.clear();
        const int b = scale(30), gap = scale(4);
        int y = 0, x = 0;
        auto next = [&] {
            x += b + gap;
            if (x + b > W) x = 0, y += b + gap;
        };
        auto addHitC = [&](draw::Rect r, std::function<void()> click, std::wstring tip, draw::Cursor cur) {
            hits_.push_back({r, std::move(click), std::move(tip), cur});
        };
        auto addHit = [&](draw::Rect r, std::function<void()> click, std::wstring tip) {
            addHitC(r, std::move(click), std::move(tip), draw::Cursor::Hand);
        };
        // Terrain: Isometric, Rectangle, Tile or Custom (kTerrainKindOrder), a segmented choice over the tools: one box with a
        // line between the segments, rounded only at its ends; the chosen one primary (as the modes' bar,
        // core/views_modebar.inc). Each segment is its name's width and an equal part of the space that is left.
        if (app.mode == Mode::Terrain) {
            const int sh = scale(26), over = scale(8);
            static const struct {
                const wchar_t* name;
                const wchar_t* tip;
            } segs[kTerrainKinds] = {{L"Isometric", L"Isometric (Ctrl+Alt+1): paint terrains, with the edges corrected. Ctrl+Alt+T selects the next choice."},
                                     {L"Tile", L"Tile (Ctrl+Alt+3): place tiles as they are, with no terrain correction. Ctrl+Alt+T selects the next choice."},
                                     {L"Custom", L"Custom (Ctrl+Alt+4): place your custom terrain, tiles that you saved from a map. Ctrl+Alt+T selects the next choice."},
                                     {L"Rectangle", L"Rectangle (Ctrl+Alt+2): place the tiles inside a terrain, with no edges, in rects of 2 x 1 tiles. Ctrl+Alt+T selects the next choice."}};
            const int kind = terrainKind();
            int edge[kTerrainKinds + 1] = {0}, names = 0;
            for (int k = 0; k < kTerrainKinds; ++k) names += c.measure(tr(segs[k].name));
            for (int i = 0; i < kTerrainKinds; ++i) edge[i + 1] = edge[i] + c.measure(tr(segs[kTerrainKindOrder[i]].name)) + (W - names) / kTerrainKinds;
            edge[kTerrainKinds] = W;
            for (int i = 0; i < kTerrainKinds; ++i) {
                const int k = kTerrainKindOrder[i];  // (the kind at place i)
                const draw::Rect r{edge[i], y, edge[i + 1], y + sh};
                const bool on = kind == k;
                c.pushClip(r);
                draw::Rect seg = r;  // (the box drawn wider on the inner sides: the segment's clip cuts it)
                if (i > 0) seg.left -= over;
                if (i < kTerrainKinds - 1) seg.right += over;
                c.roundBox(seg, on ? kSel : hot(r) ? kCtlHot : kCtl, on ? kAccentLine : kBorder, float(scale(kModeBoxRadius)));
                if (i < kTerrainKinds - 1) c.fill({r.right - 1, r.top + 1, r.right, r.bottom - 1}, kBorder);  // the line to the next segment
                c.text(r, tr(segs[k].name), on ? kAccent : kText, draw::kCenter | draw::kMiddle);
                c.popClip();
                addHit(r, [k] {
                    setTerrainKind(k);
                    shellFocusView();
                }, segs[k].tip);
            }
            y += sh + gap;
        }
        const auto tools = stripTools(app.mode);
        const bool picked = app.mode != Mode::Terrain && app.palette >= 0;
        for (size_t i = 0; i < tools.size(); ++i) {
            const StripItem it = tools[i];
            // Select: the tool with nothing picked. Place (the same tool): with a pick. The others: their tool.
            const bool shared = std::count_if(tools.begin(), tools.end(), [&](const StripItem& o) { return o.tool == it.tool; }) > 1;
            const bool on = currentTool() == it.tool && (!shared || it.select != picked) && (app.mode == Mode::Terrain || !it.select || !picked);
            const int key = stripToolKey(it);
            const draw::Rect r{x, y, x + b, y + b};
            iconButtonFace(c, r, it.icon, on, hot(square(r)), it.disabled);
            if (it.disabled) {  // (Move with nothing selected)
                addHit(square(r), [] { setMessage(L"Select terrain first, with Select, the Lasso or the Magic wand"); },
                       std::wstring(tr(it.tip)) + (key >= 0 ? std::wstring(L" (") + stripKeyName(key) + L")" : L"") + L" " + tr(L"Not available: no terrain is selected."));
                next();
                continue;
            }
            // A group: a click selects its tool and shows its tools under it.
            addHit(square(r), [this, it, sq = square(r)] {
                pickStripTool(it);
                if (!it.members.empty()) stripGroupPopout(it, *this, sq);
            }, (key >= 0 ? std::wstring(tr(it.tip)) + L" (" + stripKeyName(key) + L")" : std::wstring(it.tip)) +
                   (it.members.empty() ? L"" : tr(L" A click also shows the tools of this button.")));
            if (!it.members.empty()) {  // its arrow: a small triangle in the corner, a click lists the tools
                const draw::Rect sq = square(r);
                // (a small smooth triangle in the corner, its right angle at the corner)
                const float s = float(scale(5)), x1 = float(sq.right - scale(3)), y1 = float(sq.bottom - scale(3));
                const draw::Canvas::PointF tri[3] = {{x1, y1 - s}, {x1, y1}, {x1 - s, y1}};
                c.smoothPolygon(tri, 3, on ? kAccentBright : kDim);
                const draw::Rect corner{sq.right - scale(11), sq.bottom - scale(11), sq.right, sq.bottom};
                addHit(corner, [this, it, sq] { stripGroupPopout(it, *this, sq); }, tr(L"The tools of this button"));
                groups_.push_back({sq, it});
            }
            next();
        }
        if (x > 0) x = 0, y += b + gap;
        // The brush size and the spray's density: number boxes (the name, the value, up and down arrows).
        // A stepper: - and + at the ends of the box, the value between them (a click there types one).
        // A number's box: its name inside at the left, its value (and a small unit) at the right, the spin's arrows after it,
        // as the brush's width and height. A click on the box types the value; the arrows and the wheel step it.
        auto spinner = [&](int which, const draw::Rect& vb, const wchar_t* name, const wchar_t* unit, const wchar_t* tip, bool framed = true) {
            const bool editing = editing_ == which;
            const int spin = scale(14), pad = scale(4), mid = (vb.top + vb.bottom) / 2;
            if (framed) box(c, vb, kCtl, editing ? kAccent : kBorder);
            const draw::Rect value{vb.left, vb.top, vb.right - spin, vb.bottom}, up{vb.right - spin, vb.top + pad, vb.right - scale(2), mid},
                down{vb.right - spin, mid, vb.right - scale(2), vb.bottom - pad};
            const int unitW = [&] {
                if (!*unit) return 0;
                const draw::Font of = c.setFont(c.font({draw::Face::Ui, smallPx()}));
                const int w = c.measure(unit);
                c.setFont(of);
                return w + scale(2);
            }();
            const draw::Rect typed{std::max(value.left, value.right - scale(52)), value.top, value.right, value.bottom};  // (the field over the value)
            const std::wstring t = stripNumberText(which);
            const int nw = c.measure(t), nx = value.right - scale(6) - unitW - nw;
            if (which != kNumTurns || editing || (appState().ed && appState().ed->turns() > 12)) c.text({value.left + scale(8), value.top, (editing ? typed.left : nx) - scale(4), value.bottom}, name, kDim, draw::kMiddle | draw::kEllipsis);  // (Brushes: its dots alone)
            if (!editing && which == kNumTurns && (!appState().ed || appState().ed->turns() <= 12)) {  // (Brushes: as many dots as brushes, in a ring, in place of the number; more than 12: the number)
                const int n = appState().ed ? appState().ed->turns() : 1, dw = std::min(value.right - value.left - scale(8), value.bottom - value.top), mid = (value.left + value.right) / 2;
                turnDots(c, {mid - dw / 2, value.top, mid - dw / 2 + dw, value.bottom}, n, kText);
            } else if (!editing) {
                c.text({nx, value.top, nx + nw, value.bottom}, t, kText, draw::kMiddle | draw::kLeft);
                if (*unit) {
                    const draw::Font of = c.setFont(c.font({draw::Face::Ui, smallPx()}));
                    c.text({nx + nw + scale(2), value.top + scale(2), value.right, value.bottom}, unit, kFaint, draw::kMiddle | draw::kLeft);
                    c.setFont(of);
                }
            }
            for (const bool more : {true, false}) {
                const draw::Rect k = more ? up : down;
                const bool over = hot(k);
                if (over) box(c, k, kCtlHot, kNoLine, 2);
                glyph(c, k, more ? UiIcon::ChevronUp : UiIcon::ChevronDown, over ? kText : kDim, 6);  // chevrons up and down
            }
            addHitC(value, [this, which, typed] { beginEdit(which, typed); }, tip, draw::Cursor::IBeam);
            addHit(up, [which] { stripNumberStep(which, 1); }, L"One step more");
            addHit(down, [which] { stripNumberStep(which, -1); }, L"One step less");
            numbers_.push_back({vb, which});
        };
        auto numberRow = [&](int which, const wchar_t* name, const wchar_t* tip, const wchar_t* unit = L"") {
            const int h = scale(26);
            spinner(which, {0, y + scale(1), std::min(W, scale(184)), y + h - scale(1)}, name, unit, tip);
            y += h + gap;
        };
        static const wchar_t* const kSizeTip = L"Brush size: 1 to 32. Click to type a size. The arrows and the mouse wheel change it (Alt+wheel on the map too).";
        static const wchar_t* const kTurnsTip =
            L"Brushes: the brushes go round the symmetry center in equal steps. 1: one brush, 2: two (a half turn), 4: four (quarter turns), 1 to 64. Click to type a number. The arrows and the mouse wheel change it. On isometric terrain, a copy goes to the nearest diamond unless the turn keeps the diamonds (2, and 4 on a map twice as wide as high).";
        static const wchar_t* const kHeightTip =
            L"Brush height: 1 to 32. Click to type a height (empty: the same as the size). The arrows and the mouse wheel change it.";
        if (stripHasSize() && app.mode == Mode::Terrain) {
            // Terrain: the brush in one field the width of the strip (GitHub #113):
            //   [ shape v | W n ^v | H n ^v | link ], with the link on: [ shape v | Size n ^v | link ]
            // The shape opens a list; each number has a spin at its right, takes the mouse wheel and a click types it. The
            // link makes the width and the height one number.
            const int h = scale(26), seg = scale(40), linkW = scale(34), spin = scale(14), pad = scale(4);
            const bool linked = app.brushHeight <= 0;
            const bool editing = editing_ == kNumSize || editing_ == kNumHeight;
            const draw::Rect boxR{0, y, W, y + h};
            box(c, boxR, kCtl, editing ? kAccent : kBorder);
            auto divider = [&](int dx) { box(c, {dx, boxR.top + scale(6), dx + 1, boxR.bottom - scale(6)}, kBorder, kNoLine, 0); };
            auto hotFill = [&](const draw::Rect& r) {
                if (hot(r)) box(c, {r.left + scale(2), r.top + scale(2), r.right - scale(2), r.bottom - scale(2)}, kCtlHot, kNoLine, 3);
            };
            // The shape.
            static const UiIcon shapeIcons[3] = {UiIcon::ShapeDiamond, UiIcon::ShapeSquare, UiIcon::ShapeCircle};
            const draw::Rect sr{boxR.left, boxR.top, boxR.left + seg, boxR.bottom};
            hotFill(sr);
            c.icon({sr.left + scale(4), sr.top, sr.left + scale(24), sr.bottom}, int(shapeIcons[int(shapeOf(app)) % 3]), kText, scale(14));
            glyph(c, {sr.left + scale(24), sr.top, sr.right - scale(4), sr.bottom}, UiIcon::ChevronDown, kDim, 7);
            addHit(sr, [this, sr] {
                MenuModel m;
                for (int k = 0; k < 3; ++k) m.push_back(menuItem(k + 1, kStripShapeNames[k], {}, false, int(shapeOf(appState())) == k));
                PanelState* panel = state;
                shellMenuAt(*this, {sr.left, sr.bottom + scale(2)}, m, L"the brush shape", [panel](int id) {
                    PanelScope scope(panel);
                    shapeOf(appState()) = Brush::Shape(std::clamp(id - 1, 0, 2));
                    shellSettingsChanged();
                    shellToolsChanged();
                    shellFocusView();
                });
            }, L"Brush shape: diamond, square or circle. Click to select one; the mouse wheel changes it.");
            numbers_.push_back({sr, kNumShape});
            divider(sr.right);
            // The link.
            const draw::Rect lk{boxR.right - linkW, boxR.top, boxR.right, boxR.bottom};
            hotFill(lk);
            glyph(c, lk, UiIcon::Link, linked ? kAccent : kFaint, 10);
            addHit(lk, [] {
                AppState& a = appState();
                a.brushHeight = a.brushHeight <= 0 ? brushHeightOf(a) : 0;  // as it is now (a typed height may just have been set)
                stripNumberChanged();
                shellFocusView();
            }, linked ? L"The height is the same as the size. Click to give the height a value of its own."
                        : L"The height has a value of its own. Click to make it the same as the size.");
            divider(lk.left);
            // One size, or the width and the height with a divider between them.
            const int mx = (sr.right + lk.left) / 2;
            auto part = [&](int which, int left, int right, const wchar_t* name, const wchar_t* tip) {
                const int mid = (boxR.top + boxR.bottom) / 2;
                const draw::Rect value{left, boxR.top, right - spin, boxR.bottom}, up{right - spin, boxR.top + pad, right - scale(2), mid},
                    down{right - spin, mid, right - scale(2), boxR.bottom - pad};
                // Its name inside at the left, the number at the right (as the other number boxes).
                const std::wstring number = stripNumberText(which);
                const int nameW = c.measure(tr(name));
                const int nameLeft = value.left + scale(7);
                const int nameRight = nameLeft + nameW;
                c.text({nameLeft, value.top, nameRight, value.bottom}, name, kDim, draw::kMiddle | draw::kLeft);
                const draw::Rect typed{nameRight + scale(1), value.top, value.right, value.bottom};  // (the field over the number)
                if (editing_ != which) c.text({typed.left, value.top, value.right - scale(4), value.bottom}, number, kText, draw::kMiddle | draw::kRight);
                for (const bool more : {true, false}) {
                    const draw::Rect k = more ? up : down;
                    const bool over = hot(k);
                    if (over) box(c, k, kCtlHot, kNoLine, 2);
                    glyph(c, k, more ? UiIcon::ChevronUp : UiIcon::ChevronDown, over ? kText : kDim, 6);  // chevrons up and down
                }
                addHitC(value, [this, which, typed] { beginEdit(which, typed); }, tip, draw::Cursor::IBeam);
                addHit(up, [which] { stripNumberStep(which, 1); }, L"One step more");
                addHit(down, [which] { stripNumberStep(which, -1); }, L"One step less");
                numbers_.push_back({{left, boxR.top, right, boxR.bottom}, which});
            };
            if (linked) {
                part(kNumSize, sr.right + 1, lk.left, L"Size", kSizeTip);
            } else {
                part(kNumSize, sr.right + 1, mx, L"W", kSizeTip);
                divider(mx);
                part(kNumHeight, mx + 1, lk.left, L"H", kHeightTip);
            }
            y += h + gap;
        } else if (stripHasSize()) {
            numberRow(kNumSize, L"Size", kSizeTip);
        }
        if (stripHasDensity())
            numberRow(kNumDensity, L"Density",
                      app.mode == Mode::Terrain ? L"Density: how much of the brush each spray paints, 1 to 100%. Click to type a number. The arrows and the mouse wheel change it."
                                                : L"Density: how much of the brush each spray fills, 1 to 100% (only where things fit); Fill: how much of the ground. Click to type a number. The arrows and the mouse wheel change it.", L"%");
        if (stripHasMix())
            numberRow(kNumMix, L"Mix",
                      L"Mix: with several terrains or tiles selected, how Fill puts them in patches, 1 (large patches) to 10 (the smallest: each tile at random; Isometric: 2 diamonds). Click to type a number. The arrows and the mouse wheel change it.");
        // The symmetry: one button that shows the mirrors in use together (their axes in one square); a click opens
        // the row of No symmetry and the four mirrors (stripMirrorPopout), which combine.
        if (stripHasMirrors() && app.ed) {
            // One control: the symmetry button, a line, then the brushes (its dots and arrows), in one frame.
            const int mb = scale(28);
            const int mask = app.ed->mirrors();
            const draw::Rect br{0, y, mb + scale(2), y + mb}, joined{0, y, std::min(W, br.right + scale(68)), y + mb};
            const bool on = mask != 0, over2 = hot(br) || gMirrorPopout.open;
            c.roundBox(joined, kCtl, editing_ == kNumTurns ? kAccent : kBorder, float(scale(kModeBoxRadius)));
            if (on || over2) {  // (the button's part: its fill inside the frame, the left corners round)
                c.pushClip({br.left, br.top, br.right, br.bottom});
                c.roundBox({br.left, br.top, br.right + scale(8), br.bottom}, on ? kSel : kCtlHot, on ? kAccentLine : kNoLine, float(scale(kModeBoxRadius)));
                c.popClip();
            }
            c.fill({br.right, br.top + 1, br.right + 1, br.bottom - 1}, kBorder);  // (the line between the two)
            mirrorIcon(c, square(br), mask, on ? kAccentBright : over2 ? kText : kDim, on ? kSel : over2 ? kCtlHot : kCtl);
            addHit(br, [this, br] { stripMirrorPopout(*this, br); }, tr(L"Symmetry: click to choose the mirrors (Alt+0 to Alt+4)"));
            spinner(kNumTurns, {br.right + 1, y, joined.right, y + mb}, L"Brushes", L"", kTurnsTip, false);
            y += mb + gap;
        }
        // More: a fold (a chevron and its name, then a line), and its rows when it is open.
        if (stripHasMore()) {
            const int fh = scale(22);
            const draw::Rect fold{0, y, W, y + fh};
            const bool open = stripMoreOpen();
            c.icon({0, y, scale(16), y + fh}, int(open ? UiIcon::ChevronDown : UiIcon::ChevronRight), hot(fold) ? kText : kDim, scale(16));
            c.text({scale(18), y, W, y + fh}, tr(L"More"), hot(fold) ? kText : kDim, draw::kMiddle);
            const int lineL = scale(18) + c.measure(tr(L"More")) + scale(8);
            if (lineL < W) c.fill({lineL, y + fh / 2, W, y + fh / 2 + 1}, kBorder);
            addHit(fold, [] {
                bool& o = stripMoreOpen();
                o = !o;
                if (!gSelfTest && !gSettingsOff) prefs::setNumber(L"ToolStripMore", o ? 1 : 0);
                shellToolsChanged();
                void shellRelayout();
                shellRelayout();
            }, open ? L"Hide the less used values" : L"Show the less used values: Detailed tiles");
            y += fh + gap;
            if (open)
                numberRow(kNumDetail, L"Detailed tiles",
                          L"Detailed tiles: how often Isometric and Rectangle painting places a detailed tile, 0 to 100%. StarCraft: 5%. Click to type a number. The arrows and the mouse wheel change it.",
                          L"%");
        }
        c.setFont(prevFont);
        // The hits (their tips and cursors) are the last paint's; a hover change repaints.
        (void)H;
        if (keys_.shows(*this, int(hits_.size()))) paintFocusOutline(c, hits_[size_t(keys_.at)].r);    }

    // The keyboard: Tab goes through the strip's parts in their order (Left and Right too); Enter and Space press the
    // part with the focus (a number's value: it can be typed then).
    bool focusable() const override { return !hits_.empty(); }
    bool focusOnClick() const override { return false; }  // (a click leaves the keyboard where it is)
    void focusChanged(bool) override { invalidate(); }
    void tabbedTo(bool back) override { keys_.enter(back, int(hits_.size())), invalidate(); }
    bool tabWithin(bool back) override { return keys_.step(back, int(hits_.size())) && (invalidate(), true); }
    bool key(const draw::KeyEvent& e) override {
        const int k = keys_.index(int(hits_.size()));
        if (k < 0 || !hasFocus()) return false;  // (not a key of the number field)
        if (pressKey(e)) {
            const std::function<void()> click = hits_[size_t(k)].click;  // (the click may repaint, which clears the hits)
            if (state && state != gActivePanel) activatePanel(state);
            PanelScope scope(state);
            keys_.keys = true;
            pressFromKeys(click);
            invalidate();
            return true;
        }
        if (e.down && !e.mods && (e.key == draw::Key::Left || e.key == draw::Key::Right)) {
            keys_.move(e.key == draw::Key::Left ? -1 : 1, int(hits_.size()));
            invalidate();
            return true;
        }
        return false;
    }
    std::wstring focusedItem() const override {
        const int k = keys_.index(int(hits_.size()));
        return k < 0 ? std::wstring() : hits_[size_t(k)].tip;
    }

    bool mouse(const draw::MouseEvent& e) override {
        gStripInUse = this;
        const bool overNumber = std::any_of(numbers_.begin(), numbers_.end(), [&](const auto& n) { return n.first.contains(e.pt); });
        if (state && state != gActivePanel && (e.kind == draw::MouseKind::Down || (e.kind == draw::MouseKind::Wheel && overNumber))) {  // (its panel first)
            // The press makes the panel active, which may move this strip (the side bar's strips follow the active mode):
            // the pressed box does its work now (the release would find another strip under the mouse).
            const int k = e.kind == draw::MouseKind::Down && e.button == 0 ? indexAt(e.pt) : -1;
            const std::function<void()> click = k >= 0 ? hits_[size_t(k)].click : nullptr;
            activatePanel(state);
            if (k >= 0) {
                if (click) click();
                invalidate();
                return true;
            }
        }
        PanelScope scope(state);
        switch (e.kind) {
        case draw::MouseKind::Move: {
            const int before = indexAt(hover_), now = indexAt(e.pt);
            hover_ = e.pt;
            hovering_ = true;
            if (before != now) invalidate();
            return false;
        }
        case draw::MouseKind::Leave:
            if (hovering_) {
                hovering_ = false;
                invalidate();
            }
            return false;
        case draw::MouseKind::Down: {
            if (e.button != 0) return false;
            keys_.pressed();
            if (editing_ >= 0) {  // a click elsewhere ends the typing (it is kept)
                endEdit(true);
                shellFocusView();
            }
            pressed_ = indexAt(e.pt);
            if (pressed_ < 0) return false;
            if (draw::Host* h = host()) h->capture(*this, true);
            return true;
        }
        case draw::MouseKind::Up: {
            if (e.button == 1)  // a right click on a group's button: its tools
                for (const auto& [rect, item] : groups_)
                    if (rect.contains(e.pt)) {
                        const StripItem copy = item;  // (the menu waits; the strip paints again meanwhile)
                        return stripGroupPopout(copy, *this, rect), true;
                    }
            if (e.button != 0 || pressed_ < 0) return false;
            const int k = pressed_;
            pressed_ = -1;
            if (draw::Host* h = host()) h->capture(*this, false);
            if (indexAt(e.pt) == k && hits_[size_t(k)].click) {
                const std::function<void()> click = hits_[size_t(k)].click;  // (a menu it opens waits: the strip paints again meanwhile)
                click();
            }
            return true;
        }
        case draw::MouseKind::Wheel: {  // over a number box: a step; a fast scroll more steps for each notch (up to 8)
            if (e.wheel == 0) return false;
            for (const auto& n : numbers_)
                if (n.first.contains(e.pt)) {
                    endEdit(true);
                    static auto last = std::chrono::steady_clock::time_point{};
                    static int streak = 0;
                    const auto now = std::chrono::steady_clock::now();
                    streak = now - last < std::chrono::milliseconds(90) ? streak + 1 : 0, last = now;
                    const int steps = n.second == kNumShape ? 1 : std::min(8, 1 + streak / 2);
                    for (int k = 0; k < steps; ++k) stripNumberStep(n.second, e.wheel > 0 ? 1 : -1);
                    return true;
                }
            return false;
        }
        default: return false;
        }
    }
    void captureLost() override { pressed_ = -1; }
    std::wstring tip(draw::Point p) override {
        PanelScope scope(state);
        const int k = indexAt(p);
        return k < 0 ? std::wstring() : hits_[size_t(k)].tip;
    }
    bool hoverTip() const override { return true; }
    draw::Cursor cursor(draw::Point p) override {
        PanelScope scope(state);
        const int k = indexAt(p);
        return k < 0 ? draw::Cursor::Arrow : hits_[size_t(k)].cursor;
    }

private:
    // The hit under `p` as painted (-1: none). Later hits are on top.
    int indexAt(draw::Point p) const {
        for (size_t i = hits_.size(); i-- > 0;)
            if (hits_[i].r.contains(p)) return int(i);
        return -1;
    }
    // Whether the pointer is over the rectangle, as last moved (the kit's hot).
    bool hot(draw::Rect r) const { return hovering_ && r.contains(hover_); }
    // The small font's height: the UI font's, five sixths (ui::smallFont).
    int smallPx() const {
        const int h = -shellUiFontLfHeight();
        return h * 5 / 6;
    }

    // Typing a number: the field is placed over the value part (r) and takes the text of the number.
    void beginEdit(int which, draw::Rect r) {
        editing_ = which;
        field_.setFrame({r.left + scale(4), r.top + scale(4), r.right - scale(2), r.bottom - scale(4)});
        field_.setText(stripNumberText(which));
        field_.model().selectAll();
        field_.setVisible(true);
        field_.dropSpace = keyPressRuns();
        field_.focus();
        shellToolsChanged();
    }
    // Ends the typing: `commit` sets the number (a linked height left as it was stays linked).
    void endEdit(bool commit) {
        if (editing_ < 0) return;
        const int which = editing_;
        editing_ = -1;
        const std::wstring text = field_.text();
        field_.setVisible(false);
        const bool unchanged = which == kNumHeight && appState().brushHeight <= 0 && text == stripNumberText(kNumHeight);
        if (commit && !unchanged) stripNumberSet(which, text);
        stripNumberChanged();
    }

    StripNumberField field_;
    std::vector<StripHit> hits_;
    std::vector<std::pair<draw::Rect, int>> numbers_;  // the number boxes (for the wheel)
    ItemFocus keys_;     // the part with the keyboard focus (an index into hits_)
    int editing_ = -1;   // the number box being typed into
    std::vector<std::pair<draw::Rect, StripItem>> groups_;  // the group buttons as painted (a right click lists their tools)
    int pressed_ = -1;   // the hit the mouse is down on
    draw::Point hover_;  // the pointer, as last moved
    bool hovering_ = false;
};

ToolStripView gToolStripView;  // the strip (its window shows it: main.cpp, sdl_main_view.inc)
ToolStripView* gStripInUse = &gToolStripView;
// The side bar's other strips (sdl_main_view.inc): the other side's mode's tools, and the tools of the list that shows
// in the active side while the active mode is in a window of its own.
ToolStripView gToolStripView2, gToolStripView3;
