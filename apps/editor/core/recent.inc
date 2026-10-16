// Recently opened maps and projects, and the folder projects usually go in (where the last one was made or opened):
// saved in the settings (prefs: RecentMaps, RecentProjects, ProjectsFolder). The File menu's two lists are the host's
// (shellRecentMenu). Portable: included by core.inc and by main.cpp after ids.inc.

bool gSelfTest = false;             // the self-test runs (its settings are not the user's)
bool gSettingsOff = false;          // tests and screenshot runs: the defaults, and nothing saved (the lists, the layout, the options)
std::wstring gProjectsFolder;         // Program options > Projects folder

// The working folder: new projects go there (Program options > Working folder; Documents/StarDraft by default).
fs::path defaultProjectsFolder() {
    if (!gProjectsFolder.empty()) return gProjectsFolder;  // (made when the first project goes there)
    const fs::path docs = shellDocumentsFolder();
    return docs.empty() ? fs::path() : docs / "StarDraft";
}

std::vector<std::wstring> gRecentMaps, gRecentProjects;

void saveRecent() {
    if (gSelfTest || gSettingsOff) return;  // the self-test and the screenshot runs leave the user's lists alone
    prefs::setList(L"RecentMaps", gRecentMaps);
    prefs::setList(L"RecentProjects", gRecentProjects);
    prefs::setText(L"ProjectsFolder", gProjectsFolder);
}

// The File menu's two lists: "1  MyMap.scx    C:\Maps", newest first (the host makes the menu items, with Clear).
std::vector<std::wstring> recentLabels(const std::vector<std::wstring>& list) {
    std::vector<std::wstring> out;
    for (size_t i = 0; i < list.size(); ++i) {
        const fs::path p = list[i];
        std::wstring label = (i < 9 ? L"&" + std::to_wstring(i + 1) : L"1&0") + L"  " + p.filename().wstring() + L"\t" +
                             fs::path(p.parent_path()).make_preferred().wstring();
        for (size_t at = 0; (at = label.find(L'&', at + 2)) != std::wstring::npos;) label.insert(at, L"&");  // literal & in names
        out.push_back(label);
    }
    return out;
}

void refreshRecentMenus() {
    shellRecentMenu(false, recentLabels(gRecentMaps), L"None");
    shellRecentMenu(true, recentLabels(gRecentProjects), L"None");
}

// Equal ignoring case (the characters' lower case: the letters of the ASCII and the C locale; the paths of the lists
// are also compared with fs::equivalent).
bool equalIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return towlower(x) == towlower(y); });
}

void removeRecent(std::vector<std::wstring>& list, const fs::path& p) {
    std::error_code ec;
    std::erase_if(list, [&](const std::wstring& item) { return equalIgnoreCase(item, p.wstring()) || fs::equivalent(item, p, ec); });
}

void addRecent(std::vector<std::wstring>& list, const fs::path& path) {
    std::error_code ec;
    const fs::path p = fs::absolute(path, ec).make_preferred();
    removeRecent(list, p);
    list.insert(list.begin(), p.wstring());
    if (list.size() > size_t(kRecentMax)) list.resize(size_t(kRecentMax));
    saveRecent();
    refreshRecentMenus();
}

// A project was made or opened in <folder>/<name>/. The working folder is the one set in Program options (not the
// last project's folder): it stays as the user set it.
void rememberProjectsFolder(const fs::path&) {}
