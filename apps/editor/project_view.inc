// The Project workspace (included into main.cpp after the scripts' helpers): the project's files, its build options
// (euddraft's plugins: EUD Turbo, more units, air units that do not push apart, background music, files loaded into
// memory) and the scripts Build runs, in order. Painted with the control kit.

bool editTextValue(const std::wstring& title, std::string& text, POINT at);  // the value pop-out (main.cpp)
std::string trigCodeRtf(const std::string& code);                            // epScript in color (main.cpp)
LRESULT CALLBACK trigCodeKeys(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR);

struct ProjectPage {
    HWND wnd = nullptr, preview = nullptr;  // preview: the selected script's code, on the right (View: the </> button)
    std::string previewOf;                  // what the preview shows
    ui::Surface surface;
    int scroll = 0, height = 0;
    std::string selected;               // a script (its .eds section name)
    std::vector<std::string> sections;  // the scripts, in build order
    HANDLE epTrace = nullptr;           // epTrace while it is open
    fs::file_time_type epTraceStart{};  // when it started (a report from after it is new)
    fs::path epTraceReport;             // the report it writes (of the project that started it)
    fs::path epTraceFolder;             // that project's folder
    // epTrace's last report (read again when the file changes)
    fs::path reportFile;
    fs::file_time_type reportTime{};
    bool hasReport = false;
    eptrace::Report report;
} gProj;
constexpr UINT_PTR kEpTraceTimer = 41;

std::wstring trigCodeLineName(int line);
bool trigShowCodeLine(int line);

void projectSave(EudProject& pr, const std::wstring& done = L"") {
    std::string err;
    if (!pr.save(err)) setMessage(L"Could not save the project: " + widen(err));
    else if (!done.empty()) setMessage(done);
    InvalidateRect(gProj.wnd, nullptr, FALSE);
}

// Sections the page sets up elsewhere (Build options) or Build writes (keys and mouse, chat commands): not scripts.
bool projectManagedSection(const std::string& name) {
    for (const char* n : {"eudTurbo", "unlimiter", "noAirCollision", "bgmplayer", "dataDumper", "MSQC", "chatEvent"})
        if (name == n) return true;
    return false;
}

// The scripts in build order (the generated .dat code first; [main] is not a script).
void projectListScripts() {
    gProj.sections.clear();
    if (const EudProject* pr = activeProject())
        for (const auto& sec : pr->sections())
            if (sec.name != "main" && !projectManagedSection(sec.name)) gProj.sections.push_back(sec.name);
}

int projectPreviewWidth(int pageW) { return gDock.scriptPreview ? std::min(pageW / 2, std::max(scale(420), pageW * 2 / 5)) : 0; }

// The preview: the selected script's file (colored as epScript), a plugin's lines, or why there is nothing.
void projectPreviewRefresh(bool force = false) {
    if (!gProj.preview) return;
    RECT rc;
    GetClientRect(gProj.wnd, &rc);
    const int pw = projectPreviewWidth(rc.right);
    MoveWindow(gProj.preview, rc.right - pw, 0, pw, rc.bottom, TRUE);
    ShowWindow(gProj.preview, pw ? SW_SHOW : SW_HIDE);
    if (!pw || (!force && gProj.previewOf == gProj.selected)) return;
    gProj.previewOf = gProj.selected;
    std::string text;
    const EudProject* pr = activeProject();
    if (!pr || gProj.selected.empty()) text = "// Select a script to see its code.";
    else {
        const fs::path file = scriptPath(*pr, gProj.selected);
        const std::string ext = file.extension().string();
        std::error_code ec;
        if (ext == ".eps" || ext == ".py") {
            std::ifstream in(file, std::ios::binary);
            if (!in) text = "// The file is missing: " + file.string();
            else {
                text.assign(std::istreambuf_iterator<char>(in), {});
                if (text.size() > 2'000'000) text.resize(2'000'000), text += "\n// ...";
                std::erase(text, '\r');
            }
        } else {  // a plugin: its section of the .eds
            text = "[" + gProj.selected + "]\n";
            for (const auto& l : pr->sectionLines(gProj.selected)) text += l + "\n";
        }
    }
    const std::string rtf = trigCodeRtf(text);
    SETTEXTEX st{ST_DEFAULT, CP_ACP};
    SendMessageW(gProj.preview, EM_SETTEXTEX, WPARAM(&st), LPARAM(rtf.c_str()));
}

void projectFormRefresh() {
    projectListScripts();
    if (gProj.wnd) InvalidateRect(gProj.wnd, nullptr, FALSE);
    projectPreviewRefresh(true);
}
void triggersRefresh(const std::string& select = {}) {
    if (!select.empty()) gProj.selected = select;
    projectFormRefresh();
}
void triggersSave(EudProject& pr, const std::string& select) {
    gProj.selected = select;
    projectSave(pr);
    projectFormRefresh();
}

