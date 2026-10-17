// Help > Check for updates, the options Look for updates, Previews and Crash reports, the quiet check at start and each
// hour (the top bar's Update), the release notes window (Help > What is new, core/views_changelog.inc), on
// this system (the Windows program's update.inc and its commands in main.cpp). The release, the download and the texts
// are the core's (core/releases.inc); this file asks the user with the core's message box (msgbox::ask). A release build on Windows
// puts the new stardraft.exe in its own place and starts it; a release build on macOS opens its disk image's download,
// an AppImage the new AppImage's download, and a Flatpak tells that its package manager updates it; a build that cannot
// (made here, or a file with another name, such as stardraft-testing.exe) opens the release page.
// The crash reports (the handler, the question, --check crashes) and the Windows parts (the map files, one program for
// all maps) are shell_sdl_win.inc's.
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

releases::Packaged sdlPackaged();  // (sdl_xdg.inc)
std::vector<std::wstring> sdlOpenFiles();  // (sdl_options.inc)
std::wstring sdlInstallRelease(const fs::path& self, const fs::path& fresh, const std::vector<std::string>& more = {});

// What Check for updates does with the newest release on this system (releases::updateWay).
releases::UpdateWay sdlUpdateWay(const releases::Release& r) { return releases::updateWay(stardraft::platform::programFile(), r, sdlPackaged()); }

void sdlInstallUpdate(const releases::Release& r);
bool sdlUpdatePreviews();
void sdlUpdateFound(const releases::Release& r);

// Help > Check for updates (and Program options > Check now): the newest release of the track (Updates > Previews).
void sdlCheckForUpdates() {
    shellWaitCursor();
    releases::Release r;
    std::string json;
    if (!releases::newest(r, sdlUpdatePreviews(), &json)) {
        updatesBox(shellTr(L"Could not get the newest release from GitHub. Make sure that the computer is connected to the internet."), {L"OK"},
                   msgbox::Icon::Warning);
        return;
    }
    releases::keepRelease(shellCacheFolder() / "releases", r.tag, json);
    sdlUpdateFound(r);  // (the top bar's Update)
    const std::wstring title = widen(r.title);
    using releases::UpdateWay;
    const UpdateWay way = sdlUpdateWay(r);
    if (way == UpdateWay::UpToDate) {
        updatesBox(shellTr(L"StarDraft 2 is up to date (") + title + L").", {L"OK"}, msgbox::Icon::Info);
        return;
    }
    // The browser gets this system's download: an AppImage, the new AppImage (the user puts it in place of the old
    // one); a release build that cannot put the new program in its place (macOS: the app is in a disk image), its
    // download.
    if (way == UpdateWay::Download) {
        const bool appImage = sdlPackaged() == releases::Packaged::AppImage;
        if (updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" +
                           shellTr(appImage ? L"Download the new AppImage?" : L"Download it? The download opens in the browser."),
                       {shellTr(L"&Download"), shellTr(L"Cancel")}, msgbox::Icon::Info) == 0)
            SDL_OpenURL(r.download.c_str());
        return;
    }
    // A Flatpak: its package manager (a software center, flatpak update) gets the new release.
    if (way == UpdateWay::Package) {
        updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" + shellTr(L"Update StarDraft 2 in your software center."), {L"OK"},
                   msgbox::Icon::Info);
        return;
    }
    // A build made here, or no download for this system: the release page, to get it by hand.
    if (way == UpdateWay::Page) {
        if (updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" + shellTr(L"This is not a release build. Open the release page?"),
                       {shellTr(L"&Open the release page"), shellTr(L"Cancel")}, msgbox::Icon::Info) == 0)
            openReleasePage(r);
        return;
    }
    if (updatesBox(shellTr(L"A newer release is available: ") + title + L".\n\n" + shellTr(L"Download and install it? StarDraft 2 starts again after it."),
                   {shellTr(L"&Install"), shellTr(L"Cancel")}, msgbox::Icon::Question) != 0)
        return;
    sdlInstallUpdate(r);
}

