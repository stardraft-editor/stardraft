// stardraft-cli: the Data workspace of an EUD project (its .dat edits and the stat_txt strings), as JSON (included by
// main.cpp after its anonymous namespace). The edits are the project's files (dat/<name>.dat, dat/strings.json), as the
// Data workspace keeps them: a build reads them. The game's files come from the install (--install, as for the maps).
//
//   project-data <project> [table] [--entry n]   the tables, one table's fields, or one entry's values
//   project-data <project> strings [--index n]   the stat_txt strings (the changed ones, or one)
//   project-data-apply <project> <ops>           set_field, reset_field, set_string, reset_string (all or none)

namespace {

// The game's tables with the project's edits on top (as the Data workspace loads them: DatTable::loadEdits).
struct DataEdits {
    std::vector<std::unique_ptr<DatTable>> tables;
    StatTxt strings;
    UnitTypes unitTypes;  // (the names of the units' entries)
    std::set<std::string> changedTables;
    bool stringsChanged = false;

    static fs::path stringsFile(const EudProject& pr) { return pr.folder() / "dat" / "strings.json"; }

    DataEdits(const GameData& data, const EudProject& pr) {
        std::string err;
        for (const DatSchema& sc : DatSchema::all()) {
            auto t = std::make_unique<DatTable>();
            if (!t->load(data, sc, err)) fail(err);
            if (!t->loadEdits(pr.datFile(sc.name), err)) fail(err);
            tables.push_back(std::move(t));
        }
        if (!strings.load(data, err)) fail(err);
        if (!strings.loadEdits(stringsFile(pr), err)) fail(err);
        if (!unitTypes.load(data, err)) fail(err);
    }

    DatTable& table(const std::string& name) {
        for (auto& t : tables)
            if (t->schema().name == name) return *t;
        fail("no table \"" + name + "\" (stardraft-cli project-data <project> lists them)");
    }

    // Saves the tables and strings that changed (a table with no edits left has its file removed, as the Data workspace does).
    void save(const EudProject& pr) {
        std::string err;
        for (auto& t : tables)
            if (changedTables.count(t->schema().name) && !t->saveEdits(pr.datFile(t->schema().name), err)) fail(err);
        if (stringsChanged && !strings.saveEdits(stringsFile(pr), err)) fail(err);
    }
};

// A field's value as the JSON gives it: its number, and its name when it is a choice.
Json fieldValueJson(const DatField& f, uint32_t v) {
    Obj o;
    o.add("value", double(v));
    if (v < f.valueNames.size()) o.add("name", f.valueNames[v]);
    return o;
}

Json datTablesJson(const DataEdits& d) {
    Json a = jarr();
    for (const auto& t : d.tables) {
        const DatSchema& sc = t->schema();
        a.items.push_back(Obj().add("table", sc.name).add("entries", sc.entries).add("fields", int(sc.fields.size())).add("changed_values", t->changedCount()));
    }
    return a;
}

Json datFieldsJson(const DatSchema& sc) {
    Json a = jarr();
    for (const DatField& f : sc.fields) {
        Obj o;
        o.add("field", f.name).add("size", f.size).add("first", f.first).add("last", f.last);
        if (!f.valueNames.empty()) {
            Json names = jarr();
            for (const std::string& n : f.valueNames) names.items.push_back(jstr(n));
            o.add("names", names);
        }
        if (!f.ref.empty()) o.add("ref", f.ref);
        if (!f.flags.empty()) {
            Json flags = jarr();
            for (const std::string& n : f.flags) flags.items.push_back(jstr(n));
            o.add("flags", flags);
        }
        a.items.push_back(o);
    }
    return a;
}

Json datEntryJson(const DataEdits& d, DatTable& t, int entry) {
    const DatSchema& sc = t.schema();
    if (entry < 0 || entry >= sc.entries) fail("no entry " + std::to_string(entry) + " in " + sc.name + " (0-" + std::to_string(sc.entries - 1) + ")");
    Json fields = jarr();
    for (const DatField& f : sc.fields) {
        if (!f.covers(entry)) continue;
        fields.items.push_back(Obj(fieldValueJson(f, t.value(f, entry))).add("field", f.name).add("changed", t.changed(f, entry)));
    }
    // (the units' names come from the game's stat_txt, as the Data workspace shows them)
    const std::string label = sc.name == "units" ? (entry < int(d.unitTypes.size()) ? d.unitTypes[uint16_t(entry)].name : std::string()) : entryLabel(sc.name, entry);
    return Obj().add("table", sc.name).add("entry", entry).add("label", label).add("fields", fields);
}

// One table: its fields, or with --entry n the values of that entry.
Json datTableJson(DataEdits& d, const std::string& name, const Session& s) {
    DatTable& t = d.table(name);
    if (!s.args.has("entry")) return Obj().add("table", name).add("entries", t.schema().entries).add("fields", datFieldsJson(t.schema())).add("changed_values", t.changedCount());
    return datEntryJson(d, t, int(std::strtol(s.args.get("entry").c_str(), nullptr, 10)));
}

Json datStringsJson(const DataEdits& d, const Session& s) {
    Json a = jarr();
    const bool one = s.args.has("index");
    const int only = one ? int(std::strtol(s.args.get("index").c_str(), nullptr, 10)) : -1;
    if (one && (only < 0 || only >= d.strings.count())) fail("no string " + std::to_string(only) + " (0-" + std::to_string(d.strings.count() - 1) + ")");
    for (int i = 0; i < d.strings.count(); ++i) {
        if (one ? i != only : !d.strings.changed(i)) continue;
        a.items.push_back(Obj().add("index", i).add("text", d.strings.text(i)).add("stock", d.strings.stock(i)).add("changed", d.strings.changed(i)));
    }
    return Obj().add("strings", a).add("count", d.strings.count()).add("changed", d.strings.changedCount());
}

// One operation on the data edits (throws CliError on a bad one; the caller keeps the files as they were).
void applyDataOp(DataEdits& d, const Json& op) {
    const std::string name = op.str("op");
    if (name == "set_field" || name == "reset_field") {
        DatTable& t = d.table(need(op, "table").s);
        const DatField* f = t.field(need(op, "field").s);
        if (!f) fail("no field \"" + op.str("field") + "\" in " + t.schema().name + " (stardraft-cli project-data lists them)");
        const int entry = int(num(op, "entry"));
        if (entry < 0 || entry >= t.schema().entries || !f->covers(entry)) fail("\"" + f->name + "\" has no entry " + std::to_string(entry));
        if (name == "reset_field") {
            t.reset(*f, entry);
        } else {
            uint32_t v = 0;
            const Json& value = need(op, "value");
            if (value.type == Json::Type::Number) {
                if (value.n < 0 || value.n > f->maxValue()) fail("\"" + f->name + "\" takes 0-" + std::to_string(f->maxValue()));
                v = uint32_t(value.n);
            } else {
                bool found = false;
                for (size_t k = 0; k < f->valueNames.size() && !found; ++k)
                    if (lower(f->valueNames[k]) == lower(value.s)) v = uint32_t(k), found = true;
                if (!found) fail("\"" + f->name + "\" has no value \"" + value.s + "\"");
            }
            t.set(*f, entry, v);
        }
        d.changedTables.insert(t.schema().name);
    } else if (name == "set_string" || name == "reset_string") {
        const int i = int(num(op, "index"));
        if (i < 0 || i >= d.strings.count()) fail("no string " + std::to_string(i));
        if (name == "reset_string") d.strings.reset(i);
        else d.strings.set(i, op.str("text"));
        d.stringsChanged = true;
    } else {
        fail("unknown data op \"" + name + "\": set_field, reset_field, set_string, reset_string");
    }
}

}  // namespace

