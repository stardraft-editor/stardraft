// Options > Program options and Options > Report feedback on this system: the views are the core's (core/views_options.inc,
// core/views_feedback.inc); this file is their SDL host. Each opens as a dialog window over the root (the kit's windows, as
// the Windows program's dialogs). The options keep their values in the settings file (prefs, the same names as the Windows
// program's), the volume and the unit names in the dock, the unit limits in the core. The theme and the accent change at
// once (the color tokens, core/colors.inc); the language applies at the next start (Restart now starts the program
// again); the StarCraft folder is shellChooseInstallFolder's (shell_sdl.inc). Send files the feedback through
// core/feedback_send.inc. The Maps' program is Windows only (shell_sdl_win.inc); Check now, Look for updates on start
// and the crash reports' choice are sdl_updates.inc's. Included by main_sdl.cpp after sdl_new_page.inc.

// The Discord server's invitation (the Windows program's about.inc has the same link).

bool gOptionsOpen = false;        // the options window is open (its closed callback clears it)
fs::path gInstallInUse, gInstallNext;  // the StarCraft folder in use; the one for the next start (Options > StarCraft folder)
FeedbackView* gFbView = nullptr;  // the feedback dialog while it is open
unsigned gFbSerial = 0;           // counts the dialog's opens: a send's result goes to the dialog that sent it
std::string gSelfPath;            // the program's file as main() got it (argv[0]): Restart now starts it again
class OptionsView;
extern OptionsView gOptionsView;  // (below the program's part)

// --- The language -----------------------------------------------------------------------------------------------------

// The translator of the canvas and the windows' titles (kit_sdl::setTextTranslator): tr's tables.
bool sdlTranslate(std::wstring_view text, std::wstring& out) {
    const std::wstring en(text);
    const wchar_t* t = tr(en.c_str());
    if (t == en.c_str() || en == t) return false;
    out = t;
    return true;
}

// The language of the system (its first preferred locale), or English when the program has no translation for it.
UiLang sdlSystemLanguage() {
    int n = 0;
    UiLang lang = UiLang::English;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&n)) {
        if (n > 0 && locales[0] && locales[0]->language) {
            const std::string l = locales[0]->language, c = locales[0]->country ? locales[0]->country : "";
            if (l == "ko") lang = UiLang::Korean;
            else if (l == "es") lang = UiLang::Spanish;
            else if (l == "ru") lang = UiLang::Russian;
            else if (l == "zh" && (c.empty() || c == "CN" || c == "SG")) lang = UiLang::Chinese;  // simplified; traditional: English
        }
        SDL_free(locales);
    }
    return lang;
}

// The language to use: STARDRAFT_LANG (en, ko, es, ru or zh, for test runs) wins, then the setting, and automatic is the
// language of the system.
UiLang sdlLanguageChosen(const std::wstring& setting) {
    std::wstring code = setting;
    if (const char* e = std::getenv("STARDRAFT_LANG"); e && *e) code = widen(e);
    return uiLanguageOfCode(code, sdlSystemLanguage());
}

// At the start, before the first frame: the language of the setting Language (`given`: --lang, not saved; it is
// STARDRAFT_LANG for this run).
void loadLanguageSdl(const std::string& given) {
    prefs::text(L"Language", gLanguage);
    if (!given.empty()) {
#ifdef _WIN32
        _putenv_s("STARDRAFT_LANG", given.c_str());
#else
        setenv("STARDRAFT_LANG", given.c_str(), 1);
#endif
    }
    gUiLang = sdlLanguageChosen(gLanguage);
    kit_sdl::setTextTranslator(gUiLang == UiLang::English ? nullptr : sdlTranslate);
}

// --- The theme ----------------------------------------------------------------------------------------------------------

