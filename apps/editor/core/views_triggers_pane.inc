// The Triggers workspace's pane and its info strip (core): the selected trigger's conditions and actions as blocks, with
// their values as chips, the heavy trigger's cost and its fixes, the checker's problems, a category's or folder's page
// and the variable form; under the pane, what the selected block does. The views (TrigPaneView, TrigInfoView) paint
// through the Canvas and keep their hit areas (gTP); clicks, keys and the mouse wheel are handled in the view, and what
// needs the program (the pickers, the pop-outs, the menus, saving) goes through TrigPaneHost (main.cpp: TrigPaneProgram).

// --- The workspace's state ------------------------------------------------------------------------------------------
// Left: the project's triggers as a tree: three categories (Definitions, On map start, On every trigger cycle)
// with folders, triggers and (in Definitions) variables. Right-click to add (also from templates), F2 to rename,
// Delete, drag to move, copy and paste. Middle: the selected trigger's conditions and actions as blocks with
// their values in color (blue: click to change; purple: no clear type yet). Below: what the selected block
// does, its pitfalls, and what the checker found. Saved to triggers.json in the project folder on every
// change; Build turns it into generated/triggers.eps.

struct TriggerEditor {
    HWND wnd = nullptr, tree = nullptr, pane = nullptr, info = nullptr, code = nullptr;  // (the Win32 windows; tree, pane, info: the views' windows)
    TriggerSet set;
    const EudProject* project = nullptr;
    const std::vector<TriggerCall>* textValuesFor = nullptr;  // the text pop-out's block's values from the game (its chips' names)
    std::vector<TriggerCall> textValuesOut;                   // ... and as the pop-out left them (OK)
    std::vector<int> sel;                 // selected node
    int scroll = 0, contentH = 0;
    std::vector<TriggerIssue> issues;  // from the last check
};
TriggerEditor gTE;

TriggerNode* trigSelectedNode() { return gTE.set.at(gTE.sel); }
TriggerNode* trigSelected() {
    TriggerNode* n = trigSelectedNode();
    return n && n->kind == TriggerNode::Kind::Trigger ? n : nullptr;
}

// --- The pane: one trigger -----------------------------------------------------------------------------------
// Conditions and Actions, each collapsible. Every block has a grip; drag it to reorder or into or out of a block
// that holds blocks (If, loops, After a delay, Any of). A list is addressed by its path: the trigger's
// conditions (0) or actions (1), or through (list, index) steps to a container, that container's list n (2 + n).

struct TrigListRef {
    std::vector<std::pair<int, int>> via;
    int list = 1;
    bool operator==(const TrigListRef& o) const { return via == o.via && list == o.list; }
};
struct TrigBlockRef {
    TrigListRef list;
    int index = -1;
};

std::vector<TriggerCall>* trigNodeList(TriggerNode& t, int list) { return list == 0 ? &t.conditions : list == 1 ? &t.actions : nullptr; }
constexpr int kTextValuesList = 1000;  // a TrigListRef's list: Show text's values from the game
std::vector<TriggerCall>* trigCallList(TriggerCall& c, int list) {
    if (list == kTextValuesList) return c.textValues();
    const int n = list - 2;
    return n >= 0 && n < int(c.lists.size()) ? &c.lists[size_t(n)] : nullptr;
}
TriggerCall* trigContainer(TriggerNode& t, const TrigListRef& r) {
    TriggerCall* call = nullptr;
    for (const auto& [l, i] : r.via) {
        std::vector<TriggerCall>* v = call ? trigCallList(*call, l) : trigNodeList(t, l);
        if (!v || i < 0 || i >= int(v->size())) return nullptr;
        call = &(*v)[size_t(i)];
    }
    return call;
}
std::vector<TriggerCall>* trigResolve(TriggerNode& t, const TrigListRef& r) {
    if (r.via.empty()) return trigNodeList(t, r.list);
    TriggerCall* c = trigContainer(t, r);
    return c ? trigCallList(*c, r.list) : nullptr;
}
bool trigListIsCondition(TriggerNode& t, const TrigListRef& r) {
    if (r.via.empty()) return r.list == 0;
    const TriggerCall* c = trigContainer(t, r);
    const TriggerDef* d = c ? TriggerDef::find(c->def) : nullptr;
    const int n = r.list - 2;
    return d && n >= 0 && n < int(d->lists.size()) && d->lists[size_t(n)].condition;
}
TriggerCall* trigBlock(TriggerNode& t, const TrigBlockRef& b) {
    std::vector<TriggerCall>* v = trigResolve(t, b.list);
    return v && b.index >= 0 && b.index < int(v->size()) ? &(*v)[size_t(b.index)] : nullptr;
}
TrigListRef trigInside(const TrigBlockRef& b, int n) {
    TrigListRef r = b.list;
    r.via.push_back({b.list.list, b.index});
    r.list = 2 + n;
    return r;
}
bool trigSameBlock(const TrigBlockRef& a, const TrigBlockRef& b) { return a.index >= 0 && a.index == b.index && a.list == b.list; }
// Is a list inside a block with an effect (from the block library: theunit, counter, curplayer)?
bool trigInsideEffect(TriggerNode& t, const TrigListRef& r, const char* effect) {
    TrigListRef walk;
    walk.list = r.via.empty() ? r.list : r.via[0].first;
    for (size_t i = 0; i < r.via.size(); ++i) {
        TrigBlockRef b{walk, r.via[i].second};
        if (const TriggerCall* c = trigBlock(t, b))
            if (const TriggerDef* d = TriggerDef::find(c->def); d && d->has(effect)) return true;
        walk.via.push_back(r.via[i]);
        walk.list = i + 1 < r.via.size() ? r.via[i + 1].first : r.list;
    }
    return false;
}
bool trigInUnitLoop(TriggerNode& t, const TrigListRef& r) { return trigInsideEffect(t, r, "theunit"); }

struct TrigLine {
    TrigBlockRef ref;  // index -1: an empty list's placeholder (a drop target)
    RECT r{}, grip{};  // the block (a container: its header row), its grip
    RECT whole{};      // a container: everything it holds
    bool container = false;
};
struct TrigParamHit {
    TrigBlockRef ref;
    int param = 0;
    RECT r{};
    // Inside value blocks: `param` is the block's value holding them; `inner` the values below it, down to this one
    // (empty: the block's own value). `bracket`: the value block filling that value itself.
    std::vector<int> inner;
    bool bracket = false;
    int token = -1;  // Show text's text: -2 the words (the text pop-out), 0.. a value in it (that part)
};
struct TrigFoldHit {
    TrigBlockRef ref;
    RECT r{};
};
struct TrigAddHit {  // "+ Add" under a list
    TrigListRef list;
    RECT r{};
};
struct TrigVarHit {  // a field of the variable form
    int field = 0;   // 0 kind, 1 size, 2 starting value, 3 description
    RECT r{};
};

bool trigSameHit(const TrigParamHit& a, const TrigParamHit& b) {
    return trigSameBlock(a.ref, b.ref) && a.param == b.param && a.inner == b.inner && a.bracket == b.bracket && a.token == b.token;
}

struct TrigRowButton {  // on the right of a row: 0 on/off, 1 duplicate, 2 delete
    TrigBlockRef ref;
    int action = 0;
    RECT r{};
};

// A category's color, for the stripe on a block's left edge.
COLORREF trigCategoryColor(const TriggerDef* d) {
    const COLORREF palette[] = {kAccent, kGood, RGB(220, 184, 104), RGB(214, 140, 200),
                                       RGB(96, 200, 210),  RGB(232, 140, 110), RGB(170, 150, 240), RGB(200, 200, 120)};
    if (!d) return RGB(90, 90, 96);
    size_t h = 0;
    for (char ch : d->category) h = h * 31 + static_cast<unsigned char>(ch);
    return palette[(h + (d->condition ? 3 : 0)) % 8];
}

struct TrigPaneState {
    std::vector<TrigLine> lines;
    std::vector<TrigParamHit> params;
    std::vector<TrigFoldHit> folds;
    std::vector<TrigAddHit> adds;
    std::vector<TrigVarHit> varHits;
    RECT heading[2]{};
    RECT title{};  // the trigger's or variable's name at the top (click to rename)
    std::vector<std::pair<RECT, int>> addButtons;  // a category or folder: New trigger (0), template (1), folder (2), variable (3)
    RECT codeToggle{};  // </>: the code pane
    bool collapsed[2] = {};
    TrigBlockRef sel;
    TrigParamHit focus, hover;  // the value Tab is on, the one under the mouse (ref.index -1: none)
    TrigBlockRef hoverRow;      // the block under the mouse
    std::vector<TrigRowButton> rowButtons;
    // Problems, shown when the mouse is on a block's mark or on a red value: where, what, and how bad.
    struct Tip {
        RECT r;
        std::wstring text;
        COLORREF color;
    };
    std::vector<Tip> tips;
    TrigBlockRef drag;
    bool dragging = false;
    int dragFromY = 0;
    TrigListRef dropList;
    int dropIdx = -1;
};
TrigPaneState gTP;

// Undo: the trigger set after each saved change; `at` is the one shown.
struct TrigHistory {
    std::vector<std::pair<TriggerSet, std::vector<int>>> steps;
    size_t at = 0;
    std::string last;  // the set's text at `at`, to see whether a save changed anything
};
TrigHistory gTrigHist;

void trigHistoryReset() {
    gTrigHist.steps = {{gTE.set, gTE.sel}}, gTrigHist.at = 0, gTrigHist.last = gTE.set.text();
}

void trigHistoryRecord() {
    if (gTrigHist.steps.empty()) return trigHistoryReset();
    std::string now = gTE.set.text();
    if (now == gTrigHist.last) return;
    gTrigHist.steps.resize(gTrigHist.at + 1);
    gTrigHist.steps.push_back({gTE.set, gTE.sel});
    if (gTrigHist.steps.size() > 200) gTrigHist.steps.erase(gTrigHist.steps.begin());
    gTrigHist.at = gTrigHist.steps.size() - 1, gTrigHist.last = std::move(now);
}

