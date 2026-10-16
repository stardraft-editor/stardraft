// Help > Check for updates (included into main.cpp after feedback.inc). The newest release on GitHub has the title
// "StarDraft <version> (build <commit>)"; when its commit is not this build's, its zip is downloaded, stardraft.exe
// in it takes the place of this program (the running .exe is renamed to stardraft.exe.old, deleted at the next
// start) and StarDraft starts again. A release build also checks at start, quietly, and says so in the status bar.


bool closeAllDocs(std::vector<std::wstring>* reopen);

// The file types (apps/common/file_types_win32.cpp): project.stardraft, and the maps' Open with list and default.
using stardraft::platform::associationExe;
using stardraft::platform::mapsOpenWithStarDraft;
using stardraft::platform::registerMapFiles;
using stardraft::platform::registerProjectFiles;

// Program options > Maps, and the question at start: maps open with StarDraft 2. Where Windows keeps another program
// that the user chose, its Default apps settings open.
inline void makeMapsDefault(HWND owner) {
    if (registerMapFiles(associationExe(), true)) return setMessage(L"Maps (.scm, .scx) open with StarDraft 2");
    themedMessageBox(owner,
                     L"Windows keeps the program that you selected for .scm or .scx maps. In Default apps, search for .scm and .scx and select "
                     L"StarDraft 2.",
                     L"Default apps", MB_ICONINFORMATION);
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}
inline bool askedMapsDefault() {
    bool asked = false;
    prefs::flag(L"AskedMapsDefault", asked);
    return asked;
}
inline void askMapsDefault(HWND owner);
// A .scm or .scx file opened (not a new map, not a project): the first time, the question follows (after the map shows).
inline void mapFileOpened(const fs::path& file) {
    std::wstring ext = file.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (gBackground || gSelfTest || (ext != L".scm" && ext != L".scx") || askedMapsDefault() || mapsOpenWithStarDraft()) return;
    shellPost([] { askMapsDefault(app.main); });
}
inline void askMapsDefault(HWND owner) {
    if (askedMapsDefault()) return;  // (two maps opened at once: one question)
    prefs::setNumber(L"AskedMapsDefault", 1);
    if (themedMessageBox(owner, L"Open .scm and .scx maps with StarDraft 2 when you double-click them? You can make StarDraft 2 open them later in Program options > Maps.",
                         L"StarDraft 2", MB_YESNO | MB_ICONQUESTION) == IDYES)
        makeMapsDefault(owner);
}

namespace update {

// The newest release and the download (core/releases.inc).
using releases::latest;
using releases::newer;
using releases::Release;
using releases::releaseBuild;

// At start: the .old program of an update goes; project.stardraft opens with this program (not with a copy that
// StarDraft-latest started: the launcher does that); a release build looks for a newer release, on a thread. This
// program is the fallback now (stardraft-win32.exe; the SDL program is stardraft.exe): with "-win32" in its name it
// leaves the file types to the SDL program.
void atStart() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring name = fs::path(self).stem().wstring();
    for (wchar_t& ch : name) ch = wchar_t(towlower(ch));
    if (name.find(L"-win32") == std::wstring::npos) {
        if (name.rfind(L"stardraft-latest-", 0) != 0) registerProjectFiles(self);
        registerMapFiles(associationExe(), false);  // in Explorer's Open with list for maps (the default when asked: mapFileOpened)
    }
    std::error_code ec;
    fs::remove(stardraft::platform::oldProgramFile(self), ec);
    if (!releaseBuild() || !gCheckUpdates) return;  // File > Options > Look for updates on start (on by default)
    std::thread([] {
        Release r;
        if (latest(r) && newer(r)) {
            const std::wstring title = widen(r.title);
            shellPost([title] { setMessage(L"A newer release is available: " + title + L". Use Help > Check for updates."); });
        }
    }).detach();
}

// Help > Check for updates.
void check() {
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    Release r;
    if (!latest(r)) {
        themedMessageBox(app.main, L"Could not get the newest release from GitHub. Make sure that the computer is connected to the internet.", L"Check for updates",
                         MB_ICONWARNING);
        return;
    }
    if (!newer(r)) {
        themedMessageBox(app.main, (L"StarDraft 2 is up to date (" + widen(r.title) + L").").c_str(), L"Check for updates", MB_ICONINFORMATION);
        return;
    }
    const fs::path self = stardraft::platform::programFile();
    if (!releases::installable(self, r)) {
        // A build made here: the release page, to get it by hand.
        if (themedMessageBox(app.main, (L"A newer release is available: " + widen(r.title) + L".\n\nThis is not a release build. Open the release page?").c_str(),
                             L"Check for updates", MB_YESNO | MB_ICONINFORMATION, {L"&Open the release page", L"Cancel"}) == IDYES)
            ShellExecuteW(nullptr, L"open", widen(r.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (themedMessageBox(app.main, (L"A newer release is available: " + widen(r.title) + L".\n\nDownload and install it? StarDraft 2 starts again after it.").c_str(),
                         L"Check for updates", MB_YESNO | MB_ICONQUESTION, {L"&Install", L"Cancel"}) != IDYES)
        return;
    // Download and unpack.
    setMessage(L"Downloading " + widen(r.title) + L"\x2026");
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / ("StarDraft-update-" + std::to_string(GetCurrentProcessId()));
    auto fail = [&](const std::wstring& why) {
        themedMessageBox(app.main, (why + L"\n\nThe release page opens: get the zip from there.").c_str(), L"Check for updates", MB_ICONWARNING);
        ShellExecuteW(nullptr, L"open", widen(r.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        fs::remove_all(dir, ec);
    };
    std::wstring why;
    const fs::path fresh = releases::downloadRelease(r, dir, why);
    if (fresh.empty()) return fail(why);
    // Close the documents (asks to save them), then put the new program in place and start it.
    if (!closeAllDocs()) return fs::remove_all(dir, ec), void();
    if (why = releases::placeRelease(self, fresh); !why.empty()) return fail(why);
    fs::remove_all(dir, ec);
    stardraft::platform::startProgram(self);
    DestroyWindow(app.main);
}

}  // namespace update
