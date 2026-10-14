// Help > Check for updates (included into main.cpp after feedback.inc). The newest release on GitHub has the title
// "StarDraft <version> (build <commit>)"; when its commit is not this build's, its zip is downloaded, stardraft.exe
// in it takes the place of this program (the running .exe is renamed to stardraft.exe.old, deleted at the next
// start) and StarDraft starts again. A release build also checks at start, quietly, and says so in the status bar.

constexpr UINT WM_UPDATE_FOUND = WM_APP + 32;  // lp: std::wstring* (the release's title), deleted by the receiver

bool closeAllDocs(std::vector<std::wstring>* reopen);

// A value under HKCU\Software\Classes\<key> (for this Windows user, no administrator needed): the key's default value,
// or the named one. Written only when it changes; true then.
inline bool putClassesValue(const std::wstring& key, const std::wstring& value, const wchar_t* name = nullptr, DWORD type = REG_SZ) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + key).c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr)) return false;
    wchar_t now[1024] = L"";
    DWORD size = sizeof now, had = 0;
    const bool same = !RegQueryValueExW(k, name, nullptr, &had, reinterpret_cast<BYTE*>(now), &size) && had == type && value == now;
    if (!same) RegSetValueExW(k, name, 0, type, reinterpret_cast<const BYTE*>(value.c_str()), type == REG_NONE ? 0 : DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return !same;
}

