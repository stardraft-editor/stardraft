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
        if (!buttons.loadEdits(extrasPath(pr, "buttons.json"), err) || !reqs.loadEdits(extrasPath(pr, "requirements.json"), err) ||
            !looks.loadEdits(extrasPath(pr, "unit_display.json"), err))
            fail(err);
    }

    static fs::path extrasPath(const EudProject& pr, const char* name) { return pr.folder() / "dat" / name; }

    // The buttons, requirements and unit looks (dat_extras.h): their project files, the same as the editor's.
    ButtonSets buttons;
    Requirements reqs;
    UnitLooks looks;
    bool buttonsChanged = false, reqsChanged = false, looksChanged = false;

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
        if (buttonsChanged && !buttons.saveEdits(extrasPath(pr, "buttons.json"), err)) fail(err);
        if (reqsChanged && !reqs.saveEdits(extrasPath(pr, "requirements.json"), err)) fail(err);
        if (looksChanged && !looks.saveEdits(extrasPath(pr, "unit_display.json"), err)) fail(err);
    }
};

// --- Buttons, requirements and unit looks (dat_extras.h) -------------------------------------------------------------

// A value of a named list (a button's condition or action, a requirement's op, a panel kind): its name or its number.
template <class Named>
uint32_t namedValue(const Json& v, const char* what, size_t count, Named name) {
    if (v.type == Json::Type::Number) return uint32_t(v.n);
    for (size_t i = 0; i < count; ++i)
        if (lower(name(i)) == lower(v.s)) return uint32_t(i);
    fail(std::string(what) + ": no \"" + v.s + "\"");
}
// The index of a named value: its name, or a number that is in range (a number is never a name's index unless in range).
template <class Named>
int indexOf(const Json& v, const char* what, size_t count, Named name) {
    if (v.type == Json::Type::Number) {
        if (v.n < 0 || v.n >= double(count) || v.n != std::floor(v.n)) fail(std::string(what) + ": " + std::to_string(v.n) + " is not in 0-" + std::to_string(count - 1));
        return int(v.n);
    }
    for (size_t i = 0; i < count; ++i)
        if (lower(name(i)) == lower(v.s)) return int(i);
    fail(std::string(what) + ": no \"" + v.s + "\"");
}
// A whole number of an op's field, in 0 to count - 1.
int numIn(const Json& op, const char* k, int count) {
    const double v = num(op, k);
    if (v < 0 || v >= double(count) || v != std::floor(v)) fail(std::string("\"") + k + "\" must be 0-" + std::to_string(count - 1));
    return int(v);
}
// A whole number of a command's option (--set n, --entry n), in 0 to count - 1.
int optIn(const Session& s, const char* k, int count) {
    const std::string text = s.args.get(k);
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (text.empty() || *end || v < 0 || v >= count) fail(std::string("--") + k + " must be 0-" + std::to_string(count - 1));
    return int(v);
}
int optInt(const Json& j, const char* k, int def) {
    const Json* v = j.get(k);
    return v && v->type == Json::Type::Number ? int(v->n) : def;
}

Json buttonsJson(const ButtonSets& b, int set) {
    Json a = jarr();
    for (const Button& x : b.set(set)) {
        const ButtonFunctionDef* c = ButtonSets::condition(x.condition);
        const ButtonFunctionDef* f = ButtonSets::action(x.action);
        a.items.push_back(Obj()
                              .add("pos", int(x.pos))
                              .add("icon", int(x.icon))
                              .add("condition", c ? std::string(c->name) : std::to_string(x.condition))
                              .add("condition_arg", int(x.conditionArg))
                              .add("action", f ? std::string(f->name) : std::to_string(x.action))
                              .add("action_arg", int(x.actionArg))
                              .add("enabled_string", int(x.enabledString))
                              .add("disabled_string", int(x.disabledString)));
    }
    return Obj().add("set", set).add("changed", b.changed(set)).add("buttons", a);
}

Json requirementsJson(const Requirements& r, int kind, int entry) {
    const Requirements::Kind& k = Requirements::kind(kind);
    const RequirementList& l = r.list(kind, entry);
    Json ops = jarr();
    for (const RequirementOp& o : l.ops) {
        const RequirementOpDef* d = o.op == 0xFFFF ? nullptr : Requirements::op(o.op);
        ops.items.push_back(Obj().add("op", int(o.op)).add("name", o.op == 0xFFFF ? std::string("end") : d ? std::string(d->name) : std::string()).add("arg", int(o.arg)));
    }
    return Obj().add("kind", k.name).add("entry", entry).add("used", l.used).add("changed", r.changed(kind, entry)).add("ops", ops);
}

