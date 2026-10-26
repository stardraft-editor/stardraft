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
