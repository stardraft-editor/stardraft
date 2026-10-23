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

// Before a build: the triggers' code into generated/triggers.eps. False (with the problems in the log) when the
// checker finds errors.
bool writeTriggerCode(EudProject& pr, std::string& err) {
    TriggerSet set;
    if (gTE.project == &pr && !gTE.set.loadFailed) set = gTE.set;
    else if (!set.load(triggersFile(pr), err)) return err += ". Repair the file, then build again.", false;
    else if (gTE.project == &pr) {  // it did not load before and does now (repaired): the Triggers workspace takes it
        gTE.set = set, gTE.sel.clear();
        trigHistoryReset();
        buildTriggersReloaded();
    }
    bool any = false;
    std::function<void(const TriggerNode&)> walk = [&](const TriggerNode& n) {
        any |= n.kind == TriggerNode::Kind::Trigger || n.kind == TriggerNode::Kind::Variable;
        for (const auto& c : n.children) walk(c);
    };
    for (const auto& r : set.roots) walk(r);
    // The [MSQC] section: the lines for the keys and buttons the triggers read; lines written by hand stay.
    auto setInputs = [&](const std::string& code) {
        std::vector<std::string> lines;
        for (const auto& l : pr.sectionLines("MSQC"))
            if (!msqcGeneratedLine(l)) lines.push_back(l);
        for (const auto& l : msqcLinesFor(code)) lines.push_back(l);
        const auto chat = triggerChatLines(code);
        if (lines == pr.sectionLines("MSQC") && chat == pr.sectionLines("chatEvent")) return true;
        pr.setSectionLines("MSQC", lines);
        pr.setSectionLines("chatEvent", chat);
        return pr.save(err);
    };
    if (!any) return pr.writeGeneratedScript("triggers.eps", "", err) && setInputs("");
    std::vector<TriggerIssue> issues;
    std::vector<TriggerCodeLine> lines;
    const std::string code = set.generate(trigMapInfo(), issues, TriggerBuildOptions{pr.debugBuild()}, &lines);
    trigKeepCodeMap(pr, set, lines);  // a euddraft error in a line selects its block
    int errors = 0;
    for (const auto& i : issues)
        if (i.level == TriggerIssue::Level::Error) {
            ++errors;
            shellLogError(L"Trigger \"" + (i.node ? widen(i.node->name) : L"?") + L"\": " + widen(i.message));
        }
    if (errors) {
        err = std::to_string(errors) + " problem(s) in the triggers (see the log; they are marked red in the Triggers workspace)";
        return false;
    }
    return pr.writeGeneratedScript("triggers.eps", code, err) && setInputs(code);
}

// --- The data edits --------------------------------------------------------------------------------------------------

// Before a build: the edits saved, and their code written (or taken out when there are none).
bool writeExtrasEdits(EudProject& pr, std::string& err) {
    DataExtrasModel& x = dataExtras();
    StatTxt strings;
    Requirements reqs;
    ButtonSets buttons;
    UnitLooks looks;
    // Another project than the one the Data workspace shows, or a file of it did not load (it may be repaired now):
    // from disk.
    const bool open = dataState().project == &pr && !extrasLoadFailed();
    if (!open) {
        if (!strings.load(appState().data, err) || !strings.loadEdits(extrasFile(pr, kExtraStrings), err) || !reqs.loadEdits(extrasFile(pr, kExtraReqs), err) ||
            !buttons.loadEdits(extrasFile(pr, kExtraButtons), err) || !looks.loadEdits(extrasFile(pr, kExtraLooks), err))
            return false;
    }
    const StatTxt& s = open ? x.strings : strings;
    const Requirements& r = open ? x.reqs : reqs;
    const ButtonSets& b = open ? x.buttons : buttons;
    const UnitLooks& lk = open ? x.looks : looks;
    if (!s.saveEdits(extrasFile(pr, kExtraStrings), err) || !r.saveEdits(extrasFile(pr, kExtraReqs), err) || !b.saveEdits(extrasFile(pr, kExtraButtons), err) ||
        !lk.saveEdits(extrasFile(pr, kExtraLooks), err))
        return false;
    const std::string sCode = s.editsScript(err);
    if (!err.empty()) return false;
    const std::string rCode = r.editsScript(err);
    if (!err.empty()) return false;
    return pr.writeGeneratedScript("strings.eps", sCode, err) && pr.writeGeneratedScript("requirements.eps", rCode, err) &&
           pr.writeGeneratedScript("buttons.eps", b.editsScript(), err) && pr.writeGeneratedScript("unit_display.eps", lk.editsScript(), err);
}

