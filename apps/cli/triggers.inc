// stardraft-cli: the EUD triggers of a project (the Triggers workspace's model, include/stardraft/triggers.h), as JSON, and
// their edits (included by main.cpp after project.inc; it uses main.cpp's helpers).
//
//   project-triggers <project> [--path p]        the trigger set, or one node with its blocks
//   project-triggers-apply <project> <ops>       the edits below, all or none (saved to triggers.json)
//
// A node's path: its category (0 Definitions, 1 On map start, 2 On every trigger cycle), then its folders and triggers
// by index, as "2.0.1". A block is {"block": "<block id>", "args": {"<parameter>": value}, "enabled": bool,
// "lists": [[block, ...], ...] (a container's blocks)}; its parameters are named as the block library names them.
// Operations: add_folder (parent, name), add_trigger (parent, name, comment, enabled, conditions, actions, index),
// add_variable (parent, name, kind, initial, size), set_node (path; name, comment, enabled, initial, conditions,
// actions), delete_node (path), move_node (path, parent, index), add_block (path, list, index, block, in),
// set_block (path, list, index, block, in), delete_block (path, list, index, in).
// list: "conditions" or "actions" of a trigger. in: the way into a container's blocks, as [[list, index], ...]: each step
// takes the block at `index` of the container's list `list` (its blocks, one list per the block library's lists).

namespace {

std::vector<int> nodePath(const std::string& text) {
    std::vector<int> out;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, '.')) {
        char* end = nullptr;
        const long v = std::strtol(part.c_str(), &end, 10);
        if (part.empty() || *end) fail("a node path is numbers with dots, as \"2.0.1\": \"" + text + "\"");
        out.push_back(int(v));
    }
    if (out.empty()) fail("a node path is needed");
    return out;
}

std::string pathText(const std::vector<int>& p) {
    std::string s;
    for (size_t i = 0; i < p.size(); ++i) s += (i ? "." : "") + std::to_string(p[i]);
    return s;
}

// The parameter values of a block as a JSON object (by the library's parameter names).
Json blockArgsJson(const TriggerDef& d, const TriggerCall& c) {
    Obj o;
    for (size_t i = 0; i < d.params.size() && i < c.args.size(); ++i) o.add(d.params[i].name, c.args[i]);
    return o;
}

Json blockJson(const TriggerCall& c) {
    const TriggerDef* d = TriggerDef::find(c.def);
    Obj o;
    o.add("block", c.def).add("enabled", c.enabled);
    if (!d) {
        o.add("unknown", true);
        return o;
    }
    o.add("title", d->title).add("sentence", d->shownSentence(c.args)).add("args", blockArgsJson(*d, c));
    if (!c.lists.empty()) {
        Json lists = jarr();
        for (const auto& l : c.lists) {
            Json items = jarr();
            for (const TriggerCall& k : l) items.items.push_back(blockJson(k));
            lists.items.push_back(items);
        }
        o.add("lists", lists);
    }
    return o;
}

// A block from its JSON form: its parameters by name (the defaults of the block library for the rest).
TriggerCall blockFromJson(const Json& j) {
    const std::string id = j.str("block");
    const TriggerDef* d = TriggerDef::find(id);
    if (!d) fail("no block \"" + id + "\" in the block library");
    TriggerCall c = TriggerCall::make(*d);
    c.enabled = j.boolean("enabled", true);
    if (const Json* args = j.get("args")) {
        for (const auto& f : args->fields) {
            const int i = d->param(f.first);
            if (i < 0) fail("block " + id + " has no parameter \"" + f.first + "\"");
            const Json& v = f.second;
            if (v.type == Json::Type::Number) c.args[size_t(i)] = v.n == std::floor(v.n) ? std::to_string(static_cast<long long>(v.n)) : std::to_string(v.n);
            else if (v.type == Json::Type::String) c.args[size_t(i)] = v.s;
            else if (v.type == Json::Type::Bool) c.args[size_t(i)] = v.b ? "1" : "0";
            else fail("parameter \"" + f.first + "\" must be a number, text or yes/no");
        }
    }
    if (const Json* lists = j.get("lists")) {
        for (size_t l = 0; l < lists->items.size() && l < d->lists.size(); ++l) {
            if (c.lists.size() <= l) c.lists.resize(d->lists.size());
            for (const Json& k : lists->items[l].items) c.lists[l].push_back(blockFromJson(k));
        }
    }
    return c;
}

