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
void projectEpTraceArm();

std::wstring trigCodeLineName(int line);
bool trigShowCodeLine(int line);

void projectSave(EudProject& pr, const std::wstring& done = L"") {
    std::string err;
    if (!pr.save(err)) setMessage(L"Could not save the project: " + widen(err));
    else if (!done.empty()) setMessage(done);
    InvalidateRect(gProj.wnd, nullptr, FALSE);
}

// The scripts in build order (the generated .dat code first; [main] is not a script).
void projectListScripts() {
    gProj.sections.clear();
    if (const EudProject* pr = activeProject())
        for (const auto& sec : pr->sections())
            if (sec.name != "main" && !projectManagedSection(sec.name)) gProj.sections.push_back(sec.name);
}

// The preview's view (core/views_project.inc): a kit text field with epScript's colors, in the preview's window.
ProjectPreviewView gProjPreview;

// The script's bytes as the system's code page reads them (the RichEdit did so; the file may be in Korean or another).
std::wstring ansiToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

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
    gProjPreview.setCode(ansiToWide(text), gLightTheme);  // the file's bytes are in the system's code page
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
    platform::FileDialog d;
    d.owner = app.main;
    d.filters = {{"epScript (*.eps)", "*.eps"}, {"Scripts (*.eps;*.py)", "*.eps;*.py"}, {"All files", "*.*"}};
    d.title = narrow(L"Link a file to the build");
    if (const auto picked = platform::openFileDialog(d)) triggersAddFile(*pr, *picked);
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
    platform::FileDialog dlg;
    dlg.owner = app.main;
    dlg.filters = {{"Sounds (*.ogg;*.wav)", "*.ogg;*.wav"}};
    dlg.title = narrow(L"Background music");
    const auto picked = platform::openFileDialog(dlg);
    if (!picked) return;
    const fs::path from = *picked;
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
    projectEpTraceArm();
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

// The path as the Project page shows it: relative to the project's folder, else as it is.
std::wstring projectRel(EudProject& pr, const fs::path& p2) {
    std::error_code e2;
    fs::path r = fs::relative(p2, pr.folder(), e2);
    if (e2 || r.empty() || r.native().rfind(L"..", 0) == 0) r = p2;
    return r.make_preferred().wstring();
}