// The theme and the accent: every token, the kit's theme, and every window painted again. `save`: kept in the settings
// (a snapshot's --theme is not).
// With Automatic (macOS and Linux, sdl_window.inc: gThemeAuto, gAccentAuto) the system gives the theme or the accent, and the
// choice that is saved stays the last one the user picked.
void applyThemeSdl(bool light, int accent, bool save) {
    sdlSetLook(light, accent);
    syncKitThemeTokens(int(gDpi));
    if (save) {
        if (!gThemeAuto) prefs::setNumber(L"LightTheme", light ? 1 : 0);
        if (!gAccentAuto) prefs::setNumber(L"Accent", gAccentIndex);
        if constexpr (kSystemLook) prefs::setNumber(L"SystemTheme", gThemeAuto ? 1 : 0), prefs::setNumber(L"SystemAccent", gAccentAuto ? 1 : 0);
    }
    if (gRoot) gRoot->relayout(), gRoot->invalidate(), gRoot->map.invalidate();
    gOptionsView.invalidate();
    if (gFbView) gFbView->invalidate();
}
// At the start: the theme in the settings (`given`: --theme light, dark or system, and --accent (-2: system), for a
// snapshot). On macOS and Linux the system's theme and accent color are the default (not in a test run: a test sees the same
// colors on each computer, unless it asks for the system's).
void loadThemeSdl(const std::string& given, int givenAccent) {
    int64_t light = 0, accent = 0, sysTheme = kSystemLook && !gSilent ? 1 : 0, sysAccent = sysTheme;
    prefs::number(L"LightTheme", light), prefs::number(L"Accent", accent);
    if constexpr (kSystemLook)
        if (!gSilent) prefs::number(L"SystemTheme", sysTheme), prefs::number(L"SystemAccent", sysAccent);
    if (given == "light" || given == "dark") light = given == "light", sysTheme = 0;
    if (given == "system") sysTheme = kSystemLook ? 1 : 0;
    if (givenAccent >= 0) accent = givenAccent, sysAccent = 0;
    if (givenAccent == -2) sysAccent = kSystemLook ? 1 : 0;
    gThemeAuto = sysTheme != 0, gAccentAuto = sysAccent != 0;
    sdlSetLook(light != 0, int(accent));
    syncKitThemeTokens(int(gDpi));
}
// The message of a theme change: the theme and the accent color.
std::wstring sdlThemeMessage() {
    return std::wstring(gLightTheme ? L"Light" : L"Dark") + L" theme" + (gThemeAuto ? L" (the system's)" : L"") + L", accent color " +
           theme::kAccents[gAccentIndex].name + (gAccentAuto ? L" (the system's)" : L"");
}

// --- Restart now ----------------------------------------------------------------------------------------------------

// Program options > Language > Restart now: the open map asks to be saved (as on quit), then the program starts again
// with `--restart <this process>` and this one ends. The maps and projects that were open go to the setting Reopen
// (core/documents.inc: restartWrite), as the Win32 program's restart writes them: the new program opens them again.
std::uintptr_t sdlStartProgram(const fs::path& exe, const fs::path& dir, const std::wstring& args, const std::wstring& what,
                               std::wstring& error);  // (shell_sdl_win.inc)
// The maps and projects that are open (an unsaved project and an untitled map have no file to open again).
std::vector<std::wstring> sdlOpenFiles() {
    std::vector<std::wstring> files;
    for (int i = 0; i < int(gDocs.size()); ++i) {
        const Doc& d = gDocs[size_t(i)];
        if (d.page) continue;
        const Editor* ed = i == gActive ? gState.ed.get() : d.ed.get();
        if (d.project && !isUnsavedProject(d.project.get())) files.push_back(d.project->folder().wstring());
        else if (!d.project && ed && ed->isOpen() && !ed->path().empty()) files.push_back(ed->path().wstring());
    }
    return files;
}
void restartProgramSdl() {
    confirmReplace([] {
        std::error_code ec;
        fs::path self = stardraft::platform::programFile();  // the program's own file
        if (self.empty()) self = fs::absolute(fs::path(gSelfPath), ec);
#if defined(STARDRAFT_XDG)
        if (const fs::path image = stardraft::xdg::appImageFile(); !image.empty()) self = image;  // (the AppImage, not its mounted program)
#endif
        restartWrite(sdlOpenFiles());
        std::wstring error;
        const std::uintptr_t p = sdlStartProgram(self, fs::current_path(ec), L"--restart " + std::to_wstring(shellProcessId()), L"StarDraft 2", error);
        if (!p) {
            prefs::remove(L"Reopen");
            setMessage(L"StarDraft 2 could not start again. Close it and start it to show the new language.");
            return;
        }
        shellProcessClose(p);  // (the new program goes on; this one ends)
        quitNow();
    });
}

