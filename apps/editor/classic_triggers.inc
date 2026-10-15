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
struct ClassicTrigDialog : CtModel {  // (the model: core/views_classic_triggers.inc)
    std::set<int> collapsed;   // the action rows of collapsed blocks (by row; cleared when the trigger changes)
    HWND wnd = nullptr, filterBox = nullptr, list = nullptr, note = nullptr, entries = nullptr;
    HWND search = nullptr, count = nullptr, filterLabel = nullptr, tips = nullptr;
    HWND head = nullptr;                   // the head's window: a view window of headView (the trigger, its buttons, the players, Forces)
    std::unique_ptr<CtHeadView> headView;  // (core/views_classic_triggers.inc; made with the page's window)
    HWND editTitle = nullptr, typeBox = nullptr, disabled = nullptr, always = nullptr, batch = nullptr;
    int hold = 0;           // > 0: the changes of a batch (a paste of several triggers) go into the map once, at the end
    std::vector<HWND> params;  // the labels and pickers of the selected entry (made again when it changes)
    std::vector<HWND> paramPickers;  // one per parameter of the type
    int editTop = 0, editLeft = 0, editRight = 0;
    bool filling = false;
    // The painted lists: the triggers (list) and the conditions and actions (entries). A pane is a list's view (core) and the
    // window's host of it; made when the list's window first gets a message.
    struct Pane {
        std::unique_ptr<CtListView> view;
        std::unique_ptr<ui::Win32Host> host;  // (after the view: it goes first)
    };
    Pane listPane, entryPane;
};
ClassicTrigDialog* gCt = nullptr;  // the working copy of the page that shows (map_pages.inc)
HWND gCtValueDlg = nullptr;        // the value pop-out (ctValueProc) while it is open
BatchView* gCtBatchView = nullptr;  // the block dialog (core/views_classic_batch.inc) while it is open
CtValueView* gCtValueView = nullptr;  // the number pop-out (core/views_classic_value.inc) while it is open
extern ListPickView* gListPick;       // the list picker (core/views_list_pick.inc) while it is open (main.cpp)
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
// The most actions a trigger's rows have (each row is one action or a block with its children).
int ctActionLimit(const ClassicTrigger&) { return kClassicActions; }

// A trigger's line in the list: its conditions, then (dim) its actions.
std::wstring ctTriggerLine(const ClassicTrigger& t, bool actions) { return ctTriggerLineOf(t, actions, ctNames()); }  // (core)

// After a change: the painted lists show it.
void ctUpdateHeader();  // (below)
void ctChanged() {
    if (!gCt) return;
    InvalidateRect(gCt->list, nullptr, FALSE);
    InvalidateRect(gCt->entries, nullptr, FALSE);
    ctUpdateHeader();  // the count of actions (a block, a trigger's rows) is the change's
    if (!gCt->hold) mapPageChanged();
}

// The text a search matches: the trigger's sentences (its conditions and actions, names of units, locations and
// switches) and the names of its players.
// The list's rows: the triggers that run for the player shown and have every word of the search (core).
// The page's commands' host (core/views_classic_triggers.inc, CtModelHost): the page's windows and the program's state.
void ctSelectTrigger(int index);
class CtWinModelHost final : public CtModelHost {
public:
    ClassicNames names() override { return ctNames(); }
    void filled() override {  // the count of the shown triggers, and the list painted again
        ClassicTrigDialog& d = *gCt;
        if (d.count) SetWindowTextW(d.count, (std::to_wstring(d.shown.size()) + L" of " + std::to_wstring(d.trig.triggers.size())).c_str());
        InvalidateRect(d.list, nullptr, FALSE);
    }
    void selectTrigger(int i) override { ctSelectTrigger(i); }
    void updateHeader() override { ctUpdateHeader(); }
    void changed() override { ctChanged(); }
    void invalidate() override { InvalidateRect(gCt->list, nullptr, FALSE); }
    void message(const std::wstring& text) override { setMessage(text); }
    bool ctrl() override { return GetKeyState(VK_CONTROL) < 0; }
    bool shift() override { return GetKeyState(VK_SHIFT) < 0; }
};
CtWinModelHost gCtModelHost;
// The list's rows (the model's: core/views_classic_triggers.inc).
void ctFillList() { ctModelFill(*gCt, gCtModelHost); }

