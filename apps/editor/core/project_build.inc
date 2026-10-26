// Build (File > Project > Build, Ctrl+B): the project's generated files (the triggers' code, the .dat edits and the
// other tables' edits, the code for new graphics of preplaced units), the map saved into the project, then euddraft on
// a thread; its output streams into the Log. Moved from main.cpp (portable; the Windows program and the SDL program run
// the same code). What a host keeps: the text being typed that goes in first (mapPageFlush, stringsCommitEdit,
// dataCommitEdit) and the workspaces that show repaired files again (buildDataReloaded, buildTriggersReloaded).

void mapPageFlush();       // a text being typed on a map page goes into the map now
void stringsCommitEdit();  // the Strings tab's text being typed goes into the strings
bool dataCommitEdit();     // the Data form's value being typed goes in (false: it is not a number; the status says why)
bool trigShowBuildError(const std::string& logLine);  // a euddraft error in a line of the triggers' code selects its block (core/views_triggers_help.inc)
void buildDataReloaded(int tab, int entry);  // the repaired .dat files load now: the Data workspace shows them
void buildTriggersReloaded();                // the repaired triggers.json loads now: the Triggers workspace shows it
bool projectShowScriptError(const std::string& log);  // core/project_page.inc
void projectFormRefresh();

bool gBuilding = false;  // a build runs (Build grays, and the top bar's Build says "Building")
int gBuildResult = -1;   // the last build: 1 built, 0 failed, -1 none yet (the snapshots' --build reads it)
// The first line of the build's output that names a line of the triggers' code (it selects the block when the build fails).
std::string gTrigBuildError;

// (triggersFile and trigMapInfo are core/triggers_model.inc's; the line map, trigKeepCodeMap, is core/views_triggers_help.inc's.)
std::filesystem::path triggersFile(const EudProject& pr);
TriggerMapInfo trigMapInfo();
void trigKeepCodeMap(const EudProject& pr, const TriggerSet& set, const std::vector<TriggerCodeLine>& lines);

// The map's playability as one message ("" when StarCraft can start it).
// (startLocations false: the start location problems are left out; the save question has asked about them.)
std::wstring playabilityText(bool startLocations = true) {
    AppState& app = appState();
    if (!app.ed || !app.ed->isOpen()) return L"";
    std::wstring text;
    for (const auto& p : app.ed->playabilityProblems(startLocations)) text += L"\x2022 " + widen(p) + L"\r\n";
    return text;
}

// The question before a build of a map that StarCraft will not start (`problems`: playabilityText()): Build or Do not build.
std::wstring buildAnywayQuestion(const std::wstring& problems) {
    return L"StarCraft will not start this map:\r\n\r\n" + problems + L"\r\nTo set the players, use Players and forces on the Properties page. Build the map anyway?";
}

// --- The build's files: the steps are in the library (include/stardraft/project_build.h) -----------------------------------
// What the Data and the Triggers workspaces show, for the build: their tables, extras and triggers when they are the open
// project's. `reread`: the open tables did not load, so the build reads the files again.
stardraft::BuildOpen buildOpenFor(const EudProject& pr, bool& reread) {
    stardraft::BuildOpen open;
    reread = dataState().project == &pr && dataLoadFailed();
    if (dataState().project == &pr && !dataState().tables.empty() && !reread) open.tables = &dataState().tables;
    if (dataState().project == &pr && !extrasLoadFailed()) {
        DataExtrasModel& x = dataExtras();
        open.strings = &x.strings, open.reqs = &x.reqs, open.buttons = &x.buttons, open.looks = &x.looks;
    }
    if (gTE.project == &pr && !gTE.set.loadFailed) open.triggers = &gTE.set;
    return open;
}

// --- New graphics for preplaced units (Project page, R8) -------------------------------------------------------------
// The game makes the preplaced units before the .dat edits run, so they keep the game's graphics. With the option on,
// Build takes these units out of the map that euddraft gets and adds code that makes them at the start with the values
// from the map, and the map view shows them with the edited graphics.

// The project's units.dat, flingy.dat, sprites.dat and images.dat edits on `sp`.
void applyProjectGraphics(UnitSprites& sp) {
    dataLoadForProject();
    const DatTable* units = datTable(datTabOf("units"));
    const DatTable* flingy = datTable(datTabOf("flingy"));
    const DatTable* sprites = datTable(datTabOf("sprites"));
    const DatTable* images = datTable(datTabOf("images"));
    if (sprites && images) sp.applyEdits(sprites->bytes(), images->bytes());
    if (units && flingy) sp.applyUnitEdits(units->bytes(), flingy->bytes());
}

// The unit types that the project's edits give other graphics (none without a project).
std::vector<uint16_t> remakeUnitTypes() {
    static std::vector<uint16_t> types;
    static int at = -1;
    static const EudProject* atProject = nullptr;
    const EudProject* pr = activeProject();
    if (!pr) return {};
    dataLoadForProject();
    if (at == dataState().edits && atProject == pr) return types;
    bool reread = false;
    types = stardraft::projectRemakeUnitTypes(appState().data, buildOpenFor(*pr, reread), *pr);
    at = dataState().edits, atProject = pr;
    return types;
}

