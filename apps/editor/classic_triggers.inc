// Scenario > Classic triggers (roadmap L.2; included into main.cpp after the workspaces): the map's TRIG, as StarEdit
// edits it. Only for a classic map, or a project whose map has classic triggers (L.4: they stay in the map and run with
// the project's triggers). One window: at the left the triggers (shown for a player or for all), at the right the
// selected trigger's players, conditions and actions, and under them the selected condition or action: its type and
// a picker for each value (players, units, the map's locations, switches, sounds and strings, numbers, ...).
// The trigger list and the conditions and actions are painted with the kit (ui::Surface): rows that a drag by the
// grip moves, row actions, the values as chips to click, "+ trigger", "+ condition" and "+ action" links. Keys on a
// list: Delete, Ctrl+C, Ctrl+V, Ctrl+Up, Ctrl+Down, Up, Down.
// A page in the main area (map_pages.inc); it edits a working copy, and Apply writes TRIG (and the new strings) as one
// undo step (Editor::setClassicTriggers).

// Control ids.
enum : int {
    kCtFilter = 100,
    kCtSearch = 101,      // the search box of the trigger list
    kCtTurnOff = 102, kCtDeleteTrig = 103, kCtForces = 104,  // the trigger's header and players
    kCtPlayer = 120,      // + one of kCtBoxGroups
    kCtType = 160, kCtDisabled, kCtAlways,
    kCtBatch = 150,  // the block dialog (Edit the block)
    kCtParam = 200,  // + parameter
    kCtDim = 1,      // a dim label
    kCtHead = 3,     // a section heading (a line under it)
};
// A picker's value that is a block's variable: kCtVariable plus the block's depth (a field that holds it, not a value).
constexpr uint32_t kCtVariable = 0xFFFF0000u;
// The groups of the filter: players 1-8, all players, forces 1-4.
constexpr int kCtGroups[] = {0, 1, 2, 3, 4, 5, 6, 7, 17, 18, 19, 20, 21};
constexpr int kCtGroupCount = int(std::size(kCtGroups));
// The players with a check box in the row (players 1-8, all players); the forces are in a multi-pick list (Forces).
constexpr int kCtBoxGroups[] = {0, 1, 2, 3, 4, 5, 6, 7, 17};
constexpr int kCtBoxCount = int(std::size(kCtBoxGroups));
constexpr int kCtForceGroups[] = {18, 19, 20, 21};

struct ClassicTrigDialog {
    ClassicTriggers trig;
    StringTable strings;
    std::set<uint32_t> ownStrings;  // strings this window made (a text typed again changes them in place)
    std::vector<ClassicAiScript> ai;
    std::vector<int> shown;  // the triggers in the list
    int filter = -1;         // the group whose triggers show (-1: all)
    int sel = -1;            // the selected trigger
    int kind = 0, entry = -1;  // the selected condition (kind 0) or action (1)
    std::set<int> collapsed;   // the action rows of collapsed blocks (by row; cleared when the trigger changes)
    HWND wnd = nullptr, filterBox = nullptr, list = nullptr, note = nullptr, entries = nullptr;
    HWND search = nullptr, count = nullptr, filterLabel = nullptr, headTitle = nullptr, headCount = nullptr, turnOff = nullptr, deleteTrig = nullptr,
         forces = nullptr, tips = nullptr;
    HWND players[kCtBoxCount]{};
    HWND editTitle = nullptr, typeBox = nullptr, disabled = nullptr, always = nullptr, batch = nullptr;
    std::wstring query;     // the search box's words: a trigger shows when its text has them all
    std::set<int> marked;   // the triggers chosen with Ctrl or Shift click (the list's actions take them all); empty: the selected one
    int anchor = -1;        // the trigger a Shift click reaches from
    std::vector<HWND> params;  // the labels and pickers of the selected entry (made again when it changes)
    std::vector<HWND> paramPickers;  // one per parameter of the type
    int editTop = 0, editLeft = 0, editRight = 0;
    bool filling = false;
    std::vector<ClassicTrigger> before;  // the map's triggers when the page read them (a change is a difference from these)
    // The painted lists: the triggers (list) and the conditions and actions (entries).
    ui::Surface listSurface, entrySurface;
    int listScroll = 0, listHeight = 0, entryScroll = 0, entryHeight = 0;
};
ClassicTrigDialog* gCt = nullptr;  // the working copy of the page that shows (map_pages.inc)
HWND gCtValueDlg = nullptr;        // the value pop-out (ctValueProc) while it is open
HWND gCtBatchDlg = nullptr;        // the block dialog (ctBatchProc) while it is open
void mapPageChanged();

// Ctrl+C and Ctrl+V: a trigger, a condition or an action. The texts and sounds are kept as text, so a paste into
// another map gets that map's string ids.
struct ClassicTrigClip {
    int kind = -1;  // 0 condition, 1 action, 2 trigger
    ClassicCondition condition;
    std::vector<ClassicAction> rows;  // an action, or a block with its children (kind 1)
    std::vector<ClassicTrigger> triggers;  // kind 2: the checked triggers (or the selected one), in the list's order
    std::vector<std::vector<std::pair<std::string, std::string>>> triggerTexts;  // per trigger, per action: the text and the sound
    std::vector<std::pair<std::string, std::string>> texts;  // per action (kind 0 and 1): the text and the sound
};
ClassicTrigClip gCtClip;

constexpr const char* kCtListName = "ctTriggers";
constexpr const char* kCtEntryName[2] = {"ctConditions", "ctActions"};

ClassicNames ctNames() {
    ClassicNames n;
    n.unit = [](uint32_t u) { return narrow(unitDisplayName(int(u))); };
    n.location = [](uint32_t l) {
        const int i = int(l) - 1;
        return i >= 0 && i < app.ed->locationCount() ? app.ed->locationName(i) : std::string();
    };
    n.string = [](uint32_t id) { return gameTextStripped(gCt ? gCt->strings.get(id) : app.ed->strings().get(id)); };  // (without the codes)
    n.switchName = [](uint32_t s) { return app.ed->switchName(int(s)); };
    n.force = [](uint32_t f) { return app.ed->force(int(f)).name; };
    n.ai = gCt ? &gCt->ai : nullptr;
    return n;
}

ClassicTrigger* ctTrigger() { return gCt && gCt->sel >= 0 && gCt->sel < int(gCt->trig.triggers.size()) ? &gCt->trig.triggers[size_t(gCt->sel)] : nullptr; }
ClassicCondition* ctCondition() {
    ClassicTrigger* t = ctTrigger();
    return t && gCt->kind == 0 && gCt->entry >= 0 && gCt->entry < t->conditionCount() ? &t->conditions[size_t(gCt->entry)] : nullptr;
}
ClassicAction* ctAction() {
    ClassicTrigger* t = ctTrigger();
    return t && gCt->kind == 1 && gCt->entry >= 0 && gCt->entry < t->actionCount() ? &t->actions[size_t(gCt->entry)] : nullptr;
}
int ctEntryCount(const ClassicTrigger& t, int kind) { return kind ? t.actionCount() : t.conditionCount(); }
// The variable slot k (classicVarField) of the action at `index` holds a variable: its block's depth plus 1, else 0.
int ctVarOf(const ClassicTrigger& t, int index, int k) {
    return index >= 0 && index < t.actionCount() ? t.actions[size_t(index)].var[size_t(k)] : 0;
}
// The blocks that hold the row at `row`, by depth (scope[d] is the block at depth d).
std::vector<const ClassicBatch*> ctScopeOf(const ClassicTrigger& t, int row) {
    std::vector<const ClassicBatch*> chain;
    for (int i = 0; i < row && i < t.actionCount(); ++i) {
        const ClassicAction& a = t.actions[size_t(i)];
        chain.resize(std::min(chain.size(), size_t(a.depth)));
        if (a.type == kClassicBlockType) chain.push_back(a.batch.get());
    }
    if (row >= 0 && row < t.actionCount()) chain.resize(std::min(chain.size(), size_t(t.actions[size_t(row)].depth)));
    return chain;
}
// The most actions a trigger's rows have (each row is one action or a block with its children).
int ctActionLimit(const ClassicTrigger&) { return kClassicActions; }

// A trigger's line in the list: its conditions, then (dim) its actions.
std::wstring ctTriggerLine(const ClassicTrigger& t, bool actions) {
    const ClassicNames n = ctNames();
    std::string s;
    const int count = actions ? t.actionCount() : t.conditionCount();
    for (int i = 0; i < count && s.size() < 300; ++i) {
        if (actions) {
            const ClassicAction& a = t.actions[size_t(i)];
            const std::vector<const ClassicBatch*> scope = ctScopeOf(t, i);
            s += (s.empty() ? "" : " ") + (a.type == kClassicBlockType ? classicBatchText(*a.batch, n) : classicActionText(a, n, &scope));
        } else {
            s += (s.empty() ? "" : " ") + classicConditionText(t.conditions[size_t(i)], n);
        }
    }
    if (s.empty()) s = actions ? "No actions" : "No conditions";
    for (char& c : s)
        if (c == '\r' || c == '\n' || (static_cast<unsigned char>(c) < 32 && c != '\t')) c = ' ';
    return widen(s);
}

// After a change: the painted lists show it.
void ctUpdateHeader();  // (below)
void ctChanged() {
    if (!gCt) return;
    InvalidateRect(gCt->list, nullptr, FALSE);
    InvalidateRect(gCt->entries, nullptr, FALSE);
    ctUpdateHeader();  // the count of actions (a block, a trigger's rows) is the change's
    mapPageChanged();
}

// The text a search matches: the trigger's sentences (its conditions and actions, names of units, locations and
// switches) and the names of its players.
std::wstring ctSearchText(const ClassicTrigger& t) {
    const ClassicNames n = ctNames();
    std::wstring s = ctTriggerLine(t, false) + L" " + ctTriggerLine(t, true);
    for (int g = 0; g < kClassicGroups; ++g)
        if (t.players[size_t(g)]) s += L" " + widen(classicPlayerName(uint32_t(g), n));
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}
// The list's rows: the triggers that run for the player shown and have every word of the search.
void ctFillList() {
    ClassicTrigDialog& d = *gCt;
    std::wstring q = d.query;
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);
    std::vector<std::wstring> words;
    {
        std::wistringstream in(q);
        for (std::wstring w; in >> w;) words.push_back(w);
    }
    d.shown.clear();
    d.marked.clear();  // (the rows changed: the checked ones are not known any more)
    for (int i = 0; i < int(d.trig.triggers.size()); ++i) {
        const ClassicTrigger& t = d.trig.triggers[size_t(i)];
        if (d.filter >= 0 && !t.runsFor(d.filter)) continue;
        if (!words.empty()) {
            const std::wstring hay = ctSearchText(t);
            if (!std::all_of(words.begin(), words.end(), [&](const std::wstring& w) { return hay.find(w) != std::wstring::npos; })) continue;
        }
        d.shown.push_back(i);
    }
    // "3 of 87": the triggers shown of all the map's triggers.
    if (d.count) SetWindowTextW(d.count, (std::to_wstring(d.shown.size()) + L" of " + std::to_wstring(d.trig.triggers.size())).c_str());
    InvalidateRect(d.list, nullptr, FALSE);
}

void ctBuildEditor();

int ctTrigRowH() { return scale(46); }  // two lines: the first condition, and the players and actions
int ctEntryRowH() { return scale(ui::kRowH); }

// The selected trigger's row in view.
void ctShowSelected() {
    ClassicTrigDialog& d = *gCt;
    const auto at = std::find(d.shown.begin(), d.shown.end(), d.sel);
    if (at == d.shown.end() || !d.list) return;
    RECT rc;
    GetClientRect(d.list, &rc);
    if (rc.bottom <= 0) return;  // (not laid out yet: the row is shown when it is)
    const int top = scale(38) + int(at - d.shown.begin()) * ctTrigRowH(), h = ctTrigRowH();  // (the add row is above the rows)
    if (top < d.listScroll) d.listScroll = top;
    else if (top + h > d.listScroll + rc.bottom) d.listScroll = top + h - rc.bottom;
    InvalidateRect(d.list, nullptr, FALSE);
}

void ctLayout(HWND hwnd);  // (below)
// The editor's height: a title row and the rows of values (two values in a row; a text takes a row).
int ctEditorHeight(int step, int gap) {
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a && a->type != kClassicBlockType ? classicActionDef(a->type) : nullptr;
    int plain = 0, texts = 0;
    if (def)
        for (const ClassicParam& p : def->params) (p.kind == ClassicKind::Text ? texts : plain)++;
    const int rows = (plain + 1) / 2 + texts;
    return step * (1 + std::max(1, rows)) + gap;
}
void ctSelectEntry(int kind, int entry) {
    ClassicTrigDialog& d = *gCt;
    d.kind = kind, d.entry = entry;
    if (d.wnd) ctLayout(d.wnd);  // the editor's height follows the entry's values
    ctBuildEditor();
    InvalidateRect(d.entries, nullptr, FALSE);
}

// The header of the right side: the selected trigger's name, its action count and its buttons.
void ctUpdateHeader() {
    ClassicTrigDialog& d = *gCt;
    const ClassicTrigger* t = ctTrigger();
    SetWindowTextW(d.headTitle, t ? (L"Trigger " + std::to_wstring(d.sel + 1)).c_str() : L"None");
    SetWindowTextW(d.headCount, !t ? L"" : widen(std::string(t->disabled() ? "Turned off, " : "") + std::to_string(t->actionCount()) + (t->actionCount() == 1 ? " action" : " actions")).c_str());
    SetWindowTextW(d.turnOff, t && t->disabled() ? L"Turn on" : L"Turn off");
    for (HWND h : {d.turnOff, d.deleteTrig, d.forces}) EnableWindow(h, t != nullptr);
    InvalidateRect(d.headTitle, nullptr, TRUE);
}

