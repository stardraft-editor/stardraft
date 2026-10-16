// Help > Check for updates, the options Look for updates on start and Crash reports, and the quiet check at start, on
// this system (the Windows program's update.inc and its commands in main.cpp). The release, the download and the texts
// are the core's (core/releases.inc); this file asks the user with the core's message box (msgbox::ask). A release build on Windows
// puts the new stardraft.exe in its own place and starts it; a build that cannot (made here, or a file with another
// name, such as stardraft-testing.exe, or macOS and Linux for now) opens the release page.
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

bool sdlUpdatesWithPackage();  // (sdl_xdg.inc)
std::vector<std::wstring> sdlOpenFiles();  // (sdl_options.inc)
std::wstring sdlInstallRelease(const fs::path& self, const fs::path& fresh, const std::vector<std::string>& more = {});

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
    const releases::UpdateWay way = releases::updateWay(self, r, sdlUpdatesWithPackage());
    // A release build that cannot put the new program in its place (macOS: the app is in a disk image): the browser
    // gets this system's download.
    if (way == releases::UpdateWay::Download) {
        if (updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" + shellTr(L"Download it? The download opens in the browser."),
                       {shellTr(L"&Download"), shellTr(L"Cancel")}, msgbox::Icon::Info) == 0)
            SDL_OpenURL(r.download.c_str());
        return;
    }
    // A build made here, or a package (an AppImage, a Flatpak: sdl_xdg.inc) that updates as packages do: the release
    // page, to get it by hand.
    if (way == releases::UpdateWay::Page) {
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
    // The open map is asked about first (Save, Do not save, Cancel); then the new program takes this one's place and
    // starts, with the maps and projects that are open (as Restart now). A new program that does not start goes again.
    confirmReplace([r, self, fresh, dir] {
        if (const std::wstring failed = sdlInstallRelease(self, fresh); !failed.empty()) return updateFailed(r, failed, dir);
        std::error_code e;
        fs::remove_all(dir, e);
        quitNow();
    });
}

// The new program in place of this one, started with the maps and projects that are open: empty when it runs, else
// the reason (the old program is in its place again: releases::installRelease).
std::wstring sdlInstallRelease(const fs::path& self, const fs::path& fresh, const std::vector<std::string>& more) {
    const std::vector<std::wstring> open = sdlOpenFiles();
    if (!open.empty()) restartWrite(open);
    std::vector<std::string> args = releases::restartArguments(shellProcessId());
    args.insert(args.end(), more.begin(), more.end());
    const std::wstring failed = releases::installRelease(self, fresh, [&args](const fs::path& p) { return stardraft::platform::startProgram(p, args); });
    if (!failed.empty() && !open.empty()) prefs::remove(L"Reopen");
    return failed;
}

// --update-test <zip> [the new program's options]: Check for updates' install with a release's zip on this computer
// in place of GitHub's download, for a test (no window, no network): the zip is unpacked into a new temporary folder,
// its program takes this program's place and starts with --restart and the options after the zip. This program
// prints what happened and ends: 0 when the new program runs, 1 when not (then this program is in its place again).
int sdlUpdateTest(const fs::path& zip, const std::vector<std::string>& more) {
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path(ec) / ("StarDraft-update-test-" + std::to_string(stamp));
    std::wstring why;
    const fs::path fresh = releases::unpackRelease(zip, dir, why);
    const fs::path self = stardraft::platform::programFile();
    if (fresh.empty()) {
        std::printf("update: not unpacked: %s\n", narrow(why).c_str());
        fs::remove_all(dir, ec);
        return 1;
    }
    const std::wstring failed = sdlInstallRelease(self, fresh, more);
    fs::remove_all(dir, ec);
    if (!failed.empty()) {
        std::printf("update: not installed: %s\n", narrow(failed).c_str());
        return 1;
    }
    std::printf("update: the new program runs (%s)\n", narrow(self.filename().wstring()).c_str());
    return 0;
}

// At start (also in a test run): the program file of an earlier update goes. The new program of an update starts with
// --restart, so the old one has ended now.
void sdlRemoveOldProgram() {
    if (const fs::path self = stardraft::platform::programFile(); !self.empty()) releases::removeOldProgram(self);
}

