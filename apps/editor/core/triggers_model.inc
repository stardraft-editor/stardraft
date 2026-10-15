// The Triggers workspace's program parts (core): the project's trigger set (gTE, core/views_triggers_pane.inc) loaded,
// checked, saved and changed; the tree's commands (TrigTreeProgram, the tree gTrigTree), the clipboard, the templates,
// the undo steps and the two right-button menus as menu models (core/menu_model.inc). Both programs call these; what
// is still each program's own is declared below (main.cpp for Windows, sdl_triggers.inc for the SDL program). Included
// after core/views_triggers_pane.inc (main.cpp: right after it; core.inc: after views_triggers_pick.inc).

// --- What each program gives ----------------------------------------------------------------------------------------
void trigPaneRepaint();        // the pane paints again (core/dock_prefs.inc)
void trigInfoRepaint();        // the info strip under the pane paints again
void trigCodeRefresh(bool force = false);  // the code pane shows the set's code again (when it is open)
bool trigPaneFocused();        // the pane has the keyboard (Ctrl+C / X / V / D act on blocks)
bool trigTreeFocused();        // the tree has the keyboard
void trigFocusTree();          // the tree takes the keyboard
void trigFocusPane();          // the pane takes the keyboard
void trigTreeMenuAt(const std::vector<int>& path);  // the tree's menu at the mouse (trigTreeMenuModel, trigTreeMenuPick)
void showTriggerCode();        // Show the code (the code pane)
// The question before a node is deleted ("Delete the trigger "x"?"): true to delete.
bool trigAskDelete(const std::wstring& question);
// A list picker under `at` (the pane's coordinates): true and `out` when a row was picked.
bool trigPickFromPane(const std::wstring& title, std::vector<ListPickItem> items, draw::Point at, std::string& out);
// The block picker (core/views_triggers_pick.inc); the picked block's id, or "" when none was picked.
std::string showBlockPicker(bool condition, const std::string& startCategory = {}, const std::wstring& search = {});
extern TriggerCall* gTrigEditOverride;  // set: the pickers edit this value block (the text pop-out's chip), not the set
extern std::vector<std::string> gRecentBlocks;  // (core/views_triggers_pick.inc)

// --- The tree ---------------------------------------------------------------------------------------------------------

// The tree's program parts (core/views_triggers_tree.inc): the model's nodes and the commands (below).
class TrigTreeProgram final : public TrigTreeHost {
public:
    const TriggerNode* treeNode(const std::vector<int>& path) override;
    void treeSelected(const std::vector<int>& path) override;
    void treeRename(const std::vector<int>& path, const std::string& name) override;
    void treeDelete(const std::vector<int>& path) override;
    void treeMove(const std::vector<int>& from, const std::vector<int>& parent, int index) override;
    void treeMenu(const std::vector<int>& path) override;
};
TrigTreeProgram gTrigTreeProgram;
TrigTreeView gTrigTree(gTrigTreeProgram);
struct TrigClipboard {
    std::optional<TriggerNode> node;
    std::optional<TriggerCall> block;
    bool blockIsCondition = false;
};
TrigClipboard gTrigClip;
void trigCopy(bool cut);
void trigPaste();

std::filesystem::path triggersFile(const EudProject& pr) { return pr.folder() / "triggers.json"; }

// What the checker and the code need from the map: its locations, sounds, units and players.
TriggerMapInfo trigMapInfo() {
    TriggerMapInfo m;
    AppState& st = appState();
    if (!st.ed || !st.ed->isOpen()) return m;
    Editor& ed = *st.ed;
    m.known = true;
    m.locations.resize(size_t(ed.locationCount()));
    for (int i = 0; i < ed.locationCount(); ++i)
        if (ed.locationUsed(i) || i == kAnywhereLocation) m.locations[size_t(i)] = ed.locationName(i);
    for (const auto& f : ed.archiveFiles()) {
        std::string lower = f.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() > 4 && (lower.ends_with(".wav") || lower.ends_with(".ogg"))) m.sounds.push_back(f.name);
    }
    for (uint16_t i = 0; i < ed.unitTypes().size(); ++i) m.unitNames.push_back(ed.unitTypes()[i].name);
    for (const auto& u : ed.units()) m.units.push_back({u.serial, u.type, u.owner, u.x, u.y});
    for (int p = 0; p < 8; ++p) {
        const auto pl = ed.player(p);
        m.playerForce[p] = pl.force;
        m.playerActive[p] = pl.controller == Editor::Controller::Human || pl.controller == Editor::Controller::Computer;
    }
    return m;
}

void trigRecheck() { gTE.issues = gTE.set.check(trigMapInfo()); }