// The Project page's program parts (core/views_project.inc: ProjectHost): the active project's facts for the Files
// section, and what its buttons and the preview toggle do.
class ProjectProgram final : public ProjectHost {
public:
    Files files() override {
        Files f;
        EudProject* pr = activeProject();
        if (!pr) return f;
        std::error_code ec;
        f.hasProject = true;
        f.built = fs::exists(pr->outputMap(), ec);
        f.name = widen(pr->name());
        f.folder = fs::path(pr->folder()).make_preferred().wstring();
        f.source = projectRel(*pr, pr->inputMap());
        f.output = projectRel(*pr, pr->outputMap());
        const std::wstring inst = currentInstallFolder();
        f.install = inst.empty() ? L"Found by StarDraft 2" : inst;
        const auto use = euddraft::find({});
        f.euddraft = use ? widen(use->version) + L"   " + fs::path(use->exe).parent_path().make_preferred().wstring() : L"Downloaded on the first build";
        return f;
    }
    bool previewOn() override { return gDock.scriptPreview; }
    void togglePreview() override {
        gDock.scriptPreview = !gDock.scriptPreview, saveDock();
        InvalidateRect(gProj.wnd, nullptr, FALSE);
        projectPreviewRefresh(true);
    }
    void openFolder() override {
        if (EudProject* pr = activeProject()) ShellExecuteW(app.main, L"open", pr->folder().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    void editOutput(draw::Rect f) override {
        EudProject* p = activeProject();
        if (!p) return;
        POINT at{f.left, f.bottom + scale(2)};
        ClientToScreen(gProj.wnd, &at);
        std::string text = narrow(fs::path(p->outputMap()).make_preferred().wstring());
        if (editTextValue(L"Output file", text, at)) projectSetOutput(widen(text));
    }
    void browseOutput() override {
        if (EudProject* pr = activeProject()) {
            const std::wstring file = pickOutputFile(app.main, fs::path(pr->outputMap()).make_preferred().wstring());
            if (!file.empty()) projectSetOutput(file);
        }
    }
    void defaultOutput() override {
        if (EudProject* pr = activeProject()) projectSetOutput(fs::path(pr->defaultOutputMap()).wstring());
    }
    void changeInstall() override {
        chooseInstallFolder();
        projectFormRefresh();
    }
    void pageResized() override {
        InvalidateRect(gProj.wnd, nullptr, FALSE);
        projectPreviewRefresh();
    }
    bool buildOption(BuildOption o) override {
        EudProject* p = activeProject();
        if (!p) return false;
        switch (o) {
        case BuildOption::EudTurbo: return p->plugin("eudTurbo");
        case BuildOption::MoreUnits: return p->plugin("unlimiter");
        case BuildOption::NoAirCollision: return p->plugin("noAirCollision");
        case BuildOption::RemakeUnits: return p->remakeUnits();
        case BuildOption::DebugBuild: return p->debugBuild();
        case BuildOption::TraceBuild: return p->traceBuild();
        }
        return false;
    }
    void toggleBuildOption(BuildOption o, bool set) override {
        EudProject* p = activeProject();
        if (!p) return;
        switch (o) {
        case BuildOption::EudTurbo:
            p->setPlugin("eudTurbo", !set);
            projectSave(*p, set ? L"EUD Turbo off" : L"EUD Turbo on");
            break;
        case BuildOption::MoreUnits:
            p->setPlugin("unlimiter", !set);
            projectSave(*p, set ? L"More units and sprites off" : L"More units and sprites on");
            break;
        case BuildOption::NoAirCollision:
            p->setPlugin("noAirCollision", !set);
            projectSave(*p, set ? L"Air units push apart" : L"Air units do not push apart");
            break;
        case BuildOption::RemakeUnits:
            p->setRemakeUnits(!set);
            projectSave(*p, set ? L"New graphics for preplaced units off" : L"New graphics for preplaced units on");
            break;
        case BuildOption::DebugBuild:
            p->setDebugBuild(!set);
            projectSave(*p, set ? L"Debug build off" : L"Debug build on");
            break;
        case BuildOption::TraceBuild:
            p->setTraceBuild(!set);
            projectSave(*p, set ? L"Trace build off" : L"Trace build on: build the map again to use epTrace");
            break;
        }
        projectFormRefresh();
        if (o == BuildOption::DebugBuild || o == BuildOption::TraceBuild) InvalidateRect(gTopBar, nullptr, FALSE);  // the bug button beside Build
    }
    Music music() override {
        Music m;
        EudProject* pr = activeProject();
        if (!pr) return m;
        m.on = pr->plugin("bgmplayer");
        const std::string path = sectionValue(*pr, "bgmplayer", "path"), len = sectionValue(*pr, "bgmplayer", "length");
        m.hasFile = !path.empty();
        if (m.hasFile) m.name = fs::path(path).filename().wstring(), m.secs = std::atof(len.c_str());
        return m;
    }
    void chooseMusic() override { projectChooseMusic(); }
    void clearMusic() override {
        if (EudProject* p = activeProject()) p->setPlugin("bgmplayer", false), projectSave(*p, L"No background music");
    }
    std::vector<MemoryFile> memoryFiles() override {
        std::vector<MemoryFile> out;
        EudProject* pr = activeProject();
        if (!pr) return out;
        for (const std::string& line : pr->sectionLines("dataDumper")) {
            const size_t c = line.find(':');
            const std::string file = c == std::string::npos ? line : line.substr(0, c), at = c == std::string::npos ? "" : line.substr(c + 1);
            out.push_back({widen(file), widen(at)});
        }
        return out;
    }
    void removeMemoryFile(size_t i) override {
        EudProject* p = activeProject();
        if (!p) return;
        auto ls = p->sectionLines("dataDumper");
        if (i < ls.size()) ls.erase(ls.begin() + long(i));
        p->setPlugin("dataDumper", !ls.empty(), ls);
        projectSave(*p);
    }
    void addMemoryFile() override {
        EudProject* p = activeProject();
        if (!p) return;
        platform::FileDialog d;
        d.owner = app.main;
        d.filters = {{"All files", "*.*"}};
        d.title = narrow(L"A file to load into memory");
        const auto picked = platform::openFileDialog(d);
        if (!picked) return;
        const std::wstring buf = picked->wstring();
        std::wstring at = L"0x";
        if (!prompt(L"Memory address (for example 0x58A364)", at) || at.size() < 3) return;
        std::error_code ec;
        fs::path rel = fs::relative(buf, p->folder(), ec);
        const bool inside = !ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0;
        auto ls = p->sectionLines("dataDumper");
        ls.push_back((inside ? rel.generic_string() : narrow(buf)) + " : " + narrow(at));
        p->setPlugin("dataDumper", true, ls);
        projectSave(*p, L"The file loads into memory when the map starts");
    }
    std::vector<ScriptRow> scripts() override {
        std::vector<ScriptRow> out;
        EudProject* pr = activeProject();
        if (!pr) return out;
        for (const std::string& name : gProj.sections) {
            ScriptRow s;
            s.name = name;
            s.selected = name == gProj.selected;
            const fs::path file = scriptPath(*pr, name);
            const std::string ext = file.extension().string();
            s.script = ext == ".eps" || ext == ".py", s.generated = EudProject::isGenerated(name);
            s.linked = s.script && EudProject::isLinked(name);
            if (s.script) s.file = fs::path(file).make_preferred().wstring();
            if (s.script && !s.generated) {
                std::error_code e2;
                s.found = fs::is_regular_file(file, e2);
                if (s.found) {
                    s.changed = projectFileTime(file);
                    std::error_code e3;
                    const auto built = fs::last_write_time(pr->outputMap(), e3);
                    s.afterBuild = !e3 && fs::last_write_time(file, e2) > built;
                }
            }
            out.push_back(std::move(s));
        }
        return out;
    }
    void selectScript(const std::string& name) override {
        gProj.selected = name;
        InvalidateRect(gProj.wnd, nullptr, FALSE);
        projectPreviewRefresh();
    }
    void openScript(const std::string& name) override {
        if (EudProject* pr = activeProject()) triggersOpenFile(scriptPath(*pr, name));
    }
    void showScript(const std::string& name) override {
        if (EudProject* pr = activeProject()) {
            const fs::path file = scriptPath(*pr, name);
            ShellExecuteW(app.main, L"open", L"explorer.exe", (L"/select,\"" + fs::path(file).make_preferred().wstring() + L"\"").c_str(), nullptr,
                          SW_SHOWNORMAL);
        }
    }
    void moveScript(const std::string& name, int by) override { projectMoveScript(name, by); }
    void removeScript(const std::string& name) override { projectRemoveScript(name); }
    void newScript() override {
        addProjectScript();
        projectFormRefresh();
    }
    void linkScript() override { projectAddExistingScript(); }
};
ProjectProgram gProjProgram;
ProjectPageView gProjView(gProjProgram);

// epTrace's timer: a tick every 500 ms while it is open (the page's own timer, through its view's host), and the report
// when it closes.
void projectEpTraceTick() {
    if (!gProj.epTrace || WaitForSingleObject(gProj.epTrace, 0) == WAIT_OBJECT_0) {  // epTrace closed
        if (gProj.epTrace) CloseHandle(gProj.epTrace), gProj.epTrace = nullptr;
        epTraceLogReport();
        InvalidateRect(gProj.wnd, nullptr, FALSE);
        return;
    }
    projectEpTraceArm();
}
void projectEpTraceArm() {
    if (auto* h = gProjView.host()) h->timer(500, projectEpTraceTick);
}

// The Project page's sections after Files (shell.h's seam): the old GDI code, on the canvas's DC, until they move into the
// core. Their clicks, tips and cursors are the page's surface (gProj.surface); the view's own parts come first.
int shellPaintProjectRest(draw::Canvas& c, int y, int pageW, int pageH) {
    auto* gc = dynamic_cast<ui::GdiCanvas*>(&c);
    EudProject* pr = activeProject();
    if (!gc || !pr) return y;
    HDC mem = gc->dc();
    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, app.font);
    FormLayout l = formLayout({0, 0, pageW, pageH});
    l.labelW = scale(170), l.colRight = std::min<int>(pageW - l.x0, l.x0 + scale(1000)), l.colW = l.colRight - l.x0;
    const int rowH = scale(32);
    auto rel = [&](const fs::path& p2) { return projectRel(*pr, p2); };
    gProj.surface.hits.clear();
    std::vector<DataWidgetHit>* savedOut = gHitsOut;
    HWND savedWnd = gHitsWnd;
    gHitsOut = &gProj.surface.hits, gHitsWnd = gProj.wnd;
    gHitsOf[gProj.wnd] = &gProj.surface.hits;
    if (pr->traceBuild() || projectTraceReport(*pr)) y = paintEpTrace(mem, l, y, rowH, *pr, rel);
    gHitsOut = savedOut, gHitsWnd = savedWnd;
    return y;
}

std::wstring shellProjectRestTip(draw::Point pt) {
    const POINT p{pt.x, pt.y};
    for (auto it = gProj.surface.hits.rbegin(); it != gProj.surface.hits.rend(); ++it)
        if (PtInRect(&it->r, p) && !it->tip.empty()) return it->tip;
    return {};
}

draw::Cursor shellProjectRestCursor(draw::Point pt) {
    const POINT p{pt.x, pt.y};
    for (auto it = gProj.surface.hits.rbegin(); it != gProj.surface.hits.rend(); ++it)
        if (PtInRect(&it->r, p) && (it->click || it->drag)) return it->cursor.c;
    return draw::Cursor::Arrow;
}

bool shellProjectRestMouse(const draw::MouseEvent& e) {
    if (e.button != 0 && e.kind != draw::MouseKind::Move && e.kind != draw::MouseKind::Leave) return false;
    UINT msg = 0;
    switch (e.kind) {
    case draw::MouseKind::Move: msg = WM_MOUSEMOVE; break;
    case draw::MouseKind::Leave: msg = WM_MOUSELEAVE; break;
    case draw::MouseKind::Down: msg = WM_LBUTTONDOWN; break;
    case draw::MouseKind::Up: msg = WM_LBUTTONUP; break;
    case draw::MouseKind::DoubleClick:  // a script: open it
        if (EudProject* pr = activeProject(); pr && !gProj.selected.empty()) {
            const fs::path file = scriptPath(*pr, gProj.selected);
            const std::string ext = file.extension().string();
            if (ext == ".eps" || ext == ".py") triggersOpenFile(file);
        }
        return true;
    default: return false;
    }
    LRESULT r = 0;
    return gProj.surface.message(gProj.wnd, msg, 0, MAKELPARAM(e.pt.x, e.pt.y), r);
}

void createProjectPage() {
    if (gProj.wnd) return;
    gProj.wnd = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, app.main, nullptr, app.inst, &gProjView);
    cleanScroll(gProj.wnd);
    darkControl(gProj.wnd, L"DarkMode_Explorer");  // the scroll bar in the theme
    gProj.preview = CreateWindowExW(0, ui::kViewClass, L"", WS_CHILD, 0, 0, 1, 1, gProj.wnd, nullptr, app.inst, &gProjPreview);
}