// The block list of a node: the trigger's conditions or actions, or the list of a container block on the way `in`.
std::vector<TriggerCall>& blockList(TriggerNode& node, const std::string& list, const std::vector<Json>* in) {
    if (node.kind != TriggerNode::Kind::Trigger) fail("only a trigger has blocks");
    std::vector<TriggerCall>* v = list == "conditions" ? &node.conditions : list == "actions" ? &node.actions : nullptr;
    if (!v) fail("list must be \"conditions\" or \"actions\"");
    if (in) {
        for (const Json& step : *in) {
            if (step.items.size() != 2 || step.items[0].type != Json::Type::Number || step.items[1].type != Json::Type::Number)
                fail("\"in\" steps are [list, index]");
            const double at = step.items[1].n, l = step.items[0].n;
            if (at < 0 || at >= double(v->size()) || at != std::floor(at)) fail("\"in\": no block " + std::to_string(static_cast<long long>(at)));
            TriggerCall& call = (*v)[size_t(at)];
            // The container's lists are the block library's (TriggerDef::lists): a list the block does not have is refused.
            const TriggerDef* def = TriggerDef::find(call.def);
            if (!def || l < 0 || l >= double(def->lists.size()) || l != std::floor(l))
                fail("\"in\": block " + call.def + " has no list " + std::to_string(static_cast<long long>(l)));
            if (call.lists.size() < def->lists.size()) call.lists.resize(def->lists.size());
            v = &call.lists[size_t(l)];
        }
    }
    return *v;
}

// The kind of a variable by its name (TriggerVariable::kindLabel).
TriggerVariable::Kind variableKind(const std::string& name) {
    for (int k = 0; k < TriggerVariable::kKinds; ++k)
        if (name == TriggerVariable::kindLabel(TriggerVariable::Kind(k))) return TriggerVariable::Kind(k);
    fail("kind must be one of: number, flag, per player, array, unit, constant, text, unit group");
}

Json nodeJson(const TriggerNode& n, const std::vector<int>& path) {
    Obj o;
    o.add("path", pathText(path)).add("name", n.name).add("enabled", n.enabled).add("comment", n.comment);
    switch (n.kind) {
    case TriggerNode::Kind::Category: o.add("kind", "category"); break;
    case TriggerNode::Kind::Folder: o.add("kind", "folder"); break;
    case TriggerNode::Kind::Trigger: o.add("kind", "trigger"); break;
    case TriggerNode::Kind::Variable: o.add("kind", "variable").add("variable_kind", TriggerVariable::kindLabel(n.variable.kind)).add("initial", n.variable.initial); break;
    }
    if (n.kind == TriggerNode::Kind::Trigger) {
        Json conds = jarr(), acts = jarr();
        for (const TriggerCall& c : n.conditions) conds.items.push_back(blockJson(c));
        for (const TriggerCall& c : n.actions) acts.items.push_back(blockJson(c));
        o.add("conditions", conds).add("actions", acts);
    }
    if (!n.children.empty()) {
        Json kids = jarr();
        for (size_t i = 0; i < n.children.size(); ++i) {
            std::vector<int> p = path;
            p.push_back(int(i));
            kids.items.push_back(nodeJson(n.children[i], p));
        }
        o.add("children", kids);
    }
    return o;
}

// The category index of a parent word: a number, or the category's name.
std::vector<int> parentPath(const TriggerSet& set, const Json& op) {
    const Json* p = op.get("parent");
    if (!p) return {2};
    if (p->type == Json::Type::Number) return {int(p->n)};
    if (p->type == Json::Type::String) {
        for (size_t i = 0; i < set.roots.size(); ++i)
            if (lower(set.roots[i].name) == lower(p->s)) return {int(i)};
        return nodePath(p->s);
    }
    fail("\"parent\" is a category name or a node path");
}

TriggerNode& nodeAt(TriggerSet& set, const std::vector<int>& path) {
    TriggerNode* n = set.at(path);
    if (!n) fail("no node at " + pathText(path));
    return *n;
}