void trigSave() {
    trigRecheck();
    trigHistoryRecord();
    if (!gTE.project) return;
    if (gTE.set.newerFormat()) return setMessage(L"Did not save the triggers: a newer StarDraft 2 made triggers.json");
    std::string err;
    if (!gTE.set.save(triggersFile(*gTE.project), err)) shellLogError(widen(err));
}

// Issues of a node (its own and its blocks').
int trigIssueCount(const TriggerNode& n, TriggerIssue::Level level) { return trigIssueCount(n, level, gTE.issues); }

// The tree's label of a node: the name, with what to look at (errors, heavy) after it (core/views_triggers_tree.inc).
std::wstring trigTreeLabel(const TriggerNode& n) { return trigTreeLabel(n, gTE.issues); }

void trigTreeSelected(const std::vector<int>& path);

// Rebuilds the tree from the model, keeping `select` selected (and its parents open).
// `select` is a copy: callers often pass gTE.sel. As the native tree did, the selection is made again (the pane's
// selection and scroll reset, the code pane follows).
void trigRefreshTree(std::vector<int> select) {
    trigRecheck();
    gTrigTree.rebuild(gTE.set, gTE.issues, select);
    trigTreeSelected(select);
}

// A tab with this project closed: the Triggers workspace drops what it holds of it, so a project opened later at the
// same address loads its own triggers.json.
void trigForgetProject(const EudProject* pr) {
    if (pr && gTE.project == pr) gTE.project = nullptr, gTE.set = TriggerSet(), gTE.sel.clear(), trigHistoryReset();
}

// The active project's triggers.json, read when the project is not the one loaded (a file that did not load is read
// again). An older format is saved in the current one. True when the set was read (the tree is made again then).
bool trigLoadSet() {
    const EudProject* pr = activeProject();
    if (pr == gTE.project && pr && !gTE.set.loadFailed) return false;
    gTE.project = pr;
    gTE.set = TriggerSet();
    gTE.sel.clear();
    std::string err;
    if (pr && !gTE.set.load(triggersFile(*pr), err)) shellLogError(widen(err));
    if (pr && gTE.set.newerFormat())
        shellLogError(L"A newer StarDraft 2 made triggers.json. This StarDraft 2 shows what it can and does not save the file. Nothing in it is lost.");
    else if (pr && (gTE.set.upgradeNames(trigMapInfo()) > 0 || gTE.set.fileVersion < TriggerSet::kFormatVersion) && std::filesystem::exists(triggersFile(*pr))) {
        if (!gTE.set.save(triggersFile(*pr), err)) shellLogError(widen(err));
        else shellLogLine(L"triggers.json is now in the current format (version " + std::to_wstring(TriggerSet::kFormatVersion) + L")");
    }
    trigHistoryReset();
    return true;
}

// A unique name among the siblings: "Trigger", "Trigger 2", ... (variables: gold, gold2, ...).
std::string trigUniqueName(const TriggerNode& parent, const std::string& base, bool identifier = false) {
    const auto vars = gTE.set.variables();
    for (int i = 1;; ++i) {
        const std::string n = i == 1 ? base : base + (identifier ? "" : " ") + std::to_string(i);
        const bool taken = std::any_of(parent.children.begin(), parent.children.end(), [&](const TriggerNode& c) { return c.name == n; }) ||
                           (identifier && vars.count(n));
        if (!taken) return n;
    }
}

// Inserts a node: inside a category or folder, or after a trigger or variable (in its parent). Returns its path.
std::vector<int> trigInsertNode(const std::vector<int>& at, TriggerNode node) {
    TriggerNode* n = gTE.set.at(at);
    if (!n) return {};
    std::vector<int> parent = at;
    int index = -1;
    if (n->kind == TriggerNode::Kind::Trigger || n->kind == TriggerNode::Kind::Variable) parent.pop_back(), index = at.back() + 1;
    TriggerNode* p = gTE.set.at(parent);
    if (index < 0) index = int(p->children.size());
    p->children.insert(p->children.begin() + index, std::move(node));
    std::vector<int> path = parent;
    path.push_back(index);
    return path;
}

void trigAdd(const std::vector<int>& at, bool folder) {
    TriggerNode* n = gTE.set.at(at);
    if (!n) return;
    const TriggerNode* parent = n->kind == TriggerNode::Kind::Trigger || n->kind == TriggerNode::Kind::Variable
                                    ? gTE.set.at(std::vector<int>(at.begin(), at.end() - 1))
                                    : n;
    const std::vector<int> path = trigInsertNode(at, folder ? TriggerSet::newFolder(trigUniqueName(*parent, "New folder"))
                                                            : TriggerSet::newTrigger(trigUniqueName(*parent, "New trigger")));
    trigSave();
    trigRefreshTree(path);
    if (!gSelfTest) gTrigTree.startRename(gTrigTree.selected());
}