// project.stardraft opens with `exe`. Explorer is told when something changed.
inline void registerProjectFiles(const std::wstring& exe) {
    auto put = [](const std::wstring& key, const std::wstring& value) { return putClassesValue(key, value); };
    bool changed = put(L".stardraft", L"StarDraft.Project");
    changed |= put(L"StarDraft.Project", L"StarDraft 2 project");
    changed |= put(L"StarDraft.Project\\DefaultIcon", L"\"" + exe + L"\",1");  // the second icon: the document icon
    changed |= put(L"StarDraft.Project\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// The program that file types open: this one, or StarDraft-latest.exe for a copy that the launcher started (the copy
// is deleted later; the launcher starts the newest build with the file).
inline std::wstring associationExe() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (fs::path(self).filename().wstring().rfind(L"stardraft-latest-", 0) != 0) return self;
    wchar_t launcher[MAX_PATH] = L"";  // the launcher says where it is
    std::error_code ec;
    if (GetEnvironmentVariableW(L"STARDRAFT_LAUNCHER", launcher, MAX_PATH) && fs::exists(launcher, ec)) return launcher;
    return self;
}

// Maps (.scm, .scx): StarDraft 2 is in Explorer's Open with list for them, and with `makeDefault` it is the program a
// double-click opens. Windows keeps a program that the user chose in its own Open with window (a UserChoice): then
// only Settings > Default apps changes it, and this returns false.
inline const wchar_t* const kMapExtensions[] = {L".scm", L".scx"};
inline bool userChoiceElsewhere(const wchar_t* ext) {
    // UserChoice, and UserChoiceLatest (where Windows 11 keeps it, often without UserChoice).
    for (const wchar_t* sub : {L"\\UserChoice", L"\\UserChoiceLatest"}) {
        wchar_t progId[256] = L"";
        DWORD size = sizeof progId;
        const std::wstring key = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\") + ext + sub;
        if (!RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"ProgId", RRF_RT_REG_SZ, nullptr, progId, &size) && std::wstring(progId) != L"StarDraft.Map")
            return true;
    }
    return false;
}
inline bool mapsOpenWithStarDraft() {
    for (const wchar_t* ext : kMapExtensions) {
        wchar_t now[256] = L"";
        DWORD size = sizeof now;
        if (RegGetValueW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + ext).c_str(), nullptr, RRF_RT_REG_SZ, nullptr, now, &size) ||
            std::wstring(now) != L"StarDraft.Map" || userChoiceElsewhere(ext))
            return false;
    }
    return true;
}
inline bool registerMapFiles(const std::wstring& exe, bool makeDefault) {
    bool changed = putClassesValue(L"StarDraft.Map", L"StarDraft 2 map");
    changed |= putClassesValue(L"StarDraft.Map\\DefaultIcon", L"\"" + exe + L"\",1");
    changed |= putClassesValue(L"StarDraft.Map\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    bool ours = true;
    for (const wchar_t* ext : kMapExtensions) {
        changed |= putClassesValue(std::wstring(ext) + L"\\OpenWithProgids", L"", L"StarDraft.Map", REG_NONE);
        if (makeDefault) changed |= putClassesValue(ext, L"StarDraft.Map");
        ours = ours && !userChoiceElsewhere(ext);
    }
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ours;
}
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
constexpr UINT WM_ASK_MAPS = WM_APP + 33;  // after a map file opened: ask once if maps open with StarDraft 2
inline bool askedMapsDefault() {
    bool asked = false;
    prefs::flag(L"AskedMapsDefault", asked);
    return asked;
}
// A .scm or .scx file opened (not a new map, not a project): the first time, the question follows (after the map shows).
inline void mapFileOpened(const fs::path& file) {
    std::wstring ext = file.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (gBackground || gSelfTest || (ext != L".scm" && ext != L".scx") || askedMapsDefault() || mapsOpenWithStarDraft()) return;
    PostMessageW(app.main, WM_ASK_MAPS, 0, 0);
}
inline void askMapsDefault(HWND owner) {
    if (askedMapsDefault()) return;  // (two maps opened at once: one question)
    prefs::setNumber(L"AskedMapsDefault", 1);
    if (themedMessageBox(owner, L"Open .scm and .scx maps with StarDraft 2 when you double-click them? You can make StarDraft 2 open them later in Program options > Maps.",
                         L"StarDraft 2", MB_YESNO | MB_ICONQUESTION) == IDYES)
        makeMapsDefault(owner);
}

namespace update {

// A GET; api.github.com gets the token (the repository is private). A redirect (a release asset is served from
// another host, with its own signed address) is followed without it. `file`: write the body there.
bool get(const std::wstring& url, std::string* body, const fs::path* file = nullptr, const wchar_t* accept = L"application/vnd.github+json", int hops = 0) {
    const HttpReply r = httpRequest(url, L"GET", accept, crash::token(), "", body, file, false);
    if (!r.location.empty() && hops < 5) return get(r.location, body, file, accept, hops + 1);
    return r.status == 200 && r.complete;
}

// A string value after `"key":"` (from `from` on); "" when there is none.
std::string jsonString(const std::string& j, const std::string& key, size_t from = 0) {
    const std::string k = "\"" + key + "\":\"";
    const size_t a = j.find(k, from);
    if (a == std::string::npos) return {};
    const size_t b = j.find('"', a + k.size());
    return b == std::string::npos ? std::string() : j.substr(a + k.size(), b - a - k.size());
}

struct Release {
    std::string title, commit, zip, page;  // zip: the asset's API address (it downloads with the token)
};

// The newest release; false when GitHub cannot be reached.
bool latest(Release& r) {
    std::string j;
    if (!get(L"https://api.github.com/repos/" + std::wstring(crash::kRepo) + L"/releases/latest", &j)) return false;
    r.title = jsonString(j, "name");  // the release's own name comes before its assets' names
    r.page = jsonString(j, "html_url");
    if (const size_t a = r.title.find("(build "); a != std::string::npos) r.commit = r.title.substr(a + 7, r.title.find(')', a) - a - 7);
    for (size_t p = 0; (p = j.find("\"browser_download_url\":\"", p)) != std::string::npos; ++p) {
        const std::string u = jsonString(j, "browser_download_url", p);
        // This system's download (StarDraft-<version>-win64.zip): a release can also have downloads for other systems.
        static const std::string kOurs = stardraft::platform::releaseAssetSuffix();
        if (u.size() < kOurs.size() || u.compare(u.size() - kOurs.size(), kOurs.size(), kOurs) != 0) continue;
        // The asset's "url" (api.github.com/.../releases/assets/<id>) comes before its browser_download_url.
        for (size_t k = j.rfind("\"url\":\"", p); k != std::string::npos; k = k ? j.rfind("\"url\":\"", k - 1) : std::string::npos) {
            const std::string a = jsonString(j, "url", k);
            if (a.find("/releases/assets/") != std::string::npos) {
                r.zip = a;
                break;
            }
        }
    }
    return !r.title.empty();
}

bool newer(const Release& r) { return !r.commit.empty() && r.commit != STARDRAFT_COMMIT; }

// A release build: the one CI makes (the others are built here, by a developer).
bool releaseBuild() { return !crash::token().empty(); }

// At start: the .old program of an update goes; project.stardraft opens with this program (not with a copy that
// StarDraft-latest started: the launcher does that); a release build looks for a newer release, on a thread.
void atStart() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (fs::path(self).filename().wstring().rfind(L"stardraft-latest-", 0) != 0) registerProjectFiles(self);
    registerMapFiles(associationExe(), false);  // in Explorer's Open with list for maps (the default when asked: mapFileOpened)
    std::error_code ec;
    fs::remove(stardraft::platform::oldProgramFile(self), ec);
    if (!releaseBuild() || !gCheckUpdates) return;  // File > Options > Look for updates on start (on by default)
    std::thread([main = app.main] {
        Release r;
        if (latest(r) && newer(r)) PostMessageW(main, WM_UPDATE_FOUND, 0, LPARAM(new std::wstring(widen(r.title))));
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
    if (!releaseBuild() || _wcsicmp(self.filename().c_str(), L"stardraft.exe") != 0 || r.zip.empty()) {
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
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path zip = dir / "update.zip";
    auto fail = [&](const std::wstring& why) {
        themedMessageBox(app.main, (why + L"\n\nThe release page opens: get the zip from there.").c_str(), L"Check for updates", MB_ICONWARNING);
        ShellExecuteW(nullptr, L"open", widen(r.page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        fs::remove_all(dir, ec);
    };
    if (!get(widen(r.zip), nullptr, &zip, L"application/octet-stream")) return fail(L"The download did not complete.");
    const fs::path fresh = dir / "stardraft.exe";
    if (!stardraft::platform::unpackZip(zip, dir) || !fs::exists(fresh, ec)) return fail(L"Could not unpack the download.");
    // Close the documents (asks to save them), then put the new program in place and start it.
    if (!closeAllDocs()) return fs::remove_all(dir, ec), void();
    switch (stardraft::platform::replaceProgram(self, fresh)) {
    case stardraft::platform::Replace::OldNotMoved: return fail(L"Could not replace the program file in " + self.parent_path().wstring() + L".");
    case stardraft::platform::Replace::NewNotPlaced: return fail(L"Could not put the new program in " + self.parent_path().wstring() + L".");
    case stardraft::platform::Replace::Done: break;
    }
    fs::remove_all(dir, ec);
    stardraft::platform::startProgram(self);
    DestroyWindow(app.main);
}

}  // namespace update
