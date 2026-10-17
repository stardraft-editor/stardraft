// Edit > Undo history (portable, core.inc; feedback "undo-redo"): the steps of the undo that Ctrl+Z uses in the
// workspace that shows (the map's steps, the Data workspace's, the Triggers workspace's), each with its name, in a
// list picker (views_list_pick.inc) that opens under the top bar's Undo icon. The newest step is at the top; the
// current position has the check mark. A pick undoes (or redoes) all the steps to that one at once: the document is
// then as after that step. The program opens the list and does the jump (sdl_history.inc).

// A step's name in the language in use: the English text translated, then its %d (the number) and its %s (a name;
// the second %s: `name2`) put in.
std::wstring historyText(const wchar_t* english, int count = -1, const std::wstring& name = {}, const std::wstring& name2 = {}) {
    std::wstring t = tr(english);
    if (const size_t at = t.find(L"%d"); at != std::wstring::npos) t.replace(at, 2, std::to_wstring(std::max(count, 0)));
    size_t from = 0;
    for (const std::wstring* s : {&name, &name2}) {
        const size_t at = t.find(L"%s", from);
        if (at == std::wstring::npos) break;
        t.replace(at, 2, *s);
        from = at + s->size();
    }
    return t;
}
std::wstring historyText(const Editor::StepName& n) { return n.text.empty() ? std::wstring() : historyText(widen(n.text).c_str(), n.count); }

// The steps of one history: done[0] is the step that Undo takes back next (the newest), redo[0] the one that Redo
// gives again next.
struct UndoHistory {
    std::vector<std::wstring> done, redo;
    bool more = false;  // there are more steps done than `done` has (the list has no start row then)
};

// The longest list that the history shows on each side of the current position (the map can keep many thousands of
// steps; a name compares the map before and after its step).
constexpr size_t kHistoryShown = 300;

UndoHistory mapHistory(const Editor& ed, size_t most = kHistoryShown) {
    UndoHistory h;
    for (size_t k = 0; k < std::min(most, ed.undoSteps()); ++k) h.done.push_back(historyText(ed.undoStepName(k)));
    for (size_t k = 0; k < std::min(most, ed.redoSteps()); ++k) h.redo.push_back(historyText(ed.redoStepName(k)));
    h.more = ed.undoSteps() > most;
    return h;
}

// --- The Triggers workspace's steps: a name from the set before and after ------------------------------------------

// Each trigger, variable and folder of a set in its order (the categories are not in it), with its kind, its name and
// a hash of its text (as the file has it; a folder: its name only, its triggers come after it), and a trigger's blocks
// (its conditions, then its actions: each block's id and a hash of its values and the blocks in it).
struct TrigHistoryNode {
    TriggerNode::Kind kind;
    std::string name;
    size_t text = 0;
    std::vector<std::pair<std::string, size_t>> blocks;
    bool operator==(const TrigHistoryNode& o) const { return kind == o.kind && name == o.name && text == o.text; }
};
size_t trigBlockHash(const TriggerCall& c) {
    size_t h = std::hash<std::string>{}(c.def) ^ (c.enabled ? 0x9e3779b9u : 0u);
    auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    for (const std::string& a : c.args) mix(std::hash<std::string>{}(a));
    for (const auto& list : c.lists)
        for (const TriggerCall& k : list) mix(trigBlockHash(k));
    for (const auto& list : c.argBlocks)
        for (const TriggerCall& k : list) mix(trigBlockHash(k));
    return h;
}
void trigHistoryNodes(const TriggerNode& n, std::vector<TrigHistoryNode>& out) {
    if (n.kind == TriggerNode::Kind::Folder) out.push_back({n.kind, n.name, std::hash<std::string>{}(n.name), {}});
    else if (n.kind != TriggerNode::Kind::Category) {
        TriggerSet one;
        one.roots.resize(1);
        one.roots[0].children = {n};
        TrigHistoryNode h{n.kind, n.name, std::hash<std::string>{}(one.text()), {}};
        for (const auto* list : {&n.conditions, &n.actions})
            for (const TriggerCall& c : *list) h.blocks.emplace_back(c.def, trigBlockHash(c));
        out.push_back(std::move(h));
        return;
    }
    for (const TriggerNode& c : n.children) trigHistoryNodes(c, out);
}

// A block's title in the language in use ("Set resources").
std::wstring trigBlockTitle(const std::string& def) {
    const TriggerDef* d = TriggerDef::find(def);
    return d ? tr(widen(d->title)) : widen(def);
}