void trigAddVariable(const std::vector<int>& at, TriggerVariable::Kind kind) {
    static const char* bases[] = {"number", "flag", "perPlayer", "list", "unit", "CONSTANT"};
    TriggerNode* n = gTE.set.at(at);
    if (!n) return;
    const TriggerNode* parent = n->kind == TriggerNode::Kind::Trigger || n->kind == TriggerNode::Kind::Variable
                                    ? gTE.set.at(std::vector<int>(at.begin(), at.end() - 1))
                                    : n;
    const std::vector<int> path = trigInsertNode(at, TriggerSet::newVariable(trigUniqueName(*parent, bases[int(kind)], true), kind));
    trigSave();
    trigRefreshTree(path);
    setMessage(L"Added a variable. Press F2 to rename it.");
    if (!gSelfTest) gTrigTree.startRename(gTrigTree.selected());
}

// --- Templates: ready-made triggers to start from --------------------------------------------------------------

TriggerCall trigMake(const char* id) { return TriggerCall::make(*TriggerDef::find(id)); }
TriggerCall trigMake(const char* id, std::vector<std::string> args) {
    TriggerCall c = trigMake(id);
    for (size_t i = 0; i < args.size() && i < c.args.size(); ++i)
        if (!args[i].empty()) c.args[i] = args[i];
    return c;
}

// The templates are in the library (triggerTemplates); the tree's menus and the empty category list them.
struct TrigTemplate {
    std::wstring title, about;
    int category;  // where it goes: 1 On map start, 2 On every trigger cycle
    std::function<TriggerNode()> make;
};
const std::vector<TrigTemplate>& trigTemplates() {
    static const std::vector<TrigTemplate> t = [] {
        std::vector<TrigTemplate> out;
        for (const auto& tp : triggerTemplates()) {
            const TriggerNode node = tp.node;
            out.push_back({widen(tp.title), widen(tp.about), tp.category, [node] { return node; }});
        }
        return out;
    }();
    return t;
}

void trigAddTemplate(size_t i) {
    const auto& tp = trigTemplates()[i];
    TriggerNode node = tp.make();
    TriggerNode& cat = gTE.set.roots[size_t(tp.category)];
    node.name = trigUniqueName(cat, node.name);
    cat.children.push_back(std::move(node));
    const std::vector<int> path = {tp.category, int(cat.children.size()) - 1};
    trigSave();
    trigRefreshTree(path);
    setMessage(std::wstring(L"Added from a template: ") + tp.title);
}

void trigDelete(const std::vector<int>& path) {
    TriggerNode* n = gTE.set.at(path);
    if (!n || path.size() < 2) return;
    if (!gSelfTest) {
        const std::wstring what = n->kind == TriggerNode::Kind::Folder ? L"the folder \"" + widen(n->name) + L"\" and everything in it"
                                : n->kind == TriggerNode::Kind::Variable ? L"the variable \"" + widen(n->name) + L"\""
                                                                         : L"the trigger \"" + widen(n->name) + L"\"";
        if (!trigAskDelete(L"Delete " + what + L"?")) return;
    }
    gTE.set.remove(path);
    trigSave();
    trigRefreshTree(std::vector<int>(path.begin(), path.end() - 1));
}

// --- The tree's menu --------------------------------------------------------------------------------------------------
enum { kTrigTreeNewTrigger = 1, kTrigTreeNewFolder, kTrigTreeRename, kTrigTreeDelete, kTrigTreeToggle, kTrigTreeDuplicate, kTrigTreeCopy,
       kTrigTreeCut, kTrigTreePaste, kTrigTreeNewVar = 100, kTrigTreeTemplate = 200 };