// Shows the selected trigger: its players and lists (the first condition selected).
void ctSelectTrigger(int index) {
    ClassicTrigDialog& d = *gCt;
    d.sel = index >= 0 && index < int(d.trig.triggers.size()) ? index : -1;
    d.collapsed.clear();
    const ClassicTrigger* t = ctTrigger();
    d.filling = true;
    for (int g = 0; g < kCtBoxCount; ++g) {
        SendMessageW(d.players[g], BM_SETCHECK, t && t->runsFor(kCtBoxGroups[g]) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(d.players[g], t != nullptr);
    }
    ctUpdateHeader();
    d.filling = false;
    d.entryScroll = 0;
    ctShowSelected();
    ctSelectEntry(0, t && t->conditionCount() ? 0 : -1);
    ctChanged();
}

// The choices of a picker: value and text. Units, locations, switches, sounds and AI scripts come from the map and
// the game; a value that is not in the list (an EUD value) is added, so it shows and stays.
std::vector<std::pair<uint32_t, std::wstring>> ctKindChoices(ClassicKind k) {
    std::vector<std::pair<uint32_t, std::wstring>> out;
    const ClassicNames n = ctNames();
    switch (k) {
    case ClassicKind::Unit:
        for (int u = 0; u < 228; ++u) out.emplace_back(u, unitDisplayName(u));
        for (uint32_t g : {229u, 230u, 231u, 232u, 228u}) out.emplace_back(g, widen(classicValueText(k, g, n)));
        out[228].second[0] = towupper(out[228].second[0]);
        for (size_t i = 229; i < out.size(); ++i) out[i].second[0] = towupper(out[i].second[0]);
        break;
    case ClassicKind::Location:
        for (int i = 0; i < app.ed->locationCount(); ++i)
            if (app.ed->locationUsed(i) || i == kAnywhereLocation) out.emplace_back(uint32_t(i + 1), widen(app.ed->locationName(i)));
        break;
    case ClassicKind::Switch:
        for (uint32_t s = 0; s < 256; ++s) {
            const std::string name = app.ed->switchName(int(s));
            out.emplace_back(s, name.empty() ? L"Switch " + std::to_wstring(s + 1) : widen(name) + L" (" + std::to_wstring(s + 1) + L")");
        }
        break;
    case ClassicKind::Sound:
        out.emplace_back(0, L"No sound");
        for (uint32_t id : app.ed->soundStrings()) out.emplace_back(id, widen(gCt->strings.get(id)));
        break;
    case ClassicKind::AiScript:
        for (const ClassicAiScript& s : gCt->ai)
            out.emplace_back(s.id, widen(s.name) + L" (" + widen(classicFourCC(s.id)) + L")");
        break;
    case ClassicKind::Player:
        for (const ClassicChoice& c : classicChoices(k)) out.emplace_back(c.value, widen(classicPlayerName(c.value, n)));
        break;
    default:
        for (const ClassicChoice& c : classicChoices(k)) {
            std::wstring s = widen(c.name);
            if (!s.empty()) s[0] = towupper(s[0]);
            out.emplace_back(c.value, s);
        }
    }
    return out;
}

// The picker's choices, and the variables of the blocks that hold the selected action (a variable of this kind).
// The variable slot (classicVarField) of a field, or -1.
int ctVarSlotOfField(ClassicField f) {
    for (int k = 0; k < kClassicVarFields; ++k)
        if (classicVarField(k) == f) return k;
    return -1;
}
// The depth of the block whose variable the field of the action at `index` holds, or -1 (a value).
int ctVarDepthAt(const ClassicTrigger& t, int index, ClassicField f) {
    const int slot = ctVarSlotOfField(f);
    const int v = slot < 0 ? 0 : ctVarOf(t, index, slot);
    return v ? v - 1 : -1;
}
// The same for the selected entry (a condition has none).
int ctVarDepthOf(const ClassicCondition* c, ClassicField f) {
    const ClassicTrigger* t = ctTrigger();
    if (c || !t) return -1;
    return ctVarDepthAt(*t, gCt->entry, f);
}
void ctPickValue(int pi, POINT at);
void ctBatchClick(ClassicBatchKind kind, int editRow);  // the block dialog (below)
std::vector<std::pair<uint32_t, std::wstring>> ctChoices(ClassicKind k, uint32_t current, ClassicField f) {
    std::vector<std::pair<uint32_t, std::wstring>> out;
    const int slot = ctVarSlotOfField(f);
    if (const ClassicTrigger* t = ctTrigger(); t && slot >= 0 && gCt->kind == 1 && gCt->entry >= 0 && gCt->entry < t->actionCount()) {
        const ClassicAction& a = t->actions[size_t(gCt->entry)];
        const std::vector<const ClassicBatch*> scope = ctScopeOf(*t, gCt->entry);
        for (size_t d = 0; d < scope.size(); ++d)
            if (scope[d] && classicVarAllowed(a, slot, scope[d]->kind) && classicBatchValueKind(scope[d]->kind) == k)
                out.emplace_back(kCtVariable + uint32_t(d), widen(classicBatchVariableText(*scope[d])));
    }
    for (auto& p : ctKindChoices(k)) out.push_back(std::move(p));
    if (std::none_of(out.begin(), out.end(), [&](const auto& p) { return p.first == current; }))
        out.emplace_back(current, widen(classicValueText(k, current, ctNames())) + L" (EUD value)");
    return out;
}

bool ctPickerIsList(ClassicKind k) {
    switch (k) {
    case ClassicKind::Number:
    case ClassicKind::Milliseconds:
    case ClassicKind::Seconds:
    case ClassicKind::Count:
    case ClassicKind::Percent:
    case ClassicKind::UnitProperties:
    case ClassicKind::Text: return false;
    default: return true;
    }
}

// Is the value one of the picker's choices (as ctChoices has them)? No: an EUD value.
bool ctIsChoice(ClassicKind k, uint32_t v) {
    switch (k) {
    case ClassicKind::Unit: return v <= 232;
    case ClassicKind::Location: {
        const int i = int(v) - 1;
        return v >= 1 && i < app.ed->locationCount() && (app.ed->locationUsed(i) || i == kAnywhereLocation);
    }
    case ClassicKind::Switch: return v < 256;
    case ClassicKind::Sound: {
        if (v == 0) return true;
        const auto s = app.ed->soundStrings();
        return std::find(s.begin(), s.end(), v) != s.end();
    }
    case ClassicKind::AiScript: return std::any_of(gCt->ai.begin(), gCt->ai.end(), [&](const ClassicAiScript& s) { return s.id == v; });
    default: {
        const auto& cs = classicChoices(k);
        return std::any_of(cs.begin(), cs.end(), [&](const ClassicChoice& c) { return c.value == v; });
    }
    }
}

const wchar_t* ctKindLabel(ClassicKind k, const char* name) {
    const std::string n = name;
    if (n == "to") return L"To player";
    if (n == "dest") return L"Destination";
    if (n == "duration") return L"Duration (ms)";
    switch (k) {
    case ClassicKind::Player: return L"Player";
    case ClassicKind::Comparison: return L"Comparison";
    case ClassicKind::Number: return L"Number";
    case ClassicKind::Unit: return L"Unit";
    case ClassicKind::Location: return L"Location";
    case ClassicKind::Resource: return L"Resource";
    case ClassicKind::Score: return L"Score";
    case ClassicKind::Switch: return L"Switch";
    case ClassicKind::SwitchState: return L"State";
    case ClassicKind::SwitchAction: return L"Action";
    case ClassicKind::Modifier: return L"Modifier";
    case ClassicKind::Text: return L"Text";
    case ClassicKind::Sound: return L"Sound";
    case ClassicKind::Milliseconds: return L"Time (ms)";
    case ClassicKind::Seconds: return L"Seconds";
    case ClassicKind::Count: return L"Count (0: all)";
    case ClassicKind::AiScript: return L"AI script";
    case ClassicKind::Order: return L"Order";
    case ClassicKind::Alliance: return L"Status";
    case ClassicKind::State: return L"State";
    case ClassicKind::UnitProperties: return L"Properties";
    case ClassicKind::Percent: return L"Percent";
    }
    return L"";
}

std::wstring ctChipText(const ClassicParam& p, uint32_t v, const ClassicNames& n);  // (the painted lists, below)
// A value's text on its button in the editor: the variable's name, the list's name, the number, or the first line of a
// text (dim "None" when empty).
std::wstring ctEditorValueText(const ClassicParam& p, uint32_t v, int varDepth) {
    if (varDepth >= 0) {
        const ClassicTrigger* t = ctTrigger();
        const std::vector<const ClassicBatch*> scope = t ? ctScopeOf(*t, gCt->entry) : std::vector<const ClassicBatch*>{};
        if (size_t(varDepth) < scope.size() && scope[size_t(varDepth)]) return widen(classicBatchVariableText(*scope[size_t(varDepth)]));
    }
    if (p.kind == ClassicKind::Text) {
        std::wstring s = widen(gameTextStripped(gCt->strings.get(v)));
        for (wchar_t& ch : s)
            if (ch < 32) ch = L' ';
        return s.empty() ? L"None" : s;
    }
    if (ctPickerIsList(p.kind)) return ctChipText(p, v, ctNames());
    return widen(classicValueText(p.kind, v, ctNames()));
}

// The selected entry's title: "Condition 2 of 3"; "Action 3 of 3" at the top, "Action 3 of 3 in For each unit" in a
// block (its place among the block's children).
std::wstring ctEntryTitle(bool condition) {
    const ClassicTrigger* t = ctTrigger();
    if (!t || gCt->entry < 0) return L"";
    if (condition) return L"Condition " + std::to_wstring(gCt->entry + 1) + L" of " + std::to_wstring(t->conditionCount());
    const int entry = gCt->entry, n = t->actionCount();
    if (entry >= n) return L"";
    const int depth = t->actions[size_t(entry)].depth;
    int from = 0, to = n, parent = -1;
    for (int i = entry - 1; i >= 0; --i)  // the block that holds the row: the nearest row above that is less deep
        if (t->actions[size_t(i)].depth < depth) {
            parent = i, from = i + 1, to = t->blockEnd(i);
            break;
        }
    int siblings = 0, index = 0;
    for (int i = from; i < to; ++i)
        if (t->actions[size_t(i)].depth == depth && ++siblings && i == entry) index = siblings;
    std::wstring s = L"Action " + std::to_wstring(index) + L" of " + std::to_wstring(siblings);
    if (parent >= 0 && t->actions[size_t(parent)].batch) s += L" in For each " + widen(t->actions[size_t(parent)].batch->name);
    return s;
}

// The type, the check boxes and a picker for each value of the selected condition or action.
void ctBuildEditor() {
    ClassicTrigDialog& d = *gCt;
    for (HWND h : d.params) DestroyWindow(h);
    d.params.clear(), d.paramPickers.clear();
    ClassicCondition* c = ctCondition();
    ClassicAction* a = ctAction();
    // A block's row has no type and no values of its own: its title says what it repeats, and its controls are not shown.
    const bool block = a && a->type == kClassicBlockType && a->batch;
    const bool any = (c || a) && !block;
    for (HWND h : {d.editTitle, d.typeBox, d.disabled}) ShowWindow(h, (any || block) && (h == d.editTitle || any) ? SW_SHOW : SW_HIDE);
    ShowWindow(d.always, a && !block && (a->type == 7 || a->type == 9) ? SW_SHOW : SW_HIDE);
    if (block) {
        d.filling = true;
        SetWindowTextW(d.editTitle, widen(classicBatchText(*a->batch, ctNames())).c_str());
        d.filling = false;
        return;
    }
    if (!any) return;
    d.filling = true;
    SetWindowTextW(d.editTitle, ctEntryTitle(c != nullptr).c_str());
    const uint8_t type = c ? c->type : a->type;
    const uint8_t flags = c ? c->flags : a->flags;
    SendMessageW(d.typeBox, CB_RESETCONTENT, 0, 0);
    const auto& defs = c ? classicConditionDefs() : classicActionDefs();
    int cur = -1;
    for (const ClassicDef* def : defs) {
        const int i = int(SendMessageW(d.typeBox, CB_ADDSTRING, 0, LPARAM(widen(def->name).c_str())));
        SendMessageW(d.typeBox, CB_SETITEMDATA, WPARAM(i), LPARAM(def->type));
        if (def->type == type) cur = i;
    }
    if (cur < 0) {  // a type the game does not have: shown, kept
        cur = int(SendMessageW(d.typeBox, CB_ADDSTRING, 0, LPARAM((L"Type " + std::to_wstring(type) + L" (not a game type)").c_str())));
        SendMessageW(d.typeBox, CB_SETITEMDATA, WPARAM(cur), LPARAM(type));
    }
    SendMessageW(d.typeBox, CB_SETCURSEL, WPARAM(cur), 0);
    SendMessageW(d.disabled, BM_SETCHECK, flags & 2 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(d.always, BM_SETCHECK, flags & 4 ? BST_CHECKED : BST_UNCHECKED, 0);
    // The pickers: two columns; a text takes the whole width under them.
    const ClassicDef* def = c ? classicConditionDef(type) : classicActionDef(type);
    const int rowH = scale(ui::kFieldH), step = scale(ui::kFormRowH), labelW = scale(110), colW = (d.editRight - d.editLeft) / 2;
    const int pickW = colW - labelW - scale(16);
    int col = 0, y = d.editTop;
    auto font = [](HWND h) { SendMessageW(h, WM_SETFONT, WPARAM(app.font), TRUE); };
    std::vector<int> order;  // the texts last
    if (def) {
        for (int i = 0; i < int(def->params.size()); ++i)
            if (def->params[size_t(i)].kind != ClassicKind::Text) order.push_back(i);
        for (int i = 0; i < int(def->params.size()); ++i)
            if (def->params[size_t(i)].kind == ClassicKind::Text) order.push_back(i);
        d.paramPickers.assign(def->params.size(), nullptr);
    }
    for (int i : order) {
        const ClassicParam& p = def->params[size_t(i)];
        const int varDepth = ctVarDepthOf(c, p.field);
        const uint32_t v = varDepth >= 0 ? kCtVariable + uint32_t(varDepth) : c ? classicValue(*c, p.field) : classicValue(*a, p.field);
        const bool text = p.kind == ClassicKind::Text;
        if (text && col) col = 0, y += step;
        const int x = d.editLeft + col * colW;
        HWND lab = CreateWindowExW(0, WC_STATICW, ctKindLabel(p.kind, p.name), WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_CENTERIMAGE, x, y, labelW, rowH,
                                   d.wnd, HMENU(INT_PTR(kCtDim)), app.inst, nullptr);
        font(lab);
        d.params.push_back(lab);
        HWND h = nullptr;
        // One control for each value kind: a button that shows the value. A list value (unit, player, location, ...)
        // opens the searchable list; a number and a text open a pop-out with OK and Cancel (ctValuePopout).
        {
            const int w = text ? d.editRight - d.editLeft - labelW - scale(16) : pickW;
            h = CreateWindowExW(0, WC_BUTTONW, ctEditorValueText(p, v, varDepth).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_LEFT, x + labelW,
                                y, w, rowH, d.wnd, HMENU(INT_PTR(kCtParam + i)), app.inst, nullptr);
            darkControl(h, L"DarkMode_Explorer");
        }
        font(h);
        d.params.push_back(h);
        d.paramPickers[size_t(i)] = h;
        if (++col == 2 || text) col = 0, y += step;
    }
    d.filling = false;
}

// Sets value `i` of the selected entry.
void ctSetValue(int i, uint32_t v) {
    ClassicTrigDialog& d = *gCt;
    ClassicCondition* c = ctCondition();
    ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    if (a && ctVarSlotOfField(p.field) >= 0) {  // the variable: the field holds its block's depth; a value: none
        const int slot = ctVarSlotOfField(p.field);
        if (v >= kCtVariable) {
            a->var[size_t(slot)] = uint8_t(v - kCtVariable + 1);
            setClassicValue(*a, p.field, 0);
            ctChanged();
            return;
        }
        a->var[size_t(slot)] = 0;
    }
    if (c) setClassicValue(*c, p.field, v);
    else {
        setClassicValue(*a, p.field, v);
        if (p.kind == ClassicKind::Sound) a->time = v ? soundLength(d.strings.get(v)) : 0;  // Play WAV and Transmission hold the sound's length
    }
    ctChanged();
}

// A text value of the selected entry: the map's string with this text (an existing one, else a new one); a text this
// page made is changed in place.
void ctSetText(int i, const std::string& text) {
    ClassicTrigDialog& d = *gCt;
    const ClassicAction* a = ctAction();
    const ClassicDef* def = a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    uint32_t v = 0;
    const uint32_t old = a ? classicValue(*a, p.field) : 0;
    if (text.empty()) v = 0;
    else if (old && d.ownStrings.count(old)) d.strings.set(old, text), v = old;
    else {
        for (uint32_t id = 1; id <= d.strings.size() && !v; ++id)
            if (d.strings.get(id) == text) v = id;
        if (v) d.ownStrings.erase(v);  // a string with the same text: now shared, never changed in place
        else d.ownStrings.insert(v = d.strings.add(text));
    }
    ctSetValue(i, v);
}

// A number value: the range the field keeps (a count up to 255, a unit up to 65535, never a variable's value).
uint32_t ctClampNumber(const ClassicParam& p, unsigned long long n) {
    uint32_t v = uint32_t(std::min<unsigned long long>(n, 0xFFFFFFFFull));
    if (p.kind == ClassicKind::Count) v = std::min<uint32_t>(v, 255);
    if (p.field == ClassicField::Unit) v = std::min<uint32_t>(v, 0xFFFF);
    return std::min<uint32_t>(v, kCtVariable - 1);
}

// The value pop-outs (PRINCIPLES: values are edited in a pop-out with OK and Cancel): a number, or a text in the game
// text editor (its colors and align buttons). Cancel and a click outside leave the value as it was.
struct CtValueDialog {
    std::wstring title, label, number;
    bool text = false;
    std::string textValue;
    HWND textBox = nullptr;
};
constexpr int kCvEdit = 1101, kCvText = 1102, kCvLabel = 1103;
struct CtItem {
    DWORD style;
    short x, y, w, h;
    WORD id, cls;
    std::wstring text;
};
// A dialog template: the title, the size in dialog units and the items (a class 0x80 button, 0x81 edit, 0x82 static).
std::vector<WORD> ctDialogTemplate(const std::wstring& title, short w, short h, const std::vector<CtItem>& items) {
    std::vector<WORD> t;
    auto align = [&] { if (t.size() % 2) t.push_back(0); };
    auto str = [&](const std::wstring& s) { for (wchar_t c : s) t.push_back(c); t.push_back(0); };
    auto dword = [&](DWORD d) { t.push_back(LOWORD(d)); t.push_back(HIWORD(d)); };
    dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    dword(0);
    t.push_back(WORD(items.size()));
    t.push_back(0), t.push_back(0), t.push_back(w), t.push_back(h);
    t.push_back(0), t.push_back(0);
    str(title);
    t.push_back(9);
    str(L"Segoe UI");
    for (const CtItem& it : items) {
        align();
        dword(it.style | WS_CHILD | WS_VISIBLE);
        dword(0);
        t.push_back(it.x), t.push_back(it.y), t.push_back(it.w), t.push_back(it.h), t.push_back(it.id);
        t.push_back(0xFFFF), t.push_back(it.cls);
        str(it.text);
        t.push_back(0);
    }
    align();
    return t;
}

INT_PTR CALLBACK ctValueProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto state = [dlg] { return reinterpret_cast<CtValueDialog*>(GetWindowLongPtrW(dlg, DWLP_USER)); };
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        gCtValueDlg = dlg;
        CtValueDialog* st = state();
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, WPARAM(app.font), TRUE);
        SetDlgItemTextW(dlg, IDOK, L"Set");
        SetDlgItemTextW(dlg, IDCANCEL, L"Cancel");
        SetDlgItemTextW(dlg, kCvLabel, st->label.c_str());
        if (st->text) {  // the game text editor takes the place of the placeholder
            HWND holder = GetDlgItem(dlg, kCvText);
            RECT r;
            GetWindowRect(holder, &r);
            MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&r), 2);
            DestroyWindow(holder);
            GameTextSpec spec;
            spec.palette = TextPalette::Game, spec.multiline = true, spec.align = true, spec.crlf = true;
            const int th = gameTextHeight(spec, r.right - r.left, scale(96));
            st->textBox = gameTextCreate(dlg, kCvText, {r.left, r.top, r.right, r.top + th}, spec);
            gameTextSet(st->textBox, st->textValue);
            SetFocus(gameTextFocusBox(st->textBox));
        } else {
            SetDlgItemTextW(dlg, kCvEdit, st->number.c_str());
            SendDlgItemMessageW(dlg, kCvEdit, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dlg, kCvEdit));
        }
        darkControl(GetDlgItem(dlg, kCvEdit), L"DarkMode_CFD");
        for (int id : {IDOK, IDCANCEL}) darkControl(GetDlgItem(dlg, id), L"DarkMode_Explorer");
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(dlg, 20, &dark, sizeof dark), titleColors(dlg);
        return FALSE;  // (the focus is set above)
    }
    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor(HDC(wp), kText), SetBkColor(HDC(wp), kCtl);
        return INT_PTR(app.ctlBrush);
    }
    if (msg == WM_CTLCOLORSTATIC) {
        SetTextColor(HDC(wp), kDim), SetBkColor(HDC(wp), kBg);
        return INT_PTR(app.bgBrush);
    }
    if (msg == WM_CTLCOLORDLG) return INT_PTR(app.bgBrush);
    if (msg != WM_COMMAND) return FALSE;
    CtValueDialog* st = state();
    const int id = LOWORD(wp);
    if (id == IDOK) {
        if (st->text) st->textValue = gameTextGet(st->textBox);
        else st->number = windowText(GetDlgItem(dlg, kCvEdit));
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (id == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Runs a value pop-out: true when OK was clicked (st holds the value).
bool ctValueRun(CtValueDialog& st) {
    std::vector<CtItem> items;
    short w = 240, h = 92;
    if (st.text) {
        w = 330, h = 176;
        items.push_back({SS_LEFT, 8, 8, 310, 10, kCvLabel, 0x0082, L""});
        items.push_back({SS_LEFT, 8, 20, 310, 100, kCvText, 0x0082, L""});
    } else {
        items.push_back({SS_LEFT, 8, 10, 110, 10, kCvLabel, 0x0082, L""});
        items.push_back({WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, 120, 8, 100, 14, kCvEdit, 0x0081, L""});
    }
    items.push_back({WS_TABSTOP | BS_DEFPUSHBUTTON, short(w - 116), short(h - 22), 50, 14, IDOK, 0x0080, L""});
    items.push_back({WS_TABSTOP | BS_PUSHBUTTON, short(w - 60), short(h - 22), 50, 14, IDCANCEL, 0x0080, L""});
    const std::vector<WORD> t = ctDialogTemplate(st.title, w, h, items);
    const INT_PTR r = DialogBoxIndirectParamW(app.inst, reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), app.main, ctValueProc, LPARAM(&st));
    gCtValueDlg = nullptr;
    return r == IDOK;
}

// The pop-out of a number or a text value of the selected entry (parameter i). OK sets the value.
void ctValuePopout(int i) {
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    const uint32_t v = c ? classicValue(*c, p.field) : classicValue(*a, p.field);
    CtValueDialog st;
    st.title = ctKindLabel(p.kind, p.name);
    st.label = ctKindLabel(p.kind, p.name);
    st.text = p.kind == ClassicKind::Text;
    if (st.text) st.textValue = gCt->strings.get(v);
    else st.number = std::to_wstring(v);
    if (!ctValueRun(st)) return;
    if (st.text) ctSetText(i, st.textValue);
    else ctSetValue(i, ctClampNumber(p, wcstoull(st.number.c_str(), nullptr, 10)));
    ctBuildEditor();
}

// --- Triggers: add, duplicate, delete, move ------------------------------------------------------------------------

void ctInsertTrigger(ClassicTrigger n) {
    ClassicTrigDialog& d = *gCt;
    const int at = ctTrigger() ? d.sel + 1 : int(d.trig.triggers.size());
    d.trig.triggers.insert(d.trig.triggers.begin() + at, std::move(n));
    d.sel = at;
    ctFillList();
    ctSelectTrigger(at);
}
// A new trigger (Always, no actions) after the selected one, for the player shown.
void ctAddTrigger() {
    ClassicTrigger n = ClassicTrigger::make();
    if (gCt->filter >= 0) n.players[size_t(gCt->filter)] = 1;
    ctInsertTrigger(n);
}
// A copy of a trigger after it. A copy has not run: the flags that the game sets while it runs are cleared (the
// copy keeps "preserve trigger" and "disabled").
ClassicTrigger ctFreshCopy(ClassicTrigger t) {
    t.flags &= 4u | 8u, t.currentAction = 0;
    for (const ClassicAction& a : t.actions) gCt->ownStrings.erase(a.text);  // its texts are shared now
    return t;
}
void ctDuplicateTrigger(int i) {
    if (i < 0 || i >= int(gCt->trig.triggers.size())) return;
    gCt->sel = i;
    ctInsertTrigger(ctFreshCopy(gCt->trig.triggers[size_t(i)]));
}
void ctDeleteTrigger(int i) {
    ClassicTrigDialog& d = *gCt;
    if (i < 0 || i >= int(d.trig.triggers.size())) return;
    d.trig.triggers.erase(d.trig.triggers.begin() + i);
    d.sel = -1;
    ctFillList();
    int next = -1;  // the trigger that was after it in the list, else the one before
    for (int k : d.shown)
        if (next < 0 || k < i) next = k;
    for (int k : d.shown)
        if (k >= i) {
            next = k;
            break;
        }
    ctSelectTrigger(next);
}
// Moves the trigger at place `from` of the list to place `to` of the list without it. With a player's triggers
// shown, it goes just before (after) the trigger it now has after (before) it in the list; the triggers that do not
// show keep their places.
void ctMoveShown(int from, int to) {
    ClassicTrigDialog& d = *gCt;
    if (from < 0 || from >= int(d.shown.size())) return;
    const int src = d.shown[size_t(from)];
    std::vector<int> rest = d.shown;
    rest.erase(rest.begin() + from);
    if (rest.empty() || to < 0) return;
    to = std::min(to, int(rest.size()));
    const int dest = to < int(rest.size()) ? rest[size_t(to)] - (rest[size_t(to)] > src) : rest.back() - (rest.back() > src) + 1;
    auto& v = d.trig.triggers;
    ClassicTrigger t = v[size_t(src)];
    v.erase(v.begin() + src);
    v.insert(v.begin() + dest, std::move(t));
    d.sel = dest;
    ctFillList();
    ctSelectTrigger(dest);
}

// The triggers an action takes: the checked ones (Ctrl or Shift click), else the selected one. In the map's order.
std::vector<int> ctTargets() {
    std::vector<int> out(gCt->marked.begin(), gCt->marked.end());
    if (out.empty() && gCt->sel >= 0) out.push_back(gCt->sel);
    return out;
}
// A click on a trigger's row: a plain click selects it; Ctrl checks or unchecks it; Shift checks the rows of the list
// from the anchor to it.
void ctClickTrigger(int i) {
    ClassicTrigDialog& d = *gCt;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    const auto at = std::find(d.shown.begin(), d.shown.end(), i);
    if (shift && d.anchor >= 0 && at != d.shown.end()) {
        const auto from = std::find(d.shown.begin(), d.shown.end(), d.anchor);
        if (from != d.shown.end()) {
            d.marked.clear();
            for (auto k = std::min(from, at); k <= std::max(from, at); ++k) d.marked.insert(*k);
        }
        ctSelectTrigger(i);
    } else if (ctrl) {
        if (!d.marked.erase(i)) d.marked.insert(i);
        d.anchor = i;
        ctSelectTrigger(i);
    } else {
        d.marked.clear(), d.anchor = i;
        if (i != d.sel) ctSelectTrigger(i);
    }
    InvalidateRect(d.list, nullptr, FALSE);
}
// Turn off (or on) the triggers that ctTargets gives: all off when the first one is on, else all on.
void ctToggleTriggers() {
    const std::vector<int> targets = ctTargets();
    if (targets.empty()) return;
    // The button shows the selected trigger's state: that decides, when it is one of the targets.
    const int shown = std::find(targets.begin(), targets.end(), gCt->sel) != targets.end() ? gCt->sel : targets[0];
    const bool off = !gCt->trig.triggers[size_t(shown)].disabled();
    for (int i : targets) gCt->trig.triggers[size_t(i)].setDisabled(off);
    ctUpdateHeader();
    ctChanged();
    setMessage(off ? (targets.size() > 1 ? L"Turned off the triggers" : L"Turned off the trigger") : (targets.size() > 1 ? L"Turned on the triggers" : L"Turned on the trigger"));  // (after the change: its own message)
}
// Delete the triggers that ctTargets gives; the trigger after the first one deleted is selected.
void ctDeleteTriggers() {
    ClassicTrigDialog& d = *gCt;
    std::vector<int> targets = ctTargets();
    if (targets.empty()) return;
    const int first = *std::min_element(targets.begin(), targets.end());
    std::sort(targets.begin(), targets.end(), std::greater<int>());
    for (int i : targets) d.trig.triggers.erase(d.trig.triggers.begin() + i);
    d.marked.clear(), d.sel = -1;
    ctFillList();
    int next = -1;  // the first trigger shown from the deleted place on, else the last one shown
    for (int k : d.shown) {
        next = k;
        if (k >= first) break;
    }
    ctSelectTrigger(next);
    setMessage(targets.size() > 1 ? L"Deleted the triggers" : L"Deleted the trigger");
}
// Duplicate the triggers that ctTargets gives: each copy goes after the last one (in the list's order).
void ctDuplicateTriggers() {
    const std::vector<int> targets = ctTargets();
    if (targets.empty()) return;
    ClassicTrigDialog& d = *gCt;
    const int after = *std::max_element(targets.begin(), targets.end());
    int at = after + 1;
    for (int i : targets) {
        const ClassicTrigger copy = ctFreshCopy(d.trig.triggers[size_t(i)]);
        d.trig.triggers.insert(d.trig.triggers.begin() + at++, copy);
    }
    d.marked.clear();
    d.sel = at - 1;
    ctFillList();
    ctSelectTrigger(at - 1);
    setMessage(targets.size() > 1 ? L"Duplicated the triggers" : L"Duplicated the trigger");
}
// Forces: the players of the forces (multi-pick: each force checked or not for the selected trigger).
void ctForcesClick() {
    ClassicTrigger* t = ctTrigger();
    if (!t) return;
    const ClassicNames n = ctNames();
    std::vector<ListPickItem> items;
    std::set<std::string> checked;
    for (int g : kCtForceGroups) {
        ListPickItem it;
        it.label = widen(classicPlayerName(uint32_t(g), n)), it.value = std::to_string(g);
        items.push_back(std::move(it));
        if (t->runsFor(g)) checked.insert(std::to_string(g));
    }
    RECT rc;
    GetWindowRect(gCt->forces, &rc);
    if (!showListPickerMulti(L"Search forces", items, checked, L"Set", POINT{rc.left, rc.bottom + scale(2)})) return;
    for (int g : kCtForceGroups) t->players[size_t(g)] = checked.count(std::to_string(g)) ? 1 : 0;
    ctFillList();
    ctChanged();
}

// --- Conditions and actions: add, duplicate, delete, turn off, move ------------------------------------------------

bool ctInsertCondition(const ClassicCondition& c, int at) {
    ClassicTrigger* t = ctTrigger();
    if (!t || !t->addCondition(c, at)) return false;
    ctSelectEntry(0, at < 0 ? t->conditionCount() - 1 : at);
    ctChanged();
    return true;
}
bool ctInsertAction(const ClassicAction& a, int at) {
    ClassicTrigger* t = ctTrigger();
    if (!t || !t->addAction(a, at)) return false;
    gCt->collapsed.clear();  // the rows moved: the collapsed rows are not known any more
    ctSelectEntry(1, at < 0 ? t->actionCount() - 1 : at);
    ctChanged();
    return true;
}
// A new condition (action) of this type at the end of its list.
// Rows (an action, or a block with its children) before place `at` (the end when -1), at `depth`; the first one is
// selected.
bool ctInsertRows(const std::vector<ClassicAction>& rows, int at, int depth) {
    ClassicTrigger* t = ctTrigger();
    if (!t) return false;
    const int place = at < 0 ? t->actionCount() : at;
    if (!t->pasteRows(rows, place, depth)) return false;
    gCt->collapsed.clear();
    ctSelectEntry(1, place);
    ctChanged();
    return true;
}
// A new action goes into the selected block (as its last child) when a block is selected.
bool ctAddEntry(int kind, uint8_t type) {
    if (!kind) return ctInsertCondition(makeClassicCondition(type), -1);
    ClassicTrigger* t = ctTrigger();
    if (t && gCt->kind == 1 && gCt->entry >= 0 && gCt->entry < t->actionCount() && t->actions[size_t(gCt->entry)].type == kClassicBlockType) {
        ClassicAction a = makeClassicAction(type);
        a.depth = uint8_t(t->actions[size_t(gCt->entry)].depth + 1);
        gCt->collapsed.erase(gCt->entry);
        return ctInsertAction(a, t->blockEnd(gCt->entry));
    }
    return ctInsertAction(makeClassicAction(type), -1);
}
void ctDuplicateEntry(int kind, int i) {
    ClassicTrigger* t = ctTrigger();
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    if (kind) {  // a block is copied with its children, after itself
        std::vector<ClassicAction> rows = t->subtreeRows(i);
        for (const ClassicAction& a : rows) gCt->ownStrings.erase(a.text);  // shared now
        ctInsertRows(rows, t->blockEnd(i), t->actions[size_t(i)].depth);
    } else {
        ctInsertCondition(t->conditions[size_t(i)], i + 1);
    }
}
void ctDeleteEntry(int kind, int i) {
    ClassicTrigger* t = ctTrigger();
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    if (kind) t->removeAction(i);
    else t->removeCondition(i);
    gCt->collapsed.clear();
    const int n = ctEntryCount(*t, kind);
    ctSelectEntry(kind, n ? std::min(i, n - 1) : -1);
    ctChanged();
}
void ctToggleEntry(int kind, int i) {
    ClassicTrigger* t = ctTrigger();
    if (!t || i < 0 || i >= ctEntryCount(*t, kind)) return;
    if (kind && t->actions[size_t(i)].type == kClassicBlockType) return;  // a block is not turned off: its actions are
    uint8_t& f = kind ? t->actions[size_t(i)].flags : t->conditions[size_t(i)].flags;
    f ^= 2;
    ctSelectEntry(kind, i);
    ctChanged();
}
// Moves entry `from` to place `to` (in the list without it).
void ctMoveEntry(int kind, int from, int to) {
    ClassicTrigger* t = ctTrigger();
    if (!t) return;
    const int n = ctEntryCount(*t, kind);
    if (from < 0 || from >= n || to < 0 || to >= n || to == from) return;
    if (kind) t->moveAction(from, to);
    else t->moveCondition(from, to);
    gCt->collapsed.clear();
    ctSelectEntry(kind, to);
    ctChanged();
}

// --- Copy and paste ------------------------------------------------------------------------------------------------

std::pair<std::string, std::string> ctActionTexts(const ClassicAction& a) { return {gCt->strings.get(a.text), gCt->strings.get(a.wav)}; }
// The action's text and sound as this map's strings (an id that has no string stays as it is).
void ctResolveTexts(ClassicAction& a, const std::pair<std::string, std::string>& texts) {
    if (!texts.first.empty()) a.text = classicStringId(gCt->strings, texts.first), gCt->ownStrings.erase(a.text);
    if (!texts.second.empty()) a.wav = classicStringId(gCt->strings, texts.second), gCt->ownStrings.erase(a.wav);
}
void ctCopy(bool trigger) {
    ClassicTrigDialog& d = *gCt;
    const ClassicTrigger* t = ctTrigger();
    if (!t) return;
    if (trigger) {  // the checked triggers (or the selected one), in the list's order
        gCtClip = {};
        gCtClip.kind = 2;
        for (int i : ctTargets()) {
            const ClassicTrigger& c = d.trig.triggers[size_t(i)];
            gCtClip.triggers.push_back(c);
            gCtClip.triggerTexts.emplace_back();
            for (const ClassicAction& a : c.actions) gCtClip.triggerTexts.back().push_back(ctActionTexts(a));
        }
        return setMessage(gCtClip.triggers.size() > 1 ? L"Copied the triggers" : L"Copied the trigger");
    }
    if (d.entry < 0 || d.entry >= ctEntryCount(*t, d.kind)) return;
    gCtClip = {};
    gCtClip.kind = d.kind;
    if (d.kind) {  // a block takes its children with it
        gCtClip.rows = t->subtreeRows(d.entry);
        for (const ClassicAction& a : gCtClip.rows) gCtClip.texts.push_back(ctActionTexts(a));
    } else {
        gCtClip.condition = t->conditions[size_t(d.entry)];
    }
    setMessage(!d.kind ? L"Copied the condition" : gCtClip.rows.size() > 1 ? L"Copied the block" : L"Copied the action");
}
void ctPaste() {
    ClassicTrigDialog& d = *gCt;
    if (gCtClip.kind == 2) {  // each after the one before it: the selected trigger, then the pasted ones
        for (size_t k = 0; k < gCtClip.triggers.size(); ++k) {
            ClassicTrigger n = gCtClip.triggers[k];
            const auto& texts = gCtClip.triggerTexts[k];
            for (size_t i = 0; i < n.actions.size() && i < texts.size(); ++i) ctResolveTexts(n.actions[i], texts[i]);
            ctInsertTrigger(ctFreshCopy(n));
        }
        d.marked.clear();
        return setMessage(gCtClip.triggers.size() > 1 ? L"Pasted the triggers" : L"Pasted the trigger");
    }
    const ClassicTrigger* t = ctTrigger();
    if (!t || gCtClip.kind < 0) return;
    bool ok;
    if (gCtClip.kind) {
        // After the selected action (after its block, at its depth); at the end of the trigger when none is selected.
        int at = -1, depth = 0;
        if (d.kind == 1 && d.entry >= 0 && d.entry < t->actionCount()) at = t->blockEnd(d.entry), depth = t->actions[size_t(d.entry)].depth;
        std::vector<ClassicAction> rows = gCtClip.rows;
        for (size_t i = 0; i < rows.size() && i < gCtClip.texts.size(); ++i) ctResolveTexts(rows[i], gCtClip.texts[i]);
        ok = ctInsertRows(rows, at, depth);
        return setMessage(!ok ? L"The list is full" : rows.size() > 1 ? L"Pasted the block" : L"Pasted the action");
    }
    const int at = d.kind == gCtClip.kind && d.entry >= 0 ? d.entry + 1 : -1;
    ok = ctInsertCondition(gCtClip.condition, at);
    setMessage(!ok ? L"The list is full" : L"Pasted the condition");
}

// --- The painted lists ---------------------------------------------------------------------------------------------

// A value's text in its chip: its name; a value that is not one of the choices has "(EUD value)".
std::wstring ctChipText(const ClassicParam& p, uint32_t v, const ClassicNames& n) {
    std::wstring s = widen(classicValueText(p.kind, v, n));
    for (wchar_t& ch : s)
        if (ch < 32) ch = L' ';
    if (s.size() > 48) s = s.substr(0, 47) + L"\x2026";
    if (ctPickerIsList(p.kind) && !ctIsChoice(p.kind, v)) s += L" (EUD value)";
    return s;
}

// A value chip was clicked: a list of choices, or the value's field under the lists.
void ctChipClick(int kind, int i, int pi, RECT chip) {
    ClassicTrigDialog& d = *gCt;
    if (d.kind != kind || d.entry != i) ctSelectEntry(kind, i);
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || pi < 0 || pi >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(pi)];
    if (!ctPickerIsList(p.kind)) return ctValuePopout(pi);  // a number or a text: its pop-out
    POINT pt{chip.left, chip.bottom + scale(2)};
    ClientToScreen(d.entries, &pt);
    ctPickValue(pi, pt);
}

