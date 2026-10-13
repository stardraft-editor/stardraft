// Help > Check for updates (included into main.cpp after feedback.inc). The newest release on GitHub has the title
// "StarDraft <version> (build <commit>)"; when its commit is not this build's, its zip is downloaded, stardraft.exe
// in it takes the place of this program (the running .exe is renamed to stardraft.exe.old, deleted at the next
// start) and StarDraft starts again. A release build also checks at start, quietly, and says so in the status bar.

constexpr UINT WM_UPDATE_FOUND = WM_APP + 32;  // lp: std::wstring* (the release's title), deleted by the receiver

bool closeAllDocs();

// project.stardraft opens with `exe` (for this Windows user: HKCU\Software\Classes, no administrator needed).
// Written only when it changes; Explorer is told then.
inline void registerProjectFiles(const std::wstring& exe) {
    auto put = [](const std::wstring& key, const std::wstring& value) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + key).c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr)) return false;
        wchar_t now[1024] = L"";
        DWORD size = sizeof now;
        const bool same = !RegQueryValueExW(k, nullptr, nullptr, nullptr, reinterpret_cast<BYTE*>(now), &size) && value == now;
        if (!same) RegSetValueExW(k, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        return !same;
    };
    bool changed = put(L".stardraft", L"StarDraft 2.Project");
    changed |= put(L"StarDraft 2.Project", L"StarDraft 2 project");
    changed |= put(L"StarDraft 2.Project\\DefaultIcon", L"\"" + exe + L"\",1");  // the second icon: the document icon
    changed |= put(L"StarDraft 2.Project\\shell\\open\\command", L"\"" + exe + L"\" \"%1\"");
    if (changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

namespace update {

// A GET; api.github.com gets the token (the repository is private). A redirect (a release asset is served from
// another host, with its own signed address) is followed without it. `file`: write the body there.
bool get(const std::wstring& url, std::string* body, const fs::path* file = nullptr, const wchar_t* accept = L"application/vnd.github+json", int hops = 0) {
    URL_COMPONENTS uc{sizeof uc};
    wchar_t host[256] = L"", path[2048] = L"";
    uc.lpszHostName = host, uc.dwHostNameLength = 256, uc.lpszUrlPath = path, uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"StarDraft 2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : nullptr;
    HINTERNET q = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::wstring headers = L"Accept: " + std::wstring(accept) + L"\r\n";
    if (_wcsicmp(host, L"api.github.com") == 0 && !crash::token().empty()) headers += L"Authorization: Bearer " + widen(crash::token()) + L"\r\n";
    if (q) {
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(q, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
    }
    std::wstring redirect;
    if (q && WinHttpSendRequest(q, headers.c_str(), DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(q, nullptr)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(q, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status >= 300 && status < 400) {
            wchar_t loc[4096] = L"";
            DWORD n = sizeof loc;
            if (WinHttpQueryHeaders(q, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &n, WINHTTP_NO_HEADER_INDEX)) redirect = loc;
        }
        std::ofstream out;
        if (file) out.open(*file, std::ios::binary);
        ok = status == 200 && (!file || out);
        for (DWORD n = 0; ok && WinHttpQueryDataAvailable(q, &n) && n;) {
            std::string chunk(n, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(q, chunk.data(), n, &read)) ok = false;
            else if (file) out.write(chunk.data(), read);
            else body->append(chunk.data(), read);
        }
    }
    if (q) WinHttpCloseHandle(q);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if (!redirect.empty() && hops < 5) return get(redirect, body, file, accept, hops + 1);
    return ok;
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
        if (u.size() < 4 || u.substr(u.size() - 4) != ".zip") continue;
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
    std::error_code ec;
    fs::remove(fs::path(self).replace_extension(L".exe.old"), ec);
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
    wchar_t selfBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);
    const fs::path self = selfBuf;
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
    // tar.exe (in Windows 10 and later) unpacks zip files.
    wchar_t sys[MAX_PATH] = L"";
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + (fs::path(sys) / L"tar.exe").wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    DWORD code = 1;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    }
    const fs::path fresh = dir / "stardraft.exe";
    if (code != 0 || !fs::exists(fresh, ec)) return fail(L"Could not unpack the download.");
    // Close the documents (asks to save them), then put the new program in place and start it.
    if (!closeAllDocs()) return fs::remove_all(dir, ec), void();
    const fs::path old = fs::path(self).replace_extension(L".exe.old");
    fs::remove(old, ec);
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) return fail(L"Could not replace the program file in " + self.parent_path().wstring() + L".");
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING);  // back as it was
        return fail(L"Could not put the new program in " + self.parent_path().wstring() + L".");
    }
    fs::remove_all(dir, ec);
    std::wstring run = L"\"" + self.wstring() + L"\"";
    if (CreateProcessW(self.c_str(), run.data(), nullptr, nullptr, FALSE, 0, nullptr, self.parent_path().c_str(), &si, &pi))
        CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    DestroyWindow(app.main);
}

}  // namespace update