// The tree's right-button menu of the node at `path` (empty: no node).
MenuModel trigTreeMenuModel(const std::vector<int>& path) {
    const TriggerNode* n = gTE.set.at(path);
    if (!n) return {};
    MenuModel m{menuItem(kTrigTreeNewTrigger, L"New &trigger")};
    MenuModel tpl;
    for (size_t i = 0; i < trigTemplates().size(); ++i) tpl.push_back(menuItem(kTrigTreeTemplate + int(i), trigTemplates()[i].title));
    m.push_back(menuPopup(L"New trigger from a te&mplate", std::move(tpl)));
    m.push_back(menuItem(kTrigTreeNewFolder, L"New &folder"));
    if (path[0] == 0) {  // variables live in Definitions
        MenuModel vars;
        for (int k = 0; k < TriggerVariable::kKinds; ++k)
            if (TriggerVariable::Kind(k) != TriggerVariable::Kind::UnitGroup)  // unit groups: hidden (a map that has one keeps it)
                vars.push_back(menuItem(kTrigTreeNewVar + k, widen(std::string("A ") + TriggerVariable::kindLabel(TriggerVariable::Kind(k)))));
        m.push_back(menuPopup(L"New &variable", std::move(vars)));
    }
    m.push_back(menuSeparator());
    if (n->kind != TriggerNode::Kind::Category) {
        m.push_back(menuItem(kTrigTreeCopy, L"&Copy", L"Ctrl+C"));
        m.push_back(menuItem(kTrigTreeCut, L"Cu&t", L"Ctrl+X"));
    }
    m.push_back(menuItem(kTrigTreePaste, L"&Paste", L"Ctrl+V", !gTrigClip.node));
    if (n->kind != TriggerNode::Kind::Category) {
        m.push_back(menuSeparator());
        m.push_back(menuItem(kTrigTreeRename, L"&Rename", L"F2"));
        if (n->kind == TriggerNode::Kind::Trigger) {
            m.push_back(menuItem(kTrigTreeDuplicate, L"D&uplicate", L"Ctrl+D"));
            m.push_back(menuItem(kTrigTreeToggle, n->enabled ? L"Turn &off" : L"Turn &on"));
        }
        m.push_back(menuItem(kTrigTreeDelete, L"De&lete", L"Del"));
    }
    return m;
}

// What the tree menu's item `pick` does (0: none was picked).
void trigTreeMenuPick(const std::vector<int>& path, int pick) {
    TriggerNode* n = gTE.set.at(path);
    if (!n) return;
    if (pick >= kTrigTreeTemplate && pick < kTrigTreeTemplate + 50) return trigAddTemplate(size_t(pick - kTrigTreeTemplate));
    if (pick >= kTrigTreeNewVar && pick < kTrigTreeNewVar + TriggerVariable::kKinds) return trigAddVariable(path, TriggerVariable::Kind(pick - kTrigTreeNewVar));
    switch (pick) {
    case kTrigTreeNewTrigger: trigAdd(path, false); break;
    case kTrigTreeNewFolder: trigAdd(path, true); break;
    case kTrigTreeRename: gTrigTree.startRename(gTrigTree.selected()); break;
    case kTrigTreeDelete: trigDelete(path); break;
    case kTrigTreeCopy: trigFocusTree(), trigCopy(false); break;
    case kTrigTreeCut: trigFocusTree(), trigCopy(true); break;
    case kTrigTreePaste: trigFocusTree(), trigPaste(); break;
    case kTrigTreeToggle:
        n->enabled = !n->enabled;
        trigSave(), trigRefreshTree(path);
        break;
    case kTrigTreeDuplicate: trigFocusTree(), trigCopy(false), trigPaste(); break;
    }
}