// The searchable list of a parameter's values (a unit, a location, a switch, ...), with the variables that fit it
// first. It opens below `at` (screen coordinates); one click selects.
void ctPickValue(int pi, POINT at) {
    ClassicCondition* c = ctCondition();
    ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || pi < 0 || pi >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(pi)];
    const int vd = ctVarDepthOf(c, p.field);
    const uint32_t v = vd >= 0 ? kCtVariable + uint32_t(vd) : c ? classicValue(*c, p.field) : classicValue(*a, p.field);
    std::vector<ListPickItem> items;
    for (const auto& [value, label] : ctChoices(p.kind, v, p.field)) {
        ListPickItem it;
        it.label = label, it.value = std::to_string(value);
        items.push_back(std::move(it));
    }
    std::string picked;
    if (!showListPicker(ctKindLabel(p.kind, p.name), items, std::to_string(v), at, picked) || picked.empty()) return;
    const uint32_t nv = uint32_t(std::stoul(picked));
    if (nv == v) return;
    ctSetValue(pi, nv);
    ctBuildEditor();
}

// "+ condition", "+ action": the game's conditions (actions) to select from.
// The blocks come first in the actions' list (a section "Blocks"), then the actions (section "Actions").
void ctAddClick(int kind, RECT link) {
    std::vector<ListPickItem> items;
    if (kind)
        for (const ClassicBlockKind& k : classicBlockKinds()) {
            ListPickItem it;
            it.label = L"For each " + widen(k.name), it.section = L"Blocks", it.value = "block:" + std::to_string(int(k.kind));
            items.push_back(std::move(it));
        }
    for (const ClassicDef* def : kind ? classicActionDefs() : classicConditionDefs()) {
        ListPickItem it;
        it.label = widen(def->name), it.value = std::to_string(def->type);
        if (kind) it.section = L"Actions";
        items.push_back(std::move(it));
    }
    POINT pt{link.left, link.bottom + scale(2)};
    ClientToScreen(gCt->entries, &pt);
    std::string picked;
    if (!showListPicker(kind ? L"Search actions" : L"Search conditions", items, "", pt, picked) || picked.empty()) return;
    if (picked.rfind("block:", 0) == 0) return ctBatchClick(ClassicBatchKind(std::stoi(picked.substr(6))), -1);
    ctAddEntry(kind, uint8_t(std::stoi(picked)));
}

