// The main window's layout (research/ports/MAC-BUILD-PLAN.md step 3): where each pane goes and whether it is shown, as
// a pure function of the window's size, the dock state and the sizes the host measured. It makes no window calls, so it
// compiles without Win32 (core.inc). The Win32 host gathers the input and applies the result (main.cpp, layout()) with
// the same MoveWindow and ShowWindow calls, in the same order, as the layout it replaces.

// A pane's place in the main window's client area (device pixels), and its state.
struct PaneSlot {
    RECT rc{};              // where the pane goes: left, top, right, bottom (its size is right - left by bottom - top)
    bool placed = false;    // the host moves it to rc (false: it keeps its place)
    bool visible = false;   // shown (a hidden pane is still placed where the layout says)
};

// Everything the layout depends on. The host measures what needs the window system (the heights of the status bar,
// the Log and the top bar, the tool strip for a width); the rest is the editor's state, copied in.
struct LayoutInput {
    int dpi = 96;
    int clientW = 0, clientH = 0;  // the main window's client size
    int statusH = 0;               // the status bar's height (0: no status bar)
    int logH = 0;                  // the Log's height
    int barH = 0, tabsH = 0;       // the top tool bar's and the document tabs' heights
    int railW = 0;                 // the workspace rail's width
    int modeBarH = 0;              // the mode bar's band (0: the modes are in the top bar)
    RECT modeBar{};                // the mode bar's place (in the top bar)
    std::function<int(int)> toolStrip;  // the active mode's tool strip height at a width (the host measures it)
    std::function<int(int, Mode)> toolStripOf;  // another mode's tool strip height at a width (none: no strip for it)
    Mode mode = Mode::Units;       // the active mode
    bool mapWs = false;            // the Map workspace is shown
    bool toolsRight[kModeCount] = {false, true, false, false, false};  // the dock: each mode's tools side (true: right)
    bool minimapRight = false, minimapHidden = false;
    int leftW = 280, rightW = 280;                       // the side widths (96-dpi pixels)
    int minimapH = 0;                                    // the minimap's height (96-dpi pixels; 0: the map's shape)
    Mode lastOnSide[2] = {Mode::Terrain, Mode::Units};   // the mode last used on each side (left, right)
    bool mapOpen = false;          // a map is open
    int mapW = 0, mapH = 0;        // its size in tiles (mapW 0: no shape for the minimap)
    bool hasPanelTab[2] = {false, false};  // the side's tab strip exists (left, right)
    bool hasSectionBox = false;    // the palette's section box exists
    bool hasPal2 = false;          // the second palette exists (the other side's list)
    bool pal2Shown = false;        // ... and was shown before (for "changed")
    Mode pal2Mode = Mode::Units;   // ... and what it showed before
    bool hasPal3 = false;          // the third palette exists (the active side's list while the active mode floats)
    bool pal3Shown = false;        // ... and was shown before (for "changed")
    Mode pal3Mode = Mode::Terrain; // ... and what it showed before
    bool floats[kModeCount] = {};  // each mode's panel is in a window of its own (no tab, not the other side's list)
};

// The layout's result: every pane's place and state, and the state the layout sets (the sides' tabs, the splitters,
// the dock columns, the second palette's mode).
struct MainLayout {
    PaneSlot status, log, topBar, tabs, wsRail, workspace, minimap, modeBar;
    PaneSlot panelTabActive, tool, owner, sectionBox, search, viewBtn, filter, palette, list;
    PaneSlot panelTabOther, palette2, palette3, view;
    PaneSlot tool2, tool3;  // the other side's mode's tools over its list, and the third list's mode's tools
    RECT splitters[3] = {};  // the left side's edge, the right side's, the minimap's bottom edge
    RECT dockRects[2] = {};
    Mode panelTab[2] = {Mode::Terrain, Mode::Units};  // the tab each side shows
    Mode secondMode = Mode::Units;                    // the mode of the other side's list
    bool secondShown = false;                         // the other side has a list (and a tab)
    bool palette2Changed = false;                     // its mode or its shown state changed (the list is rebuilt)
    Mode thirdMode = Mode::Terrain;                   // the active side's list while the active mode floats
    bool palette3Changed = false;                     // its mode or its shown state changed (the list is rebuilt)
};

// The scale of `v` (96-dpi pixels) at `dpi`: the same rounding as MulDiv (halves away from zero).
inline int layoutScale(int v, int dpi) {
    const long long p = static_cast<long long>(v) * dpi;
    const long long half = 48;  // 96 / 2
    return static_cast<int>(p >= 0 ? (p + half) / 96 : -((-p + half) / 96));
}

inline RECT layoutRect(int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; }