void ctBuildEditor();

// The selected trigger's row in view (the list's view scrolls to it).
void ctShowSelected() {
    ClassicTrigDialog& d = *gCt;
    const auto at = std::find(d.shown.begin(), d.shown.end(), d.sel);
    if (at == d.shown.end() || !d.listPane.view) return;
    d.listPane.view->revealShown(int(at - d.shown.begin()));
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
    CtHeadView& h = *d.headView;
    h.title.setText(t ? tr(L"Trigger ") + std::to_wstring(d.sel + 1) : std::wstring(L"None"));
    h.count.setText(!t ? std::wstring() : widen(std::string(t->disabled() ? "Turned off, " : "") + std::to_string(t->actionCount()) + (t->actionCount() == 1 ? " action" : " actions")));
    h.turnOff.setText(t && t->disabled() ? L"Turn on" : L"Turn off");
    for (stardraft::kit::Button* b : {&h.turnOff, &h.deleteTrig, &h.forces}) b->setEnabled(t != nullptr);
}

// Shows the selected trigger: its players and lists (the first condition selected).
void ctSelectTrigger(int index) {
    ClassicTrigDialog& d = *gCt;
    d.sel = index >= 0 && index < int(d.trig.triggers.size()) ? index : -1;
    d.collapsed.clear();
    const ClassicTrigger* t = ctTrigger();
    d.filling = true;
    for (int g = 0; g < kCtBoxCount; ++g) {
        d.headView->players[size_t(g)]->setOn(t && t->runsFor(kCtBoxGroups[g]));
        d.headView->players[size_t(g)]->setEnabled(t != nullptr);
    }
    ctUpdateHeader();
    d.filling = false;
    if (d.entryPane.view) d.entryPane.view->resetScroll();
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


// Is the value one of the picker's choices (as ctChoices has them)? No: an EUD value.


std::wstring ctChipText(const ClassicParam& p, uint32_t v, const ClassicNames& n);  // (the painted lists, below)
bool ctIsChoice(ClassicKind k, uint32_t v) { return ctIsChoiceOf(*app.ed, gCt->ai, k, v); }
std::wstring ctChipText(const ClassicParam& p, uint32_t v, const ClassicNames& n) {
    return ctChipTextOf(p, v, n, !ctPickerIsList(p.kind) || ctIsChoice(p.kind, v));
}
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
    if (parent >= 0 && t->actions[size_t(parent)].batch) s += tr(L" in For each ") + widen(t->actions[size_t(parent)].batch->name);
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
    const int rowH = scale(ui::kFieldH), step = scale(ui::kFormRowH), labelW = scale(110), colW = labelW + scale(260) + scale(24);
    const int pickW = scale(260);  // a value's field: the kit's width, the same in both columns
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
            const int w = text ? std::min(d.editRight - d.editLeft - labelW - scale(16), scale(560)) : pickW;
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

// The text pop-out (PRINCIPLES: values are edited in a pop-out with OK and Cancel): a text in the game text editor (its
// colors and align buttons). Cancel and a click outside leave the value as it was. The number pop-out is a kit dialog
// (core/views_classic_value.inc, ctValuePopout).
struct CtValueDialog {
    std::wstring title, label;
    std::string textValue;
    HWND textBox = nullptr;
};
constexpr int kCvText = 1102, kCvLabel = 1103;
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
        {  // the game text editor takes the place of the placeholder
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
        }
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
        st->textValue = gameTextGet(st->textBox);
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
    const short w = 330, h = 176;
    items.push_back({SS_LEFT, 8, 8, 310, 10, kCvLabel, 0x0082, L""});
    items.push_back({SS_LEFT, 8, 20, 310, 100, kCvText, 0x0082, L""});
    items.push_back({WS_TABSTOP | BS_DEFPUSHBUTTON, short(w - 116), short(h - 22), 50, 14, IDOK, 0x0080, L""});
    items.push_back({WS_TABSTOP | BS_PUSHBUTTON, short(w - 60), short(h - 22), 50, 14, IDCANCEL, 0x0080, L""});
    const std::vector<WORD> t = ctDialogTemplate(st.title, w, h, items);
    const INT_PTR r = DialogBoxIndirectParamW(app.inst, reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), app.main, ctValueProc, LPARAM(&st));
    gCtValueDlg = nullptr;
    return r == IDOK;
}

