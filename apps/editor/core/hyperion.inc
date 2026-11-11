// Hyperion maps (portable, core.inc; src/mods/hyperion.cpp): Program options > Plugins > Hyperion. Hyperion is a 64-bit
// engine for StarCraft: Brood War with a map format of its own, .hxm (a zip of map.json and the per-tile layers: Hyperion's
// docs/hxm-format.md). While the plugin is loaded, maps open and save as .hxm too, the New map page offers 384 and 512
// tiles a side, and Tools > Hyperion checks the map with Hyperion's own check and plays it in Hyperion.

// The Hyperion program in `folder`: Hyperion.exe on Windows, the app's program on macOS, Hyperion.x86_64 or
// Hyperion.aarch64 on Linux. Empty when the folder has none.
fs::path hyperionExeIn(const fs::path& folder) {
    if (folder.empty()) return {};
    std::error_code ec;
#if defined(_WIN32)
    const char* names[] = {"Hyperion.exe"};
#elif defined(__APPLE__)
    const char* names[] = {"Hyperion.app/Contents/MacOS/Hyperion", "Hyperion"};
#else
    const char* names[] = {"Hyperion.x86_64", "Hyperion.aarch64", "Hyperion"};
#endif
    for (const char* n : names)
        if (const fs::path p = folder / fs::u8path(n); fs::is_regular_file(p, ec)) return p;
    return {};
}
// The folder that the user chose (the setting HyperionFolder, read at the start: hyperionUseFolderSetting; Browse sets
// it), when it still has the program.
fs::path gHyperionDir;
void hyperionUseFolderSetting() {
    std::wstring saved;
    if (!gSettingsOff && prefs::text(L"HyperionFolder", saved)) gHyperionDir = fs::path(saved);
}
fs::path hyperionFolder() { return hyperionExeIn(gHyperionDir).empty() ? fs::path() : gHyperionDir; }
fs::path hyperionExe() { return hyperionExeIn(hyperionFolder()); }


// The Hyperion program, else a message that says where to choose it (false).
bool hyperionReady(fs::path& exe) {
    exe = hyperionExe();
    if (!exe.empty()) return true;
    shellWarningBox(tr(L"Hyperion was not found. Select its folder in Program options > Plugins > Hyperion folder."), tr(L"Hyperion"));
    return false;
}

bool saveMapFile(const fs::path& path, std::string& err);  // (core/map_commands.inc)
// The open map as a file that Hyperion reads: the saved map, saved again first when it has changes. False (with a
// message) when it has no file yet: File > Save as names it first.
bool hyperionMapFile(fs::path& map) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return false;
    if (app.ed->path().empty()) {
        shellWarningBox(tr(L"Save the map first (File > Save as), then test it in Hyperion."), tr(L"Hyperion"));
        return false;
    }
    if (app.ed->modified()) {
        std::string err;
        if (!saveMapFile(app.ed->path(), err)) {
            shellWarningBox(std::wstring(tr(L"The map could not be saved:")) + L"\r\n" + widen(err), tr(L"Hyperion"));
            return false;
        }
    }
    map = app.ed->path();
    return true;
}

// Writes the open map as an .hxm at `out` (src/mods/hyperion.cpp), the open file staying as it is. False with err.
bool hyperionWriteCopy(const fs::path& out, std::string& err) {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return err = "No map is open.", false;
    return app.ed->writeHxmCopy(out, err);
}

// Tools > Hyperion > Check the map in Hyperion: Hyperion.exe --hxm-check on the map (an .hxm; another map is checked as
// the .hxm it would save as, written to the temporary folder). Its report: the errors that stop the map, the warnings,
// and the last line, "plays: ..." or "refused: ...".
void hyperionCheckCommand() {
    AppState& app = shellState();
    if (!app.ed || !app.ed->isOpen()) return;
    fs::path exe;
    if (!hyperionReady(exe)) return;
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / "StarDraft-hyperion-check.hxm";
    std::string err;
    if (!hyperionWriteCopy(tmp, err))
        return shellWarningBox(std::wstring(tr(L"The map could not be written as .hxm:")) + L"\r\n" + widen(err), tr(L"Check the map in Hyperion"));
    std::vector<std::string> lines;
    const int code = stardraft::runProcess({narrow(exe.wstring()), "--hxm-check", narrow(tmp.wstring())}, exe.parent_path(),
                                           [&](const std::string& l) { lines.push_back(l); });
    fs::remove(tmp, ec);
    const std::wstring nl = L"\r\n";
    std::wstring text = code == 0   ? std::wstring(tr(L"Hyperion plays this map.")) + nl + nl
                        : code == 1 ? std::wstring(tr(L"Hyperion refuses this map:")) + nl + nl
                        : code == 2 ? std::wstring(tr(L"Hyperion could not read the map.")) + nl + nl
                                    : std::wstring(tr(L"Hyperion's check did not run.")) + nl + nl;
    for (size_t i = 0; i < lines.size() && i < 40; ++i) text += widen(lines[i]) + nl;
    if (lines.size() > 40) text += std::to_wstring(lines.size() - 40) + L" more lines" + nl;
    for (const std::string& l : lines) shellLogLine(L"Hyperion check: " + widen(l));
    if (code == 0) shellInfoBox(text, tr(L"Check the map in Hyperion"));
    else shellWarningBox(text, tr(L"Check the map in Hyperion"));
}

// Tools > Hyperion > Test in Hyperion: the saved map (saved first when it has changes) in a new Hyperion, at once: a
// melee game against computers, or with `ums` as Use Map Settings (its triggers run). Hyperion plays .scm and .scx as
// well as .hxm.
bool shellStartProgram(const std::vector<std::string>& args, const fs::path& dir, std::wstring& error);  // (shell_sdl.inc)
void hyperionTestCommand(bool ums) {
    fs::path exe, map;
    if (!hyperionReady(exe) || !hyperionMapFile(map)) return;
    std::vector<std::string> args = {narrow(exe.wstring()), "--map", narrow(fs::absolute(map).wstring())};
    if (ums) args.push_back("--ums");
    std::wstring error;
    if (!shellStartProgram(args, exe.parent_path(), error)) return shellWarningBox(error, tr(L"Test in Hyperion"));
    shellLogLine(L"Started Hyperion with " + map.filename().wstring() + (ums ? L" (Use Map Settings)" : L" (melee)"));
    setMessage(L"Started Hyperion with " + map.filename().wstring());
}