// Before a build: every table's edits saved, and generated/<name>.eps written for each that has some.
bool writeDatEdits(EudProject& pr, std::string& err, std::vector<std::string>* notes) {
    DataModel& data = dataState();
    std::vector<std::unique_ptr<DatTable>> loaded;
    std::vector<std::unique_ptr<DatTable>>* tables = &data.tables;
    // Not open here, or a file of the open project did not load (it may be repaired now): from disk.
    const bool reread = data.project == &pr && dataLoadFailed();
    if (data.project != &pr || data.tables.empty() || reread) loaded = loadDatTables(pr, err), err.clear(), tables = &loaded;
    std::vector<const DatTable*> list;
    std::vector<std::unique_ptr<DatTable>> retried;
    for (size_t i = 0; i < tables->size(); ++i) {
        const DatTable* t = (*tables)[i].get();
        if (!t) {
            // The game table did not load. Without project edits for it, nothing is lost; with edits, the build stops
            // when it still does not load, as it would drop them.
            const DatSchema& sc = DatSchema::all()[i];
            std::error_code ec;
            if (!std::filesystem::exists(pr.datFile(sc.name), ec)) continue;
            auto r = std::make_unique<DatTable>();
            if (!r->load(appState().data, sc, err) || !r->loadEdits(pr.datFile(sc.name), err)) return false;
            t = retried.emplace_back(std::move(r)).get();
        } else if (!t->saveEdits(pr.datFile(t->schema().name), err)) return false;
        if (notes)
            for (std::string& m : t->unappliedSoundFiles()) notes->push_back(std::move(m));
        list.push_back(t);
    }
    const bool ok = pr.writeGenerated(list, err) && writeExtrasEdits(pr, err);
    if (ok && reread) {  // the repaired files load now: the Data workspace takes them (now when it shows, else at its next show)
        const int tab = data.datTab, entry = data.entry;
        data.project = nullptr, data.tables.clear();
        buildDataReloaded(tab, entry);
    }
    return ok;
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
    static std::unique_ptr<UnitSprites> game, edited;
    static std::vector<uint16_t> types;
    static int at = -1;
    static const EudProject* atProject = nullptr;
    const EudProject* pr = activeProject();
    if (!pr) return {};
    dataLoadForProject();
    if (at == dataState().edits && atProject == pr && edited) return types;
    std::string err;
    if (!game) {
        game = std::make_unique<UnitSprites>();
        if (!game->load(appState().data, err)) return game.reset(), std::vector<uint16_t>{};
    }
    edited = std::make_unique<UnitSprites>();
    if (!edited->load(appState().data, err)) return edited.reset(), std::vector<uint16_t>{};
    applyProjectGraphics(*edited);
    types = UnitSprites::changedUnitGraphics(*game, *edited);
    at = dataState().edits, atProject = pr;
    return types;
}

// The unit properties (UPRP) that the remake code can use: the map has 64, and its triggers use some. The map's UPUS
// marks the ones of its classic triggers; each CreateUnitWithProperties or UnitProperty in the project's code (the
// triggers' code, the scripts, the .eps and .py files beside them and in the scripts folder, which scripts can import)
// can take one.
int freeUnitProperties(const EudProject& pr) {
    AppState& app = appState();
    int used = 0;
    if (const auto* upus = app.ed->chk().find("UPUS"))
        for (size_t i = 0; i < upus->data.size() && i < size_t(EudProject::kUnitProperties); ++i) used += upus->data[i] != 0;
    auto occurrences = [](const std::string& text, const char* word) {
        int n = 0;
        for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) ++n;
        return n;
    };
    std::set<fs::path> files;
    for (const std::string& s : pr.scripts()) files.insert(pr.scriptFile(s).lexically_normal());
    // The files beside each script (an import names a module in its folder), and all of the project's scripts folder.
    std::set<fs::path> folders = {pr.folder() / "scripts"};
    for (const fs::path& f : files) folders.insert(f.parent_path());
    std::error_code ec;
    auto add = [&](const fs::path& f) {
        if (f.extension() == ".eps" || f.extension() == ".py") files.insert(f.lexically_normal());
    };
    for (const fs::path& dir : folders) {
        const bool all = dir == pr.folder() / "scripts";
        if (all) {
            for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (it->is_directory(ec) && (name == "__epspy__" || name == "__pycache__")) it.disable_recursion_pending();
                else add(it->path());
            }
        } else {
            for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) add(it->path());
        }
        ec.clear();
    }
    const fs::path remake = (pr.folder() / "generated" / "remake.eps").lexically_normal();
    for (const fs::path& file : files) {
        if (file == remake || (file.extension() != ".eps" && file.extension() != ".py")) continue;
        std::ifstream f(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        used += std::max(occurrences(text, "CreateUnitWithProperties"), occurrences(text, "UnitProperty("));
    }
    return std::max(0, EudProject::kUnitProperties - used);
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
    if (gRemakeFreePropertiesFor != pr) gRemakeFreeProperties = freeUnitProperties(*pr), gRemakeFreePropertiesFor = pr;
    if (forEd != app.ed.get() || forProject != pr || forRevision != app.ed->revision() || forEdits != dataState().edits ||
        forFree != gRemakeFreeProperties || made.size() != units.size()) {
        forEd = app.ed.get(), forProject = pr, forRevision = app.ed->revision(), forFree = gRemakeFreeProperties;
        const std::vector<uint16_t> changed = remakeUnitTypes();  // (it loads the project's data: the edits' count after it)
        forEdits = dataState().edits;
        made.assign(units.size(), false);
        for (size_t i : EudProject::planRemake(units, types, changed, gRemakeFreeProperties).made) made[i] = true;
    }
    return made[size_t(in - units.data())];
}