// --- The options' commands ----------------------------------------------------------------------------------------------

void showOptionsSdl();
// The Program options commands this system runs (the menus and the keys ask sdlRuns).
bool sdlOptionsRuns(int id) {
    return id == ID_THEME_DARK || id == ID_THEME_LIGHT || (id == ID_THEME_SYSTEM && kSystemLook) || (id >= ID_OPT_LANG && id < ID_OPT_LANG + kLanguageChoiceCount) || id == ID_OPT_RESTART ||
           id == ID_MIDDLE_MODE || id == ID_MIDDLE_MODE + 1 || id == ID_OPT_LASTPROJECT;
    // (Look for updates on start and Crash reports are the update check's and the crash reports' commands, with them.)
}
// Runs one of them (the same settings and messages as the Windows program's); false: not one of them.
bool sdlOptionsCommand(int id) {
    if (!sdlOptionsRuns(id)) return false;
    bool flag = false;
    switch (id) {
    case ID_THEME_DARK:
    case ID_THEME_LIGHT:
    case ID_THEME_SYSTEM:
        gThemeAuto = id == ID_THEME_SYSTEM;
        applyThemeSdl(id == ID_THEME_LIGHT, gAccentIndex, true);
        setMessage(sdlThemeMessage());
        break;
    case ID_OPT_RESTART: restartProgramSdl(); break;
    case ID_MIDDLE_MODE:
    case ID_MIDDLE_MODE + 1:
        prefs::setNumber(L"MiddleDrag", id == ID_MIDDLE_MODE + 1 ? 1 : 0);
        setMessage(id == ID_MIDDLE_MODE + 1 ? L"Middle mouse click and hold drags the map" : L"Middle mouse click and hold pans the map");
        break;
    case ID_OPT_LASTPROJECT:
        prefs::flag(L"OpenLastProject", flag);
        prefs::setNumber(L"OpenLastProject", flag ? 0 : 1);
        setMessage(!flag ? L"The last project opens when StarDraft 2 starts" : L"StarDraft 2 starts with no project open");
        break;
    default:  // Language: it applies at the next start (the texts are made then): restart now, or later
        gLanguage = kLanguageChoices[id - ID_OPT_LANG].code;
        prefs::setText(L"Language", gLanguage);
        if (sdlLanguageChosen(gLanguage) == gUiLang) break;  // the language in use
        gOptionsView.invalidate();  // its Restart now button
        // (the core message box, as the Windows program's MB_YESNO | MB_ICONQUESTION: Yes has the focus)
        if (gRoot && !gNoBoxes &&
            msgbox::ask(*gRoot, shellTr(L"Language"), shellTr(L"StarDraft 2 shows the new language after a restart. Restart now? Your open maps and projects open again."),
                        {shellTr(L"&Yes"), shellTr(L"&No")}, msgbox::Icon::Question) == 0)
            restartProgramSdl();
        else setMessage(L"The language changes when StarDraft 2 starts again");
        break;
    }
    gOptionsView.invalidate();
    return true;
}

bool sdlMapsOpenWithUs();  // (shell_sdl_win.inc)