inline MainLayout computeLayout(const LayoutInput& in) {
    MainLayout L;
    const auto S = [&](int x, int y, int w, int h, bool placed, bool visible) {
        PaneSlot s;
        s.rc = layoutRect(x, y, w, h), s.placed = placed, s.visible = visible;
        return s;
    };
    const auto sc = [&](int v) { return layoutScale(v, in.dpi); };
    const int pad = sc(6), rowH = sc(24);
    const int rcR = in.clientW, rcB = in.clientH;

    // The bottom: the status bar, the Log above it. The workspace fills what is left under the top bar and the tabs.
    const int statusH = in.statusH, logH = in.logH;
    const int h = rcB - statusH - logH;
    L.status = S(0, rcB - statusH, rcR, statusH, true, true);
    L.log = S(0, h, rcR, logH, true, true);
    const int barH = in.barH, tabsH = in.tabsH, top = barH + tabsH, left = in.railW;
    L.topBar = S(0, 0, rcR, barH, true, true);
    L.tabs = S(0, barH, rcR, tabsH, true, true);
    L.wsRail = S(0, top, left, std::max(1, h - top), true, true);
    L.workspace = S(left, top, std::max(1, rcR - left), std::max(1, h - top), true, true);

    // The map workspace: the map between two side columns. Each mode's panel is docked left or right; panels on the
    // same side are tabs. The minimap tops its side. A side with nothing on it takes no room.
    const int mtop = top + in.modeBarH;
    const bool toolsRight = in.toolsRight[int(in.mode)], miniRight = in.minimapRight;
    const int otherSide = toolsRight ? 0 : 1;
    std::vector<Mode> others;
    for (Mode m : kModeOrder)
        if (in.toolsRight[int(m)] == (otherSide == 1) && !in.floats[int(m)]) others.push_back(m);
    Mode second = in.lastOnSide[otherSide];
    if (std::find(others.begin(), others.end(), second) == others.end()) second = others.empty() ? in.mode : others.front();
    const bool secondShown = !others.empty();
    // The active mode's panel in a window of its own: its side shows another of its tabs' lists, as the other side does
    // (the one last used there, else the first); a side with no tab left takes no room.
    const int activeSide = toolsRight ? 1 : 0;
    const bool floated = in.floats[int(in.mode)];
    std::vector<Mode> mine;
    for (Mode m : kModeOrder)
        if (in.toolsRight[int(m)] == toolsRight && !in.floats[int(m)]) mine.push_back(m);
    Mode third = in.lastOnSide[activeSide];
    if (std::find(mine.begin(), mine.end(), third) == mine.end()) third = mine.empty() ? in.mode : mine.front();
    const bool activeUsed = !floated || !mine.empty();
    L.panelTab[activeSide] = floated ? third : in.mode, L.panelTab[otherSide] = second;
    L.secondMode = second, L.secondShown = secondShown, L.thirdMode = third;
    const bool miniShown = !in.minimapHidden;
    const bool leftUsed = (miniShown && !miniRight) || (!toolsRight && activeUsed) || (secondShown && otherSide == 0),
               rightUsed = (miniShown && miniRight) || (toolsRight && activeUsed) || (secondShown && otherSide == 1);
    const int leftW = sc(in.leftW), rightW = sc(in.rightW);
    const int lx = left, rx = rcR - rightW;
    const int viewL = left + (leftUsed ? leftW : 0), viewR = rcR - (rightUsed ? rightW : 0);
    L.splitters[0] = leftUsed ? RECT{viewL - pad, mtop, viewL, h} : RECT{};
    L.splitters[1] = rightUsed ? RECT{viewR, mtop, viewR + pad, h} : RECT{};

    // The minimap spans its column, as tall as the map's shape asks (at most half the column).
    const int miniW = (miniRight ? rightW : leftW) - 2 * pad;
    int mini = miniW;
    if (in.mapOpen && in.mapW) mini = miniW * in.mapH / in.mapW;
    mini = std::clamp(mini, sc(40), std::max(sc(40), (h - mtop) / 2));
    // Its height as dragged (its bottom edge): from a strip to most of the side (the minimap letterboxes the map).
    if (in.minimapH > 0) mini = std::clamp(sc(in.minimapH), sc(40), std::max(sc(40), (h - mtop) * 3 / 4));
    const int miniX = miniRight ? rx : lx;
    if (!miniShown) mini = -pad;  // (no room for it: the panel starts at the top)
    L.minimap = S(miniX + pad, mtop + pad, miniW, mini, miniShown, miniShown && in.mapWs);
    // Its bottom edge: the gap under it, dragged up or down.
    L.splitters[2] = miniShown ? RECT{miniX + pad, mtop + pad + mini, miniX + pad + miniW, mtop + pad + mini + pad} : RECT{};
    L.modeBar = S(in.modeBar.left, in.modeBar.top, in.modeBar.right - in.modeBar.left, in.modeBar.bottom - in.modeBar.top, true, true);

    // The active mode's side: its tabs, then its tools (icons), its options, its list.
    const int belowMini = mtop + pad + mini + pad;
    const int tabsRowH = sc(30);
    const int sideW = toolsRight ? rightW : leftW, inner = sideW - 2 * pad;
    const int sx = toolsRight ? rx : lx;
    int y = toolsRight == miniRight ? belowMini : mtop + pad;
    const bool hasTabActive = in.hasPanelTab[toolsRight ? 1 : 0];
    L.panelTabActive = S(sx + pad, y, inner, tabsRowH, hasTabActive, in.mapWs && activeUsed);
    if (hasTabActive) y += tabsRowH + pad;
    const int stripH = in.toolStrip(inner);
    L.tool = S(sx + pad, y, inner, stripH, true, true);
    y += stripH + pad;
    // Each mode's own options: the owner row (Units and Sprites).
    const bool ownerRow = in.mode == Mode::Units || in.mode == Mode::Sprites;
    L.owner = S(sx + pad, y, inner, sc(300), ownerRow, true);
    if (ownerRow) y += rowH + pad;
    const bool sectionShown = in.hasSectionBox && (in.mode == Mode::Units || in.mode == Mode::Doodads || in.mode == Mode::Sprites);
    L.sectionBox = S(sx + pad, y, inner, sc(400), sectionShown, true);
    if (sectionShown) y += rowH + pad;
    const int btnW = sc(64);
    L.search = S(sx + pad, y, inner - pad - btnW, rowH, true, true);
    L.viewBtn = S(sx + sideW - pad - btnW, y, btnW, rowH, true, true);
    L.filter = S(sx + pad, y, inner, rowH, true, true);
    y += rowH + pad;
    L.palette = S(sx + pad, y, inner, std::max(1, h - y - pad), true, true);
    L.list = S(sx + pad, y, inner, std::max(1, h - y - pad), true, true);
    int ty = L.tool.rc.top;  // (the floated mode's tools are not here: the third list's mode's tools, then its list)
    const bool tools3 = floated && !mine.empty() && in.mapWs && in.toolStripOf;
    const int h3 = tools3 ? in.toolStripOf(inner, third) : 0;
    L.tool3 = S(sx + pad, ty, inner, std::max(1, h3), tools3, tools3);
    if (tools3) ty += h3 + pad;
    const bool show3 = floated && !mine.empty() && in.mapWs;  // (Locations: its locations, by name)
    L.palette3Changed = third != in.pal3Mode || show3 != in.pal3Shown;
    L.palette3 = S(sx + pad, ty, inner, std::max(1, h - ty - pad), in.hasPal3 && show3, show3);

    // The other side: its tabs, and the list of its tab's mode (a click there goes to that mode and picks).
    const int ox = otherSide ? rx : lx, ow = otherSide ? rightW : leftW;
    int oy = miniRight == (otherSide == 1) ? belowMini : mtop + pad;
    const bool hasTabOther = in.hasPanelTab[otherSide];
    const bool tabOtherPlaced = in.hasPal2 && hasTabOther && secondShown;
    L.panelTabOther = S(ox + pad, oy, ow - 2 * pad, tabsRowH, tabOtherPlaced, in.mapWs && secondShown);
    if (in.hasPal2 && hasTabOther && secondShown) oy += tabsRowH + pad;
    // The other side's mode's tools (each docked side shows its tools; a press on them makes that mode active).
    const bool tools2 = secondShown && in.mapWs && in.toolStripOf;
    const int h2 = tools2 ? in.toolStripOf(ow - 2 * pad, second) : 0;
    L.tool2 = S(ox + pad, oy, ow - 2 * pad, std::max(1, h2), tools2, tools2);
    if (tools2) oy += h2 + pad;
    const bool show = secondShown && in.mapWs;  // (Locations: its locations, by name)
    L.palette2Changed = second != in.pal2Mode || show != in.pal2Shown;
    L.palette2 = S(ox + pad, oy, ow - 2 * pad, std::max(1, h - oy - pad), in.hasPal2 && show, show);

    // The columns and the map.
    L.dockRects[0] = {lx, mtop, lx + (leftUsed ? leftW : 0), h};
    L.dockRects[1] = {rx, mtop, rightUsed ? rcR : rx, h};
    L.view = S(viewL, mtop, std::max(1, viewR - viewL), std::max(1, h - mtop), true, true);
    return L;
}
