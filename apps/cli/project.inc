// stardraft-cli: EUD projects (project.stardraft and its scripts), as JSON (included by main.cpp after its anonymous
// namespace). Build is not here yet: it writes the triggers, the Data edits and the project's map with the editor's
// own steps (docs/dev/CLI.md, "Not yet").
//
//   project-new <parent folder> <name> <map>   a new project (the map is copied in)
//   project <project> [--section <name>]       its summary, or the lines of one section (a script's name, MSQC...)
//   project-apply <project> <ops>              operations on the project file (saved when all of them work)

// The project's summary: its name, its map, its scripts (missing: the file is not there) and its switches.
static Json projectJson(const EudProject& pr) {
    Json scripts = jarr(), sections = jarr();
    const std::vector<std::string> missing = pr.missingScripts();
    for (const std::string& s : pr.scripts()) {
        const bool gone = std::find(missing.begin(), missing.end(), s) != missing.end();
        scripts.items.push_back(Obj().add("section", s).add("file", ustr(pr.scriptFile(s))).add("linked", EudProject::isLinked(s)).add("missing", gone));
    }
    for (const auto& s : pr.sections()) sections.items.push_back(jstr(s.name));
    const auto input = pr.inputMap(), output = pr.outputMap();
    return Obj()
        .add("project", ustr(pr.file()))
        .add("name", pr.name())
        .add("map", ustr(input))
        .add("output", ustr(output))
        .add("scripts", scripts)
        .add("sections", sections)
        .add("turbo", pr.turbo())
        .add("freeze", pr.freeze())
        .add("debug", pr.debugBuild())
        .add("trace", pr.traceBuild())
        .add("remake_units", pr.remakeUnits());
}

// The project file named by a word: project.stardraft, its folder or a .eds.
static EudProject openProject(const std::string& word) {
    EudProject pr;
    std::string err;
    if (!pr.open(upath(word), err)) fail(err);
    return pr;
}

static Json cmdProjectNew(Session& s) {
    if (s.args.positional.size() < 3) fail("usage: stardraft-cli project-new <parent folder> <name> <map>");
    EudProject pr;
    std::string err;
    if (!EudProject::create(upath(s.args.positional[0]), s.args.positional[1], upath(s.args.positional[2]), pr, err)) fail(err);
    return projectJson(pr);
}

static Json cmdProject(Session& s) {
    const EudProject pr = openProject(s.args.positional[0]);
    if (s.args.has("section")) {
        const std::string name = s.args.get("section");
        bool found = false;
        for (const auto& sec : pr.sections()) found |= sec.name == name;
        if (!found) fail("no section \"" + name + "\" in the project");
        Json lines = jarr();
        for (const std::string& l : pr.sectionLines(name)) lines.items.push_back(jstr(l));
        return Obj().add("section", name).add("lines", lines);
    }
    return projectJson(pr);
}

// One operation on the project (not a map edit: it is saved with the project file, and the operations of one command
// are all or none).
static void applyProjectOp(EudProject& pr, const Json& op) {
    const std::string name = op.str("op");
    // A section name (a script's path, a plugin such as MSQC): it must be given.
    auto sectionOf = [&]() {
        const std::string s = textOf(op, "section");
        if (s.empty()) fail("\"section\" cannot be empty");
        return s;
    };
    if (name == "add_script") {
        const std::string path = textOf(op, "path");
        if (path.empty()) fail("missing \"path\"");
        pr.addScript(path);
    } else if (name == "link_script") {
        const std::string path = textOf(op, "path");
        if (path.empty()) fail("missing \"path\"");
        const fs::path file = upath(path);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) fail("no script file " + path);
        pr.linkScript(file);
    } else if (name == "remove_section") {
        const std::string section = sectionOf();
        if (!pr.removeSection(section)) fail("no section \"" + section + "\" to remove");
    } else if (name == "move_script") {
        const std::string section = sectionOf();
        const int delta = int(num(op, "delta"));
        if (delta != -1 && delta != 1) fail("\"delta\" must be -1 or 1");
        if (!pr.moveScript(section, delta)) fail("\"" + section + "\" cannot move that way");
    } else if (name == "set_section") {
        const std::string section = sectionOf();
        std::vector<std::string> lines;
        for (const Json& l : arrayOf(op, "lines")) {
            if (l.type != Json::Type::String) fail("\"lines\" must be text lines");
            lines.push_back(l.s);
        }
        if (lines.empty()) fail("\"lines\" cannot be empty (remove_section takes a section away)");
        pr.setSectionLines(section, lines);
    } else if (name == "set_plugin") {
        pr.setPlugin(sectionOf(), op.boolean("on", true));
    } else if (name == "set_flags") {
        if (op.get("freeze")) pr.setFreeze(op.boolean("freeze", true));
        if (op.get("debug")) pr.setDebugBuild(op.boolean("debug", false));
        if (op.get("trace")) pr.setTraceBuild(op.boolean("trace", false));
        if (op.get("remake_units")) pr.setRemakeUnits(op.boolean("remake_units", false));
    } else if (name == "set_output") {
        const std::string path = textOf(op, "path");
        if (path.empty()) fail("missing \"path\"");
        pr.setOutputMap(upath(path));
    } else {
        fail("unknown project op \"" + name + "\": add_script, link_script, remove_section, move_script, set_section, set_plugin, set_flags, set_output");
    }
}