// The free unit properties of the last Build of the active project (the map view reads them, and does not read the
// project's files again on each change of the map); -1: not known yet.
int gRemakeFreeProperties = -1;
const EudProject* gRemakeFreePropertiesFor = nullptr;

// For the map view: whether the remake code makes the unit `u` of the open map (the plan that Build makes), so that it
// shows the edited graphics. A unit that is not in the map's list (a preview) asks remakeKeeps.
bool remakeMakesUnit(const Unit& u) {
    AppState& app = appState();
    static const Editor* forEd = nullptr;
    static const EudProject* forProject = nullptr;
    static uint64_t forRevision = 0;
    static int forEdits = -1, forFree = -1;
    static std::vector<bool> made;
    const EudProject* pr = activeProject();
    if (!app.ed || !pr) return false;
    const auto& units = app.ed->units();
    const auto& types = app.ed->unitTypes();
    const Unit* in = &u;
    if (units.empty() || &u < units.data() || &u >= units.data() + units.size()) {
        // A copy (a unit that is being moved): the map's unit with its serial and type.
        auto it = std::find_if(units.begin(), units.end(), [&](const Unit& m) { return m.serial == u.serial && m.type == u.type; });
        if (it == units.end()) return !EudProject::remakeKeeps(u, u.type < types.size() && types[u.type].is(UnitFlag::Building));
        in = &*it;
    }
    if (gRemakeFreePropertiesFor != pr) gRemakeFreeProperties = stardraft::freeUnitProperties(*pr, *app.ed), gRemakeFreePropertiesFor = pr;
    if (forEd != app.ed.get() || forProject != pr || forRevision != app.ed->revision() || forEdits != dataState().edits ||
        forFree != gRemakeFreeProperties || made.size() != units.size()) {
        forEd = app.ed.get(), forProject = pr, forRevision = app.ed->revision(), forFree = gRemakeFreeProperties;
        const std::vector<uint16_t> changed = remakeUnitTypes();  // (it loads the project's data: the edits' count after it)
        forEdits = dataState().edits;
        made.assign(units.size(), false);
        for (size_t i : EudProject::planRemake(units, types, changed, gRemakeFreeProperties, app.ed->width(), app.ed->height()).made) made[i] = true;
    }
    return made[size_t(in - units.data())];
}
// --- The build -------------------------------------------------------------------------------------------------------

// A line of the build's output that reports an error (euddraft's [Error], a Python traceback, "Error:").
bool logLineIsError(const std::wstring& line) {
    return line.find(L"[Error]") != std::wstring::npos || line.find(L"Traceback") != std::wstring::npos || line.find(L"Error:") != std::wstring::npos;
}
void buildLogAppend(const std::wstring& line) {
    if (logLineIsError(line)) shellLogError(line);
    else shellLogLine(line);
}

// The message in the status bar only (the Log has the line already).
void buildStatus(const std::wstring& m) {
    appState().message = m;
    updateStatus();
}

void buildFinished(euddraft::BuildResult* r) {
    gBuilding = false;
    gBuildResult = r->ok ? 1 : 0;
    shellMenusChanged();
    projectFormRefresh();
    const EudProject* pr = activeProject();
    const std::wstring out = pr ? pr->outputMap().wstring() : L"";
    if (r->ok) {
        shellLogLine(L"Built " + out);
        setMessage(L"Built " + fs::path(out).filename().wstring());
    } else {
        shellLogError(L"Build failed" + (r->errors.empty() ? L"" : L": " + widen(r->errors.front())));
        buildStatus(L"Could not build the map (see the log)");
        // An error in a script (a linked file, a file it imports, scripts/...): its file and line (R5). The triggers'
        // generated code selects its block instead (T.1).
        if (!projectShowScriptError(r->log) && !gTrigBuildError.empty()) {
            trigShowBuildError(gTrigBuildError);
        }
    }
    delete r;
}

// The remake code's notes for the Log: how many units the code makes, how many keep the old graphics and why, and how many
// stand where the game cannot put them (the library's counts: stardraft::RemakeCounts).
std::vector<std::wstring> remakeNotesOf(const stardraft::RemakeCounts& c) {
    std::vector<std::wstring> notes;
    if (c.made)
        notes.push_back(tr(L"New graphics for preplaced units: ") + std::to_wstring(c.made) + tr(c.made == 1 ? L" unit is" : L" units are") +
                        tr(L" made at the start from the map's data"));
    auto kept = [&](int n, const wchar_t* why) {
        if (n) notes.push_back(std::to_wstring(n) + tr(n == 1 ? L" preplaced unit keeps" : L" preplaced units keep") + tr(why));
    };
    kept(c.loaded, L" the old graphics: a unit in a transport stays in the map.");
    kept(c.linked, L" the old graphics: a unit linked to an add-on or a Nydus Canal stays in the map.");
    kept(c.noProperty, L" the old graphics: the map has no free unit property (of 64) for their values.");
    kept(c.codeUnit, L" the old graphics: Create Unit cannot make a unit of a type above 227 reliably.");
    kept(c.edge, L" the old graphics: a unit on or past the map's edge stays in the map (Create Unit moves it or does not make it).");
    if (c.blocked)
        notes.push_back(std::to_wstring(c.blocked) + tr(c.blocked == 1 ? L" of the units that the map makes stands" : L" of the units that the map makes stand") +
                        tr(L" where the game cannot put a unit. The game puts it near its place, or does not make it."));
    return notes;
}