// The program's part of the options window: the values, the commands, the folder and the sliders' changes.
class OptionsProgramSdl final : public OptionsHost {
public:
    void values(OptionsValues& v) override {
        bool flag = false;
        v.light = gLightTheme, v.accent = gAccentIndex;
        for (int i = 0; i < theme::kAccentCount; ++i) v.accents.push_back({theme::kAccents[i].name, theme::swatch(i)});
        if constexpr (kSystemLook) {  // (macOS and Linux: Automatic, the last swatch, is the default)
            v.accents.push_back({L"Automatic (the color of the system)", sdlSystemAccentSwatch()});
            v.defaultAccent = theme::kAccentCount;
            if (gAccentAuto) v.accent = theme::kAccentCount;
            v.systemTheme = true, v.themeAuto = gThemeAuto;
        }
        v.animate = gState.animate;
        prefs::flag(L"MiddleDrag", flag), v.middleDrag = flag;
        v.volume = gDock.volume;
        v.languages = languageChoicesOffered(L"Automatic (the language of the system)"), v.language = languageChoice();
        v.restartDue = sdlLanguageChosen(gLanguage) != gUiLang;  // another language than the one in use
        v.unitNames = gDock.unitNames;
        v.unitLimit[0] = gUnitLimitClassic, v.unitLimit[1] = gUnitLimitEud;
        flag = false;
        prefs::flag(L"OpenLastProject", flag), v.openLast = flag;
        flag = true;
        prefs::flag(L"CheckUpdates", flag), v.checkUpdates = flag;
        int crash = 0;
        prefs::number(L"CrashReports", crash, 0, 2), v.crashReports = crash;
        v.installInUse = gInstallInUse.empty() ? std::wstring() : gInstallInUse.wstring();
        v.installNext = !gInstallNext.empty() && gInstallNext != gInstallInUse ? gInstallNext.wstring() : std::wstring();
        v.mapsWithSD2 = sdlMapsOpenWithUs();  // (Windows: .scm and .scx open with StarDraft 2; shell_sdl_win.inc)
        v.projectsFolder = defaultProjectsFolder().make_preferred().wstring();
#ifndef _WIN32
        v.mapsRow = false;  // (the file association is Windows only)
#endif
    }
    void command(int id) override { ::command(id); }  // (the menu's command: the same path as the menu)
    void setAccent(int i) override {
        gAccentAuto = kSystemLook && i == theme::kAccentCount;  // (the last swatch: Automatic)
        applyThemeSdl(gLightTheme, gAccentAuto ? gAccentIndex : i, true);
        setMessage(sdlThemeMessage());
    }
    void setUnitNames(int i) override;
    void setUnitLimit(int k, int value) override;
    void setVolume(int value) override;
    void browseWorkingFolder() override;
    void useDefaultWorkingFolder() override;
    void themeShown(bool) override {}  // (the dialog's frame is the kit's: it paints with the tokens)
    void close() override;
};
OptionsProgramSdl gOptionsProgram;
OptionsView gOptionsView(gOptionsProgram);

void OptionsProgramSdl::close() {
    if (gOptionsOpen && gHost) gHost->closeWindow(gOptionsView);
}

void OptionsProgramSdl::setUnitNames(int i) {
    gDock.unitNames = i;
    saveDock();
    gOptionsView.invalidate();
}
void OptionsProgramSdl::setUnitLimit(int k, int value) {
    int& limit = k == 0 ? gUnitLimitClassic : gUnitLimitEud;
    limit = std::clamp(value, 1, 65535);
    prefs::setNumber(k == 0 ? L"UnitLimitClassic" : L"UnitLimitEud", limit);
    gOptionsView.invalidate();
}
void OptionsProgramSdl::setVolume(int value) {
    if (value == gDock.volume) return;
    gDock.volume = std::clamp(value, 0, 100);
    saveDock();
    gOptionsView.invalidate();
}
// Browse: the folder new projects go to (the system's folder dialog), as on Windows.
void OptionsProgramSdl::browseWorkingFolder() {
    const std::wstring f = shellPickFolder(shellTr(L"Working folder"));
    if (f.empty()) return;
    gProjectsFolder = fs::path(f).make_preferred().wstring();
    saveRecent();
    setMessage(shellTr(L"New projects go to ") + gProjectsFolder);
    gOptionsView.invalidate();
}
void OptionsProgramSdl::useDefaultWorkingFolder() {
    gProjectsFolder.clear();
    saveRecent();
    setMessage(shellTr(L"New projects go to ") + defaultProjectsFolder().make_preferred().wstring());
    gOptionsView.invalidate();
}