// Downloads the release and puts it in place of this program (each tab with changes is asked about first), then the
// new program starts: Check for updates' Install and the release notes' Install.
void sdlInstallUpdate(const releases::Release& r) {
    const std::wstring title = widen(r.title);
    const fs::path self = stardraft::platform::programFile();
    setMessage(L"Downloading " + title + L"\x2026");
    shellWaitCursor();
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();  // (a folder of this run's own)
    const fs::path dir = fs::temp_directory_path(ec) / ("StarDraft-update-" + std::to_string(stamp));
    std::wstring why;
    const fs::path fresh = releases::downloadRelease(r, dir, why);
    if (fresh.empty()) return updateFailed(r, why, dir);
    // Each tab with changes is asked about first (Save, Do not save, Cancel); then the new program takes this one's place and
    // starts, with the maps and projects that are open (as Restart now). A new program that does not start goes again.
    confirmQuit([r, self, fresh, dir] {
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

// Program options > Updates > Previews: the pre-releases too (off by default: the stable releases only).
bool sdlUpdatePreviews() {
    bool on = false;
    prefs::flag(L"UpdatePreviews", on);
    return on;
}

// The newest release that the update check found (on the UI thread): when it is newer than this build, the top bar
// shows Update (core/views_topbar.inc), else not.
void sdlUpdateFound(const releases::Release& r) {
    if (releases::newer(r)) releases::available() = r;
    else releases::available().reset();
    if (gRoot) gRoot->topBar.invalidate();
}

// The quiet update check (a release build, Program options > Updates > Look for updates on): the newest release of
// the track on a thread; GitHub's answer goes into the cache folder (the release notes when GitHub cannot be reached).
// No question and no box: the top bar's Update shows a newer release.
void sdlLookForUpdate() {
    bool check = true;
    prefs::flag(L"CheckUpdates", check);
    if (!releases::releaseBuild() || !check || gSilent) return;  // (a test run: no network)
    const bool previews = sdlUpdatePreviews();
    const fs::path cache = shellCacheFolder() / "releases";
    std::thread([previews, cache] {
        releases::Release r;
        std::string json;
        if (!releases::newest(r, previews, &json)) return;  // (no network: the next hour)
        releases::keepRelease(cache, r.tag, json);
        releases::keepRelease(cache, releases::newestName(previews), json);
        shellPost([r] { sdlUpdateFound(r); });
    }).detach();
}

// Each hour, the update check again (the timer is the window's; it ends with the program).
void sdlUpdateEachHour() {
    if (!gHost) return;
    gHost->timer(60 * 60 * 1000, [] {
        sdlLookForUpdate();
        sdlUpdateEachHour();
    });
}

// After the window shows (not in a test run): the update check now, then each hour.
void sdlUpdatesAtStart() {
    sdlRemoveOldProgram();  // (again: the old program of an update from a release before --restart may have ended only now)
    sdlLookForUpdate();
    sdlUpdateEachHour();
}

// --- The release notes window (core/views_changelog.inc) ------------------------------------------------------------

ChangelogView gChangelogView;
bool gChangelogOpen = false;
int gChangelogAsk = 0;          // the newest request for the window's release (an older answer that comes later is dropped)
std::string gReleaseFixture;    // --release <file>: GitHub's answer for the newest release, in place of GitHub's (a test: no network)

// What Install does with the release on this system (Check for updates' ways): its button's text and tooltip, or a note.
void sdlChangelogShow(const releases::Release& r, bool kept) {
    using releases::UpdateWay;
    std::wstring label, tip, note;
    switch (sdlUpdateWay(r)) {
    case UpdateWay::Install: label = L"Install", tip = tr(L"Download and install it. StarDraft 2 starts again after it."); break;
    case UpdateWay::Download: label = L"Download", tip = tr(L"The download opens in the browser"); break;
    case UpdateWay::Package: note = tr(L"Update StarDraft 2 in your software center."); break;
    default: break;
    }
    gChangelogView.setRelease(r, kept, label, tip, note);
}

void sdlShowChangelog(const std::string& tag);

// The window's parts: the links, Install, the other releases (once).
void sdlChangelogWire() {
    static bool wired = false;
    if (wired) return;
    wired = true;
    changelog::setHtmlHooks(!gSilent);
    gChangelogView.onOpenUrl = [](const std::string& url) {
        if (gSilent) std::printf("changelog: the browser opens %s\n", url.c_str());  // (a test run opens no browser)
        else SDL_OpenURL(url.c_str());
    };
    gChangelogView.onRelease = [](const std::string& tag) { sdlShowChangelog(tag); };
    gChangelogView.onInstall = [] {
        const releases::Release r = gChangelogView.release();
        if (sdlUpdateWay(r) == releases::UpdateWay::Download) {
            SDL_OpenURL(r.download.c_str());
            return;
        }
        gChangelogView.close();
        sdlInstallUpdate(r);
    };
    gChangelogView.closed = [] { gChangelogOpen = false; };
}

// The release notes of the release with the tag `tag` ("": the newest release of the track; the top bar's Update has
// it already) in the window (one window: it shows the new release). From GitHub on a thread; when GitHub cannot be
// reached, the copy that the cache folder kept, else a line and Open on GitHub.
void sdlShowChangelog(const std::string& tag) {
    sdlChangelogWire();
    const int ask = ++gChangelogAsk;
    const bool previews = sdlUpdatePreviews();
    const std::string repo = "https://github.com/" + narrow(kIssueRepo) + "/releases";
    releases::Release fixture;
    const bool fixed = !gReleaseFixture.empty() && releases::parseRelease(gReleaseFixture, fixture);
    if (fixed && (tag.empty() || tag == fixture.tag)) sdlChangelogShow(fixture, false);
    else if (tag.empty() && releases::available()) sdlChangelogShow(*releases::available(), false);
    else if (gSilent) gChangelogView.setNotReached(tag.empty() ? repo : repo + "/tag/" + tag);  // (a test run: no network)
    else {
        gChangelogView.setLoading(tag.empty() ? repo : repo + "/tag/" + tag);
        const fs::path cache = shellCacheFolder() / "releases";
        std::thread([ask, tag, previews, cache, repo] {
            releases::Release r;
            std::string json;
            const bool got = tag.empty() ? releases::newest(r, previews, &json) : releases::byTag(tag, r, &json);
            if (got) releases::keepRelease(cache, r.tag, json);
            shellPost([ask, tag, previews, cache, repo, got, r] {
                if (ask != gChangelogAsk) return;  // (another release was asked for since)
                releases::Release kept;
                if (got) sdlChangelogShow(r, false);
                else if (releases::keptRelease(cache, tag.empty() ? releases::newestName(previews) : tag, kept)) sdlChangelogShow(kept, true);
                else gChangelogView.setNotReached(tag.empty() ? repo : repo + "/tag/" + tag);
            });
        }).detach();
    }
    if (gChangelogOpen || !gRoot || !gHost) return;
    gChangelogOpen = true;
    const draw::Point sz = ChangelogView::size();
    const int h = std::min(sz.y, std::max(scale(300), gRoot->height() - scale(80)));
    gHost->openWindow(*gRoot, gChangelogView, draw::WindowSpec{draw::WindowKind::Dialog, draw::Rect(0, 0, sz.x, h), L"Release notes"},
                      [] { gChangelogOpen = false; });
}

// --release <file>: the newest release is the one in GitHub's answer in <file> (a test of the top bar's Update and the
// release notes, with no network). Read before the window; it shows in the top bar when it is newer than this build.
void sdlSetReleaseFixture(const std::string& file) {
    if (std::ifstream in(fs::path(widen(file)), std::ios::binary); in)
        gReleaseFixture.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (gReleaseFixture.empty()) std::printf("release: no file %s (WRONG)\n", file.c_str());
}
void sdlApplyReleaseFixture() {
    releases::Release r;
    if (!gReleaseFixture.empty() && releases::parseRelease(gReleaseFixture, r)) sdlUpdateFound(r);
}

// The options' commands of this file that this system runs (the menus and the keys ask sdlRuns).
bool sdlUpdatesRuns(int id) {
#ifdef _WIN32
    if (id == ID_OPT_MAPSDEFAULT) return true;  // (the map files are Windows only)
#endif
    return id == ID_OPT_UPDATECHECK || id == ID_OPT_PREVIEWS || id == ID_HELP_WHATSNEW || id == ID_UPDATE_NOTES ||
           (id >= ID_CRASH_MODE && id <= ID_CRASH_MODE + 2);
}

// The commands of this file (sdl_menus.inc's command() asks it): true when the command was one of them.
bool sdlMakeMapsDefault();  // (shell_sdl_win.inc on Windows; false elsewhere: the map files are Windows only)
bool sdlUpdatesCommand(int id) {
    switch (id) {
    case ID_HELP_UPDATES: sdlCheckForUpdates(); return true;
    case ID_HELP_WHATSNEW: sdlShowChangelog(releases::ownTag()); return true;  // (a build that is not a release's: the newest release)
    case ID_UPDATE_NOTES: sdlShowChangelog(releases::available() ? releases::available()->tag : std::string()); return true;
    case ID_OPT_PREVIEWS: {
        const bool on = !sdlUpdatePreviews();
        if (!gSilent) prefs::setNumber(L"UpdatePreviews", on ? 1 : 0);  // (tests never change the user's settings)
        setMessage(on ? L"StarDraft 2 offers pre-releases too" : L"StarDraft 2 offers the stable releases only");
        gOptionsView.invalidate();
        sdlLookForUpdate();  // (the newest release of the track now)
        return true;
    }
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
    // release:<file>: what Check for updates does with a release in GitHub's JSON in <file> (a release of a test, not
    // GitHub's: no network).
    if (what.rfind("release:", 0) == 0) {
        std::string j;
        if (std::ifstream in(fs::path(widen(what.substr(8))), std::ios::binary); in) j.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        releases::Release r;
        if (!releases::parseRelease(j, r)) return line("release: no release in " + what.substr(8) + " (WRONG)"), true;
        using releases::UpdateWay;
        static const char* const kPackaged[] = {"none", "AppImage", "Flatpak"};
        const UpdateWay way = sdlUpdateWay(r);
        line("release: \"" + r.title + "\", commit " + r.commit + ", page " + r.page);
        line("release: this system's download (" + std::string(stardraft::platform::releaseAssetSuffix()) + ") " + (r.download.empty() ? "none" : r.download));
        line("release: newer " + yes(releases::newer(r)) + ", a release build " + yes(releases::releaseBuild()) + ", packaged " + kPackaged[int(sdlPackaged())] +
             ", Check for updates: " +
             (way == UpdateWay::UpToDate  ? std::string("nothing (up to date)")
              : way == UpdateWay::Install ? std::string("installs it")
              : way == UpdateWay::Download ? "opens " + r.download
              : way == UpdateWay::Package ? std::string("says that the package manager updates it")
                                          : "opens " + r.page));
        return true;
    }
    // release-token:<file>: the newest release when GitHub refuses the build's token (401: it expired or was revoked),
    // with GitHub's JSON in <file> as the answer to a request without it (no network).
    if (what.rfind("release-token:", 0) == 0) {
        std::string j;
        if (std::ifstream in(fs::path(widen(what.substr(14))), std::ios::binary); in) j.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        int withToken = 0, without = 0;
        const releases::HttpCall real = releases::httpCall();
        releases::httpCall() = [&](const stardraft::platform::HttpRequest& q, std::string* text, const fs::path*) {
            stardraft::platform::HttpReply r;
            r.complete = true;
            if (!q.token.empty()) return ++withToken, r.status = 401, r;
            ++without, r.status = 200;
            if (text) *text = j;
            return r;
        };
        releases::Release r;
        const bool found = releases::latest(r, "an expired token");
        releases::httpCall() = real;
        line("release-token: " + std::to_string(withToken) + " request with the token (refused), " + std::to_string(without) + " without it: " +
             (found ? "\"" + r.title + "\" (yes)" : std::string("no release (WRONG)")));
        return true;
    }
    if (what == "crashes") return out = sdlCrashCheck(), true;
    if (what == "filetypes") return out = sdlFileTypesCheck(), true;
    return false;
}

// --- Snapshots of the HTML view and the release notes (--dialog html:<file>, changelog, changelog:offline) ---------

// --dialog html:<file>: a page on disk in the HTML view, in a scroll view as large as the picture (--size).
struct HtmlPageShot final : draw::View {
    stardraft::kit::ScrollView scroll;
    stardraft::html::HtmlView page;
    HtmlPageShot() {
        scroll.setContent(&page);
        page.setExtraCss("body { padding: 12px 16px; }");
        page.onScrollTo = [this](int y) { scroll.setOffset(0, y); };
        add(scroll, {});
    }
    void paint(draw::Canvas& c) override { c.fill({0, 0, width(), height()}, kBg); }
    void resized() override { scroll.setFrame({0, 0, width(), height()}); }
};
std::unique_ptr<HtmlPageShot> gHtmlShot;

// The view of a --dialog of this file (null: not one of them); `w` and `h`: the picture's size.
draw::View* sdlChangelogShot(const std::string& arg, int& w, int& h) {
    if (arg.rfind("html:", 0) == 0) {
        changelog::setHtmlHooks(false);
        std::string html;
        const fs::path file = fs::path(widen(arg.substr(5)));
        if (std::ifstream in(file, std::ios::binary); in) html.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (html.empty()) std::printf("html: no file %s (WRONG)\n", arg.substr(5).c_str());
        gHtmlShot = std::make_unique<HtmlPageShot>();
        gHtmlShot->setFrame({0, 0, w, h});
        const std::u8string base = file.u8string();
        gHtmlShot->page.setHtml(html, std::string(base.begin(), base.end()));
        return gHtmlShot.get();
    }
    if (arg == "changelog" || arg == "changelog:offline" || arg == "changelog:links") {
        if (arg == "changelog:offline") gReleaseFixture.clear();  // (no answer and no kept copy: GitHub cannot be reached)
        sdlChangelogWire();
        const draw::Point sz = ChangelogView::size();
        w = sz.x, h = sz.y;
        gChangelogView.setFrame({0, 0, w, h});
        sdlShowChangelog(std::string());
        return &gChangelogView;
    }
    return nullptr;
}

// After the picture: what the HTML view laid out (its height, the images, each link's box), what the release notes
// window shows, and the top bar's Update (`root`: the picture is the main window).
void sdlChangelogShotLines(const std::string& arg, bool root) {
    auto htmlLines = [](const char* what, stardraft::html::HtmlView& v) {
        std::printf("%s: laid out %d x %d px, title \"%s\", images %d of %d shown\n", what, v.width(), v.contentHeight(), v.title().c_str(), v.imagesShown(),
                    v.imagesAsked());
        for (const stardraft::html::Link& l : v.links())
            std::printf("%s: link %s at %d,%d,%d,%d\n", what, l.url.c_str(), l.box.left, l.box.top, l.box.right, l.box.bottom);
    };
    // A click on a point of the view (a press and a release, after the mouse moved there), as the window's router
    // gives it: the deepest view first, then its parents.
    auto clickAt = [](draw::View& v, draw::Point at) {
        draw::MouseEvent e;
        e.pt = at;
        for (const draw::MouseKind k : {draw::MouseKind::Move, draw::MouseKind::Down, draw::MouseKind::Up}) {
            e.kind = k;
            v.mouse(e);
        }
    };
    auto middle = [](const draw::Rect& r) { return draw::Point{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; };
    if (arg.rfind("html:", 0) == 0 && gHtmlShot) {
        // The wheel (the HTML view leaves it to the scroll view), three notches down; then the "#a-list" link scrolls
        // to its heading.
        draw::MouseEvent wheel;
        wheel.kind = draw::MouseKind::Wheel, wheel.wheel = -3, wheel.pt = {100, 100};
        const bool pageTook = gHtmlShot->page.mouse(wheel);
        gHtmlShot->scroll.mouse(wheel);
        std::printf("html: the wheel (3 notches) scrolls to %d px%s\n", gHtmlShot->scroll.offsetY(), pageTook ? " (the page took it: WRONG)" : "");
        const std::vector<stardraft::html::Link> links = gHtmlShot->page.links();  // (a copy: a layout makes them again)
        for (const stardraft::html::Link& l : links)
            if (l.url == "#a-list") {
                gHtmlShot->scroll.setOffset(0, gHtmlShot->scroll.maxOffsetY());
                clickAt(gHtmlShot->page, middle(l.box));
                std::printf("html: the link #a-list scrolls to %d px (its heading at %d px)\n", gHtmlShot->scroll.offsetY(), gHtmlShot->page.anchorY("a-list"));
            }
        htmlLines("html", gHtmlShot->page);
        std::printf("html: scroll bar %s, content %d px, view %d px\n", gHtmlShot->scroll.hasVerticalBar() ? "shown" : "not shown", gHtmlShot->page.height(),
                    gHtmlShot->scroll.viewportHeight());
    }
    if (arg.rfind("changelog", 0) == 0) {
        static const char* const kStates[] = {"loading", "shown", "kept", "not reached"};
        const ChangelogView& v = gChangelogView;
        std::string buttons;
        for (const std::wstring& b : v.buttons()) buttons += (buttons.empty() ? "" : ", ") + narrow(b);
        std::printf("changelog: %s, \"%s\", page %s, buttons %s; line \"%s\"\n", kStates[int(v.state())], v.release().title.c_str(), v.page().c_str(),
                    buttons.c_str(), narrow(v.statusLine()).c_str());
        htmlLines("changelog", gChangelogView.html());
        if (arg == "changelog:links") {
            // The mouse on each link: the hand; a click on the link to the pull request opens the browser, a click on
            // the link to another release shows its notes in the window.
            stardraft::html::HtmlView& page = gChangelogView.html();
            for (const stardraft::html::Link& l : page.links()) {
                draw::MouseEvent move;
                move.kind = draw::MouseKind::Move, move.pt = middle(l.box);
                page.mouse(move);
                std::printf("changelog: the mouse on %s: %s\n", l.url.c_str(), page.cursor(move.pt) == draw::Cursor::Hand ? "the hand" : "the arrow (WRONG)");
            }
            const std::vector<stardraft::html::Link> links = page.links();
            for (const stardraft::html::Link& l : links)
                if (l.url.find("/pull/") != std::string::npos) clickAt(page, middle(l.box));
            for (const stardraft::html::Link& l : links)
                if (l.url.find("/releases/tag/") != std::string::npos) clickAt(page, middle(l.box));
            std::printf("changelog: then %s, page %s\n", kStates[int(gChangelogView.state())], gChangelogView.page().c_str());
        }
    }
    if (root && gRoot) {
        const RECT u = gRoot->topBar.updateRc;
        if (u.right > u.left)
            std::printf("topbar: Update at %d,%d,%d,%d, tip \"%s\"\n", int(u.left), int(u.top), int(u.right), int(u.bottom),
                        narrow(releases::available() ? topBarUpdateTip(*releases::available()) : std::wstring()).c_str());
        else std::printf("topbar: no Update\n");
    }
}