// generated/remake.eps, or none when the option is off. `leaveOut` gets the UNIT indexes of the units that the code
// makes (the map that euddraft gets leaves them out: writeBuildMap). `notes` gets how many units the code makes, how
// many keep the old graphics and why, and how many stand where the game cannot put them.
bool writeRemakeCode(EudProject& pr, std::string& err, std::vector<std::wstring>& notes, std::vector<size_t>& leaveOut) {
    AppState& app = appState();
    leaveOut.clear();
    EudProject::RemakePlan plan;
    if (pr.remakeUnits() && app.ed) {
        const auto& types = app.ed->unitTypes();
        const auto& units = app.ed->units();
        gRemakeFreeProperties = freeUnitProperties(pr), gRemakeFreePropertiesFor = &pr;
        plan = EudProject::planRemake(units, types, remakeUnitTypes(), gRemakeFreeProperties);
        leaveOut = plan.made;
        // The game puts a unit only where it fits: a ground unit on walkable ground (a building on buildable ground), and
        // not on other ground units. Air units can be on top of each other.
        PlacementContext ctx = app.ed->placementContext();
        ctx.stacking = false;
        int blocked = 0;
        for (size_t i : plan.made) {
            const Unit& u = units[i];
            const UnitType& ut = types[u.type];
            if (ut.is(UnitFlag::Flyer) && !ut.is(UnitFlag::Building)) continue;
            const auto issues = checkPlacement(ctx, u, int(i));
            blocked += std::any_of(issues.begin(), issues.end(), [](PlacementIssue p) { return p != PlacementIssue::NearResources; });
        }
        const int count = int(plan.made.size());
        if (count)
            notes.push_back(tr(L"New graphics for preplaced units: ") + std::to_wstring(count) + tr(count == 1 ? L" unit is" : L" units are") +
                            tr(L" made at the start from the map's data"));
        auto kept = [&](int n, const wchar_t* why) {
            if (n) notes.push_back(std::to_wstring(n) + tr(n == 1 ? L" preplaced unit keeps" : L" preplaced units keep") + tr(why));
        };
        kept(plan.loaded, L" the old graphics: a unit in a transport stays in the map.");
        kept(plan.linked, L" the old graphics: a unit linked to an add-on or a Nydus Canal stays in the map.");
        kept(plan.noProperty, L" the old graphics: the map has no free unit property (of 64) for their values.");
        if (blocked)
            notes.push_back(std::to_wstring(blocked) + tr(blocked == 1 ? L" of the units that the map makes stands" : L" of the units that the map makes stand") +
                            tr(L" where the game cannot put a unit. The game puts it near its place, or does not make it."));
    }
    return pr.writeGeneratedScript("remake.eps", plan.code, err);
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
    std::string err;
    if (!writeTriggerCode(*pr, err)) {
        shellLogError(widen(err));
        buildStatus(L"Did not build the map: " + widen(err));
        return;
    }
    std::vector<std::string> datNotes;  // logged as errors after the reset below
    if (!writeDatEdits(*pr, err, &datNotes)) {
        shellErrorBox(widen(err), L"Could not write the .dat edits");
        return;
    }
    std::vector<std::wstring> remakeNotes;
    std::vector<size_t> leaveOut;
    if (!writeRemakeCode(*pr, err, remakeNotes, leaveOut)) {
        shellLogError(widen(err));
        buildStatus(L"Did not build the map: " + widen(err));
        return;
    }
    if ((app.ed->modified() || app.ed->path() != pr->inputMap()) && !app.ed->save(pr->inputMap(), err)) {
        shellErrorBox(widen(err), L"Could not save the project's map");
        return;
    }
    // The map that euddraft gets: the project's map without the units that the remake code makes.
    if (!pr->writeBuildMap(leaveOut, err)) {
        shellLogError(widen(err));
        buildStatus(L"Did not build the map: " + widen(err));
        return;
    }
    updateTitle();
    shellLogNewBuild();  // a new build: the errors of the last one go (#85); the log keeps their lines
    shellLogLine(L"------------------------------------------------------------");  // (the log stays as it is: it opens by itself if the build fails)
    for (const std::string& m : datNotes) shellLogError(widen(m));
    for (const std::wstring& m : remakeNotes) shellLogLine(m);
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
        auto* r = new euddraft::BuildResult(copy.compile({}, post));
        shellPost([r] { buildFinished(r); });
    }).detach();
}