#ifdef __APPLE__
// macOS: Program options is a Settings window (mac_dock.h): a toolbar with the tabs (core/views_options.inc:
// optionsview::kTabs), the tab's name as the title, the window as high as the tab's content. The tab stays chosen for
// the next open.
std::vector<MacSettingsTab> sdlSettingsTabs();  // (sdl_dock.inc)
int gSettingsTab = 0;
bool gSettingsSized = false;  // the window has its first height (then a tab change animates the height)
void* sdlNsWindow(draw::View& v) {
    SDL_Window* w = gHost ? gHost->sdlWindow(v) : nullptr;
    return w ? SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr) : nullptr;
}
void sdlSettingsHeight(int px) {
    SDL_Window* w = gOptionsOpen && gHost ? gHost->sdlWindow(gOptionsView) : nullptr;
    if (!w) return;
    const float density = SDL_GetWindowPixelDensity(w);
    macSettingsContentHeight(sdlNsWindow(gOptionsView), double(px) / double(density > 0 ? density : 1), gSettingsSized);
    gSettingsSized = true;
}
void sdlSettingsShowTab(int i) {
    gSettingsTab = i;
    gOptionsView.setTab(i);
    if (SDL_Window* w = gHost ? gHost->sdlWindow(gOptionsView) : nullptr) SDL_SetWindowTitle(w, narrow(shellTr(optionsview::kTabs[i].name)).c_str());
}
#endif

// Options > Program options (one window; a second Ctrl+, does nothing while it is open). On macOS a Settings window
// (a second Cmd+, brings it to the front).
void showOptionsSdl() {
#ifdef __APPLE__
    if (SDL_Window* w = gOptionsOpen && gHost ? gHost->sdlWindow(gOptionsView) : nullptr) SDL_RaiseWindow(w);
#endif
    if (!gRoot || gOptionsOpen) return;
    int h = std::min(scale(820), std::max(scale(300), gRoot->height() - scale(80)));  // as high as the window has room for
    const wchar_t* title = L"Program options";
#ifdef __APPLE__
    gOptionsView.setTab(gSettingsTab);
    gSettingsSized = false;
    gOptionsView.onTabHeight = [](int px) { shellPost([px] { sdlSettingsHeight(px); }); };  // (after the paint)
    h = scale(400), title = optionsview::kTabs[gSettingsTab].name;  // (the first paint gives the tab's height)
#endif
    gOptionsView.resetScroll();
    gOptionsOpen = true;
    gHost->openWindow(*gRoot, gOptionsView, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(720), h), title},
                      [] { gOptionsOpen = false; });
#ifdef __APPLE__
    macSettingsToolbar(sdlNsWindow(gOptionsView), sdlSettingsTabs(), gSettingsTab, [](int i) { shellPost([i] { sdlSettingsShowTab(i); }); });
#endif
}

// Help > About StarDraft (core/views_about.inc; one window: a second click does nothing while it is open).
AboutView gAboutView;
bool gAboutOpen = false;
void showAboutSdl() {
    if (!gRoot || gAboutOpen) return;
    gAboutView.hot = -1;
    gAboutOpen = true;
    gAboutView.closed = [] { gAboutOpen = false; };
    const draw::Point sz = AboutView::size();
    gHost->openWindow(*gRoot, gAboutView, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, sz.x, sz.y), L"About StarDraft 2"},
                      [] { gAboutOpen = false; });
}

