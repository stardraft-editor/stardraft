// The Classic triggers page's model (portable, core.inc; classic maps only): the sentence of a condition or an action as
// parts in order (its words, its value chips with the parameter each shows, and the EUD tag), and the variables of the
// blocks that hold an action. The page (classic_triggers.inc) paints the parts and makes the chips clickable; the
// program passes the names and the chip texts (they come from the open map).

// A picker's value that is a block's variable: kCtVariable plus the block's depth (a field that holds it, not a value).
constexpr uint32_t kCtVariable = 0xFFFF0000u;

// One part of a sentence: a run of words, a value chip (`param`: the index of the parameter it shows), or the EUD tag.
enum class CtPart { Word, Chip, Eud };
struct CtSegment {
    CtPart part = CtPart::Word;
    std::wstring text;
    int param = -1;
};

// What the sentence needs from the program: the names of the map's units, locations, strings and switches, and the text
// of a parameter's value in its chip (a choice's name, or "(EUD value)").
struct CtSentenceEnv {
    ClassicNames names;
    std::function<std::wstring(const ClassicParam&, uint32_t)> chipText;
};

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

// The sentence of a condition or action (`t` and `index` give an action's trigger and row: its blocks' variables; null
// for a condition). Each value is a chip; a sentence that starts with a value starts with a capital.
template <class E>
std::vector<CtSegment> ctSentenceParts(const E& e, int kind, const ClassicTrigger* t, int index, const CtSentenceEnv& env) {
    std::vector<CtSegment> out;
    const ClassicDef* def = kind ? classicActionDef(e.type) : classicConditionDef(e.type);
    auto word = [&out](std::wstring s) {
        if (!s.empty()) out.push_back({CtPart::Word, std::move(s), -1});
    };
    if constexpr (std::is_same_v<E, ClassicAction>) {
        if (e.type == kClassicBlockType) {  // a block: "For each unit in ..." (its chevron is painted by the list)
            word(widen(classicBatchText(*e.batch, env.names)));
            return out;
        }
    }
    std::vector<const ClassicBatch*> scope;
    if constexpr (std::is_same_v<E, ClassicAction>)
        if (kind == 1 && t) scope = ctScopeOf(*t, index);
    if (!def) {
        std::string s;
        if constexpr (std::is_same_v<E, ClassicAction>) s = classicActionText(e, env.names, &scope);
        else s = classicConditionText(e, env.names);
        for (char& ch : s)
            if (static_cast<unsigned char>(ch) < 32) ch = ' ';
        word(widen(s));
        return out;
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
        word(run);
        run.clear();
        for (int pi = 0; pi < int(def->params.size()); ++pi) {
            const ClassicParam& p = def->params[size_t(pi)];
            if (key != p.name) continue;
            const int vd = kind == 1 && t ? ctVarDepthAt(*t, index, p.field) : -1;
            std::wstring v = vd >= 0 && size_t(vd) < scope.size() && scope[size_t(vd)] ? widen(classicBatchVariableText(*scope[size_t(vd)]))
                                                                                       : env.chipText(p, classicValue(e, p.field));
            const bool first = std::none_of(out.begin(), out.end(), [](const CtSegment& g) { return !g.text.empty(); });
            if (first && !v.empty()) v[0] = towupper(v[0]);  // a sentence that starts with a value
            if (!v.empty()) out.push_back({CtPart::Chip, std::move(v), pi});
            break;
        }
    }
    word(run);
    if (classicEud(e)) out.push_back({CtPart::Eud, L"  EUD", -1});
    return out;
}