// After the window shows (not in a test run): a release build looks for a newer release on a thread (Program options >
// Look for updates on start, on by default) and says so in the status bar.
void sdlUpdatesAtStart() {
    sdlRemoveOldProgram();  // (again: the old program of an update from a release before --restart may have ended only now)
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

// --- What an update keeps (--check) -------------------------------------------------------------------------------------

std::string sdlCrashCheck();      // (shell_sdl_win.inc: the crash reports, on each system)
std::string sdlFileTypesCheck();  // (shell_sdl_win.inc: the map files and project.stardraft in Explorer)
std::filesystem::path sdlWindowsInstallFile();

// --check settings: the settings as this run read them (with --settings <file>: a file of an older release), one line
// each; update: the program file of an update before is gone; crashes: the crash reports and what the next start
// does with them; filetypes: the files that open with StarDraft 2. False: not one of these checks.
bool sdlUpdateCheck(const std::string& what, std::string& out) {
    auto line = [&out](const std::string& s) { out += s + "\n"; };
    auto yes = [](bool b) { return std::string(b ? "yes" : "no"); };
    if (what == "settings") {
        line("settings: file " + narrow(shellSettingsFile().wstring()));
        line("settings: language " + narrow(gLanguage));
        line("settings: theme " + std::string(gLightTheme ? "light" : "dark") + ", accent " + narrow(theme::kAccents[gAccentIndex].name));
        for (const AppSetting& a : appSettings()) line("settings: " + narrow(a.name) + " " + std::to_string(a.get()));
        std::string sides;
        for (int m = 0; m < kModeCount; ++m) sides += (m ? ", " : "") + narrow(kModeNames[m]) + (gDock.toolsRight[m] ? " right" : " left");
        line("settings: layout minimap " + std::string(gDock.minimapRight ? "right" : "left") + (gDock.minimapHidden ? " hidden" : "") + "; " + sides);
        line("settings: layout widths " + std::to_string(gDock.leftW) + " " + std::to_string(gDock.rightW) + " " + std::to_string(gDock.dataListW) + ", log " +
             std::to_string(gDock.logH) + ", grid " + std::to_string(gDock.gridW) + "x" + std::to_string(gDock.gridH) + "+" + std::to_string(gDock.gridX) +
             "+" + std::to_string(gDock.gridY) + ", trigger zoom " + std::to_string(gDock.triggerZoom) + ", volume " + std::to_string(gDock.volume) +
             ", unit names " + std::to_string(gDock.unitNames) + ", code pane " + yes(gDock.triggerCode) + ", script preview " + yes(gDock.scriptPreview));
        for (const auto& m : gRecentMaps) line("settings: recent map " + narrow(m));
        for (const auto& p : gRecentProjects) line("settings: recent project " + narrow(p));
        line("settings: working folder " + narrow(defaultProjectsFolder().wstring()));
        line("settings: unit limits " + std::to_string(gUnitLimitClassic) + " " + std::to_string(gUnitLimitEud));
        bool last = false, updates = true, middle = false, asked = false;
        int crashes = 0;
        prefs::flag(L"OpenLastProject", last), prefs::flag(L"CheckUpdates", updates), prefs::flag(L"MiddleDrag", middle);
        prefs::flag(L"AskedMapsDefault", asked), prefs::number(L"CrashReports", crashes, 0, 2);
        line("settings: open the last project " + yes(last) + ", look for updates " + yes(updates) + ", crash reports " + std::to_string(crashes) +
             ", middle button drags " + yes(middle) + ", asked about maps " + yes(asked));
        std::wstring folder;
        prefs::text(L"StarCraftFolder", folder);
        std::string installTxt;
        if (const fs::path f = sdlWindowsInstallFile(); !f.empty())
            if (std::ifstream in(f); in) std::getline(in, installTxt);
        line("settings: StarCraft folder " + narrow(folder) + " (install.txt " + installTxt + ")");
        for (const std::wstring& name : prefs::names(L"DataPins")) {
            std::wstring v;
            prefs::text(L"DataPins/" + name, v);
            line("settings: data pins " + narrow(name) + " " + narrow(v));
        }
        // The projects that are not saved yet (core/documents.inc): the folder of a program that ended goes at the start.
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(shellUnsavedRoot(), ec)) {
            const auto pid = std::uint32_t(std::strtoul(e.path().filename().string().c_str(), nullptr, 10));
            const bool runs = pid == shellProcessId() || shellProcessRunning(pid);
            line("settings: unsaved project folder " + e.path().filename().string() + (runs ? " (its program runs)" : " (goes)"));
        }
        return true;
    }
    if (what == "update") {
        std::error_code ec;
        const fs::path self = stardraft::platform::programFile();
        const bool gone = !fs::exists(stardraft::platform::oldProgramFile(self), ec);
        std::vector<std::wstring> reopen;
        line("update: program " + narrow(self.filename().wstring()) + ", the program file of the update before is " + (gone ? "gone (yes)" : "still there (WRONG)") +
             ", the list of files to open again is " + (prefs::list(L"Reopen", reopen) ? "still there (WRONG)" : "gone (yes)"));
        return true;
    }
    if (what.rfind("release:", 0) == 0) {  // release:<file>: Check for updates with that file as GitHub's answer
        std::ifstream in{fs::path(widen(what.substr(8))), std::ios::binary};
        const std::string j((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        releases::Release r;
        if (!in || !releases::parseRelease(j, r)) return line("release: no release in " + what.substr(8) + " (WRONG)"), true;
        line("release: \"" + r.title + "\", commit " + r.commit + ", page " + r.page);
        line("release: this system's download (" + std::string(stardraft::platform::releaseAssetSuffix()) + ") " + (r.download.empty() ? "none" : r.download));
        const releases::UpdateWay way = releases::updateWay(stardraft::platform::programFile(), r, sdlUpdatesWithPackage());
        line("release: newer " + yes(releases::newer(r)) + ", a release build " + yes(releases::releaseBuild()) + ", Check for updates: " +
             (way == releases::UpdateWay::Install ? "installs it" : way == releases::UpdateWay::Download ? "opens " + r.download : "opens " + r.page));
        return true;
    }
    if (what == "crashes") return out = sdlCrashCheck(), true;
    if (what == "filetypes") return out = sdlFileTypesCheck(), true;
    return false;
}