// View > Control kit (core/views_kit_gallery.inc; one window). The page scrolls with the mouse wheel; the entries' icons
// are the Data pictures (sdl_data.inc), and the first add row's multi-pick is the list picker (core/views_list_pick.inc).
draw::Image sdlDataImage(DataPicture p);  // (sdl_data.inc)
KitGalleryView gKitView;
bool gKitOpen = false;
void showKitGallerySdl() {
    if (!gRoot || gKitOpen) return;
    gKitOpen = true;
    gKitView.scroll = 0;
    gKitView.refIcon = [](const char* dat, int id) { return sdlDataImage(dataRefPicture(dat, uint32_t(id))); };
    gKitView.pickValues = [] {
        ListPickView v(L"Search units", kitGalleryPickItems(), "");
        v.setMulti(kitGalleryPicked(), L"Add");
        if (!runListPick(v, {scale(40), scale(120)})) return false;
        kitGalleryPicked() = v.checked();
        return true;
    };
    const int h = std::min(scale(1000), std::max(scale(300), gRoot->height() - scale(80)));
    gHost->openWindow(*gRoot, gKitView, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(1180), h), L"Control kit"}, [] { gKitOpen = false; });
}

// --- Report feedback ----------------------------------------------------------------------------------------------------

// The system and its version for the feedback's file ("Linux 6.6.87", "macOS 15.1", "Windows 10.0.26200"), as the
// Windows program writes it.
std::string sdlSystemName() {
#ifdef _WIN32
    return stardraft::platform::systemName();  // (apps/common/crash_win32.cpp: the crash reports' too)
#else
    std::string name = SDL_GetPlatform();
#ifndef __APPLE__
    std::ifstream in("/proc/sys/kernel/osrelease");
    std::string release;
    if (std::getline(in, release) && !release.empty()) name += " " + release;
#endif
    return name;
#endif
}

// The feedback dialog's part of the program: Send writes the file and sends it (core/feedback_send.inc), the links.
class FeedbackProgramSdl final : public FeedbackHost {
public:
    void send(FeedbackView& v, const std::wstring& typed) override {
        const fs::path file = feedbackWriteFile(v.on(), typed, reportWorkspaceName(gWorkspace), narrow(shellBuildStamp()), sdlSystemName());
        const FeedbackView* sentFrom = &v;  // the dialog that sent it (and its open): the result goes only there
        const unsigned serial = gFbSerial;
        feedbackSend(v, file, [sentFrom, serial]() -> FeedbackView* { return gFbView && gFbView == sentFrom && gFbSerial == serial ? gFbView : nullptr; });
    }
    void openDiscord() override { shellOpenUrl(kDiscordInvite); }
    void openIssue(const std::wstring& page) override { SDL_OpenURL(narrow(page).c_str()); }
    void close() override {
        if (gFbView && gHost) gHost->closeWindow(*gFbView);
    }
    int timer(int ms, std::function<void()> run) override {
        draw::Host* h = gFbView ? gFbView->host() : nullptr;
        return h ? h->timer(ms, std::move(run)) : -1;
    }
    void cancelTimer(int id) override {
        draw::Host* h = gFbView ? gFbView->host() : nullptr;
        if (h && id >= 0) h->cancelTimer(id);
    }
};
FeedbackProgramSdl gFeedbackProgram;

// Options > Report feedback: a modal dialog over the window, about `on` (the menu's name, or "StarDraft 2"). owner:
// the root view of the window it opens over (a dialog or a pop-out that a right click was in); null: the main window.
void showFeedbackSdl(const std::wstring& on, draw::View* owner) {
    if (!gRoot || gFbView) return;
    FeedbackView view(gFeedbackProgram, on);
    gFbView = &view;
    ++gFbSerial;
    gHost->runModal(owner && owner->host() ? *owner : *gRoot, view,
                    draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(592), scale(370)), L"Report feedback"});
    gFbView = nullptr;
}
void showFeedbackSdl(const std::wstring& on) { showFeedbackSdl(on, nullptr); }