// The name of a change of one trigger: a block added, deleted or changed ("Wave 1: Add Set resources"), else the
// trigger changed.
std::wstring trigNodeChangeName(const TrigHistoryNode& a, const TrigHistoryNode& b) {
    const auto& x = a.blocks;
    const auto& y = b.blocks;
    const std::wstring trig = widen(b.name);
    auto missing = [](const auto& in, const auto& from) -> const std::pair<std::string, size_t>* {
        for (const auto& k : in)
            if (std::find(from.begin(), from.end(), k) == from.end()) return &k;
        return nullptr;
    };
    if (y.size() == x.size() + 1)
        if (const auto* k = missing(y, x)) return historyText(L"%s: Add %s", -1, trig, trigBlockTitle(k->first));
    if (x.size() == y.size() + 1)
        if (const auto* k = missing(x, y)) return historyText(L"%s: Delete %s", -1, trig, trigBlockTitle(k->first));
    if (x.size() == y.size()) {
        int changed = 0;
        size_t at = 0;
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != y[i]) ++changed, at = i;
        if (changed == 1 && x[at].first == y[at].first) return historyText(L"%s: Change %s", -1, trig, trigBlockTitle(y[at].first));
        if (changed > 1 && std::is_permutation(x.begin(), x.end(), y.begin(), y.end())) return historyText(L"%s: Move the blocks", -1, trig);
    }
    return historyText(L"Change %s", -1, trig);
}
// The nodes of a set, kept for each set's text: a step of the history is read once, also when the list opens again.
std::vector<TrigHistoryNode> trigHistoryNodesOf(const TriggerSet& set) {
    static std::unordered_map<size_t, std::vector<TrigHistoryNode>> known;
    const size_t key = std::hash<std::string>{}(set.text());
    if (const auto it = known.find(key); it != known.end()) return it->second;
    if (known.size() > 600) known.clear();
    std::vector<TrigHistoryNode> nodes;
    for (const TriggerNode& r : set.roots) trigHistoryNodes(r, nodes);
    known.emplace(key, nodes);
    return nodes;
}

std::wstring trigStepName(const TriggerSet& a, const TriggerSet& b) {
    const std::vector<TrigHistoryNode> x = trigHistoryNodesOf(a), y = trigHistoryNodesOf(b);
    auto missing = [](const std::vector<TrigHistoryNode>& in, const std::vector<TrigHistoryNode>& from) -> const TrigHistoryNode* {
        for (const auto& n : in)
            if (std::find(from.begin(), from.end(), n) == from.end()) return &n;
        return nullptr;
    };
    if (y.size() == x.size() + 1)
        if (const TrigHistoryNode* n = missing(y, x)) return historyText(L"Add %s", -1, widen(n->name));
    if (x.size() == y.size() + 1)
        if (const TrigHistoryNode* n = missing(x, y)) return historyText(L"Delete %s", -1, widen(n->name));
    if (x.size() == y.size()) {
        std::vector<size_t> diff;
        for (size_t i = 0; i < x.size(); ++i)
            if (!(x[i] == y[i])) diff.push_back(i);
        if (diff.size() == 1) {
            const size_t i = diff[0];
            if (x[i].name != y[i].name) return historyText(L"Rename %s", -1, widen(y[i].name));
            return trigNodeChangeName(x[i], y[i]);
        }
        if (!diff.empty()) {
            int renamed = 0;
            size_t at = 0;
            for (size_t i : diff)
                if (x[i].name != y[i].name) ++renamed, at = i;
            if (renamed == 1) return historyText(L"Rename %s", -1, widen(y[at].name));  // (a variable and its uses)
            if (std::is_permutation(x.begin(), x.end(), y.begin(), y.end())) return historyText(L"Move %s", -1, widen(y[diff[0]].name));
        }
    }
    return historyText(L"Change the triggers");
}

// --- The Data workspace's steps -------------------------------------------------------------------------------------

// A Data step's name: the entry and its tab, and the value that changed when it is one ("Terran Marine (Units):
// Change Hit Points"), found from the table before and after the step.
std::wstring dataStepName(const DataEditStep& s) {
    std::wstring what;
    if (s.tab >= 0 && s.tab < dataTabCount()) what = dataEntryName(gDataListProgram, s.tab, s.entry) + L" (" + tr(tabTitle(s.tab)) + L")";
    if (what.empty()) return historyText(L"Change the data");
    const DatTable* t = s.extra ? nullptr : datTable(s.tab);
    if (t && s.before.size() == t->bytes().size() && s.after.size() == s.before.size()) {
        DatTable a = *t, b = *t;
        a.setBytes(s.before), b.setBytes(s.after);
        const DatField* one = nullptr;
        int changed = 0;
        for (const DatField& f : t->schema().fields)
            if (s.entry >= f.first && s.entry <= f.last && a.value(f, s.entry) != b.value(f, s.entry)) ++changed, one = &f;
        if (changed == 1) return historyText(L"%s: Change %s", -1, what, tr(widen(one->name)));
    }
    return historyText(L"Change %s", -1, what);
}