// Links a script file to the build (R5), from anywhere on disk: by its path relative to the project folder when there
// is one ("../shared/main.eps"), else by its full path. The file is not copied, and StarDraft never writes to it.
std::string triggersAddFile(EudProject& pr, const fs::path& file) {
    const std::string name = pr.linkScript(file);
    triggersSave(pr, name);
    setMessage((EudProject::isLinked(name) ? L"Linked " : L"Added ") + widen(name) + L" to the build");
    return name;
}
void triggersOpenFile(const fs::path& file) {
    // The editor for .eps ("edit", else "open"); Notepad when nothing is registered for it.
    if (INT_PTR(ShellExecuteW(app.main, L"edit", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32 &&
        INT_PTR(ShellExecuteW(app.main, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        ShellExecuteW(app.main, L"open", L"notepad.exe", (L"\"" + file.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}
// A script file's last change, in local time ("2026-10-11 14:02"); empty when it is not found.
std::wstring projectFileTime(const fs::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(file.wstring().c_str(), GetFileExInfoStandard, &a)) return {};
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&a.ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t t[32];
    std::swprintf(t, 32, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return t;
}
// The scripts' commands (also for the self-test): earlier, later, remove.
bool projectMoveScript(const std::string& name, int by) {
    EudProject* pr = activeProject();
    if (!pr || !pr->moveScript(name, by)) return false;
    triggersSave(*pr, name);
    return true;
}
bool projectRemoveScript(const std::string& name) {
    EudProject* pr = activeProject();
    if (!pr) return false;
    if (EudProject::isGenerated(name)) {
        setMessage(L"This code comes from the Data workspace. To remove it, undo the .dat edits.");
        return false;
    }
    pr->removeSection(name);
    triggersSave(*pr, {});
    setMessage(EudProject::isLinked(name) ? L"Removed the link to " + widen(name) + L". StarDraft 2 does not delete the file."
                                          : L"Removed " + widen(name) + L" from the build. The file is still in the project folder.");
    return true;
}
// A failed build: when euddraft names a script's module and line (a linked file, a file it imports, scripts/...), the
// log and the status bar name the file and the line, and the script is selected (R5). The triggers' generated code is
// left to trigShowBuildError (T.1). True when it named one.
bool projectShowScriptError(const std::string& log) {
    EudProject* pr = activeProject();
    EudProject::ScriptError se;
    if (!pr || !pr->scriptError(log, se) || se.file.empty() || se.file == (pr->folder() / "generated" / "triggers.eps").lexically_normal()) return false;
    logLine(L"euddraft found an error in " + fs::path(se.file).make_preferred().wstring() + L", line " + std::to_wstring(se.line) + L": " + widen(se.message), true);
    setMessage(L"euddraft found an error in " + se.file.filename().wstring() + L", line " + std::to_wstring(se.line) + L": " + widen(se.message));
    for (const auto& s : gProj.sections)
        if (pr->scriptFile(s) == se.file) gProj.selected = s;
    InvalidateRect(gProj.wnd, nullptr, FALSE);
    projectPreviewRefresh(true);
    return true;
}

// "Link a file": an .eps (or .py) from anywhere on disk, built as it is.
void projectAddExistingScript() {
    EudProject* pr = activeProject();
    if (!pr) return;
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = app.main;
    ofn.lpstrFilter = L"epScript (*.eps)\0*.eps\0Scripts (*.eps;*.py)\0*.eps;*.py\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Link a file to the build";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) triggersAddFile(*pr, buf);
}

// The output file: relative to the project folder, .scx added; saved to the .eds.
bool projectSetOutput(const std::wstring& text) {
    EudProject* pr = activeProject();
    if (!pr) return false;
    fs::path file = text.empty() ? pr->defaultOutputMap() : fs::path(text);
    if (file.is_relative()) file = pr->folder() / file;
    if (file.extension().empty()) file += ".scx";
    if (file == pr->outputMap()) return true;
    pr->setOutputMap(file);
    projectSave(*pr, L"Build now writes " + fs::path(pr->outputMap()).make_preferred().wstring());
    return true;
}

// The StarCraft folder StarDraft reads game data from (as saved; empty: found automatically).
std::wstring currentInstallFolder() {
    std::ifstream in(settingsPath());
    std::string line;
    std::getline(in, line);
    return widen(line);
}

// Background music: the file copied into the project's music folder, its length measured; bgmplayer plays it
// again when it ends.
void projectChooseMusic() {
    EudProject* pr = activeProject();
    if (!pr) return;
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = app.main;
    ofn.lpstrFilter = L"Sounds (*.ogg;*.wav)\0*.ogg;*.wav\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Background music";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    const fs::path from = buf;
    std::ifstream in(from, std::ios::binary);
    Bytes data((std::istreambuf_iterator<char>(in)), {});
    DecodedSound d;
    if (!decodeSound(data, d) || !d.ms) return setMessage(L"Could not read the sound file");
    std::error_code ec;
    const fs::path dir = pr->folder() / "music";
    fs::create_directories(dir, ec);
    const fs::path to = dir / from.filename();
    if (!fs::equivalent(from, to, ec)) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return setMessage(L"Could not copy the file into the project: " + widen(ec.message()));
    char len[32];
    std::snprintf(len, sizeof len, "%.3f", d.ms / 1000.0);
    pr->setPlugin("bgmplayer", true, {"path : music/" + from.filename().string(), std::string("length : ") + len});
    projectSave(*pr, L"Background music: " + from.filename().wstring());
}
std::string sectionValue(const EudProject& pr, const std::string& section, const std::string& key) {
    for (const auto& l : pr.sectionLines(section)) {
        const size_t c = l.find(':');
        if (c == std::string::npos) continue;
        std::string k = l.substr(0, c), v = l.substr(c + 1);
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        };
        trim(k), trim(v);
        if (k == key) return v;
    }
    return {};
}

// --- epTrace ------------------------------------------------------------------------------------------------
// A trace build makes Build write <map>.epmap. epTrace (beside euddraft.exe; it asks for administrator rights) reads
// it from the map that plays in StarCraft, and writes its report <map>.epmap.prof when its window closes. The page
// shows the lines with the most samples: the lines where the game spends the scripts' time.

// Reads the report again when it changed. True when there is one.
bool projectTraceReport(const EudProject& pr) {
    std::error_code ec;
    const fs::path file = pr.traceReport();
    const auto t = fs::last_write_time(file, ec);
    if (ec) return gProj.hasReport = false, gProj.reportFile.clear(), false;
    if (file != gProj.reportFile || t != gProj.reportTime) {
        std::string err;
        gProj.reportFile = file, gProj.reportTime = t;
        gProj.hasReport = eptrace::read(file, pr.traceMap(), gProj.report, err);
    }
    return gProj.hasReport;
}

// The lines of a project's generated/triggers.eps now, without their indent.
std::vector<std::string> epTraceTriggerCode(const fs::path& folder) {
    std::vector<std::string> out;
    std::ifstream f(folder / "generated" / "triggers.eps");
    for (std::string s; std::getline(f, s);) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
        s.erase(0, s.find_first_not_of(" \t"));
        out.push_back(s);
    }
    return out;
}

// Where a line of a report is: the trigger and block (a line of the triggers' code that is the same now; `trig` is
// set), else the script, the line and the function.
std::wstring epTraceWhere(const eptrace::Line& ln, const fs::path& folder, const std::vector<std::string>& triggerCode, bool* trig = nullptr) {
    const bool t = eptrace::sameFile(ln.file, folder / "generated" / "triggers.eps") && ln.line >= 1 && ln.line <= int(triggerCode.size()) &&
                   triggerCode[size_t(ln.line - 1)] == ln.code;
    if (trig) *trig = t;
    std::wstring where = t ? trigCodeLineName(ln.line) : L"";
    if (where.empty()) where = ln.file.filename().wstring() + L":" + std::to_wstring(ln.line) + (ln.function.empty() ? L"" : L"  " + widen(ln.function));
    return where;
}

// epTrace closed: its report (or that there is none) in the log.
void epTraceLogReport() {
    std::error_code ec;
    const auto made = fs::last_write_time(gProj.epTraceReport, ec);
    eptrace::Report r;
    std::string err;
    if (ec || made < gProj.epTraceStart || !eptrace::read(gProj.epTraceReport, fs::path(gProj.epTraceReport).replace_extension(), r, err)) {
        logLine(L"epTrace closed without a report. Start the map in StarCraft before you start epTrace, and close epTrace's window (not the game) "
                L"to write the report.",
                true);
        setMessage(L"epTrace closed without a report (see the log)");
        return;
    }
    if (r.total == 0) {  // epTrace saw no line run: the traced code runs only at map start, or the map was not playing
        logLine(L"epTrace report: no samples. epTrace did not find a script line that runs. Code in onPluginStart and in triggers that "
                L"run once at map start runs before epTrace can see it; code that runs each trigger cycle (beforeTriggerExec, "
                L"afterTriggerExec, triggers that repeat) shows in the report.",
                true);
        setMessage(L"epTrace closed: no samples (see the log)");
        return;
    }
    logLine(L"epTrace report: " + std::to_wstring(r.total) + L" samples on " + std::to_wstring(r.lines.size()) + L" lines in " + std::to_wstring(r.files) +
            L" scripts (" + fs::path(gProj.epTraceReport).make_preferred().wstring() + L")");
    const auto code = epTraceTriggerCode(gProj.epTraceFolder);
    for (size_t i = 0; i < r.lines.size() && i < 10; ++i) {
        const auto& ln = r.lines[i];
        wchar_t pct[16];
        std::swprintf(pct, 16, L"%5.1f%%", 100.0 * double(ln.samples) / double(std::max<int64_t>(1, r.total)));
        logLine(L"  " + std::wstring(pct) + L"  " + std::to_wstring(ln.samples) + L"  " + epTraceWhere(ln, gProj.epTraceFolder, code) + L"  \x00B7  " +
                widen(ln.code));
    }
    if (r.lines.size() > 10) logLine(L"  (" + std::to_wstring(r.lines.size() - 10) + L" more lines on the Project page and in the report)");
    setMessage(L"epTrace closed: the report is on the Project page and in the log");
}

void projectStartEpTrace() {
    EudProject* pr = activeProject();
    if (!pr || gProj.epTrace) return;
    const auto use = euddraft::find({});
    const fs::path exe = use ? eptrace::exe(use->exe) : fs::path{};
    std::error_code ec;
    if (exe.empty()) return logLine(L"epTrace did not start: epTrace.exe is not in the euddraft folder", true), setMessage(L"epTrace.exe is not in the euddraft folder");
    if (!fs::exists(pr->traceMap(), ec)) return setMessage(L"No trace file: build the map with Trace build on");
    const std::wstring args = L"\"" + pr->traceMap().wstring() + L"\"", dir = pr->traceMap().parent_path().wstring();
    SHELLEXECUTEINFOW si{sizeof si};
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.hwnd = app.main;
    si.lpVerb = L"runas";  // epTrace reads StarCraft's memory: it needs administrator rights (its manifest asks for them)
    si.lpFile = exe.c_str();
    si.lpParameters = args.c_str();
    si.lpDirectory = dir.c_str();
    si.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&si) || !si.hProcess) {
        const DWORD e = GetLastError();
        const std::wstring why = e == ERROR_CANCELLED ? L"epTrace did not start: it needs administrator rights" : L"Could not start epTrace (error " + std::to_wstring(e) + L")";
        logLine(why, true);
        return setMessage(why);
    }
    gProj.epTrace = si.hProcess;
    gProj.epTraceStart = fs::file_time_type::clock::now();
    gProj.epTraceReport = pr->traceReport();
    gProj.epTraceFolder = pr->folder();
    logLine(L"epTrace started with " + pr->traceMap().make_preferred().wstring() + L". Its window shows the samples and the lines that run; close it to "
            L"write the report.");
    SetTimer(gProj.wnd, kEpTraceTimer, 500, nullptr);
    setMessage(L"epTrace is open: play the map, then close epTrace's window (it writes the report then)");
    InvalidateRect(gProj.wnd, nullptr, FALSE);
}

// The epTrace section: the trace file, epTrace, and the lines of the last report with the most samples.
template <class Rel>
int paintEpTrace(HDC mem, const FormLayout& l, int y, int rowH, const EudProject& pr, const Rel& rel) {
    y = ui::sectionHeader(mem, l, y, L"epTrace");
    std::error_code ec;
    const bool built = fs::exists(pr.traceMap(), ec);
    auto label = [&](const wchar_t* text) {
        FormLayout rl = l;
        rl.rowH = rowH;
        ui::fieldLabel(mem, rl, y, text, false);
    };
    auto dimText = [&](RECT t, const std::wstring& text, COLORREF c = kDim, UINT f = DT_LEFT) {
        SetTextColor(mem, c);
        DrawTextW(mem, text.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | f);
    };
    label(L"Trace file");
    {
        RECT t{l.x0 + l.labelW, y, l.colRight - scale(170), y + rowH};
        dimText(t, built ? rel(pr.traceMap()) + L"  \x00B7  " + projectFileTime(pr.traceMap())
                         : pr.traceBuild() ? L"None yet: Build writes it" : L"None: Trace build is off");
        const int bx = l.colRight - ui::textWidth(mem, L"Start epTrace") - scale(20);
        if (gProj.epTrace)
            dimText({bx, y, l.colRight, y + rowH}, L"epTrace is open", kAccent, DT_RIGHT);
        else
            ui::smallButton(mem, bx, y, rowH, L"Start epTrace", [] { projectStartEpTrace(); },
                            L"Start the map in StarCraft first. epTrace asks for administrator rights, shows the lines that run, and writes its "
                            L"report when you close its window",
                            ui::Tone::Normal, built);
    }
    y += rowH + scale(4);
    label(L"Report");
    const bool has = projectTraceReport(pr);
    {
        const auto& r = gProj.report;
        RECT t{l.x0 + l.labelW, y, l.colRight - scale(170), y + rowH};
        if (!has) dimText(t, L"None yet: close epTrace's window to write it");
        else if (r.total == 0) dimText(t, L"No samples: no script line ran each trigger cycle while epTrace looked");
        else
            dimText(t, std::to_wstring(r.total) + L" samples on " + std::to_wstring(r.lines.size()) + L" lines  \x00B7  " + projectFileTime(pr.traceReport()));
        if (has)
            ui::smallButton(mem, l.colRight - ui::textWidth(mem, L"Open the report") - scale(20), y, rowH, L"Open the report", [f = pr.traceReport()] {
                ShellExecuteW(app.main, L"open", L"notepad.exe", (L"\"" + f.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            }, L"Open the report in Notepad, which shows how often epTrace found each script line running");
    }
    y += rowH + scale(4);
    if (!has || gProj.report.lines.empty()) return y + scale(20);
    // The lines with the most samples: the samples (and a bar), where the line is, and its code.
    const auto& lines = gProj.report.lines;
    const int64_t most = lines.front().samples;
    const auto triggerCode = epTraceTriggerCode(pr.folder());  // the triggers' code now: a line of an older build names no trigger
    const int countW = scale(70), barW = scale(60), whereW = std::min(scale(380), (l.colRight - l.x0) / 2);
    for (size_t i = 0; i < lines.size() && i < 15; ++i) {
        const auto& ln = lines[i];
        bool trig = false;
        const std::wstring where = epTraceWhere(ln, pr.folder(), triggerCode, &trig);
        RECT r{l.x0 - scale(6), y, l.colRight, y + rowH};
        const bool over = ui::hot(r);
        ui::listRow(mem, r, false, over);
        int x = r.left + scale(6);
        dimText({x, r.top, x + countW, r.bottom}, std::to_wstring(ln.samples), kText, DT_RIGHT);
        x += countW + scale(10);
        RECT bar{x, r.top + rowH / 2 - scale(3), x + std::max<int>(scale(2), int(barW * ln.samples / std::max<int64_t>(1, most))), r.top + rowH / 2 + scale(3)};
        ui::box(mem, bar, kAccent, CLR_NONE, 3);
        x += barW + scale(12);
        dimText({x, r.top, x + whereW, r.bottom}, where, kText);
        x += whereW + scale(12);
        HGDIOBJ was = SelectObject(mem, monoFont());
        dimText({x, r.top, r.right - scale(8), r.bottom}, widen(ln.code), kDim);
        SelectObject(mem, was);
        const std::wstring tip = std::to_wstring(ln.samples) + L" of " + std::to_wstring(gProj.report.total) + L" samples: " +
                                 fs::path(ln.file).make_preferred().wstring() + L", line " + std::to_wstring(ln.line) +
                                 (trig ? L" (click: the block in the Triggers workspace)" : L" (click: open the script)");
        const fs::path file = ln.file;
        const int line = ln.line;
        hitsOut().push_back({r, nullptr, [file, line, trig] {
                                 if (trig && trigShowCodeLine(line)) return;
                                 triggersOpenFile(file);
                             },
                             tip, IDC_HAND, {}});
        y += rowH;
    }
    return y + scale(24);
}

void paintProjectPage(HWND hwnd) {
    gProj.surface.begin(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    const int pageW = int(rc.right) - projectPreviewWidth(rc.right);
    FormLayout l = formLayout({0, 0, pageW, rc.bottom});
    l.labelW = scale(170), l.colRight = std::min<int>(pageW - l.x0, l.x0 + scale(1000)), l.colW = l.colRight - l.x0;
    // The preview's toggle: </> at the top right of the page, as the Triggers workspace has it.
    ui::iconButton(mem, {pageW - scale(48), scale(10), pageW - scale(16), scale(42)}, UiIcon::Code,
                   [] {
                       gDock.scriptPreview = !gDock.scriptPreview, saveDock();
                       InvalidateRect(gProj.wnd, nullptr, FALSE);
                       projectPreviewRefresh(true);
                   },
                   gDock.scriptPreview ? L"Hide the script's code" : L"Show the selected script's code", ui::Icon::Bare, gDock.scriptPreview);
    const int rowH = scale(32);
    int y = scale(16) - gProj.scroll;
    EudProject* pr = activeProject();
    if (pr) {
        std::error_code ec;
        y = ui::title(mem, l.x0, y, l.colRight, nullptr, widen(pr->name()), fs::exists(pr->outputMap(), ec) ? L"EUD project  \x00B7  built" : L"EUD project  \x00B7  not built yet");
        auto rel = [&](const fs::path& p2) {
            std::error_code e2;
            fs::path r = fs::relative(p2, pr->folder(), e2);
            if (e2 || r.empty() || r.native().rfind(L"..", 0) == 0) r = p2;
            return r.make_preferred().wstring();
        };
        // A row: its label, a field (read only, or one to click), and buttons on the right.
        auto row = [&](const wchar_t* label, const std::wstring& text, std::function<void(RECT)> edit, std::vector<ui::ButtonSpec> buttons, const std::wstring& tip = L"") {
            FormLayout rl = l;
            rl.rowH = rowH;
            ui::fieldLabel(mem, rl, y, label, false);
            int bx = l.colRight;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) bx -= ui::buttonWidth(mem, it->text) + scale(8);
            RECT f{l.x0 + l.labelW, y + scale(2), bx, y + rowH - scale(2)};
            const bool over = edit && ui::hot(f);
            if (edit) ui::fieldBox(mem, f, over ? ui::Field::Hot : ui::Field::Normal);  // read only: the text alone (no box that looks like a field)
            RECT t{f.left + (edit ? scale(8) : 0), f.top, f.right - scale(8), f.bottom};
            SetTextColor(mem, edit ? kText : kDim);
            DrawTextW(mem, text.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            if (edit) hitsOut().push_back({f, nullptr, [f, edit] { edit(f); }, tip, IDC_IBEAM, {}});
            else if (!tip.empty()) hitsOut().push_back({f, nullptr, nullptr, tip, IDC_ARROW, {}});
            if (!buttons.empty()) ui::buttonsRight(mem, l.colRight, y + (rowH - scale(ui::kButtonH)) / 2, buttons);
            y += rowH + scale(4);
        };
        y = ui::sectionHeader(mem, l, y, L"Files");
        row(L"Folder", fs::path(pr->folder()).make_preferred().wstring(), nullptr,
            {{L"Open folder", ui::Tone::Normal, [pr] { ShellExecuteW(app.main, L"open", pr->folder().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }}});
        row(L"Source map", rel(pr->inputMap()), nullptr, {});
        row(L"Output file", rel(pr->outputMap()),
            [](RECT f) {
                EudProject* p = activeProject();
                if (!p) return;
                POINT at{f.left, f.bottom + scale(2)};
                ClientToScreen(gProj.wnd, &at);
                std::string text = narrow(fs::path(p->outputMap()).make_preferred().wstring());
                if (editTextValue(L"Output file", text, at)) projectSetOutput(widen(text));
            },
            {{L"Browse\x2026", ui::Tone::Normal,
              [pr] {
                  const std::wstring file = pickOutputFile(app.main, fs::path(pr->outputMap()).make_preferred().wstring());
                  if (!file.empty()) projectSetOutput(file);
              }},
             {L"Default", ui::Tone::Normal, [pr] { projectSetOutput(fs::path(pr->defaultOutputMap()).wstring()); }, L"In the project folder"}},
            L"Where Build writes the map");
        const std::wstring inst = currentInstallFolder();
        row(L"StarCraft folder", inst.empty() ? L"Found by StarDraft 2" : inst, nullptr, {{L"Change\x2026", ui::Tone::Normal, [] {
                                                                                             chooseInstallFolder();
                                                                                             projectFormRefresh();
                                                                                         }}},
            L"The folder that StarDraft 2 reads game data from");
        const auto use = euddraft::find({});
        row(L"euddraft", use ? widen(use->version) + L"   " + fs::path(use->exe).parent_path().make_preferred().wstring() : L"Downloaded on the first build", nullptr, {});
        y += scale(12);

        // Build options: euddraft's plugins.
        y = ui::sectionHeader(mem, l, y, L"Build");
        {  // the label column names the options (not an empty column)
            FormLayout rl = l;
            rl.rowH = rowH;
            ui::fieldLabel(mem, rl, y, L"Options", false);
        }
        auto option = [&](const wchar_t* label, const char* plugin, const std::wstring& tip, const std::wstring& on, const std::wstring& off) {
            const bool set = pr->plugin(plugin);
            const std::string name = plugin;
            ui::check(mem, {l.x0 + l.labelW, y, l.colRight, y + rowH}, label, set,
                      [name, set, on, off] {
                          EudProject* p = activeProject();
                          if (!p) return;
                          p->setPlugin(name, !set);
                          projectSave(*p, set ? off : on);
                          projectFormRefresh();
                      },
                      tip);
            y += rowH;
        };
        option(L"EUD Turbo", "eudTurbo", L"A shorter trigger cycle: the triggers run more often (euddraft's eudTurbo)", L"EUD Turbo on", L"EUD Turbo off");
        option(L"More units and sprites", "unlimiter", L"Room for more sprites, images, bullets and orders (euddraft's unlimiter)", L"More units and sprites on",
               L"More units and sprites off");
        option(L"Air units do not push apart", "noAirCollision", L"Air units can stay on top of each other (euddraft's noAirCollision)",
               L"Air units do not push apart", L"Air units push apart");
        {  // New graphics for preplaced units (R8): the map's code makes them again at the start
            const bool set = pr->remakeUnits();
            ui::check(mem, {l.x0 + l.labelW, y, l.colRight, y + rowH}, L"New graphics for preplaced units", set,
                      [set] {
                          EudProject* p = activeProject();
                          if (!p) return;
                          p->setRemakeUnits(!set);
                          projectSave(*p, set ? L"New graphics for preplaced units off" : L"New graphics for preplaced units on");
                          projectFormRefresh();
                      },
                      L"The game makes the preplaced units before the .dat edits, so they keep the old graphics. With this option, the map "
                      L"removes each preplaced unit that the edits give other graphics, and makes it again on its place at the start. The map "
                      L"view shows these units with the new graphics. A unit where the game cannot put a unit (on other units, or on ground "
                      L"that it cannot stand on) moves near its place, or the game does not make it again.");
            y += rowH;
        }
        {  // Debug build: the triggers check indexes in the game (T.5)
            const bool set = pr->debugBuild();
            ui::check(mem, {l.x0 + l.labelW, y, l.colRight, y + rowH}, L"Debug build", set,
                      [set] {
                          EudProject* p = activeProject();
                          if (!p) return;
                          p->setDebugBuild(!set);
                          projectSave(*p, set ? L"Debug build off" : L"Debug build on");
                          projectFormRefresh();
                          InvalidateRect(gTopBar, nullptr, FALSE);  // the bug button beside Build
                      },
                      L"The triggers check each list index that the game calculates. An index out of range shows a message on the screen, and the "
                      L"triggers use the first item. Turn it off for the map that you release.");
            y += rowH;
        }
        {  // Trace build: euddraft's debug mode, for epTrace
            const bool set = pr->traceBuild();
            ui::check(mem, {l.x0 + l.labelW, y, l.colRight, y + rowH}, L"Trace build (epTrace)", set,
                      [set] {
                          EudProject* p = activeProject();
                          if (!p) return;
                          p->setTraceBuild(!set);
                          projectSave(*p, set ? L"Trace build off" : L"Trace build on: build the map again to use epTrace");
                          projectFormRefresh();
                          InvalidateRect(gTopBar, nullptr, FALSE);
                      },
                      L"The scripts record each line that they run, and Build writes a trace file beside the map. epTrace reads it while the map "
                      L"plays in StarCraft and shows which lines use the most time. The map runs more slowly: turn it off for the map that you release.");
            y += rowH;
        }
        y += scale(6);
        // Background music (bgmplayer) and files in memory (dataDumper) are off for now: not shown. A project that has
        // them keeps its sections, and Build uses them.
        constexpr bool kMusicAndFiles = false;
        if (kMusicAndFiles) {
            FormLayout rl = l;
            rl.rowH = rowH;
            ui::fieldLabel(mem, rl, y, L"Background music", pr->plugin("bgmplayer"));
            const std::string path = sectionValue(*pr, "bgmplayer", "path"), len = sectionValue(*pr, "bgmplayer", "length");
            std::wstring text = L"None";
            if (!path.empty()) {
                const double secs = std::atof(len.c_str());
                wchar_t t[32];
                std::swprintf(t, 32, L"   %d:%02d", int(secs) / 60, int(secs) % 60);
                text = fs::path(path).filename().wstring() + t;
            }
            const RECT box{l.x0 + l.labelW, y + scale(2), l.colRight - scale(ui::kIconBox + 8), y + rowH - scale(2)};
            ui::pickBox(mem, box, text, false, [](RECT) { projectChooseMusic(); }, L"Plays again and again for every player (euddraft's bgmplayer)");
            if (!path.empty())
                ui::iconButton(mem, {box.right + scale(4), box.top, box.right + scale(4 + ui::kIconBox), box.bottom}, UiIcon::Close,
                               [] {
                                   if (EudProject* p = activeProject()) p->setPlugin("bgmplayer", false), projectSave(*p, L"No background music");
                               },
                               L"No background music", ui::Icon::Bare, false, true, ui::Tone::Danger);
            y += rowH + scale(4);
        }
        if (kMusicAndFiles) {
            // Files in memory (euddraft's dataDumper): each file's bytes at an address, when the map starts.
            FormLayout rl = l;
            rl.rowH = rowH;
            const auto lines = pr->sectionLines("dataDumper");
            ui::fieldLabel(mem, rl, y, L"Files in memory", !lines.empty());
            int ly = y;
            for (size_t i = 0; i < lines.size(); ++i) {
                const size_t c = lines[i].find(':');
                const std::string file = c == std::string::npos ? lines[i] : lines[i].substr(0, c), at = c == std::string::npos ? "" : lines[i].substr(c + 1);
                RECT r{l.x0 + l.labelW - scale(6), ly, l.colRight, ly + rowH};
                ui::listRow(mem, r, false, ui::hot(r));
                SetTextColor(mem, kText);
                RECT t{r.left + scale(6), r.top, r.right - scale(200), r.bottom};
                DrawTextW(mem, widen(file).c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
                SetTextColor(mem, kDim);
                RECT a{r.right - scale(200), r.top, r.right - scale(40), r.bottom};
                DrawTextW(mem, (L"at " + widen(at)).c_str(), -1, &a, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
                ui::rowActions(mem, r, {{UiIcon::Close, L"Remove from the list", [i] {
                                             EudProject* p = activeProject();
                                             if (!p) return;
                                             auto ls = p->sectionLines("dataDumper");
                                             if (i < ls.size()) ls.erase(ls.begin() + long(i));
                                             p->setPlugin("dataDumper", !ls.empty(), ls);
                                             projectSave(*p);
                                         },
                                         ui::Tone::Danger}},
                               false);
                ly += rowH;
            }
            ui::smallButton(mem, l.x0 + l.labelW, ly, rowH, L"Add a file\x2026", [] {
                EudProject* p = activeProject();
                if (!p) return;
                wchar_t buf[MAX_PATH] = L"";
                OPENFILENAMEW ofn{sizeof ofn};
                ofn.hwndOwner = app.main;
                ofn.lpstrFilter = L"All files\0*.*\0";
                ofn.lpstrFile = buf;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrTitle = L"A file to load into memory";
                ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
                if (!GetOpenFileNameW(&ofn)) return;
                std::wstring at = L"0x";
                if (!prompt(L"Memory address (for example 0x58A364)", at) || at.size() < 3) return;
                std::error_code ec;
                fs::path rel = fs::relative(buf, p->folder(), ec);
                const bool inside = !ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0;
                auto ls = p->sectionLines("dataDumper");
                ls.push_back((inside ? rel.generic_string() : narrow(buf)) + " : " + narrow(at));
                p->setPlugin("dataDumper", true, ls);
                projectSave(*p, L"The file loads into memory when the map starts");
            }, L"Loads a file's bytes into the game's memory when the map starts (euddraft's dataDumper)");
            y = ly + rowH + scale(12);
        }

        // The scripts Build runs, in order.
        {
            const int hy = y;
            y = ui::sectionHeader(mem, l, y, L"Scripts");
            hitsOut().push_back({{l.x0, hy, l.x0 + scale(120), hy + l.rowH}, nullptr, nullptr,
                                 L"Build runs these in order, from the top. onPluginStart runs once when the map starts; beforeTriggerExec and afterTriggerExec run each trigger cycle",
                                 IDC_ARROW, {}});
        }
        for (const auto& name : gProj.sections) {
            const fs::path file = scriptPath(*pr, name);
            std::error_code e2;
            const std::string ext = file.extension().string();
            const bool script = ext == ".eps" || ext == ".py", generated = EudProject::isGenerated(name);
            // The note: where the code comes from; for a script file, whether it is found and its last change (R5: a
            // change made in another editor after the last build shows in the accent color).
            const bool linked = script && EudProject::isLinked(name);
            std::wstring note, after;
            bool warn = false;
            if (generated) note = L"from the Data workspace";
            else if (!script) note = L"euddraft plugin";
            else if (!fs::is_regular_file(file, e2)) note = linked ? L"linked  \x00B7  not found: Build stops" : L"not found: Build stops", warn = true;
            else {
                note = (linked ? L"linked  \x00B7  changed " : L"changed ") + projectFileTime(file);
                std::error_code e3;
                const auto built = fs::last_write_time(pr->outputMap(), e3);
                if (!e3 && fs::last_write_time(file, e2) > built) after = L"  after the last build";
            }
            RECT r{l.x0 - scale(6), y, l.colRight, y + rowH};
            const bool on = name == gProj.selected;
            ui::listRow(mem, r, on, ui::hot(r));
            std::wstring tip;
            if (script)
                tip = (linked ? L"A linked file. StarDraft 2 does not copy or change it: " : L"") + fs::path(file).make_preferred().wstring();
            hitsOut().push_back({r, nullptr, [name] {
                                     gProj.selected = name;
                                     InvalidateRect(gProj.wnd, nullptr, FALSE);
                                     projectPreviewRefresh();
                                 },
                                 tip, IDC_HAND, {}});
            const size_t at = size_t(&name - gProj.sections.data());
            ui::reorderRow(mem, "scripts", r, int(at), [](int from, int to) {
                if (from < 0 || size_t(from) >= gProj.sections.size()) return;
                const std::string moved = gProj.sections[size_t(from)];
                for (int k = from; k < to; ++k) projectMoveScript(moved, 1);
                for (int k = from; k > to; --k) projectMoveScript(moved, -1);
            }, !generated);
            HGDIOBJ f2 = SelectObject(mem, monoFont());
            SetTextColor(mem, ui::rowText(on));
            RECT t{l.x0 + scale(16), r.top, r.right - scale(380), r.bottom};
            DrawTextW(mem, widen(name).c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            const int nameRight = t.left + std::min<int>(t.right - t.left, ui::textWidth(mem, widen(name)));
            SelectObject(mem, f2);
            SetTextColor(mem, warn ? kWarn : kDim);
            RECT n{nameRight + scale(16), r.top, r.right - scale(140), r.bottom};
            DrawTextW(mem, note.c_str(), -1, &n, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (!after.empty()) {
                SetTextColor(mem, kAccent);
                RECT na = n;
                na.left = std::min<int>(n.right, n.left + ui::textWidth(mem, note));
                DrawTextW(mem, after.c_str(), -1, &na, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            const size_t idx = size_t(&name - gProj.sections.data());
            std::vector<ui::RowAction> acts;
            if (script) {
                acts.push_back({UiIcon::OpenExternal, L"Open in your editor", [file] { triggersOpenFile(file); }});
                acts.push_back({UiIcon::Folder, L"Show in folder", [file] {
                                    ShellExecuteW(app.main, L"open", L"explorer.exe", (L"/select,\"" + fs::path(file).make_preferred().wstring() + L"\"").c_str(), nullptr,
                                                  SW_SHOWNORMAL);
                                }});
            }
            const bool movable = !generated;
            acts.push_back({UiIcon::MoveUp, L"Earlier", [name] { projectMoveScript(name, -1); }, ui::Tone::Normal, false,
                            movable && idx > 0 && !EudProject::isGenerated(gProj.sections[idx - 1])});
            acts.push_back({UiIcon::MoveDown, L"Later", [name] { projectMoveScript(name, 1); }, ui::Tone::Normal, false, movable && idx + 1 < gProj.sections.size()});
            acts.push_back({UiIcon::Close,
                            generated ? L"Comes from the Data workspace"
                            : linked  ? L"Remove the link. StarDraft 2 does not delete the file."
                                      : L"Remove from the build (the file stays in the project folder)",
                            [name] { projectRemoveScript(name); }, ui::Tone::Danger, false, !generated});
            ui::rowActions(mem, r, acts, on);
            y += rowH;
        }
        ui::reorderDropLine(mem, "scripts");
        y += scale(4);
        const int ax = ui::smallButton(mem, l.x0, y, rowH, L"New script", [] {
            addProjectScript();
            projectFormRefresh();
        });
        ui::smallButton(mem, ax + scale(8), y, rowH, L"Link a file\x2026", [] { projectAddExistingScript(); },
                 L"Add an .eps or .py file from anywhere on disk to the build. StarDraft 2 does not copy or change it.");
        y += rowH + scale(24);
        if (pr->traceBuild() || projectTraceReport(*pr)) y = paintEpTrace(mem, l, y, rowH, *pr, rel);
    }
    gProj.height = y + gProj.scroll;
    if (const int most = std::max(0, gProj.height - int(rc.bottom)); gProj.scroll > most)  // taller window: the bar hides, so no scroll is left
        gProj.scroll = most, InvalidateRect(hwnd, nullptr, FALSE);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, gProj.height - 1), UINT(rc.bottom), gProj.scroll};
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    gProj.surface.end(hwnd);
}

LRESULT CALLBACK projectWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; gProj.surface.message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_TIMER:
        if (wp == kEpTraceTimer && (!gProj.epTrace || WaitForSingleObject(gProj.epTrace, 0) == WAIT_OBJECT_0)) {  // epTrace closed
            if (gProj.epTrace) CloseHandle(gProj.epTrace), gProj.epTrace = nullptr;
            KillTimer(hwnd, kEpTraceTimer);
            epTraceLogReport();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: paintProjectPage(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE), projectPreviewRefresh(); return 0;
    case WM_LBUTTONDBLCLK:  // a script: open it
        if (EudProject* pr = activeProject(); pr && !gProj.selected.empty()) {
            const fs::path file = scriptPath(*pr, gProj.selected);
            const std::string ext = file.extension().string();
            if (ext == ".eps" || ext == ".py") triggersOpenFile(file);
        }
        return 0;
    case WM_MOUSEWHEEL:
    case WM_VSCROLL: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int s = msg == WM_MOUSEWHEEL ? gProj.scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(32) * 3 / WHEEL_DELTA : scrollFromMessage(hwnd, wp, gProj.scroll, scale(32));
        gProj.scroll = std::clamp(s, 0, std::max(0, gProj.height - int(rc.bottom)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void createProjectPage() {
    if (gProj.wnd) return;
    WNDCLASSEXW wc{sizeof wc};
    wc.hInstance = app.inst, wc.hCursor = LoadCursor(nullptr, IDC_ARROW), wc.lpfnWndProc = projectWndProc, wc.lpszClassName = L"StarDraftProjectPage";
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    gProj.wnd = CreateWindowExW(0, L"StarDraftProjectPage", L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, nullptr);
    cleanScroll(gProj.wnd);
    darkControl(gProj.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    LoadLibraryW(L"Msftedit.dll");
    gProj.preview = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 1,
                                    1, gProj.wnd, nullptr, app.inst, nullptr);
    SendMessageW(gProj.preview, WM_SETFONT, WPARAM(monoFont()), TRUE);
    SendMessageW(gProj.preview, EM_EXLIMITTEXT, 0, 0x7FFFFFFF);
    SendMessageW(gProj.preview, EM_SETBKGNDCOLOR, 0, kPanel);
    SendMessageW(gProj.preview, EM_SETTARGETDEVICE, 0, 1);  // no wrapping
    SendMessageW(gProj.preview, EM_SETMARGINS, EC_LEFTMARGIN, MAKELPARAM(scale(10), 0));
    SetWindowSubclass(gProj.preview, trigCodeKeys, 2, 0);  // copy, select all
    darkControl(gProj.preview, L"DarkMode_Explorer");
}