// The right-click menus of the page (classic_triggers.inc builds them with the program's menu and runs the command of the
// pick; the same commands as the rows' buttons and the keys). A text's tab is its shortcut. An item with id 0 is a separator.
enum : int { kCtmToggle = 1, kCtmDuplicate, kCtmDelete, kCtmCopy, kCtmPaste, kCtmIn, kCtmOut, kCtmBlock };
struct CtMenuItem {
    int id = 0;
    std::wstring text;
    bool enabled = true;
};
// A trigger's menu: turn on (when it is off) or turn off, Duplicate, Delete, a separator, Copy, Paste (`canPaste`: the
// clipboard holds triggers).
inline std::vector<CtMenuItem> ctTriggerMenuItems(bool off, bool canPaste) {
    return {{kCtmToggle, off ? L"Turn on" : L"Turn off"},
            {kCtmDuplicate, L"Duplicate\tCtrl+D"},
            {kCtmDelete, L"Delete\tDel"},
            {0, L""},
            {kCtmCopy, L"Copy\tCtrl+C"},
            {kCtmPaste, L"Paste\tCtrl+V", canPaste}};
}
// What an entry's menu depends on: a condition or an action, a block (its edit replaces the turn off), the turn off state,
// the moves (actions only), and whether the clipboard holds this kind of entry.
struct CtEntryMenuState {
    bool isAction = false;
    bool block = false;
    bool off = false;
    bool canMoveOut = false;
    bool canMoveIn = false;
    bool canPaste = false;
};
inline std::vector<CtMenuItem> ctEntryMenuItems(const CtEntryMenuState& s) {
    std::vector<CtMenuItem> items;
    if (s.block) items.push_back({kCtmBlock, L"Edit the block\x2026"});
    else items.push_back({kCtmToggle, s.off ? L"Turn on" : L"Turn off"});
    if (s.isAction) {
        items.push_back({kCtmOut, L"Move out\tCtrl+Left", s.canMoveOut});
        items.push_back({kCtmIn, L"Move in\tCtrl+Right", s.canMoveIn});
    }
    items.push_back({kCtmDuplicate, L"Duplicate\tCtrl+D"});
    items.push_back({kCtmDelete, L"Delete\tDel"});
    items.push_back({0, L""});
    items.push_back({kCtmCopy, L"Copy\tCtrl+C"});
    items.push_back({kCtmPaste, L"Paste\tCtrl+V", s.canPaste});
    return items;
}

// The list's model (the Triggers page on both systems): a trigger's line, its search text and the rows a search shows.
// `n` gives the names of the map (the page passes its own; the same text on Windows and SDL).

// A trigger's line in the list: its conditions, then (dim) its actions (`actions`: the actions' line).
inline std::wstring ctTriggerLineOf(const ClassicTrigger& t, bool actions, const ClassicNames& n) {
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

// The text a search matches, in lower case: the trigger's sentences (its conditions and actions, names of units,
// locations and switches) and the names of its players.
inline std::wstring ctSearchTextOf(const ClassicTrigger& t, const ClassicNames& n) {
    std::wstring s = ctTriggerLineOf(t, false, n) + L" " + ctTriggerLineOf(t, true, n);
    for (int g = 0; g < kClassicGroups; ++g)
        if (t.players[size_t(g)]) s += L" " + widen(classicPlayerName(uint32_t(g), n));
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// The words of a search (lower case, split at every wide space, as the stream's >> did).
inline std::vector<std::wstring> ctSearchWords(std::wstring q) {
    std::transform(q.begin(), q.end(), q.begin(), ::towlower);
    std::vector<std::wstring> words;
    std::wstring w;
    for (wchar_t c : q) {
        if (iswspace(c)) {
            if (!w.empty()) words.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) words.push_back(w);
    return words;
}

// The rows of the list: the indices of the triggers that run for `filter` (-1: every player) and have every word of
// the query, in the map's order.
inline std::vector<int> ctShownTriggers(const std::vector<ClassicTrigger>& triggers, int filter, const std::wstring& query,
                                        const ClassicNames& n) {
    const std::vector<std::wstring> words = ctSearchWords(query);
    std::vector<int> shown;
    for (int i = 0; i < int(triggers.size()); ++i) {
        const ClassicTrigger& t = triggers[size_t(i)];
        if (filter >= 0 && !t.runsFor(filter)) continue;
        if (!words.empty()) {
            const std::wstring hay = ctSearchTextOf(t, n);
            if (!std::all_of(words.begin(), words.end(), [&](const std::wstring& w) { return hay.find(w) != std::wstring::npos; })) continue;
        }
        shown.push_back(i);
    }
    return shown;
}
