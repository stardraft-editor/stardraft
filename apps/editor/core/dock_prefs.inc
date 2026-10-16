// The dock layout in the settings (prefs: Layout/...): the panels' sides and widths, the minimap, the log's height, the
// map grid, the trigger pane's zoom, the volume. Portable: included by core.inc and by main.cpp after recent.inc. Each
// program loads it once at its start; a change saves it (not in the self-test).

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
