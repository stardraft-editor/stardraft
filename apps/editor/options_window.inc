// Options > Program options (Ctrl+,): the options of the program in one window, a view (core/views_options.inc: the rows
// are kit widgets). This file is its Windows host (included by main.cpp after about.inc): the values the view shows, the
// commands it runs, the folders and the sliders' and fields' changes, and the window itself, opened through the
// anchor's Host::openWindow (the Dialog frame of the program's other windows). A second Ctrl+, brings it to the front.

// The program's anchor for the windows its views open (the main window owns them; see Win32Host::ownerWindow).
struct MainAnchor {
    stardraft::draw::View view;
    ui::Win32Host host{view};
};
MainAnchor gMainAnchor;

extern OptionsView gOptionsView;  // (defined below the program)

// The program's part of the window: the values, the commands, the pickers' work.
class OptionsProgram final : public OptionsHost {
public:
    void values(OptionsValues& v) override {
        v.light = gLightTheme, v.accent = gAccentIndex;
        for (int i = 0; i < theme::kAccentCount; ++i) v.accents.push_back({theme::kAccents[i].name, theme::swatch(i)});
        v.animate = app.animate, v.middleDrag = gMiddleDrag, v.volume = gDock.volume;
        v.languages = languageChoicesOffered(), v.language = languageChoice();
        v.restartDue = uiLanguageChosen(gLanguage) != gUiLang;  // another language than the one in use
        v.unitNames = gDock.unitNames;
        v.unitLimit[0] = gUnitLimitClassic, v.unitLimit[1] = gUnitLimitEud;
        v.openLast = gOpenLastProject, v.checkUpdates = gCheckUpdates, v.crashReports = gCrashReports;
        v.mapsWithSD2 = mapsOpenWithStarDraft();
        v.projectsFolder = defaultProjectsFolder().make_preferred().wstring();
        v.installInUse = gInstallInUse.empty() ? std::wstring() : gInstallInUse.wstring();
        v.installNext = !gInstallNext.empty() && gInstallNext != gInstallInUse ? gInstallNext.wstring() : std::wstring();
    }
    void command(int id) override { ::command(id); }
    void setAccent(int i) override { applyTheme(gLightTheme, i), shellMenusChanged(); }
    void setUnitNames(int i) override {
        gDock.unitNames = i, saveDock();
        rebuildUnitPalette();
        RedrawWindow(app.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void setUnitLimit(int k, int v) override {
        int& limit = k == 0 ? gUnitLimitClassic : gUnitLimitEud;
        limit = std::clamp(v, 1, 65535);
        if (!gSelfTest) prefs::setNumber(k == 0 ? L"UnitLimitClassic" : L"UnitLimitEud", limit);
        optionsRefresh();
    }
    void setVolume(int v) override {
        if (v == gDock.volume) return;
        gDock.volume = std::clamp(v, 0, 100), saveDock();
    }
    void browseWorkingFolder() override {
        const std::wstring f = pickFolderTitled(L"Working folder");
        if (f.empty()) return;
        gProjectsFolder = fs::path(f).make_preferred().wstring();
        saveRecent();
        setMessage(L"New projects go to " + gProjectsFolder);
        optionsRefresh();
    }
    void useDefaultWorkingFolder() override {
        gProjectsFolder.clear();
        saveRecent();
        setMessage(L"New projects go to " + defaultProjectsFolder().make_preferred().wstring());
        optionsRefresh();
    }
    void themeShown(bool light) override {
        if (HWND w = ui::Win32Host::windowOf(gOptionsView)) {
            BOOL dark = !light;
            DwmSetWindowAttribute(w, 20, &dark, sizeof dark), titleColors(w);
        }
    }
    void close() override {
        if (ui::Win32Host::windowOf(gOptionsView)) gMainAnchor.host.closeWindow(gOptionsView);
    }
};

OptionsProgram gOptionsProgram;
OptionsView gOptionsView(gOptionsProgram);

// The options window's HWND (nullptr: it is not open).
HWND optionsWindow() { return ui::Win32Host::windowOf(gOptionsView); }

void optionsRefresh() { gOptionsView.invalidate(); }

// Options > Program options (one window; a second Ctrl+, brings it to the front).
void showOptions() {
    if (HWND w = optionsWindow()) {
        if (!gSelfTest && !gBackground) SetForegroundWindow(w);
        return;
    }
    // A dialog frame (a tool window in test runs); as high as the options need, or as the screen has room for.
    RECT mr{0, 0, 1280, 900};
    if (app.main) GetWindowRect(app.main, &mr);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromRect(&mr, MONITOR_DEFAULTTONEAREST), &mi);
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    const stardraft::draw::WindowSpec spec{stardraft::draw::WindowKind::Dialog,
                                           stardraft::draw::Rect{0, 0, scale(720), std::min(scale(820), workH - scale(80))}, L"Program options"};
    gOptionsView.resetScroll();
    gMainAnchor.host.ownerWindow(app.main);
    gMainAnchor.host.openWindow(gMainAnchor.view, gOptionsView, spec, [] { gOptionsView.resetScroll(); });
    if (HWND w = optionsWindow()) darkControl(w, L"DarkMode_Explorer");  // the scroll bar in the theme
}