// One edit of the set (throws CliError on a bad one).
void applyTriggerOp(TriggerSet& set, const Json& op) {
    const std::string name = op.str("op");
    if (name == "add_folder" || name == "add_trigger" || name == "add_variable") {
        const std::vector<int> parent = parentPath(set, op);
        TriggerNode& p = nodeAt(set, parent);
        if (p.kind == TriggerNode::Kind::Trigger || p.kind == TriggerNode::Kind::Variable) fail("a trigger or a variable holds no nodes");
        const std::string title = textOf(op, "name");
        TriggerNode n;
        if (name == "add_folder") {
            n = TriggerSet::newFolder(title);
        } else if (name == "add_variable") {
            n = TriggerSet::newVariable(title, variableKind(op.str("kind", "number")));
            if (op.get("initial")) n.variable.initial = textOf(op, "initial");
            if (op.get("size")) n.variable.size = int(num(op, "size"));
        } else {
            n = TriggerSet::newTrigger(title);
            if (op.get("comment")) n.comment = textOf(op, "comment");
            n.enabled = op.boolean("enabled", true);
            if (op.get("conditions")) {
                n.conditions.clear();
                for (const Json& j : arrayOf(op, "conditions")) n.conditions.push_back(blockFromJson(j));
            }
            if (op.get("actions")) {
                n.actions.clear();
                for (const Json& j : arrayOf(op, "actions")) n.actions.push_back(blockFromJson(j));
            }
        }
        const int at = op.get("index") ? int(num(op, "index")) : int(p.children.size());
        p.children.insert(p.children.begin() + std::clamp(at, 0, int(p.children.size())), n);
    } else if (name == "set_node") {
        TriggerNode& n = nodeAt(set, nodePath(textOf(op, "path")));
        if (op.get("name")) n.name = textOf(op, "name");
        if (op.get("comment")) n.comment = textOf(op, "comment");
        if (op.get("enabled")) n.enabled = op.boolean("enabled", true);
        if (op.get("initial") && n.kind == TriggerNode::Kind::Variable) n.variable.initial = textOf(op, "initial");
        if (op.get("conditions") || op.get("actions")) {
            if (n.kind != TriggerNode::Kind::Trigger) fail("only a trigger has conditions and actions");
            if (op.get("conditions")) {
                n.conditions.clear();
                for (const Json& j : arrayOf(op, "conditions")) n.conditions.push_back(blockFromJson(j));
            }
            if (op.get("actions")) {
                n.actions.clear();
                for (const Json& j : arrayOf(op, "actions")) n.actions.push_back(blockFromJson(j));
            }
        }
    } else if (name == "delete_node") {
        const std::vector<int> p = nodePath(textOf(op, "path"));
        if (p.size() < 2) fail("categories cannot be deleted");
        nodeAt(set, p);
        set.remove(p);
    } else if (name == "move_node") {
        const std::vector<int> from = nodePath(textOf(op, "path"));
        const std::vector<int> parent = parentPath(set, op);
        const int index = op.get("index") ? int(num(op, "index")) : 0;
        if (set.move(from, parent, index).empty()) fail("the node cannot move there");
    } else if (name == "add_block") {
        TriggerNode& n = nodeAt(set, nodePath(textOf(op, "path")));
        std::vector<TriggerCall>& list = blockList(n, textOf(op, "list"), op.get("in") ? &arrayOf(op, "in") : nullptr);  // (see blockList)
        const int at = op.get("index") ? int(num(op, "index")) : int(list.size());
        list.insert(list.begin() + std::clamp(at, 0, int(list.size())), blockFromJson(need(op, "block")));
    } else if (name == "set_block") {
        TriggerNode& n = nodeAt(set, nodePath(textOf(op, "path")));
        std::vector<TriggerCall>& list = blockList(n, textOf(op, "list"), op.get("in") ? &arrayOf(op, "in") : nullptr);
        const int at = int(num(op, "index"));
        if (at < 0 || at >= int(list.size())) fail("no block " + std::to_string(at));
        list[size_t(at)] = blockFromJson(need(op, "block"));
    } else if (name == "delete_block") {
        TriggerNode& n = nodeAt(set, nodePath(textOf(op, "path")));
        std::vector<TriggerCall>& list = blockList(n, textOf(op, "list"), op.get("in") ? &arrayOf(op, "in") : nullptr);
        const int at = int(num(op, "index"));
        if (at < 0 || at >= int(list.size())) fail("no block " + std::to_string(at));
        list.erase(list.begin() + at);
    } else {
        fail("unknown trigger op \"" + name + "\": add_folder, add_trigger, add_variable, set_node, delete_node, move_node, add_block, set_block, delete_block");
    }
}

}  // namespace

static Json cmdProjectTriggers(Session& s) {
    if (s.args.positional.empty()) fail("usage: stardraft-cli project-triggers <project> [--path p]");
    const EudProject pr = openProject(s.args.positional[0]);
    TriggerSet set;
    std::string err;
    if (!set.load(triggersFileOf(pr), err)) fail(err);
    if (s.args.has("path")) {
        const std::vector<int> p = nodePath(s.args.get("path"));
        const TriggerNode* n = set.at(p);
        if (!n) fail("no node at " + pathText(p));
        return nodeJson(*n, p);
    }
    Json roots = jarr();
    for (size_t i = 0; i < set.roots.size(); ++i) roots.items.push_back(nodeJson(set.roots[i], {int(i)}));
    return Obj().add("project", ustr(pr.file())).add("format", set.fileVersion).add("categories", roots);
}

static Json cmdProjectTriggersApply(Session& s) {
    if (s.args.positional.size() < 2) fail("usage: stardraft-cli project-triggers-apply <project> <ops.json | '[...]'>");
    const EudProject pr = openProject(s.args.positional[0]);
    const std::string src = s.args.positional[1];
    const bool inlineText = !src.empty() && (src[0] == '[' || src[0] == '{');
    if (src == "-") fail("project-triggers-apply takes its operations inline or from a file");
    const std::string text = inlineText ? src : readText(upath(src));
    const Json doc = parseJson(text, "the operations");
    const Json* ops = doc.type == Json::Type::Array ? &doc : doc.get("ops");
    if (!ops || ops->type != Json::Type::Array) fail("the operations must be a list, or an object with \"ops\": [...]");
    TriggerSet set;
    std::string err;
    if (!set.load(triggersFileOf(pr), err)) fail(err);
    for (size_t i = 0; i < ops->items.size(); ++i) {
        try {
            applyTriggerOp(set, ops->items[i]);
        } catch (const std::exception& e) {
            fail("op " + std::to_string(i) + " (" + ops->items[i].str("op", "?") + "): " + e.what() + "; the triggers are not changed");
        }
    }
    if (!set.save(triggersFileOf(pr), err)) fail(err);
    return Obj().add("saved", ustr(triggersFileOf(pr))).add("operations", int(ops->items.size()));
}