// The sentence of a condition or action, its values as chips (each a click area).
template <class E>
void ctSentence(HDC dc, const E& e, int kind, int index, RECT area, bool dim, bool selected) {
    const ClassicDef* def = kind ? classicActionDef(e.type) : classicConditionDef(e.type);
    const ClassicNames n = ctNames();
    const COLORREF word = dim ? kFaint : ui::rowText(selected);
    int x = area.left;
    auto text = [&](const std::wstring& s, COLORREF col, bool chip) -> RECT {
        if (x >= area.right || s.empty()) return {};
        const int pad = chip ? scale(5) : 0;
        int w = ui::textWidth(dc, s) + 2 * pad;
        if (x + w > area.right) w = area.right - x;
        RECT r{x, area.top, x + w, area.bottom};
        if (chip) ui::chipFace(dc, {r.left, r.top + scale(4), r.right, r.bottom - scale(4)}, ui::hot(r), dim);
        SetTextColor(dc, col);
        RECT t{r.left + pad, r.top, r.right - pad, r.bottom};
        DrawTextW(dc, s.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        x += w;
        return r;
    };
    if constexpr (std::is_same_v<E, ClassicAction>) {
        if (e.type == kClassicBlockType) {  // a block: "For each unit in ..." (its chevron is painted by the list)
            text(widen(classicBatchText(*e.batch, n)), word, false);
            return;
        }
    }
    std::vector<const ClassicBatch*> scope;
    if constexpr (std::is_same_v<E, ClassicAction>)
        if (kind == 1 && ctTrigger()) scope = ctScopeOf(*ctTrigger(), index);
    if (!def) {
        std::string s;
        if constexpr (std::is_same_v<E, ClassicAction>) s = classicActionText(e, n, &scope);
        else s = classicConditionText(e, n);
        for (char& ch : s)
            if (static_cast<unsigned char>(ch) < 32) ch = ' ';
        text(widen(s), word, false);
        return;
    }
    const std::string s = def->sentence;
    std::wstring run;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '{') {
            run += wchar_t(static_cast<unsigned char>(s[i]));
            continue;
        }
        const size_t end = s.find('}', i);
        if (end == std::string::npos) break;
        const std::string key = s.substr(i + 1, end - i - 1);
        i = end;
        text(run, word, false);
        run.clear();
        for (int pi = 0; pi < int(def->params.size()); ++pi) {
            const ClassicParam& p = def->params[size_t(pi)];
            if (key != p.name) continue;
            const int vd = kind == 1 && ctTrigger() ? ctVarDepthAt(*ctTrigger(), index, p.field) : -1;
            std::wstring v = vd >= 0 && size_t(vd) < scope.size() && scope[size_t(vd)] ? widen(classicBatchVariableText(*scope[size_t(vd)]))
                                                                                     : ctChipText(p, classicValue(e, p.field), n);
            if (x == area.left && !v.empty()) v[0] = towupper(v[0]);  // a sentence that starts with a value
            const RECT r = text(v, dim ? kFaint : kText, true);
            if (r.right > r.left)
                hitsOut().push_back({r, nullptr, [kind, index, pi, r] { ctChipClick(kind, index, pi, r); }, ctKindLabel(p.kind, p.name), IDC_HAND, {}});
            break;
        }
    }
    text(run, word, false);
    if (classicEud(e)) text(L"  EUD", kDim, false);
}

// How many triggers the map gets for this trigger: 1, or more when its blocks make more than 64 slots.
int ctPartsOf(const ClassicTrigger& t) {
    bool blocks = false;
    for (int i = 0; i < t.actionCount() && !blocks; ++i) blocks = t.actions[size_t(i)].type == kClassicBlockType;
    if (!blocks) return 1;
    ClassicTriggers one;
    one.triggers.push_back(t);
    StringTable scratch;
    return int(classicExpandBatches(one, scratch).triggers.size());
}