// One step back (or forward, `redo`): the set as it was, and in `sel` what was selected when it changed. The pane's
// selection goes. False when there is no step that way. (The program saves the file and shows the set: trigUndo.)
bool trigHistoryStep(bool redo, std::vector<int>& sel) {
    if (!gTE.project || (redo ? gTrigHist.at + 1 >= gTrigHist.steps.size() : gTrigHist.at == 0)) return false;
    sel = gTrigHist.steps[gTrigHist.at].second;
    gTrigHist.at = redo ? gTrigHist.at + 1 : gTrigHist.at - 1;
    if (redo) sel = gTrigHist.steps[gTrigHist.at].second;
    gTE.set = gTrigHist.steps[gTrigHist.at].first, gTrigHist.last = gTE.set.text();
    gTP.sel = {}, gTP.focus = {}, gTP.focus.ref.index = -1;
    return true;
}
bool trigCanUndo(bool redo) { return gTE.project && (redo ? gTrigHist.at + 1 < gTrigHist.steps.size() : gTrigHist.at > 0); }


// The fixes shown on the pane and where their Apply links are (painted with the trigger).
std::vector<std::pair<RECT, TriggerCostFix>> gTrigFixLinks;

// The docs strip shows only with a block selected.
int trigInfoHeight() {
    TriggerNode* t = trigSelected();
    return t && trigBlock(*t, gTP.sel) ? scale(110) : 0;
}

// --- What the pane asks of the program ------------------------------------------------------------------------------
class TrigPaneHost {
public:
    virtual ~TrigPaneHost() = default;
    // The program's data that the values' texts read.
    virtual void loadProjectData() = 0;                                     // the project's data edits (once)
    virtual std::wstring gameString(int id) = 0;                            // stat_txt.tbl string `id` (1-based), as the project has it
    virtual const std::vector<Button>& buttonSet(int set) = 0;              // a button set, as the project has it
    virtual draw::Image entryIcon(const std::string& dat, uint32_t v) = 0;  // a .dat entry's icon (empty: none)
    // The pane's windows.
    virtual void layoutCheck() = 0;  // the info strip shows or hides (the layout follows when it changed)
    virtual void infoChanged() = 0;  // the info strip paints again
    virtual void paintNow() = 0;     // the pane paints now (its hit areas are new)
    virtual void blockShown(const std::string& def) = 0;  // the info strip shows a block (the help page follows it)
    // What a click, a key or the menu does.
    virtual void toggleCode() = 0;                                    // </>: the code pane
    virtual void applyFix(TriggerCostFix fix) = 0;                    // a heavy trigger's Apply
    virtual void addFromPage(int action, RECT button) = 0;            // a category's or folder's button
    virtual void editTitle() = 0;                                     // the name at the top
    virtual void editVariable(TrigVarHit h) = 0;                      // a field of the variable form
    virtual void editValue(TrigParamHit h) = 0;                       // a value's pop-out (Tab goes on to the next)
    virtual void changed() = 0;                                       // the trigger changed: save, check, repaint
    virtual void duplicate() = 0;                                     // the selected block
    virtual void deleteBlock() = 0;                                   // the selected block
    virtual void moveBlock(int delta) = 0;                            // Alt+Up, Alt+Down
    virtual void addWithPicker(TrigListRef list, int at) = 0;         // the block picker, then the pick at `at` (-1: the end)
    virtual std::string pickBlock(bool condition, const std::wstring& search) = 0;  // the block picker ("": none)
    virtual void addPicked(bool condition, const std::string& id) = 0;  // a picked block where added blocks go
    virtual void valueBlockMenu(TrigParamHit h) = 0;                  // the menu of a value block
    virtual void menu(draw::Point pt) = 0;                            // the pane's menu (pt: the pane's coordinates)
    virtual void drop() = 0;                                          // a dragged block dropped where gTP.dropList and dropIdx say
    virtual void help() = 0;                                          // F1: the selected block's help page
    virtual void undo(bool redo) = 0;  // Ctrl+Z, Ctrl+Y (the core's history, trigHistoryStep; the program saves and shows the set)
};
// The program's host (set when the pane is made): the values' texts read the data through it.
TrigPaneHost* gTrigPaneHost = nullptr;

// --- The values' texts ---------------------------------------------------------------------------------------------

// A unit picked on the map, named for people: "Terran Command Center #12 (Player 1)" (#: its id on the map).
std::wstring trigPickedUnitName(const std::string& v) {
    const uint32_t serial = uint32_t(std::strtoul(v.c_str() + 1, nullptr, 10));
    if (const Editor* ed = appState().ed.get())
        for (const auto& u : ed->units())
            if (u.serial == serial && u.type < ed->unitTypes().size())  // "Terran Civilian #12 (Player 1)": #: its id on the map
                return widen(ed->unitTypes()[u.type].name) + L" #" + std::to_wstring(serial) + L" (Player " + std::to_wstring(u.owner + 1) + L")";
    return L"unit #" + std::to_wstring(serial) + L" (gone from the map)";
}