Json looksJson(const UnitLooks& u, int unit) {
    const UnitLooks::Unit& x = u.unit(unit);
    const uint32_t st = u.status(unit), di = u.display(unit);
    const UnitPanelKind* kind = UnitLooks::kindOf(st, di);
    return Obj()
        .add("unit", unit)
        .add("panel", kind ? std::string(kind->name) : std::string())
        .add("status", double(st))
        .add("display", double(di))
        .add("wireframe", x.wireframe)
        .add("group_wireframe", x.groupWireframe)
        .add("transport_wireframe", x.transportWireframe)
        .add("changed", u.changed(unit));
}

// The stored values of a unit's looks are kept with the game's values where the project has none (status 0).
void applyExtrasOp(DataEdits& d, const Json& op, bool& handled) {
    const std::string name = op.str("op");
    handled = true;
    if (name == "set_buttons" || name == "reset_buttons") {
        const int set = numIn(op, "set", ButtonSets::kSets);
        if (name == "reset_buttons") {
            d.buttons.reset(set);
        } else {
            std::vector<Button> list;
            for (const Json& j : arrayOf(op, "buttons")) {
                Button b;
                b.pos = uint16_t(optInt(j, "pos", 1));
                b.icon = uint16_t(optInt(j, "icon", 0));
                const Json* c = j.get("condition");
                const Json* a = j.get("action");
                if (!c || !a) fail("a button needs \"condition\" and \"action\"");
                auto cn = [](size_t i) { return std::string(ButtonSets::conditions()[i].name); };
                auto an = [](size_t i) { return std::string(ButtonSets::actions()[i].name); };
                // (a name is looked up in the list; a number is the game's function address, as the .dat keeps it)
                b.condition = c->type == Json::Type::Number ? uint32_t(c->n) : ButtonSets::conditions()[namedValue(*c, "condition", ButtonSets::conditions().size(), cn)].address;
                b.action = a->type == Json::Type::Number ? uint32_t(a->n) : ButtonSets::actions()[namedValue(*a, "action", ButtonSets::actions().size(), an)].address;
                b.conditionArg = uint16_t(optInt(j, "condition_arg", 0));
                b.actionArg = uint16_t(optInt(j, "action_arg", 0));
                b.enabledString = uint16_t(optInt(j, "enabled_string", 0));
                b.disabledString = uint16_t(optInt(j, "disabled_string", 0));
                list.push_back(b);
            }
            d.buttons.set(set, list);
        }
        d.buttonsChanged = true;
    } else if (name == "set_requirements" || name == "reset_requirements") {
        const int kind = indexOf(need(op, "kind"), "kind", Requirements::kKinds, [](size_t i) { return std::string(Requirements::kind(int(i)).name); });
        const int entry = numIn(op, "entry", Requirements::kind(kind).entries);
        if (name == "reset_requirements") {
            d.reqs.reset(kind, entry);
        } else {
            RequirementList l;
            l.used = op.boolean("used", true);
            const size_t opCount = Requirements::ops().size();
            for (const Json& j : arrayOf(op, "ops")) {
                RequirementOp o;
                const Json* v = j.get("op");
                if (!v) fail("an op needs \"op\"");
                if (v->type == Json::Type::String && lower(v->s) == "end") o.op = 0xFFFF;
                else o.op = uint16_t(namedValue(*v, "op", opCount, [](size_t i) { return std::string(Requirements::ops()[i].name); }));
                o.arg = uint16_t(optInt(j, "arg", 0));
                l.ops.push_back(o);
            }
            d.reqs.set(kind, entry, l);
        }
        d.reqsChanged = true;
    } else if (name == "set_unit_look" || name == "reset_unit_look") {
        const int unit = numIn(op, "unit", UnitLooks::kUnits);
        if (name == "reset_unit_look") {
            d.looks.set(unit, UnitLooks::Unit{});
        } else {
            UnitLooks::Unit x = d.looks.unit(unit);
            if (const Json* p = op.get("panel")) {  // a panel kind sets both functions
                const auto kinds = UnitLooks::kinds();
                const int k = indexOf(*p, "panel", kinds.size(), [&](size_t i) { return std::string(kinds[i].name); });
                x.status = kinds[k].status, x.display = kinds[k].display;
            }
            if (op.get("status")) x.status = uint32_t(num(op, "status"));
            if (op.get("display")) x.display = uint32_t(num(op, "display"));
            if (op.get("wireframe")) x.wireframe = int(num(op, "wireframe"));
            if (op.get("group_wireframe")) x.groupWireframe = int(num(op, "group_wireframe"));
            if (op.get("transport_wireframe")) x.transportWireframe = int(num(op, "transport_wireframe"));
            d.looks.set(unit, x);
        }
        d.looksChanged = true;
    } else {
        handled = false;
    }
}