// The pop-out of a number or a text value of the selected entry (parameter i). OK sets the value. A number is a kit
// dialog (core/views_classic_value.inc, runValueDialog: the page's window owns it); a text is the game text editor.
void ctValuePopout(int i) {
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    const uint32_t v = c ? classicValue(*c, p.field) : classicValue(*a, p.field);
    if (p.kind != ClassicKind::Text) {
        std::wstring number;
        // The dialog is over the page's window, and the page waits until it closes.
        gMainAnchor.host.ownerWindow(gCt && gCt->wnd ? gCt->wnd : app.main);
        const int r = runValueDialog(gMainAnchor.view, ctKindLabel(p.kind, p.name), ctKindLabel(p.kind, p.name), std::to_wstring(v), number, gCtValueView);
        gMainAnchor.host.ownerWindow(app.main);
        if (r != kValueOk) return;
        ctSetValue(i, ctClampNumber(p, wcstoull(number.c_str(), nullptr, 10)));
        ctBuildEditor();
        return;
    }
    CtValueDialog st;
    st.title = ctKindLabel(p.kind, p.name);
    st.label = ctKindLabel(p.kind, p.name);
    st.textValue = gCt->strings.get(v);
    if (!ctValueRun(st)) return;
    ctSetText(i, st.textValue);
    ctBuildEditor();
}

// --- Triggers: add, duplicate, delete, move (the model: core/views_classic_triggers.inc) ---------------------------