bool trigIsNumberText(const std::string& v) {
    return !v.empty() && std::all_of(v.begin(), v.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
}

std::wstring entryName(const std::string& dat, int v);
// A .dat entry by number: "Gauss Rifle (Normal)", or the number when it has no name.
std::wstring trigDatEntryText(const std::string& dat, const std::string& v) {
    if (!trigIsNumberText(v)) return widen(v);  // an expression
    const std::wstring name = entryName(dat, std::atoi(v.c_str()));
    return name.empty() ? L"#" + widen(v) : name;
}

std::wstring trigUnitGroupName(int id) {
    static const wchar_t* names[] = {L"Any unit", L"Men", L"Buildings", L"Factories"};
    return id >= 229 && id <= 232 ? names[id - 229] : L"";
}

// A per-player variable of the open triggers that holds `holds` (unit types, locations).
bool trigIsPerPlayer(const std::string& v, const char* holds) {
    const auto vars = gTE.set.variables();
    const auto it = vars.find(v);
    return it != vars.end() && it->second->variable.kind == TriggerVariable::Kind::PerPlayer && it->second->variable.holds == holds;
}

// How a value reads in a sentence (type: the value's type, which may follow a property).
std::wstring trigValueText(const std::string& type, const std::string& v) {
    const TriggerType* t = triggerType(type);
    const Editor* ed = appState().ed.get();
    if (!t) return v.empty() ? L"(" + widen(type) + L")" : widen(v);
    switch (t->picker) {
    case TriggerPicker::UnitType:
    case TriggerPicker::UnitFilter:
        if (trigIsNumberText(v)) {
            const int id = std::atoi(v.c_str());
            if (!trigUnitGroupName(id).empty()) return trigUnitGroupName(id);
            if (ed && id < int(ed->unitTypes().size())) return widen(ed->unitTypes()[uint16_t(id)].name);
            return L"unit type #" + widen(v);
        }
        return v.empty() ? L"(unit type)" : widen(v) + (trigIsPerPlayer(v, "UnitType") ? tr(L" (the current player's)") : L"");
    case TriggerPicker::Location:
        if (trigIsNumberText(v)) {
            const int n = std::atoi(v.c_str());
            if (ed && n >= 1 && n <= ed->locationCount()) return widen(ed->locationName(n - 1));
            return L"location " + widen(v);
        }
        return v.empty() ? L"(location)" : widen(v) + (trigIsPerPlayer(v, "Location") ? tr(L" (the current player's)") : L"");
    case TriggerPicker::Property:
        if (const TriggerProperty* pr = triggerProperty(t->of, v)) return widen(pr->label);
        return v.empty() ? L"(a property)" : widen(v);
    case TriggerPicker::DatEntry: return v.empty() ? L"(" + widen(t->label) + L")" : trigDatEntryText(t->of, v);
    case TriggerPicker::Choice:
        for (const auto& c : t->choices)
            if (c.code == v) return widen(c.label);
        return widen(v);
    case TriggerPicker::Switch: return L"Switch " + widen(v);
    case TriggerPicker::Text: return v.empty() ? L"(text)" : L"\"" + gameTextPlain(v) + L"\"";
    case TriggerPicker::Number: return type == "Count" && v == "0" ? L"all" : v.empty() ? L"0" : widen(v);
    case TriggerPicker::UnitRef: return v.empty() ? L"(a unit)" : !v.empty() && v[0] == '@' ? trigPickedUnitName(v) : widen(v);
    case TriggerPicker::Variable: return v.empty() ? L"(a variable)" : widen(v);
    case TriggerPicker::Sound: return v.empty() ? L"(a sound)" : widen(fs::path(v).filename().string());
    case TriggerPicker::Trigger: return v.empty() ? L"(a trigger)" : widen(v);
    case TriggerPicker::Code: {  // several lines: on one row, with a mark between them
        if (v.empty()) return L"(code)";
        std::wstring out;
        for (wchar_t ch : widen(v)) out += ch == L'\n' ? std::wstring(L"  \x21B5  ") : ch == L'\r' ? std::wstring() : std::wstring(1, ch);
        return out;
    }
    case TriggerPicker::Button: {
        Button b;
        if (!buttonFromText(v, b)) return L"(a button)";
        return buttonRowTextFrom(b, b.enabledString && gTrigPaneHost ? gTrigPaneHost->gameString(b.enabledString) : L"");
    }
    case TriggerPicker::Unclear: return v.empty() ? L"(" + widen(t->label) + L")" : widen(v);
    default: return v.empty() ? L"(" + widen(t->label) + L")" : widen(v);
    }
}

const COLORREF &kTrigBlue = kAccent, &kTrigRed = kBad, &kTrigAmber = kWarn;
// The trigger editor's own colors, for each theme: purple (events), a sentence's words, If / While blocks (a warm
// ground and line), the "condition" and "Else" labels inside them.
COLORREF trigPurple() { return gLightTheme ? RGB(128, 56, 200) : RGB(190, 120, 255); }
COLORREF trigWords() { return gLightTheme ? kText : RGB(176, 206, 236); }
COLORREF trigContainerFill() { return gLightTheme ? RGB(250, 245, 232) : RGB(46, 42, 32); }
COLORREF trigContainerLine() { return gLightTheme ? RGB(200, 170, 110) : RGB(120, 100, 60); }
COLORREF trigConditionLabel() { return gLightTheme ? RGB(140, 98, 0) : RGB(220, 184, 104); }
COLORREF trigElseLabel() { return gLightTheme ? RGB(178, 64, 44) : RGB(224, 140, 120); }

// The issues of one block (and of one of its values).
std::vector<const TriggerIssue*> trigIssuesOf(const TriggerCall* c, int param = -2) {
    std::vector<const TriggerIssue*> out;
    std::function<bool(const TriggerCall&, const TriggerCall*)> inside = [&](const TriggerCall& b, const TriggerCall* x) {
        for (const auto& slot : b.argBlocks)
            if (!slot.empty() && (&slot[0] == x || inside(slot[0], x))) return true;
        return false;
    };
    for (const auto& i : gTE.issues) {
        // A value's own problems, and (for the value's red brackets and the block's notes) those inside value blocks.
        const bool inSlot = param >= 0 && param < int(c->argBlocks.size()) && !c->argBlocks[size_t(param)].empty() &&
                            (i.call == &c->argBlocks[size_t(param)][0] || inside(c->argBlocks[size_t(param)][0], i.call));
        if ((i.call == c && (param == -2 || i.param == param)) || (param == -2 && inside(*c, i.call)) || inSlot) out.push_back(&i);
    }
    return out;
}

// The list a block's index is into (nullptr: none picked).
const TriggerNode* trigIndexList(const TriggerDef& d, const TriggerCall& c) {
    const int li = d.param("list");
    if (li < 0 || li >= int(c.args.size())) return nullptr;
    const auto vars = gTE.set.variables();
    const auto it = vars.find(c.args[size_t(li)]);
    return it != vars.end() ? it->second : nullptr;
}

// How an index reads: for a player array, the player ("Player 3", not 2).
std::wstring trigIndexText(const TriggerDef& d, const TriggerCall& c, const std::string& v) {
    const TriggerNode* list = trigIndexList(d, c);
    if (list && list->variable.kind == TriggerVariable::Kind::PerPlayer) {
        if (trigIsNumberText(v) && std::atoi(v.c_str()) < 8) return L"Player " + std::to_wstring(std::atoi(v.c_str()) + 1);
        if (v == "current player") return L"the current player";
    }
    return v.empty() ? L"0" : widen(v);
}

// A game text by its words (the first line, short), or the expression.
std::wstring trigGameStringText(const std::string& v) {
    if (!trigIsNumberText(v)) return v.empty() ? L"(game text)" : widen(v);
    if (!gTrigPaneHost) return L"text " + widen(v);
    gTrigPaneHost->loadProjectData();
    std::wstring t = gTrigPaneHost->gameString(std::atoi(v.c_str()));
    if (const size_t nl = t.find(L'\n'); nl != std::wstring::npos) t.resize(nl);
    if (t.size() > 32) t = t.substr(0, 31) + L"\x2026";
    return t.empty() ? L"text " + widen(v) : L"\x201C" + t + L"\x201D";
}

// A button of the block's set, as the Buttons tab lists it (its icon in the chip); past the last one, a new one.
std::wstring trigButtonIndexText(const TriggerDef& d, const TriggerCall& c, const std::string& v, draw::Image* icon) {
    const int sp = d.param("set");
    const std::string set = sp >= 0 && sp < int(c.args.size()) ? c.args[size_t(sp)] : "";
    if (!trigIsNumberText(v) || !trigIsNumberText(set) || std::atoi(set.c_str()) >= ButtonSets::kSets || !gTrigPaneHost)
        return v.empty() ? L"(button)" : L"button " + widen(v);
    gTrigPaneHost->loadProjectData();
    const auto& bs = gTrigPaneHost->buttonSet(std::atoi(set.c_str()));
    const size_t i = size_t(std::atoi(v.c_str()));
    if (i >= bs.size()) return L"a new button";
    if (icon) *icon = gTrigPaneHost->entryIcon("icons", bs[i].icon);
    std::wstring t = bs[i].enabledString ? gTrigPaneHost->gameString(bs[i].enabledString) : L"";
    if (const size_t nl = t.find(L'\n'); nl != std::wstring::npos) t.resize(nl);
    return t.empty() ? L"button " + widen(v) : t;
}

// A value's format after its words ("" for a number): " · name", " · color", ...
std::wstring textFormatSuffix(const std::string& f) {
    static const std::map<std::string, std::wstring> names = {{"n", L"name"}, {"c", L"color"}, {"x", L"hex"}, {"X", L"HEX"}, {"s", L"text"}, {"t", L"text (EPD)"}};
    const auto it = names.find(f);
    return it == names.end() ? L"" : L" \x00B7 " + it->second;
}
// How a value in a text reads: "current player's name" for name(...):s, a text by its name, the others with their format.
std::wstring textChipLabel(const std::string& text, const std::string& format) {
    if (format == "s" && text.rfind("name(", 0) == 0 && text.back() == ')') return widen(text.substr(5, text.size() - 6)) + L"'s name";
    if (format == "s" && text.rfind("tbl(", 0) == 0 && text.back() == ')') {  // a game text: its words (the number when it is a value)
        const std::string n = text.substr(4, text.size() - 5);
        if (trigIsNumberText(n)) {
            if (gTrigPaneHost) gTrigPaneHost->loadProjectData();
            const std::wstring words = entryName("strings", std::atoi(n.c_str()));
            if (!words.empty()) return L"\x201C" + (words.size() > 32 ? words.substr(0, 31) + L"\x2026" : words) + L"\x201D";
        }
        return L"game text " + widen(n);
    }
    if (format == "s") return widen(text);
    return widen(text) + textFormatSuffix(format);
}

// The checker's messages for a block (or one of its values), one a line; the color of the worst.
std::wstring trigIssueText(const std::vector<const TriggerIssue*>& issues, COLORREF& color) {
    std::wstring t;
    int worst = 3;
    for (const TriggerIssue* i : issues) {
        const int rank = i->level == TriggerIssue::Level::Error ? 0 : i->level == TriggerIssue::Level::Warning ? 1 : 2;
        worst = std::min(worst, rank);
        t += (t.empty() ? L"" : L"\n") + widen(i->message);
    }
    color = worst == 0 ? kTrigRed : worst == 1 ? kTrigAmber : trigPurple();
    return t;
}

const COLORREF& kTrigValue = kGood;  // a value from the game, in a sentence
// Values inside values: each depth its own color, so where one ends is clear.
COLORREF trigValueColor(int depth) {
    const COLORREF colors[] = {kGood, RGB(96, 200, 210), RGB(220, 184, 104), RGB(214, 140, 200)};
    return colors[size_t(std::max(0, depth - 1)) % 4];
}

// --- The pane's hit areas ----------------------------------------------------------------------------------------

// The values of a block in Tab's order.
std::vector<TrigParamHit> trigTabOrder(const TrigBlockRef& block) {
    std::vector<TrigParamHit> out;
    for (const auto& h : gTP.params)
        if (trigSameBlock(h.ref, block) && !h.bracket) out.push_back(h);
    return out;
}

// The innermost block under the point (a container's own header counts as it).
const TrigLine* trigLineAt(POINT pt) {
    for (auto it = gTP.lines.rbegin(); it != gTP.lines.rend(); ++it)
        if (it->ref.index >= 0 && PtInRect(&it->r, pt)) return &*it;
    for (auto it = gTP.lines.rbegin(); it != gTP.lines.rend(); ++it)
        if (it->ref.index >= 0 && it->container && PtInRect(&it->whole, pt)) return &*it;
    return nullptr;
}

// Does `inner` lie inside the block `outer`?
bool trigWithin(const TrigListRef& inner, const TrigBlockRef& outer) {
    std::vector<std::pair<int, int>> path = outer.list.via;
    path.push_back({outer.list.list, outer.index});
    return inner.via.size() >= path.size() && std::equal(path.begin(), path.end(), inner.via.begin());
}

// Where a dragged block would land: before the block under the mouse (its upper half) or after it, in a list of
// the same kind (conditions in condition lists, actions in action lists), never inside itself. True: it moved.
bool trigDropTarget(int y) {
    TriggerNode* t = trigSelected();
    if (!t) return false;
    const bool cond = trigListIsCondition(*t, gTP.drag.list);
    TrigListRef list;
    int idx = -1;
    for (const auto& l : gTP.lines) {
        if (trigListIsCondition(*t, l.ref.list) != cond || trigWithin(l.ref.list, gTP.drag)) continue;
        if (y < l.r.top - scale(4) || y > l.r.bottom + scale(4)) continue;
        list = l.ref.list;
        idx = l.ref.index < 0 ? 0 : y < (l.r.top + l.r.bottom) / 2 ? l.ref.index : l.ref.index + 1;
    }
    if (list == gTP.dropList && idx == gTP.dropIdx) return false;
    gTP.dropList = list, gTP.dropIdx = idx;
    return true;
}

// --- The block docs (the info strip and the block picker) -----------------------------------------------------------
// A block's title (bold), what it does and its pitfalls (amber), in `r`.
void drawBlockDocs(draw::Canvas& c, const TriggerDef* d, draw::Rect r) {
    if (!d) return;
    const draw::Font of = c.setFont(c.font({draw::Face::Ui, 0, true}));
    c.text(r, widen(d->title), kText, 0);
    c.setFont(c.font({draw::Face::Ui}));
    r.top += scale(22);
    const std::wstring text = widen(d->docs);
    const draw::Point sz = c.measureBox(text, r.width(), draw::kWrap);
    c.text({r.left, r.top, r.right, r.top + sz.y}, text, kText, draw::kWrap);
    r.top += sz.y + scale(6);
    if (!d->pitfalls.empty()) c.text(r, L"\x26A0 " + widen(d->pitfalls), kWarn, draw::kWrap);
    c.setFont(of);
}

// --- The pane's view ---------------------------------------------------------------------------------------------
class TrigPaneView final : public draw::View {
public:
    explicit TrigPaneView(TrigPaneHost& host) : host_(host) { gTrigPaneHost = &host; }

    // The pane at its zoom (Ctrl+mouse wheel): sizes (scale) and the UI font grow or shrink while it paints. Its hit
    // areas are recorded as it paints, so clicks follow.
    void paint(draw::Canvas& c) override {
        const int zoom = gDock.triggerZoom;
        AppState& st = appState();
        const UINT dpi = st.dpi;
        uiLf_ = shellUiFontLfHeight();
        draw::Font of{};
        if (zoom != 100) {
            uiLf_ = MulDiv(uiLf_, zoom, 100);
            st.dpi = UINT(MulDiv(int(dpi), zoom, 100));
            of = c.setFont(c.font({draw::Face::Ui, -uiLf_}));
        }
        paintAt(c);
        st.dpi = dpi;
        if (of) c.setFont(of);
    }

    bool mouse(const draw::MouseEvent& e) override {
        const POINT pt{e.pt.x, e.pt.y};
        switch (e.kind) {
        case draw::MouseKind::Down:
            if (e.button == 0) return press(pt), true;
            return false;
        case draw::MouseKind::DoubleClick:
            if (e.button == 0) return doubleClick(pt), true;
            return false;
        case draw::MouseKind::Move: return move(pt), true;
        case draw::MouseKind::Up:
            if (e.button == 0) {
                pressed_ = false;
                if (draw::Host* h = host()) h->capture(*this, false);
                if (gTP.dragging) host_.drop();
                gTP.dragging = false, gTP.drag = {}, gTP.dropIdx = -1;
                invalidate();
                return true;
            }
            if (e.button == 1) return rightUp(pt), true;
            return false;
        case draw::MouseKind::Leave:
            pressed_ = false;
            gTP.hover = {}, gTP.hover.ref.index = -1, gTP.hoverRow = {};
            invalidate();
            return true;
        case draw::MouseKind::Wheel: {
            if (e.horizontal) return false;
            const int delta = int(std::lround(e.wheel * 120));
            if (e.mods & draw::kCtrl) {  // Ctrl+wheel: bigger or smaller
                const int z = std::clamp(gDock.triggerZoom + (delta > 0 ? 10 : -10), 70, 200);
                if (z != gDock.triggerZoom) gDock.triggerZoom = z, saveDock(), invalidate();
                setMessage(L"Zoom " + std::to_wstring(z) + L"% (Ctrl+0 resets it)");
                return true;
            }
            scrollTo(gTE.scroll - delta * scale(60) / 120);
            return true;
        }
        }
        return false;
    }

    bool key(const draw::KeyEvent& e) override {
        if (!e.down) return false;
        using draw::Key;
        const bool alt = (e.mods & draw::kAlt) != 0;
        if (alt) {
            if (e.key == Key::Up || e.key == Key::Down) return host_.moveBlock(e.key == Key::Up ? -1 : 1), true;
            return false;
        }
        if (e.key == Key::Tab || e.key == Key::Enter) {
            TriggerNode* t = trigSelected();
            if (!t || !trigBlock(*t, gTP.sel)) return true;
            host_.paintNow();
            const auto order = trigTabOrder(gTP.sel);
            if (order.empty()) return true;
            int at = -1;
            for (size_t i = 0; i < order.size(); ++i)
                if (trigSameHit(order[i], gTP.focus)) at = int(i);
            const bool back = e.key == Key::Tab && (e.mods & draw::kShift);
            const int next = e.key == Key::Enter ? std::max(0, at) : at < 0 ? (back ? int(order.size()) - 1 : 0) : at + (back ? -1 : 1);
            if (next >= 0 && next < int(order.size())) host_.editValue(order[size_t(next)]);
            return true;
        }
        if (e.key == Key::Up || e.key == Key::Down) {
            std::vector<TrigBlockRef> blocks;
            for (const auto& l : gTP.lines)
                if (l.ref.index >= 0) blocks.push_back(l.ref);
            std::stable_sort(blocks.begin(), blocks.end(), [](const TrigBlockRef& a, const TrigBlockRef& b) {
                RECT ra{}, rb{};
                for (const auto& l : gTP.lines) {
                    if (trigSameBlock(l.ref, a)) ra = l.r;
                    if (trigSameBlock(l.ref, b)) rb = l.r;
                }
                return ra.top < rb.top;
            });
            int at = -1;
            for (size_t i = 0; i < blocks.size(); ++i)
                if (trigSameBlock(blocks[i], gTP.sel)) at = int(i);
            const int next = std::clamp(at < 0 ? 0 : at + (e.key == Key::Down ? 1 : -1), 0, std::max(0, int(blocks.size()) - 1));
            if (!blocks.empty()) gTP.sel = blocks[size_t(next)], gTP.focus = {}, gTP.focus.ref.index = -1;
            invalidate();
            host_.infoChanged();
            return true;
        }
        if (e.key == Key::Escape) {
            gTP.focus = {}, gTP.focus.ref.index = -1;
            invalidate();
            return true;
        }
        if (e.key == Key::Delete) return host_.deleteBlock(), true;
        if ((e.mods & draw::kCtrl) && (e.key == Key('Z') || e.key == Key('Y'))) {  // (the Windows menu's accelerators take these first)
            host_.undo(e.key == Key('Y') || (e.mods & draw::kShift));
            return true;
        }
        if (e.key == Key::F1) return host_.help(), true;
        return false;
    }

    // Typing adds a block: the picker opens searching for what was typed.
    bool text(const draw::TextEvent& e) override {
        const std::wstring w = widen(e.utf8);
        const draw::Host* h = host();
        if (w.size() != 1 || !iswalnum(w[0]) || (h && (h->modifiers() & draw::kCtrl))) return false;
        TriggerNode* t = trigSelected();
        if (!t) return true;
        const bool cond = trigBlock(*t, gTP.sel) && trigListIsCondition(*t, gTP.sel.list);
        const std::string id = host_.pickBlock(cond, w);
        if (!id.empty()) host_.addPicked(cond, id);
        return true;
    }

    draw::Cursor cursor(draw::Point p) override {
        const POINT pt{p.x, p.y};
        if (gTP.dragging) return draw::Cursor::SizeAll;
        auto on = [&](const auto& hits) { return std::any_of(hits.begin(), hits.end(), [&](const auto& h) { return PtInRect(&h.r, pt) != 0; }); };
        if (on(gTP.params) || on(gTP.folds) || on(gTP.adds) || on(gTP.varHits) || on(gTP.rowButtons)) return draw::Cursor::Hand;
        if (PtInRect(&gTP.title, pt) || PtInRect(&gTP.codeToggle, pt)) return draw::Cursor::Hand;
        for (const auto& b : gTP.addButtons)
            if (PtInRect(&b.first, pt)) return draw::Cursor::Hand;
        for (const auto& l : gTP.lines)
            if (l.ref.index >= 0 && PtInRect(&l.grip, pt)) return draw::Cursor::SizeAll;
        for (const auto& h : gTP.heading)
            if (PtInRect(&h, pt)) return draw::Cursor::Hand;
        return draw::Cursor::Arrow;
    }

    // The window's scroll bar: the content's height, the pane's and the position.
    bool vscroll(int code, int /*pos*/, int track, int page) override {
        int s = gTE.scroll;
        switch (code) {  // the SB_* requests
        case 0: s -= scale(30); break;  // SB_LINEUP
        case 1: s += scale(30); break;  // SB_LINEDOWN
        case 2: s -= page; break;       // SB_PAGEUP
        case 3: s += page; break;       // SB_PAGEDOWN
        case 4:                         // SB_THUMBPOSITION
        case 5: s = track; break;       // SB_THUMBTRACK
        case 6: s = 0; break;           // SB_TOP
        case 7: s = 1 << 30; break;     // SB_BOTTOM
        default: return true;
        }
        scrollTo(s);
        return true;
    }
    bool windowScrollBar(int& range, int& page, int& pos) override {
        range = std::max(0, gTE.contentH - 1), page = height(), pos = gTE.scroll;
        return true;
    }
    void captureLost() override { pressed_ = false; }

    void scrollTo(int pos) {
        gTE.scroll = std::clamp(pos, 0, std::max(0, gTE.contentH - height()));
        invalidate();
    }

private:
    TrigPaneHost& host_;
    int uiLf_ = -12;           // the UI font's height (LOGFONT) at the pane's zoom
    draw::Color lastText_ = 0;  // the color of the last text drawn (a container's chevron takes it, as it did on the DC)
    bool pressed_ = false;     // the left button is down (since a press on the pane)
    bool overTitle_ = false;   // the mouse on the name at the top
    int overAdd_ = -1;         // the category page's button under the mouse
    int overTip_ = -1;         // the tip under the mouse

    draw::Point mousePt() const {
        const draw::Host* h = host();
        return h ? h->mouse(*this) : draw::Point{-1, -1};
    }
    bool hot(RECT r) const {
        const draw::Point m = mousePt();
        const POINT p{m.x, m.y};
        return PtInRect(&r, p) != 0;
    }
    void text(draw::Canvas& c, draw::Rect r, std::wstring_view t, draw::Color col, unsigned flags) {
        c.text(r, t, col, flags);
        lastText_ = col;
    }
    void glyph(draw::Canvas& c, draw::Rect r, kitpaint::Ico g, draw::Color col, int size = stardraft::kit::kGlyph) {
        kitpaint::drawGlyph(c, r, g, col, size);
        if (g.icon < 0) lastText_ = col;
    }

    // --- Clicks ---
    void press(POINT pt) {
        focus();  // Ctrl+C / X / V act on blocks while the pane has focus
        pressed_ = true;
        // A click: the value chip shown as focused is no longer (a click on a value focuses that one; GitHub feedback #119).
        if (gTP.focus.ref.index >= 0) gTP.focus = {}, gTP.focus.ref.index = -1, invalidate();
        if (PtInRect(&gTP.codeToggle, pt)) return host_.toggleCode();
        for (const auto& [ar, fix] : gTrigFixLinks)
            if (PtInRect(&ar, pt)) {
                const TriggerCostFix f = fix;  // (applying repaints, which clears the links)
                return host_.applyFix(f);
            }
        for (const auto& [br, action] : gTP.addButtons)
            if (PtInRect(&br, pt)) {
                const RECT b = br;
                return host_.addFromPage(action, b);
            }
        if (PtInRect(&gTP.title, pt)) return host_.editTitle();
        for (const auto& h : gTP.varHits)
            if (PtInRect(&h.r, pt)) return host_.editVariable(h);
        TriggerNode* t = trigSelected();
        if (!t) return;
        for (const auto& p : gTP.params)
            if (PtInRect(&p.r, pt)) {
                const TrigParamHit hit = p;
                gTP.sel = hit.ref;
                invalidate();
                host_.infoChanged();
                host_.paintNow();
                for (const auto& q : gTP.params)  // the hit again: the repaint laid the block out anew
                    if (trigSameHit(q, hit)) return host_.editValue(q);
                return;
            }
        for (const auto& b : gTP.rowButtons)
            if (PtInRect(&b.r, pt)) {
                const TrigRowButton btn = b;
                gTP.sel = btn.ref;
                if (btn.action == 0) {
                    if (TriggerCall* c = trigBlock(*t, btn.ref)) c->enabled = !c->enabled, host_.changed();
                } else if (btn.action == 1) {
                    host_.duplicate();
                } else {
                    host_.deleteBlock();
                }
                return;
            }
        for (const auto& f : gTP.folds)
            if (PtInRect(&f.r, pt)) {
                if (TriggerCall* c = trigBlock(*t, f.ref)) c->folded = !c->folded, host_.changed();
                return;
            }
        for (const auto& a : gTP.adds)
            if (PtInRect(&a.r, pt)) return host_.addWithPicker(a.list, -1);
        for (int sec = 0; sec < 2; ++sec)
            if (PtInRect(&gTP.heading[sec], pt)) {
                gTP.collapsed[sec] = !gTP.collapsed[sec];
                invalidate();
                return;
            }
        const TrigLine* l = trigLineAt(pt);
        gTP.sel = l ? l->ref : TrigBlockRef{};
        if (l) {
            gTP.drag = l->ref, gTP.dragFromY = pt.y;
            if (draw::Host* h = host()) h->capture(*this, true);
        }
        invalidate();
        host_.infoChanged();
    }

    // Double-click empty space: add an action. On a condition or an action: replace it with one picked (one undo step).
    void doubleClick(POINT pt) {
        TriggerNode* t = trigSelected();
        if (!t) return;
        const TrigLine* l = trigLineAt(pt);
        if (!l) return host_.addWithPicker({{}, 1}, -1);
        if (l->ref.index < 0 || !PtInRect(&l->r, pt)) return;  // on the block's row only (a container: its header, not its body)
        auto on = [&](const auto& hits) { return std::any_of(hits.begin(), hits.end(), [&](const auto& h) { return PtInRect(&h.r, pt); }); };
        if (on(gTP.rowButtons) || on(gTP.folds) || on(gTP.params)) return;  // its buttons, fold, values: their own clicks
        if (l->container) return;  // an If or a loop: its blocks would go (replace the blocks in it instead)
        const TrigBlockRef ref = l->ref;
        const std::string id = host_.pickBlock(trigListIsCondition(*t, ref.list), {});
        TriggerNode* now = trigSelected();  // (the picker ran a message loop)
        const TriggerDef* def = id.empty() ? nullptr : TriggerDef::find(id);
        if (!now || !def) return;
        if (TriggerCall* c = trigBlock(*now, ref)) {
            *c = TriggerCall::make(*def);
            gTP.sel = ref;
            host_.changed();
        }
    }

    void move(POINT pt) {
        if (!pressed_) {
            if ((PtInRect(&gTP.title, pt) != 0) != overTitle_) overTitle_ = !overTitle_, invalidate();
            int nowAdd = -1;
            for (size_t i = 0; i < gTP.addButtons.size(); ++i)
                if (PtInRect(&gTP.addButtons[i].first, pt)) nowAdd = int(i);
            if (nowAdd != overAdd_) overAdd_ = nowAdd, invalidate();
            int nowTip = -1;
            for (size_t i = 0; i < gTP.tips.size(); ++i)
                if (PtInRect(&gTP.tips[i].r, pt)) nowTip = int(i);
            if (nowTip != overTip_) overTip_ = nowTip, invalidate();
            TrigParamHit over{};
            over.ref.index = -1;
            for (const auto& h : gTP.params)
                if (PtInRect(&h.r, pt)) over = h;
            const TrigLine* row = trigLineAt(pt);
            const TrigBlockRef overRow = row ? row->ref : TrigBlockRef{};
            if (!trigSameBlock(overRow, gTP.hoverRow) && !(overRow.index < 0 && gTP.hoverRow.index < 0)) gTP.hoverRow = overRow, invalidate();
            if (!trigSameHit(over, gTP.hover)) gTP.hover = over, invalidate();
        }
        if (gTP.drag.index >= 0 && pressed_) {
            if (!gTP.dragging && std::abs(pt.y - gTP.dragFromY) > scale(5)) gTP.dragging = true;
            if (gTP.dragging) {
                if (trigDropTarget(pt.y)) invalidate();
                if (draw::Host* h = host()) h->setCursor(draw::Cursor::SizeAll);
            }
        }
    }

    void rightUp(POINT pt) {
        for (const auto& h : gTP.params)
            if (PtInRect(&h.r, pt)) {
                // A value block itself, or a value inside one: the menu of the innermost value block around it.
                TrigParamHit vb = h;
                if (!h.bracket) {
                    if (h.inner.empty()) break;
                    vb.inner.pop_back(), vb.bracket = true;
                }
                return host_.valueBlockMenu(vb);
            }
        if (const TrigLine* l = trigLineAt(pt)) gTP.sel = l->ref;
        invalidate();
        host_.menu({pt.x, pt.y});
    }

    // --- Painting ---
    // Lays out (and draws, with `paintIt`) a block's sentence from x0 within width; values are links. Returns the height
    // used.
    int sentence(draw::Canvas& c, const TriggerDef& d, const TriggerCall& call, int x0, int y0, int width, const TrigBlockRef& ref, bool dim, bool record,
                 bool paintIt) {
        const int lineH = scale(22);
        int x = x0, y = y0;
        // A word. A value is a chip (a rounded field, lighter under the mouse, outlined when Tab is on it); a value
        // block's brackets are links without a chip.
        draw::Image chipIcon;                            // set for the next chip: drawn before its text
        std::vector<const TriggerIssue*> chipIssues;    // set for the next chip (or value block): its problems
        TrigBlockRef curRef = ref;                      // the block the next hits belong to (a value in a text: that value)
        int putToken = -1;                              // the next hit's part of a text with values
        int topDepth = 0;                               // the depth whose values are the block's own (a value in a text: 1)
        auto put = [&](const std::wstring& word, COLORREF col, bool link, int param, const std::vector<int>& inner, bool bracket) {
            const int cx = c.measure(word);
            const bool chip = link && !bracket;
            const draw::Image icon = chip ? chipIcon : draw::Image{};
            chipIcon = {};
            const std::vector<const TriggerIssue*> issues = link ? std::exchange(chipIssues, {}) : std::vector<const TriggerIssue*>{};
            const int iconW = icon ? scale(20) : 0;
            const bool glyphMark = bracket && (word == L"\x27E8" || word == L"\x27E9");  // a value block's bracket: room to click it
            const int padX = chip ? scale(5) : glyphMark ? scale(4) : 0, w = cx + 2 * padX + iconW;
            if (x > x0 && x + w > x0 + width) x = x0, y += lineH;
            RECT r{x, y, x + w, y + lineH};
            if (dim) col = kFaint;
            if (paintIt) {
                if (chip) {
                    const TrigParamHit me{curRef, param, r, inner, bracket, putToken};
                    const bool hotChip = trigSameHit(gTP.hover, me), focused = trigSameHit(gTP.focus, me);
                    RECT cr{r.left, r.top + scale(2), r.right, r.bottom - scale(2)};
                    const bool bad = !dim && col == kTrigRed;  // a value to fix: a red field
                    kitpaint::drawChipFace(c, cr, hotChip, dim, bad, focused);
                }
                if (icon) c.fitted(icon, {r.left + padX - scale(1), r.top + scale(3), r.left + padX + scale(17), r.bottom - scale(3)});
                const RECT tr{r.left + padX + iconW, r.top, r.right - padX, r.bottom};
                text(c, tr, word, col, draw::kMiddle);
                if (link && bracket && trigSameHit(gTP.hover, TrigParamHit{curRef, param, r, inner, bracket, putToken}))  // a value block under the mouse: underlined
                    c.line(tr.left, r.bottom - scale(4), tr.left + cx, r.bottom - scale(4), col);
            }
            if (link && record) gTP.params.push_back({curRef, param, r, inner, bracket, putToken});
            if (record && !issues.empty() && !dim) {
                COLORREF tc;
                const std::wstring tt = trigIssueText(issues, tc);
                gTP.tips.push_back({r, tt, tc});
            }
            x += w + (chip ? scale(2) : 0);
        };
        auto linkColor = [&](const TriggerDef& dd, const TriggerCall& cc, int p) {
            bool error = false;
            const bool unclear = !triggerTypeClear(triggerParamType(dd, cc.args, p));
            for (const TriggerIssue* i : trigIssuesOf(&cc, p)) error |= i->level == TriggerIssue::Level::Error;
            return unclear ? trigPurple() : error ? kTrigRed : kTrigBlue;
        };
        // Writes a block's sentence. A value block (depth > 0) fills value `top` of the block, below it `path`.
        const std::vector<int> none;
        std::function<void(const TriggerDef&, const TriggerCall&, int, const std::vector<int>&, int)> words =
            [&](const TriggerDef& dd, const TriggerCall& cc, int top, const std::vector<int>& path, int depth) {
            // A condition or action reads as a sentence: its first letter capitalized (values inside it stay as they are).
            std::string s = dd.shownSentence(cc.args);
            if (depth == 0 && !s.empty() && s[0] != '{') s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
            const COLORREF plain = depth > 0 ? trigValueColor(depth) : kText;
            std::wstring word;
            // A value block's own words open its menu (another value, or a number or variable instead), like its brackets.
            auto flush = [&] {
                if (word.empty()) return;
                if (depth > topDepth) put(word, plain, true, top, path, true);
                else put(word, plain, false, -1, none, false);
                word.clear();
            };
            for (size_t i = 0; i < s.size();) {
                if (s[i] == '{') {
                    const size_t end = s.find('}', i);
                    const int p = end == std::string::npos ? -1 : dd.param(s.substr(i + 1, end - i - 1));
                    if (p >= 0) {
                        flush();
                        // This value's place: the block's own (top = p), or one level below the value block's.
                        const int hitTop = depth == topDepth ? p : top;
                        std::vector<int> hitPath = path;
                        if (depth > topDepth) hitPath.push_back(p);
                        if (p < int(cc.argBlocks.size()) && !cc.argBlocks[size_t(p)].empty()) {
                            const TriggerCall& vc = cc.argBlocks[size_t(p)][0];
                            bool error = false;
                            for (const auto* is : trigIssuesOf(&cc, p)) error |= is->level == TriggerIssue::Level::Error;
                            const COLORREF bracket = error ? kTrigRed : trigValueColor(depth + 1);
                            if (error) chipIssues = trigIssuesOf(&cc, p);
                            put(L"\x27E8", bracket, true, hitTop, hitPath, true);
                            if (const TriggerDef* vd = TriggerDef::find(vc.def)) words(*vd, vc, hitTop, hitPath, depth + 1);
                            else put(L"unknown value " + widen(vc.def), kTrigRed, false, -1, none, false);
                            put(L"\x27E9", bracket, true, hitTop, hitPath, true);
                        } else if (depth == 0 && dd.params[size_t(p)].type == "TextValues") {
                            // A text with values: its words (click: the text pop-out), each value a chip, each value from
                            // the game in brackets with its own values as chips.
                            const std::string v = p < int(cc.args.size()) ? cc.args[size_t(p)] : "";
                            const auto parts = triggerTextParts(v);
                            const auto* gameValues = cc.textValues();
                            const COLORREF wordsColor = trigWords();
                            bool textError = false;
                            for (const auto* is : trigIssuesOf(&cc, p)) textError |= is->level == TriggerIssue::Level::Error;
                            if (parts.empty()) putToken = -2, put(L"(text)", textError ? kTrigRed : kTrigBlue, true, hitTop, hitPath, true);
                            for (size_t k = 0; k <= parts.size(); ++k) {
                                if (k == parts.size()) {  // the end: a pencil (the text pop-out), even when it is only values
                                    if (!parts.empty()) putToken = -2, put(L"\x270E ", textError ? kTrigRed : kTrigBlue, true, hitTop, hitPath, true);
                                    break;
                                }
                                const TriggerTextPart& part = parts[k];
                                if (part.kind == TriggerTextPart::Kind::Words) {
                                    std::wstring w;
                                    for (wchar_t ch : widen(part.text)) {
                                        w += ch;
                                        if (ch == L' ') putToken = -2, put(w, textError ? kTrigRed : wordsColor, true, hitTop, hitPath, true), w.clear();
                                    }
                                    if (!w.empty()) putToken = -2, put(w, textError ? kTrigRed : wordsColor, true, hitTop, hitPath, true);
                                    continue;
                                }
                                if (part.kind == TriggerTextPart::Kind::Expression) {
                                    putToken = int(k);
                                    chipIssues = trigIssuesOf(&cc, p);
                                    put(textChipLabel(part.text, part.format), textError ? kTrigRed : kTrigBlue, true, hitTop, hitPath, false);
                                    continue;
                                }
                                const TriggerCall* vc = gameValues && part.value >= 0 && part.value < int(gameValues->size()) ? &(*gameValues)[size_t(part.value)] : nullptr;
                                const TriggerDef* vd = vc ? TriggerDef::find(vc->def) : nullptr;
                                putToken = int(k);
                                put(L"\x27E8", trigValueColor(1), true, hitTop, hitPath, true);
                                if (vd) {
                                    TrigListRef lr = ref.list;
                                    lr.via.push_back({ref.list.list, ref.index});
                                    lr.list = kTextValuesList;
                                    const TrigBlockRef saved = curRef;
                                    curRef = {lr, part.value}, topDepth = 1, putToken = -1;
                                    words(*vd, *vc, -1, none, 1);
                                    curRef = saved, topDepth = 0;
                                } else {
                                    putToken = -1;
                                    put(L"missing value", kTrigRed, false, -1, none, false);
                                }
                                if (!part.format.empty()) putToken = -1, put(textFormatSuffix(part.format).substr(1), trigValueColor(1), false, -1, none, false);
                                putToken = int(k);
                                put(L"\x27E9", trigValueColor(1), true, hitTop, hitPath, true);
                            }
                            putToken = -1;
                        } else {
                            const std::string v = p < int(cc.args.size()) ? cc.args[size_t(p)] : "";
                            const std::string vtype = triggerParamType(dd, cc.args, p);
                            const std::wstring label = dd.params[size_t(p)].type == "Index"         ? trigIndexText(dd, cc, v)
                                                       : dd.params[size_t(p)].type == "ButtonIndex" ? trigButtonIndexText(dd, cc, v, &chipIcon)
                                                       : dd.params[size_t(p)].type == "GameString"  ? trigGameStringText(v)
                                                                                                    : trigValueText(vtype, v);
                            if (const TriggerType* tt = triggerType(vtype); tt && trigIsNumberText(v) && v.size() <= 9 && gTrigPaneHost) {  // the entry's icon in the chip (a longer number is no entry: no icon)
                                if (tt->picker == TriggerPicker::UnitType || tt->picker == TriggerPicker::UnitFilter) chipIcon = gTrigPaneHost->entryIcon("units", uint32_t(std::atoi(v.c_str())));
                                else if (tt->picker == TriggerPicker::DatEntry) chipIcon = gTrigPaneHost->entryIcon(tt->of, uint32_t(std::atoi(v.c_str())));
                            }
                            if (const TriggerType* tt = triggerType(vtype); tt && tt->picker == TriggerPicker::Button && gTrigPaneHost) {  // a button: its icon
                                Button bb;
                                if (buttonFromText(v, bb)) chipIcon = gTrigPaneHost->entryIcon("icons", bb.icon);
                            }
                            chipIssues = trigIssuesOf(&cc, p);
                            put(label, linkColor(dd, cc, p), true, hitTop, hitPath, false);
                        }
                        i = end + 1;
                        continue;
                    }
                }
                word += wchar_t(static_cast<unsigned char>(s[i]));
                if (s[i] == ' ') flush();
                ++i;
            }
            flush();
        };
        words(d, call, -1, none, 0);
        return y + lineH - y0;
    }

    // Centered on the first line of the sentence (9 px of room above it, lines 22 px high; the grip is 12 px high).
    static void grip(draw::Canvas& c, RECT block, bool on) { kitpaint::drawGrip(c, block.left + scale(7), block.top + scale(9 + 11 - 6), on); }
    static void insertMark(draw::Canvas& c, int x, int w, int y) { kitpaint::drawDropLine(c, x, x + w, y - scale(2)); }
    static void roundRect(draw::Canvas& c, RECT r, COLORREF fill, COLORREF line) { c.roundBox(r, fill, line, float(scale(8)) / 2); }

    // "Add a condition" / "Add an action" under a list: a row as wide as the blocks (the kit's add row).
    void drawAdd(draw::Canvas& c, const TrigListRef& ref, bool condition, int x, int w, int& y) {
        const wchar_t* label = condition ? L"Add a condition" : L"Add an action";
        RECT r{x, y, x + w, y + scale(30)};
        const bool over = hot(r);
        kitpaint::drawAddRowFace(c, r, label, over);
        lastText_ = over ? kAccent : kDim;
        gTP.adds.push_back({ref, r});
        y += scale(36);
    }

    void drawList(draw::Canvas& c, TriggerNode& t, const TrigListRef& ref, int x, int w, int& y, bool dimAll) {
        std::vector<TriggerCall>* calls = trigResolve(t, ref);
        if (!calls) return;
        const bool cond = trigListIsCondition(t, ref);
        for (size_t i = 0; i < calls->size(); ++i) {
            TriggerCall& call = (*calls)[i];
            const TrigBlockRef me{ref, int(i)};
            const bool on = trigSameBlock(gTP.sel, me);
            const bool moving = gTP.dragging && trigSameBlock(gTP.drag, me);
            const bool dim = dimAll || !call.enabled || moving;
            if (gTP.dragging && gTP.dropList == ref && gTP.dropIdx == int(i)) insertMark(c, x, w, y);
            const TriggerDef* d = TriggerDef::find(call.def);
            bool hasError = false;
            for (const TriggerIssue* is : trigIssuesOf(&call)) hasError |= is->level == TriggerIssue::Level::Error;
            const COLORREF border = on ? kTrigBlue : hasError ? kTrigRed : kBorder;
            const int textX = x + scale(26);
            // Measure the sentence (nothing drawn), then draw the block and the text.
            int hgt = scale(22);
            if (d) hgt = sentence(c, *d, call, textX + (d->lists.empty() ? 0 : scale(18)), 0, w - scale(130) - (d->lists.empty() ? 0 : scale(18)), me, true, false, false);
            const int padY = scale(9);  // room above and below the sentence
            RECT head{x, y, x + w, y + hgt + 2 * padY};
            const bool container = d && !d->lists.empty();
            roundRect(c, head, moving ? kCtlDown : on ? kSel : container ? trigContainerFill() : kCtl, container && !on && !hasError ? trigContainerLine() : border);
            if (on || trigSameBlock(gTP.hoverRow, me) || moving) grip(c, head, on);  // the drag handle: on the row under the mouse
            if (!dim) c.fill({head.left + scale(2), head.top + scale(5), head.left + scale(4), head.bottom - scale(5)}, trigCategoryColor(d));
            // On the row under the mouse, or selected: on/off, duplicate, delete.
            if ((on || trigSameBlock(gTP.hoverRow, me)) && !gTP.dragging) {
                const kitpaint::Ico glyphs[3] = {L"\xE7E8", UiIcon::Duplicate, UiIcon::Delete};
                const wchar_t* tips[3] = {call.enabled ? L"Turn off" : L"Turn on", L"Duplicate", L"Delete"};
                const kitpaint::Tone tones[3] = {kitpaint::Tone::Quiet, kitpaint::Tone::Normal, kitpaint::Tone::Danger};
                const RECT line{head.left, head.top + padY - scale(2), head.right - scale(4), head.top + padY + scale(24)};
                for (int k = 0; k < 3; ++k) {
                    const RECT br = toRect(kitpaint::rowActionRect(line, k, 3));
                    const draw::Color gc = kitpaint::drawIconFace(c, br, glyphs[k], tones[k], k == 0 && !call.enabled, hot(br));
                    if (glyphs[k].icon < 0) lastText_ = gc;
                    gTP.rowButtons.push_back({me, k, br});
                    gTP.tips.push_back({br, tips[k], kBorderHot});
                }
            }

            int sx = textX;
            if (container) {
                RECT arrow{textX, y + padY, textX + scale(16), y + padY + scale(22)};
                glyph(c, arrow, call.folded ? L"\xE76C" : L"\xE70D", lastText_, 9);  // chevron right / down, as the trees
                gTP.folds.push_back({me, arrow});
                sx += scale(18);
            }
            if (d) sentence(c, *d, call, sx, y + padY, w - (sx - x) - scale(100), me, dim, true, true);
            else text(c, {textX, y + padY, x + w, y + padY + scale(22)}, L"Unknown block: " + widen(call.def), kTrigRed, draw::kMiddle);
            const size_t lineAt = gTP.lines.size();
            gTP.lines.push_back({me, head, {x, y, x + scale(20), head.bottom}, head, container});
            if (const auto issues = trigIssuesOf(&call); !issues.empty() && !dim) {
                COLORREF tc;
                const std::wstring tt = trigIssueText(issues, tc);
                const int bx = head.right - scale(28) * 3 - scale(34);
                RECT badge{bx, head.top + padY - scale(1), bx + scale(24), head.top + padY + scale(23)};
                glyph(c, badge, tc == kTrigRed ? UiIcon::Error : UiIcon::Warning, tc);
                gTP.tips.push_back({badge, tt, tc});
            }
            y = head.bottom + scale(5);
            if (container && !call.folded) {
                const int ix = x + scale(28), iw = w - scale(28);
                for (size_t l = 0; l < d->lists.size(); ++l) {
                    const TriggerListDef& ld = d->lists[l];
                    // Labels say which list is which; an If's own conditions and a single "Do" need none.
                    const bool label = !((d->id == "act.if" || d->id == "act.ifthen") && l == 0) && d->lists.size() > 1;
                    if (label) {
                        text(c, {ix, y, ix + iw, y + scale(24)}, widen(ld.title), ld.condition ? trigConditionLabel() : ld.title == "Else" ? trigElseLabel() : kGood,
                             draw::kMiddle);
                        y += scale(26);
                    }
                    drawList(c, t, trigInside(me, int(l)), ix, iw, y, dim);
                }
                c.fill({x + scale(10), head.bottom, x + scale(12), y - scale(4)}, on ? kTrigBlue : trigContainerLine());
            } else if (container) {
                size_t n = 0;
                for (const auto& l : call.lists) n += l.size();
                text(c, {x + scale(26), y, x + w, y + scale(18)}, L"\x22EF " + std::to_wstring(n), kDim, draw::kMiddle);
                y += scale(20);
            }
            gTP.lines[lineAt].whole = {x, head.top, x + w, y};
            y += scale(4);
        }
        if (calls->empty()) {
            // An empty list: no box of its own. The Add row below it is where a dragged block drops (the same place).
            RECT empty{x, y, x + w, y + scale(30)};
            gTP.lines.push_back({{ref, -1}, empty, {}, empty, false});
            if (gTP.dragging && gTP.dropList == ref) insertMark(c, x, w, y);
        } else if (gTP.dragging && gTP.dropList == ref && gTP.dropIdx == int(calls->size())) {
            insertMark(c, x, w, y);
        }
        drawAdd(c, ref, cond, x, w, y);
    }

    // The name at the top of the pane: large (1.5 times the UI font); a click renames it.
    void drawTitle(draw::Canvas& c, const std::wstring& name, int x0, int w, int& y, bool editable = true) {
        const draw::Font of = c.setFont(c.font({draw::Face::Ui, -(uiLf_ * 3 / 2)}));
        const int tw = c.measure(name);
        RECT t{x0, y, std::min(x0 + w, x0 + tw), y + scale(34)};
        const bool over = editable && hot(t);
        text(c, t, name, over ? kTrigBlue : kText, draw::kMiddle | draw::kEllipsis);
        c.setFont(of);
        if (!editable) {
            y += scale(44);
            return;
        }
        RECT pen{t.right + scale(8), y, t.right + scale(26), y + scale(34)};
        glyph(c, pen, UiIcon::Edit, over ? kTrigBlue : kFaint);
        gTP.title = {t.left, t.top, pen.right, t.bottom};
        y += scale(44);
    }

    // The variable form: kind, size, starting value, description.
    void drawVariable(draw::Canvas& c, TriggerNode& v, int x0, int w, int& y) {
        auto row = [&](const std::wstring& label, const std::wstring& value, int field, bool link) {
            text(c, {x0, y, x0 + scale(150), y + scale(26)}, label, kDim, draw::kMiddle);
            const int vw = c.measure(value);
            RECT r{x0 + scale(150), y, x0 + scale(150) + vw, y + scale(26)};
            text(c, r, value, link ? kTrigBlue : kText, draw::kMiddle);
            if (link) {
                c.fill({r.left, r.bottom - scale(5), r.right, r.bottom - scale(4)}, kTrigBlue);
                gTP.varHits.push_back({field, r});
            }
            y += scale(30);
        };
        drawTitle(c, widen(v.name), x0, w, y);
        row(L"Kind", widen(TriggerVariable::kindLabel(v.variable.kind)), 0, true);
        if (v.variable.kind == TriggerVariable::Kind::Array) {
            const int size = triggerListSize(v.variable, gTE.set.variables());
            row(L"Size", (v.variable.sizeConstant.empty() ? L"" : widen(v.variable.sizeConstant) + L" = ") + std::to_wstring(size) + L" items (0 to " +
                             std::to_wstring(size - 1) + L")",
                1, true);
            row(L"Default value", v.variable.initial.empty() ? L"0" : widen(v.variable.initial), 2, true);
        }
        if (v.variable.kind == TriggerVariable::Kind::UnitGroup) row(L"Room for", std::to_wstring(std::clamp(v.variable.size, 1, 1700)) + L" units", 1, true);
        if (v.variable.kind == TriggerVariable::Kind::Constant) row(L"Value", v.variable.initial.empty() ? L"0" : widen(v.variable.initial), 2, true);
        if (v.variable.kind == TriggerVariable::Kind::Text) {
            row(L"Maximum length", std::to_wstring(std::clamp(v.variable.size, 1, TriggerVariable::kTextRoomMax)) + L" letters", 1, true);
            row(L"Text", v.variable.initial.empty() ? L"\x2014" : widen(v.variable.initial), 2, true);
        }
        if (v.variable.kind == TriggerVariable::Kind::Number || v.variable.kind == TriggerVariable::Kind::Flag)
            row(L"Default value", v.variable.initial.empty() ? L"0" : widen(v.variable.initial), 2, true);
        if (v.variable.kind == TriggerVariable::Kind::Unit)
            row(L"Default value", v.variable.initial.empty() ? L"\x2014" : trigPickedUnitName(v.variable.initial), 2, true);
        if (v.variable.kind == TriggerVariable::Kind::PerPlayer) {  // what it holds, then each player's default value (GH#108)
            const std::string& holds = v.variable.holds;
            row(L"Holds", holds == "UnitType" ? L"Unit types" : holds == "Location" ? L"Locations" : L"Numbers", 4, true);
            for (int p = 0; p < 8; ++p) {
                const std::string val = p < int(v.variable.playerValues.size()) ? v.variable.playerValues[size_t(p)] : "";
                const std::wstring shown = holds.empty() ? (val.empty() ? L"0" : widen(val))
                                           : val.empty() ? L"\x2014"
                                                         : trigValueText(holds, val);
                row(L"Player " + std::to_wstring(p + 1), shown, 10 + p, true);
            }
        }
        row(L"Description", v.comment.empty() ? L"\x2014" : widen(v.comment), 3, true);
        y += scale(8);
        for (const auto& i : gTE.issues)
            if (i.node == &v) {
                text(c, {x0, y, x0 + w, y + scale(20)}, widen(i.message), i.level == TriggerIssue::Level::Error ? kTrigRed : kTrigAmber, draw::kMiddle);
                y += scale(22);
            }
    }

    // Under the heavy trigger's cost: each fix with Apply. Returns the y under them.
    int drawCostFixes(draw::Canvas& c, const TriggerNode& t, int x0, int w, int y) {
        const int category = gTE.sel.empty() ? 2 : std::clamp(gTE.sel[0], 0, 2);
        const auto fixes = TriggerSet::costFixes(t, category);
        for (size_t k = 0; k < fixes.size() && k < 2; ++k) {
            const std::wstring apply = L"Apply";
            const int aw = c.measure(apply);
            RECT ar{x0 + scale(18), y, x0 + scale(18) + aw, y + scale(20)};
            const bool over = hot(ar);
            text(c, ar, apply, over ? kAccentBright : kAccent, draw::kMiddle);
            if (over) c.fill({ar.left, ar.bottom - scale(3), ar.right, ar.bottom - scale(2)}, kAccentBright);
            gTrigFixLinks.push_back({ar, fixes[k]});
            const std::wstring label = widen(fixes[k].text) + L" (about " + std::to_wstring(fixes[k].after) + L")";
            text(c, {ar.right + scale(10), y, x0 + w, y + scale(20)}, label, kDim, draw::kMiddle | draw::kEllipsis);
            gTP.tips.push_back({{ar.left, y, x0 + w, y + scale(20)}, label, kBorderHot});
            y += scale(22);
        }
        return y;
    }

    static RECT toRect(draw::Rect r) { return {r.left, r.top, r.right, r.bottom}; }

    void paintAt(draw::Canvas& c) {
        host_.layoutCheck();
        const RECT rc{0, 0, width(), height()};
        gTP.lines.clear(), gTP.params.clear(), gTP.folds.clear(), gTP.adds.clear(), gTP.varHits.clear(), gTP.rowButtons.clear(), gTP.tips.clear();
        gTP.title = {}, gTP.addButtons.clear();
        gTrigFixLinks.clear();
        if (gTE.project) {  // </> at the top right: the code pane
            gTP.codeToggle = {rc.right - scale(44), scale(10), rc.right - scale(14), scale(40)};
            glyph(c, gTP.codeToggle, UiIcon::Code, gDock.triggerCode ? kTrigBlue : kDim);
        }
        const int x0 = scale(20), w = std::max(scale(100), int(rc.right) - 2 * x0);
        int y = scale(14) - gTE.scroll;
        TriggerNode* node = trigSelectedNode();
        TriggerNode* t = trigSelected();
        if (node && node->kind == TriggerNode::Kind::Variable) {
            drawVariable(c, *node, x0, w, y);
        } else if (node && gTE.project && (node->kind == TriggerNode::Kind::Category || node->kind == TriggerNode::Kind::Folder)) {
            // A category or folder: what runs there, and buttons to add to it.
            const bool category = node->kind == TriggerNode::Kind::Category;
            const int titleY = y;
            drawTitle(c, widen(node->name), x0, w, y, !category);
            // What runs there: a tooltip on the title.
            static const wchar_t* about[] = {L"Variables, and triggers that a Run a trigger block runs", L"These triggers run once, when the map starts",
                                             L"These triggers run each trigger cycle while the game runs"};
            const int cat = gTE.sel.empty() ? 0 : std::clamp(gTE.sel[0], 0, 2);
            gTP.tips.push_back({{x0, titleY, x0 + w / 2, titleY + scale(34)}, category ? about[cat] : L"A folder in " + widen(gTE.set.roots[size_t(cat)].name), kBorderHot});
            bool templates = false;
            for (const auto& tp : triggerTemplates()) templates |= tp.category == cat;
            std::vector<std::pair<const wchar_t*, int>> buttons = {{L"New trigger", 0}};
            if (templates) buttons.push_back({L"From a template\x2026", 1});
            buttons.push_back({L"New folder", 2});
            if (cat == 0) buttons.push_back({L"New variable", 3});
            int x = x0;
            for (const auto& [label, action] : buttons) {
                RECT br{x, y, x + kitpaint::buttonWidth(c, label), y + scale(stardraft::kit::kButtonH)};
                kitpaint::drawButtonFace(c, br, label, action == 0 ? kitpaint::Tone::Primary : kitpaint::Tone::Normal,
                                         hot(br) ? kitpaint::ButtonState::Hot : kitpaint::ButtonState::Normal);
                lastText_ = action == 0 ? kAccent : kText;
                gTP.addButtons.push_back({br, action});
                x = br.right + scale(8);
            }
        } else if (!t) {
            text(c, {x0, scale(40), rc.right - x0, rc.bottom}, gTE.project ? L"" : L"Triggers belong to an EUD project", kDim, draw::kWrap);
        } else {
            drawTitle(c, widen(t->name), x0, w, y);
            // A heavy trigger says so; so do problems with the trigger as a whole.
            if (const int cost = TriggerSet::cost(*t); cost > TriggerSet::kHeavy) {
                text(c, {x0, y, x0 + w, y + scale(20)}, L"\x23F1 about " + std::to_wstring(cost) + L" actions each run", kTrigAmber, draw::kMiddle);
                y += scale(22);
                y = drawCostFixes(c, *t, x0, w, y);  // what makes it cheaper, with Apply
            }
            for (const auto& i : gTE.issues)
                if (i.node == t && !i.call) {
                    text(c, {x0, y, x0 + w, y + scale(20)}, widen(i.message), i.level == TriggerIssue::Level::Error ? kTrigRed : kTrigAmber, draw::kMiddle);
                    y += scale(22);
                }
            y += scale(6);
            static const wchar_t* titles[2] = {L"Conditions", L"Actions"};
            for (int sec = 0; sec < 2; ++sec) {
                RECT hr{x0, y, x0 + w, y + scale(28)};
                gTP.heading[sec] = hr;
                // A section heading (ui::sectionHeader's text and line) with its fold chevron.
                glyph(c, {hr.left, hr.top, hr.left + scale(16), hr.bottom}, gTP.collapsed[sec] ? L"\xE76C" : L"\xE70D", kDim, 9);
                text(c, {hr.left + scale(20), hr.top, hr.right, hr.bottom}, titles[sec], kText, draw::kMiddle);
                c.fill({x0, hr.bottom, x0 + w, hr.bottom + 1}, kBorder);
                y += scale(36);
                if (!gTP.collapsed[sec]) drawList(c, *t, {{}, sec}, x0, w, y, false);
                y += scale(14);
            }
        }
        gTE.contentH = y + gTE.scroll;
        // The problem under the mouse, at once (no tooltip delay): a box under the mark or value.
        {
            const draw::Point m = mousePt();
            const POINT cur{m.x, m.y};
            for (auto it = gTP.tips.rbegin(); it != gTP.tips.rend(); ++it) {
                if (!PtInRect(&it->r, cur)) continue;
                const int pad = scale(8), maxW = scale(420);
                const draw::Point sz = c.measureBox(it->text, maxW, draw::kWrap);
                const int bw = sz.x + 2 * pad, bh = sz.y + 2 * pad;
                int bx = std::clamp(int(it->r.left), scale(4), std::max(scale(4), int(rc.right) - bw - scale(4)));
                int by = it->r.bottom + scale(4);
                if (by + bh > rc.bottom) by = it->r.top - bh - scale(4);
                RECT box{bx, by, bx + bw, by + bh};
                roundRect(c, box, kCtl, it->color);
                text(c, {box.left + pad, box.top + pad, box.right - pad, box.bottom - pad}, it->text, kText, draw::kWrap);
                break;
            }
        }
        if (const int most = std::max(0, gTE.contentH - int(rc.bottom)); gTE.scroll > most)  // taller window: the bar hides, so no scroll is left
            gTE.scroll = most, invalidate();
    }
};

// --- The info strip: what the selected block does -----------------------------------------------------------------
class TrigInfoView final : public draw::View {
public:
    explicit TrigInfoView(TrigPaneHost& host) : host_(host) {}
    void paint(draw::Canvas& c) override {
        c.fill({0, 0, width(), 1}, kBorder);
        const draw::Rect r{scale(20), scale(10), width() - scale(20), height() - scale(6)};
        TriggerNode* t = trigSelected();
        const TriggerCall* call = t ? trigBlock(*t, gTP.sel) : nullptr;
        if (!call) return;
        const TriggerDef* d = TriggerDef::find(call->def);
        drawBlockDocs(c, d, r);
        host_.blockShown(call->def);  // the help page, when open, shows the selected block
        if (const TriggerProperty* pr = d ? triggerCallProperty(*d, call->args) : nullptr) {
            const TriggerType* vt = triggerType(pr->type);
            const std::wstring line = widen(pr->label) + L" \x2192 " + (vt ? widen(vt->label) : widen(pr->type)) +
                                      (pr->notes.empty() ? L"" : L"  \x00B7  " + widen(pr->notes));
            c.text({r.left, r.bottom - scale(22), r.right, r.bottom}, line, triggerTypeClear(pr->type) ? kDim : trigPurple(), draw::kEllipsis);
        }
    }

private:
    TrigPaneHost& host_;
};