static Json cmdProjectApply(Session& s) {
    if (s.args.positional.size() < 2) fail("usage: stardraft-cli project-apply <project> <ops.json | - | '[...]'>");
    EudProject pr = openProject(s.args.positional[0]);
    const std::string src = s.args.positional[1];
    const bool inlineText = !src.empty() && (src[0] == '[' || src[0] == '{');
    if (src == "-" && s.serving) fail("\"-\" (stdin) is not a source in a request: put the operations in \"ops\"");
    std::string text = inlineText ? src : src == "-" ? std::string(std::istreambuf_iterator<char>(std::cin), {}) : readText(upath(src));
    const Json doc = parseJson(text, "the operations");
    const Json* ops = doc.type == Json::Type::Array ? &doc : doc.get("ops");
    if (!ops || ops->type != Json::Type::Array) fail("the operations must be a list, or an object with \"ops\": [...]");
    for (size_t i = 0; i < ops->items.size(); ++i) {
        try {
            applyProjectOp(pr, ops->items[i]);
        } catch (const std::exception& e) {
            fail("op " + std::to_string(i) + " (" + ops->items[i].str("op", "?") + "): " + e.what() + "; the project is not changed");
        }
    }
    std::string err;
    if (!pr.save(err)) fail(err);
    return projectJson(pr);
}

// project-build <project>: the Build of the editor (File > Project > Build) without the window: the generated files and
// the map that euddraft gets (stardraft::prepareBuild), then euddraft (stardraft::compileBuild). The map is the project's
// own (map/<name>.scx); the Data workspace's and the triggers' files are read as they are on disk. It may take a while.
// Its answer: whether it built, the output map, the euddraft log lines, the warnings (the map's playability: StarCraft may
// refuse the map) and the remake counts.
static Json cmdProjectBuild(Session& s) {
    if (s.args.positional.empty()) fail("usage: stardraft-cli project-build <project>");
    EudProject pr = openProject(s.args.positional[0]);
    if (!s.dataOpen) openGameData(s.data, s.args.get("install")), s.dataOpen = true;
    if (const auto missing = pr.missingScripts(); !missing.empty()) fail(pr.missingScriptText(missing.front()));
    Editor ed(s.data);
    std::string err;
    if (!ed.open(pr.inputMap(), err)) fail(err);
    checkStartLocations(s, ed);
    Json warnings = jarr();
    for (const std::string& p : ed.playabilityProblems(true)) warnings.items.push_back(jstr(p));
    stardraft::BuildReport report;
    if (!stardraft::prepareBuild(pr, ed, s.data, stardraft::BuildOpen{}, report, err)) {
        if (!report.triggerErrors.empty()) {  // the triggers' checker found errors: the answer lists them
            Json problems = jarr();
            for (const std::string& m : report.triggerErrors) problems.items.push_back(jstr(m));
            return Obj().add("built", false).add("error", err).add("trigger_problems", problems).add("warnings", warnings);
        }
        std::string detail = report.failedStep + ": " + err;
        for (const std::string& m : report.triggerErrors) detail += "; " + m;
        fail("the build stopped before euddraft (" + detail + ")");
    }
    Json log = jarr();
    const euddraft::BuildResult r = stardraft::compileBuild(pr, [&](const std::string& line) { log.items.push_back(jstr(line)); });
    Json errors = jarr();
    for (const std::string& e : r.errors) errors.items.push_back(jstr(e));
    // The script and the line that euddraft names (as the editor selects it): a file of the project, a linked file or one
    // that a script imports. The triggers' generated code is generated/triggers.eps.
    Json scriptError = Json();
    if (!r.ok) {
        std::string text;
        for (const Json& l : log.items) text += l.s + "\n";
        if (EudProject::ScriptError se; pr.scriptError(text, se))
            scriptError = Obj().add("file", ustr(se.file)).add("module", se.module).add("line", se.line).add("message", se.message);
    }
    Json triggerProblems = jarr();
    for (const std::string& m : report.triggerErrors) triggerProblems.items.push_back(jstr(m));
    const Json remake = Obj()
                            .add("on", report.remakeOn)
                            .add("units_made", report.remake.made)
                            .add("kept_loaded", report.remake.loaded)
                            .add("kept_linked", report.remake.linked)
                            .add("kept_no_property", report.remake.noProperty)
                            .add("kept_code_unit", report.remake.codeUnit)
                            .add("kept_edge", report.remake.edge)
                            .add("blocked", report.remake.blocked);
    return Obj()
        .add("built", r.ok)
        .add("output", ustr(pr.outputMap()))
        .add("exit_code", r.exitCode)
        .add("errors", errors)
        .add("script_error", scriptError)
        .add("trigger_problems", triggerProblems)
        .add("warnings", warnings)
        .add("remake", remake)
        .add("log", log);
}