void ctInsertTrigger(ClassicTrigger n) { ctModelInsertTrigger(*gCt, gCtModelHost, std::move(n)); }
ClassicTrigger ctFreshCopy(ClassicTrigger t) { return ctModelFreshCopy(*gCt, std::move(t)); }
void ctAddTrigger() { ctModelAddTrigger(*gCt, gCtModelHost); }
void ctDuplicateTrigger(int i) { ctModelDuplicateTrigger(*gCt, gCtModelHost, i); }
void ctDeleteTrigger(int i) { ctModelDeleteTrigger(*gCt, gCtModelHost, i); }
void ctMoveShown(int from, int to) { ctModelMoveShown(*gCt, gCtModelHost, from, to); }
// The triggers an action takes: the checked ones (Ctrl or Shift click), else the selected one. In the map's order.
std::vector<int> ctTargets() { return ctModelTargets(*gCt); }
// A click on a trigger's row (Ctrl, Shift: see ctModelClickTrigger).
void ctClickTrigger(int i) { ctModelClickTrigger(*gCt, gCtModelHost, i); }
void ctToggleTriggers() { ctModelToggleTriggers(*gCt, gCtModelHost); }
void ctDeleteTriggers() { ctModelDeleteTriggers(*gCt, gCtModelHost); }
void ctDuplicateTriggers() { ctModelDuplicateTriggers(*gCt, gCtModelHost); }
// Forces: the players of the forces (multi-pick: each force checked or not for the selected trigger).
void ctForcesClick() {
    ClassicTrigger* t = ctTrigger();
    if (!t) return;
    std::set<std::string> checked;
    std::vector<ListPickItem> items = ctForceItems(*t, ctNames(), checked);
    RECT rc;  // (the Forces button's place: the head's window, and the button in it)
    GetWindowRect(gCt->head, &rc);
    const draw::Rect f = gCt->headView->forces.frame();
    if (!showListPickerMulti(L"Search forces", items, checked, L"Set", POINT{rc.left + f.left, rc.top + f.bottom + scale(2)})) return;
    ctModelSetForces(*gCt, gCtModelHost, checked);
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
    if (gCtClip.kind == 2) {  // each after the one before it: the selected trigger, then the pasted ones (one undo step)
        d.hold++;
        for (size_t k = 0; k < gCtClip.triggers.size(); ++k) {
            ClassicTrigger n = gCtClip.triggers[k];
            const auto& texts = gCtClip.triggerTexts[k];
            for (size_t i = 0; i < n.actions.size() && i < texts.size(); ++i) ctResolveTexts(n.actions[i], texts[i]);
            ctInsertTrigger(ctFreshCopy(n));
        }
        d.hold--;
        d.marked.clear();
        ctChanged();
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
// When new blocks are offered (kClassicBatchesOffered), they come first in the actions' list (a section "Blocks"),
// then the actions (section "Actions").
std::vector<ListPickItem> ctAddItems(int kind) {
    std::vector<ListPickItem> items;
    const bool blocks = kind && kClassicBatchesOffered;
    if (blocks)
        for (const ClassicBlockKind& k : classicBlockKinds()) {
            ListPickItem it;
            it.label = tr(L"For each ") + widen(k.name), it.section = L"Blocks", it.value = "block:" + std::to_string(int(k.kind));
            items.push_back(std::move(it));
        }
    for (const ClassicDef* def : kind ? classicActionDefs() : classicConditionDefs()) {
        ListPickItem it;
        it.label = widen(def->name), it.value = std::to_string(def->type);
        if (blocks) it.section = L"Actions";
        items.push_back(std::move(it));
    }
    return items;
}

void ctAddClick(int kind, RECT link) {
    std::vector<ListPickItem> items = ctAddItems(kind);
    POINT pt{link.left, link.bottom + scale(2)};
    ClientToScreen(gCt->entries, &pt);
    std::string picked;
    if (!showListPicker(kind ? L"Search actions" : L"Search conditions", items, "", pt, picked) || picked.empty()) return;
    if (picked.rfind("block:", 0) == 0) return ctBatchClick(ClassicBatchKind(std::stoi(picked.substr(6))), -1);
    ctAddEntry(kind, uint8_t(std::stoi(picked)));
}

/// The right-click menus of the rows (the same commands as the rows' buttons and the keys). A right click on a row that
// is not checked selects it first (and the checks go).
// (the ids and the items: core/views_classic_triggers.inc)
bool ctCanMove(int i, bool in);  // (below)
void ctMoveRow(int i, bool in);
// The program's menu of the items (a separator for id 0; a disabled item is grayed).
HMENU ctMenu(const std::vector<CtMenuItem>& items) {
    HMENU m = CreatePopupMenu();
    for (const CtMenuItem& it : items) {
        if (!it.id) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        else AppendMenuW(m, MF_STRING | (it.enabled ? 0 : MF_GRAYED), UINT_PTR(it.id), it.text.c_str());
    }
    return m;
}
void ctTriggerMenu(POINT screen, int i) {
    ClassicTrigDialog& d = *gCt;
    if (!d.marked.count(i)) d.marked.clear(), ctSelectTrigger(i);
    const ClassicTrigger* t = ctTrigger();
    HMENU m = ctMenu(ctTriggerMenuItems(t && t->disabled(), gCtClip.kind == 2));
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
    CtEntryMenuState st;
    st.isAction = kind != 0, st.block = block, st.off = off;
    st.canMoveOut = kind && ctCanMove(i, false), st.canMoveIn = kind && ctCanMove(i, true);
    st.canPaste = gCtClip.kind == (kind ? 1 : 0);
    HMENU m = ctMenu(ctEntryMenuItems(st));
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
void ctToggleOne(int i) { ctModelToggleOne(*gCt, gCtModelHost, i); }
// Can the action move into the block before it (in), or out of its block (out)? A trial on a copy.
bool ctCanMove(int i, bool in) {
    const ClassicTrigger* t = ctTrigger();
    return t && ctCanMoveRow(*t, i, in);  // (core)
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

// The painted lists' program part (the views paint and take the mouse in core/views_classic_triggers.inc): the page's state
// and its commands, through the functions of this file.
RECT ctRectOf(draw::Rect r) { return {r.left, r.top, r.right, r.bottom}; }
class CtListProgram final : public CtListHost {
public:
    const std::vector<ClassicTrigger>& triggers() const override { return gCt->trig.triggers; }
    const std::vector<int>& shown() const override { return gCt->shown; }
    const ClassicTrigger* selectedTrigger() const override { return ctTrigger(); }
    int selectedIndex() const override { return gCt->sel; }
    bool marked(int i) const override { return gCt->marked.count(i) != 0; }
    int entryKind() const override { return gCt->kind; }
    int entryIndex() const override { return gCt->entry; }
    bool collapsed(int i) const override { return gCt->collapsed.count(i) != 0; }
    ClassicNames names() const override { return ctNames(); }
    std::wstring chipText(const ClassicParam& p, uint32_t v) override { return ctChipText(p, v, ctNames()); }
    std::wstring chipTip(const ClassicParam& p) override { return ctKindLabel(p.kind, p.name); }
    void clickTrigger(int i) override { ctClickTrigger(i); }
    void toggleTrigger(int i) override { ctToggleOne(i); }
    void duplicateTrigger(int i) override { ctDuplicateTrigger(i); }
    void deleteTrigger(int i) override { ctDeleteTrigger(i); }
    void moveShown(int from, int to) override { ctMoveShown(from, to); }
    void addTrigger() override { ctAddTrigger(); }
    void triggerMenu(int i, draw::Point at) override {
        POINT s{at.x, at.y};
        ClientToScreen(gCt->list, &s);
        ctTriggerMenu(s, i);
    }
    void selectEntry(int kind, int i) override { ctSelectEntry(kind, i); }
    void toggleEntry(int kind, int i) override { ctToggleEntry(kind, i); }
    void duplicateEntry(int kind, int i) override { ctDuplicateEntry(kind, i); }
    void deleteEntry(int kind, int i) override { ctDeleteEntry(kind, i); }
    void moveEntry(int kind, int from, int to) override { ctMoveEntry(kind, from, to); }
    void moveRow(int i, bool in) override { ctMoveRow(i, in); }
    void editBlock(int i) override {
        ctSelectEntry(1, i);
        ctBatchClick(ClassicBatchKind::Unit, i);
    }
    void entryMenu(int kind, int i, draw::Point at) override {
        POINT s{at.x, at.y};
        ClientToScreen(gCt->entries, &s);
        ctEntryMenu(s, kind, i);
    }
    void addClick(int kind, draw::Rect link) override { ctAddClick(kind, ctRectOf(link)); }
    void chipClick(int kind, int i, int pi, draw::Rect chip) override { ctChipClick(kind, i, pi, ctRectOf(chip)); }
    void toggleCollapsed(int i) override {
        if (gCt->collapsed.count(i)) gCt->collapsed.erase(i);
        else gCt->collapsed.insert(i);
        ctChanged();
    }
    void entriesLaid(int /*height*/) override {  // the entries' height changed: the editor follows it
        if (gCt && gCt->wnd) PostMessageW(gCt->wnd, WM_SIZE, 0, 0);
    }
};
CtListProgram gCtListProgram;

// The window procedure of the two lists: the list's view (core) takes the messages it paints and the mouse; the keys and
// the rest are the page's.
LRESULT CALLBACK ctListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicTrigDialog* d = gCt;
    if (!d || (hwnd != d->list && hwnd != d->entries)) return DefWindowProcW(hwnd, msg, wp, lp);
    const bool isList = hwnd == d->list;
    ClassicTrigDialog::Pane& pane = isList ? d->listPane : d->entryPane;
    if (!pane.host) {  // made with the list's window's first message
        pane.view = std::make_unique<CtListView>(gCtListProgram, isList);
        pane.host = std::make_unique<ui::Win32Host>(*pane.view);
    }
    LRESULT r = 0;
    if (pane.host->message(hwnd, msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN: ctKey(isList, wp); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Batches (R23): the variable and its values, in a dialog with OK and Cancel ------------------------------------

// The block dialog's program part (core/views_classic_batch.inc has the dialog): the values' names, the kind's values
// for the multi-pick, and the picker, which opens under the Add values button.
ClassicKind ctBatchKindOf(ClassicBatchKind k) { return classicBatchValueKind(k); }
class BatchProgram final : public BatchHost {
public:
    std::wstring valueText(ClassicBatchKind kind, uint32_t v) override { return widen(classicValueText(classicBatchValueKind(kind), v, ctNames())); }
    std::vector<std::pair<uint32_t, std::wstring>> offeredValues(ClassicBatchKind kind) override { return ctKindChoices(ctBatchKindOf(kind)); }
    bool pickValues(const std::vector<std::pair<uint32_t, std::wstring>>& offered, std::set<uint32_t>& checked, draw::Point at) override {
        std::vector<ListPickItem> items;
        for (const auto& [value, label] : offered) {
            ListPickItem it;
            it.label = label, it.value = std::to_string(value);
            items.push_back(std::move(it));
        }
        std::set<std::string> ch;
        for (uint32_t v : checked) ch.insert(std::to_string(v));
        POINT pt{at.x, at.y};
        if (HWND w = gCtBatchView ? ui::Win32Host::windowOf(*gCtBatchView) : nullptr) ClientToScreen(w, &pt);
        if (!showListPickerMulti(L"Search values", items, ch, L"Set", pt)) return false;
        checked.clear();
        for (const ListPickItem& it : items)
            if (ch.count(it.value)) checked.insert(uint32_t(std::stoul(it.value)));
        return true;
    }
};
BatchProgram gBatchProgram;

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
    // The dialog is over the page's window, and the page waits until it closes.
    gMainAnchor.host.ownerWindow(gCt && gCt->wnd ? gCt->wnd : app.main);
    const int r = runBatchDialog(gMainAnchor.view, gBatchProgram, b, editing, gCtBatchView);
    gMainAnchor.host.ownerWindow(app.main);
    if (r == kBatchRemove && editing) {
        t->unwrapAction(row);  // the block goes; its actions stay, one level out
        gCt->collapsed.clear();
        setMessage(L"Removed the block");
        ctSelectEntry(1, -1);
    } else if (r == kBatchOk && editing) {
        t->actions[size_t(row)].batch = std::make_shared<ClassicBatch>(b);
        t->pruneBlocks();
        setMessage(L"Changed the block");
    } else if (r == kBatchOk) {  // a new block goes into the selected block (as its last child), else at the end
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

// The head's player box (the box's index in the page's order) of the selected trigger: it runs for that player or not.
void ctPlayerBox(int box, bool on) { ctModelPlayerBox(*gCt, gCtModelHost, box, on); }

void ctCommand(int id, int code) {
    ClassicTrigDialog& d = *gCt;
    ClassicTrigger* t = ctTrigger();
    if (id == kCtFilter && code == CBN_SELCHANGE) {
        const LRESULT k = SendMessageW(d.filterBox, CB_GETCURSEL, 0, 0);
        d.filter = k < 0 ? -1 : int(SendMessageW(d.filterBox, CB_GETITEMDATA, WPARAM(k), 0));
        if (d.listPane.view) d.listPane.view->resetScroll();
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
        if (d.listPane.view) d.listPane.view->resetScroll();
        ctFillList();
        ctSelectTrigger(d.shown.empty() ? -1 : std::find(d.shown.begin(), d.shown.end(), d.sel) != d.shown.end() ? d.sel : d.shown[0]);
        return;
    }
    if (code != BN_CLICKED) return;
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
    // Right: the head (the header, the players and Forces: its window's place; its parts are in its coordinates, from 0).
    const int delW = scale(150), turnW = scale(96);
    const int headW = rightW - delW - turnW - 3 * gap;
    const int headH = scale(40) + 2 * step;
    place(d.head, x0, pad, rightW, headH);
    auto put = [](draw::View& v, int px_, int py, int w, int hh) { v.setFrame({px_, py, px_ + std::max(0, w), py + std::max(0, hh)}); };
    CtHeadView& hv = *d.headView;
    put(hv.title, 0, 0, std::min(headW, scale(150)), scale(28));
    put(hv.count, scale(160), scale(6), std::max(0, headW - scale(160)), rowH);
    put(hv.deleteTrig, rightW - delW, scale(2), delW, rowH);
    put(hv.turnOff, rightW - delW - gap - turnW, scale(2), turnW, rowH);
    // The players: check boxes in columns of a fixed width, from the left; All players and Forces in the next row.
    const int colW = scale(124);
    for (int g = 0; g < 8; ++g) put(*hv.players[size_t(g)], g * colW, scale(40), colW - gap, rowH);
    put(*hv.players[8], 0, scale(40) + step, colW - gap, rowH);
    put(hv.forces, colW, scale(40) + step, scale(120), rowH);
    y = pad + headH + gap;
    // The editor follows the entries: the entries take the height their rows need, up to the room left.
    const int editorH = ctEditorHeight(step, gap);
    const int room = H - pad - editorH - gap - y;
    const int entriesH = std::clamp(d.entryPane.view ? d.entryPane.view->contentHeight() : 0, scale(120), std::max(scale(120), room));
    place(d.entries, x0, y, rightW, entriesH);
    const int ey = y + entriesH + gap;
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
        // the lists' hosts go before their views (the host detaches from its view when it goes; a move assignment frees the view first)
        d->listPane.host.reset(), d->listPane.view.reset();
        d->entryPane.host.reset(), d->entryPane.view.reset();  // the lists' views and hosts of the page that went (new ones with the new windows)
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
        // The head: the kit's widgets in a view window (the trigger, its buttons, its players and Forces).
        {
            const ClassicNames n = ctNames();
            std::vector<std::wstring> labels;
            for (int g = 0; g < kCtBoxCount; ++g) labels.push_back(widen(classicPlayerName(uint32_t(kCtBoxGroups[g]), n)));
            CtHeadView::Callbacks cb;
            cb.player = [](int box, bool on) {
                ctPlayerBox(box, on);
                mapPageChanged();
            };
            cb.turnOff = [] {
                ctToggleTriggers();
                mapPageChanged();
            };
            cb.deleteTrig = [] {
                ctDeleteTriggers();
                mapPageChanged();
            };
            cb.forces = [] {
                ctForcesClick();
                mapPageChanged();
            };
            d->headView = std::make_unique<CtHeadView>(labels, std::move(cb));
            d->head = CreateWindowExW(WS_EX_CONTROLPARENT, ui::kViewClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, hwnd,
                                      nullptr, app.inst, d->headView.get());
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
    ctModelLoad(d, *app.ed);  // (the strings, the triggers and their batches as the page shows them)
    d.ai = loadClassicAiScripts(app.data);
}

// Something to apply: a trigger differs from the map's when the page read them. (A text typed in an action makes a
// string and changes the action, so the triggers tell it too.)
bool ctCopyChanged(const ClassicTrigDialog& d) { return ctModelChanged(d); }

// Apply: TRIG (and the new strings) as one undo step (Editor::setClassicTriggers). The values the page does not know
// (EUD values, the bytes after the last trigger, the flags) stay as they are.
bool ctApply(ClassicTrigDialog& d) {
    std::wstring message;
    if (ctModelApply(d, *app.ed, message) == kCtApplySet) {
        updateTitle();
        app.dirty = true;
    }
    setMessage(message);
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

// Drags row `from` of a painted list (0 the triggers, 1 the conditions, 2 the actions) by its grip to place `to`, as the
// mouse does (core: CtListView::dragRow). `shot`: a capture while the row is at the place (the drop line).
bool ctDragRow(HWND wnd, CtListView* view, int list, int from, int to, const fs::path& shot = {}) {
    RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    if (!view) return false;
    const bool done = view->dragRow(list, from, to, [wnd, shot] {
        if (shot.empty()) return;
        RedrawWindow(wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        capture(shot, GetParent(wnd));
    });
    for (int i = 0; i < 2; ++i) pump();
    return done;
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
        if (const HWND pick = gListPick ? ui::Win32Host::windowOf(*gListPick) : nullptr) capture(gCtShotFile, pick), PostMessageW(pick, WM_CLOSE, 0, 0);
        break;
    case CtShot::Dialog: {
        if (gCtValueDlg) capture(gCtShotFile, gCtValueDlg), PostMessageW(gCtValueDlg, WM_COMMAND, IDCANCEL, 0);
        else if (gCtValueView) {
            if (HWND w = ui::Win32Host::windowOf(*gCtValueView)) capture(gCtShotFile, w);
            gCtValueView->cancel();
        } else if (gCtBatchView) {
            if (HWND w = ui::Win32Host::windowOf(*gCtBatchView)) capture(gCtShotFile, w);
            gCtBatchView->cancel();
        }
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
        if (gBackground && mode == "text")  // the native text pop-out activates its window: not in a background run
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
        } else if (mode == "value" || (mode == "text" && !gBackground)) {  // a number (a Bring's count; a kit dialog) or a text (Display text message): OK and Cancel
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
        } else if (mode == "blockdlg") {  // the block dialog for a new block (For each unit): a kit dialog, also in a background run
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
            ctDragRow(gCt->entries, gCt->entryPane.view.get(), 2, 1, 0);  // Create unit first, by a drag
        } else if (mode == "move") {
            auto& v = gCt->trig.triggers;
            auto order = [&](const std::vector<ClassicTrigger>& want) { return v == want ? "yes" : "NO"; };
            if (v.size() < 3) {
                moveLog.push_back("move: NOT enough triggers (3 needed, " + std::to_string(v.size()) + ")");
            } else {
                // All triggers: the first one dragged to the third place.
                std::vector<ClassicTrigger> want = v;
                std::rotate(want.begin(), want.begin() + 1, want.begin() + 3);
                const bool dragged = ctDragRow(gCt->list, gCt->listPane.view.get(), 0, 0, 2, out / ("classic_triggers_drag" + light + ".png"));
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
                    const bool d2 = ctDragRow(gCt->list, gCt->listPane.view.get(), 0, 2, 0);
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
                    const bool d3 = ctDragRow(gCt->entries, gCt->entryPane.view.get(), 2, 0, 1);
                    moveLog.push_back("action: the first action of trigger " + std::to_string(withActions + 1) + " dragged under the second " +
                                      (d3 ? "" : "(drag NOT done) ") + order(want));
                }
                expected = v;
            }
        } else if (mode == "batch") {
            {  // New blocks are not offered while kClassicBatchesOffered is off; the map's blocks still work (below).
                const auto items = ctAddItems(1);
                const bool none = std::none_of(items.begin(), items.end(), [](const ListPickItem& it) { return it.value.rfind("block:", 0) == 0; });
                log << "batch: the action picker offers new blocks only when they are on " << (none != kClassicBatchesOffered ? "yes" : "NO") << "\n";
            }
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
        log << "state " << (mode.empty() ? std::string("page") : mode) << ": list scroll " << (gCt->listPane.view ? gCt->listPane.view->scrollPos() : 0) << ", entries scroll " << (gCt->entryPane.view ? gCt->entryPane.view->scrollPos() : 0) << ", list height "
            << (gCt->listPane.view ? gCt->listPane.view->contentHeight() : 0) << ", " << gCt->shown.size() << " triggers shown\n";
        if (set || move || batch) log << "Apply " << (mapPageClickButton(true) ? "clicked" : "NOT clicked") << "\n";
    }
    command(ID_WS_MAP);  // leave the page (with no changes left, the map is as Apply left it)
    for (int i = 0; i < 4; ++i) pump();
    if (!app.ed) return;
    if (!set && !move && !batch) {  // a state of the page changed the map (a trigger made, turned off): undo it back
        int steps = 0;
        while (steps < 30 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
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
    // The page makes an undo step for each change (the trigger, its player, each entry): undo back to the start.
    int steps = 0;
    while (steps < 30 && app.ed->chk().effective("TRIG") != before) app.ed->undo(), ++steps;
    log << "undo steps back to the map's TRIG byte for byte: " << steps << (app.ed->chk().effective("TRIG") == before ? " (back at the start)" : " (NOT back at the start)")
        << "\n";
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
