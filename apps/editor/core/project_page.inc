// The Project workspace's program parts (portable core; moved from project_view.inc): the active project's facts for the
// page (core/views_project.inc: ProjectPageView), what its buttons do (the build options, the scripts in build order,
// the output file, the background music, the files loaded into memory, epTrace's report), the script preview's text, and
// the commands that make a project (createProject, File > Project > Add a script). The host shows the page and the
// preview (project_view.inc on Windows, sdl_project.inc on SDL): it defines projectPreviewPlace and the Triggers
// workspace's two functions below. Included by core.inc at its end, and by main.cpp before project_view.inc.

std::wstring trigCodeLineName(int line);  // the trigger and block of a line of the triggers' code ("" when none)
bool trigShowCodeLine(int line);          // that block is shown in the Triggers workspace (false: none is)
// The host places the preview beside the page (projectPreviewWidth of the page's width) and shows it, or hides it.
// Returns the preview's width: 0 when it is hidden or the page has no preview yet.
int projectPreviewPlace();
// A sound file's length in milliseconds (0 when it cannot be read): the host decodes it (the background music).
uint32_t soundFileMs(const Bytes& data);

// The page's state: the scripts in build order, the selected one, the preview's script, and epTrace's process and report.
struct ProjectPage {
    std::string previewOf;              // what the preview shows
    std::string selected;               // a script (its .eds section name)
    std::vector<std::string> sections;  // the scripts, in build order
    std::uintptr_t epTrace = 0;         // epTrace's process while it is open (the seam's token)
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

// The preview's view (core/views_project.inc): a kit text field with epScript's colors, beside the page.
ProjectPreviewView gProjPreview;

class ProjectProgram;
extern ProjectPageView gProjView;

void projectSave(EudProject& pr, const std::wstring& done = L"") {
    std::string err;
    if (!pr.save(err)) setMessage(L"Could not save the project: " + widen(err));
    else if (!done.empty()) setMessage(done);
    gProjView.invalidate();
}

// The scripts in build order (the generated .dat code first; [main] is not a script).
void projectListScripts() {
    gProj.sections.clear();
    if (const EudProject* pr = activeProject())
        for (const auto& sec : pr->sections())
            if (sec.name != "main" && !projectManagedSection(sec.name)) gProj.sections.push_back(sec.name);
}

// The preview: the selected script's file (colored as epScript), a plugin's lines, or why there is nothing.
void projectPreviewRefresh(bool force = false) {
    const int pw = projectPreviewPlace();
    if (!pw || (!force && gProj.previewOf == gProj.selected)) return;
    gProj.previewOf = gProj.selected;
    std::string text;
    const EudProject* pr = activeProject();
    if (!pr || gProj.selected.empty()) text = "// Select a script to see its code.";
    else {
        const fs::path file = pr->scriptFile(gProj.selected);
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
    gProjPreview.setCode(shellCodePageText(text), gLightTheme);  // the file's bytes are in the system's code page
}

void projectFormRefresh() {
    projectListScripts();
    gProjView.invalidate();
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
void triggersOpenFile(const fs::path& file) { shellOpenInEditor(file); }
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
    shellLogError(L"euddraft found an error in " + fs::path(se.file).make_preferred().wstring() + L", line " + std::to_wstring(se.line) + L": " + widen(se.message));
    setMessage(L"euddraft found an error in " + se.file.filename().wstring() + L", line " + std::to_wstring(se.line) + L": " + widen(se.message));
    for (const auto& s : gProj.sections)
        if (pr->scriptFile(s) == se.file) gProj.selected = s;
    gProjView.invalidate();
    projectPreviewRefresh(true);
    return true;
}

// File > Project > Add a script: a script in the project (a new one gets an empty onPluginStart), from the save dialog.
void addProjectScript() {
    EudProject* pr = activeProject();
    if (!pr) return;
    ShellFilePick d;
    d.filters = {{L"epScript (*.eps)", L"*.eps"}, {L"eudplib Python (*.py)", L"*.py"}};
    d.fileName = L"new.eps";
    d.initialDir = pr->folder() / "scripts";
    d.title = L"Add a script: select one, or type the name of a new one";
    d.defaultExtension = L"eps";
    d.save = true;
    const fs::path file = shellPickFile(d);
    if (file.empty()) return;
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        std::ofstream f(file, std::ios::binary);
        f << "// " << file.filename().string() << "\n\nfunction onPluginStart() {\n}\n";
        f.close();
        if (!f) return shellErrorBox(L"Could not write " + file.wstring(), L"Add a script");
    }
    fs::path rel = fs::relative(file, pr->folder(), ec);
    if (ec || rel.empty() || rel.native().rfind(fs::path("..").native(), 0) == 0) rel = file;  // outside the folder: keep the full path
    pr->addScript(rel.generic_string());
    std::string err;
    if (!pr->save(err)) shellErrorBox(widen(err), L"Could not save the project");
    else setMessage(L"Added " + rel.wstring() + L" to the project");
    triggersRefresh(rel.generic_string());
}

// "Link a file": an .eps (or .py) from anywhere on disk, built as it is.
void projectAddExistingScript() {
    EudProject* pr = activeProject();
    if (!pr) return;
    ShellFilePick d;
    d.filters = {{L"epScript (*.eps)", L"*.eps"}, {L"Scripts (*.eps;*.py)", L"*.eps;*.py"}, {L"All files", L"*.*"}};
    d.title = L"Link a file to the build";
    if (const fs::path picked = shellPickFile(d); !picked.empty()) triggersAddFile(*pr, picked);
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

// Background music: the file copied into the project's music folder, its length measured; bgmplayer plays it
// again when it ends.
void projectChooseMusic() {
    EudProject* pr = activeProject();
    if (!pr) return;
    ShellFilePick dlg;
    dlg.filters = {{L"Sounds (*.ogg;*.wav)", L"*.ogg;*.wav"}};
    dlg.title = L"Background music";
    const fs::path from = shellPickFile(dlg);
    if (from.empty()) return;
    std::ifstream in(from, std::ios::binary);
    const Bytes data((std::istreambuf_iterator<char>(in)), {});
    const uint32_t ms = soundFileMs(data);
    if (!ms) return setMessage(L"Could not read the sound file");
    std::error_code ec;
    const fs::path dir = pr->folder() / "music";
    fs::create_directories(dir, ec);
    const fs::path to = dir / from.filename();
    if (!fs::equivalent(from, to, ec)) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return setMessage(L"Could not copy the file into the project: " + widen(ec.message()));
    char len[32];
    std::snprintf(len, sizeof len, "%.3f", ms / 1000.0);
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
        shellLogError(L"epTrace closed without a report. Start the map in StarCraft before you start epTrace, and close epTrace's window (not the game) "
                      L"to write the report.");
        setMessage(L"epTrace closed without a report (see the log)");
        return;
    }
    if (r.total == 0) {  // epTrace saw no line run: the traced code runs only at map start, or the map was not playing
        shellLogError(L"epTrace report: no samples. epTrace did not find a script line that runs. Code in onPluginStart and in triggers that "
                      L"run once at map start runs before epTrace can see it; code that runs each trigger cycle (beforeTriggerExec, "
                      L"afterTriggerExec, triggers that repeat) shows in the report.");
        setMessage(L"epTrace closed: no samples (see the log)");
        return;
    }
    shellLogLine(L"epTrace report: " + std::to_wstring(r.total) + L" samples on " + std::to_wstring(r.lines.size()) + L" lines in " + std::to_wstring(r.files) +
                 L" scripts (" + fs::path(gProj.epTraceReport).make_preferred().wstring() + L")");
    const auto code = epTraceTriggerCode(gProj.epTraceFolder);
    for (size_t i = 0; i < r.lines.size() && i < 10; ++i) {
        const auto& ln = r.lines[i];
        wchar_t pct[16];
        std::swprintf(pct, 16, L"%5.1f%%", 100.0 * double(ln.samples) / double(std::max<int64_t>(1, r.total)));
        shellLogLine(L"  " + std::wstring(pct) + L"  " + std::to_wstring(ln.samples) + L"  " + epTraceWhere(ln, gProj.epTraceFolder, code) + L"  \x00B7  " +
                     widen(ln.code));
    }
    if (r.lines.size() > 10) shellLogLine(L"  (" + std::to_wstring(r.lines.size() - 10) + L" more lines on the Project page and in the report)");
    setMessage(L"epTrace closed: the report is on the Project page and in the log");
}

void projectStartEpTrace() {
    EudProject* pr = activeProject();
    if (!pr || gProj.epTrace) return;
    const auto use = euddraft::find({});
    const fs::path exe = use ? eptrace::exe(use->exe) : fs::path{};
    std::error_code ec;
    if (exe.empty()) return shellLogError(L"epTrace did not start: epTrace.exe is not in the euddraft folder"), setMessage(L"epTrace.exe is not in the euddraft folder");
    if (!fs::exists(pr->traceMap(), ec)) return setMessage(L"No trace file: build the map with Trace build on");
    // epTrace reads StarCraft's memory: it is started with administrator rights (the seam asks for them).
    const std::wstring args = L"\"" + pr->traceMap().wstring() + L"\"", dir = pr->traceMap().parent_path().wstring();
    std::wstring why;
    const std::uintptr_t proc = shellStartElevated(exe, dir, args, L"epTrace", why);
    if (!proc) {
        shellLogError(why);
        return setMessage(why);
    }
    gProj.epTrace = proc;
    gProj.epTraceStart = fs::file_time_type::clock::now();
    gProj.epTraceReport = pr->traceReport();
    gProj.epTraceFolder = pr->folder();
    shellLogLine(L"epTrace started with " + pr->traceMap().make_preferred().wstring() + L". Its window shows the samples and the lines that run; close it to "
                 L"write the report.");
    projectEpTraceArm();
    setMessage(L"epTrace is open: play the map, then close epTrace's window (it writes the report then)");
    gProjView.invalidate();
}

// The path as the Project page shows it: relative to the project's folder, else as it is.
std::wstring projectRel(EudProject& pr, const fs::path& p2) {
    std::error_code e2;
    fs::path r = fs::relative(p2, pr.folder(), e2);
    if (e2 || r.empty() || r.native().rfind(fs::path("..").native(), 0) == 0) r = p2;
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
        const std::wstring inst = shellInstallFolderText();
        f.install = inst.empty() ? L"Found by StarDraft 2" : inst;
        const auto use = euddraft::find({});
        f.euddraft = use ? widen(use->version) + L"   " + fs::path(use->exe).parent_path().make_preferred().wstring() : L"Downloaded on the first build";
        return f;
    }
    bool previewOn() override { return gDock.scriptPreview; }
    void togglePreview() override {
        gDock.scriptPreview = !gDock.scriptPreview, saveDock();
        gProjView.invalidate();
        projectPreviewRefresh(true);
    }
    void openFolder() override {
        if (EudProject* pr = activeProject()) shellOpenFolder(pr->folder());
    }
    void editOutput(draw::Rect f) override {
        EudProject* p = activeProject();
        if (!p) return;
        std::string text = narrow(fs::path(p->outputMap()).make_preferred().wstring());
        if (shellEditValueBelow(gProjView, f, L"Output file", text)) projectSetOutput(widen(text));
    }
    void browseOutput() override {
        if (EudProject* pr = activeProject()) {
            const std::wstring file = shellPickOutputFile(fs::path(pr->outputMap()).make_preferred().wstring());
            if (!file.empty()) projectSetOutput(file);
        }
    }
    void defaultOutput() override {
        if (EudProject* pr = activeProject()) projectSetOutput(fs::path(pr->defaultOutputMap()).wstring());
    }
    void changeInstall() override {
        shellChooseInstallFolder();
        projectFormRefresh();
    }
    void pageResized() override {
        gProjView.invalidate();
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
        case BuildOption::Freeze: return p->freeze();
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
        case BuildOption::Freeze:
            p->setFreeze(!set);
            projectSave(*p, set ? L"Freeze protection off" : L"Freeze protection on");
            break;
        }
        projectFormRefresh();
        if (o == BuildOption::DebugBuild || o == BuildOption::TraceBuild) shellMenusChanged();  // the bug button beside Build
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
        ShellFilePick d;
        d.filters = {{L"All files", L"*.*"}};
        d.title = L"A file to load into memory";
        const fs::path picked = shellPickFile(d);
        if (picked.empty()) return;
        const std::wstring buf = picked.wstring();
        std::wstring at = L"0x";
        if (!shellPromptText(L"Memory address (for example 0x58A364)", at) || at.size() < 3) return;
        std::error_code ec;
        fs::path rel = fs::relative(buf, p->folder(), ec);
        const bool inside = !ec && !rel.empty() && rel.native().rfind(fs::path("..").native(), 0) != 0;
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
            const fs::path file = pr->scriptFile(name);
            const std::string ext = file.extension().string();
            s.script = ext == ".eps" || ext == ".py", s.generated = EudProject::isGenerated(name);
            s.linked = s.script && EudProject::isLinked(name);
            if (s.script) s.file = fs::path(file).make_preferred().wstring();
            if (s.script && !s.generated) {
                std::error_code e2;
                s.found = fs::is_regular_file(file, e2);
                if (s.found) {
                    s.changed = shellFileTimeText(file);
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
        gProjView.invalidate();
        projectPreviewRefresh();
    }
    void openScript(const std::string& name) override {
        if (EudProject* pr = activeProject()) triggersOpenFile(pr->scriptFile(name));
    }
    void showScript(const std::string& name) override {
        if (EudProject* pr = activeProject()) shellRevealFile(pr->scriptFile(name));
    }
    void moveScript(const std::string& name, int by) override { projectMoveScript(name, by); }
    void removeScript(const std::string& name) override { projectRemoveScript(name); }
    void newScript() override {
        addProjectScript();
        projectFormRefresh();
    }
    void linkScript() override { projectAddExistingScript(); }
    void openSelectedScript() override {
        EudProject* pr = activeProject();
        if (!pr || gProj.selected.empty()) return;
        const fs::path file = pr->scriptFile(gProj.selected);
        const std::string ext = file.extension().string();
        if (ext == ".eps" || ext == ".py") triggersOpenFile(file);
    }
    // epTrace's section: the trace file, the report and its lines with the most samples (the page draws them).
    Trace trace() override {
        Trace t;
        EudProject* pr = activeProject();
        if (!pr) return t;
        t.open = gProj.epTrace != 0;
        t.traceBuild = pr->traceBuild();
        t.hasReport = projectTraceReport(*pr);
        t.shown = t.traceBuild || t.hasReport;
        if (!t.shown) return t;
        std::error_code ec;
        t.built = fs::exists(pr->traceMap(), ec);
        if (t.built) t.traceFile = projectRel(*pr, pr->traceMap()) + L"  \x00B7  " + shellFileTimeText(pr->traceMap());
        if (!t.hasReport) return t;
        const auto& r = gProj.report;
        t.total = r.total, t.lineCount = r.lines.size();
        t.reportTime = shellFileTimeText(pr->traceReport());
        const auto triggerCode = epTraceTriggerCode(pr->folder());  // the triggers' code now: a line of an older build names no trigger
        for (size_t i = 0; i < r.lines.size() && i < 15; ++i) {
            const auto& ln = r.lines[i];
            bool trig = false;
            TraceLine tl;
            tl.samples = ln.samples;
            tl.where = epTraceWhere(ln, pr->folder(), triggerCode, &trig);
            tl.code = widen(ln.code);
            tl.tip = std::to_wstring(ln.samples) + L" of " + std::to_wstring(r.total) + L" samples: " +
                     fs::path(ln.file).make_preferred().wstring() + L", line " + std::to_wstring(ln.line) +
                     (trig ? L" (click: the block in the Triggers workspace)" : L" (click: open the script)");
            t.lines.push_back(std::move(tl));
        }
        return t;
    }
    void startEpTrace() override { projectStartEpTrace(); }
    void openReport() override {
        if (EudProject* pr = activeProject()) shellOpenWithNotepad(pr->traceReport());
    }
    void openTraceLine(size_t i) override {
        EudProject* pr = activeProject();
        if (!pr || i >= gProj.report.lines.size()) return;
        const auto& ln = gProj.report.lines[i];
        bool trig = false;
        epTraceWhere(ln, pr->folder(), epTraceTriggerCode(pr->folder()), &trig);
        if (trig && trigShowCodeLine(ln.line)) return;
        triggersOpenFile(ln.file);
    }
};
ProjectProgram gProjProgram;
ProjectPageView gProjView(gProjProgram);

// epTrace's timer: a tick every 500 ms while it is open (the page's own timer, through its view's host), and the report
// when it closes.
void projectEpTraceTick() {
    if (!gProj.epTrace || shellProcessEnded(gProj.epTrace)) {  // epTrace closed
        if (gProj.epTrace) shellProcessClose(gProj.epTrace), gProj.epTrace = 0;
        epTraceLogReport();
        gProjView.invalidate();
        return;
    }
    projectEpTraceArm();
}
void projectEpTraceArm() {
    if (auto* h = gProjView.host()) h->timer(500, projectEpTraceTick);
}

// --- Making a project ---------------------------------------------------------------------------------------------

// New map of a tileset, filled with one terrain, in a tab of its own.
void newMap(uint16_t width, uint16_t height, uint16_t era, uint16_t terrain) {
    AppState& app = shellState();
    auto ed = std::make_unique<Editor>(app.data);
    std::string err;
    loadingStart(tr(L"Creating map\x2026"));
    shellWaitCursor();
    if (!loadingTimed(tr(L"Creating MTXM and ISOM\x2026"), 0.8f, [&] { return ed->newMap(width, height, era, terrain, err); })) {
        loadingEnd();
        shellErrorBox(widen(err), L"Could not create the map");
        return;
    }
    loadingTimed(tr(L"Creating tab and minimap\x2026"), 1.0f, [&] { return addDocument(std::move(ed), false), 0; });
    loadingEnd();
    const IsomTerrainTable* table = IsomTerrainTable::forEra(era);
    setMessage(L"Created a new " + std::wstring(kTilesetNames[era & 7]) + L" map (" + std::to_wstring(width) + L"x" + std::to_wstring(height) + L", " +
               widen(table ? table->terrainName(terrain) : "") + L")");
}

// The default new map: badlands dirt, 128x128 (startup, the + tab button).
void newMap() { newMap(128, 128, 0, 1); }

// The map Build writes, as the New page and the New EUD project dialog take it: empty (the project's default), a path
// relative to the project's folder (<location>/<name>), with .scx added when it has no extension.
fs::path projectOutputFile(const std::wstring& location, const std::wstring& name, const std::wstring& output) {
    fs::path out = output;
    if (!out.empty() && out.is_relative()) out = fs::path(location) / name / out;
    if (!out.empty() && out.extension().empty()) out += ".scx";
    return out;
}

// Makes the project folder and moves the map into it (the open map, or a new blank one); the tab
// becomes the project's. `outputFile`: the map Build writes (empty: the project's default).
bool createProject(const fs::path& parent, const std::string& name, bool fromOpenMap, std::string& err, const fs::path& outputFile) {
    AppState& app = shellState();
    if (fromOpenMap && (!app.ed || !app.ed->isOpen())) fromOpenMap = false;
    if (fromOpenMap && !app.ed->chk().find("UNIx")) {
        err = "The open map is in original StarCraft format; euddraft needs a Brood War map. Start from a new blank map instead.";
        return false;
    }
    EudProject pr;
    std::error_code ec;
    fs::create_directories(parent, ec);
    if (!EudProject::create(parent, name, {}, pr, err)) return false;
    if (!outputFile.empty()) {
        pr.setOutputMap(outputFile);
        if (!pr.save(err)) return false;
    }
    if (!fromOpenMap) newMap();
    if (!app.ed->save(pr.inputMap(), err)) return false;
    gDocs[size_t(gActive)].project = std::make_shared<EudProject>(pr);
    updateTitle();
    addRecent(gRecentProjects, pr.folder());
    rememberProjectsFolder(pr.folder());
    // L.4: the map's classic triggers, settings and briefings stay in the project's map (the built map keeps them).
    const bool classic = fromOpenMap && (!app.ed->classicTriggers().triggers.empty() || !app.ed->chk().effective("MBRF").empty());
    setMessage(L"Created the project " + widen(name) + L" in " + pr.folder().wstring() + L" (Build: Ctrl+B)" +
               (classic ? L". The project's map keeps its classic triggers, unit, upgrade and technology settings, and briefings. The side bar's Classic shows the triggers." : L""));
    return true;
}

// Save for an unsaved project (core/documents.inc), to `chosen` (<folder>/<name>.stardraft: the project goes to
// <folder>/<name>/): everything in its folder moves there, and the tab goes on as that project. The host asks for
// `chosen` (its save dialog). A problem shows in a message box (Save project); false then.
bool saveUnsavedProjectTo(const fs::path& chosen) {
    AppState& app = shellState();
    EudProject* pr = activeProject();
    if (!pr || !app.ed) return false;
    std::error_code ec;
    const std::string name = chosen.stem().string();
    const fs::path dest = chosen.parent_path() / chosen.stem();
    if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos) return shellWarningBox(L"The name cannot hold \\ / : * ? \" < > |", L"Save project"), false;
    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        shellWarningBox(dest.wstring() + L" is there already and is not empty.", L"Save project");
        return false;
    }
    std::string err;
    if (!app.ed->save(pr->inputMap(), err)) return shellErrorBox(widen(err), L"Could not save the map"), false;
    // Everything in the folder, with the project's own files named after it.
    const fs::path from = pr->folder();
    fs::create_directories(dest, ec);
    for (auto it = fs::recursive_directory_iterator(from, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), from, ec);
        const std::string r = rel.generic_string();
        if (r == "Untitled_EUD.scx" || r == "Untitled.eds") continue;  // Build makes them again
        fs::path to = dest / rel;
        if (r == "Untitled.scx") to = dest / (name + ".scx");
        if (it->is_directory(ec)) {
            fs::create_directories(to, ec);
            continue;
        }
        if (r == EudProject::kFileName) {  // its name, map and output by the new name
            std::ifstream in(it->path(), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), {});
            for (const auto& [a, b] : {std::pair<std::string, std::string>{"Untitled_EUD.scx", name + "_EUD.scx"}, {"Untitled.scx", name + ".scx"},
                                       {"name: Untitled", "name: " + name}, {"Untitled.eds", name + ".eds"}})
                for (size_t p = text.find(a); p != std::string::npos; p = text.find(a, p + b.size())) text.replace(p, a.size(), b);
            std::ofstream out(dest / EudProject::kFileName, std::ios::binary);
            out << text;
            out.close();
            if (!out) return shellErrorBox(L"Could not write " + (dest / EudProject::kFileName).wstring(), L"Save project"), false;
            continue;
        }
        fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
        if (ec) return shellErrorBox(L"Could not copy " + it->path().wstring() + L": " + widen(ec.message()), L"Save project"), false;
    }
    // The tab goes on as the saved project (the same project object: the workspaces keep it).
    const fs::path oldParent = from.parent_path();
    if (!pr->open(dest / EudProject::kFileName, err) || !app.ed->save(pr->inputMap(), err)) return shellErrorBox(widen(err), L"Save project"), false;
    fs::remove_all(oldParent, ec);
    addRecent(gRecentProjects, pr->folder());
    rememberProjectsFolder(pr->folder());
    updateTitle();
    setWorkspace(gWorkspace);  // the workspaces read the project from its new folder
    shellInvalidateTabs();
    setMessage(L"Saved the project to " + dest.wstring());
    return true;
}