static Json cmdProjectData(Session& s) {
    if (s.args.positional.size() < 1) fail("usage: stardraft-cli project-data <project> [table | strings]");
    const EudProject pr = openProject(s.args.positional[0]);
    if (!s.dataOpen) openGameData(s.data, s.args.get("install")), s.dataOpen = true;
    DataEdits d(s.data, pr);
    if (s.args.positional.size() < 2) return Obj().add("project", ustr(pr.file())).add("tables", datTablesJson(d)).add("strings_changed", d.strings.changedCount());
    const std::string what = s.args.positional[1];
    if (what == "strings") return datStringsJson(d, s);
    return datTableJson(d, what, s);
}

static Json cmdProjectDataApply(Session& s) {
    if (s.args.positional.size() < 2) fail("usage: stardraft-cli project-data-apply <project> <ops.json | - | '[...]'>");
    const EudProject pr = openProject(s.args.positional[0]);
    if (!s.dataOpen) openGameData(s.data, s.args.get("install")), s.dataOpen = true;
    const std::string src = s.args.positional[1];
    const bool inlineText = !src.empty() && (src[0] == '[' || src[0] == '{');
    const std::string text = inlineText ? src : src == "-" ? std::string(std::istreambuf_iterator<char>(std::cin), {}) : readText(upath(src));
    const Json doc = parseJson(text, "the operations");
    const Json* ops = doc.type == Json::Type::Array ? &doc : doc.get("ops");
    if (!ops || ops->type != Json::Type::Array) fail("the operations must be a list, or an object with \"ops\": [...]");
    DataEdits d(s.data, pr);
    for (size_t i = 0; i < ops->items.size(); ++i) {
        try {
            applyDataOp(d, ops->items[i]);
        } catch (const std::exception& e) {
            fail("op " + std::to_string(i) + " (" + ops->items[i].str("op", "?") + "): " + e.what() + "; nothing was saved");
        }
    }
    d.save(pr);
    return Obj().add("project", ustr(pr.file())).add("tables", datTablesJson(d)).add("strings_changed", d.strings.changedCount());
}
