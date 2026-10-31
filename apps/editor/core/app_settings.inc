// The options of the menus (View, Options, Edit; portable, core.inc; moved from main.cpp): kept in the settings file
// (Settings/<name>), so they stay after StarDraft starts again, also after an update (GitHub feedback #122). Saved after
// each command (only what changed), read at the start; tests use the defaults and save nothing. The palette's views of
// each mode are in the table too (PaletteViews: core/palette_view.inc). Both programs save the table (the Windows
// main.cpp: saveAppSettings; shell_sdl.inc: shellSettingsChanged).

int paletteViewBits();  // (core/palette_view.inc)
void setPaletteViewBits(int v);
int paletteSortBits();
void setPaletteSortBits(int v);

struct AppSetting {
    const wchar_t* name;
    int (*get)();
    void (*set)(int);
};
const std::vector<AppSetting>& appSettings() {
#define SD_SETTING(key, expr, type) {L"" key, [] { return int(expr); }, [](int v) { expr = type(v); }}
    static const std::vector<AppSetting> all = {
        SD_SETTING("Quality", shellState().quality, GraphicsQuality),
        SD_SETTING("Carbot", shellState().carbot, bool),
        SD_SETTING("ShowLocationsInAllModes", shellState().showLocations, bool),
        SD_SETTING("ShowGrid", shellState().showGrid, bool),
        SD_SETTING("Correction", shellState().correction, bool),
        SD_SETTING("AllowIllegalUnits", shellState().allowIllegalUnits, bool),
        SD_SETTING("AllowStacking", shellState().allowStacking, bool),
        SD_SETTING("ResourceDistance", shellState().resourceDistance, bool),
        SD_SETTING("SnapBuildings", shellState().snapBuildings, bool),
        SD_SETTING("DragSnapBack", shellState().dragSnapBack, bool),
        SD_SETTING("RandomFacing", shellState().randomFacing, bool),
        SD_SETTING("Animate", shellState().animate, bool),
        SD_SETTING("DoodadCheck", shellState().doodadCheck, bool),
        SD_SETTING("DoodadNearest", shellState().doodadNearest, bool),
        SD_SETTING("UnitGrid", shellState().unitGrid, int),
        SD_SETTING("SpriteGrid", shellState().spriteGrid, int),
        SD_SETTING("LocationGrid", shellState().locationGrid, int),
        SD_SETTING("LocationWrap", shellState().locationWrap, bool),
        SD_SETTING("CopyUnitsWithTerrain", shellState().copyUnitsWithTerrain, bool),
        SD_SETTING("CopyDoodadsWithTerrain", shellState().copyDoodadsWithTerrain, bool),
        SD_SETTING("BrushShape", shellState().brushShape, Brush::Shape),
        SD_SETTING("SelectShape", shellState().selectShape, Brush::Shape),
        // (the one switch before the ranges had kinds: on, all of them; RangeKindsShown, read after it, decides once it is saved)
        {L"ShowRanges", [] { return int(gRangeKinds != 0); }, [](int v) { if (v && !gRangeKinds) gRangeKinds = kRangeAll; }},
        SD_SETTING("RangeKindsShown", gRangeKinds, int),
        SD_SETTING("LayerTerrain", gLayers.terrain, bool),
        SD_SETTING("LayerDoodads", gLayers.doodads, bool),
        SD_SETTING("LayerSprites", gLayers.sprites, bool),
        SD_SETTING("LayerUnits", gLayers.units, bool),
        SD_SETTING("LayerRevealers", gLayers.revealers, bool),
        SD_SETTING("LayerCreep", gLayers.creep, bool),
        SD_SETTING("LayerProtected", gLayers.protectedTerrain, bool),
        {L"PaletteViews", [] { return paletteViewBits(); }, [](int v) { setPaletteViewBits(v); }},
        {L"PaletteSorts", [] { return paletteSortBits(); }, [](int v) { setPaletteSortBits(v); }},
    };
#undef SD_SETTING
    return all;
}
std::map<std::wstring, int> gSavedSettings;  // what the settings file has (written only when a value differs)
// (gSettingsOff, tests and screenshot runs: the defaults, and nothing saved, is in recent.inc.)

// One option: written when it differs from what the file has.
void putAppSetting(const wchar_t* name, int v) {
    auto it = gSavedSettings.find(name);
    if (it != gSavedSettings.end() && it->second == v) return;
    prefs::setNumber(std::wstring(L"Settings/") + name, v);
    gSavedSettings[name] = v;
}
// One option from the file; false: it has none.
bool getAppSetting(const wchar_t* name, int& v) {
    int64_t n = 0;
    if (!prefs::number(std::wstring(L"Settings/") + name, n)) return false;
    v = int(uint32_t(n));  // (a registry DWORD as it was read before: an int)
    gSavedSettings[name] = v;
    return true;
}
// The table's options: saved, or read. False: tests (nothing is saved or read).
bool saveSettingsTable() {
    if (gSelfTest || gSettingsOff) return false;
    for (const AppSetting& a : appSettings()) putAppSetting(a.name, a.get());
    return true;
}
bool loadSettingsTable() {
    if (gSelfTest || gSettingsOff) return false;
    for (const AppSetting& a : appSettings())
        if (int v = 0; getAppSetting(a.name, v)) a.set(v);
    return true;
}
