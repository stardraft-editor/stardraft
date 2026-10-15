// Help > Check for updates, the options Look for updates on start and Crash reports, and the quiet check at start, on
// this system (the Windows program's update.inc and its commands in main.cpp). The release, the download and the texts
// are the core's (core/releases.inc); this file asks the user with the core's message box (msgbox::ask). A release build on Windows
// puts the new stardraft.exe in its own place and starts it; a build that cannot (made here, or a file with another
// name, such as stardraft-sdl.exe beside the Windows program, or macOS and Linux for now) opens the release page.
// The Windows parts (the crash handler, the map files, one program for all maps) are shell_sdl_win.inc's.
// Included by main_sdl.cpp after sdl_options.inc.

// Options > Crash reports (0 ask after a crash, 1 send, 2 do not send), read at start (the crash handler reads it).
int gSdlCrashReports = 0;

namespace {

// The release page opens in the browser.
void openReleasePage(const releases::Release& r) {
    if (!r.page.empty()) SDL_OpenURL(r.page.c_str());
}

// A message box over the window with the title Check for updates.
int updatesBox(const std::wstring& text, const std::vector<std::wstring>& buttons, msgbox::Icon icon) {
    return gRoot ? msgbox::ask(*gRoot, shellTr(L"Check for updates"), text, buttons, icon) : -1;
}

// The download did not work: why, then the release page.
void updateFailed(const releases::Release& r, const std::wstring& why, const fs::path& dir) {
    updatesBox(shellTr(why.c_str()) + L"\n\n" + shellTr(L"The release page opens: get the zip from there."), {L"OK"}, msgbox::Icon::Warning);
    openReleasePage(r);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace

// Help > Check for updates (and Program options > Check now).
void sdlCheckForUpdates() {
    shellWaitCursor();
    releases::Release r;
    if (!releases::latest(r)) {
        updatesBox(shellTr(L"Could not get the newest release from GitHub. Make sure that the computer is connected to the internet."), {L"OK"},
                   msgbox::Icon::Warning);
        return;
    }
    const std::wstring title = widen(r.title);
    if (!releases::newer(r)) {
        updatesBox(shellTr(L"StarDraft 2 is up to date (") + title + L").", {L"OK"}, msgbox::Icon::Info);
        return;
    }
    const fs::path self = stardraft::platform::programFile();
    if (!releases::installable(self, r)) {  // a build made here: the release page, to get it by hand
        if (updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" + shellTr(L"This is not a release build. Open the release page?"),
                       {shellTr(L"&Open the release page"), shellTr(L"Cancel")}, msgbox::Icon::Info) == 0)
            openReleasePage(r);
        return;
    }
    if (updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" + shellTr(L"Download and install it? StarDraft 2 starts again after it."),
                   {shellTr(L"&Install"), shellTr(L"Cancel")}, msgbox::Icon::Question) != 0)
        return;
    setMessage(L"Downloading " + title + L"\x2026");
    shellWaitCursor();
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();  // (a folder of this run's own)
    const fs::path dir = fs::temp_directory_path(ec) / ("StarDraft-update-" + std::to_string(stamp));
    std::wstring why;
    const fs::path fresh = releases::downloadRelease(r, dir, why);
    if (fresh.empty()) return updateFailed(r, why, dir);
    // The open map is asked about first (Save, Do not save, Cancel); then the new program takes this one's place.
    confirmReplace([r, self, fresh, dir] {
        if (const std::wstring failed = releases::placeRelease(self, fresh); !failed.empty()) return updateFailed(r, failed, dir);
        std::error_code e;
        fs::remove_all(dir, e);
        stardraft::platform::startProgram(self);
        quitNow();
    });
}

// At start: the program of an earlier update goes, and a release build looks for a newer release on a thread (Program
// options > Look for updates on start, on by default) and says so in the status bar.
void sdlUpdatesAtStart() {
    std::error_code ec;
    if (const fs::path self = stardraft::platform::programFile(); !self.empty()) fs::remove(stardraft::platform::oldProgramFile(self), ec);
    bool check = true;
    prefs::flag(L"CheckUpdates", check);
    if (!releases::releaseBuild() || !check) return;
    std::thread([] {
        releases::Release r;
        if (releases::latest(r) && releases::newer(r)) {
            const std::wstring title = widen(r.title);
            shellPost([title] { setMessage(L"A newer release is available: " + title + L". Use Help > Check for updates."); });
        }
    }).detach();
}

// The options' commands of this file that this system runs (the menus and the keys ask sdlRuns).
bool sdlUpdatesRuns(int id) {
#ifdef _WIN32
    if (id == ID_OPT_MAPSDEFAULT) return true;  // (the map files are Windows only)
#endif
    return id == ID_OPT_UPDATECHECK || (id >= ID_CRASH_MODE && id <= ID_CRASH_MODE + 2);
}

// The commands of this file (sdl_menus.inc's command() asks it): true when the command was one of them.
bool sdlMakeMapsDefault();  // (shell_sdl_win.inc on Windows; false elsewhere: the map files are Windows only)
bool sdlUpdatesCommand(int id) {
    switch (id) {
    case ID_HELP_UPDATES: sdlCheckForUpdates(); return true;
    case ID_OPT_UPDATECHECK: {
        bool on = true;
        prefs::flag(L"CheckUpdates", on);
        on = !on;
        if (!gSilent) prefs::setNumber(L"CheckUpdates", on ? 1 : 0);  // (tests never change the user's settings)
        setMessage(releases::updateCheckMessage(on));
        gOptionsView.invalidate();
        return true;
    }
    case ID_CRASH_MODE:
    case ID_CRASH_MODE + 1:
    case ID_CRASH_MODE + 2:
        gSdlCrashReports = id - ID_CRASH_MODE;
        if (!gSilent) prefs::setNumber(L"CrashReports", gSdlCrashReports);
        setMessage(releases::crashModeMessage(gSdlCrashReports));
        gOptionsView.invalidate();
        return true;
    case ID_OPT_MAPSDEFAULT: return sdlMakeMapsDefault();
    default: return false;
    }
}