// Build: save the map into the project, then run euddraft on a thread; its output streams into the Log.
void compileProject() {
    AppState& app = appState();
    typingFlush();  // a text being typed on a map page or in the Briefing is in the build too
    stringsCommitEdit();  // and a text or value being typed in the Data workspace (not a number: no build, the status says why)
    if (!dataCommitEdit()) return;
    EudProject* pr = activeProject();
    if (!app.ed || !pr) {
        setMessage(L"Build needs an EUD project. Use File > Project > New project or Open project.");
        return;
    }
    if (gBuilding) {
        setMessage(L"A build is already running");
        return;
    }
    // The map's save below: a map without start locations asks first (Place them, Save anyway, Cancel).
    if (!confirmStartLocations()) {
        setMessage(tr(L"Did not build the map"));
        return;
    }
    if (const std::wstring problems = playabilityText(false); !problems.empty()) {
        if (gSelfTest || !shellAskYesNo(buildAnywayQuestion(problems), L"Build", L"&Build", L"Do &not build")) {
            setMessage(L"Did not build the map: " + problems.substr(2, problems.find(L"\r\n") - 2));
            return;
        }
    }
    // A linked file that is not found (moved, renamed, deleted): stop, and say which (R5).
    if (const auto missing = pr->missingScripts(); !missing.empty()) {
        shellLogError(widen(pr->missingScriptText(missing.front())));
        buildStatus(L"Did not build the map: " + widen(missing.front()) + L" is not found (see the Project page)");
        return;
    }
    bool reread = false;
    const stardraft::BuildOpen open = buildOpenFor(*pr, reread);
    stardraft::BuildReport report;
    std::string err;
    const bool built = stardraft::prepareBuild(*pr, *app.ed, app.data, open, report, err);
    if (report.codeGenerated) trigKeepCodeMap(*pr, report.triggerSet, report.triggerLines);  // (a euddraft error selects its block)
    if (report.triggersFromFile && gTE.project == pr) {  // it did not load before and does now (repaired): the Triggers workspace takes it
        gTE.set = report.triggerSet, gTE.sel.clear();
        trigHistoryReset();
        buildTriggersReloaded();
    }
    if (!built) {
        if (report.failedStep == "dat") {
            shellErrorBox(widen(err), L"Could not write the .dat edits");
            return;
        }
        if (report.failedStep == "save") {
            shellErrorBox(widen(err), L"Could not save the project's map");
            return;
        }
        for (const std::string& m : report.triggerErrors) shellLogError(widen(m));  // the checker's problems, then the reason
        shellLogError(widen(err));
        buildStatus(L"Did not build the map: " + widen(err));
        return;
    }
    if (reread) {  // the repaired .dat files load now: the Data workspace takes them
        const int tab = dataState().datTab, entry = dataState().entry;
        dataState().project = nullptr, dataState().tables.clear();
        buildDataReloaded(tab, entry);
    }
    if (report.remakeOn) gRemakeFreeProperties = report.freeProperties, gRemakeFreePropertiesFor = pr;
    updateTitle();
    shellLogNewBuild();  // a new build: the errors of the last one go (#85); the log keeps their lines
    shellLogLine(L"------------------------------------------------------------");  // (the log stays as it is: it opens by itself if the build fails)
    for (const std::string& m : report.datNotes) shellLogError(widen(m));
    if (report.remakeOn)
        for (const std::wstring& m : remakeNotesOf(report.remake)) shellLogLine(m);
    buildLogAppend(L"Compiling " + pr->file().wstring());
    gTrigBuildError.clear();
    gBuilding = true;
    shellMenusChanged();  // Build grays, and the top tool bar's Build says "Building"
    setMessage(L"Building the map\x2026");
    EudProject copy = *pr;
    std::thread([copy] {
        auto post = [](const std::string& line) {
            shellPost([line] {
                buildLogAppend(widen(line));
                if (gTrigBuildError.empty() && triggerErrorLine(line) > 0) gTrigBuildError = line;
            });
        };
        auto* r = new euddraft::BuildResult(stardraft::compileBuild(copy, post));
        shellPost([r] { buildFinished(r); });
    }).detach();
}
