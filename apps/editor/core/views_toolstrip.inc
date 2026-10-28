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

// The brush sizes (diamonds; the sprays' radius too): 0.5 (half a diamond: one corner), 1 to 16, then every other to 32.
const std::vector<float>& brushSizes() {
    static const std::vector<float> v = [] {
        std::vector<float> o{0.5f};
        for (int i = 1; i <= 16; ++i) o.push_back(float(i));
        for (int i = 18; i <= 32; i += 2) o.push_back(float(i));
        return o;
    }();
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
enum StripNumber { kNumSize, kNumDensity, kNumHeight, kNumShape, kNumTurns };

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
static const wchar_t* const kStripShapeNames[3] = {L"Diamond", L"Square", L"Circle"};

// --- Rules of the strip (no drawing) ------------------------------------------------------------------------------------
bool objectSprayOn() {
    const AppState& app = appState();
    return (app.mode == Mode::Units && currentTool() == USpray) || (app.mode == Mode::Doodads && currentTool() == DSpray) ||
           (app.mode == Mode::Sprites && currentTool() == SSpray);
}
// The brush size: Terrain, and the sprays of Units, Doodads and Sprites (their radius).
bool stripHasSize() { return appState().mode == Mode::Terrain || objectSprayOn(); }
// Density: the sprays (Terrain, Units, Doodads, Sprites).
bool stripHasDensity() { return (appState().mode == Mode::Terrain && currentTool() == TSpray) || objectSprayOn(); }
// The mirrors: the modes whose edits repeat with them (Rectangle too: its tiles at the mirror images; sprites as units do).
bool stripHasMirrors() {
    const AppState& app = appState();
    return app.mode == Mode::Terrain || app.mode == Mode::Units || app.mode == Mode::Doodads || app.mode == Mode::Sprites;
}

// The Terrain tool strip's Isometric | Rectangle: what the terrain tools work on (ISOM terrains, or tiles as they are).
// Each map's own: its file keeps it (a new map starts in Isometric).
// The third choice, Custom: Rectangle's tools on the map, and the panel shows the user's custom terrain (core/stamps.inc)
// in place of the Tiles grid (TilesView::stamps). 0 Isometric, 1 Rectangle, 2 Custom.
int terrainKind() { return !appState().terrainRect ? 0 : gTiles->stamps ? 2 : 1; }
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
    if (!rect && !app.terrainSelMask.empty()) app.hasTerrainSel = false, app.terrainSelMask.clear();  // (Isometric has no such selection)
    if (app.terrainRect != rect) {
        app.terrainRect = rect;
        std::swap(app.brushSize, app.otherBrushSize), std::swap(app.brushHeight, app.otherBrushHeight);  // (each its own size)
    }
    app.terrainPasting = false;
    stampsPanelSync();
    if (shellViewShown()) shellRelayout();  // the strip's height (no mirrors in Rectangle)
    fillToolCombo(app);  // tilesSync: the Tiles grid or the terrain palette
    updateStatus();
    setMessage(kind == 2 ? L"Custom: the tools place your custom terrain. Select custom terrain in the list."
               : rect    ? L"Rectangle: the tools place tiles as they are. Select a tile in the Tiles list."
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
    else std::swprintf(t, 24, L"%d", app.sprayDensity);
    return t;
}
void stripNumberChanged() {
    shellToolsChanged();
    appState().dirty = true;
}
// (Select has a shape of its own and makes its selection by a drag.)
bool stripSelectTool() { return appState().mode == Mode::Terrain && currentTool() == TSelect; }
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
    } else {
        app.sprayDensity = std::clamp(app.sprayDensity + dir, 1, kDensityMax);
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
    if (text.empty() || !end || end == text.c_str() || *end || !std::isfinite(v)) {
        setMessage(L"Not a number: " + text);
        return false;
    }
    if (which == kNumSize) {
        app.brushSize = v < 1 ? 0.5f : float(std::clamp(std::round(v), 1.0, 32.0));
    } else if (which == kNumHeight) {
        app.brushHeight = float(std::clamp(std::round(v), 1.0, 32.0));  // a value of its own (the link turns off)
    } else if (which == kNumTurns) {
        if (app.ed) app.ed->setTurns(int(std::lround(std::clamp(v, -1000.0, 1000.0))));
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
        app.terrainPasting = false;
        fillToolCombo(app);  // the strip's rows (size, density) follow the tool
        if (app.palette < 0 && app.mode != Mode::Terrain) setMessage(L"Select what to place in the list");
        updateStatus();
    }
    shellToolsChanged();
    tilesSync();
    shellFocusView();
}
// The keys V, B, A, F: the active mode's tool of that key (the Map workspace only).
void stripToolByKey(int key) {
    if (gWorkspace != Workspace::Map) return;
    for (const StripItem& it : stripTools(appState().mode))
        if (stripToolKey(it) == key) return pickStripTool(it);
}

