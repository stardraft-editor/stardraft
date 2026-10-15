// Options > Program options and Options > Report feedback on this system: the views are the core's (core/views_options.inc,
// core/views_feedback.inc); this file is their SDL host. Each opens as a dialog window over the root (the kit's windows, as
// the Windows program's dialogs). The options keep their values in the settings file (prefs), the volume and the unit names
// in the dock, the unit limits in the core. Not on this system yet: the themes and accents (dark only), the languages, the
// StarCraft folder and the Maps' program, the folder pickers, and sending the feedback (the text stays in the dialog).
// Included by main_sdl.cpp after sdl_new_page.inc.

// The Discord server's invitation (the Windows program's about.inc has the same link).
const wchar_t* const kSdlDiscordInvite = L"https://discord.gg/JBMCwSRJBm";

bool gOptionsOpen = false;        // the options window is open (its closed callback clears it)
fs::path gInstallInUse, gInstallNext;  // the StarCraft folder in use; the one for the next start (Options > StarCraft folder)
FeedbackView* gFbView = nullptr;  // the feedback dialog while it is open

// The program's part of the options window: the values, the commands, the folder and the sliders' changes.
class OptionsProgramSdl final : public OptionsHost {
public:
    void values(OptionsValues& v) override {
        bool flag = false;
        v.light = false;  // (dark only on this system)
        v.animate = gState.animate;
        prefs::flag(L"MiddleDrag", flag), v.middleDrag = flag;
        v.volume = gDock.volume;
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
        v.projectsFolder = defaultProjectsFolder().make_preferred().wstring();
    }
    void command(int id) override { ::command(id); }  // (the menu's command: the same path as the menu)
    void setAccent(int) override {}                    // (no accents on this system)
    void setUnitNames(int i) override;
    void setUnitLimit(int k, int value) override;
    void setVolume(int value) override;
    void browseWorkingFolder() override;
    void useDefaultWorkingFolder() override;
    void themeShown(bool) override {}
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

// Options > Program options (one window; a second Ctrl+, does nothing while it is open).
void showOptionsSdl() {
    if (!gRoot || gOptionsOpen) return;
    const int h = std::min(scale(820), std::max(scale(300), gRoot->height() - scale(80)));  // as high as the window has room for
    gOptionsView.resetScroll();
    gOptionsOpen = true;
    gHost->openWindow(*gRoot, gOptionsView, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(720), h), L"Program options"},
                      [] { gOptionsOpen = false; });
}

// The feedback dialog's part of the program: sending is not on this system yet (the text stays in the dialog), the links.
class FeedbackProgramSdl final : public FeedbackHost {
public:
    // (As the Windows build without a token does: the dialog is done, and the status says why.)
    void send(FeedbackView& v, const std::wstring&) override {
        v.sending(L"");
        v.done(false, 0, L"");
        v.showStatus(shellTr(L"Sending feedback is not on this system yet"));
    }
    void openDiscord() override { SDL_OpenURL(narrow(kSdlDiscordInvite).c_str()); }
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

// Options > Report feedback: a modal dialog over the window, about `on` (the menu's name, or "StarDraft 2").
void showFeedbackSdl(const std::wstring& on) {
    if (!gRoot || gFbView) return;
    FeedbackView view(gFeedbackProgram, on);
    gFbView = &view;
    gHost->runModal(*gRoot, view, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, scale(592), scale(370)), L"Report feedback"});
    gFbView = nullptr;
}
