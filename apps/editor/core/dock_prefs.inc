// The dock layout in the settings (prefs: Layout/...): the panels' sides and widths, the minimap, the log's height, the
// map grid, the trigger pane's zoom, the volume. Portable: included by core.inc and by main.cpp after recent.inc. Each
// program loads it once at its start; a change saves it (not in the self-test).

// The floating terrain palette (Roadmap R40, as ScmDraft 2's terrain window): the Terrain mode's palette (the brush's
// terrains, Rectangle mode's tiles) in a window of its own, or in the side bar (docked). Its place and size are SDL's
// window coordinates (points; on Windows pixels) on the screens; w 0: no place yet (the program gives one).
struct FloatPalette {
    bool floating = false;
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const FloatPalette& o) const { return floating == o.floating && x == o.x && y == o.y && w == o.w && h == o.h; }
};
FloatPalette gFloatPal;
constexpr int kFloatPalMinW = 160, kFloatPalMinH = 200;  // (the window's smallest size, in points)

// The settings' names (Layout/...).
FloatPalette floatPaletteLoad(const stardraft::Settings& s) {
    FloatPalette p;
    auto get = [&](const char* name, int lo, int hi, int def) {
        const auto n = s.number(std::string("Layout/") + name);
        return n ? int(std::clamp<int64_t>(*n, lo, hi)) : def;
    };
    p.floating = get("PaletteFloating", 0, 1, 0) != 0;
    p.x = get("PaletteX", -100000, 100000, 0), p.y = get("PaletteY", -100000, 100000, 0);
    p.w = get("PaletteWidth", 0, 16384, 0), p.h = get("PaletteHeight", 0, 16384, 0);
    if (p.w < kFloatPalMinW || p.h < kFloatPalMinH) p.w = p.h = 0;  // (no size kept: the program's)
    return p;
}
void floatPaletteSave(stardraft::Settings& s, const FloatPalette& p) {
    s.set("Layout/PaletteFloating", int64_t(p.floating ? 1 : 0));
    if (p.w <= 0 || p.h <= 0) return;
    s.set("Layout/PaletteX", int64_t(p.x)), s.set("Layout/PaletteY", int64_t(p.y));
    s.set("Layout/PaletteWidth", int64_t(p.w)), s.set("Layout/PaletteHeight", int64_t(p.h));
}

// Whether the kept place is still on a screen (`screens`: their usable areas, in the same coordinates): the window's
// top band (its title bar, where the user moves it from) must be on one screen for at least 64 x 16 points. Else the
// program gives the palette a new place (a screen was removed or its size changed).
bool floatPaletteOnScreens(const FloatPalette& p, const std::vector<RECT>& screens) {
    if (p.w <= 0 || p.h <= 0) return false;
    const RECT band{p.x, p.y - 32, p.x + p.w, p.y + 32};  // (the title bar is over the client area on most systems)
    for (const RECT& r : screens) {
        const LONG l = std::max(band.left, r.left), t = std::max(band.top, r.top), rr = std::min(band.right, r.right), b = std::min(band.bottom, r.bottom);
        if (rr - l >= std::min<LONG>(64, p.w) && b - t >= 16) return true;
    }
    return false;
}

void loadDock() {
    auto get = [](const wchar_t* name, bool& v) { prefs::flag(std::wstring(L"Layout/") + name, v); };
    get(L"MinimapRight", gDock.minimapRight);
    get(L"MinimapHidden", gDock.minimapHidden);
    for (int i = 0; i < kModeCount; ++i) get(kDockSideNames[i], gDock.toolsRight[i]);
    auto number = [](const wchar_t* name, int& v, int lo, int hi) { prefs::number(std::wstring(L"Layout/") + name, v, lo, hi); };
    number(L"LeftWidth", gDock.leftW, kMinSideW, kMaxSideW), number(L"RightWidth", gDock.rightW, kMinSideW, kMaxSideW);
    number(L"DataListWidth", gDock.dataListW, kMinSideW, kMaxSideW);
    get(L"TriggerCode", gDock.triggerCode);
    number(L"UnitNames", gDock.unitNames, 0, 3);
    number(L"Volume", gDock.volume, 0, 100);
    get(L"ScriptPreview", gDock.scriptPreview);
    number(L"LogHeight", gDock.logH, 80, 1200);
    number(L"GridWidth", gDock.gridW, 1, 8192), number(L"GridHeight", gDock.gridH, 1, 8192);
    number(L"GridX", gDock.gridX, 0, 8191), number(L"GridY", gDock.gridY, 0, 8191);
    number(L"TriggerZoom", gDock.triggerZoom, 70, 200);
    gFloatPal = floatPaletteLoad(prefs::store());
}

void saveDock() {
    if (gSelfTest || gSettingsOff) return;  // the self-test and the screenshot runs move panels without touching the user's layout
    auto put = [](const wchar_t* name, int v) { prefs::setNumber(std::wstring(L"Layout/") + name, v); };
    put(L"MinimapRight", gDock.minimapRight);
    put(L"MinimapHidden", gDock.minimapHidden);
    put(L"UnitNames", gDock.unitNames);
    put(L"Volume", gDock.volume);
    for (int i = 0; i < kModeCount; ++i) put(kDockSideNames[i], gDock.toolsRight[i]);
    put(L"LeftWidth", gDock.leftW), put(L"RightWidth", gDock.rightW), put(L"DataListWidth", gDock.dataListW);
    put(L"LogHeight", gDock.logH);
    put(L"GridWidth", gDock.gridW), put(L"GridHeight", gDock.gridH), put(L"GridX", gDock.gridX), put(L"GridY", gDock.gridY);
    put(L"TriggerCode", gDock.triggerCode);
    put(L"ScriptPreview", gDock.scriptPreview);
    put(L"TriggerZoom", gDock.triggerZoom);
    floatPaletteSave(prefs::store(), gFloatPal);
}

// The trigger pane's zoom back to 100% (Ctrl+0): the pane is drawn again (trigPaneRepaint: the Win32 program's trigger
// pane, main.cpp; the SDL program has no trigger pane yet).
void trigPaneRepaint();
void setMessage(const std::wstring& m);
void trigZoomReset() {
    gDock.triggerZoom = 100, saveDock();
    trigPaneRepaint();
    setMessage(L"Zoom 100%");
}