int toolStripHeight(int width, bool titled) {
    const AppState& app = appState();
    const int b = scale(30), gap = scale(4);
    const int perRow = std::max(1, (width + gap) / (b + gap));
    const int tools = int(stripTools(app.mode).size());
    const int rows = (tools + perRow - 1) / perRow;  // (Terrain: the brush's shape is in its size field)
    const int extra = (stripHasSize() ? scale(26) + gap : 0) + (stripHasMirrors() ? scale(28) + gap + scale(26) + gap : 0) + (stripHasDensity() ? scale(26) + gap : 0) +
                      (app.mode == Mode::Terrain ? scale(26) + gap : 0);  // Terrain: Isometric | Rectangle
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
// A glyph of the Segoe MDL2 face, centered, at `size` (96-dpi pixels).
void glyph(Canvas& c, Rect r, wchar_t g, Color col, int size) {
    const draw::Font prev = c.setFont(c.font({draw::Face::Glyphs, scale(size)}));
    c.text(r, std::wstring_view(&g, 1), col, draw::kMiddle | draw::kCenter);
    c.setFont(prev);
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
void iconButtonFace(Canvas& c, Rect r, UiIcon g, bool on, bool over) {
    r = square(r);
    if (on) box(c, r, kSel, kAccentLine);
    else if (over) box(c, r, kCtlHot, kNoLine);
    icon(c, r, g, on ? kAccentBright : over ? kText : kDim, 16);  // (a UI icon is always at 16, as the Win32 glyph was)
}
// A mirror's picture: a square and its axis. Smooth, in the button's color (the square's line at 110 of 255).
void mirrorIcon(Canvas& c, Rect r, int kind, Color col, Color /*ground*/) {
    using P = Canvas::PointF;
    const float s = float(scale(14)), x0 = (r.left + r.right - s) / 2.0f, y0 = (r.top + r.bottom - s) / 2.0f;
    const float aw = float(std::max(1, scale(2))) * 0.75f;  // the axis's width
    c.smoothFrame(P{x0, y0}, s, s, col, 1.0f, 110);
    switch (kind) {
    case 0: break;  // none: the square only
    case Mirror::LeftRight: c.smoothLine(P{x0 + s / 2, y0 - 1}, P{x0 + s / 2, y0 + s + 1}, col, aw); break;
    case Mirror::TopBottom: c.smoothLine(P{x0 - 1, y0 + s / 2}, P{x0 + s + 1, y0 + s / 2}, col, aw); break;
    case Mirror::Diagonal: c.smoothLine(P{x0, y0}, P{x0 + s, y0 + s}, col, aw); break;
    case Mirror::AntiDiagonal: c.smoothLine(P{x0 + s, y0}, P{x0, y0 + s}, col, aw); break;
    default: break;
    }
}
}  // namespace stripLook

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
    // Tests: the typed text, then Enter.
    void typeForTest(const std::wstring& t) {
        field_.setText(t);
        if (field_.onEnter) field_.onEnter();
    }

    void paint(draw::Canvas& c) override {
        using namespace stripLook;
        PanelScope scope(state);
        AppState& app = appState();
        const draw::Font prevFont = c.setFont(c.font({draw::Face::Ui, 0}));  // the UI font
        const int W = width(), H = height();
        c.fill({0, 0, W, H}, kBg);
        hits_.clear();
        numbers_.clear();
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
        // Terrain: Isometric, Rectangle or Custom, a segmented choice over the tools.
        if (app.mode == Mode::Terrain) {
            const int sh = scale(26), sg = scale(4), third = (W - 2 * sg) / 3;
            static const struct {
                const wchar_t* name;
                const wchar_t* tip;
            } segs[3] = {{L"Isometric", L"Isometric: paint terrains, with the edges corrected. Ctrl+Alt+I selects the next choice."},
                         {L"Rectangle", L"Rectangle: place tiles as they are, with no terrain correction. Ctrl+Alt+I selects the next choice."},
                         {L"Custom", L"Custom: place your custom terrain, tiles that you saved from a map. Ctrl+Alt+I selects the next choice."}};
            const int kind = terrainKind();
            for (int k = 0; k < 3; ++k) {
                const draw::Rect r{k * (third + sg), y, k == 2 ? W : k * (third + sg) + third, y + sh};
                buttonFace(c, r, segs[k].name, kind == k, hot(r));
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
            iconButtonFace(c, r, it.icon, on, hot(square(r)));
            addHit(square(r), [it] { pickStripTool(it); },
                   key >= 0 ? std::wstring(tr(it.tip)) + L" (" + wchar_t(L"VBAF"[key]) + L")" : std::wstring(it.tip));
            next();
        }
        if (x > 0) x = 0, y += b + gap;
        // The brush size and the spray's density: number boxes (the name, the value, up and down arrows).
        // A stepper: - and + at the ends of the box, the value between them (a click there types one).
        auto stepper = [&](int which, const draw::Rect& vb, const wchar_t* tip, int sw) {
            const bool editing = editing_ == which;
            box(c, vb, kCtl, editing ? kAccent : kBorder);
            const draw::Rect down{vb.left, vb.top, vb.left + sw, vb.bottom}, up{vb.right - sw, vb.top, vb.right, vb.bottom},
                value{down.right, vb.top, up.left, vb.bottom};
            if (!editing) c.text(value, stripNumberText(which), kText, draw::kCenter | draw::kMiddle);
            for (const bool plus : {false, true}) {
                const draw::Rect k = plus ? up : down;
                const bool over = hot(k);
                if (over) box(c, {k.left + scale(3), k.top + scale(3), k.right - scale(3), k.bottom - scale(3)}, kCtlHot, kNoLine, 4);
                glyph(c, k, plus ? L'\xE710' : L'\xE738', over ? kText : kDim, 9);  // + and -
            }
            addHitC(value, [this, which, value] { beginEdit(which, value); }, tip, draw::Cursor::IBeam);
            addHit(up, [which] { stripNumberStep(which, 1); }, L"One step more");
            addHit(down, [which] { stripNumberStep(which, -1); }, L"One step less");
            numbers_.push_back({vb, which});
        };
        auto numberRow = [&](int which, const wchar_t* name, const wchar_t* tip, const wchar_t* unit = nullptr) {
            const int h = scale(26), lw = scale(64);
            c.text({0, y, lw, y + h}, name, kDim, draw::kMiddle);
            const draw::Rect vb{lw, y + scale(1), std::min(W, lw + scale(120)), y + h - scale(1)};
            stepper(which, vb, tip, scale(26));
            if (unit && vb.right + scale(6) < W) {  // what the number counts (GitHub feedback #121)
                c.text({vb.right + scale(6), y, W, y + h}, unit, kDim, draw::kMiddle | draw::kEllipsis);
            }
            y += h + gap;
        };
        static const wchar_t* const kSizeTip = L"Brush size: 1 to 32. Click to type a size. - and + and the mouse wheel change it (Alt+wheel on the map too).";
        static const wchar_t* const kTurnsTip =
            L"Turns: the copies go round the map's middle in equal steps. 1: none, 2: a half turn, 4: quarter turns, 1 to 8. Click to type a number. - and + and the mouse wheel change it. On isometric terrain, a copy goes to the nearest diamond unless the turn keeps the diamonds (2, and 4 on a map twice as wide as high).";
        static const wchar_t* const kHeightTip =
            L"Brush height: 1 to 32. Click to type a height (empty: the same as the size). The arrows and the mouse wheel change it.";
        if (stripHasSize() && app.mode == Mode::Terrain) {
            // Terrain: the brush in one field the width of the strip (GitHub #113):
            //   [ shape v | width w ^v | height h ^v | link ]
            // The shape opens a list; each number has a spin at its right, takes the mouse wheel and a click types it. The
            // link keeps the height the same as the width (the height then shows in the dim color).
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
            glyph(c, {sr.left + scale(24), sr.top, sr.right - scale(4), sr.bottom}, L'\xE70D', kDim, 7);
            addHit(sr, [this, sr] {
                std::vector<std::wstring> names;
                for (const wchar_t* n : kStripShapeNames) names.push_back(std::wstring(tr(n)));
                const int pick = shellPopupPick(names, int(shapeOf(appState())), sr.left, sr.bottom + scale(2));
                if (pick >= 0) {
                    shapeOf(appState()) = Brush::Shape(pick);
                    shellSettingsChanged();
                    shellToolsChanged();
                }
                shellFocusView();
            }, L"Brush shape: diamond, square or circle. Click to select one; the mouse wheel changes it.");
            numbers_.push_back({sr, kNumShape});
            divider(sr.right);
            // The link.
            const draw::Rect lk{boxR.right - linkW, boxR.top, boxR.right, boxR.bottom};
            hotFill(lk);
            glyph(c, lk, L'\xE71B', linked ? kAccent : kFaint, 10);
            addHit(lk, [] {
                AppState& a = appState();
                a.brushHeight = a.brushHeight <= 0 ? brushHeightOf(a) : 0;  // as it is now (a typed height may just have been set)
                stripNumberChanged();
                shellFocusView();
            }, linked ? L"The height is the same as the size. Click to give the height a value of its own."
                        : L"The height has a value of its own. Click to make it the same as the size.");
            divider(lk.left);
            // The width and the height, a divider between them.
            const int mx = (sr.right + lk.left) / 2;
            auto part = [&](int which, int left, int right, const wchar_t* suffix, const wchar_t* tip) {
                const int mid = (boxR.top + boxR.bottom) / 2;
                const draw::Rect value{left, boxR.top, right - spin, boxR.bottom}, up{right - spin, boxR.top + pad, right - scale(2), mid},
                    down{right - spin, mid, right - scale(2), boxR.bottom - pad};
                if (editing_ != which) {
                    // The number and a small suffix after it (w: width, h: height), centered together.
                    const std::wstring t = stripNumberText(which);
                    const int nw = c.measure(t);
                    const draw::Font of = c.setFont(c.font({draw::Face::Ui, smallPx()}));
                    const int sw = c.measure(suffix);
                    c.setFont(of);
                    const int gx = (value.left + value.right - nw - scale(2) - sw) / 2;
                    c.text({gx, value.top, gx + nw, value.bottom}, t, which == kNumHeight && linked ? kDim : kText, draw::kMiddle | draw::kLeft);
                    const draw::Font of2 = c.setFont(c.font({draw::Face::Ui, smallPx()}));
                    const int sx = gx + nw + scale(2);  // near the numbers' baseline
                    c.text({sx, value.top + scale(2), sx + sw, value.bottom}, suffix, kFaint, draw::kMiddle | draw::kLeft);
                    c.setFont(of2);
                }
                for (const bool more : {true, false}) {
                    const draw::Rect k = more ? up : down;
                    const bool over = hot(k);
                    if (over) box(c, k, kCtlHot, kNoLine, 2);
                    glyph(c, k, more ? L'\xE70E' : L'\xE70D', over ? kText : kDim, 6);  // chevrons up and down
                }
                addHitC(value, [this, which, value] { beginEdit(which, value); }, tip, draw::Cursor::IBeam);
                addHit(up, [which] { stripNumberStep(which, 1); }, L"One step more");
                addHit(down, [which] { stripNumberStep(which, -1); }, L"One step less");
                numbers_.push_back({{left, boxR.top, right, boxR.bottom}, which});
            };
            part(kNumSize, sr.right + 1, mx, L"w", kSizeTip);
            divider(mx);
            part(kNumHeight, mx + 1, lk.left, L"h", kHeightTip);
            y += h + gap;
        } else if (stripHasSize()) {
            numberRow(kNumSize, L"Size", kSizeTip);
        }
        if (stripHasDensity())
            numberRow(kNumDensity, L"Density",
                      app.mode == Mode::Terrain ? L"Density: the number of dabs (Rectangle: tiles) each spray puts down, 1 to 50. Click to type a number. - and + and the mouse wheel change it."
                      : app.mode == Mode::Units ? L"Density: the number of units each spray puts down (where they fit), 1 to 50. Click to type a number. - and + and the mouse wheel change it."
                                                : L"Density: the number of doodads each spray puts down (where they fit), 1 to 50. Click to type a number. - and + and the mouse wheel change it.");
        // The mirrors: they combine (left/right and top/bottom together mirror both ways); None turns them all off.
        if (stripHasMirrors() && app.ed) {
            static const struct {
                int flag;
                const wchar_t* tip;
            } kinds[] = {{0, L"No symmetry"},
                         {Mirror::LeftRight, L"Mirror left and right"},
                         {Mirror::TopBottom, L"Mirror top and bottom"},
                         {Mirror::Diagonal, L"Mirror across the diagonal"},
                         {Mirror::AntiDiagonal, L"Mirror across the other diagonal"}};
            const int mb = scale(28);
            int mx = 0;
            for (const auto& k : kinds) {
                const int mask = app.ed->mirrors();
                const bool on = k.flag ? (mask & k.flag) != 0 : mask == 0 && app.ed->turns() == 1;
                const draw::Rect br{mx, y, mx + mb, y + mb};
                const int flag = k.flag;
                const bool over = hot(square(br));
                if (on) box(c, square(br), kSel, kAccentLine);
                else if (over) box(c, square(br), kCtlHot, kNoLine);
                mirrorIcon(c, square(br), flag, on ? kAccentBright : over ? kText : kDim, on ? kSel : over ? kCtlHot : kBg);
                addHit(square(br), [flag] {
                    AppState& a = appState();
                    if (!a.ed) return;
                    const int m2 = flag ? a.ed->mirrors() ^ flag : 0;
                    a.ed->setMirrors(m2);
                    if (!flag) a.ed->setTurns(1);  // None turns the turns off too
                    shellToolsChanged();
                    a.dirty = true;
                    shellFocusView();
                }, k.tip);
                mx += mb + scale(3);
            }
            // Turns: on the same row, after the mirrors (its name too where it fits); the row below when the strip is
            // too narrow.
            const int name = c.measure(tr(L"Turns")) + scale(8), minStep = scale(84), left = mx + scale(6);
            if (W - left >= minStep) {
                const bool named = W - left - name >= minStep;
                if (named) c.text({left, y, left + name, y + mb}, tr(L"Turns"), kDim, draw::kMiddle);
                const int sx = named ? left + name : left;
                stepper(kNumTurns, {sx, y + scale(1), std::min(W, sx + scale(120)), y + mb - scale(1)}, kTurnsTip, scale(26));
                y += mb + gap;
            } else {
                y += mb + gap;
                numberRow(kNumTurns, L"Turns", kTurnsTip);
            }
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
            if (e.button != 0 || pressed_ < 0) return false;
            const int k = pressed_;
            pressed_ = -1;
            if (draw::Host* h = host()) h->capture(*this, false);
            if (indexAt(e.pt) == k && hits_[size_t(k)].click) hits_[size_t(k)].click();
            return true;
        }
        case draw::MouseKind::Wheel: {  // over a number box: one step
            if (e.wheel == 0) return false;
            for (const auto& n : numbers_)
                if (n.first.contains(e.pt)) {
                    endEdit(true);
                    stripNumberStep(n.second, e.wheel > 0 ? 1 : -1);
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
    int pressed_ = -1;   // the hit the mouse is down on
    draw::Point hover_;  // the pointer, as last moved
    bool hovering_ = false;
};

ToolStripView gToolStripView;  // the strip (its window shows it: main.cpp, sdl_main_view.inc)
ToolStripView* gStripInUse = &gToolStripView;
// The side bar's other strips (sdl_main_view.inc): the other side's mode's tools, and the tools of the list that shows
// in the active side while the active mode is in a window of its own.
ToolStripView gToolStripView2, gToolStripView3;