// Renames a trigger, folder or variable (a variable's uses follow). False, with a message, when the name cannot be.
bool trigRenameNode(std::vector<int> path, const std::string& name) {
    TriggerNode* n = gTE.set.at(path);
    if (!n || name.empty() || n->kind == TriggerNode::Kind::Category) return false;
    if (name == n->name) return true;
    if (n->kind == TriggerNode::Kind::Variable) {
        const bool ok = (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_') &&
                        std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
        if (!ok) {
            setMessage(L"Use only letters, digits and _ in a variable name. Not accepted: " + widen(name) + L" was not taken");
            return false;
        }
        if (gTE.set.variables().count(name)) {
            setMessage(L"There is already a variable with the name " + widen(name));
            return false;
        }
        const int uses = gTE.set.renameVariable(n->name, name);
        setMessage(L"Renamed the variable and " + std::to_wstring(uses) + L" uses of it");
    } else if (n->kind == TriggerNode::Kind::Trigger && !path.empty() && path[0] == 0) {  // a trigger in Definitions: its Run a trigger blocks too
        const int uses = gTE.set.renameTrigger(n->name, name);
        if (uses) setMessage(L"Renamed the trigger and " + std::to_wstring(uses) + L" Run a trigger blocks");
    } else {
        n->name = name;
    }
    trigSave();
    trigRefreshTree(path);
    return true;
}

// Undo and redo in the Triggers workspace: the set as it was, with what was selected when it changed.
// The step itself is trigHistoryStep (core/views_triggers_pane.inc); this saves the file and shows it.
bool trigUndo(bool redo) {
    std::vector<int> sel;
    if (!trigHistoryStep(redo, sel)) return false;
    std::string err;
    if (!gTE.set.newerFormat() && !gTE.set.save(triggersFile(*gTE.project), err)) shellLogError(widen(err));
    trigRecheck();
    trigRefreshTree(gTE.set.at(sel) ? sel : std::vector<int>{1});
    trigPaneRepaint();
    trigCodeRefresh();
    setMessage(redo ? L"Redo" : L"Undo");
    return true;
}

// --- The pane's changes -------------------------------------------------------------------------------------------------

void trigChanged() {
    if (gTrigEditOverride) return;  // a chip's value in the text pop-out: kept there until OK
    trigSave();
    trigCodeRefresh();
    // The tree's labels carry the issue counts.
    if (TriggerNode* n = trigSelectedNode()) gTrigTree.relabelSelected(trigTreeLabel(*n));
    trigPaneRepaint();
    trigInfoRepaint();
}

// Inserts at a list position; a list holding only Do nothing (or Always) has it replaced.
void trigInsert(TriggerNode& t, const TrigListRef& ref, int at, TriggerCall call) {
    std::vector<TriggerCall>* list = trigResolve(t, ref);
    if (!list) return;
    for (const char* placeholder : {"act.donothing", "cond.always"})
        if (list->size() == 1 && (*list)[0].def == placeholder && call.def != placeholder) list->clear(), at = 0;
    at = std::clamp(at, 0, int(list->size()));
    list->insert(list->begin() + at, std::move(call));
    gTP.sel = {ref, at};
}

void trigAddTo(TrigListRef ref, int at, const TriggerDef& d) {
    TriggerNode* t = trigSelected();
    if (!t) return;
    std::erase(gRecentBlocks, d.id);
    gRecentBlocks.insert(gRecentBlocks.begin(), d.id);
    if (gRecentBlocks.size() > 10) gRecentBlocks.resize(10);
    trigInsert(*t, ref, at, TriggerCall::make(d));
    if (ref.via.empty()) gTP.collapsed[ref.list] = false;
    trigChanged();
    setMessage(L"Added " + widen(d.title));
}

// Opens the block picker for a list, adding the pick at `at` (-1: the end).
// `ref` is a copy: the picker repaints the pane, which rebuilds the hit lists it may come from.
void trigAddWithPicker(TrigListRef ref, int at) {
    TriggerNode* t = trigSelected();
    if (!t) return;
    const bool cond = trigListIsCondition(*t, ref);
    const std::string id = showBlockPicker(cond);
    if (id.empty()) return;
    t = trigSelected();
    std::vector<TriggerCall>* list = t ? trigResolve(*t, ref) : nullptr;
    if (list) trigAddTo(ref, at < 0 ? int(list->size()) : at, *TriggerDef::find(id));
}

// Where pasted or added blocks go: after the selected block when it is of the same kind, else the end of the
// trigger's conditions or actions.
std::pair<TrigListRef, int> trigAddTarget(bool condition) {
    TriggerNode* t = trigSelected();
    if (t && trigBlock(*t, gTP.sel) && trigListIsCondition(*t, gTP.sel.list) == condition) return {gTP.sel.list, gTP.sel.index + 1};
    TrigListRef top{{}, condition ? 0 : 1};
    return {top, t ? int(trigResolve(*t, top)->size()) : 0};
}

void trigMoveLine(int delta) {
    TriggerNode* t = trigSelected();
    if (!t || !trigBlock(*t, gTP.sel)) return;
    std::vector<TriggerCall>& list = *trigResolve(*t, gTP.sel.list);
    const int to = gTP.sel.index + delta;
    if (to < 0 || to >= int(list.size())) return;
    std::swap(list[size_t(gTP.sel.index)], list[size_t(to)]);
    gTP.sel.index = to;
    trigChanged();
}

void trigDeleteLine() {
    TriggerNode* t = trigSelected();
    if (!t || !trigBlock(*t, gTP.sel)) return;
    std::vector<TriggerCall>& list = *trigResolve(*t, gTP.sel.list);
    list.erase(list.begin() + gTP.sel.index);
    gTP.sel = {};
    trigChanged();
}

// A dragged block dropped where gTP.dropList and gTP.dropIdx say.
void trigDrop() {
    TriggerNode* t = trigSelected();
    TriggerCall* src = t && gTP.dropIdx >= 0 ? trigBlock(*t, gTP.drag) : nullptr;
    if (!src) return;
    TriggerCall moving = *src;
    TrigListRef to = gTP.dropList;
    int at = gTP.dropIdx;
    const auto& from = gTP.drag;
    if (to == from.list && at > from.index) --at;
    const size_t depth = from.list.via.size();
    if (to.via.size() > depth && std::equal(from.list.via.begin(), from.list.via.end(), to.via.begin()) && to.via[depth].first == from.list.list &&
        to.via[depth].second > from.index)
        --to.via[depth].second;
    std::vector<TriggerCall>& fromList = *trigResolve(*t, from.list);
    fromList.erase(fromList.begin() + from.index);
    trigInsert(*t, to, at, std::move(moving));
    trigChanged();
}

// A heavy trigger's fix (the pane's Apply link).
void trigApplyCostFix(const TriggerCostFix& fix) {
    TriggerNode* t = trigSelected();
    if (!t) return;
    const int before = TriggerSet::cost(*t);
    if (!TriggerSet::applyCostFix(*t, fix)) return;
    gTP.sel = {}, gTP.sel.index = -1;
    trigChanged();
    trigRefreshTree(gTE.sel);  // the tree's "heavy" mark
    setMessage(L"The trigger costs about " + std::to_wstring(TriggerSet::cost(*trigSelected())) + L" actions each run (it was " + std::to_wstring(before) +
               L"). Ctrl+Z undoes it.");
}

// The buttons on a category's or folder's page: add a trigger, one from a template, a folder or a variable there.
// `button` is the button's place on the pane (the picker opens under it).
void trigAddFromPane(int action, RECT button) {
    const std::vector<int> at = gTE.sel;
    if (!gTE.set.at(at)) return;
    if (action == 0 || action == 2) return trigAdd(at, action == 2);
    std::vector<ListPickItem> items;
    if (action == 1) {
        for (size_t i = 0; i < trigTemplates().size(); ++i)
            if (trigTemplates()[i].category == at[0]) items.push_back({trigTemplates()[i].title, L"", std::to_string(i)});
    } else {
        for (int k = 0; k < TriggerVariable::kKinds; ++k)
            if (TriggerVariable::Kind(k) != TriggerVariable::Kind::UnitGroup)
                items.push_back({widen(std::string("A ") + TriggerVariable::kindLabel(TriggerVariable::Kind(k))), L"", std::to_string(k)});
    }
    std::string pick;
    if (!trigPickFromPane(action == 1 ? L"Templates" : L"Kind", items, {button.left, button.bottom + scale(4)}, pick) || pick.empty()) return;
    if (action == 1) trigAddTemplate(size_t(std::stoul(pick)));
    else trigAddVariable(at, TriggerVariable::Kind(std::stoi(pick)));
}

// --- The pane's menu --------------------------------------------------------------------------------------------------
enum { kTrigPaneAddCond = 1, kTrigPaneAddAct, kTrigPaneUp, kTrigPaneDown, kTrigPaneToggle, kTrigPaneDelete, kTrigPaneFold, kTrigPaneCopy, kTrigPaneCut,
       kTrigPanePaste, kTrigPaneCode, kTrigPaneDuplicate, kTrigPaneInside = 100, kTrigPaneBlock = 1000 };

// The pane's right-button menu (the selected trigger; empty: none is selected). `blocks` gets the blocks of the Add
// submenus (item kTrigPaneBlock + index).
MenuModel trigPaneMenuModel(std::vector<const TriggerDef*>& blocks) {
    blocks.clear();
    TriggerNode* t = trigSelected();
    if (!t) return {};
    TriggerCall* sel = trigBlock(*t, gTP.sel);
    const TriggerDef* sd = sel ? TriggerDef::find(sel->def) : nullptr;
    MenuModel m;
    // Add a condition, Add an action: submenus of the picker's categories and their blocks (as the Add window lists
    // them), and Search... (the window). The block goes after the selected block of its kind, else at the end.
    auto addMenu = [&](bool cond) {
        MenuModel sub{menuItem(cond ? kTrigPaneAddCond : kTrigPaneAddAct, L"&Search\x2026"), menuSeparator()};
        constexpr int kPart = 15;  // the most blocks in one submenu: a menu taller than the screen cannot show all (it does not scroll)
        for (const std::string& cat : TriggerDef::categories(cond)) {
            std::vector<const TriggerDef*> list;
            for (const auto& d : TriggerDef::all())
                if (!d.value && d.condition == cond && d.tier != "replaced" && d.tier != "hidden" && d.category == cat) list.push_back(&d);
            const int parts = (int(list.size()) + kPart - 1) / kPart;
            for (int part = 0; part < parts; ++part) {
                MenuModel cm;
                for (int k = part * kPart; k < std::min(int(list.size()), (part + 1) * kPart); ++k) {
                    std::wstring title = widen(list[size_t(k)]->title);
                    for (size_t q = title.find(L'&'); q != std::wstring::npos; q = title.find(L'&', q + 2)) title.insert(q, 1, L'&');
                    cm.push_back(menuItem(kTrigPaneBlock + int(blocks.size()), title));
                    blocks.push_back(list[size_t(k)]);
                }
                std::wstring name = widen(cat);
                if (parts > 1) name += L" (" + std::to_wstring(part + 1) + L" of " + std::to_wstring(parts) + L")";
                sub.push_back(menuPopup(name, std::move(cm)));
            }
        }
        m.push_back(menuPopup(cond ? L"Add a &condition" : L"Add an &action", std::move(sub)));
    };
    addMenu(true);
    addMenu(false);
    if (sd)
        for (size_t l = 0; l < sd->lists.size(); ++l) m.push_back(menuItem(kTrigPaneInside + int(l), L"Add to " + widen(sd->lists[l].title) + L"\x2026"));
    m.push_back(menuSeparator());
    if (sel) m.push_back(menuItem(kTrigPaneCopy, L"&Copy", L"Ctrl+C")), m.push_back(menuItem(kTrigPaneCut, L"Cu&t", L"Ctrl+X"));
    m.push_back(menuItem(kTrigPanePaste, L"&Paste", L"Ctrl+V", !gTrigClip.block));
    if (sel) m.push_back(menuItem(kTrigPaneDuplicate, L"D&uplicate", L"Ctrl+D"));
    if (sel) {
        const int n = int(trigResolve(*t, gTP.sel.list)->size());
        m.push_back(menuSeparator());
        if (sd && !sd->lists.empty()) m.push_back(menuItem(kTrigPaneFold, sel->folded ? L"&Unfold" : L"&Fold"));
        m.push_back(menuItem(kTrigPaneUp, L"Move &up", L"Alt+Up", gTP.sel.index <= 0));
        m.push_back(menuItem(kTrigPaneDown, L"Move &down", L"Alt+Down", gTP.sel.index + 1 >= n));
        m.push_back(menuItem(kTrigPaneToggle, sel->enabled ? L"Turn o&ff" : L"Turn &on"));
        m.push_back(menuItem(kTrigPaneDelete, L"De&lete", L"Del"));
    }
    m.push_back(menuSeparator());
    m.push_back(menuItem(kTrigPaneCode, L"Show the &code"));
    return m;
}

// What the pane menu's item `pick` does (0: none was picked); `blocks` as trigPaneMenuModel made them.
void trigPaneMenuPick(int pick, const std::vector<const TriggerDef*>& blocks) {
    TriggerNode* t = trigSelected();
    if (!t) return;
    TriggerCall* sel = trigBlock(*t, gTP.sel);
    if (pick >= kTrigPaneBlock && pick < kTrigPaneBlock + int(blocks.size())) {  // a block from the Add submenus
        const TriggerDef& d = *blocks[size_t(pick - kTrigPaneBlock)];
        const auto [ref, at] = trigAddTarget(d.condition);
        trigAddTo(ref, at, d);
        return;
    }
    if (pick >= kTrigPaneInside && sel) return trigAddWithPicker(trigInside(gTP.sel, pick - kTrigPaneInside), -1);
    switch (pick) {
    case kTrigPaneAddCond:
    case kTrigPaneAddAct: {
        const bool cond = pick == kTrigPaneAddCond;
        const std::string id = showBlockPicker(cond);
        if (id.empty()) return;
        const auto [ref, at] = trigAddTarget(cond);
        trigAddTo(ref, at, *TriggerDef::find(id));
        return;
    }
    case kTrigPaneUp: trigMoveLine(-1); return;
    case kTrigPaneDown: trigMoveLine(1); return;
    case kTrigPaneDelete: trigDeleteLine(); return;
    case kTrigPaneToggle:
        if (sel) sel->enabled = !sel->enabled, trigChanged();
        return;
    case kTrigPaneFold:
        if (sel) sel->folded = !sel->folded, trigChanged();
        return;
    case kTrigPaneCopy:
    case kTrigPaneCut: trigFocusPane(), trigCopy(pick == kTrigPaneCut); return;
    case kTrigPanePaste: trigFocusPane(), trigPaste(); return;
    case kTrigPaneDuplicate: trigFocusPane(), trigDuplicate(); return;
    case kTrigPaneCode: showTriggerCode(); return;
    }
}

// --- Copy, cut, paste and duplicate -----------------------------------------------------------------------------------
// With the pane focused: the selected block in the pane (with all it holds); else the tree's trigger, folder or
// variable. Pasted nodes get a unique name; pasted blocks go after the selected block when it is of the same kind,
// else at the end of the trigger's conditions or actions.

void trigCopy(bool cut) {
    TriggerNode* t = trigSelected();
    if (trigPaneFocused()) {
        TriggerCall* b = t ? trigBlock(*t, gTP.sel) : nullptr;
        if (!b) return;
        gTrigClip.block = *b;
        gTrigClip.blockIsCondition = trigListIsCondition(*t, gTP.sel.list);
        const TriggerDef* d = TriggerDef::find(b->def);
        setMessage((cut ? L"Cut " : L"Copied ") + widen(d ? d->title : b->def));
        if (cut) trigDeleteLine();
        return;
    }
    TriggerNode* n = gTE.set.at(gTE.sel);
    if (!n || gTE.sel.size() < 2) return;
    gTrigClip.node = *n;
    setMessage((cut ? L"Cut " : L"Copied ") + widen(n->name));
    if (cut) {
        gTE.set.remove(gTE.sel);
        trigSave();
        trigRefreshTree(std::vector<int>(gTE.sel.begin(), gTE.sel.end() - 1));
    }
}

// Ctrl+D: a copy of the selected block right below it (selected), or of the selected trigger, folder or
// variable in the tree. The clipboard is left alone.
void trigDuplicate() {
    TriggerNode* t = trigSelected();
    if (!trigTreeFocused() && t && trigBlock(*t, gTP.sel)) {
        std::vector<TriggerCall>& list = *trigResolve(*t, gTP.sel.list);
        const TriggerCall copy = list[size_t(gTP.sel.index)];
        list.insert(list.begin() + gTP.sel.index + 1, copy);
        ++gTP.sel.index;
        gTP.focus = {}, gTP.focus.ref.index = -1;
        trigChanged();
        setMessage(L"Duplicated");
        return;
    }
    TriggerNode* n = gTE.set.at(gTE.sel);
    if (!n || gTE.sel.size() < 2) return;
    TriggerNode node = *n;
    std::vector<int> parent(gTE.sel.begin(), gTE.sel.end() - 1);
    node.name = trigUniqueName(*gTE.set.at(parent), node.name, node.kind == TriggerNode::Kind::Variable);
    const std::vector<int> path = trigInsertNode(gTE.sel, std::move(node));
    trigSave();
    trigRefreshTree(path);
}

void trigPaste() {
    if (trigPaneFocused() || (!gTrigClip.node && gTrigClip.block)) {
        TriggerNode* t = trigSelected();
        if (!t || !gTrigClip.block) return;
        const auto [ref, at] = trigAddTarget(gTrigClip.blockIsCondition);
        trigInsert(*t, ref, at, *gTrigClip.block);
        trigChanged();
        setMessage(L"Pasted");
        return;
    }
    if (!gTrigClip.node || !gTE.set.at(gTE.sel)) return;
    TriggerNode node = *gTrigClip.node;
    std::vector<int> parent = gTE.sel;
    if (const TriggerNode* at = gTE.set.at(gTE.sel); at->kind == TriggerNode::Kind::Trigger || at->kind == TriggerNode::Kind::Variable) parent.pop_back();
    node.name = trigUniqueName(*gTE.set.at(parent), node.name, node.kind == TriggerNode::Kind::Variable);
    const std::vector<int> path = trigInsertNode(gTE.sel, std::move(node));
    trigSave();
    trigRefreshTree(path);
    setMessage(L"Pasted " + widen(gTE.set.at(path)->name));
}

// --- The tree's commands (TrigTreeHost) -------------------------------------------------------------------------------
const TriggerNode* TrigTreeProgram::treeNode(const std::vector<int>& path) { return gTE.set.at(path); }

void trigTreeSelected(const std::vector<int>& path) {
    gTE.sel = path;
    gTE.scroll = 0;
    gTP.sel = {};
    trigCodeRefresh();
    trigPaneRepaint();
    trigInfoRepaint();
}
void TrigTreeProgram::treeSelected(const std::vector<int>& path) { trigTreeSelected(path); }
void TrigTreeProgram::treeRename(const std::vector<int>& path, const std::string& name) { trigRenameNode(path, name); }
void TrigTreeProgram::treeDelete(const std::vector<int>&) { trigDelete(gTE.sel); }
// Drop: into a category or folder, or between triggers and variables (in their folder).
void TrigTreeProgram::treeMove(const std::vector<int>& from, const std::vector<int>& parent, int index) {
    const std::vector<int> moved = gTE.set.move(from, parent, index);
    if (!moved.empty()) trigSave(), trigRefreshTree(moved);
}
void TrigTreeProgram::treeMenu(const std::vector<int>& path) { trigTreeMenuAt(path); }