// How many of the Data workspace's Players edits Ctrl+Z (redo: Ctrl+Y) in Data takes before its data steps: from the
// newest, each one while it is still the map's step at that place (a Map edit between two Players edits stops the count:
// Data's Undo does not take the Map edit, and the Players edits before it go from the list, views_data_players.inc).
size_t dataMapStepsShown(bool redo) {
    if (!dataMapCanUndo(redo)) return 0;
    const Editor& ed = *shellState().ed;
    const auto& list = redo ? gDataMapRedo : gDataMapSteps;
    const size_t can = redo ? ed.redoSteps() : ed.undoSteps();
    size_t n = 0;
    while (n < list.size() && n < can && list[list.size() - 1 - n] == (redo ? ed.redoStateId(n) : ed.undoStateId(n))) ++n;
    return n;
}

// The history of the workspace that shows: the same steps, in the same order, as Ctrl+Z and Ctrl+Y take them there.
// Data: the Players steps of the map first (Data's Undo takes them first), then the Data steps.
UndoHistory workspaceHistory() {
    UndoHistory h;
    if (gWorkspace == Workspace::Triggers) {
        if (!trigCanUndo(false) && !trigCanUndo(true)) return h;
        const auto& st = gTrigHist.steps;
        for (size_t i = gTrigHist.at; i > 0 && h.done.size() < kHistoryShown; --i) h.done.push_back(trigStepName(st[i - 1].first, st[i].first));
        h.more = gTrigHist.at > h.done.size();
        for (size_t i = gTrigHist.at + 1; i < st.size() && h.redo.size() < kHistoryShown; ++i) h.redo.push_back(trigStepName(st[i - 1].first, st[i].first));
        return h;
    }
    const AppState& app = shellState();
    const bool map = app.ed && app.ed->isOpen();
    if (gWorkspace == Workspace::Data) {
        const DataEditHistory& d = dataState().history;
        const size_t players = dataMapStepsShown(false);
        if (players) h.done = mapHistory(*app.ed, players).done;
        for (size_t i = d.undo.size(); i > 0 && h.done.size() < kHistoryShown; --i) h.done.push_back(dataStepName(d.undo[i - 1]));
        h.more = players + d.undo.size() > h.done.size();
        if (const size_t again = dataMapStepsShown(true)) h.redo = mapHistory(*app.ed, again).redo;
        for (size_t i = d.redo.size(); i > 0 && h.redo.size() < kHistoryShown; --i) h.redo.push_back(dataStepName(d.redo[i - 1]));
        return h;
    }
    return map ? mapHistory(*app.ed) : h;
}

// The list picker's rows: the steps to redo (the last one at the top), then the steps done (the newest first), then
// the start of the history. A row's value is the number of steps done after it; `current` gets the row of now.
std::vector<ListPickItem> historyItems(const UndoHistory& h, std::string& current) {
    std::vector<ListPickItem> items;
    const size_t done = h.done.size();
    for (size_t k = h.redo.size(); k > 0; --k) items.push_back({h.redo[k - 1], tr(L"Redo"), std::to_string(done + k)});
    for (size_t k = 0; k < done; ++k) items.push_back({h.done[k], tr(L"Undo"), std::to_string(done - k)});
    if (!h.more) items.push_back({tr(L"Start of the history"), tr(L"Undo"), "0"});
    current = std::to_string(done);
    return items;
}

// The name of the step that Undo (redo: Redo) takes next in the workspace that shows ("" when there is none), for the
// status bar ("Undo: Add 3 units").
std::wstring workspaceNextStep(bool redo) {
    if (gWorkspace == Workspace::Triggers) {
        if (!trigCanUndo(redo)) return {};
        const auto& st = gTrigHist.steps;
        const size_t at = gTrigHist.at;
        return redo ? trigStepName(st[at].first, st[at + 1].first) : trigStepName(st[at - 1].first, st[at].first);
    }
    const AppState& app = shellState();
    const bool map = app.ed && app.ed->isOpen();
    if (gWorkspace == Workspace::Data) {
        if (dataMapCanUndo(redo)) return historyText(redo ? app.ed->redoStepName(0) : app.ed->undoStepName(0));
        const auto& list = redo ? dataState().history.redo : dataState().history.undo;
        return list.empty() ? std::wstring() : dataStepName(list.back());
    }
    if (!map) return {};
    return historyText(redo ? app.ed->redoStepName(0) : app.ed->undoStepName(0));
}

// The status bar's text after a jump of `steps` steps back (redo: forward); `name`: the step's name when it is one.
std::wstring historyMessage(bool redo, size_t steps, const std::wstring& name) {
    if (steps == 1 && !name.empty()) return historyText(redo ? L"Redo: %s" : L"Undo: %s", -1, name);
    return historyText(redo ? L"Redo: %d steps" : L"Undo: %d steps", int(steps));
}