// The problems that a map's checks list: the start locations that StarCraft needs (the editor's question before a save),
// then the rest of the playability (players, humans).
static void mapProblems(const Editor& ed, Json& problems, Json& warnings) {
    auto add = [&](const std::string& kind, const std::string& text) { problems.items.push_back(Obj().add("kind", kind).add("problem", text)); };
    const std::vector<std::string> start = ed.startLocationProblems();
    for (const std::string& p : start) add("start_locations", p);
    for (const std::string& p : ed.playabilityProblems(false)) add("playability", p);
    // A warning (island maps are made so): start locations that cannot reach each other by ground (not in one connected
    // area of the game's regions).
    constexpr uint16_t kStartLocation = 214;
    std::vector<const Unit*> starts;
    for (const Unit& u : ed.units())
        if (u.type == kStartLocation && u.owner < 8) starts.push_back(&u);
    if (starts.size() < 2) return;
    const PathingRegions regions = computePathingRegions(ed.terrain(), ed.tileset());
    auto groupOf = [&](const Unit& u) {
        const int r = regions.regionAt(u.x / 8, u.y / 8);
        return r >= 0 && size_t(r) < regions.regions.size() && regions.regions[size_t(r)].walkable() ? regions.regions[size_t(r)].group : -1;
    };
    const int first = groupOf(*starts[0]);
    for (size_t i = 1; i < starts.size(); ++i)
        if (groupOf(*starts[i]) != first || first < 0)
            warnings.items.push_back(Obj().add("kind", "pathing").add("problem", "Player " + std::to_string(starts[i]->owner + 1) +
                "'s start location cannot reach player " + std::to_string(starts[0]->owner + 1) + "'s by ground"));
}

// check <map | project>: the checks of a save and a build, with no change: for a map, its start locations and
// playability; for a project, also its scripts that are not there and the triggers' checker (errors and warnings).
// `passed` is false when a problem stops a save or a build (warnings do not).
static Json cmdCheck(Session& s) {
    if (s.args.positional.empty()) fail("usage: stardraft-cli check <map | project>");
    const std::string word = s.args.positional[0];
    const fs::path path = upath(word);
    std::error_code ec;
    const std::string ext = path.extension().string();
    const bool project = ext == ".stardraft" || ext == ".eds" || (fs::is_directory(path, ec) && fs::exists(path / "project.stardraft", ec));
    Json problems = jarr(), warnings = jarr();
    if (!project) {
        mapProblems(s.load(resolve(word)), problems, warnings);
        return Obj().add("passed", problems.items.empty()).add("problems", problems).add("warnings", warnings);
    }
    EudProject pr = openProject(word);
    if (!s.dataOpen) openGameData(s.data, s.args.get("install")), s.dataOpen = true;
    for (const std::string& m : pr.missingScripts()) problems.items.push_back(Obj().add("kind", "script").add("problem", pr.missingScriptText(m)));
    Editor ed(s.data);
    std::string err;
    if (!ed.open(pr.inputMap(), err)) fail(err);
    mapProblems(ed, problems, warnings);
    TriggerSet set;
    if (fs::exists(stardraft::triggersFileOf(pr), ec) && !set.load(stardraft::triggersFileOf(pr), err)) {
        problems.items.push_back(Obj().add("kind", "triggers").add("problem", "the triggers file does not load: " + err));
    } else {
        for (const TriggerIssue& i : set.check(stardraft::triggerMapInfo(ed))) {
            if (i.level == TriggerIssue::Level::Info) continue;
            const Json item = Obj().add("kind", "triggers").add("trigger", i.node ? i.node->name : std::string()).add("problem", i.message);
            (i.level == TriggerIssue::Level::Error ? problems : warnings).items.push_back(item);
        }
    }
    return Obj().add("project", ustr(pr.file())).add("passed", problems.items.empty()).add("problems", problems).add("warnings", warnings);
}