// A field's value as the JSON gives it: its number, and its name when it is a choice.
Json fieldValueJson(const DatField& f, uint32_t v) {
    Obj o;
    o.add("value", double(v));
    if (const uint32_t k = datShownScale(f); k > 1) o.add("shown", datShownText(k, v));  // hit points: the editor's unit
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
                // A number is in the shown unit (hit points, decimals too), as the Data form takes it; "raw": true
                // gives the stored value (units.dat: 1/256 hit point).
                const uint32_t k = op.boolean("raw", false) ? 1 : datShownScale(*f);
                if (value.n < 0 || value.n > double(f->maxValue()) / k) fail("\"" + f->name + "\" takes 0-" + std::to_string(double(f->maxValue()) / k));
                v = datRawFromShown(value.n, k, f->maxValue());
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
        else d.strings.set(i, textOf(op, "text"));
        d.stringsChanged = true;
    } else {
        bool handled = false;
        applyExtrasOp(d, op, handled);
        if (!handled) fail("unknown data op \"" + name + "\": set_field, reset_field, set_string, reset_string, set_buttons, reset_buttons, set_requirements, reset_requirements, set_unit_look, reset_unit_look");
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
    if (what == "buttons") {  // the button sets: one with --set n, else the changed ones
        if (s.args.has("set")) return buttonsJson(d.buttons, optIn(s, "set", ButtonSets::kSets));
        Json a = jarr();
        for (int i = 0; i < ButtonSets::kSets; ++i)
            if (d.buttons.changed(i)) a.items.push_back(Obj().add("set", i).add("buttons", int(d.buttons.set(i).size())));
        return Obj().add("changed_sets", a);
    }
    if (what == "requirements") {  // one entry with --kind and --entry; else the changed entries of the kind
        Json a = jarr();
        int kind = -1;
        for (int k = 0; s.args.has("kind") && k < Requirements::kKinds; ++k)
            if (lower(Requirements::kind(k).name) == lower(s.args.get("kind"))) kind = k;
        if (kind < 0) fail("requirements needs --kind (units, upgrades, research, tech use, orders)");
        if (s.args.has("entry")) return requirementsJson(d.reqs, kind, optIn(s, "entry", Requirements::kind(kind).entries));
        for (int e = 0; e < Requirements::kind(kind).entries; ++e)
            if (d.reqs.changed(kind, e)) a.items.push_back(Obj().add("entry", e));
        return Obj().add("kind", Requirements::kind(kind).name).add("changed_entries", a);
    }
    if (what == "unit-looks") {  // one unit with --unit n, else the changed units
        if (s.args.has("unit")) return looksJson(d.looks, optIn(s, "unit", UnitLooks::kUnits));
        Json a = jarr();
        for (int u = 0; u < UnitLooks::kUnits; ++u)
            if (d.looks.changed(u)) a.items.push_back(Obj().add("unit", u));
        return Obj().add("changed_units", a);
    }
    return datTableJson(d, what, s);
}

static Json cmdProjectDataApply(Session& s) {
    if (s.args.positional.size() < 2) fail("usage: stardraft-cli project-data-apply <project> <ops.json | - | '[...]'>");
    const EudProject pr = openProject(s.args.positional[0]);
    if (!s.dataOpen) openGameData(s.data, s.args.get("install")), s.dataOpen = true;
    const std::string src = s.args.positional[1];
    const bool inlineText = !src.empty() && (src[0] == '[' || src[0] == '{');
    if (src == "-" && s.serving) fail("\"-\" (stdin) is not a source in a request: put the operations in \"ops\"");
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
