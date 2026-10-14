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
    kCtPlayer = 120,  // + one of kCtGroups
    kCtType = 160, kCtDisabled, kCtAlways,
    kCtBatch = 150,  // the batch button (Make a batch, Edit the batch)
    kCtParam = 200,  // + parameter
    kCtDim = 1,      // a dim label
    kCtHead = 3,     // a section heading (a line under it)
};
// A picker's value that is a block's variable: kCtVariable plus the block's depth (a field that holds it, not a value).
constexpr uint32_t kCtVariable = 0xFFFF0000u;
// The groups that have a check box (players 1-8, all players, forces 1-4); the others stay as they are.
constexpr int kCtGroups[] = {0, 1, 2, 3, 4, 5, 6, 7, 17, 18, 19, 20, 21};
constexpr int kCtGroupCount = int(std::size(kCtGroups));

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
    HWND players[kCtGroupCount]{};
    HWND editTitle = nullptr, typeBox = nullptr, disabled = nullptr, always = nullptr, batch = nullptr;
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
void mapPageChanged();

// Ctrl+C and Ctrl+V: a trigger, a condition or an action. The texts and sounds are kept as text, so a paste into
// another map gets that map's string ids.
struct ClassicTrigClip {
    int kind = -1;  // 0 condition, 1 action, 2 trigger
    ClassicCondition condition;
    ClassicAction action;
    ClassicTrigger trigger;
    std::vector<std::pair<std::string, std::string>> texts;  // per action: the text and the sound
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
void ctChanged() {
    if (!gCt) return;
    InvalidateRect(gCt->list, nullptr, FALSE);
    InvalidateRect(gCt->entries, nullptr, FALSE);
    mapPageChanged();
}

void ctFillList() {
    ClassicTrigDialog& d = *gCt;
    d.shown.clear();
    for (int i = 0; i < int(d.trig.triggers.size()); ++i)
        if (d.filter < 0 || d.trig.triggers[size_t(i)].runsFor(d.filter)) d.shown.push_back(i);
    InvalidateRect(d.list, nullptr, FALSE);
}

void ctBuildEditor();
void ctUpdateBatchButton();

int ctTrigRowH() { return scale(44); }
int ctEntryRowH() { return scale(30); }

// The selected trigger's row in view.
void ctShowSelected() {
    ClassicTrigDialog& d = *gCt;
    const auto at = std::find(d.shown.begin(), d.shown.end(), d.sel);
    if (at == d.shown.end() || !d.list) return;
    RECT rc;
    GetClientRect(d.list, &rc);
    const int top = int(at - d.shown.begin()) * ctTrigRowH(), h = ctTrigRowH();
    if (top < d.listScroll) d.listScroll = top;
    else if (top + h > d.listScroll + rc.bottom) d.listScroll = top + h - rc.bottom;
    InvalidateRect(d.list, nullptr, FALSE);
}

void ctSelectEntry(int kind, int entry) {
    ClassicTrigDialog& d = *gCt;
    d.kind = kind, d.entry = entry;
    ctUpdateBatchButton();
    ctBuildEditor();
    InvalidateRect(d.entries, nullptr, FALSE);
}

// The block button: "Add a block" for a trigger, "Edit the block" when a block's row is selected.
void ctUpdateBatchButton() {
    const ClassicTrigger* t = ctTrigger();
    const bool onBlock = t && gCt->kind == 1 && gCt->entry >= 0 && gCt->entry < t->actionCount() && t->actions[size_t(gCt->entry)].type == kClassicBlockType;
    SetWindowTextW(gCt->batch, onBlock ? L"Edit the block\x2026" : L"Add a block\x2026");
    EnableWindow(gCt->batch, t != nullptr);
}

// Shows the selected trigger: its players and lists (the first condition selected).
void ctSelectTrigger(int index) {
    ClassicTrigDialog& d = *gCt;
    d.sel = index >= 0 && index < int(d.trig.triggers.size()) ? index : -1;
    d.collapsed.clear();
    const ClassicTrigger* t = ctTrigger();
    d.filling = true;
    for (int g = 0; g < kCtGroupCount; ++g) {
        SendMessageW(d.players[g], BM_SETCHECK, t && t->runsFor(kCtGroups[g]) ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(d.players[g], t != nullptr);
    }
    ctUpdateBatchButton();
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

// The type, the check boxes and a picker for each value of the selected condition or action.
void ctBuildEditor() {
    ClassicTrigDialog& d = *gCt;
    for (HWND h : d.params) DestroyWindow(h);
    d.params.clear(), d.paramPickers.clear();
    ClassicCondition* c = ctCondition();
    ClassicAction* a = ctAction();
    const bool any = c || a;
    for (HWND h : {d.editTitle, d.typeBox, d.disabled}) ShowWindow(h, any ? SW_SHOW : SW_HIDE);
    ShowWindow(d.always, a && (a->type == 7 || a->type == 9) ? SW_SHOW : SW_HIDE);
    if (!any) return;
    d.filling = true;
    SetWindowTextW(d.editTitle, c ? (L"Condition " + std::to_wstring(d.entry + 1)).c_str() : (L"Action " + std::to_wstring(d.entry + 1)).c_str());
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
        if (text) {  // a game text editor: in-game text (the game's colors), lines with \r\n as StarEdit writes them
            GameTextSpec spec;
            spec.palette = TextPalette::Game, spec.multiline = true, spec.align = true, spec.crlf = true;
            const int w = d.editRight - d.editLeft - labelW - scale(16), th = gameTextHeight(spec, w, scale(78));
            h = gameTextCreate(d.wnd, kCtParam + i, {x + labelW, y, x + labelW + w, y + th}, spec);
            gameTextSet(h, d.strings.get(v));
            y += th - step;
        } else if (p.kind == ClassicKind::Unit) {  // a unit type: the searchable list (ctPickValue), as the chips
            std::wstring label;
            for (const auto& [value, text] : ctChoices(p.kind, v, p.field))
                if (value == v) label = text;
            h = CreateWindowExW(0, WC_BUTTONW, label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_LEFT, x + labelW, y, pickW, rowH, d.wnd,
                                HMENU(INT_PTR(kCtParam + i)), app.inst, nullptr);
            darkControl(h, L"DarkMode_Explorer");
        } else if (ctPickerIsList(p.kind)) {
            h = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x + labelW, y, pickW, scale(320), d.wnd,
                                HMENU(INT_PTR(kCtParam + i)), app.inst, nullptr);
            darkControl(h, L"DarkMode_CFD");
            int sel = -1;
            for (const auto& [value, label] : ctChoices(p.kind, v, p.field)) {
                const int k = int(SendMessageW(h, CB_ADDSTRING, 0, LPARAM(label.c_str())));
                SendMessageW(h, CB_SETITEMDATA, WPARAM(k), LPARAM(value));
                if (value == v && sel < 0) sel = k;
            }
            SendMessageW(h, CB_SETCURSEL, WPARAM(sel), 0);
        } else {
            h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, std::to_wstring(v).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                x + labelW, y, scale(120), rowH, d.wnd, HMENU(INT_PTR(kCtParam + i)), app.inst, nullptr);
            darkControl(h, L"DarkMode_CFD");
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

// A value of the selected entry changed in its picker.
void ctParamChanged(int i, HWND h) {
    ClassicTrigDialog& d = *gCt;
    ClassicCondition* c = ctCondition();
    ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def || i < 0 || i >= int(def->params.size())) return;
    const ClassicParam& p = def->params[size_t(i)];
    uint32_t v = 0;
    if (p.kind == ClassicKind::Text) {
        const std::string text = gameTextGet(h);  // (the map's texts break lines with \r\n as StarEdit writes them)
        const uint32_t old = a ? classicValue(*a, p.field) : 0;
        if (text.empty()) v = 0;
        else if (old && d.ownStrings.count(old)) d.strings.set(old, text), v = old;  // a text this window made: changed in place
        else {
            for (uint32_t id = 1; id <= d.strings.size() && !v; ++id)
                if (d.strings.get(id) == text) v = id;
            if (v) d.ownStrings.erase(v);  // a string with the same text: now shared, never changed in place
            else d.ownStrings.insert(v = d.strings.add(text));
        }
    } else if (ctPickerIsList(p.kind)) {
        const LRESULT k = SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (k < 0) return;
        v = uint32_t(SendMessageW(h, CB_GETITEMDATA, WPARAM(k), 0));
    } else {
        const std::wstring t = windowText(h);
        v = t.empty() ? 0 : uint32_t(std::min<unsigned long long>(wcstoull(t.c_str(), nullptr, 10), 0xFFFFFFFFull));
        if (p.kind == ClassicKind::Count) v = std::min<uint32_t>(v, 255);
        if (p.field == ClassicField::Unit) v = std::min<uint32_t>(v, 0xFFFF);
        v = std::min<uint32_t>(v, kCtVariable - 1);  // a typed value never reads as a variable (kCtVariable and up)
    }
    ctSetValue(i, v);
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
    if (kind) {
        const ClassicAction a = t->actions[size_t(i)];
        gCt->ownStrings.erase(a.text);  // shared now
        ctInsertAction(a, i + 1);
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
    if (trigger) {
        gCtClip = {};
        gCtClip.kind = 2, gCtClip.trigger = *t;
        for (const ClassicAction& a : t->actions) gCtClip.texts.push_back(ctActionTexts(a));
        return setMessage(L"Copied the trigger");
    }
    if (d.entry < 0 || d.entry >= ctEntryCount(*t, d.kind)) return;
    gCtClip = {};
    gCtClip.kind = d.kind;
    if (d.kind) gCtClip.action = t->actions[size_t(d.entry)], gCtClip.texts.push_back(ctActionTexts(gCtClip.action));
    else gCtClip.condition = t->conditions[size_t(d.entry)];
    setMessage(d.kind ? L"Copied the action" : L"Copied the condition");
}
void ctPaste() {
    ClassicTrigDialog& d = *gCt;
    if (gCtClip.kind == 2) {
        ClassicTrigger n = gCtClip.trigger;
        for (size_t i = 0; i < n.actions.size() && i < gCtClip.texts.size(); ++i) ctResolveTexts(n.actions[i], gCtClip.texts[i]);
        ctInsertTrigger(ctFreshCopy(n));
        return setMessage(L"Pasted the trigger");
    }
    const ClassicTrigger* t = ctTrigger();
    if (!t || gCtClip.kind < 0) return;
    const int at = d.kind == gCtClip.kind && d.entry >= 0 ? d.entry + 1 : -1;
    bool ok;
    if (gCtClip.kind) {
        ClassicAction a = gCtClip.action;
        if (!gCtClip.texts.empty()) ctResolveTexts(a, gCtClip.texts[0]);
        ok = ctInsertAction(a, at);
    } else {
        ok = ctInsertCondition(gCtClip.condition, at);
    }
    setMessage(!ok ? L"The list is full" : gCtClip.kind ? L"Pasted the action" : L"Pasted the condition");
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
    if (!ctPickerIsList(p.kind)) {
        if (pi < int(d.paramPickers.size()) && d.paramPickers[size_t(pi)]) {
            HWND box = p.kind == ClassicKind::Text ? gameTextFocusBox(d.paramPickers[size_t(pi)]) : d.paramPickers[size_t(pi)];  // (a text: its editor's box)
            SetFocus(box);
            SendMessageW(box, EM_SETSEL, 0, -1);
        }
        return;
    }
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
void ctAddClick(int kind, RECT link) {
    std::vector<ListPickItem> items;
    for (const ClassicDef* def : kind ? classicActionDefs() : classicConditionDefs()) {
        ListPickItem it;
        it.label = widen(def->name), it.value = std::to_string(def->type);
        items.push_back(std::move(it));
    }
    POINT pt{link.left, link.bottom + scale(2)};
    ClientToScreen(gCt->entries, &pt);
    std::string picked;
    if (!showListPicker(kind ? L"Search actions" : L"Search conditions", items, "", pt, picked) || picked.empty()) return;
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

// Paints the trigger list (list) or the conditions and actions (entries).
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
        const int rowH = ctTrigRowH(), acts = 2, textX = scale(22);
        if (d.shown.empty()) none(scale(10));
        for (int k = 0; k < int(d.shown.size()); ++k) {
            const int i = d.shown[size_t(k)];
            const RECT r{0, y, w, y + rowH};
            y += rowH;
            if (visible(r)) {
                const bool sel = i == d.sel;
                ui::listRow(mem, r, sel, ui::hot(r));
                hitsOut().push_back({r, nullptr, [i] {
                                         if (i != gCt->sel) ctSelectTrigger(i);
                                     },
                                     L"", IDC_ARROW, {}});
            }
            ui::reorderRow(mem, kCtListName, r, k, [](int from, int to) { ctMoveShown(from, to); });
            if (!visible(r)) continue;
            const bool sel = i == d.sel;
            const ClassicTrigger& t = d.trig.triggers[size_t(i)];
            const int right = r.right - scale(8) - scale(ui::kIconBox) * acts;
            RECT top{textX, r.top, right, (r.top + r.bottom) / 2 + scale(1)}, bottom{textX, top.bottom - scale(2), right, r.bottom};
            SetTextColor(mem, ui::rowText(sel));
            const int parts = ctPartsOf(t);  // a trigger too long for 64 slots is several triggers in the map
            const std::wstring split = parts > 1 ? L"Becomes " + std::to_wstring(parts) + L" triggers in the map. " : L"";
            const std::wstring head = std::to_wstring(i + 1) + L". " + split + ctTriggerLine(t, false);
            DrawTextW(mem, head.c_str(), -1, &top, DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
            SetTextColor(mem, kDim);
            std::wstring who;
            for (int g = 0; g < kClassicGroups; ++g)
                if (t.players[size_t(g)]) who += (who.empty() ? L"" : L", ") + widen(classicPlayerName(uint32_t(g), n));
            const std::wstring tail = (who.empty() ? L"No players" : who) + L": " + ctTriggerLine(t, true);
            DrawTextW(mem, tail.c_str(), -1, &bottom, DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
            ui::rowActions(mem, r,
                           {{UiIcon::Duplicate, L"Duplicate", [i] { ctDuplicateTrigger(i); }},
                            {UiIcon::Delete, L"Delete", [i] { ctDeleteTrigger(i); }, ui::Tone::Danger}},
                           sel);
        }
        ui::reorderDropLine(mem, kCtListName);
        y += scale(4);
        ui::addRow(mem, {scale(10), y, w - scale(10), y + scale(30)}, L"Add a trigger", [] { ctAddTrigger(); }, L"Add a trigger after the selected one");
        y += scale(30) + scale(8);
    } else {
        const ClassicTrigger* t = ctTrigger();
        FormLayout l{};
        l.x0 = 0, l.colRight = w, l.rowH = scale(ui::kRowH);
        for (int kind = 0; kind < 2; ++kind) {
            y = sectionHeader(mem, l, y, kind ? L"Actions" : L"Conditions");
            const int count = t ? ctEntryCount(*t, kind) : 0;
            if (!count && !t) none(scale(10));  // (a trigger: its add row says it)
            const int rowH = ctEntryRowH(), acts = 3;
            int hiddenEnd = -1;  // the children of a collapsed block are not shown: up to this row
            for (int i = 0; i < count; ++i) {
                if (kind && i < hiddenEnd) continue;
                hiddenEnd = -1;
                const bool isBlock = kind && t->actions[size_t(i)].type == kClassicBlockType;
                if (isBlock && d.collapsed.count(i)) hiddenEnd = t->blockEnd(i);
                const int indent = kind ? t->actions[size_t(i)].depth * scale(18) : 0;
                const RECT r{0, y, w, y + rowH};
                y += rowH;
                const bool sel = d.kind == kind && d.entry == i;
                if (visible(r)) {
                    ui::listRow(mem, r, sel, ui::hot(r));
                    hitsOut().push_back({r, nullptr, [kind, i] { ctSelectEntry(kind, i); }, L"", IDC_ARROW, {}});
                }
                ui::reorderRow(mem, kCtEntryName[kind], r, i, [kind](int from, int to) { ctMoveEntry(kind, from, to); });
                if (!visible(r)) continue;
                const uint8_t flags = kind ? t->actions[size_t(i)].flags : t->conditions[size_t(i)].flags;
                const bool off = (flags & 2) != 0;
                if (isBlock) {  // the chevron: expanded (down) or collapsed (right); a click shows or hides the children
                    const bool shut = d.collapsed.count(i) != 0;
                    RECT chev{scale(6) + indent, r.top, scale(6) + indent + scale(14), r.bottom};
                    SetTextColor(mem, ui::rowText(sel));
                    DrawTextW(mem, shut ? L"\x25B8" : L"\x25BE", -1, &chev, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
                    hitsOut().push_back({chev, nullptr, [i] {
                                             if (gCt->collapsed.count(i)) gCt->collapsed.erase(i);
                                             else gCt->collapsed.insert(i);
                                             ctChanged();
                                         },
                                         shut ? L"Show the actions of the block" : L"Hide the actions of the block", IDC_HAND, {}});
                }
                const RECT area{scale(22) + indent, r.top, r.right - scale(8) - scale(ui::kIconBox) * acts, r.bottom};
                if (kind) ctSentence(mem, t->actions[size_t(i)], 1, i, area, off, sel);
                else ctSentence(mem, t->conditions[size_t(i)], 0, i, area, off, sel);
                ui::rowActions(mem, r,
                               {{L"\xE7E8", off ? L"Turn on" : L"Turn off", [kind, i] { ctToggleEntry(kind, i); }, ui::Tone::Quiet, off},
                                {UiIcon::Duplicate, L"Duplicate", [kind, i] { ctDuplicateEntry(kind, i); }},
                                {UiIcon::Delete, L"Delete", [kind, i] { ctDeleteEntry(kind, i); }, ui::Tone::Danger}},
                               sel);
            }
            ui::reorderDropLine(mem, kCtEntryName[kind]);
            y += scale(4);
            if (t && count < (kind ? ctActionLimit(*t) : kClassicConditions)) {
                const RECT lr{scale(10), y, w - scale(10), y + scale(30)};
                ui::addRow(mem, lr, kind ? L"Add an action" : L"Add a condition", [kind, lr] { ctAddClick(kind, lr); });
            }
            y += scale(30) + scale(16);
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
    if (ctrl && vk == 'V') return ctPaste();
    if (isList) {
        const auto at = std::find(d.shown.begin(), d.shown.end(), d.sel);
        if (at == d.shown.end()) return;
        const int k = int(at - d.shown.begin());
        if (vk == VK_DELETE) return ctDeleteTrigger(d.sel);
        if (vk == VK_UP || vk == VK_DOWN) {
            const int to = k + (vk == VK_UP ? -1 : 1);
            if (to < 0 || to >= int(d.shown.size())) return;
            if (ctrl) ctMoveShown(k, to);
            else ctSelectTrigger(d.shown[size_t(to)]);
        }
        return;
    }
    const ClassicTrigger* t = ctTrigger();
    if (!t || d.entry < 0) return;
    if (vk == VK_DELETE) return ctDeleteEntry(d.kind, d.entry);
    if (ctrl && d.kind == 1 && (vk == VK_LEFT || vk == VK_RIGHT)) {  // Move in: into the block above; Move out: after the block
        ClassicTrigger* m = ctTrigger();
        const bool moved = vk == VK_RIGHT ? m->indentAction(d.entry) : m->outdentAction(d.entry);
        if (moved) {
            d.collapsed.clear();
            ctChanged();
            ctBuildEditor();
        }
        return;
    }
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

constexpr int kCtBatchName = 1001, kCtBatchKind = 1002, kCtBatchValues = 1003, kCtBatchRemove = 1004;

struct CtBatchDialog {
    ClassicBatch* batch = nullptr;
    bool hadBatch = false;
    ClassicBatchKind shown = ClassicBatchKind::Unit;  // the kind the list shows now (the default name follows it)
};

ClassicKind ctBatchKindOf(ClassicBatchKind k) { return classicBatchValueKind(k); }
// The name a new batch's variable gets: the kind's name.
std::string batchDefaultName(ClassicBatchKind k) { return k == ClassicBatchKind::Player ? "player" : k == ClassicBatchKind::Location ? "location" : "unit"; }
const char* ctBatchKindLabel(ClassicBatchKind k) {
    return k == ClassicBatchKind::Player ? "Player" : k == ClassicBatchKind::Location ? "Location" : "Unit type";
}

// The values a batch of this kind can have: the unit types of the game, the players and the map's locations.
void ctBatchFillValues(HWND dlg, ClassicBatchKind kind, const std::vector<uint32_t>& chosen) {
    HWND list = GetDlgItem(dlg, kCtBatchValues);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& [value, label] : ctKindChoices(ctBatchKindOf(kind))) {
        if (kind == ClassicBatchKind::Unit && value > 227) continue;  // (the groups of units are not one unit type)
        const int i = int(SendMessageW(list, LB_ADDSTRING, 0, LPARAM(label.c_str())));
        SendMessageW(list, LB_SETITEMDATA, WPARAM(i), LPARAM(value));
        if (std::find(chosen.begin(), chosen.end(), value) != chosen.end()) SendMessageW(list, LB_SETSEL, TRUE, i);
    }
    EnableWindow(GetDlgItem(dlg, IDOK), SendMessageW(list, LB_GETSELCOUNT, 0, 0) > 0);
}

// The dialog's answer: the kind, the name and the chosen values. False when no value is chosen (OK does nothing then).
bool ctBatchCollect(HWND dlg, CtBatchDialog& st) {
    HWND list = GetDlgItem(dlg, kCtBatchValues);
    const int n = int(SendMessageW(list, LB_GETSELCOUNT, 0, 0));
    if (n <= 0) return false;
    std::vector<int> picked(static_cast<size_t>(n));
    SendMessageW(list, LB_GETSELITEMS, WPARAM(n), LPARAM(picked.data()));
    HWND kindBox = GetDlgItem(dlg, kCtBatchKind);
    const ClassicBatchKind kind = ClassicBatchKind(SendMessageW(kindBox, CB_GETITEMDATA, WPARAM(SendMessageW(kindBox, CB_GETCURSEL, 0, 0)), 0));
    st.batch->values.clear();
    for (int i : picked) st.batch->values.push_back(uint32_t(SendMessageW(list, LB_GETITEMDATA, WPARAM(i), 0)));
    st.batch->kind = kind;
    st.batch->name = classicBatchName(narrow(windowText(GetDlgItem(dlg, kCtBatchName))), kind);
    return true;
}

INT_PTR CALLBACK ctBatchProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto state = [dlg] { return reinterpret_cast<CtBatchDialog*>(GetWindowLongPtrW(dlg, DWLP_USER)); };
    if (msg == WM_INITDIALOG) {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
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
        for (ClassicBatchKind k : {ClassicBatchKind::Unit, ClassicBatchKind::Player, ClassicBatchKind::Location}) {
            const int i = int(SendMessageW(kindBox, CB_ADDSTRING, 0, LPARAM(widen(ctBatchKindLabel(k)).c_str())));
            SendMessageW(kindBox, CB_SETITEMDATA, WPARAM(i), LPARAM(k));
            if (k == st->batch->kind) cur = i;
        }
        SendMessageW(kindBox, CB_SETCURSEL, WPARAM(cur), 0);
        ctBatchFillValues(dlg, st->batch->kind, st->batch->values);
        for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, WPARAM(app.font), TRUE);
        for (int id : {kCtBatchName, kCtBatchKind, kCtBatchValues}) darkControl(GetDlgItem(dlg, id), id == kCtBatchValues ? L"DarkMode_Explorer" : L"DarkMode_CFD");
        for (int id : {kCtBatchRemove, IDOK, IDCANCEL}) darkControl(GetDlgItem(dlg, id), L"DarkMode_Explorer");
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
        EnableWindow(GetDlgItem(dlg, IDOK), SendMessageW(GetDlgItem(dlg, kCtBatchValues), LB_GETSELCOUNT, 0, 0) > 0);
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
    t.push_back(9);  // items
    t.push_back(0), t.push_back(0), t.push_back(240), t.push_back(170);
    t.push_back(0), t.push_back(0);  // menu, class
    str(L"");                        // title (set later)
    t.push_back(9);
    str(L"Segoe UI");
    auto item = [&](DWORD style, short x, short y, short w, short h, WORD id, WORD cls, const wchar_t* text) {
        align();
        dword(style | WS_CHILD | WS_VISIBLE);
        dword(0);
        t.push_back(x), t.push_back(y), t.push_back(w), t.push_back(h), t.push_back(id);
        t.push_back(0xFFFF), t.push_back(cls);
        str(text);
        t.push_back(0);  // no creation data
    };
    item(SS_LEFT, 8, 11, 84, 10, 0xFFFF, 0x0082, L"Variable name");
    item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 96, 8, 136, 14, kCtBatchName, 0x0081, L"");
    item(SS_LEFT, 8, 31, 84, 10, 0xFFFF, 0x0082, L"Kind");
    item(WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 96, 28, 136, 80, kCtBatchKind, 0x0085, L"");
    item(SS_LEFT, 8, 51, 84, 10, 0xFFFF, 0x0082, L"Values");
    item(WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_MULTIPLESEL | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY, 96, 48, 136, 96, kCtBatchValues,
         0x0083, L"");
    item(WS_TABSTOP | BS_PUSHBUTTON, 8, 152, 110, 14, kCtBatchRemove, 0x0080, L"");
    item(WS_TABSTOP | BS_DEFPUSHBUTTON, 130, 152, 50, 14, IDOK, 0x0080, L"");
    item(WS_TABSTOP | BS_PUSHBUTTON, 184, 152, 50, 14, IDCANCEL, 0x0080, L"");
    align();
    return t;
}

INT_PTR ctBatchRun(CtBatchDialog& st) {
    const std::vector<WORD> t = ctBatchTemplate();
    return DialogBoxIndirectParamW(app.inst, reinterpret_cast<LPCDLGTEMPLATEW>(t.data()), app.main, ctBatchProc, LPARAM(&st));
}

// Make a batch from the selected trigger, or change the batch (its variable, its values). The batch is written when
// the window applies its changes.
void ctBatchClick() {
    ClassicTrigger* t = ctTrigger();
    if (!t) return;
    const int row = gCt->entry;
    const bool editing = gCt->kind == 1 && row >= 0 && row < t->actionCount() && t->actions[size_t(row)].type == kClassicBlockType;
    if (!editing && t->actionCount() >= kClassicActions) return setMessage(L"A trigger has at most 64 actions");
    ClassicBatch b = editing ? *t->actions[size_t(row)].batch : ClassicBatch{};
    if (!editing) b.name = batchDefaultName(b.kind);
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
    } else if (r == IDOK) {
        ClassicAction blk;
        blk.type = kClassicBlockType;
        blk.batch = std::make_shared<ClassicBatch>(b);
        t->addAction(blk, -1);
        setMessage(L"Added a block");
        ctSelectEntry(1, t->actionCount() - 1);
    } else {
        return;
    }
    ctFillList();
    ctBuildEditor();
    ctUpdateBatchButton();
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
        if (code == BN_CLICKED) {  // a unit type's button: the searchable list below it
            RECT rc;
            GetWindowRect(GetDlgItem(d.wnd, id), &rc);
            return ctPickValue(id - kCtParam, POINT{rc.left, rc.bottom + scale(2)});
        }
        if (code == CBN_SELCHANGE || code == EN_CHANGE) ctParamChanged(id - kCtParam, GetDlgItem(d.wnd, id));
        return;
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
    if (code != BN_CLICKED) return;
    if (id == kCtBatch) return ctBatchClick();
    if (id >= kCtPlayer && id < kCtPlayer + kCtGroupCount) {
        if (!t) return;
        const bool on = SendMessageW(GetDlgItem(d.wnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
        t->players[size_t(kCtGroups[id - kCtPlayer])] = on;
        ctFillList();  // it can leave (or join) the list of the player shown
        return;
    }
    if (id == kCtDisabled || id == kCtAlways) {
        const uint8_t bit = id == kCtDisabled ? 2 : 4;
        const bool on = SendMessageW(GetDlgItem(d.wnd, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
        uint8_t* flags = ctCondition() ? &ctCondition()->flags : ctAction() ? &ctAction()->flags : nullptr;
        if (flags) *flags = uint8_t(on ? *flags | bit : *flags & ~bit);
        ctChanged();
        return;
    }
}

LRESULT CALLBACK classicTrigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ClassicTrigDialog* d = gCt;
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        d->wnd = hwnd;
        d->params.clear(), d->paramPickers.clear();  // made again (the page showed before): the old ones went with it
        d->listScroll = d->entryScroll = 0;
        d->listSurface = {}, d->entrySurface = {};  // their tips belonged to the lists that went
        const int pad = scale(ui::kDialogPad), rowH = scale(ui::kFieldH), gap = scale(8), step = scale(ui::kFormRowH);  // fields 24, rows 32 apart
        const int listW = scale(420), x0 = pad + listW + scale(24), rightW = scale(820), right = x0 + rightW;
        auto font = [](HWND h, HFONT f = nullptr) { SendMessageW(h, WM_SETFONT, WPARAM(f ? f : app.font), TRUE); };
        auto label = [&](const wchar_t* s, int x, int y, int w, bool dim = true) {
            HWND h = CreateWindowExW(0, WC_STATICW, s, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS | SS_CENTERIMAGE, x, y, w, rowH, hwnd,
                                     HMENU(INT_PTR(dim ? kCtDim : 0)), app.inst, nullptr);
            font(h);
            return h;
        };
        auto check = [&](int id, const std::wstring& s, int x, int y, int w) {
            HWND h = CreateWindowExW(0, WC_BUTTONW, s.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, w, rowH, hwnd, HMENU(INT_PTR(id)),
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
        // Right: the players, the conditions and actions, the selected entry.
        int y = pad;
        SetWindowLongPtrW(label(L"Players", x0, y, rightW - scale(210), false), GWLP_ID, kCtHead);
        d->batch = CreateWindowExW(0, WC_BUTTONW, L"Add a block\x2026", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x0 + rightW - scale(200), y,
                                   scale(200), rowH, hwnd, HMENU(INT_PTR(kCtBatch)), app.inst, nullptr);
        font(d->batch);
        darkControl(d->batch, L"DarkMode_Explorer");
        y += rowH + scale(8);
        {
            const ClassicNames n = ctNames();
            for (int g = 0; g < kCtGroupCount; ++g) {
                const bool second = g >= 8;  // players 1-8, then all players and the forces
                // All players under Player 1; the forces from under Player 2, in the rest of the row (room for long force names).
                const int cw = rightW / 8, fw = (rightW - cw) / 4;
                const int x = !second ? x0 + g * cw : g == 8 ? x0 : x0 + cw + (g - 9) * fw, wdt = !second || g == 8 ? cw - gap : fw - gap;
                d->players[g] = check(kCtPlayer + g, widen(classicPlayerName(uint32_t(kCtGroups[g]), n)), x, y + (second ? step : 0), wdt);
            }
        }
        y += 2 * step + scale(8);
        const int entriesTop = y, entriesH = scale(440);
        y += entriesH + pad;
        d->editTitle = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_NOPREFIX | SS_CENTERIMAGE, x0, y, scale(200), scale(28), hwnd, nullptr, app.inst, nullptr);
        font(d->editTitle, ui::titleFont());
        d->typeBox = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x0 + scale(200), y + scale(2), scale(300), scale(400), hwnd,
                                     HMENU(INT_PTR(kCtType)), app.inst, nullptr);
        font(d->typeBox);
        darkControl(d->typeBox, L"DarkMode_CFD");
        d->disabled = check(kCtDisabled, L"Turn off", x0 + scale(520), y + scale(2), scale(110));
        d->always = check(kCtAlways, L"Always show", x0 + scale(640), y + scale(2), scale(140));
        d->editTop = y + step + gap, d->editLeft = x0, d->editRight = right;
        const int editBottom = d->editTop + 3 * step + scale(78);
        // Left: which triggers show, and the list.
        y = pad;
        label(L"Show the triggers of", pad, y, scale(150));
        d->filterBox = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, pad + scale(150), y,
                                       listW - scale(150), scale(320), hwnd, HMENU(INT_PTR(kCtFilter)), app.inst, nullptr);
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
        y += step;
        d->note = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_NOPREFIX, pad, y, listW, rowH * 2, hwnd, HMENU(INT_PTR(kCtDim)), app.inst, nullptr);
        font(d->note);
        const int listTop = y;
        d->list = painted(pad, listTop, listW, editBottom - listTop);
        d->entries = painted(x0, entriesTop, rightW, entriesH);
        // As big as its content (the page has the Apply and Revert bar).
        SetWindowPos(hwnd, nullptr, 0, 0, right + pad, editBottom + pad, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        // A note over the list: a project's map (L.4), EUD values.
        std::wstring note;
        if (projectMode()) note = L"The project's map has these classic triggers. The built map keeps them; they run with the project's triggers.";
        int eud = 0;
        for (const ClassicTrigger& t : d->trig.triggers) {
            for (int i = 0; i < t.conditionCount(); ++i) eud += classicEud(t.conditions[size_t(i)]);
            for (int i = 0; i < t.actionCount(); ++i) eud += classicEud(t.actions[size_t(i)]);
        }
        if (eud) note += (note.empty() ? L"" : L" ") + std::to_wstring(eud) + L" conditions and actions have EUD values. They stay as they are.";
        if (!note.empty()) {
            SetWindowTextW(d->note, note.c_str());
            ShowWindow(d->note, SW_SHOW);
            SetWindowPos(d->list, nullptr, pad, listTop + rowH * 2 + gap, listW, editBottom - listTop - rowH * 2 - gap, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ctFillList();
        ctSelectTrigger(d->shown.empty() ? -1 : d->shown[0]);
        return 0;
    }
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
// The picker of the selected entry's value with this name ("n", "unit", "text", ...).
HWND ctPicker(const char* name) {
    const ClassicCondition* c = ctCondition();
    const ClassicAction* a = ctAction();
    const ClassicDef* def = c ? classicConditionDef(c->type) : a ? classicActionDef(a->type) : nullptr;
    if (!def) return nullptr;
    for (size_t i = 0; i < def->params.size() && i < gCt->paramPickers.size(); ++i)
        if (std::string(def->params[i].name) == name) return gCt->paramPickers[i];
    return nullptr;
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
    const bool wasModified = app.ed && app.ed->modified();
    const Bytes before = app.ed ? app.ed->chk().effective("TRIG") : Bytes();
    int shownTriggers = -1;
    command(ID_SCEN_CLASSICTRIG);  // the page, in the main area
    if (gWorkspace == Workspace::ClassicTriggers && gCt) {
        const HWND w = mapPageWindow();
        for (int i = 0; i < 4; ++i) pump();
        shownTriggers = int(gCt->shown.size());
        const std::string light = gLightTheme ? "_light" : "";
        if (mode == "set") {
            ctPick(gCt->filterBox, 0);  // player 1's triggers
            ctAddTrigger();
            ctPick(gCt->typeBox, 3);  // Bring
            if (HWND h = ctPicker("n")) SetWindowTextW(h, L"5");
            if (HWND h = ctPicker("unit")) ctPick(h, 230);  // men
            ctAddEntry(1, 9);  // Display text message
            if (HWND h = ctPicker("text")) {  // as typing does (the editor tells the window)
                SetWindowTextW(gameTextFocusBox(h), L"Hello from StarDraft 2");
                SendMessageW(gCt->wnd, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), EN_CHANGE), LPARAM(h));
                RedrawWindow(app.main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                capture(dir / ("classic_triggers_text" + light + ".png"), app.main);  // the text's editor (the game's colors)
            }
            ctAddEntry(1, 9);
            ctPick(gCt->typeBox, 44);  // Create unit
            if (HWND h = ctPicker("unit")) ctPick(h, 37);  // Zergling
            if (HWND h = ctPicker("player")) ctPick(h, 0);
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
            madeCopy = *t;
            ctFillList();
            ctSelectEntry(1, 2);
            log << "nested batch: " << (t->actionCount() == 3 ? "yes" : "NO") << "\n";
            ctChanged();  // the blocks are an edit of the page: Apply writes them
        } else if (!gCt->shown.empty()) {
            ctSelectEntry(1, 0);
        }
        mapPageChanged();
        RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        for (int i = 0; i < 10; ++i) pump(), Sleep(20);
        capture(out / ("classic_triggers" + (mode.empty() ? std::string() : "_" + mode) + light + ".png"), app.main);
        if (set || move || batch) log << "Apply " << (mapPageClickButton(true) ? "clicked" : "NOT clicked") << "\n";
    }
    command(ID_WS_MAP);  // leave the page (with no changes left, the map is as Apply left it)
    for (int i = 0; i < 4; ++i) pump();
    if (!app.ed) return;
    const size_t count = ctTriggerCount(app.ed->chk().effective("TRIG")), had = ctTriggerCount(before);
    log << spec << ": the page " << (shownTriggers >= 0 ? "opened" : "did NOT open") << ", " << shownTriggers << " triggers in the list (" << had
        << " in the map)\n";
    if (batch) {
        // The map has the template and the three triggers the batch makes; read back, the batch is the same; undone.
        const ClassicTriggers saved = app.ed->classicTriggers();
        log << "the map has one trigger more (the blocks make six copies in it) " << (count == had + 1 ? "yes" : "NO") << "\n";
        // Each edit is one undo step (the page writes it into the map at once): the variable was the last one.
        const ClassicTriggers working = classicCollapseBatches(saved, app.ed->strings());
        const bool same = !working.triggers.empty() && working.triggers.back() == madeCopy;
        log << "read back: the blocks are the same (the outer unit block with the player block in it) " << (same ? "yes" : "NO") << "\n";
        std::string ext = app.ed->path().extension().string(), err;
        const fs::path file = dir / ("classictrig_batch" + (ext.empty() ? std::string(".scx") : ext));
        Editor back(app.data);
        const bool reopened = app.ed->save(file, err) && back.open(file, err);
        const ClassicTriggers reread = reopened ? classicCollapseBatches(back.classicTriggers(), back.strings()) : ClassicTriggers{};
        const bool again = !reread.triggers.empty() && reread.triggers.back() == madeCopy;
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
        log << "no Apply: no change " << (app.ed->modified() == wasModified && app.ed->chk().effective("TRIG") == before ? "yes" : "NO") << "\n";
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