/// The right-click menus of the rows (the same commands as the rows' buttons and the keys). A right click on a row that
// is not checked selects it first (and the checks go).
enum : int { kCtmToggle = 1, kCtmDuplicate, kCtmDelete, kCtmCopy, kCtmPaste, kCtmIn, kCtmOut, kCtmBlock };
bool ctCanMove(int i, bool in);  // (below)
void ctMoveRow(int i, bool in);
void ctTriggerMenu(POINT screen, int i) {
    ClassicTrigDialog& d = *gCt;
    if (!d.marked.count(i)) d.marked.clear(), ctSelectTrigger(i);
    const ClassicTrigger* t = ctTrigger();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, kCtmToggle, t && t->disabled() ? L"Turn on" : L"Turn off");
    AppendMenuW(m, MF_STRING, kCtmDuplicate, L"Duplicate\tCtrl+D");
    AppendMenuW(m, MF_STRING, kCtmDelete, L"Delete\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kCtmCopy, L"Copy\tCtrl+C");
    AppendMenuW(m, MF_STRING | (gCtClip.kind == 2 ? 0 : MF_GRAYED), kCtmPaste, L"Paste\tCtrl+V");
    const int pick = popupMenu(m, TPM_LEFTALIGN, screen.x, screen.y, d.list);
    DestroyMenu(m);
    switch (pick) {
    case kCtmToggle: return ctToggleTriggers();
    case kCtmDuplicate: return ctDuplicateTriggers();
    case kCtmDelete: return ctDeleteTriggers();
    case kCtmCopy: return ctCopy(true);
    case kCtmPaste: return ctPaste();
    }
}
// The right-click menu of an entry's row: the row's commands (a block: its edit, no turn off).
void ctEntryMenu(POINT screen, int kind, int i) {
    ClassicTrigDialog& d = *gCt;
    ctSelectEntry(kind, i);
    const ClassicTrigger* t = ctTrigger();
    if (!t) return;
    const bool block = kind && t->actions[size_t(i)].type == kClassicBlockType;
    const bool off = kind ? (t->actions[size_t(i)].flags & 2) != 0 : (t->conditions[size_t(i)].flags & 2) != 0;
    HMENU m = CreatePopupMenu();
    if (block) AppendMenuW(m, MF_STRING, kCtmBlock, L"Edit the block\x2026");
    else AppendMenuW(m, MF_STRING, kCtmToggle, off ? L"Turn on" : L"Turn off");
    if (kind) {
        AppendMenuW(m, MF_STRING | (ctCanMove(i, false) ? 0 : MF_GRAYED), kCtmOut, L"Move out\tCtrl+Left");
        AppendMenuW(m, MF_STRING | (ctCanMove(i, true) ? 0 : MF_GRAYED), kCtmIn, L"Move in\tCtrl+Right");
    }
    AppendMenuW(m, MF_STRING, kCtmDuplicate, L"Duplicate\tCtrl+D");
    AppendMenuW(m, MF_STRING, kCtmDelete, L"Delete\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kCtmCopy, L"Copy\tCtrl+C");
    AppendMenuW(m, MF_STRING | (gCtClip.kind == (kind ? 1 : 0) ? 0 : MF_GRAYED), kCtmPaste, L"Paste\tCtrl+V");
    const int pick = popupMenu(m, TPM_LEFTALIGN, screen.x, screen.y, d.entries);
    DestroyMenu(m);
    switch (pick) {
    case kCtmBlock: ctBatchClick(ClassicBatchKind::Unit, i); return;
    case kCtmToggle: return ctToggleEntry(kind, i);
    case kCtmOut: return ctMoveRow(i, false);
    case kCtmIn: return ctMoveRow(i, true);
    case kCtmDuplicate: return ctDuplicateEntry(kind, i);
    case kCtmDelete: return ctDeleteEntry(kind, i);
    case kCtmCopy: return ctCopy(false);
    case kCtmPaste: return ctPaste();
    }
}

// Turn off (or on) one trigger, as its row's button does.
void ctToggleOne(int i) {
    if (i < 0 || i >= int(gCt->trig.triggers.size())) return;
    ClassicTrigger& t = gCt->trig.triggers[size_t(i)];
    t.setDisabled(!t.disabled());
    ctUpdateHeader();
    ctChanged();
}
// Can the action move into the block before it (in), or out of its block (out)? A trial on a copy.
bool ctCanMove(int i, bool in) {
    const ClassicTrigger* t = ctTrigger();
    if (!t || i < 0 || i >= t->actionCount()) return false;
    ClassicTrigger trial = *t;
    return in ? trial.indentAction(i) : trial.outdentAction(i);
}
// Moves the action at row i one level in or out (Ctrl+Right and Ctrl+Left; the row's buttons).
void ctMoveRow(int i, bool in) {
    ClassicTrigger* t = ctTrigger();
    if (!t) return;
    const bool moved = in ? t->indentAction(i) : t->outdentAction(i);
    if (!moved) return;
    gCt->collapsed.clear();
    ctChanged();
    ctBuildEditor();
}

// Paints the trigger list (list) or the conditions and actions (entries).
// The list: "Add a trigger" first, then each trigger in two lines (its first condition; its players and action count),
// an "Off" chip on a turned-off trigger, and the row's actions on the row under the mouse (or the selected one).
// The entries: the conditions, then the actions. A block row has its chevron, its children below it with a rule at
// each depth, and its child count ("2 actions", or "3 actions hidden" when collapsed).
void ctPaint(HWND hwnd, bool isList) {
    ClassicTrigDialog& d = *gCt;
    ui::Surface& surface = isList ? d.listSurface : d.entrySurface;
    surface.begin(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP back = CreateCompatibleBitmap(dc, std::max(1, int(rc.right)), std::max(1, int(rc.bottom)));
    HGDIOBJ ob = SelectObject(mem, back), of = SelectObject(mem, app.font);
    FillRect(mem, &rc, app.bgBrush);
    SetBkMode(mem, TRANSPARENT);
    const int w = int(rc.right);
    const int scroll = isList ? d.listScroll : d.entryScroll;
    int y = -scroll;
    auto visible = [&](const RECT& r) { return r.bottom > 0 && r.top < rc.bottom; };
    auto none = [&](int x) {  // an empty list
        RECT r{x, y, w, y + scale(ui::kRowH)};
        SetTextColor(mem, kDim);
        DrawTextW(mem, L"None", -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        y += scale(ui::kRowH);
    };
    const ClassicNames n = ctNames();
    if (isList) {
        const int rowH = ctTrigRowH(), textX = scale(22);
        {  // the add row is the first row of the list
            const RECT ar{scale(10), y + scale(2), w - scale(10), y + scale(32)};
            ui::addRow(mem, ar, L"Add a trigger", [] { ctAddTrigger(); }, L"Add a trigger after the selected one");
            y += scale(38);
        }
        if (d.shown.empty()) none(scale(10));
        for (int k = 0; k < int(d.shown.size()); ++k) {
            const int i = d.shown[size_t(k)];
            const RECT r{0, y, w, y + rowH};
            y += rowH;
            const bool sel = i == d.sel, checked = d.marked.count(i) != 0;
            const ClassicTrigger& t = d.trig.triggers[size_t(i)];
            if (visible(r)) {
                const std::wstring cond = ctTriggerLine(t, false), acts = ctTriggerLine(t, true);  // (built for the rows shown only)
                ui::listRow(mem, r, sel || checked, ui::hot(r));
                hitsOut().push_back({r, nullptr, [i] { ctClickTrigger(i); }, (cond.empty() ? L"" : cond + L". ") + acts, IDC_ARROW, {}});
                hitsOut().back().context = [i](POINT p) { ctTriggerMenu(p, i); };
            }
            ui::reorderRow(mem, kCtListName, r, k, [](int from, int to) { ctMoveShown(from, to); });
            if (!visible(r)) continue;
            const bool lit = sel || checked;
            const int parts = ctPartsOf(t);  // a trigger too long for 64 slots is several triggers in the map
            const int actionCount = t.actionCount();
            const int right = r.right - scale(8) - scale(ui::kIconBox) * 3;
            const RECT top{textX, r.top, right, (r.top + r.bottom) / 2 + scale(1)}, bottom{textX, top.bottom - scale(2), right, r.bottom};
            // The first condition (Always when there is none), and the Off chip at the right of it.
            std::wstring first = t.conditionCount() ? widen(classicConditionText(t.conditions[0], n)) : L"Always";
            if (parts > 1) first = L"Becomes " + std::to_wstring(parts) + L" triggers in the map. " + first;
            SetTextColor(mem, ui::rowText(lit));
            int textRight = right;
            if (t.disabled()) {
                const int cw = ui::textWidth(mem, L"Off") + scale(14);
                const RECT chip{right - cw, top.top + scale(5), right, top.bottom - scale(5)};
                ui::chipFace(mem, chip, false, true);
                SetTextColor(mem, kWarn);
                RECT offText = chip;
                DrawTextW(mem, L"Off", -1, &offText, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
                textRight = chip.left - scale(8);
            }
            RECT head{textX, top.top, textRight, top.bottom};
            DrawTextW(mem, (std::to_wstring(i + 1) + L". " + first).c_str(), -1, &head, DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
            // The players and the number of actions: "Player 1, Player 2 | 2 actions".
            std::wstring who;
            for (int g = 0; g < kClassicGroups; ++g)
                if (t.players[size_t(g)]) who += (who.empty() ? L"" : L", ") + widen(classicPlayerName(uint32_t(g), n));
            SetTextColor(mem, kDim);
            const std::wstring tail = (who.empty() ? L"No players" : who) + L" | " + std::to_wstring(actionCount) + (actionCount == 1 ? L" action" : L" actions");
            RECT tailRect = bottom;
            DrawTextW(mem, tail.c_str(), -1, &tailRect, DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
            ui::rowActions(mem, r,
                           {{UiIcon::Power, t.disabled() ? L"Turn on" : L"Turn off", [i] { ctToggleOne(i); }, ui::Tone::Quiet, t.disabled()},
                            {UiIcon::Duplicate, L"Duplicate", [i] { ctDuplicateTrigger(i); }},
                            {UiIcon::Delete, L"Delete", [i] { ctDeleteTrigger(i); }, ui::Tone::Danger}},
                           lit);
        }
        ui::reorderDropLine(mem, kCtListName);
    } else {
        const ClassicTrigger* t = ctTrigger();
        FormLayout l{};
        l.x0 = 0, l.colRight = w, l.rowH = scale(ui::kRowH);
        for (int kind = 0; kind < 2; ++kind) {
            y = sectionHeader(mem, l, y, kind ? L"Actions" : L"Conditions");
            const int count = t ? ctEntryCount(*t, kind) : 0;
            if (!count && !t) none(scale(10));  // (a trigger: its add row says it)
            const int rowH = ctEntryRowH();
            int hiddenEnd = -1;  // the children of a collapsed block are not shown: up to this row
            for (int i = 0; i < count; ++i) {
                if (kind && i < hiddenEnd) continue;
                hiddenEnd = -1;
                const bool isBlock = kind && t->actions[size_t(i)].type == kClassicBlockType;
                if (isBlock && d.collapsed.count(i)) hiddenEnd = t->blockEnd(i);
                const int depth = kind ? t->actions[size_t(i)].depth : 0;
                const int indent = depth * scale(18) + scale(6);  // the chevron's cell; the sentence starts after it
                const RECT r{0, y, w, y + rowH};
                y += rowH;
                const bool sel = d.kind == kind && d.entry == i;
                if (visible(r)) {
                    ui::listRow(mem, r, sel, ui::hot(r));
                    hitsOut().push_back({r, nullptr, [kind, i] { ctSelectEntry(kind, i); }, L"", IDC_ARROW, {}});
                    hitsOut().back().context = [kind, i](POINT p) { ctEntryMenu(p, kind, i); };
                }
                ui::reorderRow(mem, kCtEntryName[kind], r, i, [kind](int from, int to) { ctMoveEntry(kind, from, to); });
                if (!visible(r)) continue;
                // A rule at each depth the row is inside: the children of a block read as inside it.
                for (int lv = 0; lv < depth; ++lv) {
                    const int x = lv * scale(18) + scale(13);
                    fillAlpha(mem, {x, r.top, x + scale(1), r.bottom}, kBorder, 255);
                }
                const uint8_t flags = kind ? t->actions[size_t(i)].flags : t->conditions[size_t(i)].flags;
                const bool off = (flags & 2) != 0;
                if (isBlock) {  // the chevron: expanded (down) or collapsed (right); the whole indent column shows and takes a click
                    const bool shut = d.collapsed.count(i) != 0;
                    const RECT column{0, r.top, indent + scale(14), r.bottom};
                    RECT glyphBox{indent, r.top, indent + scale(14), r.bottom};
                    SetTextColor(mem, ui::rowText(sel));
                    ui::glyph(mem, glyphBox, shut ? L"\xE76C" : L"\xE70D", ui::rowText(sel), scale(12));
                    hitsOut().push_back({column, nullptr, [i] {
                                             if (gCt->collapsed.count(i)) gCt->collapsed.erase(i);
                                             else gCt->collapsed.insert(i);
                                             ctChanged();
                                         },
                                         shut ? L"Show the actions of the block" : L"Hide the actions of the block", IDC_HAND, {}});
                }
                // The row's actions: a block (edit, turn on or off, move, duplicate, delete); an action (turn on or off,
                // move out, move in, duplicate, delete); a condition (turn on or off, duplicate, delete).
                std::vector<ui::RowAction> acts;
                if (isBlock) acts.push_back({UiIcon::Edit, L"Edit the block", [i] { ctSelectEntry(1, i); ctBatchClick(ClassicBatchKind::Unit, i); }});
                acts.push_back({UiIcon::Power, off ? L"Turn on" : L"Turn off", [kind, i] { ctToggleEntry(kind, i); }, ui::Tone::Quiet, off, !isBlock});
                if (kind) {
                    const bool canOut = ctCanMove(i, false), canIn = ctCanMove(i, true);
                    acts.push_back({UiIcon::Back, L"Move out (Ctrl+Left)", [i] { ctMoveRow(i, false); }, ui::Tone::Normal, false, canOut});
                    acts.push_back({UiIcon::Forward, L"Move in (Ctrl+Right)", [i] { ctMoveRow(i, true); }, ui::Tone::Normal, false, canIn});
                }
                acts.push_back({UiIcon::Duplicate, L"Duplicate", [kind, i] { ctDuplicateEntry(kind, i); }});
                acts.push_back({UiIcon::Delete, L"Delete", [kind, i] { ctDeleteEntry(kind, i); }, ui::Tone::Danger});
                const int actsW = scale(ui::kIconBox) * int(acts.size());
                const int textLeft = indent + scale(18);
                const int areaRight = r.right - scale(8) - actsW - (isBlock ? scale(96) : 0);
                const RECT area{textLeft, r.top, areaRight, r.bottom};
                if (kind) ctSentence(mem, t->actions[size_t(i)], 1, i, area, off, sel);
                else ctSentence(mem, t->conditions[size_t(i)], 0, i, area, off, sel);
                if (isBlock) {  // the child count: "2 actions", or "3 actions hidden" when collapsed
                    const int children = t->blockEnd(i) - i - 1;
                    const std::wstring childText = std::to_wstring(children) + (children == 1 ? L" action" : L" actions") + (hiddenEnd >= 0 ? L" hidden" : L"");
                    RECT cr{areaRight + scale(4), r.top, areaRight + scale(96), r.bottom};
                    SetTextColor(mem, kDim);
                    DrawTextW(mem, childText.c_str(), -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
                }
                ui::rowActions(mem, r, acts, sel);
            }
            ui::reorderDropLine(mem, kCtEntryName[kind]);
            y += scale(4);
            if (t) {  // the add row: an action inside the selected block says which block
                const int limit = kind ? kClassicActions : kClassicConditions;
                const bool full = count >= limit;
                const RECT lr{scale(10), y, w - scale(10), y + scale(30)};
                std::wstring text = kind ? L"Add an action\x2026" : L"Add a condition\x2026";
                std::wstring tip = full ? (kind ? L"A trigger has at most 64 actions" : L"A trigger has at most 16 conditions") : L"";
                if (kind && !full && d.kind == 1 && d.entry >= 0 && d.entry < count && t->actions[size_t(d.entry)].type == kClassicBlockType) {
                    text = L"Add an action to For each " + widen(t->actions[size_t(d.entry)].batch->name) + L"\x2026";
                    tip = L"Add an action inside the block (the end of its actions)";
                }
                ui::addRow(mem, lr, text, [kind, lr] { ctAddClick(kind, lr); }, tip, !full);
                y += scale(30);
            }
            y += scale(16);
        }
    }
    int& height = isList ? d.listHeight : d.entryHeight;
    height = y + scroll;
    if (const int most = std::max(0, height - int(rc.bottom)); scroll > most)  // taller window: the bar hides, so no scroll is left
        (isList ? d.listScroll : d.entryScroll) = most, InvalidateRect(hwnd, nullptr, FALSE);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, std::max(0, height - 1), UINT(rc.bottom), scroll};
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, of), SelectObject(mem, ob);
    DeleteObject(back);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    surface.end(hwnd);
}

// The keys of a painted list: Delete, Ctrl+C, Ctrl+V, Ctrl+Up and Ctrl+Down (move), Up and Down (select).
void ctKey(bool isList, WPARAM vk) {
    ClassicTrigDialog& d = *gCt;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    if (ctrl && vk == 'C') return ctCopy(isList);
    if (ctrl && vk == 'X') {  // cut: copy, then delete (a block with its children)
        if (isList) {
            if (d.sel < 0) return;  // (no trigger selected: nothing to cut)
            ctCopy(true);
            return ctDeleteTriggers();  // (the checked ones, as the copy took them)
        }
        if (d.entry < 0) return;
        ctCopy(false);
        return ctDeleteEntry(d.kind, d.entry);
    }
    if (ctrl && vk == 'V') return ctPaste();
    if (isList) {  // the checked triggers (Ctrl or Shift click) when there are some, else the selected one
        if (vk == VK_DELETE) return ctDeleteTriggers();
        if (ctrl && vk == 'D') return ctDuplicateTriggers();
        const auto at = std::find(d.shown.begin(), d.shown.end(), d.sel);
        if (at == d.shown.end()) return;
        const int k = int(at - d.shown.begin());
        if (vk == VK_UP || vk == VK_DOWN) {
            const int to = k + (vk == VK_UP ? -1 : 1);
            if (to < 0 || to >= int(d.shown.size())) return;
            if (ctrl) ctMoveShown(k, to);
            else ctClickTrigger(d.shown[size_t(to)]);  // (Shift: the rows from the anchor to it)
        }
        return;
    }
    const ClassicTrigger* t = ctTrigger();
    if (!t || d.entry < 0) return;
    if (vk == VK_DELETE) return ctDeleteEntry(d.kind, d.entry);
    if (ctrl && d.kind == 1 && (vk == VK_LEFT || vk == VK_RIGHT)) return ctMoveRow(d.entry, vk == VK_RIGHT);  // Move in, Move out
    if (vk == VK_UP || vk == VK_DOWN) {
        const int to = d.entry + (vk == VK_UP ? -1 : 1);
        if (ctrl) return ctMoveEntry(d.kind, d.entry, to);
        if (to >= 0 && to < ctEntryCount(*t, d.kind)) ctSelectEntry(d.kind, to);
        else if (vk == VK_DOWN && d.kind == 0 && t->actionCount()) ctSelectEntry(1, 0);
        else if (vk == VK_UP && d.kind == 1 && t->conditionCount()) ctSelectEntry(0, t->conditionCount() - 1);
    }
}

LRESULT CALLBACK ctListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicTrigDialog* d = gCt;
    if (!d || (hwnd != d->list && hwnd != d->entries)) return DefWindowProcW(hwnd, msg, wp, lp);
    const bool isList = hwnd == d->list;
    ui::Surface& surface = isList ? d->listSurface : d->entrySurface;
    int& scroll = isList ? d->listScroll : d->entryScroll;
    const int height = isList ? d->listHeight : d->entryHeight;
    RECT rc;
    GetClientRect(hwnd, &rc);
    auto scrollTo = [&](int s) {
        s = std::clamp(s, 0, std::max(0, height - int(rc.bottom)));
        if (s != scroll) scroll = s, InvalidateRect(hwnd, nullptr, FALSE);
    };
    if (msg == WM_MOUSEMOVE && surface.grab.down && ui::gReorder.from >= 0) {  // a row dragged to the edge: the list scrolls
        const int y = GET_Y_LPARAM(lp), edge = scale(20);
        if (y < edge) scrollTo(scroll - scale(12));
        else if (y > rc.bottom - edge) scrollTo(scroll + scale(12));
    }
    if (LRESULT r; surface.message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_PAINT: ctPaint(hwnd, isList); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN: ctKey(isList, wp); return 0;
    case WM_MOUSEWHEEL: scrollTo(scroll - GET_WHEEL_DELTA_WPARAM(wp) * scale(26) * 3 / WHEEL_DELTA); return 0;
    case WM_VSCROLL: scrollTo(scrollFromMessage(hwnd, wp, scroll, scale(26))); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Batches (R23): the variable and its values, in a dialog with OK and Cancel ------------------------------------

constexpr int kCtBatchName = 1001, kCtBatchKind = 1002, kCtBatchValues = 1003, kCtBatchRemove = 1004, kCtBatchAddValue = 1005,
              kCtBatchRemoveValue = 1006;

struct CtBatchDialog {
    ClassicBatch* batch = nullptr;
    bool hadBatch = false;
    ClassicBatchKind shown = ClassicBatchKind::Unit;  // the kind the list shows now (the default name follows it)
};

ClassicKind ctBatchKindOf(ClassicBatchKind k) { return classicBatchValueKind(k); }
// The name a new batch's variable gets: the kind's name (classicBlockKinds).
std::string batchDefaultName(ClassicBatchKind k) { return classicBlockKind(k).name; }
const char* ctBatchKindLabel(ClassicBatchKind k) { return classicBlockKind(k).label; }

// The values a batch of this kind can have: the unit types of the game, the players and the map's locations.
// The chosen values, in order (a row each); the buttons and OK follow the list.
void ctBatchFillValues(HWND dlg, ClassicBatchKind kind, const std::vector<uint32_t>& chosen) {
    HWND list = GetDlgItem(dlg, kCtBatchValues);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (uint32_t value : chosen) {
        const std::wstring label = widen(classicValueText(classicBatchValueKind(kind), value, ctNames()));
        const int i = int(SendMessageW(list, LB_ADDSTRING, 0, LPARAM(label.c_str())));
        SendMessageW(list, LB_SETITEMDATA, WPARAM(i), LPARAM(value));
    }
    EnableWindow(GetDlgItem(dlg, IDOK), !chosen.empty());
    EnableWindow(GetDlgItem(dlg, kCtBatchRemoveValue), FALSE);
    EnableWindow(GetDlgItem(dlg, kCtBatchKind), chosen.empty());  // the kind is locked while the block has values
}

// The dialog's answer: the kind, the name and the values, in the list's order. False when no value is chosen.
bool ctBatchCollect(HWND dlg, CtBatchDialog& st) {
    HWND list = GetDlgItem(dlg, kCtBatchValues);
    const int n = int(SendMessageW(list, LB_GETCOUNT, 0, 0));
    if (n <= 0) return false;
    HWND kindBox = GetDlgItem(dlg, kCtBatchKind);
    const ClassicBatchKind kind = ClassicBatchKind(SendMessageW(kindBox, CB_GETITEMDATA, WPARAM(SendMessageW(kindBox, CB_GETCURSEL, 0, 0)), 0));
    st.batch->values.clear();
    for (int i = 0; i < n; ++i) st.batch->values.push_back(uint32_t(SendMessageW(list, LB_GETITEMDATA, WPARAM(i), 0)));
    st.batch->kind = kind;
    st.batch->name = classicBatchName(narrow(windowText(GetDlgItem(dlg, kCtBatchName))), kind);
    return true;
}

INT_PTR CALLBACK ctBatchProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto state = [dlg] { return reinterpret_cast<CtBatchDialog*>(GetWindowLongPtrW(dlg, DWLP_USER)); };
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        gCtBatchDlg = dlg;
        CtBatchDialog* st = state();
        st->shown = st->batch->kind;
        SetWindowTextW(dlg, st->hadBatch ? L"Edit the block" : L"Add a block");
        SetDlgItemTextW(dlg, kCtBatchName, widen(st->batch->name).c_str());
        SetDlgItemTextW(dlg, IDOK, st->hadBatch ? L"Save" : L"Add the block");
        SetDlgItemTextW(dlg, IDCANCEL, L"Cancel");
        SetDlgItemTextW(dlg, kCtBatchRemove, L"Remove the block");
        ShowWindow(GetDlgItem(dlg, kCtBatchRemove), st->hadBatch ? SW_SHOW : SW_HIDE);
        HWND kindBox = GetDlgItem(dlg, kCtBatchKind);
        int cur = 0;
        for (const ClassicBlockKind& k : classicBlockKinds()) {
            const int i = int(SendMessageW(kindBox, CB_ADDSTRING, 0, LPARAM(widen(k.label).c_str())));
            SendMessageW(kindBox, CB_SETITEMDATA, WPARAM(i), LPARAM(k.kind));
            if (k.kind == st->batch->kind) cur = i;
        }
        SendMessageW(kindBox, CB_SETCURSEL, WPARAM(cur), 0);
        ctBatchFillValues(dlg, st->batch->kind, st->batch->values);
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, WPARAM(app.font), TRUE);
        for (int id : {kCtBatchName, kCtBatchKind, kCtBatchValues}) darkControl(GetDlgItem(dlg, id), id == kCtBatchValues ? L"DarkMode_Explorer" : L"DarkMode_CFD");
        for (int id : {kCtBatchRemove, kCtBatchAddValue, kCtBatchRemoveValue, IDOK, IDCANCEL}) darkControl(GetDlgItem(dlg, id), L"DarkMode_Explorer");
        BOOL dark = !gLightTheme;
        DwmSetWindowAttribute(dlg, 20, &dark, sizeof dark), titleColors(dlg);
        return TRUE;
    }
    if (msg == WM_CTLCOLORDLG) return INT_PTR(app.bgBrush);
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor(HDC(wp), kText), SetBkColor(HDC(wp), kCtl);
        return INT_PTR(app.ctlBrush);
    }
    if (msg == WM_CTLCOLORSTATIC) {
        SetTextColor(HDC(wp), kDim), SetBkColor(HDC(wp), kBg);
        return INT_PTR(app.bgBrush);
    }
    if (msg != WM_COMMAND) return FALSE;
    const int id = LOWORD(wp), code = HIWORD(wp);
    CtBatchDialog* st = state();
    HWND kindBox = GetDlgItem(dlg, kCtBatchKind);
    const ClassicBatchKind chosenKind = ClassicBatchKind(SendMessageW(kindBox, CB_GETITEMDATA, WPARAM(SendMessageW(kindBox, CB_GETCURSEL, 0, 0)), 0));
    if (id == kCtBatchKind && code == CBN_SELCHANGE) {
        // The default name follows the kind (a name that is the old kind's default); the values start empty.
        const ClassicBatchKind old = st->shown;
        std::wstring name = windowText(GetDlgItem(dlg, kCtBatchName));
        if (name == widen(batchDefaultName(old))) SetDlgItemTextW(dlg, kCtBatchName, widen(batchDefaultName(chosenKind)).c_str());
        st->shown = chosenKind;
        ctBatchFillValues(dlg, chosenKind, {});
        return TRUE;
    }
    if (id == kCtBatchValues && code == LBN_SELCHANGE) {
        EnableWindow(GetDlgItem(dlg, kCtBatchRemoveValue), SendMessageW(GetDlgItem(dlg, kCtBatchValues), LB_GETCURSEL, 0, 0) != LB_ERR);
        return TRUE;
    }
    if (id == kCtBatchAddValue && code == BN_CLICKED) {
        // The searchable list of the block's kind in multi-pick: the block's values are checked, the list stays open
        // while values are checked, and OK makes the checked values the block's values (the values that stay keep
        // their order; new ones go last).
        HWND list = GetDlgItem(dlg, kCtBatchValues);
        const ClassicBatchKind kind = chosenKind;
        std::vector<uint32_t> old;
        std::set<std::string> checked;
        for (int i = 0; i < int(SendMessageW(list, LB_GETCOUNT, 0, 0)); ++i) {
            const uint32_t v = uint32_t(SendMessageW(list, LB_GETITEMDATA, WPARAM(i), 0));
            old.push_back(v), checked.insert(std::to_string(v));
        }
        std::vector<ListPickItem> items;
        for (const auto& [value, label] : ctKindChoices(ctBatchKindOf(kind))) {
            if (kind == ClassicBatchKind::Unit && value > 227) continue;  // (the groups of units are not one unit type)
            ListPickItem it;
            it.label = label, it.value = std::to_string(value);
            items.push_back(std::move(it));
        }
        RECT rc;
        GetWindowRect(GetDlgItem(dlg, kCtBatchAddValue), &rc);
        if (!showListPickerMulti(L"Search values", items, checked, L"Set", POINT{rc.left, rc.bottom + scale(2)})) return TRUE;
        std::vector<uint32_t> next;
        for (uint32_t v : old)
            if (checked.count(std::to_string(v))) next.push_back(v);
        for (const ListPickItem& it : items) {
            const uint32_t v = uint32_t(std::stoul(it.value));
            if (checked.count(it.value) && std::find(old.begin(), old.end(), v) == old.end()) next.push_back(v);
        }
        ctBatchFillValues(dlg, kind, next);
        return TRUE;
    }
    if (id == kCtBatchRemoveValue && code == BN_CLICKED) {  // the selected value goes; the others keep their order
        HWND list = GetDlgItem(dlg, kCtBatchValues);
        const int cur = int(SendMessageW(list, LB_GETCURSEL, 0, 0));
        if (cur == LB_ERR) return TRUE;
        SendMessageW(list, LB_DELETESTRING, WPARAM(cur), 0);
        const int n = int(SendMessageW(list, LB_GETCOUNT, 0, 0));
        if (n > 0) SendMessageW(list, LB_SETCURSEL, WPARAM(std::min(cur, n - 1)), 0);
        EnableWindow(GetDlgItem(dlg, IDOK), n > 0);
        EnableWindow(GetDlgItem(dlg, kCtBatchRemoveValue), n > 0);
        EnableWindow(GetDlgItem(dlg, kCtBatchKind), n == 0);  // the kind is locked while the block has values
        return TRUE;
    }
    if (id == IDOK) {
        if (ctBatchCollect(dlg, *st)) EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (id == kCtBatchRemove) {
        EndDialog(dlg, kCtBatchRemove);
        return TRUE;
    }
    if (id == IDCANCEL) {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// The dialog's template: a label and a field or list in each row, and the buttons under them.
std::vector<WORD> ctBatchTemplate() {
    std::vector<WORD> t;
    auto align = [&] { if (t.size() % 2) t.push_back(0); };
    auto str = [&](const wchar_t* s) { for (; *s; ++s) t.push_back(*s); t.push_back(0); };
    auto dword = [&](DWORD d) { t.push_back(LOWORD(d)); t.push_back(HIWORD(d)); };
    dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    dword(0);
    const size_t countAt = t.size();  // the number of items: set after they are all in (below)
    t.push_back(0);
    WORD items = 0;
    t.push_back(0), t.push_back(0), t.push_back(240), t.push_back(206);
    t.push_back(0), t.push_back(0);  // menu, class
    str(L"");                        // title (set later)
    t.push_back(9);
    str(L"Segoe UI");
    auto item = [&](DWORD style, short x, short y, short w, short h, WORD id, WORD cls, const wchar_t* text) {
        ++items;
        align();
        dword(style | WS_CHILD | WS_VISIBLE);
        dword(0);
        t.push_back(x), t.push_back(y), t.push_back(w), t.push_back(h), t.push_back(id);
        t.push_back(0xFFFF), t.push_back(cls);
        str(text);
        t.push_back(0);  // no creation data
    };
    // What a block is, in two sentences at the top (the dialog's text: PRINCIPLES, "Help in its place").
    item(SS_LEFT, 8, 8, 224, 22, 0xFFFF, 0x0082, L"A block repeats the actions in it for each value. The variable is the value of each copy.");
    item(SS_LEFT, 8, 37, 84, 10, 0xFFFF, 0x0082, L"Variable name");
    item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 96, 34, 136, 14, kCtBatchName, 0x0081, L"");
    item(SS_LEFT, 8, 57, 84, 10, 0xFFFF, 0x0082, L"Kind");
    item(WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 96, 54, 136, 80, kCtBatchKind, 0x0085, L"");
    item(SS_LEFT, 8, 77, 84, 10, 0xFFFF, 0x0082, L"Values");
    item(WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY, 96, 74, 136, 80, kCtBatchValues, 0x0083, L"");
    item(WS_TABSTOP | BS_PUSHBUTTON, 96, 158, 66, 14, kCtBatchAddValue, 0x0080, L"Add values\x2026");
    item(WS_TABSTOP | BS_PUSHBUTTON, 166, 158, 68, 14, kCtBatchRemoveValue, 0x0080, L"Remove the value");
    item(WS_TABSTOP | BS_PUSHBUTTON, 8, 186, 110, 14, kCtBatchRemove, 0x0080, L"");
    item(WS_TABSTOP | BS_DEFPUSHBUTTON, 130, 186, 50, 14, IDOK, 0x0080, L"");
    item(WS_TABSTOP | BS_PUSHBUTTON, 184, 186, 50, 14, IDCANCEL, 0x0080, L"");
    t[countAt] = items;
    align();
    return t;
}

INT_PTR ctBatchRun(CtBatchDialog& st) {
    const std::vector<WORD> t = ctBatchTemplate();
    const INT_PTR r = DialogBoxIndirectParamW(app.inst, reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), app.main, ctBatchProc, LPARAM(&st));
    gCtBatchDlg = nullptr;
    return r;
}

// Make a batch from the selected trigger, or change the batch (its variable, its values). The batch is written when
// the window applies its changes.
// `kind`: the kind of a new block (the picker's choice); an edited block keeps its kind until it has no values.
void ctBatchClick(ClassicBatchKind kind, int editRow) {
    ClassicTrigger* t = ctTrigger();
    if (!t) return;
    const int row = editRow;
    const bool editing = row >= 0 && row < t->actionCount() && t->actions[size_t(row)].type == kClassicBlockType;
    if (!editing && t->actionCount() >= kClassicActions) return setMessage(L"A trigger has at most 64 actions");
    ClassicBatch b = editing ? *t->actions[size_t(row)].batch : ClassicBatch{};
    if (!editing) b.kind = kind, b.name = batchDefaultName(kind);
    CtBatchDialog st;
    st.batch = &b, st.hadBatch = editing;
    const INT_PTR r = ctBatchRun(st);
    if (r == kCtBatchRemove && editing) {
        t->unwrapAction(row);  // the block goes; its actions stay, one level out
        gCt->collapsed.clear();
        setMessage(L"Removed the block");
        ctSelectEntry(1, -1);
    } else if (r == IDOK && editing) {
        t->actions[size_t(row)].batch = std::make_shared<ClassicBatch>(b);
        t->pruneBlocks();
        setMessage(L"Changed the block");
    } else if (r == IDOK) {  // a new block goes into the selected block (as its last child), else at the end
        ClassicAction blk;
        blk.type = kClassicBlockType;
        blk.batch = std::make_shared<ClassicBatch>(b);
        const int sel = gCt->entry;
        const bool inBlock = gCt->kind == 1 && sel >= 0 && sel < t->actionCount() && t->actions[size_t(sel)].type == kClassicBlockType;
        const int at = inBlock ? t->blockEnd(sel) : -1;
        if (inBlock) blk.depth = uint8_t(t->actions[size_t(sel)].depth + 1), gCt->collapsed.erase(sel);
        t->addAction(blk, at);
        setMessage(L"Added a block");
        ctSelectEntry(1, at >= 0 ? at : t->actionCount() - 1);
    } else {
        return;
    }
    ctFillList();
    ctBuildEditor();
    ctUpdateHeader();
    ctChanged();
}

void ctCommand(int id, int code) {
    ClassicTrigDialog& d = *gCt;
    ClassicTrigger* t = ctTrigger();
    if (id == kCtFilter && code == CBN_SELCHANGE) {
        const LRESULT k = SendMessageW(d.filterBox, CB_GETCURSEL, 0, 0);
        d.filter = k < 0 ? -1 : int(SendMessageW(d.filterBox, CB_GETITEMDATA, WPARAM(k), 0));
        d.listScroll = 0;
        ctFillList();
        ctSelectTrigger(d.shown.empty() ? -1 : std::find(d.shown.begin(), d.shown.end(), d.sel) != d.shown.end() ? d.sel : d.shown[0]);
        return;
    }
    if (d.filling) return;
    if (id >= kCtParam && id < kCtParam + 32) {
        if (code != BN_CLICKED) return;
        const int pi = id - kCtParam;
        const ClassicCondition* c = ctCondition();
        const ClassicAction* a = ctAction();
        const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
        if (def && pi < int(def->params.size()) && ctPickerIsList(def->params[size_t(pi)].kind)) {  // a list: the searchable list below the button
            RECT rc;
            GetWindowRect(GetDlgItem(d.wnd, id), &rc);
            return ctPickValue(pi, POINT{rc.left, rc.bottom + scale(2)});
        }
        return ctValuePopout(pi);  // a number or a text: its pop-out (OK and Cancel)
    }
    if (id == kCtType && code == CBN_SELCHANGE) {
        const LRESULT k = SendMessageW(d.typeBox, CB_GETCURSEL, 0, 0);
        if (k < 0) return;
        const uint8_t type = uint8_t(SendMessageW(d.typeBox, CB_GETITEMDATA, WPARAM(k), 0));
        if (ClassicCondition* c = ctCondition()) *c = retypeClassicCondition(*c, type);
        else if (ClassicAction* a = ctAction()) *a = retypeClassicAction(*a, type);
        if (t) t->pruneBlocks();  // a variable whose field the new type does not hold goes
        ctChanged();
        ctBuildEditor();
        return;
    }
    if (id == kCtSearch && code == EN_CHANGE) {
        d.query = windowText(d.search);
        d.listScroll = 0;
        ctFillList();
        ctSelectTrigger(d.shown.empty() ? -1 : std::find(d.shown.begin(), d.shown.end(), d.sel) != d.shown.end() ? d.sel : d.shown[0]);
        return;
    }
    if (code != BN_CLICKED) return;
    if (id >= kCtPlayer && id < kCtPlayer + kCtBoxCount) {
        if (!t) return;
        const bool on = SendMessageW(GetDlgItem(d.wnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
        t->players[size_t(kCtBoxGroups[id - kCtPlayer])] = on;
        ctFillList();  // it can leave (or join) the list of the player shown
        return;
    }
    if (id == kCtForces) return ctForcesClick();
    if (id == kCtTurnOff) return ctToggleTriggers();
    if (id == kCtDeleteTrig) return ctDeleteTriggers();
    if (id == kCtDisabled || id == kCtAlways) {
        const uint8_t bit = id == kCtDisabled ? 2 : 4;
        const bool on = SendMessageW(GetDlgItem(d.wnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
        uint8_t* flags = ctCondition() ? &ctCondition()->flags : ctAction() ? &ctAction()->flags : nullptr;
        if (flags) *flags = uint8_t(on ? *flags | bit : *flags & ~bit);
        ctChanged();
        return;
    }
}

// A tooltip for a native control of the page (the page's own tooltip window, which takes the control by its handle).
void ctTip(HWND control, const wchar_t* text) {
    if (!gCt || !gCt->tips || !control) return;
    TOOLINFOW ti{sizeof ti};
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = gCt->wnd, ti.uId = UINT_PTR(control);
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(gCt->tips, TTM_ADDTOOLW, 0, LPARAM(&ti));
}

// The page's parts at the size of its window: the left column (the search, the count, the filter, the list, the
// note) and the right one (the selected trigger's header and buttons, the players, the entries, the editor under
// them). The lists take the height that is left.
void ctLayout(HWND hwnd) {
    ClassicTrigDialog& d = *gCt;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    const int pad = scale(ui::kDialogPad), rowH = scale(ui::kFieldH), step = scale(ui::kFormRowH), gap = scale(8);
    // The least height the parts take (the header, the players, 120 for the entries, the editor): a shorter window
    // makes the page taller, and its view scrolls.
    const int needH = pad + scale(40) + 2 * step + gap + scale(120) + gap + step * 4 + gap + pad;
    if (H < needH) {
        SetWindowPos(hwnd, nullptr, 0, 0, W, needH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;  // (the size change comes back here)
    }
    const int leftW = std::clamp(W * 3 / 10, scale(280), scale(460));
    const int x0 = pad + leftW + scale(24), rightW = std::max(scale(600), W - x0 - pad);
    auto place = [](HWND h, int x, int y, int w, int hh) {
        if (h) MoveWindow(h, x, y, std::max(0, w), std::max(0, hh), TRUE);
    };
    // Left.
    int y = pad;
    place(d.search, pad, y, leftW - scale(92), rowH);
    place(d.count, pad + leftW - scale(84), y + scale(4), scale(84), rowH);
    y += step;
    place(d.filterLabel, pad, y, scale(150), rowH);
    place(d.filterBox, pad + scale(150), y, leftW - scale(150), scale(320));
    y += step;
    const bool noteShown = GetWindowTextLengthW(d.note) > 0;
    const int noteH = noteShown ? scale(44) : 0;
    const int listBottom = H - pad - (noteShown ? noteH + gap : 0);
    place(d.list, pad, y, leftW, listBottom - y);
    place(d.note, pad, listBottom + gap, leftW, noteH);
    // Right: the header.
    y = pad;
    const int delW = scale(150), turnW = scale(96);
    const int headW = rightW - delW - turnW - 3 * gap;
    place(d.headTitle, x0, y, std::min(headW, scale(150)), scale(28));
    place(d.headCount, x0 + scale(160), y + scale(6), std::max(0, headW - scale(160)), rowH);
    place(d.deleteTrig, x0 + rightW - delW, y + scale(2), delW, rowH);
    place(d.turnOff, x0 + rightW - delW - gap - turnW, y + scale(2), turnW, rowH);
    y += scale(40);
    // The players: one row of check boxes (players 1 to 8), then All players and the forces.
    const int cw = rightW / 8;
    for (int g = 0; g < 8; ++g) place(d.players[g], x0 + g * cw, y, cw - gap, rowH);
    y += step;
    place(d.players[8], x0, y, cw * 2 - gap, rowH);
    place(d.forces, x0 + cw * 2, y, scale(150), rowH);
    y += step + gap;
    // The editor under the entries: its title row and three rows of values.
    const int editorH = ctEditorHeight(step, gap), editorTop = H - pad - editorH;
    const int entriesH = std::max(scale(120), editorTop - gap - y);
    place(d.entries, x0, y, rightW, entriesH);
    const int ey = editorTop;
    place(d.editTitle, x0, ey, scale(360), scale(28));
    place(d.typeBox, x0 + scale(370), ey + scale(2), scale(200), scale(400));
    place(d.disabled, x0 + scale(580), ey + scale(2), scale(110), rowH);
    place(d.always, x0 + scale(700), ey + scale(2), scale(140), rowH);
    const int editTop = ey + step + gap, editRight = x0 + rightW;
    if (editTop != d.editTop || editRight != d.editRight || x0 != d.editLeft) {  // the values' controls go where the editor is
        d.editTop = editTop, d.editLeft = x0, d.editRight = editRight;
        ctBuildEditor();
    }
    ctShowSelected();  // the selected trigger in view at this size
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK classicTrigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicTrigDialog* d = gCt;
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        d->wnd = hwnd;
        d->params.clear(), d->paramPickers.clear();  // made again (the page showed before): the old ones went with it
        d->query.clear(), d->filter = -1, d->marked.clear(), d->anchor = -1;  // the search and the filter start empty, as their boxes do
        d->listScroll = d->entryScroll = 0;
        d->listSurface = {}, d->entrySurface = {};  // their tips belonged to the lists that went
        d->tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd, nullptr, app.inst, nullptr);
        styleTips(d->tips);
        auto font = [](HWND h, HFONT f = nullptr) { SendMessageW(h, WM_SETFONT, WPARAM(f ? f : app.font), TRUE); };
        auto label = [&](const wchar_t* s, int id = kCtDim, DWORD extra = 0) {
            HWND h = CreateWindowExW(0, WC_STATICW, s, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS | SS_CENTERIMAGE | extra, 0, 0, 0, 0, hwnd,
                                     HMENU(INT_PTR(id)), app.inst, nullptr);
            font(h);
            return h;
        };
        auto check = [&](int id, const std::wstring& s) {
            HWND h = CreateWindowExW(0, WC_BUTTONW, s.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd, HMENU(INT_PTR(id)),
                                     app.inst, nullptr);
            font(h);
            darkControl(h, L"DarkMode_Explorer");
            return h;
        };
        auto button = [&](int id, const std::wstring& s) {
            HWND h = CreateWindowExW(0, WC_BUTTONW, s.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, HMENU(INT_PTR(id)),
                                     app.inst, nullptr);
            font(h);
            darkControl(h, L"DarkMode_Explorer");
            return h;
        };
        auto painted = [&](int x, int y, int w, int h) {
            HWND l = CreateWindowExW(0, L"StarDraftClassicTrigList", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL, x, y, w, h, hwnd, nullptr, app.inst, nullptr);
            darkControl(l, L"DarkMode_Explorer");  // the scroll bar in the theme
            return l;
        };
        // Left: the search box (the triggers whose text has its words), the count, the triggers of a player, the list.
        d->search = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, HMENU(INT_PTR(kCtSearch)), app.inst, nullptr);
        font(d->search);
        darkControl(d->search, L"DarkMode_CFD");
        SendMessageW(d->search, EM_SETCUEBANNER, TRUE, LPARAM(L"Search triggers"));
        addSearchClear(d->search);
        d->count = label(L"", kCtDim, SS_RIGHT);
        d->filterLabel = label(L"Show the triggers of");
        d->filterBox = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd,
                                       HMENU(INT_PTR(kCtFilter)), app.inst, nullptr);
        font(d->filterBox);
        darkControl(d->filterBox, L"DarkMode_CFD");
        {
            const ClassicNames n = ctNames();
            auto add = [&](const std::wstring& s, int g) {
                const int k = int(SendMessageW(d->filterBox, CB_ADDSTRING, 0, LPARAM(s.c_str())));
                SendMessageW(d->filterBox, CB_SETITEMDATA, WPARAM(k), LPARAM(g));
            };
            add(L"All triggers", -1);
            for (int g : kCtGroups) add(widen(classicPlayerName(uint32_t(g), n)), g);
            SendMessageW(d->filterBox, CB_SETCURSEL, 0, 0);
        }
        d->note = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_LEFT, 0, 0, 0, 0, hwnd, HMENU(INT_PTR(kCtDim)), app.inst, nullptr);
        font(d->note);
        d->list = painted(0, 0, 0, 0);
        d->entries = painted(0, 0, 0, 0);
        // Right: the selected trigger (its name, its count, its buttons), its players, the entry's editor.
        d->headTitle = label(L"None", 0);
        font(d->headTitle, ui::titleFont());
        d->headCount = label(L"");
        d->turnOff = button(kCtTurnOff, L"Turn off");
        d->deleteTrig = button(kCtDeleteTrig, L"Delete the trigger");
        d->forces = button(kCtForces, L"Forces\x2026");
        ctTip(d->forces, L"Forces: the forces that run the trigger");
        {
            const ClassicNames n = ctNames();
            for (int g = 0; g < kCtBoxCount; ++g) d->players[g] = check(kCtPlayer + g, widen(classicPlayerName(uint32_t(kCtBoxGroups[g]), n)));
        }
        d->editTitle = label(L"", 0);
        font(d->editTitle, ui::titleFont());
        d->typeBox = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, hwnd, HMENU(INT_PTR(kCtType)),
                                     app.inst, nullptr);
        font(d->typeBox);
        darkControl(d->typeBox, L"DarkMode_CFD");
        d->disabled = check(kCtDisabled, L"Turn off");
        ctTip(d->disabled, L"The condition or action does nothing in the game (Turn off)");
        d->always = check(kCtAlways, L"Always show");
        ctTip(d->always, L"The game's Always display flag for this action");
        d->batch = nullptr;
        // A note over the list: a project's map (L.4), EUD values.
        std::wstring note;
        if (projectMode()) note = L"The project's map has these classic triggers. The built map keeps them; they run with the project's triggers.";
        int eud = 0;
        for (const ClassicTrigger& t : d->trig.triggers) {
            for (int i = 0; i < t.conditionCount(); ++i) eud += classicEud(t.conditions[size_t(i)]);
            for (int i = 0; i < t.actionCount(); ++i) eud += classicEud(t.actions[size_t(i)]);
        }
        if (eud) note += (note.empty() ? L"" : L" ") + std::to_wstring(eud) + L" conditions and actions have EUD values. They stay as they are.";
        SetWindowTextW(d->note, note.c_str());
        ctLayout(hwnd);
        ctFillList();
        ctSelectTrigger(d->shown.empty() ? -1 : d->shown[0]);
        return 0;
    }
    case WM_SIZE:
        if (gCt && gCt->list) ctLayout(hwnd);
        return 0;
    case WM_COMMAND:
        ctCommand(LOWORD(wp), HIWORD(wp));
        mapPageChanged();
        return 0;
    case WM_ERASEBKGND: {  // the theme's ground (the class brush is the one of the theme at the first open)
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(HDC(wp), &rc, app.bgBrush);
        return 1;
    }
    case WM_PAINT: {  // the line under each section heading (kCtHead), as ui::sectionHeader draws it
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            if (GetDlgCtrlID(c) == kCtHead) {
                RECT r;
                GetWindowRect(c, &r);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
                fillAlpha(dc, {r.left, r.bottom, r.right, r.bottom + 1}, kBorder, 255);
            }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc = HDC(wp);
        const HWND c = HWND(lp);
        SetTextColor(dc, !IsWindowEnabled(c) ? kFaint : GetDlgCtrlID(c) == kCtDim ? kDim : kText);
        const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        SetBkColor(dc, field ? kCtl : kBg);
        return LRESULT(field ? app.ctlBrush : app.bgBrush);
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// L.4: a project made from a classic map keeps its classic data in the map. Does the project's map have classic
// triggers (any trigger in TRIG), or classic settings (a unit, upgrade or tech that does not use the game's values, or
// players with their own values)? Read again only when the map changed.
bool classicSettingsUsed(const Editor& ed) {
    const SettingDefaults* def = ed.settingDefaults();
    if (!def) return false;
    auto own = [](const std::array<bool, kSettingPlayers>& usesDefault) {
        return std::any_of(usesDefault.begin(), usesDefault.end(), [](bool b) { return !b; });
    };
    const UnitSettings u = ed.unitSettings();
    for (int i = 0; i < kSettingUnits; ++i)
        if (!u.units[size_t(i)].useDefault || !u.availability[size_t(i)].global || own(u.availability[size_t(i)].usesDefault)) return true;
    const UpgradeSettings g = ed.upgradeSettings();
    for (int i = 0; i < kSettingUpgrades; ++i)
        if (!g.upgrades[size_t(i)].useDefault || own(g.levels[size_t(i)].usesDefault)) return true;
    const TechSettings t = ed.techSettings();
    for (int i = 0; i < kSettingTechs; ++i)
        if (!t.techs[size_t(i)].useDefault || own(t.players[size_t(i)].usesDefault)) return true;
    return false;
}

bool classicCommandInProject(int id) {
    static const Editor* ed = nullptr;
    static uint64_t revision = 0;
    static bool triggers = false, settings = false;
    if (!app.ed || !app.ed->isOpen()) return false;
    if (ed != app.ed.get() || revision != app.ed->revision()) {
        ed = app.ed.get(), revision = app.ed->revision();
        triggers = !app.ed->classicTriggers().triggers.empty();
        settings = classicSettingsUsed(*app.ed);
    }
    return id == ID_SCEN_CLASSICTRIG ? triggers : id == ID_SCEN_CLASSIC ? settings : false;
}

// The Classic triggers page's working copy from the map.
void ctLoad(ClassicTrigDialog& d) {
    d = ClassicTrigDialog{};
    d.strings = app.ed->strings();
    d.trig = classicCollapseBatches(app.ed->classicTriggers(), d.strings);  // a batch is its template in the list
    d.before = d.trig.triggers;
    d.ai = loadClassicAiScripts(app.data);
}

// Something to apply: a trigger differs from the map's when the page read them. (A text typed in an action makes a
// string and changes the action, so the triggers tell it too.)
bool ctCopyChanged(const ClassicTrigDialog& d) { return d.trig.triggers != d.before; }

// Apply: TRIG (and the new strings) as one undo step (Editor::setClassicTriggers). The values the page does not know
// (EUD values, the bytes after the last trigger, the flags) stay as they are.
bool ctApply(ClassicTrigDialog& d) {
    if (!classicCanExpand(d.trig)) {  // a long trigger needs a switch that no trigger uses
        setMessage(L"Could not set the classic triggers. A long trigger needs a switch that no trigger uses.");
        return true;
    }
    // The strings this page made that no action uses now go again.
    for (uint32_t id : d.ownStrings) {
        bool used = false;
        for (const ClassicTrigger& t : d.trig.triggers)
            for (int i = 0; i < t.actionCount() && !used; ++i) used = t.actions[size_t(i)].text == id;
        if (!used) d.strings.set(id, "");
    }
    const ClassicTriggers expanded = classicExpandBatches(d.trig, d.strings);  // the batches' triggers, as the game reads them
    if (!app.ed->setClassicTriggers(expanded, &d.strings)) {
        setMessage(L"Classic triggers: no change");
        return true;
    }
    updateTitle();
    app.dirty = true;
    setMessage(L"Set the classic triggers");
    return true;
}

// --- Tests --------------------------------------------------------------------------------------------------------
void pump();
bool capture(const fs::path& file, HWND wnd);
HWND mapPageWindow();                 // map_pages.inc: the page that shows
bool mapPageClickButton(bool apply);  // map_pages.inc: Apply (or Revert) clicked, as the mouse does

// Picks the item with this data in a combo box, as a click does (the window gets CBN_SELCHANGE).
void ctPick(HWND combo, uint32_t value) {
    const int n = int(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < n; ++i)
        if (uint32_t(SendMessageW(combo, CB_GETITEMDATA, WPARAM(i), 0)) == value) {
            SendMessageW(combo, CB_SETCURSEL, WPARAM(i), 0);
            SendMessageW(GetParent(combo), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), LPARAM(combo));
            return;
        }
}
// The index of the selected entry's parameter with this name ("n", "unit", "text", ...), or -1.
int ctParamIndex(const char* name) {
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def) return -1;
    for (size_t i = 0; i < def->params.size(); ++i)
        if (std::string(def->params[i].name) == name) return int(i);
    return -1;
}
// Sets the selected entry's parameter with this name to a value (as its list or pop-out does).
void ctSetParam(const char* name, uint32_t v) {
    const int i = ctParamIndex(name);
    if (i < 0) return;
    ctSetValue(i, v);
    ctBuildEditor();
}

size_t ctTriggerCount(const Bytes& trig) {
    Chk c;
    c.sections.push_back({"TRIG", trig});
    return readClassicTriggers(c).triggers.size();
}

// Drags row `from` of a painted list by its grip to place `to` (in the list without it), through the click areas
// the paint made, as the mouse does: the grip's drag at the row, at the place, then the release. `shot`: a capture
// while the row is at the place (the drop line).
bool ctDragRow(HWND wnd, ui::Surface& surface, const char* list, int from, int to, const fs::path& shot = {}) {
    RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    const auto rows = ui::gReorderRows[list];
    if (from < 0 || from >= int(rows.size()) || to < 0 || to >= int(rows.size())) return false;
    const POINT grip{rows[size_t(from)].left + scale(8), (rows[size_t(from)].top + rows[size_t(from)].bottom) / 2};
    const DataWidgetHit* h = surface.at(grip);
    if (!h || !h->drag || !h->release) return false;
    const auto drag = h->drag;
    const auto release = h->release;
    const int slot = to > from ? to + 1 : to;  // the row the dragged one goes before
    const POINT at{grip.x, slot < int(rows.size()) ? rows[size_t(slot)].top + 1 : rows.back().bottom - 1};
    drag(grip);
    drag(at);
    if (!shot.empty()) {
        RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        capture(shot, GetParent(wnd));
    }
    release();
    for (int i = 0; i < 2; ++i) pump();
    return true;
}

/// The modal pop-outs of the datashots: a picker, a value pop-out, the block dialog and a right-click menu are run by their
// own loops, which dispatch timers. The timer captures the pop-out and closes it (ctModalShot).
fs::path gCtShotFile;                 // where the pop-out's picture goes
enum class CtShot { None, Picker, Dialog, Menu };
CtShot gCtShotKind = CtShot::None;
VOID CALLBACK ctShotTimer(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    switch (gCtShotKind) {
    case CtShot::Picker:  // (the picker's window closes with WM_CLOSE: it is a cancel)
        if (const HWND pick = FindWindowW(L"StarDraftListPick", nullptr)) capture(gCtShotFile, pick), PostMessageW(pick, WM_CLOSE, 0, 0);
        break;
    case CtShot::Dialog: {
        const HWND dlg = gCtValueDlg ? gCtValueDlg : gCtBatchDlg;
        if (dlg) capture(gCtShotFile, dlg), PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
        break;
    }
    case CtShot::Menu:
        if (const HWND m = openMenuWindow()) capture(gCtShotFile, m);
        closeMenus();
        break;
    case CtShot::None: break;
    }
    gCtShotKind = CtShot::None;
}
// Runs `open` (a modal pop-out of the page) with the timer that captures it to `file`. The log says what the shot did.
void ctModalShot(CtShot kind, const fs::path& file, const std::function<void()>& open) {
    gCtShotFile = file, gCtShotKind = kind;
    const UINT_PTR timer = SetTimer(nullptr, 0, 500, ctShotTimer);  // (a timer of the thread: its id is the one returned)
    open();
    if (timer) KillTimer(nullptr, timer);
    gCtShotKind = CtShot::None;
}

// Datashots classictrig: (the page: classic_triggers.png), classictrig:set (a trigger made through the page: for
// player 1, Bring 5 men to Anywhere; Create 1 Zergling, then Show a text; Apply, saved, opened again, undone) and
// classictrig:move (triggers, then an action, moved by a drag: in the list of all triggers and in a player's list;
// Apply; the map has the new order; one undo gives back TRIG byte for byte). classictrig.txt says what holds.
void classicTrigDatashot(const std::string& spec, const fs::path& dir) {
    std::ofstream log(dir / "classictrig.txt", std::ios::app);
    static std::string mode;
    static fs::path out;
    static std::vector<ClassicTrigger> expected;  // move: the order the window must have before OK
    static std::vector<std::string> moveLog;
    mode = spec.substr(spec.find(':') + 1), out = dir, expected.clear(), moveLog.clear();
    const bool set = mode == "set", move = mode == "move", batch = mode == "batch";
    ClassicTrigger madeCopy;  // batch: the trigger that was made, as the page has it
    int madeAt = -1;          // its place in the list (ctAddTrigger puts it after the selected one)
    const Bytes before = app.ed ? app.ed->chk().effective("TRIG") : Bytes();
    int shownTriggers = -1;
    command(ID_SCEN_CLASSICTRIG);  // the page, in the main area
    if (gWorkspace == Workspace::ClassicTriggers && gCt) {
        const HWND w = mapPageWindow();
        for (int i = 0; i < 4; ++i) pump();
        shownTriggers = int(gCt->shown.size());
        const std::string light = gLightTheme ? "_light" : "";
        // The states of the page (classictrig:<state>): the page as the map has it, then the state. A pop-out is captured
        // by its own timer (classictrig_popout_<state>.png) and closed; the page is captured after it.
        if (gBackground && (mode == "value" || mode == "text" || mode == "blockdlg"))  // a dialog activates its window: not in a background run
            log << mode << ": NOT captured (its dialog takes the focus; run it in the foreground)\n";
        const std::string scaleName = app.dpi >= 180 ? "_200" : "";  // (dpi:192 before the spec: 200 %)
        const fs::path popout = out / ("classictrig_popout_" + mode + scaleName + light + ".png");
        if (mode == "search") {  // a search with results: "create" in the box
            SetWindowTextW(gCt->search, L"create");
            SendMessageW(gCt->wnd, WM_COMMAND, MAKEWPARAM(kCtSearch, EN_CHANGE), LPARAM(gCt->search));
        } else if (mode == "off") {  // a turned-off trigger: the first one of the list
            ctSelectTrigger(0);
            ctToggleOne(0);
        } else if (mode == "add") {  // the add-action picker with the Blocks group first
            ctSelectTrigger(0);
            ctModalShot(CtShot::Picker, popout, [] { ctAddClick(1, RECT{scale(10), 0, scale(200), scale(30)}); });
        } else if ((mode == "value" || mode == "text") && !gBackground) {  // a number (a Bring's count) or a text (Display text message): OK and Cancel
            ctAddTrigger();
            if (mode == "value") ctAddEntry(0, 3);  // Bring: its count is the number
            else ctAddEntry(1, 9);                  // Display text message: its text
            const int pi = ctParamIndex(mode == "value" ? "n" : "text");
            ctModalShot(CtShot::Dialog, popout, [pi] { ctValuePopout(pi); });
        } else if (mode == "forces") {  // the forces of the trigger: a multi-pick list
            ctSelectTrigger(0);
            ctModalShot(CtShot::Picker, popout, [] { ctForcesClick(); });
        } else if (mode == "values") {  // a block's values: the searchable list in multi-pick, three checked
            std::vector<ListPickItem> items;
            for (const auto& [value, label] : ctKindChoices(ClassicKind::Unit)) {
                if (value > 227) continue;
                ListPickItem it;
                it.label = label, it.value = std::to_string(value);
                items.push_back(std::move(it));
            }
            std::set<std::string> checked{"0", "1", "37"};
            ctModalShot(CtShot::Picker, popout, [&] { showListPickerMulti(L"Search values", items, checked, L"Set", POINT{scale(400), scale(200)}); });
        } else if (mode == "blockdlg" && !gBackground) {  // the block dialog for a new block (For each unit)
            ctSelectTrigger(0);
            ctModalShot(CtShot::Dialog, popout, [] { ctBatchClick(ClassicBatchKind::Unit, -1); });
        } else if (mode == "menu") {  // the right-click menu of the first trigger's row
            ctSelectTrigger(0);
            RECT lr{};
            GetWindowRect(gCt->list, &lr);
            const POINT at{lr.left + scale(60), lr.top + scale(70)};
            ctModalShot(CtShot::Menu, popout, [at] { ctTriggerMenu(at, 0); });
        }
        if (mode == "set") {
            ctPick(gCt->filterBox, 0);  // player 1's triggers
            ctAddTrigger();
            ctPick(gCt->typeBox, 3);  // Bring
            ctSetParam("n", 5);
            ctSetParam("unit", 230);  // men
            ctAddEntry(1, 9);  // Display text message
        if (const int i = ctParamIndex("text"); i >= 0) ctSetText(i, "Hello from StarDraft 2");  // (the text's pop-out: OK in it)
            ctAddEntry(1, 9);
            ctPick(gCt->typeBox, 44);  // Create unit
            ctSetParam("unit", 37);  // Zergling
            ctSetParam("player", 0);
            ctDragRow(gCt->entries, gCt->entrySurface, kCtEntryName[1], 1, 0);  // Create unit first, by a drag
        } else if (mode == "move") {
            auto& v = gCt->trig.triggers;
            auto order = [&](const std::vector<ClassicTrigger>& want) { return v == want ? "yes" : "NO"; };
            if (v.size() < 3) {
                moveLog.push_back("move: NOT enough triggers (3 needed, " + std::to_string(v.size()) + ")");
            } else {
                // All triggers: the first one dragged to the third place.
                std::vector<ClassicTrigger> want = v;
                std::rotate(want.begin(), want.begin() + 1, want.begin() + 3);
                const bool dragged = ctDragRow(gCt->list, gCt->listSurface, kCtListName, 0, 2, out / ("classic_triggers_drag" + light + ".png"));
                moveLog.push_back(std::string("all triggers: the first trigger dragged to the third place ") + (dragged ? "" : "(drag NOT done) ") + order(want));
                // A player's triggers: the third one in the list dragged to the top; the triggers that do not show
                // keep their places.
                int group = -1;
                for (int g : kCtGroups) {
                    std::vector<int> s;
                    for (int i = 0; i < int(v.size()); ++i)
                        if (v[size_t(i)].runsFor(g)) s.push_back(i);
                    if (s.size() >= 3 && s.size() < v.size() && (group < 0 || s[2] - s[0] > 2)) group = g;
                }
                if (group < 0) {
                    moveLog.push_back("player's triggers: NOT a player with 3 triggers and triggers that do not show");
                } else {
                    ctPick(gCt->filterBox, uint32_t(group));
                    const std::vector<int> s = gCt->shown;
                    want = v;
                    ClassicTrigger t = want[size_t(s[2])];
                    want.erase(want.begin() + s[2]);
                    want.insert(want.begin() + s[0], t);
                    const bool d2 = ctDragRow(gCt->list, gCt->listSurface, kCtListName, 2, 0);
                    moveLog.push_back("player's triggers (group " + std::to_string(group) + ", triggers " + std::to_string(s[0] + 1) + " and " +
                                      std::to_string(s[2] + 1) + ", " + std::to_string(s[2] - s[0] - 2) + " not shown between): the third dragged to the top " +
                                      (d2 ? "" : "(drag NOT done) ") + order(want));
                }
                // An action: the first one dragged under the second.
                ctPick(gCt->filterBox, uint32_t(-1));
                int withActions = -1;
                for (int i = 0; i < int(v.size()) && withActions < 0; ++i)
                    if (v[size_t(i)].actionCount() >= 2) withActions = i;
                if (withActions < 0) {
                    moveLog.push_back("action: NOT a trigger with 2 actions");
                } else {
                    ctSelectTrigger(withActions);
                    want = v;
                    want[size_t(withActions)].moveAction(0, 1);
                    const bool d3 = ctDragRow(gCt->entries, gCt->entrySurface, kCtEntryName[1], 0, 1);
                    moveLog.push_back("action: the first action of trigger " + std::to_string(withActions + 1) + " dragged under the second " +
                                      (d3 ? "" : "(drag NOT done) ") + order(want));
                }
                expected = v;
            }
        } else if (mode == "batch") {
            // A trigger for player 1 with Create unit (a Zergling). The batch dialog is made as the window makes it,
            // but not shown (no focus): it selects Terran Marine, Terran Firebat and Zergling, then reads them back.
            // Then the unit is the batch's variable.
            // A block of units (Terran Marine, Terran Firebat, Zergling) with a block of players (Player 1, Player 2)
            // in it, and Create unit in the inner block: its unit is the outer variable, its player the inner one.
            ctPick(gCt->filterBox, 0);
            ctAddTrigger();
            ClassicTrigger* t = ctTrigger();
            ClassicAction outer;
            outer.type = kClassicBlockType, outer.depth = 0;
            outer.batch = std::make_shared<ClassicBatch>(ClassicBatch{"unit", ClassicBatchKind::Unit, {0, 1, 37}});
            ClassicAction inner;
            inner.type = kClassicBlockType, inner.depth = 1;
            inner.batch = std::make_shared<ClassicBatch>(ClassicBatch{"player", ClassicBatchKind::Player, {0, 1}});
            ClassicAction make = makeClassicAction(44);  // Create unit
            make.depth = 2, make.var[2] = 1, make.var[1] = 2;
            t->addAction(outer, -1), t->addAction(inner, -1), t->addAction(make, -1);
            madeCopy = *t, madeAt = gCt->sel;
            ctFillList();
            ctSelectEntry(1, 1);  // the inner block: the picture shows a block in a block
            log << "nested batch: " << (t->actionCount() == 3 ? "yes" : "NO") << "\n";
            ctChanged();  // the blocks are an edit of the page: Apply writes them
        } else if (!gCt->shown.empty()) {
            ctSelectEntry(1, 0);
        }
        mapPageChanged();
        RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        for (int i = 0; i < 10; ++i) pump(), Sleep(20);
        capture(out / ("classic_triggers" + (mode.empty() ? std::string() : "_" + mode) + scaleName + light + ".png"), app.main);
        log << "state " << (mode.empty() ? std::string("page") : mode) << ": list scroll " << gCt->listScroll << ", entries scroll " << gCt->entryScroll << ", list height "
            << gCt->listHeight << ", " << gCt->shown.size() << " triggers shown\n";
        if (set || move || batch) log << "Apply " << (mapPageClickButton(true) ? "clicked" : "NOT clicked") << "\n";
    }
    command(ID_WS_MAP);  // leave the page (with no changes left, the map is as Apply left it)
    for (int i = 0; i < 4; ++i) pump();
    if (!app.ed) return;
    if (!set && !move && !batch) {  // a state of the page changed the map (a trigger made, turned off): undo it back
        int steps = 0;
        while (steps < 8 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
        log << "the state's changes are undone " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << " (" << steps << " undo steps)\n";
    }
    const size_t count = ctTriggerCount(app.ed->chk().effective("TRIG")), had = ctTriggerCount(before);
    log << spec << ": the page " << (shownTriggers >= 0 ? "opened" : "did NOT open") << ", " << shownTriggers << " triggers in the list (" << had
        << " in the map)\n";
    if (batch) {
        // The map has the template and the three triggers the batch makes; read back, the batch is the same; undone.
        const ClassicTriggers saved = app.ed->classicTriggers();
        log << "the map has one trigger more (the blocks make six copies in it) " << (count == had + 1 ? "yes" : "NO") << "\n";
        // Each edit is one undo step (the page writes it into the map at once): the variable was the last one.
        const ClassicTriggers working = classicCollapseBatches(saved, app.ed->strings());
        const bool same = madeAt >= 0 && size_t(madeAt) < working.triggers.size() && working.triggers[size_t(madeAt)] == madeCopy;
        log << "read back: the blocks are the same (the outer unit block with the player block in it) " << (same ? "yes" : "NO") << "\n";
        std::string ext = app.ed->path().extension().string(), err;
        const fs::path file = dir / ("classictrig_batch" + (ext.empty() ? std::string(".scx") : ext));
        Editor back(app.data);
        const bool reopened = app.ed->save(file, err) && back.open(file, err);
        const ClassicTriggers reread = reopened ? classicCollapseBatches(back.classicTriggers(), back.strings()) : ClassicTriggers{};
        const bool again = madeAt >= 0 && size_t(madeAt) < reread.triggers.size() && reread.triggers[size_t(madeAt)] == madeCopy;
        log << "saved and opened again: the same blocks " << (reopened && again ? "yes" : "NO " + err) << "\n";
        // The page makes an undo step for each change (adding the trigger, adding the block): undo back to the start.
        int steps = 0;
        while (steps < 6 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
        log << "undo steps back to the map before the page changed it: " << steps << (app.ed->chk().effective("TRIG") == before ? " (back at the start)" : " (NOT back at the start)")
            << "\n";
        return;
    }
    if (move) {
        for (const std::string& s : moveLog) log << s << "\n";
        if (expected.empty()) return;
        log << "Apply: the map has the new order " << (app.ed->classicTriggers().triggers == expected ? "yes" : "NO") << "\n";
        app.ed->undo();
        log << "one undo step gives back the map's TRIG byte for byte " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << "\n";
        return;
    }
    if (!set) {
        log << "no Apply: no change " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << "\n";
        return;
    }
    auto check = [&](Editor& e, const char* what) {
        const ClassicTriggers t = e.classicTriggers();
        const ClassicTrigger* made = nullptr;
        for (const ClassicTrigger& tr : t.triggers)
            if (tr.conditions[0].type == 3 && tr.conditions[0].amount == 5 && tr.conditions[0].unit == 230) made = &tr;
        const bool ok = made && made->runsFor(0) && made->actionCount() == 2 && made->actions[0].type == 44 && made->actions[0].unit == 37 &&
                        made->actions[0].player == 0 && made->actions[1].type == 9 && e.strings().get(made->actions[1].text) == "Hello from StarDraft 2";
        log << what << ": " << t.triggers.size() << " triggers; the new one (player 1, Bring 5 men, Create unit, then the text) " << (ok ? "yes" : "NO")
            << "\n";
    };
    log << "one trigger more " << (count == had + 1 ? "yes" : "NO") << "\n";
    check(*app.ed, "set");
    std::string ext = app.ed->path().extension().string(), err;
    const fs::path file = dir / ("classictrig_set" + (ext.empty() ? std::string(".scx") : ext));
    Editor back(app.data);
    if (app.ed->save(file, err) && back.open(file, err)) check(back, "saved and opened again");
    else log << "save: NO (" << err << ")\n";
    app.ed->undo();
    log << "one undo step gives back the map's TRIG " << (app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << "\n";
}

// Datashot classicproject: (L.4) the open classic map made a project as File > Project > Make this map a project
// does (into <dir>/classicproject/). The project's map keeps TRIG, MBRF and the settings sections byte-identical;
// Scenario > Classic triggers is there in the project (Map data is not: classic maps only), with the note
// (classic_triggers_project.png). classicproject.txt says what holds. (classic_trigger_test --migrate builds one.)
void classicProjectDatashot(const fs::path& dir) {
    std::ofstream log(dir / "classicproject.txt");
    if (!app.ed || !classicMode()) {
        log << "NOT a classic map tab\n";
        return;
    }
    const Chk original = app.ed->chk();
    const bool settings = classicSettingsUsed(*app.ed);
    std::error_code ec;
    fs::remove_all(dir / "classicproject", ec);
    std::string err;
    const bool made = createProject(dir, "classicproject", true, err);
    for (int i = 0; i < 4; ++i) pump();
    log << "made a project: " << (made && projectMode() ? "yes" : "NO " + err) << "\n";
    if (!made || !activeProject()) return;
    Bytes in;
    std::string differ;
    if (readMapChk(activeProject()->inputMap(), in, err)) {
        const Chk c = Chk::parse(in);
        for (const char* n : {"TRIG", "MBRF", "UNIx", "UNIS", "PUNI", "UPGx", "UPGS", "PUPx", "UPGR", "TECx", "TECS", "PTEx", "PTEC", "MRGN", "STR ", "STRx"}) {
            SectionName s;
            std::copy(n, n + 4, s.c.begin());
            if (original.effective(s) != c.effective(s)) differ += std::string(" ") + n;
        }
    } else {
        differ = " (the project's map does not read: " + err + ")";
    }
    log << "the project's map keeps TRIG, MBRF, the settings, MRGN and the strings byte-identical " << (differ.empty() ? "yes" : "NO:" + differ) << "\n";
    const bool trig = !readClassicTriggers(original).triggers.empty();
    log << "Classic triggers in the project " << (commandInMode(ID_SCEN_CLASSICTRIG) == trig ? "yes" : "NO") << " (the map has " << (trig ? "" : "no ")
        << "classic triggers); Map data not in the project " << (!commandInMode(ID_SCEN_CLASSIC) ? "yes" : "NO") << " (the map has "
        << (settings ? "" : "no ") << "unit, upgrade or tech settings; the project's map keeps them)\n";
    if (trig) {
        command(ID_SCEN_CLASSICTRIG);
        for (int i = 0; i < 10; ++i) pump(), Sleep(20);
        const bool shown = gWorkspace == Workspace::ClassicTriggers && gCt && IsWindowVisible(gCt->note) && !gCt->trig.triggers.empty();
        capture(dir / "classic_triggers_project.png", app.main);
        command(ID_WS_MAP);
        log << "the Classic triggers page opens in the project with the note " << (shown ? "yes" : "NO") << "\n";
    }
}
